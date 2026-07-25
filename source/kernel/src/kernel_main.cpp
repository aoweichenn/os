#include "os/kernel/kernel_main.hpp"

#include "os/kernel/ata_pio.hpp"
#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/file_system.hpp"
#include "os/kernel/interrupt_runtime.hpp"
#include "os/kernel/memory_manager.hpp"
#include "os/kernel/process_memory_layout.hpp"
#include "os/kernel/process_runtime.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/serial_port.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MAIN_BSS_PROBE_ZERO_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MAIN_BSS_PROBE_WRITTEN_VALUE = 0xB007B007B007B007ULL;
constexpr char OS_KERNEL_MAIN_ENTERED_MESSAGE[] = "[OS][KERNEL] ENTERED\r\n";
constexpr char OS_KERNEL_MAIN_BOOT_INFO_VALID_MESSAGE[] = "[OS][KERNEL] BOOT_INFO_VALID\r\n";
constexpr char OS_KERNEL_MAIN_BOOT_INFO_INVALID_MESSAGE[] = "[OS][KERNEL] BOOT_INFO_INVALID\r\n";
constexpr char OS_KERNEL_MAIN_BSS_ZEROED_MESSAGE[] = "[OS][KERNEL] BSS_ZEROED\r\n";
constexpr char OS_KERNEL_MAIN_BSS_INVALID_MESSAGE[] = "[OS][KERNEL] BSS_INVALID\r\n";
constexpr char OS_KERNEL_MAIN_CR3_VALID_MESSAGE[] = "[OS][KERNEL] CR3_VALID\r\n";
constexpr char OS_KERNEL_MAIN_CR3_INVALID_MESSAGE[] = "[OS][KERNEL] CR3_INVALID\r\n";
constexpr char OS_KERNEL_MAIN_GDT_READY_MESSAGE[] = "[OS][KERNEL] GDT_READY\r\n";
constexpr char OS_KERNEL_MAIN_TSS_READY_MESSAGE[] = "[OS][KERNEL] TSS_READY\r\n";
constexpr char OS_KERNEL_MAIN_IDT_READY_MESSAGE[] = "[OS][KERNEL] IDT_READY\r\n";
constexpr char OS_KERNEL_MAIN_DESCRIPTOR_TABLES_VALID_MESSAGE[] =
    "[OS][KERNEL] DESCRIPTOR_TABLES_VALID\r\n";
constexpr char OS_KERNEL_MAIN_DESCRIPTOR_TABLES_INVALID_MESSAGE[] =
    "[OS][KERNEL] DESCRIPTOR_TABLES_INVALID\r\n";
constexpr char OS_KERNEL_MAIN_EXCEPTION_SELF_TEST_READY_MESSAGE[] =
    "[OS][KERNEL] EXCEPTION_SELF_TEST_READY\r\n";
constexpr char OS_KERNEL_MAIN_MEMORY_MAP_VALID_MESSAGE[] = "[OS][KERNEL] MEMORY_MAP_VALID\r\n";
constexpr char OS_KERNEL_MAIN_MEMORY_INITIALIZATION_FAILED_PREFIX[] =
    "[OS][KERNEL] MEMORY_INITIALIZATION_FAILED=";
constexpr char OS_KERNEL_MAIN_MEMORY_MAP_ENTRY_COUNT_PREFIX[] = "[OS][KERNEL] MEMORY_MAP_ENTRIES=";
constexpr char OS_KERNEL_MAIN_MEMORY_DESCRIBED_PREFIX[] = "[OS][KERNEL] MEMORY_DESCRIBED_BYTES=";
constexpr char OS_KERNEL_MAIN_MEMORY_USABLE_PREFIX[] = "[OS][KERNEL] MEMORY_USABLE_BYTES=";
constexpr char OS_KERNEL_MAIN_MEMORY_MANAGED_PREFIX[] = "[OS][KERNEL] MEMORY_MANAGED_BYTES=";
constexpr char OS_KERNEL_MAIN_MEMORY_MANAGED_LIMIT_PREFIX[] =
    "[OS][KERNEL] MEMORY_MANAGED_PHYSICAL_LIMIT=";
constexpr char OS_KERNEL_MAIN_PHYSICAL_ADDRESS_WIDTH_PREFIX[] =
    "[OS][KERNEL] PHYSICAL_ADDRESS_BITS=";
constexpr char OS_KERNEL_MAIN_VIRTUAL_ADDRESS_WIDTH_PREFIX[] = "[OS][KERNEL] VIRTUAL_ADDRESS_BITS=";
constexpr char OS_KERNEL_MAIN_FIVE_LEVEL_PAGING_SUPPORTED_PREFIX[] =
    "[OS][KERNEL] FIVE_LEVEL_PAGING_SUPPORTED=";
constexpr char OS_KERNEL_MAIN_FRAME_STATE_STORAGE_ADDRESS_PREFIX[] =
    "[OS][KERNEL] FRAME_STATE_STORAGE_ADDRESS=";
constexpr char OS_KERNEL_MAIN_FRAME_STATE_STORAGE_SIZE_PREFIX[] =
    "[OS][KERNEL] FRAME_STATE_STORAGE_BYTES=";
constexpr char OS_KERNEL_MAIN_BUDDY_STORAGE_ADDRESS_PREFIX[] =
    "[OS][KERNEL] BUDDY_STORAGE_ADDRESS=";
constexpr char OS_KERNEL_MAIN_BUDDY_STORAGE_SIZE_PREFIX[] = "[OS][KERNEL] BUDDY_STORAGE_BYTES=";
constexpr char OS_KERNEL_MAIN_BUDDY_ALLOCATOR_READY_MESSAGE[] =
    "[OS][KERNEL] BUDDY_ALLOCATOR_READY\r\n";
constexpr char OS_KERNEL_MAIN_BUDDY_MAXIMUM_ORDER_PREFIX[] = "[OS][KERNEL] BUDDY_MAX_ORDER=";
constexpr char OS_KERNEL_MAIN_BUDDY_FREE_BLOCK_COUNT_PREFIX[] = "[OS][KERNEL] BUDDY_FREE_BLOCKS=";
constexpr char OS_KERNEL_MAIN_BUDDY_ACTIVE_BLOCK_COUNT_PREFIX[] =
    "[OS][KERNEL] BUDDY_ACTIVE_BLOCKS=";
constexpr char OS_KERNEL_MAIN_BUDDY_SUCCESSFUL_ALLOCATION_COUNT_PREFIX[] =
    "[OS][KERNEL] BUDDY_SUCCESSFUL_ALLOCATIONS=";
constexpr char OS_KERNEL_MAIN_BUDDY_RELEASE_COUNT_PREFIX[] = "[OS][KERNEL] BUDDY_RELEASES=";
constexpr char OS_KERNEL_MAIN_BUDDY_SPLIT_COUNT_PREFIX[] = "[OS][KERNEL] BUDDY_SPLITS=";
constexpr char OS_KERNEL_MAIN_BUDDY_MERGE_COUNT_PREFIX[] = "[OS][KERNEL] BUDDY_MERGES=";
constexpr char OS_KERNEL_MAIN_BUDDY_LARGEST_FREE_ORDER_PREFIX[] =
    "[OS][KERNEL] BUDDY_LARGEST_FREE_ORDER=";
