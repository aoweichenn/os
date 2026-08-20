import json
from pathlib import Path
import shutil
import tempfile
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.release_identity import (
    OS_RELEASE_IDENTITY_ABI_SYSTEM_CALL_COUNT,
    OS_RELEASE_IDENTITY_PROJECT_VERSION,
    OS_RELEASE_IDENTITY_ROOTFS_FORMAT_VERSION,
    auditReleaseIdentity,
    calculateReleaseSourceTreeSha256,
    writeReleaseManifest,
)
from tools.os_tools.swap_image import writeSwapImage


OS_TEST_RELEASE_IDENTITY_PROJECT_ROOT = Path(__file__).resolve().parents[2]
OS_TEST_RELEASE_IDENTITY_SOURCE_COMMIT = "a" * 40
OS_TEST_RELEASE_IDENTITY_FIRMWARE_SIZE_BYTES = 131_072
OS_TEST_RELEASE_IDENTITY_BOOT_DISK_SIZE_BYTES = 128 * 1024 * 1024 * 1024
OS_TEST_RELEASE_IDENTITY_ROOTFS_START_BYTES = 32_768 * 512
OS_TEST_RELEASE_IDENTITY_REQUIRED_PATHS = (
    "CMakeLists.txt",
    "README.md",
    "docs/releases/v2.6.md",
    "source/abi/include/os/abi/version.hpp",
    "source/kernel/include/os/kernel/fs/root_file_system_format.hpp",
    "source/kernel/src/core/kernel_main.cpp",
    "source/user/programs/argument_probe.cpp",
    "source/user/programs/init.cpp",
    "source/user/src/shell.cpp",
    "tools/os_tools/qemu_runner.py",
)


class ReleaseIdentityToolTests(unittest.TestCase):
    def copyIdentityFixture(self, destinationRoot: Path) -> None:
        for relativePath in OS_TEST_RELEASE_IDENTITY_REQUIRED_PATHS:
            sourcePath = OS_TEST_RELEASE_IDENTITY_PROJECT_ROOT / relativePath
            destinationPath = destinationRoot / relativePath
            destinationPath.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(sourcePath, destinationPath)

    def testAuditsFrozenSourceIdentity(self) -> None:
        identity = auditReleaseIdentity(OS_TEST_RELEASE_IDENTITY_PROJECT_ROOT)

        self.assertEqual(identity.projectVersion, OS_RELEASE_IDENTITY_PROJECT_VERSION)
        self.assertEqual(
            identity.abiSystemCallCount,
            OS_RELEASE_IDENTITY_ABI_SYSTEM_CALL_COUNT,
        )
        self.assertEqual(
            identity.rootfsFormatVersion,
            OS_RELEASE_IDENTITY_ROOTFS_FORMAT_VERSION,
        )
        self.assertEqual(identity.primaryMemoryMebibytes, 4096)
        self.assertEqual(
            len(calculateReleaseSourceTreeSha256(OS_TEST_RELEASE_IDENTITY_PROJECT_ROOT)),
            64,
        )

    def testWritesStructuredArtifactManifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryPath = Path(temporaryDirectory)
            firmwarePath = temporaryPath / "firmware.bin"
            kernelPath = temporaryPath / "kernel.payload.elf"
            bootDiskPath = temporaryPath / "boot_disk.img"
            swapDiskPath = temporaryPath / "swap_disk.img"
            manifestPath = temporaryPath / "project_release.json"
            firmwarePath.write_bytes(bytes(OS_TEST_RELEASE_IDENTITY_FIRMWARE_SIZE_BYTES))
            kernelPath.write_bytes(b"kernel-release-identity")
            with bootDiskPath.open("wb") as bootDisk:
                bootDisk.write(b"boot-release-identity")
                bootDisk.seek(OS_TEST_RELEASE_IDENTITY_ROOTFS_START_BYTES)
                bootDisk.write(b"rootfs-release-identity")
                bootDisk.truncate(OS_TEST_RELEASE_IDENTITY_BOOT_DISK_SIZE_BYTES)
            writeSwapImage(swapDiskPath)

            writeReleaseManifest(
                OS_TEST_RELEASE_IDENTITY_PROJECT_ROOT,
                manifestPath,
                OS_TEST_RELEASE_IDENTITY_SOURCE_COMMIT,
                firmwarePath,
                kernelPath,
                bootDiskPath,
                swapDiskPath,
                requireAllocatedStorage=False,
            )

            manifest = json.loads(manifestPath.read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema_version"], 1)
            self.assertEqual(
                manifest["source_commit"],
                OS_TEST_RELEASE_IDENTITY_SOURCE_COMMIT,
            )
            self.assertEqual(len(manifest["source_tree_sha256"]), 64)
            self.assertEqual(
                manifest["identity"]["projectVersion"],
                OS_RELEASE_IDENTITY_PROJECT_VERSION,
            )
            self.assertTrue(manifest["artifacts"]["boot_disk"]["sparse"])
            self.assertTrue(manifest["artifacts"]["swap_disk"]["sparse"])
            self.assertEqual(
                len(manifest["artifacts"]["firmware"]["contentIdentity"]["sha256"]),
                64,
            )

    def testRejectsNonCanonicalCommitIdentity(self) -> None:
        with self.assertRaises(OsToolError):
            writeReleaseManifest(
                OS_TEST_RELEASE_IDENTITY_PROJECT_ROOT,
                Path("unused.json"),
                "FFE9359",
                Path("missing-firmware"),
                Path("missing-kernel"),
                Path("missing-boot"),
                Path("missing-swap"),
            )

    def testRejectsPartiallyUpdatedProjectVersion(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            projectRoot = Path(temporaryDirectory)
            self.copyIdentityFixture(projectRoot)
            cmakePath = projectRoot / "CMakeLists.txt"
            cmakePath.write_text(
                cmakePath.read_text(encoding="utf-8").replace(
                    "VERSION 2.6.0",
                    "VERSION 9.9.9",
                ),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(OsToolError, "项目版本"):
                auditReleaseIdentity(projectRoot)
