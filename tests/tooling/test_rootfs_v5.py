from dataclasses import replace
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from tools.os_tools.rootfs_v5 import (
    OS_ROOTFS_V5_BLOCK_SIZE_BYTES,
    OS_ROOTFS_V5_CORRUPTION_KINDS,
    OS_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES,
    OS_ROOTFS_V5_INCOMPAT_BLOCK_GROUPS,
    OS_ROOTFS_V5_INODE_SIZE_BYTES,
    OS_ROOTFS_V5_READ_ONLY_COMPAT_METADATA_CRC32C,
    RootfsV5FormatProfile,
    RootfsV5Uuid,
    buildInitialRootfsV5BlockBitmap,
    buildInitialRootfsV5GroupDescriptor,
    buildInitialRootfsV5InodeBitmap,
    calculateRootfsV5Crc32c,
    corruptRootfsV5,
    decodeRootfsV5GroupDescriptor,
    decodeRootfsV5Inode,
    decodeRootfsV5Superblock,
    encodeRootfsV5Inode,
    encodeRootfsV5Superblock,
    formatRootfsV5,
    inspectRootfsV5,
    makeProductionRootfsV5FormatProfile,
    planRootfsV5Superblock,
    rootfsV5InspectionAsJson,
    validateRootfsV5Superblock,
)


OS_TEST_ROOTFS_V5_CREATION_TIME_NANOSECONDS = 123456789
OS_TEST_ROOTFS_V5_UUID = RootfsV5Uuid(
    low=0x0123_4567_89AB_CDEF,
    high=0xFEDC_BA98_7654_3210,
)
OS_TEST_ROOTFS_V5_START_LBA = 8
OS_TEST_ROOTFS_V5_TOTAL_BLOCK_COUNT = 1000
OS_TEST_ROOTFS_V5_BLOCKS_PER_GROUP = 256
OS_TEST_ROOTFS_V5_INODES_PER_GROUP = 64
OS_TEST_ROOTFS_V5_GROUP_COUNT = 4
OS_TEST_ROOTFS_V5_EXPECTED_BACKUP_GROUP_COUNT = 3
OS_TEST_ROOTFS_V5_EXPECTED_CRC32C = 0xE306_9283
OS_TEST_ROOTFS_V5_EXPECTED_SUPERBLOCK_CHECKSUM = 0x9B5E_7B2E


def makeSmallProfile() -> RootfsV5FormatProfile:
    return RootfsV5FormatProfile(
        sectorSizeBytes=512,
        blockSizeBytes=OS_ROOTFS_V5_BLOCK_SIZE_BYTES,
        fileSystemStartLba=OS_TEST_ROOTFS_V5_START_LBA,
        deviceSectorCount=(
            OS_TEST_ROOTFS_V5_START_LBA
            + OS_TEST_ROOTFS_V5_TOTAL_BLOCK_COUNT * 8
        ),
        blocksPerGroup=OS_TEST_ROOTFS_V5_BLOCKS_PER_GROUP,
        groupDescriptorSizeBytes=(
            OS_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES
        ),
        inodeSizeBytes=OS_ROOTFS_V5_INODE_SIZE_BYTES,
        inodesPerGroup=OS_TEST_ROOTFS_V5_INODES_PER_GROUP,
        creationTimeNanoseconds=(
            OS_TEST_ROOTFS_V5_CREATION_TIME_NANOSECONDS
        ),
        fileSystemUuid=OS_TEST_ROOTFS_V5_UUID,
    )


