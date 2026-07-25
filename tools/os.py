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
    OS_QEMU_STAGE1_MEMORY_MAP_INVALID_MARKER,
    OS_QEMU_STAGE1_MEMORY_MAP_READY_MARKER,
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
    OS_QEMU_KERNEL_MEMORY_MANAGED_MARKER,
    OS_QEMU_KERNEL_MEMORY_DESCRIBED_MARKER,
    OS_QEMU_KERNEL_MEMORY_MAP_ENTRIES_MARKER,
    OS_QEMU_KERNEL_MEMORY_MAP_VALID_MARKER,
    OS_QEMU_KERNEL_MEMORY_PERMISSIONS_VALID_MARKER,
    OS_QEMU_KERNEL_MEMORY_USABLE_MARKER,
    OS_QEMU_KERNEL_FRAME_ALLOCATOR_READY_MARKER,
    OS_QEMU_KERNEL_FREE_FRAMES_MARKER,
    OS_QEMU_KERNEL_ALLOCATED_FRAMES_MARKER,
    OS_QEMU_KERNEL_RESERVED_FRAMES_MARKER,
    OS_QEMU_KERNEL_PAGING_READY_MARKER,
    OS_QEMU_KERNEL_PAGING_ROOT_MARKER,
    OS_QEMU_KERNEL_HEAP_READY_MARKER,
    OS_QEMU_KERNEL_HEAP_CAPACITY_MARKER,
    OS_QEMU_KERNEL_HEAP_SELF_TEST_PASSED_MARKER,
    OS_QEMU_KERNEL_ATA_BOOT_DESCRIPTOR_VALID_MARKER,
    OS_QEMU_KERNEL_ATA_PIO_READY_MARKER,
    OS_QEMU_KERNEL_DEVICE_INITIALIZATION_FAILED_MARKER,
    OS_QEMU_KERNEL_INTERRUPTS_ENABLED_MARKER,
    OS_QEMU_KERNEL_KEYBOARD_A_PRESSED_MARKER,
    OS_QEMU_KERNEL_KEYBOARD_SCANCODE_MARKER,
    OS_QEMU_KERNEL_LEGACY_INTERRUPT_ROUTING_READY_MARKER,
    OS_QEMU_KERNEL_MONOTONIC_MILLISECONDS_MARKER,
    OS_QEMU_KERNEL_PAGE_FAULT_ADDRESS_MARKER,
    OS_QEMU_KERNEL_PAGE_FAULT_INJECTION_MARKER,
    OS_QEMU_KERNEL_PAGE_FAULT_VECTOR_MARKER,
    OS_QEMU_KERNEL_PANIC_MARKER,
    OS_QEMU_KERNEL_PIC_MASK_MARKER,
    OS_QEMU_KERNEL_PIC_READY_MARKER,
    OS_QEMU_KERNEL_PIC_SPURIOUS_SELF_TEST_PASSED_MARKER,
    OS_QEMU_KERNEL_PIT_DIVISOR_MARKER,
    OS_QEMU_KERNEL_PIT_FREQUENCY_MARKER,
    OS_QEMU_KERNEL_PIT_READY_MARKER,
    OS_QEMU_KERNEL_PROCESS_CR3_MARKER,
    OS_QEMU_KERNEL_PROCESS_DISPATCH_COUNT_MARKER,
    OS_QEMU_KERNEL_PROCESS_PIPE_READ_BYTES_MARKER,
    OS_QEMU_KERNEL_PROCESS_PIPE_WRITTEN_BYTES_MARKER,
    OS_QEMU_KERNEL_PROCESS_ID_MARKER,
    OS_QEMU_KERNEL_PROCESS_KERNEL_STACK_TOP_MARKER,
    OS_QEMU_KERNEL_PROCESS_RESOURCES_RECLAIMED_MARKER,
    OS_QEMU_KERNEL_PROCESS_RUN_TICKS_MARKER,
    OS_QEMU_KERNEL_PROCESS_RUNTIME_READY_MARKER,
    OS_QEMU_KERNEL_PIPE_READY_MARKER,
    OS_QEMU_KERNEL_PIPE_CAPACITY_MARKER,
    OS_QEMU_KERNEL_PIPE_WRITTEN_BYTES_MARKER,
    OS_QEMU_KERNEL_PIPE_READ_BYTES_MARKER,
    OS_QEMU_KERNEL_PIPE_WRITER_BLOCKS_MARKER,
    OS_QEMU_KERNEL_PIPE_END_OF_FILE_MARKER,
    OS_QEMU_KERNEL_PIPE_TRANSFER_VALID_MARKER,
    OS_QEMU_KERNEL_PIPE_ENDPOINTS_CLOSED_MARKER,
    OS_QEMU_KERNEL_PS2_KEYBOARD_READY_MARKER,
    OS_QEMU_KERNEL_READY_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_COMPLETE_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_CREATED_PROCESSES_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_DISPATCHES_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_BLOCKS_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_WAKEUPS_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_PREEMPTIONS_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_STARTED_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_TERMINATED_PROCESSES_MARKER,
    OS_QEMU_KERNEL_SCHEDULER_TIMER_TICKS_MARKER,
    OS_QEMU_KERNEL_TIMER_SELF_TEST_PASSED_MARKER,
    OS_QEMU_KERNEL_TIMER_TICKS_MARKER,
    OS_QEMU_KERNEL_TSS_READY_MARKER,
    OS_QEMU_KERNEL_USER_ELF_REJECTED_MARKER,
    OS_QEMU_KERNEL_USER_ELF_VALID_MARKER,
    OS_QEMU_KERNEL_USER_ENTRY_MARKER,
    OS_QEMU_KERNEL_USER_EXCEPTION_RIP_MARKER,
    OS_QEMU_KERNEL_USER_EXCEPTION_ZERO_ERROR_CODE_MARKER,
    OS_QEMU_KERNEL_USER_EXIT_ZERO_MARKER,
    OS_QEMU_KERNEL_USER_INVALID_OPCODE_VECTOR_MARKER,
    OS_QEMU_KERNEL_USER_MAPPED_PAGES_MARKER,
    OS_QEMU_KERNEL_USER_PAGE_FAULT_ADDRESS_MARKER,
    OS_QEMU_KERNEL_USER_PAGE_FAULT_ERROR_CODE_MARKER,
    OS_QEMU_KERNEL_USER_PAGE_FAULT_VECTOR_MARKER,
    OS_QEMU_KERNEL_USER_RESULT_INVALID_MARKER,
    OS_QEMU_KERNEL_USER_RETURNED_TO_KERNEL_MARKER,
    OS_QEMU_KERNEL_USER_RING3_ENTER_MARKER,
    OS_QEMU_KERNEL_USER_SYSTEM_CALL_COUNT_MARKER,
    OS_QEMU_KERNEL_USER_STACK_READY_MARKER,
    OS_QEMU_KERNEL_USER_TERMINATED_MARKER,
    OS_QEMU_KERNEL_USER_ZERO_SYSTEM_CALL_COUNT_MARKER,
    OS_QEMU_USER_HELLO_FROM_RING3_MARKER,
    OS_QEMU_USER_ADDRESS_SPACE_ISOLATED_MARKER,
    OS_QEMU_USER_INVALID_POINTER_REJECTED_MARKER,
    OS_QEMU_USER_UNKNOWN_SYSTEM_CALL_REJECTED_MARKER,
    OS_QEMU_USER_PIPE_PRODUCER_STARTED_MARKER,
    OS_QEMU_USER_PIPE_PRODUCER_COMPLETED_MARKER,
    OS_QEMU_USER_PIPE_CONSUMER_STARTED_MARKER,
    OS_QEMU_USER_PIPE_PAYLOAD_VERIFIED_MARKER,
    OS_QEMU_USER_PIPE_END_OF_FILE_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_2_STEP_1_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_2_STEP_2_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_2_STEP_3_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_3_STEP_1_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_3_STEP_2_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_3_STEP_3_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_4_STEP_1_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_4_STEP_2_MARKER,
    OS_QEMU_USER_WORKER_PROCESS_4_STEP_3_MARKER,
    OS_QEMU_KERNEL_WRITE_PROTECTION_ADDRESS_MARKER,
    OS_QEMU_KERNEL_WRITE_PROTECTION_ERROR_CODE_MARKER,
    OS_QEMU_KERNEL_WRITE_PROTECTION_INJECTION_MARKER,
    runQemuFirmwareBoot,
    runQemuHardwareSmoke,
)
from os_tools.source_metrics import reportSourceMetrics
from os_tools.stage1_image import auditStage1DiskImage
from os_tools.toolchain import checkToolchain
from os_tools.user_elf import auditUserElf


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


