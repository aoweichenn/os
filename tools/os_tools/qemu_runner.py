from collections.abc import Callable
import json
from pathlib import Path
import re
import shlex
import shutil
import socket
import subprocess
import tempfile
import threading
import time

from .errors import OsToolError
from .process import runCommand


OS_QEMU_SMOKE_TIMEOUT_SECONDS = 2.0
OS_QEMU_FIRMWARE_TIMEOUT_SECONDS = 15.0
OS_QEMU_TERMINATION_TIMEOUT_SECONDS = 1.0
OS_QEMU_QMP_CONNECTION_TIMEOUT_SECONDS = 1.0
OS_QEMU_QMP_READY_TIMEOUT_SECONDS = OS_QEMU_FIRMWARE_TIMEOUT_SECONDS
OS_QEMU_QMP_RETRY_INTERVAL_SECONDS = 0.01
OS_QEMU_KEY_INJECTION_INTERVAL_SECONDS = 0.003
OS_QEMU_QMP_MAXIMUM_RESPONSE_COUNT = 32
OS_QEMU_COMPLETION_POLL_INTERVAL_SECONDS = 0.01
OS_QEMU_COMPLETION_SETTLE_SECONDS = 0.05
OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES = 64
OS_QEMU_PRIMARY_GUEST_MEMORY_MEBIBYTES = 64 * 1024
OS_QEMU_FILE_SYSTEM_START_LBA = 2048
OS_QEMU_FILE_SYSTEM_SECTOR_SIZE_BYTES = 512
OS_QEMU_FILE_SYSTEM_CORRUPTION_OFFSET_BYTES = 64
OS_QEMU_FILE_SYSTEM_CORRUPTION_MASK = 0x80
OS_QEMU_FIRMWARE_RESET_MARKER = "[OS][FIRMWARE] RESET"
OS_QEMU_FIRMWARE_SERIAL_READY_MARKER = "[OS][FIRMWARE] SERIAL_READY"
OS_QEMU_FIRMWARE_CLOCK_READY_MARKER = "[OS][FIRMWARE] CLOCK_READY"
OS_QEMU_FIRMWARE_STAGE1_HEADER_VALID_MARKER = (
    "[OS][FIRMWARE] STAGE1_HEADER_VALID"
)
OS_QEMU_FIRMWARE_STAGE1_LOADED_MARKER = "[OS][FIRMWARE] STAGE1_LOADED"
OS_QEMU_FIRMWARE_IDE_TIMEOUT_MARKER = "[OS][FIRMWARE] IDE_TIMEOUT"
OS_QEMU_FIRMWARE_IDE_ERROR_MARKER = "[OS][FIRMWARE] IDE_ERROR"
OS_QEMU_FIRMWARE_STAGE1_HEADER_INVALID_MARKER = (
    "[OS][FIRMWARE] STAGE1_HEADER_INVALID"
)
OS_QEMU_FIRMWARE_STAGE1_CHECKSUM_INVALID_MARKER = (
    "[OS][FIRMWARE] STAGE1_CHECKSUM_INVALID"
)
OS_QEMU_STAGE1_ENTERED_MARKER = "[OS][STAGE1] ENTERED"
OS_QEMU_STAGE1_A20_READY_MARKER = "[OS][STAGE1] A20_READY"
OS_QEMU_STAGE1_A20_INVALID_MARKER = "[OS][STAGE1] A20_INVALID"
OS_QEMU_STAGE1_GDT_READY_MARKER = "[OS][STAGE1] GDT_READY"
OS_QEMU_STAGE1_PROTECTED_MODE_MARKER = "[OS][STAGE1] PROTECTED_MODE"
OS_QEMU_STAGE1_PAGE_TABLES_READY_MARKER = (
    "[OS][STAGE1] PAGE_TABLES_READY"
)
OS_QEMU_STAGE1_PAGE_TABLES_INVALID_MARKER = (
    "[OS][STAGE1] PAGE_TABLES_INVALID"
)
OS_QEMU_STAGE1_PAE_READY_MARKER = "[OS][STAGE1] PAE_READY"
OS_QEMU_STAGE1_PAE_INVALID_MARKER = "[OS][STAGE1] PAE_INVALID"
OS_QEMU_STAGE1_LME_READY_MARKER = "[OS][STAGE1] LME_READY"
OS_QEMU_STAGE1_LME_INVALID_MARKER = "[OS][STAGE1] LME_INVALID"
OS_QEMU_STAGE1_PAGING_ENABLED_MARKER = "[OS][STAGE1] PAGING_ENABLED"
OS_QEMU_STAGE1_PAGING_INVALID_MARKER = "[OS][STAGE1] PAGING_INVALID"
OS_QEMU_STAGE1_LONG_MODE_MARKER = "[OS][STAGE1] LONG_MODE"
OS_QEMU_STAGE1_MEMORY_MAP_READY_MARKER = "[OS][STAGE1] MEMORY_MAP_READY"
OS_QEMU_STAGE1_MEMORY_MAP_INVALID_MARKER = "[OS][STAGE1] MEMORY_MAP_INVALID"
OS_QEMU_STAGE1_KERNEL_HEADER_VALID_MARKER = (
    "[OS][STAGE1] KERNEL_HEADER_VALID"
)
OS_QEMU_STAGE1_KERNEL_PAYLOAD_VALID_MARKER = (
    "[OS][STAGE1] KERNEL_PAYLOAD_VALID"
)
OS_QEMU_STAGE1_KERNEL_ELF_VALID_MARKER = (
    "[OS][STAGE1] KERNEL_ELF_VALID"
)
OS_QEMU_STAGE1_KERNEL_SEGMENTS_LOADED_MARKER = (
    "[OS][STAGE1] KERNEL_SEGMENTS_LOADED"
)
OS_QEMU_STAGE1_BOOT_INFO_READY_MARKER = (
    "[OS][STAGE1] BOOT_INFO_READY"
)
OS_QEMU_STAGE1_KERNEL_TRANSFER_MARKER = (
    "[OS][STAGE1] KERNEL_TRANSFER"
)
OS_QEMU_STAGE1_KERNEL_HEADER_INVALID_MARKER = (
    "[OS][STAGE1] KERNEL_HEADER_INVALID"
)
OS_QEMU_STAGE1_KERNEL_CHECKSUM_INVALID_MARKER = (
    "[OS][STAGE1] KERNEL_CHECKSUM_INVALID"
)
OS_QEMU_STAGE1_KERNEL_ELF_INVALID_MARKER = (
    "[OS][STAGE1] KERNEL_ELF_INVALID"
)
OS_QEMU_STAGE1_KERNEL_ATA_TIMEOUT_MARKER = (
    "[OS][STAGE1] KERNEL_ATA_TIMEOUT"
)
OS_QEMU_STAGE1_KERNEL_ATA_ERROR_MARKER = (
    "[OS][STAGE1] KERNEL_ATA_ERROR"
)
OS_QEMU_STAGE1_KERNEL_RETURNED_MARKER = (
    "[OS][STAGE1] KERNEL_RETURNED"
)
OS_QEMU_KERNEL_ENTERED_MARKER = "[OS][KERNEL] ENTERED"
OS_QEMU_KERNEL_BOOT_INFO_VALID_MARKER = (
    "[OS][KERNEL] BOOT_INFO_VALID"
)
OS_QEMU_KERNEL_BOOT_INFO_INVALID_MARKER = (
    "[OS][KERNEL] BOOT_INFO_INVALID"
)
OS_QEMU_KERNEL_BSS_ZEROED_MARKER = "[OS][KERNEL] BSS_ZEROED"
OS_QEMU_KERNEL_BSS_INVALID_MARKER = "[OS][KERNEL] BSS_INVALID"
OS_QEMU_KERNEL_CR3_VALID_MARKER = "[OS][KERNEL] CR3_VALID"
OS_QEMU_KERNEL_CR3_INVALID_MARKER = "[OS][KERNEL] CR3_INVALID"
OS_QEMU_KERNEL_GDT_READY_MARKER = "[OS][KERNEL] GDT_READY"
OS_QEMU_KERNEL_TSS_READY_MARKER = "[OS][KERNEL] TSS_READY"
OS_QEMU_KERNEL_IDT_READY_MARKER = "[OS][KERNEL] IDT_READY"
OS_QEMU_KERNEL_DESCRIPTOR_TABLES_VALID_MARKER = (
    "[OS][KERNEL] DESCRIPTOR_TABLES_VALID"
)
OS_QEMU_KERNEL_DESCRIPTOR_TABLES_INVALID_MARKER = (
    "[OS][KERNEL] DESCRIPTOR_TABLES_INVALID"
)
OS_QEMU_KERNEL_BREAKPOINT_HANDLED_MARKER = (
    "[OS][KERNEL] BREAKPOINT_HANDLED"
)
OS_QEMU_KERNEL_EXCEPTION_SELF_TEST_READY_MARKER = (
    "[OS][KERNEL] EXCEPTION_SELF_TEST_READY"
)
OS_QEMU_KERNEL_MEMORY_MAP_VALID_MARKER = "[OS][KERNEL] MEMORY_MAP_VALID"
OS_QEMU_KERNEL_MEMORY_MAP_ENTRIES_MARKER = "[OS][KERNEL] MEMORY_MAP_ENTRIES=0x"
OS_QEMU_KERNEL_MEMORY_DESCRIBED_MARKER = (
    "[OS][KERNEL] MEMORY_DESCRIBED_BYTES=0x"
)
OS_QEMU_KERNEL_MEMORY_USABLE_MARKER = "[OS][KERNEL] MEMORY_USABLE_BYTES=0x"
OS_QEMU_KERNEL_MEMORY_MANAGED_MARKER = "[OS][KERNEL] MEMORY_MANAGED_BYTES=0x"
OS_QEMU_KERNEL_MEMORY_MANAGED_LIMIT_MARKER = (
    "[OS][KERNEL] MEMORY_MANAGED_PHYSICAL_LIMIT=0x"
)
OS_QEMU_KERNEL_PHYSICAL_ADDRESS_WIDTH_MARKER = (
    "[OS][KERNEL] PHYSICAL_ADDRESS_BITS=0x"
)
OS_QEMU_KERNEL_VIRTUAL_ADDRESS_WIDTH_MARKER = (
    "[OS][KERNEL] VIRTUAL_ADDRESS_BITS=0x"
)
OS_QEMU_KERNEL_FRAME_STATE_STORAGE_ADDRESS_MARKER = (
    "[OS][KERNEL] FRAME_STATE_STORAGE_ADDRESS=0x"
)
OS_QEMU_KERNEL_FRAME_STATE_STORAGE_SIZE_MARKER = (
    "[OS][KERNEL] FRAME_STATE_STORAGE_BYTES=0x"
)
OS_QEMU_KERNEL_BUDDY_STORAGE_ADDRESS_MARKER = (
    "[OS][KERNEL] BUDDY_STORAGE_ADDRESS=0x"
)
OS_QEMU_KERNEL_BUDDY_STORAGE_SIZE_MARKER = (
    "[OS][KERNEL] BUDDY_STORAGE_BYTES=0x"
)
OS_QEMU_KERNEL_BUDDY_ALLOCATOR_READY_MARKER = (
    "[OS][KERNEL] BUDDY_ALLOCATOR_READY"
)
OS_QEMU_KERNEL_BUDDY_MAXIMUM_ORDER_MARKER = (
    "[OS][KERNEL] BUDDY_MAX_ORDER=0x"
)
OS_QEMU_KERNEL_BUDDY_FREE_BLOCK_COUNT_MARKER = (
    "[OS][KERNEL] BUDDY_FREE_BLOCKS=0x"
)
OS_QEMU_KERNEL_BUDDY_ACTIVE_BLOCK_COUNT_MARKER = (
    "[OS][KERNEL] BUDDY_ACTIVE_BLOCKS=0x"
)
OS_QEMU_KERNEL_BUDDY_SUCCESSFUL_ALLOCATION_COUNT_MARKER = (
    "[OS][KERNEL] BUDDY_SUCCESSFUL_ALLOCATIONS=0x"
)
OS_QEMU_KERNEL_BUDDY_RELEASE_COUNT_MARKER = (
    "[OS][KERNEL] BUDDY_RELEASES=0x"
)
OS_QEMU_KERNEL_BUDDY_SPLIT_COUNT_MARKER = (
    "[OS][KERNEL] BUDDY_SPLITS=0x"
)
OS_QEMU_KERNEL_BUDDY_MERGE_COUNT_MARKER = (
    "[OS][KERNEL] BUDDY_MERGES=0x"
)
OS_QEMU_KERNEL_BUDDY_LARGEST_FREE_ORDER_MARKER = (
    "[OS][KERNEL] BUDDY_LARGEST_FREE_ORDER=0x"
)
OS_QEMU_KERNEL_BUDDY_SELF_TEST_ADDRESS_MARKER = (
    "[OS][KERNEL] BUDDY_SELF_TEST_ADDRESS=0x"
)
OS_QEMU_KERNEL_BUDDY_SELF_TEST_ORDER_MARKER = (
    "[OS][KERNEL] BUDDY_SELF_TEST_ORDER=0x"
)
OS_QEMU_KERNEL_BUDDY_SELF_TEST_PASSED_MARKER = (
    "[OS][KERNEL] BUDDY_SELF_TEST_PASSED"
)
OS_QEMU_KERNEL_FRAME_ALLOCATOR_READY_MARKER = (
    "[OS][KERNEL] FRAME_ALLOCATOR_READY"
)
OS_QEMU_KERNEL_FREE_FRAMES_MARKER = "[OS][KERNEL] FREE_FRAMES=0x"
OS_QEMU_KERNEL_ALLOCATED_FRAMES_MARKER = "[OS][KERNEL] ALLOCATED_FRAMES=0x"
OS_QEMU_KERNEL_RESERVED_FRAMES_MARKER = "[OS][KERNEL] RESERVED_FRAMES=0x"
OS_QEMU_KERNEL_PAGING_READY_MARKER = "[OS][KERNEL] PAGING_READY"
OS_QEMU_KERNEL_PAGING_ROOT_MARKER = "[OS][KERNEL] PAGING_ROOT=0x"
OS_QEMU_KERNEL_DIRECT_MAP_BASE_MARKER = (
    "[OS][KERNEL] DIRECT_MAP_BASE=0xFFFF888000000000"
)
OS_QEMU_KERNEL_DIRECT_MAP_MAPPED_BYTES_MARKER = (
    "[OS][KERNEL] DIRECT_MAP_MAPPED_BYTES=0x"
)
OS_QEMU_KERNEL_DIRECT_MAP_LARGE_PAGE_COUNT_MARKER = (
    "[OS][KERNEL] DIRECT_MAP_2M_PAGES=0x"
)
OS_QEMU_KERNEL_DIRECT_MAP_SMALL_PAGE_COUNT_MARKER = (
    "[OS][KERNEL] DIRECT_MAP_4K_PAGES=0x"
)
OS_QEMU_KERNEL_HIGH_MEMORY_TEST_ADDRESS_MARKER = (
    "[OS][KERNEL] HIGH_MEMORY_TEST_ADDRESS=0x"
)
OS_QEMU_KERNEL_HIGH_MEMORY_VALIDATION_COMPLETE_MARKER = (
    "[OS][KERNEL] HIGH_MEMORY_VALIDATION_COMPLETE"
)
OS_QEMU_KERNEL_MEMORY_PERMISSIONS_VALID_MARKER = (
    "[OS][KERNEL] MEMORY_PERMISSIONS_VALID"
)
OS_QEMU_KERNEL_HEAP_READY_MARKER = "[OS][KERNEL] HEAP_READY"
OS_QEMU_KERNEL_HEAP_CAPACITY_MARKER = "[OS][KERNEL] HEAP_CAPACITY_BYTES=0x"
OS_QEMU_KERNEL_HEAP_ACTIVE_ALLOCATION_COUNT_MARKER = (
    "[OS][KERNEL] HEAP_ACTIVE_ALLOCATIONS=0x0000000000000000"
)
OS_QEMU_KERNEL_HEAP_PEAK_CONSUMED_BYTES_MARKER = (
    "[OS][KERNEL] HEAP_PEAK_CONSUMED_BYTES=0x"
)
OS_QEMU_KERNEL_HEAP_LARGEST_FREE_ALLOCATION_BYTES_MARKER = (
    "[OS][KERNEL] HEAP_LARGEST_FREE_ALLOCATION_BYTES=0x"
)
OS_QEMU_KERNEL_HEAP_SELF_TEST_PASSED_MARKER = (
    "[OS][KERNEL] HEAP_SELF_TEST_PASSED"
)
OS_QEMU_KERNEL_LEGACY_INTERRUPT_ROUTING_READY_MARKER = (
    "[OS][KERNEL] LEGACY_INTERRUPT_ROUTING_READY"
)
OS_QEMU_KERNEL_PIC_READY_MARKER = "[OS][KERNEL] PIC_READY"
OS_QEMU_KERNEL_PIC_MASK_MARKER = "[OS][KERNEL] PIC_MASK=0x"
OS_QEMU_KERNEL_PIT_READY_MARKER = "[OS][KERNEL] PIT_READY"
OS_QEMU_KERNEL_PIT_DIVISOR_MARKER = "[OS][KERNEL] PIT_DIVISOR=0x"
OS_QEMU_KERNEL_PIT_FREQUENCY_MARKER = "[OS][KERNEL] PIT_FREQUENCY_HZ=0x"
OS_QEMU_KERNEL_PS2_KEYBOARD_READY_MARKER = (
    "[OS][KERNEL] PS2_KEYBOARD_READY"
)
OS_QEMU_KERNEL_ATA_PIO_READY_MARKER = "[OS][KERNEL] ATA_PIO_READY"
OS_QEMU_KERNEL_ATA_BOOT_DESCRIPTOR_VALID_MARKER = (
    "[OS][KERNEL] ATA_BOOT_DESCRIPTOR_VALID"
)
OS_QEMU_KERNEL_FILE_SYSTEM_FORMATTED_MARKER = (
    "[OS][KERNEL] FILE_SYSTEM_FORMATTED"
)
OS_QEMU_KERNEL_FILE_SYSTEM_MOUNTED_MARKER = (
    "[OS][KERNEL] FILE_SYSTEM_MOUNTED"
)
OS_QEMU_KERNEL_FILE_SYSTEM_CORRUPT_MARKER = (
    "[OS][KERNEL] FILE_SYSTEM_CORRUPT"
)
OS_QEMU_KERNEL_FILE_SYSTEM_PERSISTENCE_RESTORED_MARKER = (
    "[OS][KERNEL] FILE_SYSTEM_PERSISTENCE_RESTORED"
)
OS_QEMU_KERNEL_FILE_SYSTEM_CONSISTENT_MARKER = (
    "[OS][KERNEL] FILE_SYSTEM_CONSISTENT"
)
OS_QEMU_KERNEL_FILE_SYSTEM_PAYLOAD_VALID_MARKER = (
    "[OS][KERNEL] FILE_SYSTEM_PAYLOAD_VALID"
)
OS_QEMU_KERNEL_FILE_SYSTEM_SYNCED_MARKER = (
    "[OS][KERNEL] FILE_SYSTEM_SYNCED"
)
OS_QEMU_KERNEL_PIC_SPURIOUS_SELF_TEST_PASSED_MARKER = (
    "[OS][KERNEL] PIC_SPURIOUS_SELF_TEST_PASSED"
)
OS_QEMU_KERNEL_INTERRUPTS_ENABLED_MARKER = "[OS][KERNEL] INTERRUPTS_ENABLED"
OS_QEMU_KERNEL_TIMER_TICKS_MARKER = "[OS][KERNEL] TIMER_TICKS=0x"
OS_QEMU_KERNEL_MONOTONIC_MILLISECONDS_MARKER = (
    "[OS][KERNEL] MONOTONIC_MILLISECONDS=0x"
)
OS_QEMU_KERNEL_TIMER_SELF_TEST_PASSED_MARKER = (
    "[OS][KERNEL] TIMER_SELF_TEST_PASSED"
)
OS_QEMU_KERNEL_USER_ELF_VALID_MARKER = "[OS][KERNEL] USER_ELF_VALID"
OS_QEMU_KERNEL_USER_ELF_REJECTED_MARKER = (
    "[OS][KERNEL] USER_ELF_REJECTED=0x0000000000000002"
)
OS_QEMU_KERNEL_USER_ENTRY_MARKER = (
    "[OS][KERNEL] USER_ENTRY=0x0000000040000000"
)
OS_QEMU_KERNEL_USER_MAPPED_PAGES_MARKER = (
    "[OS][KERNEL] USER_MAPPED_PAGES=0x"
)
OS_QEMU_KERNEL_USER_STACK_READY_MARKER = "[OS][KERNEL] USER_STACK_READY"
OS_QEMU_KERNEL_USER_RING3_ENTER_MARKER = "[OS][KERNEL] USER_RING3_ENTER"
OS_QEMU_USER_INVALID_POINTER_REJECTED_MARKER = (
    "[OS][USER] INVALID_POINTER_REJECTED"
)
OS_QEMU_USER_UNKNOWN_SYSTEM_CALL_REJECTED_MARKER = (
    "[OS][USER] UNKNOWN_SYSCALL_REJECTED"
)
OS_QEMU_USER_HELLO_FROM_RING3_MARKER = "[OS][USER] HELLO_FROM_RING3"
OS_QEMU_KERNEL_USER_EXIT_ZERO_MARKER = (
    "[OS][KERNEL] USER_EXIT_CODE=0x0000000000000000"
)
OS_QEMU_KERNEL_USER_SYSTEM_CALL_COUNT_MARKER = (
    "[OS][KERNEL] USER_SYSCALL_COUNT=0x"
)
OS_QEMU_KERNEL_USER_ZERO_SYSTEM_CALL_COUNT_MARKER = (
    "[OS][KERNEL] USER_SYSCALL_COUNT=0x0000000000000000"
)
OS_QEMU_KERNEL_USER_INVALID_OPCODE_VECTOR_MARKER = (
    "[OS][KERNEL] USER_EXCEPTION_VECTOR=0x0000000000000006"
)
OS_QEMU_KERNEL_USER_PAGE_FAULT_VECTOR_MARKER = (
    "[OS][KERNEL] USER_EXCEPTION_VECTOR=0x000000000000000E"
)
OS_QEMU_KERNEL_USER_EXCEPTION_ZERO_ERROR_CODE_MARKER = (
    "[OS][KERNEL] USER_EXCEPTION_ERROR_CODE=0x0000000000000000"
)
OS_QEMU_KERNEL_USER_PAGE_FAULT_ERROR_CODE_MARKER = (
    "[OS][KERNEL] USER_EXCEPTION_ERROR_CODE=0x0000000000000004"
)
OS_QEMU_KERNEL_USER_EXCEPTION_RIP_MARKER = (
    "[OS][KERNEL] USER_EXCEPTION_RIP=0x"
)
OS_QEMU_KERNEL_USER_PAGE_FAULT_ADDRESS_MARKER = (
    "[OS][KERNEL] USER_PAGE_FAULT_ADDRESS=0x0000000030000000"
)
OS_QEMU_KERNEL_USER_TERMINATED_MARKER = "[OS][KERNEL] USER_TERMINATED"
OS_QEMU_KERNEL_USER_RETURNED_TO_KERNEL_MARKER = (
    "[OS][KERNEL] USER_RETURNED_TO_KERNEL"
)
OS_QEMU_KERNEL_PROCESS_RUNTIME_READY_MARKER = (
    "[OS][KERNEL] PROCESS_RUNTIME_READY"
)
OS_QEMU_KERNEL_PIPE_READY_MARKER = "[OS][KERNEL] PIPE_READY"
OS_QEMU_KERNEL_PROCESS_ID_MARKER = "[OS][KERNEL] PROCESS_ID=0x"
OS_QEMU_KERNEL_PROCESS_CR3_MARKER = "[OS][KERNEL] PROCESS_CR3=0x"
OS_QEMU_KERNEL_PROCESS_KERNEL_STACK_TOP_MARKER = (
    "[OS][KERNEL] PROCESS_KERNEL_STACK_TOP=0x"
)
OS_QEMU_KERNEL_PROCESS_RUN_TICKS_MARKER = (
    "[OS][KERNEL] PROCESS_RUN_TICKS=0x"
)
OS_QEMU_KERNEL_PROCESS_DISPATCH_COUNT_MARKER = (
    "[OS][KERNEL] PROCESS_DISPATCH_COUNT=0x"
)
OS_QEMU_KERNEL_PROCESS_PIPE_READ_BYTES_MARKER = (
    "[OS][KERNEL] PROCESS_PIPE_READ_BYTES=0x"
)
OS_QEMU_KERNEL_PROCESS_PIPE_WRITTEN_BYTES_MARKER = (
    "[OS][KERNEL] PROCESS_PIPE_WRITTEN_BYTES=0x"
)
OS_QEMU_KERNEL_PROCESS_FILE_READ_BYTES_MARKER = (
    "[OS][KERNEL] PROCESS_FILE_READ_BYTES=0x"
)
OS_QEMU_KERNEL_PROCESS_FILE_WRITTEN_BYTES_MARKER = (
    "[OS][KERNEL] PROCESS_FILE_WRITTEN_BYTES=0x"
)
OS_QEMU_KERNEL_PROCESS_CONSOLE_READ_BYTES_MARKER = (
    "[OS][KERNEL] PROCESS_CONSOLE_READ_BYTES=0x"
)
OS_QEMU_KERNEL_PROCESS_CONSOLE_WRITTEN_BYTES_MARKER = (
    "[OS][KERNEL] PROCESS_CONSOLE_WRITTEN_BYTES=0x"
)
OS_QEMU_KERNEL_SCHEDULER_STARTED_MARKER = "[OS][KERNEL] SCHEDULER_STARTED"
OS_QEMU_KERNEL_SCHEDULER_CREATED_PROCESSES_MARKER = (
    "[OS][KERNEL] SCHEDULER_CREATED_PROCESSES=0x"
)
OS_QEMU_KERNEL_SCHEDULER_TERMINATED_PROCESSES_MARKER = (
    "[OS][KERNEL] SCHEDULER_TERMINATED_PROCESSES=0x"
)
OS_QEMU_KERNEL_SCHEDULER_TIMER_TICKS_MARKER = (
    "[OS][KERNEL] SCHEDULER_TIMER_TICKS=0x"
)
OS_QEMU_KERNEL_SCHEDULER_PREEMPTIONS_MARKER = (
    "[OS][KERNEL] SCHEDULER_PREEMPTIONS=0x"
)
OS_QEMU_KERNEL_SCHEDULER_DISPATCHES_MARKER = (
    "[OS][KERNEL] SCHEDULER_DISPATCHES=0x"
)
OS_QEMU_KERNEL_SCHEDULER_BLOCKS_MARKER = (
    "[OS][KERNEL] SCHEDULER_BLOCKS=0x"
)
OS_QEMU_KERNEL_SCHEDULER_WAKEUPS_MARKER = (
    "[OS][KERNEL] SCHEDULER_WAKEUPS=0x"
)
OS_QEMU_KERNEL_PIPE_CAPACITY_MARKER = (
    "[OS][KERNEL] PIPE_CAPACITY_BYTES=0x0000000000000040"
)
OS_QEMU_KERNEL_PIPE_WRITTEN_BYTES_MARKER = (
    "[OS][KERNEL] PIPE_WRITTEN_BYTES=0x0000000000000100"
)
OS_QEMU_KERNEL_PIPE_READ_BYTES_MARKER = (
    "[OS][KERNEL] PIPE_READ_BYTES=0x0000000000000100"
)
OS_QEMU_KERNEL_PIPE_WRITER_BLOCKS_MARKER = (
    "[OS][KERNEL] PIPE_WRITER_BLOCKS=0x"
)
OS_QEMU_KERNEL_PIPE_END_OF_FILE_MARKER = (
    "[OS][KERNEL] PIPE_EOF_OBSERVATIONS=0x0000000000000001"
)
OS_QEMU_KERNEL_CONSOLE_SUBMITTED_BYTES_MARKER = (
    "[OS][KERNEL] CONSOLE_SUBMITTED_BYTES=0x"
)
OS_QEMU_KERNEL_CONSOLE_READ_BYTES_MARKER = (
    "[OS][KERNEL] CONSOLE_READ_BYTES=0x"
)
OS_QEMU_KERNEL_CONSOLE_DROPPED_BYTES_MARKER = (
    "[OS][KERNEL] CONSOLE_DROPPED_BYTES=0x0000000000000000"
)
OS_QEMU_KERNEL_CONSOLE_BUFFERED_BYTES_MARKER = (
    "[OS][KERNEL] CONSOLE_BUFFERED_BYTES=0x0000000000000000"
)
OS_QEMU_KERNEL_PIPE_TRANSFER_VALID_MARKER = (
    "[OS][KERNEL] PIPE_TRANSFER_VALID"
)
OS_QEMU_KERNEL_PIPE_ENDPOINTS_CLOSED_MARKER = (
    "[OS][KERNEL] PIPE_ENDPOINTS_CLOSED"
)
OS_QEMU_KERNEL_PROCESS_RESOURCES_RECLAIMED_MARKER = (
    "[OS][KERNEL] PROCESS_RESOURCES_RECLAIMED"
)
OS_QEMU_KERNEL_SCHEDULER_COMPLETE_MARKER = "[OS][KERNEL] SCHEDULER_COMPLETE"
OS_QEMU_USER_WORKER_PROCESS_2_STEP_1_MARKER = (
    "[OS][USER][PID2] WORKER_STEP_1"
)
OS_QEMU_USER_WORKER_PROCESS_2_STEP_2_MARKER = (
    "[OS][USER][PID2] WORKER_STEP_2"
)
OS_QEMU_USER_WORKER_PROCESS_2_STEP_3_MARKER = (
    "[OS][USER][PID2] WORKER_STEP_3"
)
OS_QEMU_USER_WORKER_PROCESS_3_STEP_1_MARKER = (
    "[OS][USER][PID3] WORKER_STEP_1"
)
OS_QEMU_USER_WORKER_PROCESS_3_STEP_2_MARKER = (
    "[OS][USER][PID3] WORKER_STEP_2"
)
OS_QEMU_USER_WORKER_PROCESS_3_STEP_3_MARKER = (
    "[OS][USER][PID3] WORKER_STEP_3"
)
OS_QEMU_USER_WORKER_PROCESS_4_STEP_1_MARKER = (
    "[OS][USER][PID4] WORKER_STEP_1"
)
OS_QEMU_USER_WORKER_PROCESS_4_STEP_2_MARKER = (
    "[OS][USER][PID4] WORKER_STEP_2"
)
OS_QEMU_USER_WORKER_PROCESS_4_STEP_3_MARKER = (
    "[OS][USER][PID4] WORKER_STEP_3"
)
OS_QEMU_USER_ADDRESS_SPACE_ISOLATED_MARKER = (
    "[OS][USER] ADDRESS_SPACE_ISOLATED"
)
OS_QEMU_USER_PIPE_PRODUCER_STARTED_MARKER = (
    "[OS][USER][PIPE] PRODUCER_STARTED"
)
OS_QEMU_USER_PIPE_PRODUCER_COMPLETED_MARKER = (
    "[OS][USER][PIPE] PRODUCER_COMPLETED"
)
OS_QEMU_USER_PIPE_CONSUMER_STARTED_MARKER = (
    "[OS][USER][PIPE] CONSUMER_STARTED"
)
OS_QEMU_USER_PIPE_PAYLOAD_VERIFIED_MARKER = (
    "[OS][USER][PIPE] PAYLOAD_VERIFIED"
)
OS_QEMU_USER_PIPE_END_OF_FILE_MARKER = (
    "[OS][USER][PIPE] EOF_OBSERVED"
)
OS_QEMU_USER_FILE_WRITTEN_MARKER = "[OS][USER][FS] FILE_WRITTEN"
OS_QEMU_USER_FILE_VERIFIED_MARKER = "[OS][USER][FS] FILE_VERIFIED"
OS_QEMU_USER_SHELL_READY_MARKER = "[OS][USER][SHELL] READY"
OS_QEMU_USER_SHELL_HELP_MARKER = "[OS][USER][SHELL] COMMAND=HELP"
OS_QEMU_USER_SHELL_ECHO_MARKER = "[OS][USER][SHELL] COMMAND=ECHO"
OS_QEMU_USER_SHELL_PWD_MARKER = "[OS][USER][SHELL] COMMAND=PWD"
OS_QEMU_USER_SHELL_MKDIR_MARKER = "[OS][USER][SHELL] COMMAND=MKDIR"
OS_QEMU_USER_SHELL_WRITE_MARKER = "[OS][USER][SHELL] COMMAND=WRITE"
OS_QEMU_USER_SHELL_CAT_MARKER = "[OS][USER][SHELL] COMMAND=CAT"
OS_QEMU_USER_SHELL_LS_MARKER = "[OS][USER][SHELL] COMMAND=LS"
OS_QEMU_USER_SHELL_SYNC_MARKER = "[OS][USER][SHELL] COMMAND=SYNC"
OS_QEMU_USER_SHELL_UNKNOWN_MARKER = (
    "[OS][USER][SHELL] UNKNOWN_COMMAND_REJECTED"
)
OS_QEMU_USER_SHELL_EXIT_COMMAND_MARKER = (
    "[OS][USER][SHELL] COMMAND=EXIT"
)
OS_QEMU_USER_SHELL_EXIT_MARKER = "[OS][USER][SHELL] EXIT"
OS_QEMU_USER_SHELL_TEST_INPUT = (
    "help\n"
    "echo hello world\n"
    "pwd\n"
    "mkdir /demo\n"
    "write /demo/message hello\n"
    "cat /demo/message\n"
    "ls /demo\n"
    "sync\n"
    "unknown\n"
    "exit\n"
)
OS_QEMU_KERNEL_USER_RESULT_INVALID_MARKER = (
    "[OS][KERNEL] USER_RESULT_INVALID"
)
OS_QEMU_KERNEL_KEYBOARD_SCANCODE_MARKER = (
    "[OS][KERNEL] KEYBOARD_SCANCODE=0x000000000000001E"
)
OS_QEMU_KERNEL_KEYBOARD_A_PRESSED_MARKER = (
    "[OS][KERNEL] KEYBOARD_EVENT=A_PRESSED"
)
OS_QEMU_KERNEL_DEVICE_INITIALIZATION_FAILED_MARKER = (
    "[OS][KERNEL] DEVICE_INITIALIZATION_FAILED="
)
OS_QEMU_KERNEL_INVALID_OPCODE_INJECTION_MARKER = (
    "[OS][KERNEL] FAULT_INJECTION=INVALID_OPCODE"
)
OS_QEMU_KERNEL_PAGE_FAULT_INJECTION_MARKER = (
    "[OS][KERNEL] FAULT_INJECTION=PAGE_FAULT"
)
OS_QEMU_KERNEL_WRITE_PROTECTION_INJECTION_MARKER = (
    "[OS][KERNEL] FAULT_INJECTION=WRITE_PROTECTION"
)
OS_QEMU_KERNEL_EXCEPTION_MARKER = "[OS][KERNEL] EXCEPTION\n"
OS_QEMU_KERNEL_INVALID_OPCODE_VECTOR_MARKER = (
    "[OS][KERNEL] EXCEPTION_VECTOR=0x0000000000000006"
)
OS_QEMU_KERNEL_PAGE_FAULT_VECTOR_MARKER = (
    "[OS][KERNEL] EXCEPTION_VECTOR=0x000000000000000E"
)
OS_QEMU_KERNEL_EXCEPTION_ZERO_ERROR_CODE_MARKER = (
    "[OS][KERNEL] EXCEPTION_ERROR_CODE=0x0000000000000000"
)
OS_QEMU_KERNEL_WRITE_PROTECTION_ERROR_CODE_MARKER = (
    "[OS][KERNEL] EXCEPTION_ERROR_CODE=0x0000000000000003"
)
OS_QEMU_KERNEL_PAGE_FAULT_ADDRESS_MARKER = (
    "[OS][KERNEL] PAGE_FAULT_ADDRESS=0x0000000004000000"
)
OS_QEMU_KERNEL_WRITE_PROTECTION_ADDRESS_MARKER = (
    "[OS][KERNEL] PAGE_FAULT_ADDRESS=0xFFFF800000100000"
)
OS_QEMU_KERNEL_PANIC_MARKER = "[OS][KERNEL] PANIC"
OS_QEMU_KERNEL_FILE_SIZE_MARKER = "[OS][KERNEL] FILE_SIZE=0x"
OS_QEMU_KERNEL_LOAD_SEGMENTS_MARKER = (
    "[OS][KERNEL] LOAD_SEGMENTS=0x"
)
OS_QEMU_KERNEL_READY_MARKER = "[OS][KERNEL] READY"


