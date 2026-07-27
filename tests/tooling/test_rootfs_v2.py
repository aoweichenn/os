from pathlib import Path
import tempfile
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.rootfs_v2 import (
    OS_ROOTFS_V2_CAPACITY_IMAGE_SIZE_BYTES,
    OS_ROOTFS_V2_CORRUPTION_KINDS,
    OS_ROOTFS_V2_BLOCK_SIZE_BYTES,
    OS_ROOTFS_V2_DIRECT_BLOCK_COUNT,
    OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES,
    OS_ROOTFS_V2_REGION_SIZE_BYTES,
    RootfsV2InstallFile,
    corruptRootfsV2,
    formatRootfsV2,
    installRootfsV2Files,
    inspectRootfsV2,
    inspectionAsJson,
    readRootfsV2File,
)
from tools.os_tools.sparse_image import copySparseImage


OS_TEST_ROOTFS_MAXIMUM_ALLOCATED_IMAGE_BYTES = 16 * 1024 * 1024
OS_TEST_ROOTFS_SPARSE_TAIL = b"TAIL"
OS_TEST_ROOTFS_INSTALL_PATH = "/sbin/init"
OS_TEST_ROOTFS_INSTALL_SECOND_PATH = "/bin/probe"
OS_TEST_ROOTFS_INSTALL_PATTERN_MODULUS = 251
OS_TEST_ROOTFS_INSTALL_INDIRECT_EXTRA_BYTES = 37


class RootfsV2ToolTests(unittest.TestCase):
    def testFormatsSparseImageAndInspectsEmptyRoot(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "rootfs.img"
            formatRootfsV2(imagePath, createImage=True)

            inspection = inspectRootfsV2(imagePath)

            self.assertEqual(
                imagePath.stat().st_size,
                OS_ROOTFS_V2_CAPACITY_IMAGE_SIZE_BYTES,
            )
            self.assertLess(
                imagePath.stat().st_blocks * 512,
                OS_TEST_ROOTFS_MAXIMUM_ALLOCATED_IMAGE_BYTES,
            )
            self.assertEqual(inspection.reachableInodeCount, 1)
            self.assertEqual(inspection.directoryCount, 1)
            self.assertEqual(inspection.regularFileCount, 0)
            self.assertEqual(inspection.allocatedDataBlockCount, 0)
            self.assertEqual(inspection.allocatedMetadataBlockCount, 0)
            self.assertIn(
                f'"rootfs_size_bytes": {OS_ROOTFS_V2_REGION_SIZE_BYTES}',
                inspectionAsJson(inspection),
            )
            self.assertIn(
                f'"maximum_file_size_bytes": '
                f"{OS_ROOTFS_V2_MAXIMUM_FILE_SIZE_BYTES}",
                inspectionAsJson(inspection),
            )

    def testRefusesImplicitOverwrite(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "rootfs.img"
            formatRootfsV2(imagePath, createImage=True)

            with self.assertRaises(OsToolError):
                formatRootfsV2(imagePath)

            formatRootfsV2(imagePath, force=True)
            self.assertEqual(
                inspectRootfsV2(imagePath).reachableInodeCount,
                1,
            )

    def testInstallsNestedFilesAndReadsIndirectBlocks(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryPath = Path(temporaryDirectory)
            imagePath = temporaryPath / "rootfs.img"
            initPath = temporaryPath / "init.elf"
            probePath = temporaryPath / "probe.elf"
            payloadSizeBytes = (
                OS_ROOTFS_V2_DIRECT_BLOCK_COUNT
                * OS_ROOTFS_V2_BLOCK_SIZE_BYTES
                + OS_TEST_ROOTFS_INSTALL_INDIRECT_EXTRA_BYTES
            )
            initPayload = bytes(
                byteIndex % OS_TEST_ROOTFS_INSTALL_PATTERN_MODULUS
                for byteIndex in range(payloadSizeBytes)
            )
            probePayload = b"probe"
            initPath.write_bytes(initPayload)
            probePath.write_bytes(probePayload)
            formatRootfsV2(imagePath, createImage=True)

            installRootfsV2Files(
                imagePath,
                (
                    RootfsV2InstallFile(
                        imagePath=OS_TEST_ROOTFS_INSTALL_PATH,
                        sourcePath=initPath,
                    ),
                    RootfsV2InstallFile(
                        imagePath=OS_TEST_ROOTFS_INSTALL_SECOND_PATH,
                        sourcePath=probePath,
                    ),
                ),
            )

            inspection = inspectRootfsV2(imagePath)
            self.assertEqual(
                readRootfsV2File(
                    imagePath,
                    OS_TEST_ROOTFS_INSTALL_PATH,
                ),
                initPayload,
            )
            self.assertEqual(
                readRootfsV2File(
                    imagePath,
                    OS_TEST_ROOTFS_INSTALL_SECOND_PATH,
                ),
                probePayload,
            )
            self.assertEqual(inspection.directoryCount, 3)
            self.assertEqual(inspection.regularFileCount, 2)
            with self.assertRaises(OsToolError):
                installRootfsV2Files(
                    imagePath,
                    (
                        RootfsV2InstallFile(
                            imagePath=OS_TEST_ROOTFS_INSTALL_PATH,
                            sourcePath=initPath,
                        ),
                    ),
                )

    def testEverySupportedCorruptionIsRejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryPath = Path(temporaryDirectory)
            sourcePath = temporaryPath / "source.img"
            formatRootfsV2(sourcePath, createImage=True)

            for corruptionKind in OS_ROOTFS_V2_CORRUPTION_KINDS:
                with self.subTest(corruptionKind=corruptionKind):
                    corruptPath = (
                        temporaryPath / f"{corruptionKind}.img"
                    )
                    copySparseImage(sourcePath, corruptPath)
                    corruptRootfsV2(corruptPath, corruptionKind)
                    with self.assertRaises(OsToolError):
                        inspectRootfsV2(corruptPath)

    def testSparseCopyPreservesLogicalSizeAndTailExtent(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryPath = Path(temporaryDirectory)
            sourcePath = temporaryPath / "source.img"
            destinationPath = temporaryPath / "destination.img"
            formatRootfsV2(sourcePath, createImage=True)
            with sourcePath.open("r+b") as sourceFile:
                sourceFile.seek(
                    OS_ROOTFS_V2_CAPACITY_IMAGE_SIZE_BYTES
                    - len(OS_TEST_ROOTFS_SPARSE_TAIL)
                )
                sourceFile.write(OS_TEST_ROOTFS_SPARSE_TAIL)

            copySparseImage(sourcePath, destinationPath)

            self.assertEqual(
                destinationPath.stat().st_size,
                OS_ROOTFS_V2_CAPACITY_IMAGE_SIZE_BYTES,
            )
            with destinationPath.open("rb") as destinationFile:
                destinationFile.seek(
                    OS_ROOTFS_V2_CAPACITY_IMAGE_SIZE_BYTES
                    - len(OS_TEST_ROOTFS_SPARSE_TAIL)
                )
                self.assertEqual(
                    destinationFile.read(
                        len(OS_TEST_ROOTFS_SPARSE_TAIL)
                    ),
                    OS_TEST_ROOTFS_SPARSE_TAIL,
                )
            self.assertLess(
                destinationPath.stat().st_blocks * 512,
                OS_TEST_ROOTFS_MAXIMUM_ALLOCATED_IMAGE_BYTES,
            )
            self.assertEqual(
                inspectRootfsV2(destinationPath).reachableInodeCount,
                1,
            )


if __name__ == "__main__":
    unittest.main()
