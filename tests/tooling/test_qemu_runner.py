from pathlib import Path
import tempfile
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.qemu_runner import validateImageSize


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


if __name__ == "__main__":
    unittest.main()
