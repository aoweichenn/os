from pathlib import Path

from .process import runCommand


OS_BUILD_CMAKE_PRESET = "developer"
OS_BUILD_CTEST_PRESET = "developer"
OS_BUILD_CTEST_PARALLEL_JOB_COUNT = 20
OS_BUILD_TEST_LAYERS = (
    "unit",
    "integration",
    "randomized",
    "system",
    "failure-path",
)


def configureProject(projectRoot: Path) -> None:
    runCommand(
        [
            "cmake",
            "--preset",
            OS_BUILD_CMAKE_PRESET,
            "-S",
            str(projectRoot),
        ],
        projectRoot,
    )


def buildProject(projectRoot: Path) -> None:
    runCommand(
        [
            "cmake",
            "--build",
            "--preset",
            OS_BUILD_CMAKE_PRESET,
        ],
        projectRoot,
    )


def testProject(projectRoot: Path, layer: str | None = None) -> None:
    command = [
        "ctest",
        "--preset",
        OS_BUILD_CTEST_PRESET,
        "--parallel",
        str(OS_BUILD_CTEST_PARALLEL_JOB_COUNT),
    ]
    if layer is not None:
        command.extend(["--label-regex", f"^{layer}$"])
    runCommand(command, projectRoot)
