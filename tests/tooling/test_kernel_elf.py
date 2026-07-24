import struct
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.kernel_elf import (
    OS_KERNEL_ELF_CLASS_64,
    OS_KERNEL_ELF_DATA_LITTLE_ENDIAN,
    OS_KERNEL_ELF_EXPECTED_ENTRY_ADDRESS,
    OS_KERNEL_ELF_HEADER_FORMAT,
    OS_KERNEL_ELF_HEADER_SIZE_BYTES,
    OS_KERNEL_ELF_IDENT_CLASS_OFFSET,
    OS_KERNEL_ELF_IDENT_DATA_OFFSET,
    OS_KERNEL_ELF_IDENT_VERSION_OFFSET,
    OS_KERNEL_ELF_MACHINE_X86_64,
    OS_KERNEL_ELF_MAXIMUM_LOAD_END_ADDRESS,
    OS_KERNEL_ELF_MINIMUM_LOAD_ADDRESS,
    OS_KERNEL_ELF_MAGIC,
    OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE,
    OS_KERNEL_ELF_PROGRAM_FLAG_READ,
    OS_KERNEL_ELF_PROGRAM_FLAG_WRITE,
    OS_KERNEL_ELF_PROGRAM_HEADER_FORMAT,
    OS_KERNEL_ELF_PROGRAM_HEADER_SIZE_BYTES,
    OS_KERNEL_ELF_PROGRAM_TYPE_LOAD,
    OS_KERNEL_ELF_TYPE_EXECUTABLE,
    OS_KERNEL_ELF_VERSION_CURRENT,
    parseKernelLoadSegments,
    validateKernelArchitectureSymbols,
    validateKernelEntry,
)


OS_TEST_KERNEL_ELF_IDENTIFICATION_SIZE_BYTES = 16
OS_TEST_KERNEL_ELF_PAYLOAD_SIZE_BYTES = 1
OS_TEST_KERNEL_ELF_PROGRAM_HEADER_COUNT = 1
OS_TEST_KERNEL_ELF_PAGE_ALIGNMENT_BYTES = 0x1000
OS_TEST_KERNEL_ELF_REQUIRED_SYMBOLS = {
    "osKernelEntry",
    "osKernelLoadGdtAndTss",
    "osKernelLoadIdt",
    "osKernelExceptionDispatch",
    "osKernelDispatchException",
    "osKernelExceptionStubTable",
    "osKernelImageStart",
    "osKernelImageEnd",
    "osKernelTextStart",
    "osKernelTextEnd",
    "osKernelReadOnlyDataStart",
    "osKernelReadOnlyDataEnd",
    "osKernelWritableDataStart",
    "osKernelWritableDataEnd",
    *(f"os_kernel_exception_vector_{vector}" for vector in range(32)),
}


def createValidKernelElf(
    *,
    entryAddress: int = OS_KERNEL_ELF_EXPECTED_ENTRY_ADDRESS,
    programFlags: int = (
        OS_KERNEL_ELF_PROGRAM_FLAG_READ
        | OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE
    ),
    payloadOffset: int = OS_TEST_KERNEL_ELF_PAGE_ALIGNMENT_BYTES,
    virtualAddress: int = OS_KERNEL_ELF_EXPECTED_ENTRY_ADDRESS,
    physicalAddress: int = OS_KERNEL_ELF_EXPECTED_ENTRY_ADDRESS,
    alignmentBytes: int = OS_TEST_KERNEL_ELF_PAGE_ALIGNMENT_BYTES,
) -> bytes:
    identification = bytearray(OS_TEST_KERNEL_ELF_IDENTIFICATION_SIZE_BYTES)
    identification[: len(OS_KERNEL_ELF_MAGIC)] = OS_KERNEL_ELF_MAGIC
    identification[OS_KERNEL_ELF_IDENT_CLASS_OFFSET] = OS_KERNEL_ELF_CLASS_64
    identification[OS_KERNEL_ELF_IDENT_DATA_OFFSET] = (
        OS_KERNEL_ELF_DATA_LITTLE_ENDIAN
    )
    identification[OS_KERNEL_ELF_IDENT_VERSION_OFFSET] = (
        OS_KERNEL_ELF_VERSION_CURRENT
    )

    programHeaderOffset = OS_KERNEL_ELF_HEADER_SIZE_BYTES
    elfHeader = struct.pack(
        OS_KERNEL_ELF_HEADER_FORMAT,
        bytes(identification),
        OS_KERNEL_ELF_TYPE_EXECUTABLE,
        OS_KERNEL_ELF_MACHINE_X86_64,
        OS_KERNEL_ELF_VERSION_CURRENT,
        entryAddress,
        programHeaderOffset,
        0,
        0,
        OS_KERNEL_ELF_HEADER_SIZE_BYTES,
        OS_KERNEL_ELF_PROGRAM_HEADER_SIZE_BYTES,
        OS_TEST_KERNEL_ELF_PROGRAM_HEADER_COUNT,
        0,
        0,
        0,
    )
    programHeader = struct.pack(
        OS_KERNEL_ELF_PROGRAM_HEADER_FORMAT,
        OS_KERNEL_ELF_PROGRAM_TYPE_LOAD,
        programFlags,
        payloadOffset,
        virtualAddress,
        physicalAddress,
        OS_TEST_KERNEL_ELF_PAYLOAD_SIZE_BYTES,
        OS_TEST_KERNEL_ELF_PAYLOAD_SIZE_BYTES,
        alignmentBytes,
    )
    headerPaddingSizeBytes = payloadOffset - len(elfHeader) - len(programHeader)
    return (
        elfHeader
        + programHeader
        + bytes(headerPaddingSizeBytes)
        + bytes(OS_TEST_KERNEL_ELF_PAYLOAD_SIZE_BYTES)
    )


