import { cp, mkdir, rm } from "node:fs/promises";
import path from "node:path";

const projectRootPath = process.cwd();
const staticExportPath = path.join(projectRootPath, "out");
const workerOutputPath = path.join(projectRootPath, ".open-next");
const workerAssetsPath = path.join(workerOutputPath, "assets");
const workerSourcePath = path.join(projectRootPath, "site", "worker.mjs");

await rm(workerOutputPath, { recursive: true, force: true });
await mkdir(workerAssetsPath, { recursive: true });
await cp(staticExportPath, workerAssetsPath, { recursive: true });
await cp(workerSourcePath, path.join(workerOutputPath, "worker.js"));

console.log("静态站点 Worker 已生成到 .open-next/。");
