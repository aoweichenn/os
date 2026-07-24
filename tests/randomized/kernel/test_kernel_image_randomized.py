import random
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.kernel_image import (
    OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA,
    createKernelDiskImageBytes,
    parseAndValidateKernelDiskImage,
)
from tools.os_tools.stage1_image import (
    OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES,
    createStage1DiskImageBytes,
)
from tests.tooling.test_kernel_elf import createValidKernelElf


OS_TEST_KERNEL_IMAGE_RANDOM_SEED = 0x4B45_524E_494D_47
OS_TEST_KERNEL_IMAGE_RANDOM_VALID_CASE_COUNT = 128
OS_TEST_KERNEL_IMAGE_RANDOM_CORRUPTION_CASE_COUNT = 256
OS_TEST_KERNEL_IMAGE_RANDOM_PADDING_CASE_COUNT = 128
OS_TEST_KERNEL_IMAGE_RANDOM_DISK_SECTOR_COUNT = 256
OS_TEST_KERNEL_IMAGE_RANDOM_MAXIMUM_SUFFIX_SIZE_BYTES = 16_384
OS_TEST_KERNEL_IMAGE_RANDOM_STAGE1_BINARY = b"\xFA\xFC\xF4"
OS_TEST_KERNEL_IMAGE_RANDOM_CORRUPTION_BIT = 0x01


def createStage1DiskImage() -> bytes:
    return createStage1DiskImageBytes(
        OS_TEST_KERNEL_IMAGE_RANDOM_STAGE1_BINARY,
        (
            OS_TEST_KERNEL_IMAGE_RANDOM_DISK_SECTOR_COUNT
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        ),
    )


class KernelImageRandomizedTests(unittest.TestCase):
    def testRoundTripsRandomElfFileSizes(self) -> None:
        generator = random.Random(OS_TEST_KERNEL_IMAGE_RANDOM_SEED)

        for caseIndex in range(
            OS_TEST_KERNEL_IMAGE_RANDOM_VALID_CASE_COUNT
        ):
            suffixSizeBytes = generator.randint(
                0,
                OS_TEST_KERNEL_IMAGE_RANDOM_MAXIMUM_SUFFIX_SIZE_BYTES,
            )
            kernelElf = (
                createValidKernelElf()
                + generator.randbytes(suffixSizeBytes)
            )

            with self.subTest(
                seed=OS_TEST_KERNEL_IMAGE_RANDOM_SEED,
                caseIndex=caseIndex,
                suffixSizeBytes=suffixSizeBytes,
            ):
                diskImage = createKernelDiskImageBytes(
                    createStage1DiskImage(),
                    kernelElf,
                )
                descriptor, parsedKernelElf = (
                    parseAndValidateKernelDiskImage(diskImage)
                )
                self.assertEqual(
                    descriptor.fileSizeBytes,
                    len(kernelElf),
                )
                self.assertEqual(parsedKernelElf, kernelElf)

    def testRejectsRandomKernelPayloadCorruption(self) -> None:
        generator = random.Random(OS_TEST_KERNEL_IMAGE_RANDOM_SEED)
        kernelElf = createValidKernelElf()
        validDiskImage = createKernelDiskImageBytes(
            createStage1DiskImage(),
            kernelElf,
        )
        payloadOffset = (
            OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )

        for caseIndex in range(
            OS_TEST_KERNEL_IMAGE_RANDOM_CORRUPTION_CASE_COUNT
        ):
            mutationOffset = generator.randrange(len(kernelElf))
            corruptedDiskImage = bytearray(validDiskImage)
            corruptedDiskImage[
                payloadOffset + mutationOffset
            ] ^= OS_TEST_KERNEL_IMAGE_RANDOM_CORRUPTION_BIT

            with self.subTest(
                seed=OS_TEST_KERNEL_IMAGE_RANDOM_SEED,
                caseIndex=caseIndex,
                mutationOffset=mutationOffset,
            ):
                with self.assertRaises(OsToolError):
                    parseAndValidateKernelDiskImage(
                        bytes(corruptedDiskImage)
                    )

    def testRejectsRandomNonzeroKernelPadding(self) -> None:
        generator = random.Random(OS_TEST_KERNEL_IMAGE_RANDOM_SEED)
        kernelElf = createValidKernelElf()
        validDiskImage = createKernelDiskImageBytes(
            createStage1DiskImage(),
            kernelElf,
        )
        payloadOffset = (
            OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )
        paddedPayloadSizeBytes = (
            (
                len(kernelElf)
                + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
                - 1
            )
            // OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )
        paddingBeginOffset = payloadOffset + len(kernelElf)
        paddingEndOffset = payloadOffset + paddedPayloadSizeBytes

        for caseIndex in range(
            OS_TEST_KERNEL_IMAGE_RANDOM_PADDING_CASE_COUNT
        ):
            mutationOffset = generator.randrange(
                paddingBeginOffset,
                paddingEndOffset,
            )
            corruptedDiskImage = bytearray(validDiskImage)
            corruptedDiskImage[mutationOffset] = (
                OS_TEST_KERNEL_IMAGE_RANDOM_CORRUPTION_BIT
            )

            with self.subTest(
                seed=OS_TEST_KERNEL_IMAGE_RANDOM_SEED,
                caseIndex=caseIndex,
                mutationOffset=mutationOffset,
            ):
                with self.assertRaises(OsToolError):
                    parseAndValidateKernelDiskImage(
                        bytes(corruptedDiskImage)
                    )


if __name__ == "__main__":
    unittest.main()
