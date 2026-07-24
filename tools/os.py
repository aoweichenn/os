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
from os_tools.book_export import exportBookToPhone
from os_tools.boot_image import writeBootDiskImages
from os_tools.elf_audit import auditFreestandingLibrary
from os_tools.errors import OsToolError
from os_tools.firmware_audit import auditFirmwareImage
from os_tools.images import createEmptyImages
from os_tools.kernel_elf import auditKernelElf
from os_tools.kernel_image import auditKernelDiskImage
from os_tools.qemu_runner import (
    OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
    OS_QEMU_FIRMWARE_IDE_ERROR_MARKER,
    OS_QEMU_FIRMWARE_IDE_TIMEOUT_MARKER,
    OS_QEMU_FIRMWARE_RESET_MARKER,
    OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
    OS_QEMU_FIRMWARE_STAGE1_CHECKSUM_INVALID_MARKER,
    OS_QEMU_FIRMWARE_STAGE1_HEADER_INVALID_MARKER,
    OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
    OS_QEMU_FIRMWARE_STAGE1_LOADED_MARKER,
    OS_QEMU_STAGE1_ENTERED_MARKER,
    OS_QEMU_STAGE1_A20_INVALID_MARKER,
    OS_QEMU_STAGE1_A20_READY_MARKER,
    OS_QEMU_STAGE1_GDT_READY_MARKER,
    OS_QEMU_STAGE1_PAGE_TABLES_READY_MARKER,
    OS_QEMU_STAGE1_PAGE_TABLES_INVALID_MARKER,
    OS_QEMU_STAGE1_PROTECTED_MODE_MARKER,
    OS_QEMU_STAGE1_PAE_INVALID_MARKER,
    OS_QEMU_STAGE1_PAE_READY_MARKER,
    OS_QEMU_STAGE1_LME_INVALID_MARKER,
    OS_QEMU_STAGE1_LME_READY_MARKER,
    OS_QEMU_STAGE1_PAGING_ENABLED_MARKER,
    OS_QEMU_STAGE1_PAGING_INVALID_MARKER,
    OS_QEMU_STAGE1_LONG_MODE_MARKER,
    OS_QEMU_STAGE1_BOOT_INFO_READY_MARKER,
    OS_QEMU_STAGE1_KERNEL_CHECKSUM_INVALID_MARKER,
    OS_QEMU_STAGE1_KERNEL_ATA_ERROR_MARKER,
    OS_QEMU_STAGE1_KERNEL_ATA_TIMEOUT_MARKER,
    OS_QEMU_STAGE1_KERNEL_ELF_INVALID_MARKER,
    OS_QEMU_STAGE1_KERNEL_ELF_VALID_MARKER,
    OS_QEMU_STAGE1_KERNEL_HEADER_INVALID_MARKER,
    OS_QEMU_STAGE1_KERNEL_HEADER_VALID_MARKER,
    OS_QEMU_STAGE1_KERNEL_PAYLOAD_VALID_MARKER,
    OS_QEMU_STAGE1_KERNEL_RETURNED_MARKER,
    OS_QEMU_STAGE1_KERNEL_SEGMENTS_LOADED_MARKER,
    OS_QEMU_STAGE1_KERNEL_TRANSFER_MARKER,
    OS_QEMU_KERNEL_BOOT_INFO_INVALID_MARKER,
    OS_QEMU_KERNEL_BOOT_INFO_VALID_MARKER,
    OS_QEMU_KERNEL_BSS_INVALID_MARKER,
    OS_QEMU_KERNEL_BSS_ZEROED_MARKER,
    OS_QEMU_KERNEL_CR3_INVALID_MARKER,
    OS_QEMU_KERNEL_CR3_VALID_MARKER,
    OS_QEMU_KERNEL_BREAKPOINT_HANDLED_MARKER,
    OS_QEMU_KERNEL_DESCRIPTOR_TABLES_INVALID_MARKER,
    OS_QEMU_KERNEL_DESCRIPTOR_TABLES_VALID_MARKER,
    OS_QEMU_KERNEL_ENTERED_MARKER,
    OS_QEMU_KERNEL_EXCEPTION_MARKER,
    OS_QEMU_KERNEL_EXCEPTION_SELF_TEST_READY_MARKER,
    OS_QEMU_KERNEL_EXCEPTION_ZERO_ERROR_CODE_MARKER,
    OS_QEMU_KERNEL_FILE_SIZE_MARKER,
    OS_QEMU_KERNEL_GDT_READY_MARKER,
    OS_QEMU_KERNEL_IDT_READY_MARKER,
    OS_QEMU_KERNEL_INVALID_OPCODE_INJECTION_MARKER,
    OS_QEMU_KERNEL_INVALID_OPCODE_VECTOR_MARKER,
    OS_QEMU_KERNEL_LOAD_SEGMENTS_MARKER,
    OS_QEMU_KERNEL_PAGE_FAULT_ADDRESS_MARKER,
    OS_QEMU_KERNEL_PAGE_FAULT_INJECTION_MARKER,
    OS_QEMU_KERNEL_PAGE_FAULT_VECTOR_MARKER,
    OS_QEMU_KERNEL_PANIC_MARKER,
    OS_QEMU_KERNEL_READY_MARKER,
    OS_QEMU_KERNEL_TSS_READY_MARKER,
    runQemuFirmwareBoot,
    runQemuHardwareSmoke,
)
from os_tools.source_metrics import reportSourceMetrics
from os_tools.stage1_image import auditStage1DiskImage
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