constexpr char OS_KERNEL_MAIN_BUDDY_SELF_TEST_ADDRESS_PREFIX[] =
    "[OS][KERNEL] BUDDY_SELF_TEST_ADDRESS=";
constexpr char OS_KERNEL_MAIN_BUDDY_SELF_TEST_ORDER_PREFIX[] =
    "[OS][KERNEL] BUDDY_SELF_TEST_ORDER=";
constexpr char OS_KERNEL_MAIN_BUDDY_SELF_TEST_PASSED_MESSAGE[] =
    "[OS][KERNEL] BUDDY_SELF_TEST_PASSED\r\n";
constexpr char OS_KERNEL_MAIN_FRAME_ALLOCATOR_READY_MESSAGE[] =
    "[OS][KERNEL] FRAME_ALLOCATOR_READY\r\n";
constexpr char OS_KERNEL_MAIN_FREE_FRAME_COUNT_PREFIX[] = "[OS][KERNEL] FREE_FRAMES=";
constexpr char OS_KERNEL_MAIN_ALLOCATED_FRAME_COUNT_PREFIX[] = "[OS][KERNEL] ALLOCATED_FRAMES=";
constexpr char OS_KERNEL_MAIN_RESERVED_FRAME_COUNT_PREFIX[] = "[OS][KERNEL] RESERVED_FRAMES=";
constexpr char OS_KERNEL_MAIN_PAGING_READY_MESSAGE[] = "[OS][KERNEL] PAGING_READY\r\n";
constexpr char OS_KERNEL_MAIN_PAGING_ROOT_PREFIX[] = "[OS][KERNEL] PAGING_ROOT=";
constexpr char OS_KERNEL_MAIN_DIRECT_MAP_BASE_PREFIX[] = "[OS][KERNEL] DIRECT_MAP_BASE=";
constexpr char OS_KERNEL_MAIN_DIRECT_MAP_MAPPED_BYTES_PREFIX[] =
    "[OS][KERNEL] DIRECT_MAP_MAPPED_BYTES=";
constexpr char OS_KERNEL_MAIN_DIRECT_MAP_LARGE_PAGE_COUNT_PREFIX[] =
    "[OS][KERNEL] DIRECT_MAP_2M_PAGES=";
constexpr char OS_KERNEL_MAIN_DIRECT_MAP_SMALL_PAGE_COUNT_PREFIX[] =
    "[OS][KERNEL] DIRECT_MAP_4K_PAGES=";
constexpr char OS_KERNEL_MAIN_HIGH_MEMORY_TEST_ADDRESS_PREFIX[] =
    "[OS][KERNEL] HIGH_MEMORY_TEST_ADDRESS=";
constexpr char OS_KERNEL_MAIN_HIGH_MEMORY_VALIDATION_COMPLETE_MESSAGE[] =
    "[OS][KERNEL] HIGH_MEMORY_VALIDATION_COMPLETE\r\n";
constexpr char OS_KERNEL_MAIN_MEMORY_PERMISSIONS_VALID_MESSAGE[] =
    "[OS][KERNEL] MEMORY_PERMISSIONS_VALID\r\n";
constexpr char OS_KERNEL_MAIN_HEAP_READY_MESSAGE[] = "[OS][KERNEL] HEAP_READY\r\n";
constexpr char OS_KERNEL_MAIN_HEAP_CAPACITY_PREFIX[] = "[OS][KERNEL] HEAP_CAPACITY_BYTES=";
constexpr char OS_KERNEL_MAIN_HEAP_ACTIVE_ALLOCATION_COUNT_PREFIX[] =
    "[OS][KERNEL] HEAP_ACTIVE_ALLOCATIONS=";
constexpr char OS_KERNEL_MAIN_HEAP_PEAK_CONSUMED_BYTES_PREFIX[] =
    "[OS][KERNEL] HEAP_PEAK_CONSUMED_BYTES=";
constexpr char OS_KERNEL_MAIN_HEAP_LARGEST_FREE_ALLOCATION_BYTES_PREFIX[] =
    "[OS][KERNEL] HEAP_LARGEST_FREE_ALLOCATION_BYTES=";
constexpr char OS_KERNEL_MAIN_HEAP_SELF_TEST_PASSED_MESSAGE[] =
    "[OS][KERNEL] HEAP_SELF_TEST_PASSED\r\n";
constexpr char OS_KERNEL_MAIN_DEVICE_INITIALIZATION_FAILED_PREFIX[] =
    "[OS][KERNEL] DEVICE_INITIALIZATION_FAILED=";
constexpr char OS_KERNEL_MAIN_LEGACY_INTERRUPT_ROUTING_READY_MESSAGE[] =
    "[OS][KERNEL] LEGACY_INTERRUPT_ROUTING_READY\r\n";
constexpr char OS_KERNEL_MAIN_PIC_READY_MESSAGE[] = "[OS][KERNEL] PIC_READY\r\n";
constexpr char OS_KERNEL_MAIN_PIC_MASK_PREFIX[] = "[OS][KERNEL] PIC_MASK=";
constexpr char OS_KERNEL_MAIN_PIT_READY_MESSAGE[] = "[OS][KERNEL] PIT_READY\r\n";
constexpr char OS_KERNEL_MAIN_PIT_DIVISOR_PREFIX[] = "[OS][KERNEL] PIT_DIVISOR=";
constexpr char OS_KERNEL_MAIN_PIT_FREQUENCY_PREFIX[] = "[OS][KERNEL] PIT_FREQUENCY_HZ=";
constexpr char OS_KERNEL_MAIN_PS2_KEYBOARD_READY_MESSAGE[] = "[OS][KERNEL] PS2_KEYBOARD_READY\r\n";
constexpr char OS_KERNEL_MAIN_ATA_PIO_READY_MESSAGE[] = "[OS][KERNEL] ATA_PIO_READY\r\n";
constexpr char OS_KERNEL_MAIN_ATA_BOOT_DESCRIPTOR_VALID_MESSAGE[] =
    "[OS][KERNEL] ATA_BOOT_DESCRIPTOR_VALID\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_FORMATTED_MESSAGE[] =
    "[OS][KERNEL] FILE_SYSTEM_FORMATTED\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_MOUNTED_MESSAGE[] =
    "[OS][KERNEL] FILE_SYSTEM_MOUNTED\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_CORRUPT_MESSAGE[] =
    "[OS][KERNEL] FILE_SYSTEM_CORRUPT\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_PERSISTENCE_RESTORED_MESSAGE[] =
    "[OS][KERNEL] FILE_SYSTEM_PERSISTENCE_RESTORED\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_CONSISTENT_MESSAGE[] =
    "[OS][KERNEL] FILE_SYSTEM_CONSISTENT\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_VALID_MESSAGE[] =
    "[OS][KERNEL] FILE_SYSTEM_PAYLOAD_VALID\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_SYNCED_MESSAGE[] = "[OS][KERNEL] FILE_SYSTEM_SYNCED\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_STATUS_PREFIX[] = "[OS][KERNEL] FILE_SYSTEM_STATUS=";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_GENERATION_PREFIX[] =
    "[OS][KERNEL] FILE_SYSTEM_GENERATION=";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_INODE_COUNT_PREFIX[] =
    "[OS][KERNEL] FILE_SYSTEM_ALLOCATED_INODES=";
constexpr char OS_KERNEL_MAIN_FILE_SYSTEM_DATA_BLOCK_COUNT_PREFIX[] =
    "[OS][KERNEL] FILE_SYSTEM_ALLOCATED_DATA_BLOCKS=";