def validateImageSize(
    imagePath: Path,
    expectedSizeBytes: int,
    imageDescription: str,
) -> None:
    actualSizeBytes = imagePath.stat().st_size
    if actualSizeBytes != expectedSizeBytes:
        raise OsToolError(
            f"{imageDescription}大小不正确："
            f"实际 {actualSizeBytes} 字节，预期 {expectedSizeBytes} 字节"
        )


def runQemuHardwareSmoke(
    projectRoot: Path,
    firmwareImagePath: Path,
    diskImagePath: Path,
    expectedFirmwareSizeBytes: int,
    expectedDiskSizeBytes: int,
) -> None:
    validateImageSize(
        firmwareImagePath,
        expectedFirmwareSizeBytes,
        "空固件镜像",
    )
    validateImageSize(
        diskImagePath,
        expectedDiskSizeBytes,
        "空磁盘镜像",
    )

    command = [
        "qemu-system-x86_64",
        "-machine",
        "pc,accel=tcg",
        "-cpu",
        "qemu64",
        "-m",
        str(OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES),
        "-nodefaults",
        "-display",
        "none",
        "-serial",
        "none",
        "-monitor",
        "none",
        "-S",
        "-no-reboot",
        "-no-shutdown",
        "-bios",
        str(firmwareImagePath),
        "-drive",
        f"file={diskImagePath},format=raw,if=ide,snapshot=on",
    ]

    try:
        completedProcess = runCommand(
            command,
            projectRoot,
            check=False,
            timeoutSeconds=OS_QEMU_SMOKE_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        print("QEMU TCG 已使用自定义空固件与空磁盘稳定启动。")
        return

    raise OsToolError(
        f"QEMU 硬件冒烟测试异常提前退出：{completedProcess.returncode}"
    )


def createQemuFirmwareCommand(
    firmwareImagePath: Path,
    diskImagePath: Path,
    qmpSocketPath: Path | None = None,
    persistentDiskWrites: bool = False,
    memoryMebibytes: int = OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES,
) -> list[str]:
    if memoryMebibytes < OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES:
        raise OsToolError(
            "QEMU 客户机内存不得低于"
            f" {OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES} MiB"
        )
    snapshotMode = "off" if persistentDiskWrites else "on"
    command = [
        "qemu-system-x86_64",
        "-machine",
        "pc,accel=tcg",
        "-cpu",
        "qemu64",
        "-m",
        str(memoryMebibytes),
        "-nodefaults",
        "-display",
        "none",
        "-serial",
        "stdio",
        "-monitor",
        "none",
        "-no-reboot",
        "-no-shutdown",
        "-bios",
        str(firmwareImagePath),
        "-drive",
        f"file={diskImagePath},format=raw,if=ide,snapshot={snapshotMode}",
    ]
    if qmpSocketPath is not None:
        command.extend(
            (
                "-qmp",
                f"unix:{qmpSocketPath},server=on,wait=off",
            )
        )
    return command


def normalizeCapturedOutput(output: str | bytes | None) -> str:
    if output is None:
        return ""
    if isinstance(output, bytes):
        return output.decode("utf-8", errors="replace")
    return output


def runQemuWithTimedSerial(
    command: list[str],
    projectRoot: Path,
    timeoutSeconds: float,
    lineObserver: Callable[[str], None] | None = None,
    completionEvent: threading.Event | None = None,
) -> tuple[str, str, bool, bool, int]:
    """逐行捕获串口，并按最终里程碑或总截止条件回收目标进程。"""
    normalizedCommand = [str(argument) for argument in command]
    print(f"+ {shlex.join(normalizedCommand)}", flush=True)

    startTime = time.monotonic()
    serialLines: list[str] = []
    timedLines: list[str] = []
    qemuProcess = subprocess.Popen(
        normalizedCommand,
        cwd=projectRoot,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )

    def captureSerialLines() -> None:
        if qemuProcess.stdout is None:
            return
        for line in qemuProcess.stdout:
            elapsedMilliseconds = int(
                (time.monotonic() - startTime) * 1000
            )
            serialLines.append(line)
            timedLines.append(
                f"[QEMU][T+{elapsedMilliseconds:06d}ms] "
                f"{line.rstrip('\r\n')}\n"
            )
            if lineObserver is not None:
                lineObserver(line)

    captureThread = threading.Thread(
        target=captureSerialLines,
        name="os-qemu-serial-capture",
        daemon=True,
    )
    captureThread.start()

    timedOut = False
    completedByObserver = False
    try:
        completionDeadline = startTime + timeoutSeconds
        while qemuProcess.poll() is None:
            if completionEvent is not None and completionEvent.is_set():
                completedByObserver = True
                time.sleep(OS_QEMU_COMPLETION_SETTLE_SECONDS)
                break
            remainingSeconds = completionDeadline - time.monotonic()
            if remainingSeconds <= 0.0:
                timedOut = True
                break
            waitSeconds = min(
                remainingSeconds,
                OS_QEMU_COMPLETION_POLL_INTERVAL_SECONDS,
            )
            if completionEvent is None:
                time.sleep(waitSeconds)
            else:
                completionEvent.wait(waitSeconds)

        if qemuProcess.poll() is None:
            qemuProcess.terminate()
            try:
                returnCode = qemuProcess.wait(
                    timeout=OS_QEMU_TERMINATION_TIMEOUT_SECONDS
                )
            except subprocess.TimeoutExpired:
                qemuProcess.kill()
                returnCode = qemuProcess.wait()
        else:
            returnCode = qemuProcess.returncode
    finally:
        if qemuProcess.poll() is None:
            qemuProcess.kill()
            qemuProcess.wait()
        captureThread.join()
        if qemuProcess.stdout is not None:
            qemuProcess.stdout.close()

    return (
        "".join(serialLines),
        "".join(timedLines),
        timedOut,
        completedByObserver,
        returnCode,
    )


def waitForQmpResponse(
    qmpStream: socket.SocketIO,
) -> dict[str, object]:
    for _ in range(OS_QEMU_QMP_MAXIMUM_RESPONSE_COUNT):
        responseLine = qmpStream.readline()
        if not responseLine:
            raise OsToolError("QMP 在返回命令结果前关闭连接。")
        response = json.loads(responseLine.decode("utf-8"))
        if "return" in response or "error" in response:
            return response
    raise OsToolError("QMP 返回事件过多，未找到命令结果。")


def sendQmpCommand(
    qmpStream: socket.SocketIO,
    command: dict[str, object],
) -> None:
    qmpStream.write(
        json.dumps(command, separators=(",", ":")).encode("utf-8")
        + b"\r\n"
    )
    qmpStream.flush()
    response = waitForQmpResponse(qmpStream)
    if "error" in response:
        raise OsToolError(f"QMP 命令执行失败：{response['error']!r}")


def qemuKeyNameForCharacter(character: str) -> str:
    if len(character) != 1:
        raise OsToolError("QEMU 键盘映射只接受单个字符。")
    if "a" <= character <= "z" or "0" <= character <= "9":
        return character
    keyNames = {
        "\n": "ret",
        " ": "spc",
        "/": "slash",
        ".": "dot",
        "-": "minus",
        "=": "equal",
        ",": "comma",
    }
    if character not in keyNames:
        raise OsToolError(
            f"QEMU 键盘输入包含未支持字符：{character!r}"
        )
    return keyNames[character]


def injectQemuText(
    qmpSocketPath: Path,
    inputText: str,
    readyEvent: threading.Event,
    finishedEvent: threading.Event,
    failureMessages: list[str],
) -> None:
    if not readyEvent.wait(OS_QEMU_QMP_READY_TIMEOUT_SECONDS):
        return

    connectionDeadline = (
        time.monotonic() + OS_QEMU_QMP_CONNECTION_TIMEOUT_SECONDS
    )
    while (
        not qmpSocketPath.exists()
        and not finishedEvent.is_set()
        and time.monotonic() < connectionDeadline
    ):
        time.sleep(OS_QEMU_QMP_RETRY_INTERVAL_SECONDS)
    if finishedEvent.is_set():
        return

    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as qmpSocket:
            qmpSocket.settimeout(OS_QEMU_QMP_CONNECTION_TIMEOUT_SECONDS)
            qmpSocket.connect(str(qmpSocketPath))
            with qmpSocket.makefile("rwb", buffering=0) as qmpStream:
                greetingLine = qmpStream.readline()
                if not greetingLine:
                    raise OsToolError("QMP 未返回握手信息。")
                greeting = json.loads(greetingLine.decode("utf-8"))
                if "QMP" not in greeting:
                    raise OsToolError("QMP 握手缺少版本对象。")
                sendQmpCommand(qmpStream, {"execute": "qmp_capabilities"})
                for character in inputText:
                    if finishedEvent.is_set():
                        return
                    keyName = qemuKeyNameForCharacter(character)
                    sendQmpCommand(
                        qmpStream,
                        {
                            "execute": "human-monitor-command",
                            "arguments": {
                                "command-line": f"sendkey {keyName} 1",
                            },
                        },
                    )
                    time.sleep(OS_QEMU_KEY_INJECTION_INTERVAL_SECONDS)
    except (OSError, ValueError, OsToolError) as error:
        failureMessages.append(str(error))


def validateSerialProtocol(
    serialOutput: str,
    requiredMarkers: tuple[str, ...],
    forbiddenMarkers: tuple[str, ...],
    expectedMarkerCounts: tuple[tuple[str, int], ...] = (),
    minimumHexMarkerValues: tuple[tuple[str, int], ...] = (),
) -> None:
    previousMarkerPosition = 0
    for requiredMarker in requiredMarkers:
        markerPosition = serialOutput.find(
            requiredMarker,
            previousMarkerPosition,
        )
        if markerPosition < 0:
            raise OsToolError(
                "串口输出缺少必需标记或标记顺序错误："
                f"{requiredMarker!r}"
            )
        previousMarkerPosition = markerPosition + len(requiredMarker)

    for forbiddenMarker in forbiddenMarkers:
        if forbiddenMarker in serialOutput:
            raise OsToolError(
                f"串口输出包含禁止标记：{forbiddenMarker!r}"
            )

    for marker, expectedCount in expectedMarkerCounts:
        actualCount = serialOutput.count(marker)
        if actualCount != expectedCount:
            raise OsToolError(
                "串口标记出现次数不正确："
                f"{marker!r} 实际 {actualCount} 次，预期 {expectedCount} 次"
            )

    for marker, minimumValue in minimumHexMarkerValues:
        markerPattern = re.compile(
            re.escape(marker) + r"([0-9A-Fa-f]{16})(?:\r?\n|$)"
        )
        markerValues = [
            int(match.group(1), 16)
            for match in markerPattern.finditer(serialOutput)
        ]
        if not markerValues or min(markerValues) < minimumValue:
            raise OsToolError(
                "串口十六进制统计未达到下界："
                f"{marker!r} 下界 {minimumValue}"
            )


def runQemuFirmwareBoot(
    projectRoot: Path,
    firmwareImagePath: Path,
    diskImagePath: Path,
    expectedFirmwareSizeBytes: int,
    expectedDiskSizeBytes: int,
    requiredMarkers: tuple[str, ...],
    forbiddenMarkers: tuple[str, ...],
    keyboardInputText: str | None = None,
    keyboardReadyMarker: str = OS_QEMU_KERNEL_READY_MARKER,
    expectedMarkerCounts: tuple[tuple[str, int], ...] = (),
    minimumHexMarkerValues: tuple[tuple[str, int], ...] = (),
    persistentDiskWrites: bool = False,
    memoryMebibytes: int = OS_QEMU_MINIMUM_GUEST_MEMORY_MEBIBYTES,
) -> None:
    validateImageSize(
        firmwareImagePath,
        expectedFirmwareSizeBytes,
        "固件 ROM",
    )
    validateImageSize(
        diskImagePath,
        expectedDiskSizeBytes,
        "启动磁盘镜像",
    )

    with tempfile.TemporaryDirectory(prefix="os-qemu-") as temporaryDirectory:
        qmpSocketPath = Path(temporaryDirectory) / "qmp.sock"
        keyboardReadyEvent = threading.Event()
        protocolCompleteEvent = threading.Event()
        qemuFinishedEvent = threading.Event()
        qmpFailureMessages: list[str] = []
        finalRequiredMarker = requiredMarkers[-1]

        def observeSerialLine(line: str) -> None:
            if keyboardReadyMarker in line:
                keyboardReadyEvent.set()
            if finalRequiredMarker in line:
                protocolCompleteEvent.set()

        qmpThread: threading.Thread | None = None
        if keyboardInputText is not None:
            qmpThread = threading.Thread(
                target=injectQemuText,
                args=(
                    qmpSocketPath,
                    keyboardInputText,
                    keyboardReadyEvent,
                    qemuFinishedEvent,
                    qmpFailureMessages,
                ),
                name="os-qemu-keyboard-injection",
                daemon=True,
            )
            qmpThread.start()

        command = createQemuFirmwareCommand(
            firmwareImagePath,
            diskImagePath,
            qmpSocketPath if keyboardInputText is not None else None,
            persistentDiskWrites,
            memoryMebibytes,
        )
        try:
            (
                serialOutput,
                timedSerialOutput,
                timedOut,
                completedByObserver,
                returnCode,
            ) = runQemuWithTimedSerial(
                command,
                projectRoot,
                OS_QEMU_FIRMWARE_TIMEOUT_SECONDS,
                observeSerialLine,
                protocolCompleteEvent,
            )
        finally:
            qemuFinishedEvent.set()
            keyboardReadyEvent.set()
            if qmpThread is not None:
                qmpThread.join()

    if qmpFailureMessages:
        print(timedSerialOutput, end="")
        raise OsToolError(
            "QEMU 键盘注入失败：" + "; ".join(qmpFailureMessages)
        )
    if timedOut or completedByObserver:
        try:
            validateSerialProtocol(
                serialOutput,
                requiredMarkers,
                forbiddenMarkers,
                expectedMarkerCounts,
                minimumHexMarkerValues,
            )
        except OsToolError:
            print(timedSerialOutput, end="")
            raise
        print(timedSerialOutput, end="")
        print("QEMU 固件串口协议验收通过。")
        return

    raise OsToolError(
        f"QEMU 固件测试异常提前退出：{returnCode}"
    )


def runQemuFileSystemPersistence(
    projectRoot: Path,
    firmwareImagePath: Path,
    diskImagePath: Path,
    expectedFirmwareSizeBytes: int,
    expectedDiskSizeBytes: int,
) -> None:
    """在同一可写磁盘上连续启动两次，并对第三次启动注入超级块损坏。"""
    validateImageSize(
        firmwareImagePath,
        expectedFirmwareSizeBytes,
        "固件 ROM",
    )
    validateImageSize(
        diskImagePath,
        expectedDiskSizeBytes,
        "启动磁盘镜像",
    )
    forbiddenRuntimeFailureMarkers = (
        OS_QEMU_KERNEL_FILE_SYSTEM_CORRUPT_MARKER,
        OS_QEMU_KERNEL_USER_RESULT_INVALID_MARKER,
        OS_QEMU_KERNEL_EXCEPTION_MARKER,
        OS_QEMU_KERNEL_PANIC_MARKER,
    )
    with tempfile.TemporaryDirectory(
        prefix="os-qemu-file-system-"
    ) as temporaryDirectory:
        writableDiskPath = Path(temporaryDirectory) / "persistent_disk.img"
        shutil.copyfile(diskImagePath, writableDiskPath)

        runQemuFirmwareBoot(
            projectRoot,
            firmwareImagePath,
            writableDiskPath,
            expectedFirmwareSizeBytes,
            expectedDiskSizeBytes,
            (
                OS_QEMU_KERNEL_FILE_SYSTEM_FORMATTED_MARKER,
                OS_QEMU_USER_FILE_WRITTEN_MARKER,
                OS_QEMU_USER_FILE_VERIFIED_MARKER,
                OS_QEMU_KERNEL_FILE_SYSTEM_SYNCED_MARKER,
                OS_QEMU_KERNEL_FILE_SYSTEM_PAYLOAD_VALID_MARKER,
                OS_QEMU_KERNEL_READY_MARKER,
            ),
            forbiddenRuntimeFailureMarkers,
            keyboardInputText=OS_QEMU_USER_SHELL_TEST_INPUT,
            keyboardReadyMarker=OS_QEMU_USER_SHELL_READY_MARKER,
            persistentDiskWrites=True,
        )
        runQemuFirmwareBoot(
            projectRoot,
            firmwareImagePath,
            writableDiskPath,
            expectedFirmwareSizeBytes,
            expectedDiskSizeBytes,
            (
                OS_QEMU_KERNEL_FILE_SYSTEM_MOUNTED_MARKER,
                OS_QEMU_KERNEL_FILE_SYSTEM_PERSISTENCE_RESTORED_MARKER,
                OS_QEMU_USER_FILE_WRITTEN_MARKER,
                OS_QEMU_USER_FILE_VERIFIED_MARKER,
                OS_QEMU_KERNEL_FILE_SYSTEM_PAYLOAD_VALID_MARKER,
                OS_QEMU_KERNEL_READY_MARKER,
            ),
            forbiddenRuntimeFailureMarkers,
            keyboardInputText=OS_QEMU_USER_SHELL_TEST_INPUT,
            keyboardReadyMarker=OS_QEMU_USER_SHELL_READY_MARKER,
            persistentDiskWrites=True,
        )

        superblockByteOffset = (
            OS_QEMU_FILE_SYSTEM_START_LBA *
            OS_QEMU_FILE_SYSTEM_SECTOR_SIZE_BYTES +
            OS_QEMU_FILE_SYSTEM_CORRUPTION_OFFSET_BYTES
        )
        with writableDiskPath.open("r+b") as writableDisk:
            writableDisk.seek(superblockByteOffset)
            originalByte = writableDisk.read(1)
            if len(originalByte) != 1:
                raise OsToolError("无法读取文件系统超级块故障注入字节。")
            writableDisk.seek(superblockByteOffset)
            writableDisk.write(
                bytes(
                    (
                        originalByte[0] ^
                        OS_QEMU_FILE_SYSTEM_CORRUPTION_MASK,
                    )
                )
            )

        runQemuFirmwareBoot(
            projectRoot,
            firmwareImagePath,
            writableDiskPath,
            expectedFirmwareSizeBytes,
            expectedDiskSizeBytes,
            (OS_QEMU_KERNEL_FILE_SYSTEM_CORRUPT_MARKER,),
            (
                OS_QEMU_KERNEL_USER_RING3_ENTER_MARKER,
                OS_QEMU_KERNEL_FILE_SYSTEM_PAYLOAD_VALID_MARKER,
                OS_QEMU_KERNEL_READY_MARKER,
            ),
            persistentDiskWrites=True,
        )
