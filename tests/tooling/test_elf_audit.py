import unittest

from tools.os_tools.elf_audit import (
    OS_ELF_AUDIT_EXPECTED_MACHINE,
    isExpectedMachine,
    parseUndefinedSymbols,
)


class ElfAuditToolTests(unittest.TestCase):
    def testIgnoresArchiveMemberHeadings(self) -> None:
        llvmNmOutput = "\naddress_range.cpp.o:\n\n"

        self.assertEqual(parseUndefinedSymbols(llvmNmOutput), ())

    def testPreservesUndefinedSymbols(self) -> None:
        llvmNmOutput = "address_range.cpp.o:\n                 U memcpy\n"

        self.assertEqual(
            parseUndefinedSymbols(llvmNmOutput),
            ("U memcpy",),
        )

    def testRecognizesX8664MachineHeader(self) -> None:
        llvmReadelfOutput = (
            f"Machine:                           "
            f"{OS_ELF_AUDIT_EXPECTED_MACHINE}\n"
        )

        self.assertTrue(isExpectedMachine(llvmReadelfOutput))
        self.assertFalse(isExpectedMachine("Machine: AArch64\n"))


if __name__ == "__main__":
    unittest.main()