constexpr char OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_PASSED_MESSAGE[] =
    "[OS][KERNEL] PIC_SPURIOUS_SELF_TEST_PASSED\r\n";
constexpr char OS_KERNEL_MAIN_INTERRUPTS_ENABLED_MESSAGE[] = "[OS][KERNEL] INTERRUPTS_ENABLED\r\n";
constexpr char OS_KERNEL_MAIN_TIMER_SELF_TEST_PASSED_MESSAGE[] =
    "[OS][KERNEL] TIMER_SELF_TEST_PASSED\r\n";
constexpr char OS_KERNEL_MAIN_TIMER_TICK_COUNT_PREFIX[] = "[OS][KERNEL] TIMER_TICKS=";
constexpr char OS_KERNEL_MAIN_MONOTONIC_MILLISECONDS_PREFIX[] =
    "[OS][KERNEL] MONOTONIC_MILLISECONDS=";
constexpr char OS_KERNEL_MAIN_KEYBOARD_SCAN_CODE_PREFIX[] = "[OS][KERNEL] KEYBOARD_SCANCODE=";
constexpr char OS_KERNEL_MAIN_KEYBOARD_A_PRESSED_MESSAGE[] =
    "[OS][KERNEL] KEYBOARD_EVENT=A_PRESSED\r\n";
constexpr char OS_KERNEL_MAIN_KEYBOARD_SUPPORTED_EVENT_MESSAGE[] =
    "[OS][KERNEL] KEYBOARD_EVENT=SUPPORTED\r\n";
constexpr char OS_KERNEL_MAIN_INVALID_OPCODE_INJECTION_MESSAGE[] =
    "[OS][KERNEL] FAULT_INJECTION=INVALID_OPCODE\r\n";
constexpr char OS_KERNEL_MAIN_PAGE_FAULT_INJECTION_MESSAGE[] =
    "[OS][KERNEL] FAULT_INJECTION=PAGE_FAULT\r\n";
constexpr char OS_KERNEL_MAIN_WRITE_PROTECTION_INJECTION_MESSAGE[] =
    "[OS][KERNEL] FAULT_INJECTION=WRITE_PROTECTION\r\n";
constexpr char OS_KERNEL_MAIN_USER_ELF_VALID_MESSAGE[] = "[OS][KERNEL] USER_ELF_VALID\r\n";
constexpr char OS_KERNEL_MAIN_USER_ELF_REJECTED_PREFIX[] = "[OS][KERNEL] USER_ELF_REJECTED=";
constexpr char OS_KERNEL_MAIN_USER_ADDRESS_SPACE_FAILED_PREFIX[] =
    "[OS][KERNEL] USER_ADDRESS_SPACE_FAILED=";
constexpr char OS_KERNEL_MAIN_USER_ENTRY_PREFIX[] = "[OS][KERNEL] USER_ENTRY=";
constexpr char OS_KERNEL_MAIN_USER_MAPPED_PAGE_COUNT_PREFIX[] = "[OS][KERNEL] USER_MAPPED_PAGES=";
constexpr char OS_KERNEL_MAIN_USER_STACK_READY_MESSAGE[] = "[OS][KERNEL] USER_STACK_READY\r\n";
constexpr char OS_KERNEL_MAIN_USER_RING3_ENTER_MESSAGE[] = "[OS][KERNEL] USER_RING3_ENTER\r\n";
constexpr char OS_KERNEL_MAIN_USER_EXECUTION_FAILED_PREFIX[] =
    "[OS][KERNEL] USER_EXECUTION_FAILED=";
constexpr char OS_KERNEL_MAIN_USER_EXIT_CODE_PREFIX[] = "[OS][KERNEL] USER_EXIT_CODE=";
constexpr char OS_KERNEL_MAIN_USER_EXCEPTION_VECTOR_PREFIX[] =
    "[OS][KERNEL] USER_EXCEPTION_VECTOR=";
constexpr char OS_KERNEL_MAIN_USER_EXCEPTION_ERROR_CODE_PREFIX[] =
    "[OS][KERNEL] USER_EXCEPTION_ERROR_CODE=";
constexpr char OS_KERNEL_MAIN_USER_EXCEPTION_RIP_PREFIX[] = "[OS][KERNEL] USER_EXCEPTION_RIP=";
constexpr char OS_KERNEL_MAIN_USER_PAGE_FAULT_ADDRESS_PREFIX[] =
    "[OS][KERNEL] USER_PAGE_FAULT_ADDRESS=";
constexpr char OS_KERNEL_MAIN_USER_SYSTEM_CALL_COUNT_PREFIX[] = "[OS][KERNEL] USER_SYSCALL_COUNT=";
constexpr char OS_KERNEL_MAIN_USER_TERMINATED_MESSAGE[] = "[OS][KERNEL] USER_TERMINATED\r\n";
constexpr char OS_KERNEL_MAIN_USER_RETURNED_TO_KERNEL_MESSAGE[] =
    "[OS][KERNEL] USER_RETURNED_TO_KERNEL\r\n";
constexpr char OS_KERNEL_MAIN_USER_RESULT_INVALID_MESSAGE[] =
    "[OS][KERNEL] USER_RESULT_INVALID\r\n";
constexpr char OS_KERNEL_MAIN_PROCESS_RUNTIME_READY_MESSAGE[] =
    "[OS][KERNEL] PROCESS_RUNTIME_READY\r\n";
constexpr char OS_KERNEL_MAIN_PROCESS_ID_PREFIX[] = "[OS][KERNEL] PROCESS_ID=";
constexpr char OS_KERNEL_MAIN_PROCESS_CR3_PREFIX[] = "[OS][KERNEL] PROCESS_CR3=";
constexpr char OS_KERNEL_MAIN_PROCESS_KERNEL_STACK_TOP_PREFIX[] =
    "[OS][KERNEL] PROCESS_KERNEL_STACK_TOP=";
constexpr char OS_KERNEL_MAIN_PROCESS_RUN_TICKS_PREFIX[] = "[OS][KERNEL] PROCESS_RUN_TICKS=";
constexpr char OS_KERNEL_MAIN_PROCESS_DISPATCH_COUNT_PREFIX[] =
    "[OS][KERNEL] PROCESS_DISPATCH_COUNT=";
constexpr char OS_KERNEL_MAIN_PROCESS_PIPE_READ_BYTES_PREFIX[] =
    "[OS][KERNEL] PROCESS_PIPE_READ_BYTES=";
constexpr char OS_KERNEL_MAIN_PROCESS_PIPE_WRITTEN_BYTES_PREFIX[] =
    "[OS][KERNEL] PROCESS_PIPE_WRITTEN_BYTES=";
constexpr char OS_KERNEL_MAIN_PROCESS_FILE_READ_BYTES_PREFIX[] =
    "[OS][KERNEL] PROCESS_FILE_READ_BYTES=";
constexpr char OS_KERNEL_MAIN_PROCESS_FILE_WRITTEN_BYTES_PREFIX[] =
    "[OS][KERNEL] PROCESS_FILE_WRITTEN_BYTES=";
constexpr char OS_KERNEL_MAIN_PROCESS_CONSOLE_READ_BYTES_PREFIX[] =
    "[OS][KERNEL] PROCESS_CONSOLE_READ_BYTES=";
