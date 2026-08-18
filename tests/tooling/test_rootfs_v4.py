from pathlib import Path
import tempfile
import unittest

from tools.os_tools.rootfs_v4 import (
    OS_ROOTFS_V4_BLOCK_SIZE_BYTES,
    OS_ROOTFS_V4_CAPACITY_IMAGE_SIZE_BYTES,
    OS_ROOTFS_V4_DATA_BITMAP_START_RELATIVE_BLOCK,
    OS_ROOTFS_V4_DATA_BLOCK_COUNT,
    OS_ROOTFS_V4_DATA_START_RELATIVE_BLOCK,
    OS_ROOTFS_V4_DIRECT_BLOCK_COUNT,
    OS_ROOTFS_V4_INODE_BITMAP_START_RELATIVE_BLOCK,
    OS_ROOTFS_V4_INODE_SIZE_BYTES,
    OS_ROOTFS_V4_INODE_TABLE_START_RELATIVE_BLOCK,
    OS_ROOTFS_V4_MODE_DIRECTORY,
    OS_ROOTFS_V4_MODE_REGULAR,
    OS_ROOTFS_V4_NODE_TYPE_DIRECTORY,
    OS_ROOTFS_V4_NODE_TYPE_REGULAR_FILE,
    OS_ROOTFS_V4_ROOT_INODE_NUMBER,
    OS_ROOTFS_V4_START_LBA,
    OS_ROOTFS_V4_TOTAL_BLOCK_COUNT,
    RootfsV4DirectoryEntry,
    RootfsV4Inode,
    encodeRootfsV4DirectoryEntry,
    encodeRootfsV4Inode,
    encodeRootfsV4Superblock,
    formatRootfsV4,
    inspectRootfsV4,
    readRootfsV4File,
    writeRootfsV4Block,
)


OS_TEST_ROOTFS_V4_HIGH_FILE_NAME = b"high-lba"
OS_TEST_ROOTFS_V4_HIGH_FILE_PATH = "/high-lba"
OS_TEST_ROOTFS_V4_HIGH_FILE_PAYLOAD = b"rootfs-v4-high-lba"
OS_TEST_ROOTFS_V4_FILE_INODE_NUMBER = 2
OS_TEST_ROOTFS_V4_FILE_GENERATION = 2
OS_TEST_ROOTFS_V4_NEXT_INODE_GENERATION = 3
OS_TEST_ROOTFS_V4_MAXIMUM_ALLOCATED_IMAGE_BYTES = 8 * 1024 * 1024
OS_TEST_ROOTFS_V4_LBA28_MAXIMUM = 0x0FFF_FFFF


def makeInode(
    nodeType: int,
    sizeBytes: int,
    generation: int,
    parentInodeNumber: int,
    directBlock: int,
) -> RootfsV4Inode:
    directBlocks = (directBlock,) + (0,) * (
        OS_ROOTFS_V4_DIRECT_BLOCK_COUNT - 1
    )
    return RootfsV4Inode(
        nodeType=nodeType,
        flags=0,
        sizeBytes=sizeBytes,
        generation=generation,
        linkCount=1,
        allocatedDataBlockCount=1,
        allocatedMetadataBlockCount=0,
        parentInodeNumber=parentInodeNumber,
        directBlocks=directBlocks,
        singleIndirectBlock=0,
        doubleIndirectBlock=0,
        tripleIndirectBlock=0,
        quadrupleIndirectBlock=0,
        quintupleIndirectBlock=0,
        accessTimeNanoseconds=1,
        modificationTimeNanoseconds=1,
        changeTimeNanoseconds=1,
        birthTimeNanoseconds=1,
        ownerUserIdentifier=0,
        ownerGroupIdentifier=0,
        mode=(
            (OS_ROOTFS_V4_MODE_DIRECTORY | 0o755)
            if nodeType == OS_ROOTFS_V4_NODE_TYPE_DIRECTORY
            else (OS_ROOTFS_V4_MODE_REGULAR | 0o644)
        ),
    )