def handleAuditUserElf(arguments: argparse.Namespace) -> None:
    auditUserElf(OS_TOOL_PROJECT_ROOT, arguments.userElfPath)


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
        OS_QEMU_STAGE1_MEMORY_MAP_READY_MARKER,
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
        OS_QEMU_KERNEL_MEMORY_MAP_VALID_MARKER,
        OS_QEMU_KERNEL_MEMORY_MAP_ENTRIES_MARKER,
        OS_QEMU_KERNEL_MEMORY_DESCRIBED_MARKER,
        OS_QEMU_KERNEL_MEMORY_USABLE_MARKER,
        OS_QEMU_KERNEL_MEMORY_MANAGED_MARKER,
        OS_QEMU_KERNEL_FRAME_ALLOCATOR_READY_MARKER,
        OS_QEMU_KERNEL_FREE_FRAMES_MARKER,
        OS_QEMU_KERNEL_ALLOCATED_FRAMES_MARKER,
        OS_QEMU_KERNEL_RESERVED_FRAMES_MARKER,
        OS_QEMU_KERNEL_PAGING_READY_MARKER,
        OS_QEMU_KERNEL_PAGING_ROOT_MARKER,
        OS_QEMU_KERNEL_MEMORY_PERMISSIONS_VALID_MARKER,
        OS_QEMU_KERNEL_HEAP_READY_MARKER,
        OS_QEMU_KERNEL_HEAP_CAPACITY_MARKER,
        OS_QEMU_KERNEL_HEAP_SELF_TEST_PASSED_MARKER,
    )
    completedKernelUserPreparationMarkers = (
        OS_QEMU_KERNEL_PROCESS_RUNTIME_READY_MARKER,
        OS_QEMU_KERNEL_PIPE_READY_MARKER,
        OS_QEMU_KERNEL_USER_ELF_VALID_MARKER,
        OS_QEMU_KERNEL_USER_ENTRY_MARKER,
        OS_QEMU_KERNEL_USER_MAPPED_PAGES_MARKER,
        OS_QEMU_KERNEL_USER_STACK_READY_MARKER,
    )
    completedKernelDeviceMarkers = (
        OS_QEMU_KERNEL_LEGACY_INTERRUPT_ROUTING_READY_MARKER,
        OS_QEMU_KERNEL_PIC_READY_MARKER,
        OS_QEMU_KERNEL_PIC_MASK_MARKER,
        OS_QEMU_KERNEL_PIT_READY_MARKER,
        OS_QEMU_KERNEL_PIT_DIVISOR_MARKER,
        OS_QEMU_KERNEL_PIT_FREQUENCY_MARKER,
        OS_QEMU_KERNEL_PS2_KEYBOARD_READY_MARKER,
        OS_QEMU_KERNEL_ATA_PIO_READY_MARKER,
        OS_QEMU_KERNEL_ATA_BOOT_DESCRIPTOR_VALID_MARKER,
        OS_QEMU_KERNEL_PIC_SPURIOUS_SELF_TEST_PASSED_MARKER,
        OS_QEMU_KERNEL_INTERRUPTS_ENABLED_MARKER,
        OS_QEMU_KERNEL_TIMER_TICKS_MARKER,
        OS_QEMU_KERNEL_MONOTONIC_MILLISECONDS_MARKER,
        OS_QEMU_KERNEL_TIMER_SELF_TEST_PASSED_MARKER,
    )
    completedKernelUserSmokeMarkers = (
        OS_QEMU_KERNEL_USER_RING3_ENTER_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_STARTED_MARKER,
        OS_QEMU_USER_INVALID_POINTER_REJECTED_MARKER,
        OS_QEMU_USER_UNKNOWN_SYSTEM_CALL_REJECTED_MARKER,
        OS_QEMU_USER_HELLO_FROM_RING3_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_CREATED_PROCESSES_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_TERMINATED_PROCESSES_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_TIMER_TICKS_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_PREEMPTIONS_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_DISPATCHES_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_BLOCKS_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_WAKEUPS_MARKER,
        OS_QEMU_KERNEL_PIPE_CAPACITY_MARKER,
        OS_QEMU_KERNEL_PIPE_WRITTEN_BYTES_MARKER,
        OS_QEMU_KERNEL_PIPE_READ_BYTES_MARKER,
        OS_QEMU_KERNEL_PIPE_WRITER_BLOCKS_MARKER,
        OS_QEMU_KERNEL_PIPE_END_OF_FILE_MARKER,
        OS_QEMU_KERNEL_USER_EXIT_ZERO_MARKER,
        OS_QEMU_KERNEL_USER_SYSTEM_CALL_COUNT_MARKER,
        OS_QEMU_KERNEL_PROCESS_RUN_TICKS_MARKER,
        OS_QEMU_KERNEL_PROCESS_DISPATCH_COUNT_MARKER,
        OS_QEMU_KERNEL_PROCESS_PIPE_READ_BYTES_MARKER,
        OS_QEMU_KERNEL_PROCESS_PIPE_WRITTEN_BYTES_MARKER,
        OS_QEMU_KERNEL_USER_TERMINATED_MARKER,
        OS_QEMU_KERNEL_PIPE_TRANSFER_VALID_MARKER,
        OS_QEMU_KERNEL_PIPE_ENDPOINTS_CLOSED_MARKER,
        OS_QEMU_KERNEL_PROCESS_RESOURCES_RECLAIMED_MARKER,
        OS_QEMU_KERNEL_SCHEDULER_COMPLETE_MARKER,
        OS_QEMU_KERNEL_USER_RETURNED_TO_KERNEL_MARKER,
    )
    completedKernelEntryMarkers = (
        *completedKernelFoundationMarkers,
        *completedKernelUserPreparationMarkers,
        *completedKernelDeviceMarkers,
        *completedKernelUserSmokeMarkers,
        OS_QEMU_KERNEL_FILE_SIZE_MARKER,
        OS_QEMU_KERNEL_LOAD_SEGMENTS_MARKER,
        OS_QEMU_KERNEL_READY_MARKER,
        OS_QEMU_KERNEL_KEYBOARD_SCANCODE_MARKER,
        OS_QEMU_KERNEL_KEYBOARD_A_PRESSED_MARKER,
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
    expectedMarkerCounts: tuple[tuple[str, int], ...] = ()
    minimumHexMarkerValues: tuple[tuple[str, int], ...] = ()
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
            OS_QEMU_STAGE1_MEMORY_MAP_INVALID_MARKER,
            *kernelFailureMarkers,
            OS_QEMU_KERNEL_BOOT_INFO_INVALID_MARKER,
            OS_QEMU_KERNEL_BSS_INVALID_MARKER,
            OS_QEMU_KERNEL_CR3_INVALID_MARKER,
            OS_QEMU_KERNEL_DESCRIPTOR_TABLES_INVALID_MARKER,
            OS_QEMU_KERNEL_DEVICE_INITIALIZATION_FAILED_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_MARKER,
            OS_QEMU_KERNEL_PANIC_MARKER,
            OS_QEMU_KERNEL_USER_RESULT_INVALID_MARKER,
        )
        expectedMarkerCounts = (
            (OS_QEMU_KERNEL_PROCESS_RUNTIME_READY_MARKER, 1),
            (OS_QEMU_KERNEL_USER_ELF_VALID_MARKER, 4),
            (OS_QEMU_KERNEL_USER_ENTRY_MARKER, 4),
            (OS_QEMU_KERNEL_USER_MAPPED_PAGES_MARKER, 4),
            (OS_QEMU_KERNEL_USER_STACK_READY_MARKER, 4),
            (OS_QEMU_KERNEL_PROCESS_ID_MARKER, 8),
            (OS_QEMU_KERNEL_PROCESS_CR3_MARKER, 4),
            (OS_QEMU_KERNEL_PROCESS_KERNEL_STACK_TOP_MARKER, 4),
            (OS_QEMU_KERNEL_SCHEDULER_STARTED_MARKER, 1),
            (OS_QEMU_USER_WORKER_PROCESS_3_STEP_1_MARKER, 1),
            (OS_QEMU_USER_WORKER_PROCESS_3_STEP_2_MARKER, 1),
            (OS_QEMU_USER_WORKER_PROCESS_3_STEP_3_MARKER, 1),
            (OS_QEMU_USER_WORKER_PROCESS_4_STEP_1_MARKER, 1),
            (OS_QEMU_USER_WORKER_PROCESS_4_STEP_2_MARKER, 1),
            (OS_QEMU_USER_WORKER_PROCESS_4_STEP_3_MARKER, 1),
            (OS_QEMU_USER_ADDRESS_SPACE_ISOLATED_MARKER, 2),
            (OS_QEMU_USER_PIPE_PRODUCER_STARTED_MARKER, 1),
            (OS_QEMU_USER_PIPE_PRODUCER_COMPLETED_MARKER, 1),
            (OS_QEMU_USER_PIPE_CONSUMER_STARTED_MARKER, 1),
            (OS_QEMU_USER_PIPE_PAYLOAD_VERIFIED_MARKER, 1),
            (OS_QEMU_USER_PIPE_END_OF_FILE_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_CREATED_PROCESSES_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_TERMINATED_PROCESSES_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_TIMER_TICKS_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_PREEMPTIONS_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_DISPATCHES_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_BLOCKS_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_WAKEUPS_MARKER, 1),
            (OS_QEMU_KERNEL_PIPE_CAPACITY_MARKER, 1),
            (OS_QEMU_KERNEL_PIPE_WRITTEN_BYTES_MARKER, 1),
            (OS_QEMU_KERNEL_PIPE_READ_BYTES_MARKER, 1),
            (OS_QEMU_KERNEL_PIPE_WRITER_BLOCKS_MARKER, 1),
            (OS_QEMU_KERNEL_PIPE_END_OF_FILE_MARKER, 1),
            (OS_QEMU_KERNEL_USER_EXIT_ZERO_MARKER, 4),
            (OS_QEMU_KERNEL_USER_SYSTEM_CALL_COUNT_MARKER, 4),
            (OS_QEMU_KERNEL_PROCESS_RUN_TICKS_MARKER, 4),
            (OS_QEMU_KERNEL_PROCESS_DISPATCH_COUNT_MARKER, 4),
            (OS_QEMU_KERNEL_PROCESS_PIPE_READ_BYTES_MARKER, 4),
            (OS_QEMU_KERNEL_PROCESS_PIPE_WRITTEN_BYTES_MARKER, 4),
            (OS_QEMU_KERNEL_USER_TERMINATED_MARKER, 4),
            (OS_QEMU_KERNEL_PIPE_TRANSFER_VALID_MARKER, 1),
            (OS_QEMU_KERNEL_PIPE_ENDPOINTS_CLOSED_MARKER, 1),
            (OS_QEMU_KERNEL_PROCESS_RESOURCES_RECLAIMED_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_COMPLETE_MARKER, 1),
        )
        minimumHexMarkerValues = (
            (OS_QEMU_KERNEL_SCHEDULER_CREATED_PROCESSES_MARKER, 4),
            (OS_QEMU_KERNEL_SCHEDULER_TERMINATED_PROCESSES_MARKER, 4),
            (OS_QEMU_KERNEL_SCHEDULER_TIMER_TICKS_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_PREEMPTIONS_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_DISPATCHES_MARKER, 4),
            (OS_QEMU_KERNEL_SCHEDULER_BLOCKS_MARKER, 1),
            (OS_QEMU_KERNEL_SCHEDULER_WAKEUPS_MARKER, 1),
            (OS_QEMU_KERNEL_PIPE_WRITER_BLOCKS_MARKER, 1),
            (OS_QEMU_KERNEL_PROCESS_RUN_TICKS_MARKER, 1),
            (OS_QEMU_KERNEL_PROCESS_DISPATCH_COUNT_MARKER, 1),
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
    elif arguments.expectedOutcome == "memory-map-invalid":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers[:-1],
            OS_QEMU_STAGE1_MEMORY_MAP_INVALID_MARKER,
        )
        forbiddenMarkers = (
            OS_QEMU_STAGE1_MEMORY_MAP_READY_MARKER,
            *completedKernelLoadMarkers,
            *completedKernelEntryMarkers,
        )
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
    elif arguments.expectedOutcome == "kernel-write-protection":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            *completedKernelLoadMarkers,
            *completedKernelFoundationMarkers,
            OS_QEMU_KERNEL_WRITE_PROTECTION_INJECTION_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_MARKER,
            OS_QEMU_KERNEL_PAGE_FAULT_VECTOR_MARKER,
            OS_QEMU_KERNEL_WRITE_PROTECTION_ERROR_CODE_MARKER,
            OS_QEMU_KERNEL_WRITE_PROTECTION_ADDRESS_MARKER,
            OS_QEMU_KERNEL_PANIC_MARKER,
        )
        forbiddenMarkers = (
            OS_QEMU_KERNEL_INVALID_OPCODE_INJECTION_MARKER,
            OS_QEMU_KERNEL_PAGE_FAULT_INJECTION_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_ZERO_ERROR_CODE_MARKER,
            OS_QEMU_KERNEL_PAGE_FAULT_ADDRESS_MARKER,
            OS_QEMU_KERNEL_FILE_SIZE_MARKER,
            OS_QEMU_KERNEL_READY_MARKER,
            OS_QEMU_KERNEL_DESCRIPTOR_TABLES_INVALID_MARKER,
        )
    elif arguments.expectedOutcome == "user-invalid-opcode":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            *completedKernelLoadMarkers,
            *completedKernelFoundationMarkers,
            *completedKernelUserPreparationMarkers,
            *completedKernelDeviceMarkers,
            OS_QEMU_KERNEL_USER_RING3_ENTER_MARKER,
            OS_QEMU_KERNEL_USER_INVALID_OPCODE_VECTOR_MARKER,
            OS_QEMU_KERNEL_USER_EXCEPTION_ZERO_ERROR_CODE_MARKER,
            OS_QEMU_KERNEL_USER_EXCEPTION_RIP_MARKER,
            OS_QEMU_KERNEL_USER_ZERO_SYSTEM_CALL_COUNT_MARKER,
            OS_QEMU_KERNEL_USER_TERMINATED_MARKER,
            OS_QEMU_KERNEL_USER_RETURNED_TO_KERNEL_MARKER,
            OS_QEMU_KERNEL_FILE_SIZE_MARKER,
            OS_QEMU_KERNEL_LOAD_SEGMENTS_MARKER,
            OS_QEMU_KERNEL_READY_MARKER,
        )
        forbiddenMarkers = (
            OS_QEMU_KERNEL_EXCEPTION_MARKER,
            OS_QEMU_KERNEL_PANIC_MARKER,
            OS_QEMU_KERNEL_USER_PAGE_FAULT_VECTOR_MARKER,
            OS_QEMU_KERNEL_USER_PAGE_FAULT_ADDRESS_MARKER,
            OS_QEMU_KERNEL_USER_RESULT_INVALID_MARKER,
            OS_QEMU_USER_HELLO_FROM_RING3_MARKER,
        )
    elif arguments.expectedOutcome == "user-page-fault":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            *completedKernelLoadMarkers,
            *completedKernelFoundationMarkers,
            *completedKernelUserPreparationMarkers,
            *completedKernelDeviceMarkers,
            OS_QEMU_KERNEL_USER_RING3_ENTER_MARKER,
            OS_QEMU_KERNEL_USER_PAGE_FAULT_VECTOR_MARKER,
            OS_QEMU_KERNEL_USER_PAGE_FAULT_ERROR_CODE_MARKER,
            OS_QEMU_KERNEL_USER_EXCEPTION_RIP_MARKER,
            OS_QEMU_KERNEL_USER_PAGE_FAULT_ADDRESS_MARKER,
            OS_QEMU_KERNEL_USER_ZERO_SYSTEM_CALL_COUNT_MARKER,
            OS_QEMU_KERNEL_USER_TERMINATED_MARKER,
            OS_QEMU_KERNEL_USER_RETURNED_TO_KERNEL_MARKER,
            OS_QEMU_KERNEL_FILE_SIZE_MARKER,
            OS_QEMU_KERNEL_LOAD_SEGMENTS_MARKER,
            OS_QEMU_KERNEL_READY_MARKER,
        )
        forbiddenMarkers = (
            OS_QEMU_KERNEL_EXCEPTION_MARKER,
            OS_QEMU_KERNEL_PANIC_MARKER,
            OS_QEMU_KERNEL_USER_INVALID_OPCODE_VECTOR_MARKER,
            OS_QEMU_KERNEL_USER_RESULT_INVALID_MARKER,
            OS_QEMU_USER_HELLO_FROM_RING3_MARKER,
        )
    elif arguments.expectedOutcome == "user-invalid-elf":
        requiredMarkers = (
            OS_QEMU_FIRMWARE_RESET_MARKER,
            OS_QEMU_FIRMWARE_SERIAL_READY_MARKER,
            OS_QEMU_FIRMWARE_CLOCK_READY_MARKER,
            OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER,
            *completedLongModeMarkers,
            *completedKernelLoadMarkers,
            *completedKernelFoundationMarkers,
            OS_QEMU_KERNEL_USER_ELF_REJECTED_MARKER,
        )
        forbiddenMarkers = (
            OS_QEMU_KERNEL_USER_ELF_VALID_MARKER,
            OS_QEMU_KERNEL_USER_STACK_READY_MARKER,
            OS_QEMU_KERNEL_USER_RING3_ENTER_MARKER,
            OS_QEMU_KERNEL_INTERRUPTS_ENABLED_MARKER,
            OS_QEMU_KERNEL_EXCEPTION_MARKER,
            OS_QEMU_KERNEL_PANIC_MARKER,
            OS_QEMU_KERNEL_READY_MARKER,
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
        "a" if arguments.expectedOutcome == "success" else None,
        expectedMarkerCounts,
        minimumHexMarkerValues,
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

    userElfAuditParser = addCommand(
        subparsers,
        "audit-user-elf",
        "检查自研 Ring 3 ELF64 程序的格式、权限、入口和符号",
        handleAuditUserElf,
    )
    userElfAuditParser.add_argument("userElfPath", type=Path)

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
            "memory-map-invalid",
            "kernel-header-invalid",
            "kernel-checksum-invalid",
            "kernel-elf-invalid",
            "kernel-ata-timeout",
            "kernel-ata-error",
            "kernel-invalid-opcode",
            "kernel-page-fault",
            "kernel-write-protection",
            "user-invalid-opcode",
            "user-page-fault",
            "user-invalid-elf",
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
