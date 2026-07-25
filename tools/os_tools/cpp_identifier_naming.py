from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys
from typing import Sequence


OS_CPP_IDENTIFIER_NAMING_SOURCE_DIRECTORIES = ("source", "tests")
OS_CPP_IDENTIFIER_NAMING_FILE_SUFFIXES = frozenset({".cpp", ".hpp", ".tpp"})
OS_CPP_IDENTIFIER_NAMING_NAMESPACE_DECLARATION_PATTERN = re.compile(
    r"\bnamespace\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)"
)
OS_CPP_IDENTIFIER_NAMING_NAMESPACE_WORD_PATTERN = re.compile(r"^[a-z]+$")
OS_CPP_IDENTIFIER_NAMING_RAW_STRING_PREFIXES = (
    'u8R"',
    'uR"',
    'UR"',
    'LR"',
    'R"',
)
OS_CPP_IDENTIFIER_NAMING_RAW_DELIMITER_MAXIMUM_LENGTH = 16
OS_CPP_IDENTIFIER_NAMING_CLANG_TIDY_CHECK = "readability-identifier-naming"


@dataclass(frozen=True)
class NamespaceNamingViolation:
    path: Path
    line_number: int
    namespace_level: str


def _mask_range_preserving_lines(
    characters: list[str],
    begin_index: int,
    end_index: int,
) -> None:
    for character_index in range(begin_index, end_index):
        if characters[character_index] not in ("\n", "\r"):
            characters[character_index] = " "


def _find_raw_string_end(source_text: str, begin_index: int) -> int | None:
    matched_prefix = next(
        (
            prefix
            for prefix in OS_CPP_IDENTIFIER_NAMING_RAW_STRING_PREFIXES
            if source_text.startswith(prefix, begin_index)
        ),
        None,
    )
    if matched_prefix is None:
        return None

    delimiter_begin_index = begin_index + len(matched_prefix)
    opening_parenthesis_index = source_text.find(
        "(",
        delimiter_begin_index,
        delimiter_begin_index
        + OS_CPP_IDENTIFIER_NAMING_RAW_DELIMITER_MAXIMUM_LENGTH
        + 1,
    )
    if opening_parenthesis_index < 0:
        return None

    delimiter = source_text[
        delimiter_begin_index:opening_parenthesis_index
    ]
    if any(
        character.isspace() or character in ("(", ")", "\\")
        for character in delimiter
    ):
        return None

    terminator = ")" + delimiter + '"'
    terminator_index = source_text.find(
        terminator,
        opening_parenthesis_index + 1,
    )
    if terminator_index < 0:
        return len(source_text)
    return terminator_index + len(terminator)


def maskCppCommentsAndLiterals(source_text: str) -> str:
    masked_characters = list(source_text)
    source_size = len(source_text)
    character_index = 0

    while character_index < source_size:
        raw_string_end_index = _find_raw_string_end(
            source_text,
            character_index,
        )
        if raw_string_end_index is not None:
            _mask_range_preserving_lines(
                masked_characters,
                character_index,
                raw_string_end_index,
            )
            character_index = raw_string_end_index
            continue

        if source_text.startswith("//", character_index):
            comment_end_index = source_text.find("\n", character_index + 2)
            if comment_end_index < 0:
                comment_end_index = source_size
            _mask_range_preserving_lines(
                masked_characters,
                character_index,
                comment_end_index,
            )
            character_index = comment_end_index
            continue

        if source_text.startswith("/*", character_index):
            terminator_index = source_text.find("*/", character_index + 2)
            comment_end_index = (
                source_size
                if terminator_index < 0
                else terminator_index + 2
            )
            _mask_range_preserving_lines(
                masked_characters,
                character_index,
                comment_end_index,
            )
            character_index = comment_end_index
            continue

        if source_text[character_index] in ('"', "'"):
            quote = source_text[character_index]
            literal_end_index = character_index + 1
            while literal_end_index < source_size:
                if source_text[literal_end_index] == "\\":
                    literal_end_index = min(
                        source_size,
                        literal_end_index + 2,
                    )
                    continue
                if source_text[literal_end_index] == quote:
                    literal_end_index += 1
                    break
                literal_end_index += 1
            _mask_range_preserving_lines(
                masked_characters,
                character_index,
                literal_end_index,
            )
            character_index = literal_end_index
            continue

        character_index += 1

    return "".join(masked_characters)


