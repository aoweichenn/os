import random
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.kernel_elf import parseKernelLoadSegments
from tests.tooling.test_kernel_elf import createValidKernelElf


OS_TEST_KERNEL_ELF_RANDOM_SEED = 0x4B45_524E_454C
OS_TEST_KERNEL_ELF_RANDOM_CASE_COUNT = 256
OS_TEST_KERNEL_ELF_PROTECTED_IDENTIFICATION_SIZE_BYTES = 7
OS_TEST_KERNEL_ELF_MUTATION_MINIMUM = 1
OS_TEST_KERNEL_ELF_MUTATION_MAXIMUM = 255


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


if __name__ == "__main__":
    unittest.main()
