from pathlib import Path
import tempfile
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.qemu_runner import (
    createQemuFirmwareCommand,
    normalizeCapturedOutput,
    validateImageSize,
    validateSerialProtocol,
)


class QemuRunnerToolTests(unittest.TestCase):
    def testAcceptsExpectedImageSize(self) -> None:
        expectedSizeBytes = 64

        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "image.bin"
            imagePath.write_bytes(bytes(expectedSizeBytes))

            validateImageSize(
                imagePath,
                expectedSizeBytes,
                "测试镜像",
            )

    def testRejectsUnexpectedImageSize(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "image.bin"
            imagePath.write_bytes(bytes(32))

            with self.assertRaises(OsToolError):
                validateImageSize(
                    imagePath,
                    64,
                    "测试镜像",
                )

    def testCreatesArchitectureIndependentTcgCommand(self) -> None:
        command = createQemuFirmwareCommand(
            Path("firmware.bin"),
            Path("disk.img"),
        )

        self.assertIn("pc,accel=tcg", command)
        self.assertIn("qemu64", command)
        self.assertIn("firmware.bin", command)
        self.assertNotIn("-kernel", command)

    def testAcceptsRequiredAndAbsentForbiddenMarkers(self) -> None:
        validateSerialProtocol(
            "[OS][FIRMWARE] RESET\r\n",
            ("[OS][FIRMWARE] RESET",),
            ("[OS][FIRMWARE] SERIAL_READY",),
        )

    def testRejectsMissingRequiredMarker(self) -> None:
        with self.assertRaises(OsToolError):
            validateSerialProtocol(
                "",
                ("[OS][FIRMWARE] RESET",),
                (),
            )

    def testRejectsForbiddenMarker(self) -> None:
        with self.assertRaises(OsToolError):
            validateSerialProtocol(
                "[OS][FIRMWARE] SERIAL_READY",
                (),
                ("[OS][FIRMWARE] SERIAL_READY",),
            )

    def testNormalizesByteOutput(self) -> None:
        self.assertEqual(
            normalizeCapturedOutput(b"serial"),
            "serial",
        )


if __name__ == "__main__":
    unittest.main()