def handleAuditKernelElf(arguments: argparse.Namespace) -> None:
    auditKernelElf(OS_TOOL_PROJECT_ROOT, arguments.kernelElfPath)


def handleAuditFirmware(arguments: argparse.Namespace) -> None:
    auditFirmwareImage(arguments.firmwareImagePath)


def handleCreateBootImages(arguments: argparse.Namespace) -> None:
    writeBootDiskImages(
        arguments.stage1BinaryPath,
        arguments.kernelElfPath,
        arguments.outputDirectory,
        arguments.diskSizeBytes,
    )


def handleAuditStage1(arguments: argparse.Namespace) -> None:
    auditStage1DiskImage(arguments.diskImagePath)


def handleAuditKernelImage(arguments: argparse.Namespace) -> None:
    auditKernelDiskImage(arguments.diskImagePath)


def handleQemuSmoke(arguments: argparse.Namespace) -> None:
    runQemuHardwareSmoke(
        OS_TOOL_PROJECT_ROOT,
        arguments.firmwareImagePath,
        arguments.diskImagePath,
        arguments.expectedFirmwareSizeBytes,
        arguments.expectedDiskSizeBytes,
    )


def handleQemuFirmware(arguments: argparse.Namespace) -> None:
    completedLongModeMarkers = (
        OS_QEMU_FIRMWARE_STAGE1_LOADED_MARKER,
        OS_QEMU_STAGE1_A20_READY_MARKER,
        OS_QEMU_STAGE1_ENTERED_MARKER,
        OS_QEMU_STAGE1_GDT_READY_MARKER,
        OS_QEMU_STAGE1_PROTECTED_MODE_MARKER,
        OS_QEMU_STAGE1_PAGE_TABLES_READY_MARKER,
        OS_QEMU_STAGE1_PAE_READY_MARKER,
        OS_QEMU_STAGE1_LME_READY_MARKER,
        OS_QEMU_STAGE1_PAGING_ENABLED_MARKER,
        OS_QEMU_STAGE1_LONG_MODE_MARKER,
    )
    completedKernelLoadMarkers = (
        OS_QEMU_STAGE1_KERNEL_HEADER_VALID_MARKER,
        OS_QEMU_STAGE1_KERNEL_PAYLOAD_VALID_MARKER,
        OS_QEMU_STAGE1_KERNEL_ELF_VALID_MARKER,
        OS_QEMU_STAGE1_KERNEL_SEGMENTS_LOADED_MARKER,
        OS_QEMU_STAGE1_BOOT_INFO_READY_MARKER,
        OS_QEMU_STAGE1_KERNEL_TRANSFER_MARKER,
    )
    completedKernelFoundationMarkers = (
        OS_QEMU_KERNEL_ENTERED_MARKER,
        OS_QEMU_KERNEL_BOOT_INFO_VALID_MARKER,
        OS_QEMU_KERNEL_BSS_ZEROED_MARKER,
        OS_QEMU_KERNEL_CR3_VALID_MARKER,
        OS_QEMU_KERNEL_GDT_READY_MARKER,
        OS_QEMU_KERNEL_TSS_READY_MARKER,
        OS_QEMU_KERNEL_IDT_READY_MARKER,
        OS_QEMU_KERNEL_DESCRIPTOR_TABLES_VALID_MARKER,
        OS_QEMU_KERNEL_BREAKPOINT_HANDLED_MARKER,
        OS_QEMU_KERNEL_EXCEPTION_SELF_TEST_READY_MARKER,
    )
    completedKernelEntryMarkers = (
        *completedKernelFoundationMarkers,
        OS_QEMU_KERNEL_FILE_SIZE_MARKER,
        OS_QEMU_KERNEL_LOAD_SEGMENTS_MARKER,
        OS_QEMU_KERNEL_READY_MARKER,
    )
    completedBootMarkers = (
        *completedLongModeMarkers,
        *completedKernelLoadMarkers,
        *completedKernelEntryMarkers,
    )
    kernelFailureMarkers = (
        OS_QEMU_STAGE1_KERNEL_ATA_TIMEOUT_MARKER,
        OS_QEMU_STAGE1_KERNEL_ATA_ERROR_MARKER,
        OS_QEMU_STAGE1_KERNEL_HEADER_INVALID_MARKER,
        OS_QEMU_STAGE1_KERNEL_CHECKSUM_INVALID_MARKER,
        OS_QEMU_STAGE1_KERNEL_ELF_INVALID_MARKER,
        OS_QEMU_STAGE1_KERNEL_RETURNED_MARKER,
    )
    if arguments.expectedOutcome == "success":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedBootMarkers,
        )
        forbiddenMarkers: tuple[str, ...] = (
            OS_QEMU_STAGE1_PAGE_TABLES_INVALID_MARKER,
            OS_QEMU_STAGE1_PAE_INVALID_MARKER,
            OS_QEMU_STAGE1_LME_INVALID_MARKER,
            OS_QEMU_STAGE1_PAGING_INVALID_MARKER,
            OS_QEMU_STAGE1_A20_INVALID_MARKER,
            *kernelFailureMarkers,
            OS_QEMU_KERNEL_BOOT_INFO_INVALID_MARKER,
            OS_QEMU_KERNEL_BSS_INVALID_MARKER,
            OS_QEMU_KERNEL_CR3_INVALID_MARKER,
            OS_QEMU_KERNEL_DESCRIPTOR_TABLES_INVALID_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_MARKER,
            OS_QEMU_KERNEL_PANIC_MARKER,
        )
    elif arguments.expectedOutcome == "serial-failure":
        requiredMarkers = (OS_QEMU_FIRMWARE_RESET_MARKER,)
        forbiddenMarkers = (
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            *completedBootMarkers,
        )
    elif arguments.expectedOutcome == "ide-timeout":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_IDE_TIMEOUT_MARKER,
        )
        forbiddenMarkers = completedBootMarkers
    elif arguments.expectedOutcome == "ide-error":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_IDE_ERROR_MARKER,
        )
        forbiddenMarkers = completedBootMarkers
    elif arguments.expectedOutcome == "stage1-header-invalid":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_INVALID_MARKER,
        )
        forbiddenMarkers = completedBootMarkers
    elif arguments.expectedOutcome == "stage1-checksum-invalid":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_CHECKSUM_INVALID_MARKER,
        )
        forbiddenMarkers = completedBootMarkers
    elif arguments.expectedOutcome == "kernel-header-invalid":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            OS_QEMU_STAGE1_KERNEL_HEADER_INVALID_MARKER,
        )
        forbiddenMarkers = (
            *completedKernelLoadMarkers,
            *completedKernelEntryMarkers,
            *kernelFailureMarkers[:2],
            *kernelFailureMarkers[3:],
        )
    elif arguments.expectedOutcome == "kernel-checksum-invalid":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            OS_QEMU_STAGE1_KERNEL_HEADER_VALID_MARKER,
            OS_QEMU_STAGE1_KERNEL_CHECKSUM_INVALID_MARKER,
        )
        forbiddenMarkers = (
            *completedKernelLoadMarkers[1:],
            *completedKernelEntryMarkers,
            *kernelFailureMarkers[:3],
            *kernelFailureMarkers[4:],
        )
    elif arguments.expectedOutcome == "kernel-elf-invalid":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            OS_QEMU_STAGE1_KERNEL_HEADER_VALID_MARKER,
            OS_QEMU_STAGE1_KERNEL_PAYLOAD_VALID_MARKER,
            OS_QEMU_STAGE1_KERNEL_ELF_INVALID_MARKER,
        )
        forbiddenMarkers = (
            *completedKernelLoadMarkers[2:],
            *completedKernelEntryMarkers,
            *kernelFailureMarkers[:4],
            *kernelFailureMarkers[5:],
        )
    elif arguments.expectedOutcome == "kernel-ata-timeout":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            OS_QEMU_STAGE1_KERNEL_ATA_TIMEOUT_MARKER,
        )
        forbiddenMarkers = (
            *completedKernelLoadMarkers,
            *completedKernelEntryMarkers,
            *kernelFailureMarkers[1:],
        )
    elif arguments.expectedOutcome == "kernel-invalid-opcode":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            *completedKernelLoadMarkers,
            *completedKernelFoundationMarkers,
            OS_QEMU_KERNEL_INVALID_OPCODE_INJECTION_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_MARKER,
            OS_QEMU_KERNEL_INVALID_OPCODE_VECTOR_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_ZERO_ERROR_CODE_MARKER,
            OS_QEMU_KERNEL_PANIC_MARKER,
        )
        forbiddenMarkers = (
            OS_QEMU_KERNEL_PAGE_FAULT_INJECTION_MARKER,
            OS_QEMU_KERNEL_PAGE_FAULT_VECTOR_MARKER,
            OS_QEMU_KERNEL_PAGE_FAULT_ADDRESS_MARKER,
            OS_QEMU_KERNEL_FILE_SIZE_MARKER,
            OS_QEMU_KERNEL_READY_MARKER,
            OS_QEMU_KERNEL_DESCRIPTOR_TABLES_INVALID_MARKER,
        )
    elif arguments.expectedOutcome == "kernel-page-fault":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            *completedKernelLoadMarkers,
            *completedKernelFoundationMarkers,
            OS_QEMU_KERNEL_PAGE_FAULT_INJECTION_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_MARKER,
            OS_QEMU_KERNEL_PAGE_FAULT_VECTOR_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_ZERO_ERROR_CODE_MARKER,
            OS_QEMU_KERNEL_PAGE_FAULT_ADDRESS_MARKER,
            OS_QEMU_KERNEL_PANIC_MARKER,
        )
        forbiddenMarkers = (
            OS_QEMU_KERNEL_INVALID_OPCODE_INJECTION_MARKER,
            OS_QEMU_KERNEL_INVALID_OPCODE_VECTOR_MARKER,
            OS_QEMU_KERNEL_FILE_SIZE_MARKER,
            OS_QEMU_KERNEL_READY_MARKER,
            OS_QEMU_KERNEL_DESCRIPTOR_TABLES_INVALID_MARKER,
        )
    else:
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            OS_QEMU_STAGE1_KERNEL_ATA_ERROR_MARKER,
        )
        forbiddenMarkers = (
            *completedKernelLoadMarkers,
            *completedKernelEntryMarkers,
            *kernelFailureMarkers[:1],
            *kernelFailureMarkers[2:],
        )

    runQemuFirmwareBoot(
        OS_TOOL_PROJECT_ROOT,
        arguments.firmwareImagePath,
        arguments.diskImagePath,
        arguments.expectedFirmwareSizeBytes,
        arguments.expectedDiskSizeBytes,
        requiredMarkers,
        forbiddenMarkers,
    )


