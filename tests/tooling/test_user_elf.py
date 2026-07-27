from pathlib import Path
import re
import struct
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.user_elf import (
    OS_USER_ELF_CLASS_64,
    OS_USER_ELF_DATA_LITTLE_ENDIAN,
    OS_USER_ELF_HEADER_FORMAT,
    OS_USER_ELF_HEADER_SIZE_BYTES,
    OS_USER_ELF_IDENT_CLASS_OFFSET,
    OS_USER_ELF_IDENT_DATA_OFFSET,
    OS_USER_ELF_IDENT_VERSION_OFFSET,
    OS_USER_ELF_MACHINE_X86_64,
    OS_USER_ELF_MAGIC,
    OS_USER_ELF_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE,
    OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT,
    OS_USER_ELF_MINIMUM_VIRTUAL_ADDRESS,
    OS_USER_ELF_PAGE_SIZE_BYTES,
    OS_USER_ELF_PROGRAM_FLAG_EXECUTE,
    OS_USER_ELF_PROGRAM_FLAG_READ,
    OS_USER_ELF_PROGRAM_FLAG_WRITE,
    OS_USER_ELF_PROGRAM_HEADER_FORMAT,
    OS_USER_ELF_PROGRAM_HEADER_SIZE_BYTES,
    OS_USER_ELF_PROGRAM_TYPE_LOAD,
    OS_USER_ELF_TYPE_EXECUTABLE,
    OS_USER_ELF_VERSION_CURRENT,
    parseUserLoadSegments,
)


OS_TEST_USER_ELF_IDENTIFICATION_SIZE_BYTES = 16
OS_TEST_USER_ELF_ENTRY_ADDRESS = 0x4000_0000
OS_TEST_USER_ELF_PAYLOAD_SIZE_BYTES = 1
OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT = 1
OS_TEST_USER_ELF_KERNEL_HEADER_PATH = (
    Path(__file__).resolve().parents[2]
    / "source/kernel/include/os/kernel/user/user_elf.hpp"
)
OS_TEST_USER_ELF_KERNEL_MAPPED_PAGE_LIMIT_PATTERN = re.compile(
    r"OS_KERNEL_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT\s*=\s*(\d+)ULL;"
)


def createValidUserElf(
    *,
    entryAddress: int = OS_TEST_USER_ELF_ENTRY_ADDRESS,
    virtualAddress: int = OS_TEST_USER_ELF_ENTRY_ADDRESS,
    physicalAddress: int = OS_TEST_USER_ELF_ENTRY_ADDRESS,
    programFlags: int = (
        OS_USER_ELF_PROGRAM_FLAG_READ
        | OS_USER_ELF_PROGRAM_FLAG_EXECUTE
    ),
    alignmentBytes: int = OS_USER_ELF_PAGE_SIZE_BYTES,
    memorySizeBytes: int = OS_USER_ELF_PAGE_SIZE_BYTES,
) -> bytes:
    identification = bytearray(OS_TEST_USER_ELF_IDENTIFICATION_SIZE_BYTES)
    identification[: len(OS_USER_ELF_MAGIC)] = OS_USER_ELF_MAGIC
    identification[OS_USER_ELF_IDENT_CLASS_OFFSET] = OS_USER_ELF_CLASS_64
    identification[OS_USER_ELF_IDENT_DATA_OFFSET] = (
        OS_USER_ELF_DATA_LITTLE_ENDIAN
    )
    identification[OS_USER_ELF_IDENT_VERSION_OFFSET] = (
        OS_USER_ELF_VERSION_CURRENT
    )
    payloadOffset = OS_USER_ELF_PAGE_SIZE_BYTES
    header = struct.pack(
        OS_USER_ELF_HEADER_FORMAT,
        bytes(identification),
        OS_USER_ELF_TYPE_EXECUTABLE,
        OS_USER_ELF_MACHINE_X86_64,
        OS_USER_ELF_VERSION_CURRENT,
        entryAddress,
        OS_USER_ELF_HEADER_SIZE_BYTES,
        0,
        0,
        OS_USER_ELF_HEADER_SIZE_BYTES,
        OS_USER_ELF_PROGRAM_HEADER_SIZE_BYTES,
        OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT,
        0,
        0,
        0,
    )
    programHeader = struct.pack(
        OS_USER_ELF_PROGRAM_HEADER_FORMAT,
        OS_USER_ELF_PROGRAM_TYPE_LOAD,
        programFlags,
        payloadOffset,
        virtualAddress,
        physicalAddress,
        OS_TEST_USER_ELF_PAYLOAD_SIZE_BYTES,
        memorySizeBytes,
        alignmentBytes,
    )
    paddingSizeBytes = payloadOffset - len(header) - len(programHeader)
    return (
        header
        + programHeader
        + bytes(paddingSizeBytes)
        + bytes(OS_TEST_USER_ELF_PAYLOAD_SIZE_BYTES)
    )


