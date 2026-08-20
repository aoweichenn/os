from pathlib import Path
import re
import tempfile
import unittest

from tools.os_tools.swap_image import (
    OS_SWAP_IMAGE_DATA_SIZE_BYTES,
    OS_SWAP_IMAGE_ENTRY_SIZE_BYTES,
    OS_SWAP_IMAGE_SIZE_BYTES,
    OS_SWAP_IMAGE_SLOT_CAPACITY,
    auditSwapImage,
    writeSwapImage,
)


class SwapImageToolTests(unittest.TestCase):
    def testCreatesChecked28GibSwapGeometry(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "swap.img"
            writeSwapImage(imagePath)

            auditSwapImage(imagePath)
            self.assertEqual(
                OS_SWAP_IMAGE_DATA_SIZE_BYTES,
                28 * 1024 * 1024 * 1024,
            )
            self.assertEqual(
                OS_SWAP_IMAGE_SLOT_CAPACITY,
                28 * 1024 * 1024 // 4,
            )
            self.assertEqual(imagePath.stat().st_size, OS_SWAP_IMAGE_SIZE_BYTES)
            self.assertLess(imagePath.stat().st_blocks * 512, 1024 * 1024)

    def testPythonKernelAndCmakeUseSameGeometry(self) -> None:
        projectRoot = Path(__file__).resolve().parents[2]
        header = (
            projectRoot
            / "source/kernel/include/os/kernel/memory/swap_storage.hpp"
        ).read_text(encoding="utf-8")
        cmake = (projectRoot / "CMakeLists.txt").read_text(encoding="utf-8")
        dataSizeMatch = re.search(
            r"OS_KERNEL_SWAP_STORAGE_DATA_SIZE_BYTES\s*=\s*([0-9]+)ULL;",
            header,
        )
        entrySizeMatch = re.search(
            r"OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES\s*=\s*([0-9]+)ULL;",
            header,
        )
        imageSizeMatch = re.search(
            r"set\(OS_SWAP_DISK_IMAGE_SIZE_BYTES ([0-9]+)\)",
            cmake,
        )
        self.assertIsNotNone(dataSizeMatch)
        self.assertIsNotNone(entrySizeMatch)
        self.assertIsNotNone(imageSizeMatch)
        assert dataSizeMatch is not None
        assert entrySizeMatch is not None
        assert imageSizeMatch is not None
        self.assertEqual(int(dataSizeMatch.group(1)), OS_SWAP_IMAGE_DATA_SIZE_BYTES)
        self.assertEqual(int(entrySizeMatch.group(1)), OS_SWAP_IMAGE_ENTRY_SIZE_BYTES)
        self.assertEqual(int(imageSizeMatch.group(1)), OS_SWAP_IMAGE_SIZE_BYTES)
