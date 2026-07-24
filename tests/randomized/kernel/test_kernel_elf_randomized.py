import random
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.kernel_elf import parseKernelLoadSegments
from tools.os_tools.kernel_elf import (
    OS_KERNEL_ELF_MAXIMUM_LOAD_END_ADDRESS,
    OS_KERNEL_ELF_MINIMUM_LOAD_ADDRESS,
)
from tests.tooling.test_kernel_elf import createValidKernelElf


OS_TEST_KERNEL_ELF_RANDOM_SEED = 0x4B45_524E_454C
OS_TEST_KERNEL_ELF_RANDOM_CASE_COUNT = 256
OS_TEST_KERNEL_ELF_PROTECTED_IDENTIFICATION_SIZE_BYTES = 7
OS_TEST_KERNEL_ELF_MUTATION_MINIMUM = 1
OS_TEST_KERNEL_ELF_MUTATION_MAXIMUM = 255
OS_TEST_KERNEL_ELF_RANDOM_LOAD_ADDRESS_CASE_COUNT = 256
OS_TEST_KERNEL_ELF_PAGE_SIZE_BYTES = 0x1000


class KernelElfRandomizedTests(unittest.TestCase):
    def testRejectsRandomIdentificationCorruption(self) -> None:
        generator = random.Random(OS_TEST_KERNEL_ELF_RANDOM_SEED)

        for caseIndex in range(OS_TEST_KERNEL_ELF_RANDOM_CASE_COUNT):
            corruptedElf = bytearray(createValidKernelElf())
            mutationOffset = generator.randrange(
                OS_TEST_KERNEL_ELF_PROTECTED_IDENTIFICATION_SIZE_BYTES
            )
            mutationValue = generator.randint(
                OS_TEST_KERNEL_ELF_MUTATION_MINIMUM,
                OS_TEST_KERNEL_ELF_MUTATION_MAXIMUM,
            )
            corruptedElf[mutationOffset] ^= mutationValue

            with self.subTest(
                seed=OS_TEST_KERNEL_ELF_RANDOM_SEED,
                caseIndex=caseIndex,
                mutationOffset=mutationOffset,
                mutationValue=mutationValue,
            ):
                with self.assertRaises(OsToolError):
                    parseKernelLoadSegments(bytes(corruptedElf))

    def testRejectsRandomAddressesOutsideTargetLoadArea(self) -> None:
        generator = random.Random(OS_TEST_KERNEL_ELF_RANDOM_SEED)
        minimumPageIndex = (
            OS_KERNEL_ELF_MINIMUM_LOAD_ADDRESS
            // OS_TEST_KERNEL_ELF_PAGE_SIZE_BYTES
        )
        maximumPageIndex = (
            OS_KERNEL_ELF_MAXIMUM_LOAD_END_ADDRESS
            // OS_TEST_KERNEL_ELF_PAGE_SIZE_BYTES
        )

        for caseIndex in range(
            OS_TEST_KERNEL_ELF_RANDOM_LOAD_ADDRESS_CASE_COUNT
        ):
            if generator.getrandbits(1) == 0:
                pageIndex = generator.randrange(minimumPageIndex)
            else:
                pageIndex = generator.randrange(
                    maximumPageIndex,
                    maximumPageIndex
                    + OS_TEST_KERNEL_ELF_RANDOM_LOAD_ADDRESS_CASE_COUNT,
                )
            invalidAddress = (
                pageIndex * OS_TEST_KERNEL_ELF_PAGE_SIZE_BYTES
            )

            with self.subTest(
                seed=OS_TEST_KERNEL_ELF_RANDOM_SEED,
                caseIndex=caseIndex,
                invalidAddress=invalidAddress,
            ):
                with self.assertRaises(OsToolError):
                    parseKernelLoadSegments(
                        createValidKernelElf(
                            entryAddress=invalidAddress,
                            virtualAddress=invalidAddress,
                            physicalAddress=invalidAddress,
                        )
                    )


if __name__ == "__main__":
    unittest.main()
