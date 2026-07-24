import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.firmware_audit import (
    OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET,
    OS_FIRMWARE_AUDIT_NEAR_JUMP_OPCODE,
    OS_FIRMWARE_AUDIT_RESET_VECTOR_FILE_OFFSET,
    OS_FIRMWARE_AUDIT_ROM_SIZE_BYTES,
    auditFirmwareImageBytes,
    decodeResetVectorTarget,
)


OS_TEST_FIRMWARE_RESET_DISPLACEMENT = -4083


def createValidFirmwareImage() -> bytes:
    firmwareImage = bytearray(
        (0xFF,) * OS_FIRMWARE_AUDIT_ROM_SIZE_BYTES
    )
    firmwareImage[
        OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET:
        OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET + 2
    ] = bytes((0xFA, 0xFC))
    resetVectorOffset = OS_FIRMWARE_AUDIT_RESET_VECTOR_FILE_OFFSET
    firmwareImage[resetVectorOffset] = (
        OS_FIRMWARE_AUDIT_NEAR_JUMP_OPCODE
    )
    firmwareImage[
        resetVectorOffset + 1:resetVectorOffset + 3
    ] = OS_TEST_FIRMWARE_RESET_DISPLACEMENT.to_bytes(
        length=2,
        byteorder="little",
        signed=True,
    )
    return bytes(firmwareImage)


class FirmwareAuditToolTests(unittest.TestCase):
    def testAcceptsValidRomLayout(self) -> None:
        auditFirmwareImageBytes(createValidFirmwareImage())

    def testDecodesResetTarget(self) -> None:
        self.assertEqual(
            decodeResetVectorTarget(createValidFirmwareImage()),
            0xF000,
        )

    def testRejectsWrongRomSize(self) -> None:
        with self.assertRaises(OsToolError):
            auditFirmwareImageBytes(bytes(64))

    def testRejectsWrongResetOpcode(self) -> None:
        firmwareImage = bytearray(createValidFirmwareImage())
        firmwareImage[
            OS_FIRMWARE_AUDIT_RESET_VECTOR_FILE_OFFSET
        ] = 0x90

        with self.assertRaises(OsToolError):
            auditFirmwareImageBytes(bytes(firmwareImage))

    def testRejectsWrongResetTarget(self) -> None:
        firmwareImage = bytearray(createValidFirmwareImage())
        resetVectorOffset = OS_FIRMWARE_AUDIT_RESET_VECTOR_FILE_OFFSET
        firmwareImage[
            resetVectorOffset + 1:resetVectorOffset + 3
        ] = (0).to_bytes(length=2, byteorder="little", signed=True)

        with self.assertRaises(OsToolError):
            auditFirmwareImageBytes(bytes(firmwareImage))

    def testRejectsWrongEntryPrefix(self) -> None:
        firmwareImage = bytearray(createValidFirmwareImage())
        firmwareImage[OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET] = 0x90

        with self.assertRaises(OsToolError):
            auditFirmwareImageBytes(bytes(firmwareImage))


if __name__ == "__main__":
    unittest.main()
