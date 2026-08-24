from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import re

from .allocated_image import allocatedImageBytes, requireAllocatedImage
from .boot_layout import (
    OS_BOOT_LAYOUT_REFERENCE_DISK_SIZE_BYTES,
    OS_BOOT_LAYOUT_ROOTFS_START_BYTES,
)
from .errors import OsToolError
from .source_metrics import collectSourceMetrics
from .swap_image import (
    OS_SWAP_IMAGE_DATA_SIZE_BYTES,
    OS_SWAP_IMAGE_SIZE_BYTES,
)


OS_RELEASE_IDENTITY_SCHEMA_VERSION = 1
OS_RELEASE_IDENTITY_PROJECT_NAME = "x86_64_os_lab"
OS_RELEASE_IDENTITY_PROJECT_VERSION = "2.6.0"
OS_RELEASE_IDENTITY_ABI_VERSION = "2.6.0"
OS_RELEASE_IDENTITY_ABI_SYSTEM_CALL_COUNT = 105
OS_RELEASE_IDENTITY_ABI_LAST_ERROR = -60
OS_RELEASE_IDENTITY_ROOTFS_FORMAT_VERSION = 4
OS_RELEASE_IDENTITY_PRIMARY_MEMORY_MEBIBYTES = 4096
OS_RELEASE_IDENTITY_FIRMWARE_SIZE_BYTES = 131_072
OS_RELEASE_IDENTITY_BOOT_PREFIX_HASH_SIZE_BYTES = 4 * 1024 * 1024
OS_RELEASE_IDENTITY_SECTOR_SIZE_BYTES = 512
OS_RELEASE_IDENTITY_SOURCE_COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$")
OS_RELEASE_IDENTITY_SOURCE_TREE_DIRECTORIES = (
    "docs",
    "source",
    "tests",
    "tools",
)
OS_RELEASE_IDENTITY_SOURCE_TREE_ROOT_FILES = (
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
)
OS_RELEASE_IDENTITY_IGNORED_DIRECTORY_NAMES = frozenset(
    ("__pycache__", "build")
)
OS_RELEASE_IDENTITY_IGNORED_FILE_SUFFIXES = frozenset((".pyc", ".pyo"))


@dataclass(frozen=True)
class ReleaseIdentity:
    projectName: str
    projectVersion: str
    abiVersion: str
    abiSystemCallCount: int
    abiLastError: int
    rootfsFormatVersion: int
    primaryMemoryMebibytes: int
    bootDiskSizeBytes: int
    swapDataSizeBytes: int
    swapDiskSizeBytes: int


@dataclass(frozen=True)
class ReleaseArtifactIdentity:
    path: str
    logicalSizeBytes: int
    allocatedSizeBytes: int
    sparse: bool
    contentIdentity: dict[str, str]


def _readRequiredText(projectRoot: Path, relativePath: str) -> str:
    sourcePath = projectRoot / relativePath
    if not sourcePath.is_file():
        raise OsToolError(f"发布身份缺少文件：{relativePath}")
    return sourcePath.read_text(encoding="utf-8")


def _requirePattern(
    sourceText: str,
    pattern: str,
    expectedValue: str,
    description: str,
) -> None:
    match = re.search(pattern, sourceText, re.MULTILINE)
    if match is None or match.group(1) != expectedValue:
        observedValue = "缺失" if match is None else match.group(1)
        raise OsToolError(
            f"发布身份不一致：{description}={observedValue}，预期 {expectedValue}"
        )


