import struct
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.kernel_image import (
    OS_KERNEL_IMAGE_CORRUPTION_BIT,
    OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA,
    OS_KERNEL_IMAGE_DESCRIPTOR_LBA,
    OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
    OS_KERNEL_IMAGE_HEADER_SIZE_BYTES,
    OS_KERNEL_IMAGE_MAGIC_OFFSET,
    OS_KERNEL_IMAGE_UINT32_FORMAT,
    calculateCrc32,
    createKernelDiskImageBytes,
    parseAndValidateKernelDiskImage,
)
from tools.os_tools.stage1_image import (
    OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES,
    createStage1DiskImageBytes,
)
from tests.tooling.test_kernel_elf import createValidKernelElf


OS_TEST_KERNEL_IMAGE_DISK_SECTOR_COUNT = 128
OS_TEST_KERNEL_IMAGE_STAGE1_BINARY = b"\xFA\xFC\xF4"
OS_TEST_KERNEL_IMAGE_DISK_SIZE_BYTES = (
    OS_TEST_KERNEL_IMAGE_DISK_SECTOR_COUNT
    * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
)


def createValidStage1DiskImage() -> bytes:
    return createStage1DiskImageBytes(
        OS_TEST_KERNEL_IMAGE_STAGE1_BINARY,
        OS_TEST_KERNEL_IMAGE_DISK_SIZE_BYTES,
    )


class KernelImageToolTests(unittest.TestCase):
    def testBuildsAndParsesKernelDiskImage(self) -> None:
        kernelElf = createValidKernelElf()
        diskImage = createKernelDiskImageBytes(
            createValidStage1DiskImage(),
            kernelElf,
        )

        descriptor, parsedKernelElf = parseAndValidateKernelDiskImage(
            diskImage
        )

        self.assertEqual(
            descriptor.payloadLba,
            OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA,
        )
        self.assertEqual(descriptor.fileSizeBytes, len(kernelElf))
        self.assertEqual(parsedKernelElf, kernelElf)

    def testDescriptorChecksumCoversReservedBytes(self) -> None:
        diskImage = createKernelDiskImageBytes(
            createValidStage1DiskImage(),
            createValidKernelElf(),
        )
        descriptorOffset = (
            OS_KERNEL_IMAGE_DESCRIPTOR_LBA
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )
        descriptor = bytearray(
            diskImage[
                descriptorOffset:
                descriptorOffset + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
            ]
        )
        storedChecksum = struct.unpack_from(
            OS_KERNEL_IMAGE_UINT32_FORMAT,
            descriptor,
            OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
        )[0]
        struct.pack_into(
            OS_KERNEL_IMAGE_UINT32_FORMAT,
            descriptor,
            OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
            0,
        )

        self.assertEqual(calculateCrc32(descriptor), storedChecksum)

    def testRejectsCorruptedDescriptor(self) -> None:
        diskImage = bytearray(
            createKernelDiskImageBytes(
                createValidStage1DiskImage(),
                createValidKernelElf(),
            )
        )
        descriptorOffset = (
            OS_KERNEL_IMAGE_DESCRIPTOR_LBA
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )
        diskImage[
            descriptorOffset + OS_KERNEL_IMAGE_MAGIC_OFFSET
        ] ^= OS_KERNEL_IMAGE_CORRUPTION_BIT

        with self.assertRaises(OsToolError):
            parseAndValidateKernelDiskImage(bytes(diskImage))

    def testRejectsCorruptedKernelElf(self) -> None:
        diskImage = bytearray(
            createKernelDiskImageBytes(
                createValidStage1DiskImage(),
                createValidKernelElf(),
            )
        )
        payloadOffset = (
            OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )
        diskImage[payloadOffset] ^= OS_KERNEL_IMAGE_CORRUPTION_BIT

        with self.assertRaises(OsToolError):
            parseAndValidateKernelDiskImage(bytes(diskImage))

    def testRejectsInvalidKernelElfBeforePackaging(self) -> None:
        with self.assertRaises(OsToolError):
            createKernelDiskImageBytes(
                createValidStage1DiskImage(),
                b"not an ELF",
            )

    def testRejectsOccupiedKernelRegion(self) -> None:
        stage1DiskImage = bytearray(createValidStage1DiskImage())
        descriptorOffset = (
            OS_KERNEL_IMAGE_DESCRIPTOR_LBA
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )
        stage1DiskImage[descriptorOffset] = (
            OS_KERNEL_IMAGE_CORRUPTION_BIT
        )

        with self.assertRaises(OsToolError):
            createKernelDiskImageBytes(
                bytes(stage1DiskImage),
                createValidKernelElf(),
            )

    def testRejectsStage1PayloadOverlappingDescriptor(self) -> None:
        overlappingStage1Image = createStage1DiskImageBytes(
            OS_TEST_KERNEL_IMAGE_STAGE1_BINARY,
            OS_TEST_KERNEL_IMAGE_DISK_SIZE_BYTES,
            payloadLba=OS_KERNEL_IMAGE_DESCRIPTOR_LBA,
        )

        with self.assertRaises(OsToolError):
            createKernelDiskImageBytes(
                overlappingStage1Image,
                createValidKernelElf(),
            )

    def testRejectsNonzeroReservedDescriptorBytes(self) -> None:
        diskImage = bytearray(
            createKernelDiskImageBytes(
                createValidStage1DiskImage(),
                createValidKernelElf(),
            )
        )
        descriptorOffset = (
            OS_KERNEL_IMAGE_DESCRIPTOR_LBA
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )
        descriptor = bytearray(
            diskImage[
                descriptorOffset:
                descriptorOffset + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
            ]
        )
        descriptor[OS_KERNEL_IMAGE_HEADER_SIZE_BYTES] = (
            OS_KERNEL_IMAGE_CORRUPTION_BIT
        )
        struct.pack_into(
            OS_KERNEL_IMAGE_UINT32_FORMAT,
            descriptor,
            OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
            0,
        )
        updatedChecksum = calculateCrc32(descriptor)
        struct.pack_into(
            OS_KERNEL_IMAGE_UINT32_FORMAT,
            descriptor,
            OS_KERNEL_IMAGE_HEADER_CHECKSUM_OFFSET,
            updatedChecksum,
        )
        diskImage[
            descriptorOffset:
            descriptorOffset + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        ] = descriptor

        with self.assertRaises(OsToolError):
            parseAndValidateKernelDiskImage(bytes(diskImage))


if __name__ == "__main__":
    unittest.main()