constexpr char OS_KERNEL_MAIN_PROCESS_CONSOLE_WRITTEN_BYTES_PREFIX[] =
    "[OS][KERNEL] PROCESS_CONSOLE_WRITTEN_BYTES=";
constexpr char OS_KERNEL_MAIN_SCHEDULER_STARTED_MESSAGE[] = "[OS][KERNEL] SCHEDULER_STARTED\r\n";
constexpr char OS_KERNEL_MAIN_SCHEDULER_CREATED_PROCESS_COUNT_PREFIX[] =
    "[OS][KERNEL] SCHEDULER_CREATED_PROCESSES=";
constexpr char OS_KERNEL_MAIN_SCHEDULER_TERMINATED_PROCESS_COUNT_PREFIX[] =
    "[OS][KERNEL] SCHEDULER_TERMINATED_PROCESSES=";
constexpr char OS_KERNEL_MAIN_SCHEDULER_TIMER_TICK_COUNT_PREFIX[] =
    "[OS][KERNEL] SCHEDULER_TIMER_TICKS=";
constexpr char OS_KERNEL_MAIN_SCHEDULER_PREEMPTION_COUNT_PREFIX[] =
    "[OS][KERNEL] SCHEDULER_PREEMPTIONS=";
constexpr char OS_KERNEL_MAIN_SCHEDULER_DISPATCH_COUNT_PREFIX[] =
    "[OS][KERNEL] SCHEDULER_DISPATCHES=";
constexpr char OS_KERNEL_MAIN_SCHEDULER_BLOCK_COUNT_PREFIX[] = "[OS][KERNEL] SCHEDULER_BLOCKS=";
constexpr char OS_KERNEL_MAIN_SCHEDULER_WAKEUP_COUNT_PREFIX[] = "[OS][KERNEL] SCHEDULER_WAKEUPS=";
constexpr char OS_KERNEL_MAIN_PIPE_CAPACITY_PREFIX[] = "[OS][KERNEL] PIPE_CAPACITY_BYTES=";
constexpr char OS_KERNEL_MAIN_PIPE_WRITTEN_BYTES_PREFIX[] = "[OS][KERNEL] PIPE_WRITTEN_BYTES=";
constexpr char OS_KERNEL_MAIN_PIPE_READ_BYTES_PREFIX[] = "[OS][KERNEL] PIPE_READ_BYTES=";
constexpr char OS_KERNEL_MAIN_PIPE_READER_BLOCK_COUNT_PREFIX[] = "[OS][KERNEL] PIPE_READER_BLOCKS=";
constexpr char OS_KERNEL_MAIN_PIPE_WRITER_BLOCK_COUNT_PREFIX[] = "[OS][KERNEL] PIPE_WRITER_BLOCKS=";
constexpr char OS_KERNEL_MAIN_PIPE_END_OF_FILE_COUNT_PREFIX[] =
    "[OS][KERNEL] PIPE_EOF_OBSERVATIONS=";
constexpr char OS_KERNEL_MAIN_CONSOLE_SUBMITTED_BYTES_PREFIX[] =
    "[OS][KERNEL] CONSOLE_SUBMITTED_BYTES=";
constexpr char OS_KERNEL_MAIN_CONSOLE_READ_BYTES_PREFIX[] = "[OS][KERNEL] CONSOLE_READ_BYTES=";
constexpr char OS_KERNEL_MAIN_CONSOLE_DROPPED_BYTES_PREFIX[] =
    "[OS][KERNEL] CONSOLE_DROPPED_BYTES=";
constexpr char OS_KERNEL_MAIN_CONSOLE_BUFFERED_BYTES_PREFIX[] =
    "[OS][KERNEL] CONSOLE_BUFFERED_BYTES=";
constexpr char OS_KERNEL_MAIN_PIPE_READY_MESSAGE[] = "[OS][KERNEL] PIPE_READY\r\n";
constexpr char OS_KERNEL_MAIN_PIPE_TRANSFER_VALID_MESSAGE[] =
    "[OS][KERNEL] PIPE_TRANSFER_VALID\r\n";
constexpr char OS_KERNEL_MAIN_PIPE_ENDPOINTS_CLOSED_MESSAGE[] =
    "[OS][KERNEL] PIPE_ENDPOINTS_CLOSED\r\n";
constexpr char OS_KERNEL_MAIN_PROCESS_RESOURCES_RECLAIMED_MESSAGE[] =
    "[OS][KERNEL] PROCESS_RESOURCES_RECLAIMED\r\n";
constexpr char OS_KERNEL_MAIN_SCHEDULER_COMPLETE_MESSAGE[] = "[OS][KERNEL] SCHEDULER_COMPLETE\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SIZE_PREFIX[] = "[OS][KERNEL] FILE_SIZE=";
constexpr char OS_KERNEL_MAIN_LOAD_SEGMENT_COUNT_PREFIX[] = "[OS][KERNEL] LOAD_SEGMENTS=";
constexpr char OS_KERNEL_MAIN_READY_MESSAGE[] = "[OS][KERNEL] READY\r\n";
constexpr uint64_t OS_KERNEL_MAIN_TIMER_SELF_TEST_MINIMUM_TICK_COUNT = 16ULL;
constexpr uint64_t OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_EXPECTED_COUNT = 1ULL;
constexpr int64_t OS_KERNEL_MAIN_USER_EXPECTED_EXIT_CODE = 0LL;
constexpr uint64_t OS_KERNEL_MAIN_USER_INVALID_OPCODE_VECTOR = 6ULL;
constexpr uint64_t OS_KERNEL_MAIN_USER_PAGE_FAULT_VECTOR = 14ULL;
constexpr uint64_t OS_KERNEL_MAIN_USER_PAGE_FAULT_ERROR_CODE = 0x0000000000000004ULL;
constexpr uint64_t OS_KERNEL_MAIN_USER_PAGE_FAULT_ADDRESS = 0x0000000030000000ULL;
constexpr uint64_t OS_KERNEL_MAIN_NORMAL_PROCESS_COUNT = 4ULL;
constexpr uint64_t OS_KERNEL_MAIN_FAULT_PROCESS_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_MAIN_MINIMUM_PREEMPTION_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_MAIN_MINIMUM_BLOCK_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES = 256ULL;
constexpr uint64_t OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_MAIN_EXPECTED_END_OF_FILE_OBSERVATION_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_MAIN_EXPECTED_BROKEN_PIPE_OBSERVATION_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_MAIN_FIRST_PROCESS_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_MAIN_SECOND_PROCESS_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_MAIN_THIRD_PROCESS_INDEX = 2ULL;
constexpr uint64_t OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_SIZE_BYTES = 256ULL;
constexpr uint64_t OS_KERNEL_MAIN_FILE_SYSTEM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MAIN_FILE_SYSTEM_BYTE_MULTIPLIER = 37ULL;
constexpr uint64_t OS_KERNEL_MAIN_FILE_SYSTEM_BYTE_INCREMENT = 11ULL;
constexpr uint64_t OS_KERNEL_MAIN_FILE_SYSTEM_BYTE_MASK = 0xFFULL;
constexpr uint8_t OS_KERNEL_MAIN_FILE_SYSTEM_ZERO_BYTE = 0U;
constexpr uint8_t OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('s'), static_cast<uint8_t>('h'),
    static_cast<uint8_t>('a'), static_cast<uint8_t>('r'), static_cast<uint8_t>('e'),
    static_cast<uint8_t>('d'), static_cast<uint8_t>('/'), static_cast<uint8_t>('p'),
    static_cast<uint8_t>('a'), static_cast<uint8_t>('y'), static_cast<uint8_t>('l'),
    static_cast<uint8_t>('o'), static_cast<uint8_t>('a'), static_cast<uint8_t>('d'),
    static_cast<uint8_t>('.'), static_cast<uint8_t>('b'), static_cast<uint8_t>('i'),
    static_cast<uint8_t>('n'),
};