def findNamespaceNamingViolations(
    source_path: Path,
    source_text: str,
) -> list[NamespaceNamingViolation]:
    masked_source_text = maskCppCommentsAndLiterals(source_text)
    violations: list[NamespaceNamingViolation] = []

    for declaration_match in (
        OS_CPP_IDENTIFIER_NAMING_NAMESPACE_DECLARATION_PATTERN.finditer(
            masked_source_text
        )
    ):
        qualified_name = declaration_match.group("name")
        line_number = (
            masked_source_text.count(
                "\n",
                0,
                declaration_match.start("name"),
            )
            + 1
        )
        for namespace_level in qualified_name.split("::"):
            if (
                OS_CPP_IDENTIFIER_NAMING_NAMESPACE_WORD_PATTERN.fullmatch(
                    namespace_level
                )
                is None
            ):
                violations.append(
                    NamespaceNamingViolation(
                        path=source_path,
                        line_number=line_number,
                        namespace_level=namespace_level,
                    )
                )

    return violations


def collectCppSourcePaths(project_root: Path) -> list[Path]:
    source_paths: list[Path] = []
    for source_directory_name in (
        OS_CPP_IDENTIFIER_NAMING_SOURCE_DIRECTORIES
    ):
        source_directory = project_root / source_directory_name
        source_paths.extend(
            path
            for path in source_directory.rglob("*")
            if path.is_file()
            and path.suffix in OS_CPP_IDENTIFIER_NAMING_FILE_SUFFIXES
        )
    return sorted(source_paths)


def runIdentifierNamingAudit(
    *,
    project_root: Path,
    build_directory: Path,
    run_clang_tidy_path: Path,
    parallel_job_count: int,
) -> int:
    source_paths = collectCppSourcePaths(project_root)
    namespace_violations: list[NamespaceNamingViolation] = []
    for source_path in source_paths:
        namespace_violations.extend(
            findNamespaceNamingViolations(
                source_path.relative_to(project_root),
                source_path.read_text(encoding="utf-8"),
            )
        )

    if namespace_violations:
        for violation in namespace_violations:
            print(
                f"{violation.path}:{violation.line_number}: "
                f"命名空间层级 {violation.namespace_level!r} "
                "不是单个小写英文单词。",
                file=sys.stderr,
            )
        return 1

    clang_tidy_result = subprocess.run(
        [
            str(run_clang_tidy_path),
            "-p",
            str(build_directory),
            "-j",
            str(parallel_job_count),
            "-quiet",
            "-extra-arg=-Wno-error",
            (
                "-warnings-as-errors="
                + OS_CPP_IDENTIFIER_NAMING_CLANG_TIDY_CHECK
            ),
        ],
        cwd=project_root,
        check=False,
    )
    if clang_tidy_result.returncode != 0:
        return clang_tidy_result.returncode

    print(
        "C++ 标识符检查通过："
        f"{len(source_paths)} 个文件，命名空间层级均为单个小写英文单词，"
        "Clang-Tidy AST 零诊断。"
    )
    return 0


def parseArguments(arguments: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="检查 C++ 标识符和命名空间命名规则。"
    )
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--run-clang-tidy", type=Path, required=True)
    parser.add_argument("--jobs", type=int, required=True)
    return parser.parse_args(arguments)


def main(arguments: Sequence[str] | None = None) -> int:
    parsed_arguments = parseArguments(
        sys.argv[1:] if arguments is None else arguments
    )
    if parsed_arguments.jobs <= 0:
        print("--jobs 必须大于零。", file=sys.stderr)
        return 2

    return runIdentifierNamingAudit(
        project_root=parsed_arguments.project_root.resolve(),
        build_directory=parsed_arguments.build_directory.resolve(),
        run_clang_tidy_path=parsed_arguments.run_clang_tidy.resolve(),
        parallel_job_count=parsed_arguments.jobs,
    )


if __name__ == "__main__":
    raise SystemExit(main())