def auditReleaseIdentity(projectRoot: Path) -> ReleaseIdentity:
    cmakeText = _readRequiredText(projectRoot, "CMakeLists.txt")
    abiText = _readRequiredText(
        projectRoot,
        "source/abi/include/os/abi/version.hpp",
    )
    rootfsText = _readRequiredText(
        projectRoot,
        "source/kernel/include/os/kernel/fs/root_file_system_format.hpp",
    )
    qemuText = _readRequiredText(projectRoot, "tools/os_tools/qemu_runner.py")
    kernelText = _readRequiredText(
        projectRoot,
        "source/kernel/src/core/kernel_main.cpp",
    )
    shellText = _readRequiredText(projectRoot, "source/user/src/shell.cpp")
    initText = _readRequiredText(projectRoot, "source/user/programs/init.cpp")
    argumentText = _readRequiredText(
        projectRoot,
        "source/user/programs/argument_probe.cpp",
    )
    readmeText = _readRequiredText(projectRoot, "README.md")
    _readRequiredText(projectRoot, "docs/releases/v2.6.md")

    _requirePattern(
        cmakeText,
        r"\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
        OS_RELEASE_IDENTITY_PROJECT_VERSION,
        "项目版本",
    )
    _requirePattern(
        cmakeText,
        r"set\(OS_DISK_IMAGE_SIZE_BYTES\s+([0-9]+)\)",
        str(OS_BOOT_LAYOUT_REFERENCE_DISK_SIZE_BYTES),
        "rootfs 磁盘字节数",
    )
    _requirePattern(
        cmakeText,
        r"set\(OS_SWAP_DISK_IMAGE_SIZE_BYTES\s+([0-9]+)\)",
        str(OS_SWAP_IMAGE_SIZE_BYTES),
        "交换盘字节数",
    )
    for constantName, expectedValue in (
        ("OS_ABI_VERSION_MAJOR", "2"),
        ("OS_ABI_VERSION_MINOR", "6"),
        ("OS_ABI_VERSION_PATCH", "0"),
        (
            "OS_ABI_SYSTEM_CALL_COUNT",
            str(OS_RELEASE_IDENTITY_ABI_SYSTEM_CALL_COUNT),
        ),
        (
            "OS_ABI_SYSTEM_CALL_LAST_NUMBER",
            str(OS_RELEASE_IDENTITY_ABI_SYSTEM_CALL_COUNT),
        ),
    ):
        _requirePattern(
            abiText,
            rf"\b{constantName}\s*=\s*([0-9]+)ULL;",
            expectedValue,
            constantName,
        )
    _requirePattern(
        abiText,
        r"\bOS_ABI_SYSTEM_CALL_FIRST_ERROR\s*=\s*(-[0-9]+)LL;",
        "-1",
        "ABI 首个错误码",
    )
    _requirePattern(
        abiText,
        r"\bOS_ABI_SYSTEM_CALL_LAST_ERROR\s*=\s*(-[0-9]+)LL;",
        str(OS_RELEASE_IDENTITY_ABI_LAST_ERROR),
        "ABI 最后错误码",
    )
    _requirePattern(
        rootfsText,
        r"\bOS_KERNEL_ROOTFS_FORMAT_VERSION\s*=\s*([0-9]+)ULL;",
        str(OS_RELEASE_IDENTITY_ROOTFS_FORMAT_VERSION),
        "rootfs 格式版本",
    )
    _requirePattern(
        qemuText,
        r"OS_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES\s*=\s*([0-9]+)\s*\*\s*1024",
        "4",
        "手机主规格 GiB",
    )
    requiredTexts = (
        (kernelText, "x86-64 OS v2.6 terminal ready", "Kernel banner"),
        (kernelText, "OS_STAGE=v2.6", "Kernel init 环境"),
        (shellText, "x86-64 OS Lab v2.6", "Shell banner"),
        (initText, "OS_STAGE=v2.6", "PID1 环境"),
        (argumentText, "OS_STAGE=v2.6", "参数探针环境"),
        (qemuText, "environment-v2.6", "QEMU 环境标记"),
        (readmeText, "v2.6 集成冻结与正式发布", "README 当前阶段"),
    )
    for sourceText, requiredText, description in requiredTexts:
        if requiredText not in sourceText:
            raise OsToolError(f"发布身份缺少 {description}：{requiredText}")

    return ReleaseIdentity(
        projectName=OS_RELEASE_IDENTITY_PROJECT_NAME,
        projectVersion=OS_RELEASE_IDENTITY_PROJECT_VERSION,
        abiVersion=OS_RELEASE_IDENTITY_ABI_VERSION,
        abiSystemCallCount=OS_RELEASE_IDENTITY_ABI_SYSTEM_CALL_COUNT,
        abiLastError=OS_RELEASE_IDENTITY_ABI_LAST_ERROR,
        rootfsFormatVersion=OS_RELEASE_IDENTITY_ROOTFS_FORMAT_VERSION,
        primaryMemoryMebibytes=OS_RELEASE_IDENTITY_PRIMARY_MEMORY_MEBIBYTES,
        bootDiskSizeBytes=OS_BOOT_LAYOUT_REFERENCE_DISK_SIZE_BYTES,
        swapDataSizeBytes=OS_SWAP_IMAGE_DATA_SIZE_BYTES,
        swapDiskSizeBytes=OS_SWAP_IMAGE_SIZE_BYTES,
    )