class RootfsV4ToolTests(unittest.TestCase):
    def testHighLbaFileUsesLastLbaAndKeepsImageSparse(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "rootfs-v4.img"
            formatRootfsV4(imagePath, createImage=True)
            lastRelativeBlock = OS_ROOTFS_V4_TOTAL_BLOCK_COUNT - 1
            self.assertEqual(
                OS_ROOTFS_V4_START_LBA + lastRelativeBlock,
                OS_TEST_ROOTFS_V4_LBA28_MAXIMUM,
            )

            directoryEntry = encodeRootfsV4DirectoryEntry(
                RootfsV4DirectoryEntry(
                    inodeNumber=OS_TEST_ROOTFS_V4_FILE_INODE_NUMBER,
                    inodeGeneration=OS_TEST_ROOTFS_V4_FILE_GENERATION,
                    nodeType=OS_ROOTFS_V4_NODE_TYPE_REGULAR_FILE,
                    name=OS_TEST_ROOTFS_V4_HIGH_FILE_NAME,
                )
            )
            directoryBlock = bytearray(OS_ROOTFS_V4_BLOCK_SIZE_BYTES)
            directoryBlock[:len(directoryEntry)] = directoryEntry

            rootInode = makeInode(
                OS_ROOTFS_V4_NODE_TYPE_DIRECTORY,
                len(directoryEntry),
                1,
                OS_ROOTFS_V4_ROOT_INODE_NUMBER,
                OS_ROOTFS_V4_DATA_START_RELATIVE_BLOCK,
            )
            fileInode = makeInode(
                OS_ROOTFS_V4_NODE_TYPE_REGULAR_FILE,
                len(OS_TEST_ROOTFS_V4_HIGH_FILE_PAYLOAD),
                OS_TEST_ROOTFS_V4_FILE_GENERATION,
                OS_ROOTFS_V4_ROOT_INODE_NUMBER,
                lastRelativeBlock,
            )
            inodeBlock = bytearray(OS_ROOTFS_V4_BLOCK_SIZE_BYTES)
            inodeBlock[:OS_ROOTFS_V4_INODE_SIZE_BYTES] = (
                encodeRootfsV4Inode(rootInode)
            )
            inodeBlock[
                OS_ROOTFS_V4_INODE_SIZE_BYTES:
                2 * OS_ROOTFS_V4_INODE_SIZE_BYTES
            ] = encodeRootfsV4Inode(fileInode)

            inodeBitmap = bytearray(OS_ROOTFS_V4_BLOCK_SIZE_BYTES)
            inodeBitmap[0] = 0x03
            firstDataBitmap = bytearray(OS_ROOTFS_V4_BLOCK_SIZE_BYTES)
            firstDataBitmap[0] = 0x01
            lastDataBit = OS_ROOTFS_V4_DATA_BLOCK_COUNT - 1
            lastDataBitmapBlockIndex = lastDataBit // (
                OS_ROOTFS_V4_BLOCK_SIZE_BYTES * 8
            )
            lastDataBitmapBlock = bytearray(OS_ROOTFS_V4_BLOCK_SIZE_BYTES)
            lastDataBitmapBit = lastDataBit % (
                OS_ROOTFS_V4_BLOCK_SIZE_BYTES * 8
            )
            lastDataBitmapBlock[lastDataBitmapBit // 8] |= (
                1 << (lastDataBitmapBit % 8)
            )

            with imagePath.open("r+b") as imageFile:
                writeRootfsV4Block(
                    imageFile,
                    OS_ROOTFS_V4_INODE_BITMAP_START_RELATIVE_BLOCK,
                    bytes(inodeBitmap),
                )
                writeRootfsV4Block(
                    imageFile,
                    OS_ROOTFS_V4_INODE_TABLE_START_RELATIVE_BLOCK,
                    bytes(inodeBlock),
                )
                writeRootfsV4Block(
                    imageFile,
                    OS_ROOTFS_V4_DATA_BITMAP_START_RELATIVE_BLOCK,
                    bytes(firstDataBitmap),
                )
                writeRootfsV4Block(
                    imageFile,
                    OS_ROOTFS_V4_DATA_BITMAP_START_RELATIVE_BLOCK
                    + lastDataBitmapBlockIndex,
                    bytes(lastDataBitmapBlock),
                )
                writeRootfsV4Block(
                    imageFile,
                    OS_ROOTFS_V4_DATA_START_RELATIVE_BLOCK,
                    bytes(directoryBlock),
                )
                payloadBlock = bytearray(OS_ROOTFS_V4_BLOCK_SIZE_BYTES)
                payloadBlock[:len(OS_TEST_ROOTFS_V4_HIGH_FILE_PAYLOAD)] = (
                    OS_TEST_ROOTFS_V4_HIGH_FILE_PAYLOAD
                )
                writeRootfsV4Block(
                    imageFile,
                    lastRelativeBlock,
                    bytes(payloadBlock),
                )
                writeRootfsV4Block(
                    imageFile,
                    0,
                    encodeRootfsV4Superblock(
                        nextInodeGeneration=(
                            OS_TEST_ROOTFS_V4_NEXT_INODE_GENERATION
                        ),
                        allocatedInodeCount=2,
                        allocatedDataBlockCount=2,
                        allocatedMetadataBlockCount=0,
                    ),
                )

            inspection = inspectRootfsV4(imagePath)
            self.assertEqual(
                readRootfsV4File(
                    imagePath,
                    OS_TEST_ROOTFS_V4_HIGH_FILE_PATH,
                ),
                OS_TEST_ROOTFS_V4_HIGH_FILE_PAYLOAD,
            )
            self.assertEqual(
                inspection.highestAllocatedLba,
                OS_TEST_ROOTFS_V4_LBA28_MAXIMUM,
            )
            self.assertEqual(
                imagePath.stat().st_size,
                OS_ROOTFS_V4_CAPACITY_IMAGE_SIZE_BYTES,
            )
            self.assertLess(
                imagePath.stat().st_blocks * 512,
                OS_TEST_ROOTFS_V4_MAXIMUM_ALLOCATED_IMAGE_BYTES,
            )


if __name__ == "__main__":
    unittest.main()
