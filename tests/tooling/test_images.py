from pathlib import Path
import tempfile
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.images import (
    OS_IMAGES_EMPTY_DISK_FILE_NAME,
    OS_IMAGES_EMPTY_FIRMWARE_FILE_NAME,
    createEmptyImages,
)


class ImageToolTests(unittest.TestCase):
    def testCreatesImagesWithRequestedSizes(self) -> None:
        firmwareSizeBytes = 128
        diskSizeBytes = 512

        with tempfile.TemporaryDirectory() as temporaryDirectory:
            outputDirectory = Path(temporaryDirectory)
            createEmptyImages(
                outputDirectory,
                firmwareSizeBytes,
                diskSizeBytes,
            )

            self.assertEqual(
                (outputDirectory / OS_IMAGES_EMPTY_FIRMWARE_FILE_NAME)
                .stat()
                .st_size,
                firmwareSizeBytes,
            )
            self.assertEqual(
                (outputDirectory / OS_IMAGES_EMPTY_DISK_FILE_NAME)
                .stat()
                .st_size,
                diskSizeBytes,
            )

    def testRejectsNegativeImageSize(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            with self.assertRaises(OsToolError):
                createEmptyImages(
                    Path(temporaryDirectory),
                    -1,
                    512,
                )


if __name__ == "__main__":
    unittest.main()
