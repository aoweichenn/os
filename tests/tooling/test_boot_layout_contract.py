from pathlib import Path
import re
import unittest

from tools.os_tools.boot_layout import (
    OS_BOOT_LAYOUT_KERNEL_MAXIMUM_LOAD_END_ADDRESS,
    OS_BOOT_LAYOUT_KERNEL_STAGING_ADDRESS,
    OS_BOOT_LAYOUT_KERNEL_STAGING_CAPACITY_BYTES,
    OS_BOOT_LAYOUT_KERNEL_STAGING_END_ADDRESS,
    OS_BOOT_LAYOUT_ROOTFS_START_BYTES,
    OS_BOOT_LAYOUT_ROOTFS_START_LBA,
    OS_BOOT_LAYOUT_SECTOR_SIZE_BYTES,
)
from tools.os_tools.kernel_image import (
    OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA,
    OS_KERNEL_IMAGE_FILE_SYSTEM_START_LBA,
)
from tools.os_tools.rootfs_v2 import OS_ROOTFS_V2_START_LBA


OS_TEST_BOOT_LAYOUT_PROJECT_ROOT = Path(__file__).resolve().parents[2]
OS_TEST_BOOT_LAYOUT_STAGE1_CONSTANTS_PATH = (
    OS_TEST_BOOT_LAYOUT_PROJECT_ROOT
    / "source/boot/stage1/include/kernel_loader.inc"
)
OS_TEST_BOOT_LAYOUT_BOOT_INFO_PATH = (
    OS_TEST_BOOT_LAYOUT_PROJECT_ROOT
    / "source/kernel/src/boot/boot_info.cpp"
)
OS_TEST_BOOT_LAYOUT_ROOTFS_HEADER_PATH = (
    OS_TEST_BOOT_LAYOUT_PROJECT_ROOT
    / "source/kernel/include/os/kernel/fs/root_file_system_format.hpp"
)
OS_TEST_BOOT_LAYOUT_IDENTITY_MAP_SIZE_BYTES = 0x0400_0000
OS_TEST_BOOT_LAYOUT_KERNEL_STACK_RESERVED_BEGIN = 0x03FE_F000
OS_TEST_BOOT_LAYOUT_EXPECTED_STAGING_CAPACITY_BYTES = 8 * 1024 * 1024


def readAssemblyHexConstant(sourceText: str, constantName: str) -> int:
    match = re.search(
        rf"^{re.escape(constantName)}\s+equ\s+(0x[0-9A-Fa-f]+)$",
        sourceText,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"未找到 Stage 1 布局常量：{constantName}")
    return int(match.group(1), 16)


def readCppIntegerConstant(sourceText: str, constantName: str) -> int:
    match = re.search(
        rf"\b{re.escape(constantName)}\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)ULL;",
        sourceText,
    )
    if match is None:
        raise AssertionError(f"未找到 C++ 布局常量：{constantName}")
    return int(match.group(1), 0)


class BootLayoutContractToolTests(unittest.TestCase):
    def testStage1PythonAndKernelBootInfoUseSameStagingWindow(
        self,
    ) -> None:
        stage1Source = OS_TEST_BOOT_LAYOUT_STAGE1_CONSTANTS_PATH.read_text(
            encoding="utf-8"
        )
        bootInfoSource = OS_TEST_BOOT_LAYOUT_BOOT_INFO_PATH.read_text(
            encoding="utf-8"
        )

        self.assertEqual(
            readAssemblyHexConstant(
                stage1Source,
                "OS_STAGE1_KERNEL_STAGING_ADDRESS",
            ),
            OS_BOOT_LAYOUT_KERNEL_STAGING_ADDRESS,
        )
        self.assertEqual(
            readAssemblyHexConstant(
                stage1Source,
                "OS_STAGE1_KERNEL_STAGING_END_ADDRESS",
            ),
            OS_BOOT_LAYOUT_KERNEL_STAGING_END_ADDRESS,
        )
        self.assertEqual(
            readAssemblyHexConstant(
                stage1Source,
                "OS_STAGE1_ELF_MAXIMUM_LOAD_END_ADDRESS",
            ),
            OS_BOOT_LAYOUT_KERNEL_MAXIMUM_LOAD_END_ADDRESS,
        )
        self.assertEqual(
            readCppIntegerConstant(
                bootInfoSource,
                "OS_KERNEL_BOOT_INFO_KERNEL_FILE_PHYSICAL_ADDRESS",
            ),
            OS_BOOT_LAYOUT_KERNEL_STAGING_ADDRESS,
        )
        self.assertEqual(
            readCppIntegerConstant(
                bootInfoSource,
                "OS_KERNEL_BOOT_INFO_MAXIMUM_KERNEL_FILE_SIZE_BYTES",
            ),
            OS_BOOT_LAYOUT_KERNEL_STAGING_CAPACITY_BYTES,
        )

    def testStagingWindowHasHeadroomAndNoReservedRangeOverlap(
        self,
    ) -> None:
        self.assertEqual(
            OS_BOOT_LAYOUT_KERNEL_STAGING_CAPACITY_BYTES,
            OS_TEST_BOOT_LAYOUT_EXPECTED_STAGING_CAPACITY_BYTES,
        )
        self.assertEqual(
            OS_BOOT_LAYOUT_KERNEL_MAXIMUM_LOAD_END_ADDRESS,
            OS_BOOT_LAYOUT_KERNEL_STAGING_ADDRESS,
        )
        self.assertLessEqual(
            OS_BOOT_LAYOUT_KERNEL_STAGING_END_ADDRESS,
            OS_TEST_BOOT_LAYOUT_KERNEL_STACK_RESERVED_BEGIN,
        )
        self.assertLessEqual(
            OS_BOOT_LAYOUT_KERNEL_STAGING_END_ADDRESS,
            OS_TEST_BOOT_LAYOUT_IDENTITY_MAP_SIZE_BYTES,
        )

    def testDiskPrefixAndRootFileSystemShareBoundary(
        self,
    ) -> None:
        rootfsHeader = OS_TEST_BOOT_LAYOUT_ROOTFS_HEADER_PATH.read_text(
            encoding="utf-8"
        )
        payloadStartBytes = (
            OS_KERNEL_IMAGE_DEFAULT_PAYLOAD_LBA
            * OS_BOOT_LAYOUT_SECTOR_SIZE_BYTES
        )

        self.assertEqual(
            OS_KERNEL_IMAGE_FILE_SYSTEM_START_LBA,
            OS_BOOT_LAYOUT_ROOTFS_START_LBA,
        )
        self.assertEqual(
            OS_ROOTFS_V2_START_LBA,
            OS_BOOT_LAYOUT_ROOTFS_START_LBA,
        )
        self.assertEqual(
            readCppIntegerConstant(
                rootfsHeader,
                "OS_KERNEL_ROOTFS_START_LBA",
            ),
            OS_BOOT_LAYOUT_ROOTFS_START_LBA,
        )
        self.assertGreaterEqual(
            OS_BOOT_LAYOUT_ROOTFS_START_BYTES - payloadStartBytes,
            OS_BOOT_LAYOUT_KERNEL_STAGING_CAPACITY_BYTES,
        )


if __name__ == "__main__":
    unittest.main()
