from pathlib import Path
import tempfile
import unittest

from tools.os_tools.boot_image import cloneBootImageWithPrefix


OS_TEST_BOOT_IMAGE_CLONE_SIZE_BYTES = 8 * 1024 * 1024
OS_TEST_BOOT_IMAGE_CLONE_DATA_OFFSET_BYTES = 4 * 1024 * 1024
OS_TEST_BOOT_IMAGE_CLONE_ORIGINAL_PREFIX = b"original-prefix"
OS_TEST_BOOT_IMAGE_CLONE_REPLACEMENT_PREFIX = b"replacement"
OS_TEST_BOOT_IMAGE_CLONE_DATA = bytes(range(251)) * 17


class BootImageV5CloneTests(unittest.TestCase):
    def testSparseClonePreservesDataAndReplacesOnlyPrefix(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            temporaryPath = Path(temporaryDirectory)
            sourcePath = temporaryPath / "source.img"
            destinationPath = temporaryPath / "destination.img"
            with sourcePath.open("w+b") as sourceFile:
                sourceFile.truncate(OS_TEST_BOOT_IMAGE_CLONE_SIZE_BYTES)
                sourceFile.write(OS_TEST_BOOT_IMAGE_CLONE_ORIGINAL_PREFIX)
                sourceFile.seek(OS_TEST_BOOT_IMAGE_CLONE_DATA_OFFSET_BYTES)
                sourceFile.write(OS_TEST_BOOT_IMAGE_CLONE_DATA)
            cloneBootImageWithPrefix(
                sourcePath,
                destinationPath,
                OS_TEST_BOOT_IMAGE_CLONE_REPLACEMENT_PREFIX,
                OS_TEST_BOOT_IMAGE_CLONE_SIZE_BYTES,
            )
            self.assertEqual(
                destinationPath.stat().st_size,
                OS_TEST_BOOT_IMAGE_CLONE_SIZE_BYTES,
            )
            with destinationPath.open("rb") as destinationFile:
                self.assertEqual(
                    destinationFile.read(len(OS_TEST_BOOT_IMAGE_CLONE_REPLACEMENT_PREFIX)),
                    OS_TEST_BOOT_IMAGE_CLONE_REPLACEMENT_PREFIX,
                )
                destinationFile.seek(OS_TEST_BOOT_IMAGE_CLONE_DATA_OFFSET_BYTES)
                self.assertEqual(
                    destinationFile.read(len(OS_TEST_BOOT_IMAGE_CLONE_DATA)),
                    OS_TEST_BOOT_IMAGE_CLONE_DATA,
                )


if __name__ == "__main__":
    unittest.main()