class UserElfToolTests(unittest.TestCase):
    def testMappedPageLimitMatchesKernelContract(self) -> None:
        kernelHeader = OS_TEST_USER_ELF_KERNEL_HEADER_PATH.read_text(
            encoding="utf-8"
        )
        mappedPageLimitMatch = (
            OS_TEST_USER_ELF_KERNEL_MAPPED_PAGE_LIMIT_PATTERN.search(
                kernelHeader
            )
        )

        self.assertIsNotNone(mappedPageLimitMatch)
        assert mappedPageLimitMatch is not None
        self.assertEqual(
            int(mappedPageLimitMatch.group(1)),
            OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT,
        )

    def testAcceptsValidUserElf(self) -> None:
        entryAddress, loadSegments = parseUserLoadSegments(
            createValidUserElf()
        )

        self.assertEqual(entryAddress, OS_TEST_USER_ELF_ENTRY_ADDRESS)
        self.assertEqual(len(loadSegments), 1)

    def testRejectsTruncatedHeader(self) -> None:
        with self.assertRaises(OsToolError):
            parseUserLoadSegments(b"ELF")

    def testRejectsWritableExecutableSegment(self) -> None:
        with self.assertRaises(OsToolError):
            parseUserLoadSegments(
                createValidUserElf(
                    programFlags=(
                        OS_USER_ELF_PROGRAM_FLAG_READ
                        | OS_USER_ELF_PROGRAM_FLAG_WRITE
                        | OS_USER_ELF_PROGRAM_FLAG_EXECUTE
                    )
                )
            )

    def testRejectsUnalignedSegment(self) -> None:
        invalidAddress = OS_TEST_USER_ELF_ENTRY_ADDRESS + 1

        with self.assertRaises(OsToolError):
            parseUserLoadSegments(
                createValidUserElf(
                    entryAddress=invalidAddress,
                    virtualAddress=invalidAddress,
                    physicalAddress=invalidAddress,
                )
            )

    def testRejectsSegmentBelowUserBoundary(self) -> None:
        invalidAddress = (
            OS_USER_ELF_MINIMUM_VIRTUAL_ADDRESS
            - OS_USER_ELF_PAGE_SIZE_BYTES
        )

        with self.assertRaises(OsToolError):
            parseUserLoadSegments(
                createValidUserElf(
                    entryAddress=invalidAddress,
                    virtualAddress=invalidAddress,
                    physicalAddress=invalidAddress,
                )
            )

    def testRejectsSegmentPastProcessProgramWindow(self) -> None:
        invalidAddress = OS_USER_ELF_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE

        with self.assertRaises(OsToolError):
            parseUserLoadSegments(
                createValidUserElf(
                    entryAddress=invalidAddress,
                    virtualAddress=invalidAddress,
                    physicalAddress=invalidAddress,
                )
            )

    def testRejectsTooManyMappedPages(self) -> None:
        with self.assertRaises(OsToolError):
            parseUserLoadSegments(
                createValidUserElf(
                    memorySizeBytes=(
                        OS_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT + 1
                    )
                    * OS_USER_ELF_PAGE_SIZE_BYTES
                )
            )

    def testRejectsEntryOutsideExecutableSegment(self) -> None:
        with self.assertRaises(OsToolError):
            parseUserLoadSegments(
                createValidUserElf(
                    entryAddress=(
                        OS_TEST_USER_ELF_ENTRY_ADDRESS
                        + OS_USER_ELF_PAGE_SIZE_BYTES
                    )
                )
            )

    def testRejectsNonIdentityAddress(self) -> None:
        with self.assertRaises(OsToolError):
            parseUserLoadSegments(
                createValidUserElf(
                    physicalAddress=(
                        OS_TEST_USER_ELF_ENTRY_ADDRESS
                        + OS_USER_ELF_PAGE_SIZE_BYTES
                    )
                )
            )


if __name__ == "__main__":
    unittest.main()