def _sha256Region(imagePath: Path, offsetBytes: int, lengthBytes: int) -> str:
    if offsetBytes < 0 or lengthBytes <= 0:
        raise OsToolError("发布产物哈希范围无效。")
    digest = hashlib.sha256()
    with imagePath.open("rb") as imageFile:
        imageFile.seek(offsetBytes)
        remainingBytes = lengthBytes
        while remainingBytes > 0:
            content = imageFile.read(min(1024 * 1024, remainingBytes))
            if not content:
                raise OsToolError(f"发布产物哈希范围被截断：{imagePath}")
            digest.update(content)
            remainingBytes -= len(content)
    return digest.hexdigest()


def calculateReleaseSourceTreeSha256(projectRoot: Path) -> str:
    sourcePaths: list[Path] = []
    for relativePath in OS_RELEASE_IDENTITY_SOURCE_TREE_ROOT_FILES:
        sourcePath = projectRoot / relativePath
        if not sourcePath.is_file():
            raise OsToolError(f"发布源码树缺少根文件：{relativePath}")
        sourcePaths.append(sourcePath)
    for directoryName in OS_RELEASE_IDENTITY_SOURCE_TREE_DIRECTORIES:
        sourceDirectory = projectRoot / directoryName
        if not sourceDirectory.is_dir():
            raise OsToolError(f"发布源码树缺少目录：{directoryName}")
        for sourcePath in sourceDirectory.rglob("*"):
            relativeParts = sourcePath.relative_to(sourceDirectory).parts
            if (
                sourcePath.is_file()
                and not any(
                    part in OS_RELEASE_IDENTITY_IGNORED_DIRECTORY_NAMES
                    for part in relativeParts[:-1]
                )
                and sourcePath.suffix not in OS_RELEASE_IDENTITY_IGNORED_FILE_SUFFIXES
            ):
                sourcePaths.append(sourcePath)
    digest = hashlib.sha256()
    for sourcePath in sorted(set(sourcePaths)):
        relativePath = sourcePath.relative_to(projectRoot).as_posix().encode("utf-8")
        fileSizeBytes = sourcePath.stat().st_size
        digest.update(len(relativePath).to_bytes(8, "little"))
        digest.update(relativePath)
        digest.update(fileSizeBytes.to_bytes(8, "little"))
        with sourcePath.open("rb") as sourceFile:
            while True:
                content = sourceFile.read(1024 * 1024)
                if not content:
                    break
                digest.update(content)
    return digest.hexdigest()


def _collectSmallArtifact(imagePath: Path) -> ReleaseArtifactIdentity:
    logicalSizeBytes = imagePath.stat().st_size
    return ReleaseArtifactIdentity(
        path=imagePath.name,
        logicalSizeBytes=logicalSizeBytes,
        allocatedSizeBytes=allocatedImageBytes(imagePath),
        sparse=allocatedImageBytes(imagePath) < logicalSizeBytes,
        contentIdentity={
            "sha256": _sha256Region(imagePath, 0, logicalSizeBytes),
        },
    )


