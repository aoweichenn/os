from pathlib import Path
import tempfile
import unittest

from tools.os_tools.allocated_image import materializeImage, requireAllocatedImage
from tools.os_tools.sparse_image import writeSparseImage


class AllocatedImageToolTests(unittest.TestCase):
    def testMaterializesEveryHostBlockWithoutChangingContent(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            sourcePath = Path(temporaryDirectory) / "source.img"
            destinationPath = Path(temporaryDirectory) / "allocated.img"
            prefix = b"allocated-image-test"
            writeSparseImage(sourcePath, prefix, 4 * 1024 * 1024)

            materializeImage(sourcePath, destinationPath)
            requireAllocatedImage(destinationPath)
            self.assertEqual(destinationPath.read_bytes()[:len(prefix)], prefix)
