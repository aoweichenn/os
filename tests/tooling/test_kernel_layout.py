from pathlib import Path
import tempfile
import unittest

from tools.os_tools.errors import OsToolError
from tools.os_tools.kernel_layout import (
    OS_KERNEL_LAYOUT_EXTRA_IMPLEMENTATION_PATHS,
    OS_KERNEL_LAYOUT_MODULE_NAMES,
    validateKernelSourceLayout,
)


class KernelLayoutToolTests(unittest.TestCase):
    def testAcceptsCurrentRepositoryLayout(self) -> None:
        validateKernelSourceLayout(Path.cwd())

    def testRejectsFlatKernelHeader(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            projectRoot = Path(temporaryDirectory)
            self.createMinimumLayout(projectRoot)
            flatHeaderPath = (
                projectRoot
                / "source/kernel/include/os/kernel/flat.hpp"
            )
            flatHeaderPath.write_text("#pragma once\n", encoding="utf-8")

            with self.assertRaises(OsToolError):
                validateKernelSourceLayout(projectRoot)

    def testRejectsHeaderWithoutMatchingSource(self) -> None:
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            projectRoot = Path(temporaryDirectory)
            self.createMinimumLayout(projectRoot)
            unmatchedHeaderPath = (
                projectRoot
                / "source/kernel/include/os/kernel/memory/unmatched.hpp"
            )
            unmatchedHeaderPath.write_text("#pragma once\n", encoding="utf-8")

            with self.assertRaises(OsToolError):
                validateKernelSourceLayout(projectRoot)

    @staticmethod
    def createMinimumLayout(projectRoot: Path) -> None:
        includeRoot = projectRoot / "source/kernel/include/os/kernel"
        sourceRoot = projectRoot / "source/kernel/src"
        for moduleName in OS_KERNEL_LAYOUT_MODULE_NAMES:
            (includeRoot / moduleName).mkdir(parents=True, exist_ok=True)
            (sourceRoot / moduleName).mkdir(parents=True, exist_ok=True)
        for extraImplementationPath in (
            OS_KERNEL_LAYOUT_EXTRA_IMPLEMENTATION_PATHS
        ):
            implementationPath = sourceRoot / extraImplementationPath
            implementationPath.write_text("", encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