def handleSourceMetrics(arguments: argparse.Namespace) -> None:
    reportSourceMetrics(
        OS_TOOL_PROJECT_ROOT,
        arguments.latexOutputPath,
    )


def handlePhoneBookExport(_: argparse.Namespace) -> None:
    exportBookToPhone(OS_TOOL_PROJECT_ROOT)


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

    kernelElfAuditParser = addCommand(
        subparsers,
        "audit-kernel-elf",
        "检查自研 ELF64 内核的头、程序段、入口和符号",
        handleAuditKernelElf,
    )
    kernelElfAuditParser.add_argument("kernelElfPath", type=Path)

    firmwareAuditParser = addCommand(
        subparsers,
        "audit-firmware",
        "检查 128 KiB ROM、复位向量和固件入口",
        handleAuditFirmware,
    )
    firmwareAuditParser.add_argument("firmwareImagePath", type=Path)

    bootImageParser = addCommand(
        subparsers,
        "create-boot-images",
        "生成包含 Stage 1 与 Kernel ELF 的正常和损坏磁盘镜像",
        handleCreateBootImages,
    )
    bootImageParser.add_argument("stage1BinaryPath", type=Path)
    bootImageParser.add_argument("kernelElfPath", type=Path)
    bootImageParser.add_argument("outputDirectory", type=Path)
    bootImageParser.add_argument("diskSizeBytes", type=int)

    stage1AuditParser = addCommand(
        subparsers,
        "audit-stage1",
        "检查 Stage 1 描述符、加载范围和负载校验",
        handleAuditStage1,
    )
    stage1AuditParser.add_argument("diskImagePath", type=Path)

    kernelImageAuditParser = addCommand(
        subparsers,
        "audit-kernel-image",
        "检查磁盘中的 Kernel 描述符、ELF 文件校验和结构",
        handleAuditKernelImage,
    )
    kernelImageAuditParser.add_argument("diskImagePath", type=Path)

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
        choices=(
            "success",
            "serial-failure",
            "ide-timeout",
            "ide-error",
            "stage1-header-invalid",
            "stage1-checksum-invalid",
            "kernel-header-invalid",
            "kernel-checksum-invalid",
            "kernel-elf-invalid",
            "kernel-ata-timeout",
            "kernel-ata-error",
            "kernel-invalid-opcode",
            "kernel-page-fault",
        ),
        required=True,
        dest="expectedOutcome",
    )

    sourceMetricsParser = addCommand(
        subparsers,
        "source-metrics",
        "统计只进入目标系统的真实代码",
        handleSourceMetrics,
    )
    sourceMetricsParser.add_argument(
        "--latex-output",
        dest="latexOutputPath",
        type=Path,
    )
    addCommand(
        subparsers,
        "phone-book-export",
        "把教材 PDF 导出到手机独立书籍目录",
        handlePhoneBookExport,
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