class RootfsV5ToolTests(unittest.TestCase):
    def testProductionProfileAndCrc32cAreStable(self) -> None:
        superblock = planRootfsV5Superblock(
            makeProductionRootfsV5FormatProfile(
                OS_TEST_ROOTFS_V5_CREATION_TIME_NANOSECONDS,
                OS_TEST_ROOTFS_V5_UUID,
            )
        )
        self.assertEqual(calculateRootfsV5Crc32c(b"123456789"),
                         OS_TEST_ROOTFS_V5_EXPECTED_CRC32C)
        self.assertEqual(superblock.totalBlockCount, 33_550_336)
        self.assertEqual(superblock.groupCount, 1024)
        self.assertEqual(superblock.groupDescriptorTableBlockCount, 64)
        self.assertEqual(superblock.inodeCount, 2_097_152)
        self.assertEqual(superblock.freeBlockCount, 33_416_241)
        self.assertEqual(superblock.freeInodeCount, 2_097_137)
        self.assertEqual(
            int.from_bytes(
                encodeRootfsV5Superblock(superblock)[-4:],
                "little",
            ),
            OS_TEST_ROOTFS_V5_EXPECTED_SUPERBLOCK_CHECKSUM,
        )

    def testSmallImageRoundTripBackupsBitmapsAndSafety(self) -> None:
        profile = makeSmallProfile()
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "rootfs-v5.img"
            superblock = formatRootfsV5(
                imagePath,
                profile=profile,
                createImage=True,
            )
            inspection = inspectRootfsV5(
                imagePath,
                fileSystemStartLba=profile.fileSystemStartLba,
            )
            self.assertEqual(superblock.groupCount,
                             OS_TEST_ROOTFS_V5_GROUP_COUNT)
            self.assertEqual(inspection.groupCount,
                             OS_TEST_ROOTFS_V5_GROUP_COUNT)
            self.assertEqual(inspection.sparseBackupGroupCount,
                             OS_TEST_ROOTFS_V5_EXPECTED_BACKUP_GROUP_COUNT)
            self.assertEqual(inspection.rootInodeNumber, 2)
            self.assertEqual(inspection.allocatedDirectoryCount, 1)
            self.assertLess(inspection.allocatedImageSizeBytes,
                            inspection.logicalImageSizeBytes)
            self.assertEqual(
                json.loads(rootfsV5InspectionAsJson(inspection))["version"],
                5,
            )

            with imagePath.open("rb") as imageFile:
                imageFile.seek(profile.fileSystemStartLba * profile.sectorSizeBytes)
                encodedSuperblock = imageFile.read(profile.blockSizeBytes)
                decodedSuperblock = decodeRootfsV5Superblock(encodedSuperblock)
                imageFile.seek(
                    profile.fileSystemStartLba * profile.sectorSizeBytes
                    + decodedSuperblock.groupDescriptorTableStartBlock
                    * decodedSuperblock.blockSizeBytes
                )
                encodedDescriptor = imageFile.read(
                    decodedSuperblock.groupDescriptorSizeBytes
                )
                descriptor = decodeRootfsV5GroupDescriptor(
                    decodedSuperblock,
                    encodedDescriptor,
                )
                imageFile.seek(
                    profile.fileSystemStartLba * profile.sectorSizeBytes
                    + descriptor.blockBitmapBlock
                    * decodedSuperblock.blockSizeBytes
                )
                self.assertEqual(
                    imageFile.read(decodedSuperblock.blockSizeBytes),
                    buildInitialRootfsV5BlockBitmap(
                        decodedSuperblock,
                        descriptor,
                    ),
                )
                imageFile.seek(
                    profile.fileSystemStartLba * profile.sectorSizeBytes
                    + descriptor.inodeBitmapBlock
                    * decodedSuperblock.blockSizeBytes
                )
                self.assertEqual(
                    imageFile.read(decodedSuperblock.blockSizeBytes),
                    buildInitialRootfsV5InodeBitmap(
                        decodedSuperblock,
                        descriptor,
                    ),
                )

            with self.assertRaises(ValueError):
                formatRootfsV5(imagePath, profile=profile)
            with self.assertRaises(ValueError):
                formatRootfsV5(
                    imagePath,
                    profile=profile,
                    createImage=True,
                )
            formatRootfsV5(
                imagePath,
                profile=profile,
                createImage=True,
                force=True,
            )
            with imagePath.open("r+b") as imageFile:
                imageFile.seek(
                    profile.fileSystemStartLba * profile.sectorSizeBytes
                )
                decodedSuperblock = decodeRootfsV5Superblock(
                    imageFile.read(profile.blockSizeBytes)
                )
                imageFile.seek(
                    profile.fileSystemStartLba * profile.sectorSizeBytes
                    + decodedSuperblock.groupDescriptorTableStartBlock
                    * decodedSuperblock.blockSizeBytes
                )
                descriptor = decodeRootfsV5GroupDescriptor(
                    decodedSuperblock,
                    imageFile.read(
                        decodedSuperblock.groupDescriptorSizeBytes
                    ),
                )
                rootOffset = (
                    profile.fileSystemStartLba * profile.sectorSizeBytes
                    + descriptor.inodeTableStartBlock
                    * decodedSuperblock.blockSizeBytes
                    + (decodedSuperblock.rootInodeNumber - 1)
                    * decodedSuperblock.inodeSizeBytes
                )
                imageFile.seek(rootOffset)
                rootInode = decodeRootfsV5Inode(
                    decodedSuperblock,
                    imageFile.read(decodedSuperblock.inodeSizeBytes),
                )
                imageFile.seek(rootOffset)
                imageFile.write(
                    encodeRootfsV5Inode(
                        decodedSuperblock,
                        replace(rootInode, mode=rootInode.mode ^ 0o055),
                    )
                )
            with self.assertRaises(ValueError):
                inspectRootfsV5(
                    imagePath,
                    fileSystemStartLba=profile.fileSystemStartLba,
                )

    def testFeatureCompatibilityRules(self) -> None:
        superblock = planRootfsV5Superblock(makeSmallProfile())
        unknownCompat = replace(
            superblock,
            compatibleFeatures=superblock.compatibleFeatures | (1 << 63),
        )
        self.assertEqual(
            decodeRootfsV5Superblock(
                encodeRootfsV5Superblock(unknownCompat)
            ).compatibleFeatures,
            unknownCompat.compatibleFeatures,
        )
        with self.assertRaises(ValueError):
            validateRootfsV5Superblock(
                replace(
                    superblock,
                    readOnlyCompatibleFeatures=(
                        OS_ROOTFS_V5_READ_ONLY_COMPAT_METADATA_CRC32C
                        | (1 << 63)
                    ),
                )
            )
        with self.assertRaises(ValueError):
            validateRootfsV5Superblock(
                replace(
                    superblock,
                    incompatibleFeatures=(
                        superblock.incompatibleFeatures | (1 << 63)
                    ),
                )
            )
        with self.assertRaises(ValueError):
            validateRootfsV5Superblock(
                replace(
                    superblock,
                    incompatibleFeatures=(
                        superblock.incompatibleFeatures
                        & ~OS_ROOTFS_V5_INCOMPAT_BLOCK_GROUPS
                    ),
                )
            )

    def testEveryCorruptionKindIsRejected(self) -> None:
        profile = makeSmallProfile()
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            sourcePath = Path(temporaryDirectory) / "source.img"
            formatRootfsV5(
                sourcePath,
                profile=profile,
                createImage=True,
            )
            for corruptionKind in OS_ROOTFS_V5_CORRUPTION_KINDS:
                corruptPath = (
                    Path(temporaryDirectory) / f"{corruptionKind}.img"
                )
                shutil.copyfile(sourcePath, corruptPath)
                corruptRootfsV5(
                    corruptPath,
                    corruptionKind,
                    fileSystemStartLba=profile.fileSystemStartLba,
                )
                with self.subTest(corruptionKind=corruptionKind):
                    with self.assertRaises(ValueError):
                        inspectRootfsV5(
                            corruptPath,
                            fileSystemStartLba=profile.fileSystemStartLba,
                        )


if __name__ == "__main__":
    unittest.main()
