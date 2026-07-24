#!/usr/bin/env python3

import argparse
from collections.abc import Callable, Sequence
from pathlib import Path
import subprocess
import sys
from typing import Protocol

from os_tools.build import (
    OS_BUILD_TEST_LAYERS,
    buildProject,
    configureProject,
    testProject,
)
from os_tools.elf_audit import auditFreestandingLibrary
from os_tools.errors import OsToolError
from os_tools.firmware_audit import auditFirmwareImage
from os_tools.images import createEmptyImages
from os_tools.qemu_runner import (
    OS_QEMU_FIRMWARE_RESET_MARKER,
    OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
    runQemuFirmwareBoot,
    runQemuHardwareSmoke,
)
from os_tools.toolchain import checkToolchain


OS_TOOL_PROJECT_ROOT = Path(__file__).resolve().parent.parent
OS_TOOL_MINIMUM_PYTHON_VERSION = (3, 11)


class SubparserCollection(Protocol):
    def add_parser(
        self,
        name: str,
        *,
        help: str,
    ) -> argparse.ArgumentParser: ...


def handleDoctor(_: argparse.Namespace) -> None:
    checkToolchain(OS_TOOL_PROJECT_ROOT)


def handleConfigure(_: argparse.Namespace) -> None:
    configureProject(OS_TOOL_PROJECT_ROOT)


def handleBuild(_: argparse.Namespace) -> None:
    buildProject(OS_TOOL_PROJECT_ROOT)


def handleTest(arguments: argparse.Namespace) -> None:
    testProject(OS_TOOL_PROJECT_ROOT, arguments.layer)


def handleVerify(_: argparse.Namespace) -> None:
    checkToolchain(OS_TOOL_PROJECT_ROOT)
    configureProject(OS_TOOL_PROJECT_ROOT)
    buildProject(OS_TOOL_PROJECT_ROOT)
    testProject(OS_TOOL_PROJECT_ROOT)


def handleCreateImages(arguments: argparse.Namespace) -> None:
    createEmptyImages(
        arguments.outputDirectory,
        arguments.firmwareSizeBytes,
        arguments.diskSizeBytes,
    )


def handleAuditElf(arguments: argparse.Namespace) -> None:
    auditFreestandingLibrary(OS_TOOL_PROJECT_ROOT, arguments.libraryPath)


def handleAuditFirmware(arguments: argparse.Namespace) -> None:
    auditFirmwareImage(arguments.firmwareImagePath)


def handleQemuSmoke(arguments: argparse.Namespace) -> None:
    runQemuHardwareSmoke(
        OS_TOOL_PROJECT_ROOT,
        arguments.firmwareImagePath,
        arguments.diskImagePath,
        arguments.expectedFirmwareSizeBytes,
        arguments.expectedDiskSizeBytes,
    )


def handleQemuFirmware(arguments: argparse.Namespace) -> None:
    if arguments.expectedOutcome == "success":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
        )
        forbiddenMarkers: tuple[str, ...] = ()
    else:
        requiredMarkers = (OS_QEMU_FIRMWARE_RESET_MARKER,)
        forbiddenMarkers = (OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,)

    runQemuFirmwareBoot(
        OS_TOOL_PROJECT_ROOT,
        arguments.firmwareImagePath,
        arguments.diskImagePath,
        arguments.expectedFirmwareSizeBytes,
        arguments.expectedDiskSizeBytes,
        requiredMarkers,
        forbiddenMarkers,
    )


def addCommand(
    subparsers: SubparserCollection,
    commandName: str,
    helpText: str,
    handler: Callable[[argparse.Namespace], None],
) -> argparse.ArgumentParser:
    commandParser = subparsers.add_parser(commandName, help=helpText)
    commandParser.set_defaults(handler=handler)
    return commandParser


def createArgumentParser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="x86-64 OS Lab 构建、测试和 QEMU 调度入口"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    addCommand(subparsers, "doctor", "检查宿主工具链", handleDoctor)
    addCommand(subparsers, "configure", "配置开发构建", handleConfigure)
    addCommand(subparsers, "build", "构建全部目标", handleBuild)

    testParser = addCommand(subparsers, "test", "运行 CTest", handleTest)
    testParser.add_argument(
        "--layer",
        choices=OS_BUILD_TEST_LAYERS,
        help="只运行指定测试层",
    )

    addCommand(subparsers, "verify", "执行完整构建与测试", handleVerify)

    imageParser = addCommand(
        subparsers,
        "create-images",
        "生成指定大小的空固件和磁盘镜像",
        handleCreateImages,
    )
    imageParser.add_argument(
        "--output-directory",
        dest="outputDirectory",
        type=Path,
        required=True,
    )
    imageParser.add_argument(
        "--firmware-size-bytes",
        dest="firmwareSizeBytes",
        type=int,
        required=True,
    )
    imageParser.add_argument(
        "--disk-size-bytes",
        dest="diskSizeBytes",
        type=int,
        required=True,
    )

    elfAuditParser = addCommand(
        subparsers,
        "audit-elf",
        "检查 freestanding 静态库的架构和未解析符号",
        handleAuditElf,
    )
    elfAuditParser.add_argument(
        "libraryPath",
        type=Path,
    )

    firmwareAuditParser = addCommand(
        subparsers,
        "audit-firmware",
        "检查 128 KiB ROM、复位向量和固件入口",
        handleAuditFirmware,
    )
    firmwareAuditParser.add_argument("firmwareImagePath", type=Path)

    qemuParser = addCommand(
        subparsers,
        "qemu-smoke",
        "运行 QEMU TCG 硬件冒烟测试",
        handleQemuSmoke,
    )
    qemuParser.add_argument("firmwareImagePath", type=Path)
    qemuParser.add_argument("diskImagePath", type=Path)
    qemuParser.add_argument("expectedFirmwareSizeBytes", type=int)
    qemuParser.add_argument("expectedDiskSizeBytes", type=int)

    qemuFirmwareParser = addCommand(
        subparsers,
        "qemu-firmware",
        "运行并验收自研固件的串口协议",
        handleQemuFirmware,
    )
    qemuFirmwareParser.add_argument("firmwareImagePath", type=Path)
    qemuFirmwareParser.add_argument("diskImagePath", type=Path)
    qemuFirmwareParser.add_argument("expectedFirmwareSizeBytes", type=int)
    qemuFirmwareParser.add_argument("expectedDiskSizeBytes", type=int)
    qemuFirmwareParser.add_argument(
        "--expected-outcome",
        choices=("success", "serial-failure"),
        required=True,
        dest="expectedOutcome",
    )
    return parser


def main(arguments: Sequence[str] | None = None) -> int:
    if sys.version_info < OS_TOOL_MINIMUM_PYTHON_VERSION:
        requiredVersion = ".".join(
            str(component) for component in OS_TOOL_MINIMUM_PYTHON_VERSION
        )
        print(f"需要 Python {requiredVersion} 或更高版本。", file=sys.stderr)
        return 1

    parser = createArgumentParser()
    parsedArguments = parser.parse_args(arguments)

    try:
        parsedArguments.handler(parsedArguments)
    except (OsToolError, subprocess.CalledProcessError) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
