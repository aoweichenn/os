import tempfile
from pathlib import Path
import unittest

from tools.os_tools.source_metrics import (
    collectSourceMetrics,
    countAssemblyCodeLines,
    countCppCodeLines,
    renderLatexSourceMetrics,
)


class SourceMetricsToolTests(unittest.TestCase):
    def testCountsCppCodeWithoutBlankOrCommentOnlyLines(self) -> None:
        sourceText = """
// 纯注释
std::uint64_t value = 1; // 行尾注释
/*
 * 块注释
 */
return value;
"""

        self.assertEqual(countCppCodeLines(sourceText), 2)

    def testCountsCodeAroundInlineBlockComments(self) -> None:
        sourceText = """
std::uint64_t first = 1; /* 注释
仍是注释 */ std::uint64_t second = 2;
"""

        self.assertEqual(countCppCodeLines(sourceText), 2)

    def testCountsAssemblyCodeWithoutCommentOnlyLines(self) -> None:
        sourceText = """
; 纯注释
bits 16
mov ax, bx ; 行尾注释
"""

        self.assertEqual(countAssemblyCodeLines(sourceText), 2)

    def testCollectsOnlyProductionSourceExtensions(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            projectRoot = Path(temporaryDirectory)
            sourceDirectory = projectRoot / "source" / "module"
            sourceDirectory.mkdir(parents=True)
            (sourceDirectory / "entry.asm").write_text(
                "bits 16\n; 注释\nhlt\n",
                encoding="utf-8",
            )
            (sourceDirectory / "model.cpp").write_text(
                "// 注释\nreturn;\n",
                encoding="utf-8",
            )
            (sourceDirectory / "CMakeLists.txt").write_text(
                "add_library(example)\n",
                encoding="utf-8",
            )

            sourceMetrics = collectSourceMetrics(projectRoot)

            self.assertEqual(sourceMetrics.fileCount, 2)
            self.assertEqual(sourceMetrics.physicalLineCount, 5)
            self.assertEqual(sourceMetrics.codeLineCount, 3)
            self.assertEqual(sourceMetrics.cppCodeLineCount, 1)
            self.assertEqual(sourceMetrics.assemblyCodeLineCount, 2)

    def testRendersLatexCommands(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            projectRoot = Path(temporaryDirectory)
            sourceDirectory = projectRoot / "source"
            sourceDirectory.mkdir()
            (sourceDirectory / "entry.asm").write_text(
                "bits 16\nhlt\n",
                encoding="utf-8",
            )

            renderedMetrics = renderLatexSourceMetrics(
                collectSourceMetrics(projectRoot)
            )

            self.assertIn(
                "\\newcommand{\\OsProductionCodeLineCount}{2}",
                renderedMetrics,
            )


if __name__ == "__main__":
    unittest.main()