// 非零初值不能用于证明加载器执行了 p_memsz 对应的 BSS 清零。
uint64_t kernel_main_bss_probe;

void WriteRequiredMessage(const SerialPort &serial_port, const char *message) noexcept {
    if (!serial_port.TryWriteString(message)) {
        HaltProcessor();
    }
}

void WriteRequiredHexLine(const SerialPort &serial_port, const char *prefix,
                          const uint64_t value) noexcept {
    if (!serial_port.TryWriteHexLine(prefix, value)) {
        HaltProcessor();
    }
}

void ValidateBootEnvironment(const SerialPort &serial_port, const BootInfo *boot_info) noexcept {
    if (ValidateBootInfo(boot_info) != BootInfoValidationStatus::Succeeded) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_BOOT_INFO_INVALID_MESSAGE);
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_BOOT_INFO_VALID_MESSAGE);

    if (kernel_main_bss_probe != OS_KERNEL_MAIN_BSS_PROBE_ZERO_VALUE) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_BSS_INVALID_MESSAGE);
        HaltProcessor();
    }
    kernel_main_bss_probe = OS_KERNEL_MAIN_BSS_PROBE_WRITTEN_VALUE;
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_BSS_ZEROED_MESSAGE);

    if (ReadPageTableRoot() != boot_info->page_table_root_physical_address) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_CR3_INVALID_MESSAGE);
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_CR3_VALID_MESSAGE);
}

void InitializeKernelArchitecture(const SerialPort &serial_port,
                                  const BootInfo &boot_info) noexcept {
    static_cast<void>(boot_info);
    InitializeGlobalDescriptorTable();
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_GDT_READY_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_TSS_READY_MESSAGE);

    InitializeInterruptDescriptorTable();
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_IDT_READY_MESSAGE);
    if (ValidateDescriptorTables() != DescriptorTableValidationStatus::Succeeded) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_DESCRIPTOR_TABLES_INVALID_MESSAGE);
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_DESCRIPTOR_TABLES_VALID_MESSAGE);

    TriggerBreakpoint();
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_EXCEPTION_SELF_TEST_READY_MESSAGE);
}

void InitializeKernelMemorySubsystem(const SerialPort &serial_port,
                                     const BootInfo &boot_info) noexcept {
    const KernelMemoryInitializationStatus status = InitializeKernelMemory(boot_info);
    if (status != KernelMemoryInitializationStatus::Succeeded) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_MEMORY_INITIALIZATION_FAILED_PREFIX,
                             static_cast<uint64_t>(status));
        HaltProcessor();
    }
    const KernelMemoryStatistics &statistics = GetKernelMemoryStatistics();
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_MEMORY_MAP_VALID_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_MEMORY_MAP_ENTRY_COUNT_PREFIX,
                         statistics.memory_map_entry_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_MEMORY_DESCRIBED_PREFIX,
                         statistics.described_address_bytes);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_MEMORY_USABLE_PREFIX,
                         statistics.reported_usable_memory_bytes);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_MEMORY_MANAGED_PREFIX,
                         statistics.managed_usable_memory_bytes);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_MEMORY_MANAGED_LIMIT_PREFIX,
                         statistics.managed_physical_address_limit);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PHYSICAL_ADDRESS_WIDTH_PREFIX,
                         statistics.physical_address_width_bits);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_VIRTUAL_ADDRESS_WIDTH_PREFIX,
                         statistics.virtual_address_width_bits);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FIVE_LEVEL_PAGING_SUPPORTED_PREFIX,
                         statistics.five_level_paging_supported);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FRAME_STATE_STORAGE_ADDRESS_PREFIX,
                         statistics.frame_state_storage_physical_address);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FRAME_STATE_STORAGE_SIZE_PREFIX,
                         statistics.frame_state_storage_size_bytes);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_STORAGE_ADDRESS_PREFIX,
                         statistics.buddy_storage_physical_address);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_STORAGE_SIZE_PREFIX,
                         statistics.buddy_storage_size_bytes);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FRAME_ALLOCATOR_READY_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FREE_FRAME_COUNT_PREFIX,
                         statistics.free_frame_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_ALLOCATED_FRAME_COUNT_PREFIX,
                         statistics.allocated_frame_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_RESERVED_FRAME_COUNT_PREFIX,
                         statistics.reserved_frame_count);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_BUDDY_ALLOCATOR_READY_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_MAXIMUM_ORDER_PREFIX,
                         statistics.buddy_maximum_order);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_FREE_BLOCK_COUNT_PREFIX,
                         statistics.buddy_free_block_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_ACTIVE_BLOCK_COUNT_PREFIX,
                         statistics.buddy_active_block_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_SUCCESSFUL_ALLOCATION_COUNT_PREFIX,
                         statistics.buddy_successful_allocation_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_RELEASE_COUNT_PREFIX,
                         statistics.buddy_release_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_SPLIT_COUNT_PREFIX,
                         statistics.buddy_split_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_MERGE_COUNT_PREFIX,
                         statistics.buddy_merge_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_LARGEST_FREE_ORDER_PREFIX,
                         statistics.buddy_largest_free_order);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_SELF_TEST_ADDRESS_PREFIX,
                         statistics.buddy_self_test_physical_address);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_BUDDY_SELF_TEST_ORDER_PREFIX,
                         statistics.buddy_self_test_order);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_BUDDY_SELF_TEST_PASSED_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PAGING_READY_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PAGING_ROOT_PREFIX,
                         statistics.page_table_root_physical_address);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_DIRECT_MAP_BASE_PREFIX,
                         OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_DIRECT_MAP_MAPPED_BYTES_PREFIX,
                         statistics.direct_map_mapped_bytes);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_DIRECT_MAP_LARGE_PAGE_COUNT_PREFIX,
                         statistics.direct_map_large_page_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_DIRECT_MAP_SMALL_PAGE_COUNT_PREFIX,
                         statistics.direct_map_small_page_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_HIGH_MEMORY_TEST_ADDRESS_PREFIX,
                         statistics.high_memory_test_physical_address);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_HIGH_MEMORY_VALIDATION_COMPLETE_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_MEMORY_PERMISSIONS_VALID_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_HEAP_READY_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_HEAP_CAPACITY_PREFIX,
                         statistics.heap_capacity_bytes);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_HEAP_ACTIVE_ALLOCATION_COUNT_PREFIX,
                         statistics.heap_active_allocation_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_HEAP_PEAK_CONSUMED_BYTES_PREFIX,
                         statistics.heap_peak_consumed_bytes);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_HEAP_LARGEST_FREE_ALLOCATION_BYTES_PREFIX,
                         statistics.heap_largest_free_allocation_bytes);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_HEAP_SELF_TEST_PASSED_MESSAGE);
}

