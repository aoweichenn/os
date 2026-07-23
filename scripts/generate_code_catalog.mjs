import { mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import path from "node:path";

const OS_CODE_CATALOG_PROJECT_ROOT = process.cwd();
const OS_CODE_CATALOG_OUTPUT_DIRECTORY = path.join(
  OS_CODE_CATALOG_PROJECT_ROOT,
  "generated",
);
const OS_CODE_CATALOG_OUTPUT_PATH = path.join(
  OS_CODE_CATALOG_OUTPUT_DIRECTORY,
  "code_catalog.json",
);
const OS_CODE_CATALOG_MAXIMUM_FILE_SIZE_BYTES = 200_000;
const OS_CODE_CATALOG_ROOT_DIRECTORIES = [
  ".github",
  "app",
  "components",
  "docs",
  "lib",
  "scripts",
  "site",
  "source",
  "tests",
];
const OS_CODE_CATALOG_ROOT_FILES = [
  ".clang-format",
  ".editorconfig",
  ".gitignore",
  "CMakeLists.txt",
  "CMakePresets.json",
  "README.md",
  "next.config.ts",
  "package.json",
  "tsconfig.json",
  "wrangler.jsonc",
];
const OS_CODE_CATALOG_SUPPORTED_EXTENSIONS = new Set([
  ".asm",
  ".cmake",
  ".cpp",
  ".css",
  ".hpp",
  ".inc",
  ".ini",
  ".json",
  ".jsonc",
  ".ld",
  ".md",
  ".mjs",
  ".sh",
  ".svg",
  ".tpp",
  ".ts",
  ".tsx",
  ".yaml",
  ".yml",
]);
const OS_CODE_CATALOG_LANGUAGE_BY_EXTENSION = new Map([
  [".asm", "asm"],
  [".cmake", "cmake"],
  [".cpp", "cpp"],
  [".css", "css"],
  [".hpp", "cpp"],
  [".inc", "asm"],
  [".ini", "ini"],
  [".json", "json"],
  [".jsonc", "jsonc"],
  [".ld", "text"],
  [".md", "markdown"],
  [".mjs", "javascript"],
  [".sh", "bash"],
  [".svg", "xml"],
  [".tpp", "cpp"],
  [".ts", "typescript"],
  [".tsx", "tsx"],
  [".yaml", "yaml"],
  [".yml", "yaml"],
]);
const OS_CODE_CATALOG_LANGUAGE_BY_FILE_NAME = new Map([
  [".clang-format", "yaml"],
  [".editorconfig", "ini"],
  [".gitignore", "text"],
  ["CMakeLists.txt", "cmake"],
]);

function normalizeRepositoryPath(filePath) {
  return filePath.split(path.sep).join("/");
}

function countSourceLines(content) {
  if (content.length === 0) {
    return 0;
  }

  const lines = content.split(/\r?\n/);
  if (lines.at(-1) === "") {
    lines.pop();
  }

  return lines.length;
}

function resolveLanguage(repositoryPath) {
  const fileName = path.basename(repositoryPath);
  const fileNameLanguage = OS_CODE_CATALOG_LANGUAGE_BY_FILE_NAME.get(fileName);

  if (fileNameLanguage) {
    return fileNameLanguage;
  }

  return (
    OS_CODE_CATALOG_LANGUAGE_BY_EXTENSION.get(path.extname(fileName)) ??
    "text"
  );
}

function isSupportedFile(repositoryPath) {
  const fileName = path.basename(repositoryPath);
  return (
    OS_CODE_CATALOG_LANGUAGE_BY_FILE_NAME.has(fileName) ||
    OS_CODE_CATALOG_SUPPORTED_EXTENSIONS.has(path.extname(fileName))
  );
}

async function collectDirectoryFiles(relativeDirectoryPath) {
  const absoluteDirectoryPath = path.join(
    OS_CODE_CATALOG_PROJECT_ROOT,
    relativeDirectoryPath,
  );
  const directoryEntries = await readdir(absoluteDirectoryPath, {
    withFileTypes: true,
  });
  const collectedPaths = [];

  for (const directoryEntry of directoryEntries) {
    const relativeEntryPath = path.join(
      relativeDirectoryPath,
      directoryEntry.name,
    );

    if (directoryEntry.isDirectory()) {
      collectedPaths.push(
        ...(await collectDirectoryFiles(relativeEntryPath)),
      );
    } else if (
      directoryEntry.isFile() &&
      isSupportedFile(relativeEntryPath)
    ) {
      collectedPaths.push(relativeEntryPath);
    }
  }

  return collectedPaths;
}

async function readCatalogFile(relativeFilePath) {
  const absoluteFilePath = path.join(
    OS_CODE_CATALOG_PROJECT_ROOT,
    relativeFilePath,
  );
  const content = await readFile(absoluteFilePath, "utf8");
  const sizeBytes = Buffer.byteLength(content, "utf8");

  if (sizeBytes > OS_CODE_CATALOG_MAXIMUM_FILE_SIZE_BYTES) {
    return undefined;
  }

  const repositoryPath = normalizeRepositoryPath(relativeFilePath);

  return {
    path: repositoryPath,
    name: path.basename(repositoryPath),
    directory: path.posix.dirname(repositoryPath),
    language: resolveLanguage(repositoryPath),
    lineCount: countSourceLines(content),
    sizeBytes,
    content,
  };
}

const sourceFilePaths = [...OS_CODE_CATALOG_ROOT_FILES];

for (const rootDirectory of OS_CODE_CATALOG_ROOT_DIRECTORIES) {
  sourceFilePaths.push(...(await collectDirectoryFiles(rootDirectory)));
}

const sourceFiles = (
  await Promise.all(
    sourceFilePaths
      .sort((leftPath, rightPath) => leftPath.localeCompare(rightPath))
      .map((filePath) => readCatalogFile(filePath)),
  )
).filter((sourceFile) => sourceFile !== undefined);

await mkdir(OS_CODE_CATALOG_OUTPUT_DIRECTORY, { recursive: true });
await writeFile(
  OS_CODE_CATALOG_OUTPUT_PATH,
  `${JSON.stringify({ files: sourceFiles }, null, 2)}\n`,
  "utf8",
);

const totalLineCount = sourceFiles.reduce(
  (lineCount, sourceFile) => lineCount + sourceFile.lineCount,
  0,
);

console.log(
  `代码目录已生成：${sourceFiles.length} 个文件，${totalLineCount} 行。`,
);
