from pathlib import Path
import unittest
from unittest.mock import patch

from tools.os_tools.build import (
    OS_BUILD_CTEST_PARALLEL_JOB_COUNT,
    testProject,
)


class BuildTest(unittest.TestCase):
    @patch("tools.os_tools.build.runCommand")
    def testRunsCompleteCtestInParallel(self, runCommandMock) -> None:
        projectRoot = Path("/tmp/os-test-project")

        testProject(projectRoot)

        runCommandMock.assert_called_once_with(
            [
                "ctest",
                "--preset",
                "developer",
                "--parallel",
                str(OS_BUILD_CTEST_PARALLEL_JOB_COUNT),
            ],
            projectRoot,
        )

    @patch("tools.os_tools.build.runCommand")
    def testRunsCtestInParallel(self, runCommandMock) -> None:
        projectRoot = Path("/tmp/os-test-project")

        testProject(projectRoot, "unit")

        runCommandMock.assert_called_once_with(
            [
                "ctest",
                "--preset",
                "developer",
                "--parallel",
                str(OS_BUILD_CTEST_PARALLEL_JOB_COUNT),
                "--label-regex",
                "^unit$",
            ],
            projectRoot,
        )


if __name__ == "__main__":
    unittest.main()
