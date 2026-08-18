import struct
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.stage1_image import (
    OS_STAGE1_IMAGE_DEFAULT_LOAD_SEGMENT,
    OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA,
    OS_STAGE1_IMAGE_CORRUPTION_BIT,
    OS_STAGE1_IMAGE_HEADER_CHECKSUM_OFFSET,
    OS_STAGE1_IMAGE_MAGIC_OFFSET,
    OS_STAGE1_IMAGE_PAYLOAD_LBA_OFFSET,
    OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES,
    OS_STAGE1_IMAGE_UINT16_MAXIMUM,
    calculateWordChecksum,
    createStage1DiskImageBytes,
    parseAndValidateStage1DiskImage,
)


OS_TEST_STAGE1_DISK_SIZE_BYTES = 8 * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
OS_TEST_STAGE1_BINARY = b"\xFA\xFC\xF4"


class Stage1ImageToolTests(unittest.TestCase):
    def testBuildsAndParsesValidStage1Image(self) -> None:
        diskImage = createStage1DiskImageBytes(
            OS_TEST_STAGE1_BINARY,
            OS_TEST_STAGE1_DISK_SIZE_BYTES,
        )

        descriptor = parseAndValidateStage1DiskImage(diskImage)

        self.assertEqual(
            descriptor.loadSegment,
            OS_STAGE1_IMAGE_DEFAULT_LOAD_SEGMENT,
        )
        self.assertEqual(
            descriptor.payloadLba,
            OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA,
        )
        self.assertEqual(descriptor.payloadSectorCount, 1)
        self.assertEqual(len(diskImage), OS_TEST_STAGE1_DISK_SIZE_BYTES)

    def testDescriptorChecksumCoversReservedBytes(self) -> None:
        diskImage = createStage1DiskImageBytes(
            OS_TEST_STAGE1_BINARY,
            OS_TEST_STAGE1_DISK_SIZE_BYTES,
        )
        descriptor = diskImage[:OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES]

        self.assertEqual(calculateWordChecksum(descriptor), 0)

    def testRejectsTruncatedDescriptor(self) -> None:
        with self.assertRaises(OsToolError):
            parseAndValidateStage1DiskImage(
                bytes(OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES - 1)
            )

    def testRejectsCorruptedMagic(self) -> None:
        diskImage = bytearray(
            createStage1DiskImageBytes(
                OS_TEST_STAGE1_BINARY,
                OS_TEST_STAGE1_DISK_SIZE_BYTES,
            )
        )
        diskImage[
            OS_STAGE1_IMAGE_MAGIC_OFFSET
        ] ^= OS_STAGE1_IMAGE_CORRUPTION_BIT

        with self.assertRaises(OsToolError):
            parseAndValidateStage1DiskImage(bytes(diskImage))

    def testRejectsCorruptedPayload(self) -> None:
        diskImage = bytearray(
            createStage1DiskImageBytes(
                OS_TEST_STAGE1_BINARY,
                OS_TEST_STAGE1_DISK_SIZE_BYTES,
            )
        )
        payloadOffset = (
            OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )
        diskImage[payloadOffset] ^= OS_STAGE1_IMAGE_CORRUPTION_BIT

        with self.assertRaises(OsToolError):
            parseAndValidateStage1DiskImage(bytes(diskImage))

    def testRejectsPayloadLbaOutsideDisk(self) -> None:
        diskImage = bytearray(
            createStage1DiskImageBytes(
                OS_TEST_STAGE1_BINARY,
                OS_TEST_STAGE1_DISK_SIZE_BYTES,
            )
        )
        struct.pack_into(
            "<I",
            diskImage,
            OS_STAGE1_IMAGE_PAYLOAD_LBA_OFFSET,
            OS_TEST_STAGE1_DISK_SIZE_BYTES
            // OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES,
        )
        struct.pack_into(
            "<H",
            diskImage,
            OS_STAGE1_IMAGE_HEADER_CHECKSUM_OFFSET,
            0,
        )
        descriptor = diskImage[:OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES]
        descriptorChecksum = (
            -calculateWordChecksum(descriptor)
        ) & OS_STAGE1_IMAGE_UINT16_MAXIMUM
        struct.pack_into(
            "<H",
            diskImage,
            OS_STAGE1_IMAGE_HEADER_CHECKSUM_OFFSET,
            descriptorChecksum,
        )

        with self.assertRaises(OsToolError):
            parseAndValidateStage1DiskImage(bytes(diskImage))

    def testRejectsLoadRangeBelowFirmwareContract(self) -> None:
        with self.assertRaises(OsToolError):
            createStage1DiskImageBytes(
                OS_TEST_STAGE1_BINARY,
                OS_TEST_STAGE1_DISK_SIZE_BYTES,
                loadSegment=0x0100,
            )

    def testRejectsLoadRangeOverlappingVgaState(self) -> None:
        with self.assertRaisesRegex(OsToolError, "VGA 共享状态"):
            createStage1DiskImageBytes(
                OS_TEST_STAGE1_BINARY,
                OS_TEST_STAGE1_DISK_SIZE_BYTES,
                loadSegment=0x8000,
            )


if __name__ == "__main__":
    unittest.main()