void InitializeKernelDevices(const SerialPort &serial_port) noexcept {
    const InterruptRuntimeStatus status = InitializeInterruptRuntime();
    if (status != InterruptRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_DEVICE_INITIALIZATION_FAILED_PREFIX,
                             static_cast<uint64_t>(status));
        HaltProcessor();
    }

    InterruptRuntimeStatistics statistics = GetInterruptRuntimeStatistics();
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_LEGACY_INTERRUPT_ROUTING_READY_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PIC_READY_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIC_MASK_PREFIX, statistics.pic_mask);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PIT_READY_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIT_DIVISOR_PREFIX, statistics.pit_divisor);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIT_FREQUENCY_PREFIX,
                         statistics.pit_actual_frequency_hz);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PS2_KEYBOARD_READY_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_ATA_PIO_READY_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_ATA_BOOT_DESCRIPTOR_VALID_MESSAGE);

    TriggerLegacyPicSpuriousInterrupt();
    statistics = GetInterruptRuntimeStatistics();
    if (statistics.spurious_interrupt_count !=
        OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_EXPECTED_COUNT) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_DEVICE_INITIALIZATION_FAILED_PREFIX,
                             statistics.spurious_interrupt_count);
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_PASSED_MESSAGE);

    EnableInterrupts();
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_INTERRUPTS_ENABLED_MESSAGE);
    do {
        WaitForInterrupt();
        statistics = GetInterruptRuntimeStatistics();
    } while (statistics.timer_tick_count < OS_KERNEL_MAIN_TIMER_SELF_TEST_MINIMUM_TICK_COUNT);

    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_TIMER_TICK_COUNT_PREFIX,
                         statistics.timer_tick_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_MONOTONIC_MILLISECONDS_PREFIX,
                         statistics.monotonic_milliseconds);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_TIMER_SELF_TEST_PASSED_MESSAGE);
}

[[nodiscard]] uint8_t ExpectedFileSystemPayloadByte(const uint64_t byte_index) noexcept {
    return static_cast<uint8_t>((byte_index * OS_KERNEL_MAIN_FILE_SYSTEM_BYTE_MULTIPLIER +
                                 OS_KERNEL_MAIN_FILE_SYSTEM_BYTE_INCREMENT) &
                                OS_KERNEL_MAIN_FILE_SYSTEM_BYTE_MASK);
}

[[nodiscard]] bool ValidateFileSystemPayload(FileSystem &file_system) noexcept {
    const FileSystemOpenOptions options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
    };
    FileSystemHandle handle{};
    if (file_system.Open(OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_PATH,
                         sizeof(OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_PATH), options,
                         handle) != FileSystemStatus::Succeeded) {
        return false;
    }
    uint8_t payload[OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_SIZE_BYTES]{};
    uint64_t read_bytes = OS_KERNEL_MAIN_FILE_SYSTEM_EMPTY_VALUE;
    bool valid = file_system.Read(handle, payload, OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_SIZE_BYTES,
                                  read_bytes) == FileSystemStatus::Succeeded &&
                 read_bytes == OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_SIZE_BYTES;
    for (uint64_t byte_index = OS_KERNEL_MAIN_FILE_SYSTEM_EMPTY_VALUE;
         byte_index < OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_SIZE_BYTES; ++byte_index) {
        valid = valid && payload[byte_index] == ExpectedFileSystemPayloadByte(byte_index);
    }
    uint8_t end_of_file_probe = OS_KERNEL_MAIN_FILE_SYSTEM_ZERO_BYTE;
    read_bytes = OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_SIZE_BYTES;
    valid = valid &&
            file_system.Read(handle, &end_of_file_probe, sizeof(end_of_file_probe), read_bytes) ==
                FileSystemStatus::Succeeded &&
            read_bytes == OS_KERNEL_MAIN_FILE_SYSTEM_EMPTY_VALUE &&
            file_system.Close(handle) == FileSystemStatus::Succeeded;
    return valid;
}

void WriteFileSystemStatistics(const SerialPort &serial_port,
                               const FileSystem &file_system) noexcept {
    const FileSystemStatistics statistics = file_system.Statistics();
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_GENERATION_PREFIX,
                         statistics.transaction_generation);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_INODE_COUNT_PREFIX,
                         statistics.allocated_inode_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_DATA_BLOCK_COUNT_PREFIX,
                         statistics.allocated_data_block_count);
}

void InitializeKernelFileSystem(const SerialPort &serial_port, FileSystem &file_system,
                                AtaPioDevice &device) noexcept {
    bool formatted = false;
    const FileSystemStatus mount_status = file_system.MountOrFormat(device, formatted);
    if (mount_status != FileSystemStatus::Succeeded) {
        if (mount_status == FileSystemStatus::Corrupt ||
            mount_status == FileSystemStatus::IncompleteTransaction) {
            WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_CORRUPT_MESSAGE);
        }
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_STATUS_PREFIX,
                             static_cast<uint64_t>(mount_status));
        HaltProcessor();
    }
    if (formatted) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_FORMATTED_MESSAGE);
    } else {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_MOUNTED_MESSAGE);
        if (!ValidateFileSystemPayload(file_system)) {
            WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_CORRUPT_MESSAGE);
            HaltProcessor();
        }
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_PERSISTENCE_RESTORED_MESSAGE);
    }
    if (file_system.CheckConsistency() != FileSystemStatus::Succeeded) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_CORRUPT_MESSAGE);
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_CONSISTENT_MESSAGE);
    WriteFileSystemStatistics(serial_port, file_system);
}

void FinalizeKernelFileSystem(const SerialPort &serial_port, FileSystem &file_system,
                              const bool require_payload) noexcept {
    if (file_system.Sync() != FileSystemStatus::Succeeded) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_STATUS_PREFIX,
                             static_cast<uint64_t>(FileSystemStatus::DeviceFailure));
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_SYNCED_MESSAGE);
    if (file_system.CheckConsistency() != FileSystemStatus::Succeeded ||
        (require_payload && !ValidateFileSystemPayload(file_system))) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_CORRUPT_MESSAGE);
        HaltProcessor();
    }
    if (require_payload) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_VALID_MESSAGE);
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_FILE_SYSTEM_CONSISTENT_MESSAGE);
    WriteFileSystemStatistics(serial_port, file_system);
}

void CreateRequiredProcess(const SerialPort &serial_port,
                           const UserProgramSelection selection) noexcept {
    ProcessCreationResult creation_result{};
    UserElfValidationStatus elf_validation_status = UserElfValidationStatus::Succeeded;
    UserAddressSpaceStatus address_space_status = UserAddressSpaceStatus::Succeeded;
    const ProcessRuntimeStatus runtime_status =
        CreateProcess(selection, creation_result, elf_validation_status, address_space_status);
    if (runtime_status == ProcessRuntimeStatus::InvalidElf) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_ELF_REJECTED_PREFIX,
                             static_cast<uint64_t>(elf_validation_status));
        HaltProcessor();
    }
    if (runtime_status != ProcessRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_ADDRESS_SPACE_FAILED_PREFIX,
                             static_cast<uint64_t>(address_space_status));
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_USER_ELF_VALID_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_ENTRY_PREFIX,
                         creation_result.entry_virtual_address);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_MAPPED_PAGE_COUNT_PREFIX,
                         creation_result.mapped_page_count);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_USER_STACK_READY_MESSAGE);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_ID_PREFIX, creation_result.process_id);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_CR3_PREFIX,
                         creation_result.root_physical_address);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_KERNEL_STACK_TOP_PREFIX,
                         ProcessKernelStackTopAddress(creation_result.process_index));
}

