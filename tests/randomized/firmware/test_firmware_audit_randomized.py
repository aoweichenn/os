import random
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.firmware_audit import (
    OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET,
    OS_FIRMWARE_AUDIT_FONT_FILE_OFFSET,
    OS_FIRMWARE_AUDIT_FONT_PREFIX,
    OS_FIRMWARE_AUDIT_NEAR_JUMP_OPCODE,
    OS_FIRMWARE_AUDIT_RESET_VECTOR_FILE_OFFSET,
    OS_FIRMWARE_AUDIT_ROM_SIZE_BYTES,
    auditFirmwareImageBytes,
)


OS_TEST_FIRMWARE_RANDOM_SEED = 0x5EED_0001
OS_TEST_FIRMWARE_RANDOM_CASE_COUNT = 256
OS_TEST_FIRMWARE_RESET_DISPLACEMENT = -4083
OS_TEST_FIRMWARE_MUTABLE_DISPLACEMENT_MIN = -32768
OS_TEST_FIRMWARE_MUTABLE_DISPLACEMENT_MAX = 32767


def createValidFirmwareImage() -> bytearray:
    firmwareImage = bytearray(
        (0xFF,) * OS_FIRMWARE_AUDIT_ROM_SIZE_BYTES
    )
    firmwareImage[
        OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET:
        OS_FIRMWARE_AUDIT_ENTRY_FILE_OFFSET + 2
    ] = bytes((0xFA, 0xFC))
    firmwareImage[
        OS_FIRMWARE_AUDIT_FONT_FILE_OFFSET:
        OS_FIRMWARE_AUDIT_FONT_FILE_OFFSET + len(OS_FIRMWARE_AUDIT_FONT_PREFIX)
    ] = OS_FIRMWARE_AUDIT_FONT_PREFIX
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
    return firmwareImage


class FirmwareAuditRandomizedTests(unittest.TestCase):
    def testRejectsRandomWrongResetTargets(self) -> None:
        generator = random.Random(OS_TEST_FIRMWARE_RANDOM_SEED)

        for _ in range(OS_TEST_FIRMWARE_RANDOM_CASE_COUNT):
            displacement = generator.randint(
                OS_TEST_FIRMWARE_MUTABLE_DISPLACEMENT_MIN,
                OS_TEST_FIRMWARE_MUTABLE_DISPLACEMENT_MAX,
            )
            if displacement == OS_TEST_FIRMWARE_RESET_DISPLACEMENT:
                displacement += 1

            firmwareImage = createValidFirmwareImage()
            resetVectorOffset = (
                OS_FIRMWARE_AUDIT_RESET_VECTOR_FILE_OFFSET
            )
            firmwareImage[
                resetVectorOffset + 1:resetVectorOffset + 3
            ] = displacement.to_bytes(
                length=2,
                byteorder="little",
                signed=True,
            )

            with self.subTest(displacement=displacement):
                with self.assertRaises(OsToolError):
                    auditFirmwareImageBytes(bytes(firmwareImage))


if __name__ == "__main__":
    unittest.main()