def _collectBootDiskArtifact(imagePath: Path) -> ReleaseArtifactIdentity:
    logicalSizeBytes = imagePath.stat().st_size
    if logicalSizeBytes != OS_BOOT_LAYOUT_REFERENCE_DISK_SIZE_BYTES:
        raise OsToolError("发布 rootfs 镜像长度不符合 128 GiB 规格。")
    allocatedSize = allocatedImageBytes(imagePath)
    return ReleaseArtifactIdentity(
        path=imagePath.name,
        logicalSizeBytes=logicalSizeBytes,
        allocatedSizeBytes=allocatedSize,
        sparse=allocatedSize < logicalSizeBytes,
        contentIdentity={
            "boot_prefix_sha256": _sha256Region(
                imagePath,
                0,
                OS_RELEASE_IDENTITY_BOOT_PREFIX_HASH_SIZE_BYTES,
            ),
            "rootfs_superblock_sha256": _sha256Region(
                imagePath,
                OS_BOOT_LAYOUT_ROOTFS_START_BYTES,
                OS_RELEASE_IDENTITY_SECTOR_SIZE_BYTES,
            ),
            "last_sector_sha256": _sha256Region(
                imagePath,
                logicalSizeBytes - OS_RELEASE_IDENTITY_SECTOR_SIZE_BYTES,
                OS_RELEASE_IDENTITY_SECTOR_SIZE_BYTES,
            ),
        },
    )


def _collectSwapDiskArtifact(imagePath: Path) -> ReleaseArtifactIdentity:
    logicalSizeBytes = imagePath.stat().st_size
    if logicalSizeBytes != OS_SWAP_IMAGE_SIZE_BYTES:
        raise OsToolError("发布交换盘镜像长度不符合固定规格。")
    allocatedSize = allocatedImageBytes(imagePath)
    return ReleaseArtifactIdentity(
        path=imagePath.name,
        logicalSizeBytes=logicalSizeBytes,
        allocatedSizeBytes=allocatedSize,
        sparse=allocatedSize < logicalSizeBytes,
        contentIdentity={
            "superblock_sha256": _sha256Region(
                imagePath,
                0,
                OS_RELEASE_IDENTITY_SECTOR_SIZE_BYTES,
            ),
            "last_sector_sha256": _sha256Region(
                imagePath,
                logicalSizeBytes - OS_RELEASE_IDENTITY_SECTOR_SIZE_BYTES,
                OS_RELEASE_IDENTITY_SECTOR_SIZE_BYTES,
            ),
        },
    )


def writeReleaseManifest(
    projectRoot: Path,
    outputPath: Path,
    sourceCommit: str,
    firmwareImagePath: Path,
    kernelImagePath: Path,
    bootDiskImagePath: Path,
    swapDiskImagePath: Path,
    requireAllocatedStorage: bool = True,
) -> None:
    if OS_RELEASE_IDENTITY_SOURCE_COMMIT_PATTERN.fullmatch(sourceCommit) is None:
        raise OsToolError("发布主仓 SHA 必须是 40 位小写十六进制。")
    identity = auditReleaseIdentity(projectRoot)
    for artifactPath, description in (
        (firmwareImagePath, "ROM"),
        (kernelImagePath, "Kernel"),
        (bootDiskImagePath, "rootfs 磁盘"),
        (swapDiskImagePath, "交换盘"),
    ):
        if not artifactPath.is_file():
            raise OsToolError(f"发布产物不存在：{description}={artifactPath}")
    if firmwareImagePath.stat().st_size != OS_RELEASE_IDENTITY_FIRMWARE_SIZE_BYTES:
        raise OsToolError("发布 ROM 长度不是 128 KiB。")
    if kernelImagePath.stat().st_size <= 0:
        raise OsToolError("发布 Kernel 载荷为空。")
    if requireAllocatedStorage:
        requireAllocatedImage(bootDiskImagePath)
        requireAllocatedImage(swapDiskImagePath)
    metrics = collectSourceMetrics(projectRoot)
    manifest = {
        "schema_version": OS_RELEASE_IDENTITY_SCHEMA_VERSION,
        "source_commit": sourceCommit,
        "source_tree_sha256": calculateReleaseSourceTreeSha256(projectRoot),
        "identity": asdict(identity),
        "source_metrics": asdict(metrics),
        "artifacts": {
            "firmware": asdict(_collectSmallArtifact(firmwareImagePath)),
            "kernel": asdict(_collectSmallArtifact(kernelImagePath)),
            "boot_disk": asdict(_collectBootDiskArtifact(bootDiskImagePath)),
            "swap_disk": asdict(_collectSwapDiskArtifact(swapDiskImagePath)),
        },
    }
    outputPath.parent.mkdir(parents=True, exist_ok=True)
    outputPath.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