void PrepareRequiredProcesses(const SerialPort &serial_port,
                              const UserProgramSelection selection) noexcept {
    if (InitializeProcessRuntime() != ProcessRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_EXECUTION_FAILED_PREFIX,
                             static_cast<uint64_t>(ProcessRuntimeStatus::SchedulerFailure));
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PROCESS_RUNTIME_READY_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PIPE_READY_MESSAGE);

    const uint64_t process_count = selection == UserProgramSelection::Smoke
                                       ? OS_KERNEL_MAIN_NORMAL_PROCESS_COUNT
                                       : OS_KERNEL_MAIN_FAULT_PROCESS_COUNT;
    for (uint64_t process_index = OS_KERNEL_MAIN_FIRST_PROCESS_INDEX; process_index < process_count;
         ++process_index) {
        UserProgramSelection process_selection = selection;
        if (selection == UserProgramSelection::Smoke) {
            if (process_index == OS_KERNEL_MAIN_FIRST_PROCESS_INDEX) {
                process_selection = UserProgramSelection::Shell;
            } else if (process_index == OS_KERNEL_MAIN_SECOND_PROCESS_INDEX) {
                process_selection = UserProgramSelection::IpcProducer;
            } else if (process_index == OS_KERNEL_MAIN_THIRD_PROCESS_INDEX) {
                process_selection = UserProgramSelection::IpcConsumer;
            } else {
                process_selection = UserProgramSelection::SchedulerWorker;
            }
        }
        CreateRequiredProcess(serial_port, process_selection);
    }
}

[[nodiscard]] bool IsExpectedProcessExecutionResult(const ProcessExecutionResult &result) noexcept {
    const bool exited_successfully =
        result.termination_reason == ProcessTerminationReason::Exited &&
        result.exit_code == OS_KERNEL_MAIN_USER_EXPECTED_EXIT_CODE;
    if (result.selection == UserProgramSelection::Shell) {
        return exited_successfully &&
               result.pipe_bytes_read == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.pipe_bytes_written == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.console_bytes_read != OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.console_bytes_written != OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT;
    }
    if (result.selection == UserProgramSelection::IpcProducer) {
        return exited_successfully &&
               result.pipe_bytes_read == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.pipe_bytes_written == OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES &&
               result.file_system_bytes_read == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.file_system_bytes_written == OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_SIZE_BYTES;
    }
    if (result.selection == UserProgramSelection::IpcConsumer) {
        return exited_successfully &&
               result.pipe_bytes_read == OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES &&
               result.pipe_bytes_written == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.file_system_bytes_read == OS_KERNEL_MAIN_FILE_SYSTEM_PAYLOAD_SIZE_BYTES &&
               result.file_system_bytes_written == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT;
    }
    if (result.selection == UserProgramSelection::Smoke ||
        result.selection == UserProgramSelection::SchedulerWorker) {
        return exited_successfully &&
               result.pipe_bytes_read == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.pipe_bytes_written == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.file_system_bytes_read == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.file_system_bytes_written == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT;
    }
    if (result.selection == UserProgramSelection::InvalidOpcode) {
        return result.termination_reason == ProcessTerminationReason::Exception &&
               result.exception_vector == OS_KERNEL_MAIN_USER_INVALID_OPCODE_VECTOR;
    }
    if (result.selection == UserProgramSelection::PageFault) {
        return result.termination_reason == ProcessTerminationReason::Exception &&
               result.exception_vector == OS_KERNEL_MAIN_USER_PAGE_FAULT_VECTOR &&
               result.exception_error_code == OS_KERNEL_MAIN_USER_PAGE_FAULT_ERROR_CODE &&
               result.page_fault_address == OS_KERNEL_MAIN_USER_PAGE_FAULT_ADDRESS;
    }
    return false;
}

void WriteProcessExecutionResult(const SerialPort &serial_port,
                                 const ProcessExecutionResult &result) noexcept {
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_ID_PREFIX, result.process_id);
    if (result.termination_reason == ProcessTerminationReason::Exited) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_EXIT_CODE_PREFIX,
                             static_cast<uint64_t>(result.exit_code));
    } else if (result.termination_reason == ProcessTerminationReason::Exception) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_EXCEPTION_VECTOR_PREFIX,
                             result.exception_vector);
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_EXCEPTION_ERROR_CODE_PREFIX,
                             result.exception_error_code);
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_EXCEPTION_RIP_PREFIX,
                             result.exception_instruction_pointer);
        if (result.exception_vector == OS_KERNEL_MAIN_USER_PAGE_FAULT_VECTOR) {
            WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_PAGE_FAULT_ADDRESS_PREFIX,
                                 result.page_fault_address);
        }
    }
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_SYSTEM_CALL_COUNT_PREFIX,
                         result.system_call_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_RUN_TICKS_PREFIX,
                         result.run_tick_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_DISPATCH_COUNT_PREFIX,
                         result.dispatch_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_PIPE_READ_BYTES_PREFIX,
                         result.pipe_bytes_read);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_PIPE_WRITTEN_BYTES_PREFIX,
                         result.pipe_bytes_written);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_FILE_READ_BYTES_PREFIX,
                         result.file_system_bytes_read);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_FILE_WRITTEN_BYTES_PREFIX,
                         result.file_system_bytes_written);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_CONSOLE_READ_BYTES_PREFIX,
                         result.console_bytes_read);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PROCESS_CONSOLE_WRITTEN_BYTES_PREFIX,
                         result.console_bytes_written);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_USER_TERMINATED_MESSAGE);

    if (!IsExpectedProcessExecutionResult(result)) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_USER_RESULT_INVALID_MESSAGE);
        HaltProcessor();
    }
}

[[nodiscard]] bool
ProcessResourcesWereReclaimed(const ProcessRuntimeStatistics &statistics) noexcept {
    return statistics.frames_before_processes.managed_frame_count ==
               statistics.frames_after_processes.managed_frame_count &&
           statistics.frames_before_processes.free_frame_count ==
               statistics.frames_after_processes.free_frame_count &&
           statistics.frames_before_processes.allocated_frame_count ==
               statistics.frames_after_processes.allocated_frame_count &&
           statistics.frames_before_processes.reserved_frame_count ==
               statistics.frames_after_processes.reserved_frame_count;
}