class KernelElfToolTests(unittest.TestCase):
    def testAcceptsValidElf64LoadSegment(self) -> None:
        entryAddress, loadSegments = parseKernelLoadSegments(
            createValidKernelElf()
        )

        self.assertEqual(entryAddress, OS_KERNEL_ELF_EXPECTED_ENTRY_ADDRESS)
        self.assertEqual(len(loadSegments), 1)
        validateKernelEntry(entryAddress, loadSegments)

    def testRejectsTruncatedElfHeader(self) -> None:
        with self.assertRaises(OsToolError):
            parseKernelLoadSegments(b"ELF")

    def testRejectsEntryOutsideExecutableSegment(self) -> None:
        entryAddress, loadSegments = parseKernelLoadSegments(
            createValidKernelElf()
        )

        with self.assertRaises(OsToolError):
            validateKernelEntry(entryAddress + 1, loadSegments)

    def testRejectsWritableExecutableSegment(self) -> None:
        invalidFlags = (
            OS_KERNEL_ELF_PROGRAM_FLAG_READ
            | OS_KERNEL_ELF_PROGRAM_FLAG_WRITE
            | OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE
        )

        with self.assertRaises(OsToolError):
            parseKernelLoadSegments(
                createValidKernelElf(programFlags=invalidFlags)
            )

    def testRejectsSegmentWithoutReadPermission(self) -> None:
        with self.assertRaises(OsToolError):
            parseKernelLoadSegments(
                createValidKernelElf(
                    programFlags=OS_KERNEL_ELF_PROGRAM_FLAG_EXECUTE
                )
            )

    def testRejectsNonPageAlignedSegment(self) -> None:
        with self.assertRaises(OsToolError):
            parseKernelLoadSegments(
                createValidKernelElf(alignmentBytes=1)
            )

    def testRejectsNonIdentityMappedSegment(self) -> None:
        with self.assertRaises(OsToolError):
            parseKernelLoadSegments(
                createValidKernelElf(
                    physicalAddress=(
                        OS_KERNEL_ELF_EXPECTED_ENTRY_ADDRESS
                        + OS_TEST_KERNEL_ELF_PAGE_ALIGNMENT_BYTES
                    )
                )
            )

    def testRejectsSegmentBelowTargetLoadArea(self) -> None:
        invalidAddress = OS_KERNEL_ELF_MINIMUM_LOAD_ADDRESS - (
            OS_TEST_KERNEL_ELF_PAGE_ALIGNMENT_BYTES
        )

        with self.assertRaises(OsToolError):
            parseKernelLoadSegments(
                createValidKernelElf(
                    entryAddress=invalidAddress,
                    virtualAddress=invalidAddress,
                    physicalAddress=invalidAddress,
                )
            )

    def testRejectsSegmentPastTargetLoadArea(self) -> None:
        invalidAddress = OS_KERNEL_ELF_MAXIMUM_LOAD_END_ADDRESS

        with self.assertRaises(OsToolError):
            parseKernelLoadSegments(
                createValidKernelElf(
                    entryAddress=invalidAddress,
                    virtualAddress=invalidAddress,
                    physicalAddress=invalidAddress,
                )
            )

    def testAcceptsCompleteKernelArchitectureSymbols(self) -> None:
        validateKernelArchitectureSymbols(
            set(OS_TEST_KERNEL_ELF_REQUIRED_SYMBOLS)
        )

    def testRejectsMissingExceptionVectorSymbol(self) -> None:
        incompleteSymbols = set(OS_TEST_KERNEL_ELF_REQUIRED_SYMBOLS)
        incompleteSymbols.remove("os_kernel_exception_vector_31")

        with self.assertRaises(OsToolError):
            validateKernelArchitectureSymbols(incompleteSymbols)


if __name__ == "__main__":
    unittest.main()
