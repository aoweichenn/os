import random
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.user_elf import (
    OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT,
    OS_USER_ELF_PAGE_SIZE_BYTES,
    parseUserLoadSegments,
)
from tests.tooling.test_user_elf import createValidUserElf


OS_TEST_USER_ELF_RANDOM_SEED = 0x5553_4552_454C_46
OS_TEST_USER_ELF_RANDOM_CASE_COUNT = 256
OS_TEST_USER_ELF_RANDOM_PROTECTED_IDENTIFICATION_SIZE_BYTES = 7
OS_TEST_USER_ELF_RANDOM_MUTATION_MINIMUM = 1
OS_TEST_USER_ELF_RANDOM_MUTATION_MAXIMUM = 255


class UserElfRandomizedTests(unittest.TestCase):
    def testRejectsRandomIdentificationCorruption(self) -> None:
        generator = random.Random(OS_TEST_USER_ELF_RANDOM_SEED)

        for caseIndex in range(OS_TEST_USER_ELF_RANDOM_CASE_COUNT):
            corruptedElf = bytearray(createValidUserElf())
            mutationOffset = generator.randrange(
                OS_TEST_USER_ELF_RANDOM_PROTECTED_IDENTIFICATION_SIZE_BYTES
            )
            corruptedElf[mutationOffset] ^= generator.randint(
                OS_TEST_USER_ELF_RANDOM_MUTATION_MINIMUM,
                OS_TEST_USER_ELF_RANDOM_MUTATION_MAXIMUM,
            )
            with self.subTest(
                seed=OS_TEST_USER_ELF_RANDOM_SEED,
                caseIndex=caseIndex,
                mutationOffset=mutationOffset,
            ):
                with self.assertRaises(OsToolError):
                    parseUserLoadSegments(bytes(corruptedElf))

    def testValidatesRandomMappedPageLimits(self) -> None:
        generator = random.Random(OS_TEST_USER_ELF_RANDOM_SEED)

        for caseIndex in range(OS_TEST_USER_ELF_RANDOM_CASE_COUNT):
            pageCount = generator.randint(
                1,
                OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT * 2,
            )
            userElf = createValidUserElf(
                memorySizeBytes=pageCount * OS_USER_ELF_PAGE_SIZE_BYTES
            )
            with self.subTest(
                seed=OS_TEST_USER_ELF_RANDOM_SEED,
                caseIndex=caseIndex,
                pageCount=pageCount,
            ):
                if pageCount <= OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT:
                    parseUserLoadSegments(userElf)
                else:
                    with self.assertRaises(OsToolError):
                        parseUserLoadSegments(userElf)


if __name__ == "__main__":
    unittest.main()