void ExecuteRequiredProcesses(const SerialPort &serial_port,
                              const UserProgramSelection selection) noexcept {
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_USER_RING3_ENTER_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_SCHEDULER_STARTED_MESSAGE);
    const ProcessRuntimeStatus runtime_status = ExecuteProcesses();
    if (runtime_status != ProcessRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_EXECUTION_FAILED_PREFIX,
                             static_cast<uint64_t>(runtime_status));
        HaltProcessor();
    }

    const ProcessRuntimeStatistics statistics = GetProcessRuntimeStatistics();
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_SCHEDULER_CREATED_PROCESS_COUNT_PREFIX,
                         statistics.scheduler.created_process_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_SCHEDULER_TERMINATED_PROCESS_COUNT_PREFIX,
                         statistics.scheduler.terminated_process_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_SCHEDULER_TIMER_TICK_COUNT_PREFIX,
                         statistics.scheduler.timer_tick_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_SCHEDULER_PREEMPTION_COUNT_PREFIX,
                         statistics.scheduler.preemption_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_SCHEDULER_DISPATCH_COUNT_PREFIX,
                         statistics.scheduler.dispatch_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_SCHEDULER_BLOCK_COUNT_PREFIX,
                         statistics.scheduler.block_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_SCHEDULER_WAKEUP_COUNT_PREFIX,
                         statistics.scheduler.wakeup_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIPE_CAPACITY_PREFIX,
                         OS_KERNEL_PIPE_CAPACITY_BYTES);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIPE_WRITTEN_BYTES_PREFIX,
                         statistics.ipc.pipe.bytes_written);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIPE_READ_BYTES_PREFIX,
                         statistics.ipc.pipe.bytes_read);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIPE_READER_BLOCK_COUNT_PREFIX,
                         statistics.ipc.reader_block_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIPE_WRITER_BLOCK_COUNT_PREFIX,
                         statistics.ipc.writer_block_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_PIPE_END_OF_FILE_COUNT_PREFIX,
                         statistics.ipc.end_of_file_observation_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_CONSOLE_SUBMITTED_BYTES_PREFIX,
                         statistics.console_input.submitted_byte_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_CONSOLE_READ_BYTES_PREFIX,
                         statistics.console_input.read_byte_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_CONSOLE_DROPPED_BYTES_PREFIX,
                         statistics.console_input.dropped_byte_count);
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_CONSOLE_BUFFERED_BYTES_PREFIX,
                         statistics.console_input.buffered_byte_count);

    const uint64_t expected_process_count = selection == UserProgramSelection::Smoke
                                                ? OS_KERNEL_MAIN_NORMAL_PROCESS_COUNT
                                                : OS_KERNEL_MAIN_FAULT_PROCESS_COUNT;
    if (statistics.scheduler.created_process_count != expected_process_count ||
        statistics.scheduler.terminated_process_count != expected_process_count ||
        (selection == UserProgramSelection::Smoke &&
         (statistics.scheduler.preemption_count < OS_KERNEL_MAIN_MINIMUM_PREEMPTION_COUNT ||
          statistics.scheduler.block_count < OS_KERNEL_MAIN_MINIMUM_BLOCK_COUNT ||
          statistics.scheduler.wakeup_count != statistics.scheduler.block_count ||
          statistics.ipc.pipe.bytes_written != OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES ||
          statistics.ipc.pipe.bytes_read != OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES ||
          statistics.ipc.pipe.buffered_byte_count !=
              OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT ||
          !statistics.ipc.pipe.reader_closed || !statistics.ipc.pipe.writer_closed ||
          statistics.ipc.writer_block_count < OS_KERNEL_MAIN_MINIMUM_BLOCK_COUNT ||
          statistics.ipc.end_of_file_observation_count !=
              OS_KERNEL_MAIN_EXPECTED_END_OF_FILE_OBSERVATION_COUNT ||
          statistics.ipc.broken_pipe_observation_count !=
              OS_KERNEL_MAIN_EXPECTED_BROKEN_PIPE_OBSERVATION_COUNT ||
          statistics.console_input.submitted_byte_count ==
              OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT ||
          statistics.console_input.submitted_byte_count !=
              statistics.console_input.read_byte_count ||
          statistics.console_input.dropped_byte_count !=
              OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT ||
          statistics.console_input.buffered_byte_count !=
              OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT)) ||
        !ProcessResourcesWereReclaimed(statistics)) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_USER_RESULT_INVALID_MESSAGE);
        HaltProcessor();
    }

    for (uint64_t process_index = OS_KERNEL_MAIN_FIRST_PROCESS_INDEX;
         process_index < expected_process_count; ++process_index) {
        WriteProcessExecutionResult(serial_port, statistics.processes[process_index]);
    }
    if (selection == UserProgramSelection::Smoke) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PIPE_TRANSFER_VALID_MESSAGE);
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PIPE_ENDPOINTS_CLOSED_MESSAGE);
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PROCESS_RESOURCES_RECLAIMED_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_SCHEDULER_COMPLETE_MESSAGE);
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_USER_RETURNED_TO_KERNEL_MESSAGE);
}

void WriteKeyboardEvent(const SerialPort &serial_port, const KeyboardEvent &event) noexcept {
    WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_KEYBOARD_SCAN_CODE_PREFIX, event.scan_code);
    if (event.key == KeyboardKey::A && event.pressed) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_KEYBOARD_A_PRESSED_MESSAGE);
        return;
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_KEYBOARD_SUPPORTED_EVENT_MESSAGE);
}

[[noreturn]] void RunKernelEventLoop(const SerialPort &serial_port) noexcept {
    while (true) {
        WaitForInterrupt();
        KeyboardEvent event{};
        if (TryTakeKeyboardEvent(event)) {
            WriteKeyboardEvent(serial_port, event);
        }
    }
}

[[noreturn]] void ExecuteFaultInjection(const SerialPort &serial_port,
                                        const KernelFaultInjection fault_injection) noexcept {
    if (fault_injection == KernelFaultInjection::InvalidOpcode) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_INVALID_OPCODE_INJECTION_MESSAGE);
        TriggerInvalidOpcode();
    }
    if (fault_injection == KernelFaultInjection::WriteProtection) {
        WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_WRITE_PROTECTION_INJECTION_MESSAGE);
        TriggerWriteProtectionFault(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS);
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_PAGE_FAULT_INJECTION_MESSAGE);
    TriggerPageFault();
}
}

[[noreturn]] void RunKernel(const BootInfo *boot_info, const KernelFaultInjection fault_injection,
                            const UserProgramSelection user_program_selection) noexcept {
    const SerialPort serial_port{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    serial_port.Initialize();
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_ENTERED_MESSAGE);

    ValidateBootEnvironment(serial_port, boot_info);
    InitializeKernelArchitecture(serial_port, *boot_info);
    InitializeKernelMemorySubsystem(serial_port, *boot_info);

    if (fault_injection != KernelFaultInjection::None) {
        ExecuteFaultInjection(serial_port, fault_injection);
    }

    PrepareRequiredProcesses(serial_port, user_program_selection);
    InitializeKernelDevices(serial_port);
    AtaPioDevice file_system_device{};
    FileSystem file_system{};
    InitializeKernelFileSystem(serial_port, file_system, file_system_device);
    if (AttachProcessFileSystem(file_system) != ProcessRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serial_port, OS_KERNEL_MAIN_USER_EXECUTION_FAILED_PREFIX,
                             static_cast<uint64_t>(ProcessRuntimeStatus::NotInitialized));
        HaltProcessor();
    }
    ExecuteRequiredProcesses(serial_port, user_program_selection);
    FinalizeKernelFileSystem(serial_port, file_system,
                             user_program_selection == UserProgramSelection::Smoke);

    if (!serial_port.TryWriteHexLine(OS_KERNEL_MAIN_FILE_SIZE_PREFIX,
                                     boot_info->kernel_file_size_bytes) ||
        !serial_port.TryWriteHexLine(OS_KERNEL_MAIN_LOAD_SEGMENT_COUNT_PREFIX,
                                     boot_info->kernel_load_segment_count)) {
        HaltProcessor();
    }
    WriteRequiredMessage(serial_port, OS_KERNEL_MAIN_READY_MESSAGE);
    RunKernelEventLoop(serial_port);
}
}
