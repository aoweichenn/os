import random
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.stage1_image import (
    OS_STAGE1_IMAGE_DEFAULT_LOAD_SEGMENT,
    OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA,
    OS_STAGE1_IMAGE_MAXIMUM_PAYLOAD_SECTORS,
    OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES,
    createStage1DiskImageBytes,
    parseAndValidateStage1DiskImage,
)


OS_TEST_STAGE1_RANDOM_SEED = 0x5EED_0002
OS_TEST_STAGE1_RANDOM_CASE_COUNT = 256
OS_TEST_STAGE1_RANDOM_DISK_SECTOR_COUNT = 256
OS_TEST_STAGE1_RANDOM_MINIMUM_BINARY_SIZE_BYTES = 1
OS_TEST_STAGE1_RANDOM_INVALID_LBA_MULTIPLIER = 4
OS_TEST_STAGE1_RANDOM_MAXIMUM_BINARY_SIZE_BYTES = (
    OS_STAGE1_IMAGE_MAXIMUM_PAYLOAD_SECTORS
    * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
)


class Stage1ImageRandomizedTests(unittest.TestCase):
    def testRoundTripsRandomValidPayloads(self) -> None:
        generator = random.Random(OS_TEST_STAGE1_RANDOM_SEED)
        diskSizeBytes = (
            OS_TEST_STAGE1_RANDOM_DISK_SECTOR_COUNT
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )

        for caseIndex in range(OS_TEST_STAGE1_RANDOM_CASE_COUNT):
            payloadSizeBytes = generator.randint(
                OS_TEST_STAGE1_RANDOM_MINIMUM_BINARY_SIZE_BYTES,
                OS_TEST_STAGE1_RANDOM_MAXIMUM_BINARY_SIZE_BYTES,
            )
            payload = generator.randbytes(payloadSizeBytes)
            payloadSectorCount = (
                payloadSizeBytes + OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES - 1
            ) // OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
            maximumPayloadLba = (
                OS_TEST_STAGE1_RANDOM_DISK_SECTOR_COUNT
                - payloadSectorCount
            )
            payloadLba = generator.randint(
                OS_STAGE1_IMAGE_DEFAULT_PAYLOAD_LBA,
                maximumPayloadLba,
            )

            with self.subTest(
                seed=OS_TEST_STAGE1_RANDOM_SEED,
                caseIndex=caseIndex,
                payloadSizeBytes=payloadSizeBytes,
                payloadLba=payloadLba,
            ):
                diskImage = createStage1DiskImageBytes(
                    payload,
                    diskSizeBytes,
                    payloadLba=payloadLba,
                    loadSegment=OS_STAGE1_IMAGE_DEFAULT_LOAD_SEGMENT,
                )
                descriptor = parseAndValidateStage1DiskImage(diskImage)
                self.assertEqual(
                    descriptor.payloadSectorCount,
                    payloadSectorCount,
                )
                self.assertEqual(descriptor.payloadLba, payloadLba)

    def testRejectsRandomOutOfBoundsLbas(self) -> None:
        generator = random.Random(OS_TEST_STAGE1_RANDOM_SEED)
        diskSizeBytes = (
            OS_TEST_STAGE1_RANDOM_DISK_SECTOR_COUNT
            * OS_STAGE1_IMAGE_SECTOR_SIZE_BYTES
        )

        for caseIndex in range(OS_TEST_STAGE1_RANDOM_CASE_COUNT):
            invalidPayloadLba = generator.randint(
                OS_TEST_STAGE1_RANDOM_DISK_SECTOR_COUNT,
                OS_TEST_STAGE1_RANDOM_DISK_SECTOR_COUNT
                * OS_TEST_STAGE1_RANDOM_INVALID_LBA_MULTIPLIER,
            )

            with self.subTest(
                seed=OS_TEST_STAGE1_RANDOM_SEED,
                caseIndex=caseIndex,
                invalidPayloadLba=invalidPayloadLba,
            ):
                with self.assertRaises(OsToolError):
                    createStage1DiskImageBytes(
                        b"\xF4",
                        diskSizeBytes,
                        payloadLba=invalidPayloadLba,
                    )


if __name__ == "__main__":
    unittest.main()
