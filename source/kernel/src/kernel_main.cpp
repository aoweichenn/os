#include "os/kernel/kernel_main.hpp"

#include "os/kernel/descriptor_tables.hpp"
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
constexpr char OS_KERNEL_MAIN_FRAME_ALLOCATOR_READY_MESSAGE[] =
    "[OS][KERNEL] FRAME_ALLOCATOR_READY\r\n";
constexpr char OS_KERNEL_MAIN_FREE_FRAME_COUNT_PREFIX[] = "[OS][KERNEL] FREE_FRAMES=";
constexpr char OS_KERNEL_MAIN_ALLOCATED_FRAME_COUNT_PREFIX[] = "[OS][KERNEL] ALLOCATED_FRAMES=";
constexpr char OS_KERNEL_MAIN_RESERVED_FRAME_COUNT_PREFIX[] = "[OS][KERNEL] RESERVED_FRAMES=";
constexpr char OS_KERNEL_MAIN_PAGING_READY_MESSAGE[] = "[OS][KERNEL] PAGING_READY\r\n";
constexpr char OS_KERNEL_MAIN_PAGING_ROOT_PREFIX[] = "[OS][KERNEL] PAGING_ROOT=";
constexpr char OS_KERNEL_MAIN_MEMORY_PERMISSIONS_VALID_MESSAGE[] =
    "[OS][KERNEL] MEMORY_PERMISSIONS_VALID\r\n";
constexpr char OS_KERNEL_MAIN_HEAP_READY_MESSAGE[] = "[OS][KERNEL] HEAP_READY\r\n";
constexpr char OS_KERNEL_MAIN_HEAP_CAPACITY_PREFIX[] = "[OS][KERNEL] HEAP_CAPACITY_BYTES=";
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

// 非零初值不能用于证明加载器执行了 p_memsz 对应的 BSS 清零。
uint64_t kernelMainBssProbe;

void WriteRequiredMessage(const SerialPort &serialPort, const char *message) noexcept {
    if (!serialPort.TryWriteString(message)) {
        HaltProcessor();
    }
}

void WriteRequiredHexLine(const SerialPort &serialPort, const char *prefix,
                          const uint64_t value) noexcept {
    if (!serialPort.TryWriteHexLine(prefix, value)) {
        HaltProcessor();
    }
}

void ValidateBootEnvironment(const SerialPort &serialPort, const BootInfo *bootInfo) noexcept {
    if (ValidateBootInfo(bootInfo) != BootInfoValidationStatus::Succeeded) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_BOOT_INFO_INVALID_MESSAGE);
        HaltProcessor();
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_BOOT_INFO_VALID_MESSAGE);

    if (kernelMainBssProbe != OS_KERNEL_MAIN_BSS_PROBE_ZERO_VALUE) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_BSS_INVALID_MESSAGE);
        HaltProcessor();
    }
    kernelMainBssProbe = OS_KERNEL_MAIN_BSS_PROBE_WRITTEN_VALUE;
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_BSS_ZEROED_MESSAGE);

    if (ReadPageTableRoot() != bootInfo->pageTableRootPhysicalAddress) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_CR3_INVALID_MESSAGE);
        HaltProcessor();
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_CR3_VALID_MESSAGE);
}

void InitializeKernelArchitecture(const SerialPort &serialPort, const BootInfo &bootInfo) noexcept {
    static_cast<void>(bootInfo);
    InitializeGlobalDescriptorTable();
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_GDT_READY_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_TSS_READY_MESSAGE);

    InitializeInterruptDescriptorTable();
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_IDT_READY_MESSAGE);
    if (ValidateDescriptorTables() != DescriptorTableValidationStatus::Succeeded) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_DESCRIPTOR_TABLES_INVALID_MESSAGE);
        HaltProcessor();
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_DESCRIPTOR_TABLES_VALID_MESSAGE);

    TriggerBreakpoint();
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_EXCEPTION_SELF_TEST_READY_MESSAGE);
}

void InitializeKernelMemorySubsystem(const SerialPort &serialPort,
                                     const BootInfo &bootInfo) noexcept {
    const KernelMemoryInitializationStatus status = InitializeKernelMemory(bootInfo);
    if (status != KernelMemoryInitializationStatus::Succeeded) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_INITIALIZATION_FAILED_PREFIX,
                             static_cast<uint64_t>(status));
        HaltProcessor();
    }
    const KernelMemoryStatistics &statistics = GetKernelMemoryStatistics();
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_MEMORY_MAP_VALID_MESSAGE);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_MAP_ENTRY_COUNT_PREFIX,
                         statistics.memoryMapEntryCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_DESCRIBED_PREFIX,
                         statistics.describedAddressBytes);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_USABLE_PREFIX,
                         statistics.reportedUsableMemoryBytes);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_MANAGED_PREFIX,
                         statistics.managedUsableMemoryBytes);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_FRAME_ALLOCATOR_READY_MESSAGE);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_FREE_FRAME_COUNT_PREFIX,
                         statistics.freeFrameCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_ALLOCATED_FRAME_COUNT_PREFIX,
                         statistics.allocatedFrameCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_RESERVED_FRAME_COUNT_PREFIX,
                         statistics.reservedFrameCount);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PAGING_READY_MESSAGE);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PAGING_ROOT_PREFIX,
                         statistics.pageTableRootPhysicalAddress);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_MEMORY_PERMISSIONS_VALID_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_HEAP_READY_MESSAGE);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_HEAP_CAPACITY_PREFIX,
                         statistics.heapCapacityBytes);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_HEAP_SELF_TEST_PASSED_MESSAGE);
}

void InitializeKernelDevices(const SerialPort &serialPort) noexcept {
    const InterruptRuntimeStatus status = InitializeInterruptRuntime();
    if (status != InterruptRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_DEVICE_INITIALIZATION_FAILED_PREFIX,
                             static_cast<uint64_t>(status));
        HaltProcessor();
    }

    InterruptRuntimeStatistics statistics = GetInterruptRuntimeStatistics();
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_LEGACY_INTERRUPT_ROUTING_READY_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PIC_READY_MESSAGE);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIC_MASK_PREFIX, statistics.picMask);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PIT_READY_MESSAGE);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIT_DIVISOR_PREFIX, statistics.pitDivisor);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIT_FREQUENCY_PREFIX,
                         statistics.pitActualFrequencyHz);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PS2_KEYBOARD_READY_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_ATA_PIO_READY_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_ATA_BOOT_DESCRIPTOR_VALID_MESSAGE);

    TriggerLegacyPicSpuriousInterrupt();
    statistics = GetInterruptRuntimeStatistics();
    if (statistics.spuriousInterruptCount != OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_EXPECTED_COUNT) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_DEVICE_INITIALIZATION_FAILED_PREFIX,
                             statistics.spuriousInterruptCount);
        HaltProcessor();
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_PASSED_MESSAGE);

    EnableInterrupts();
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_INTERRUPTS_ENABLED_MESSAGE);
    do {
        WaitForInterrupt();
        statistics = GetInterruptRuntimeStatistics();
    } while (statistics.timerTickCount < OS_KERNEL_MAIN_TIMER_SELF_TEST_MINIMUM_TICK_COUNT);

    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_TIMER_TICK_COUNT_PREFIX,
                         statistics.timerTickCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_MONOTONIC_MILLISECONDS_PREFIX,
                         statistics.monotonicMilliseconds);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_TIMER_SELF_TEST_PASSED_MESSAGE);
}

void CreateRequiredProcess(const SerialPort &serialPort,
                           const UserProgramSelection selection) noexcept {
    ProcessCreationResult creationResult{};
    UserElfValidationStatus elfValidationStatus = UserElfValidationStatus::Succeeded;
    UserAddressSpaceStatus addressSpaceStatus = UserAddressSpaceStatus::Succeeded;
    const ProcessRuntimeStatus runtimeStatus =
        CreateProcess(selection, creationResult, elfValidationStatus, addressSpaceStatus);
    if (runtimeStatus == ProcessRuntimeStatus::InvalidElf) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_ELF_REJECTED_PREFIX,
                             static_cast<uint64_t>(elfValidationStatus));
        HaltProcessor();
    }
    if (runtimeStatus != ProcessRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_ADDRESS_SPACE_FAILED_PREFIX,
                             static_cast<uint64_t>(addressSpaceStatus));
        HaltProcessor();
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_USER_ELF_VALID_MESSAGE);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_ENTRY_PREFIX,
                         creationResult.entryVirtualAddress);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_MAPPED_PAGE_COUNT_PREFIX,
                         creationResult.mappedPageCount);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_USER_STACK_READY_MESSAGE);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PROCESS_ID_PREFIX, creationResult.processId);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PROCESS_CR3_PREFIX,
                         creationResult.rootPhysicalAddress);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PROCESS_KERNEL_STACK_TOP_PREFIX,
                         ProcessKernelStackTopAddress(creationResult.processIndex));
}

void PrepareRequiredProcesses(const SerialPort &serialPort,
                              const UserProgramSelection selection) noexcept {
    if (InitializeProcessRuntime() != ProcessRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_EXECUTION_FAILED_PREFIX,
                             static_cast<uint64_t>(ProcessRuntimeStatus::SchedulerFailure));
        HaltProcessor();
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PROCESS_RUNTIME_READY_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PIPE_READY_MESSAGE);

    const uint64_t processCount = selection == UserProgramSelection::Smoke
                                      ? OS_KERNEL_MAIN_NORMAL_PROCESS_COUNT
                                      : OS_KERNEL_MAIN_FAULT_PROCESS_COUNT;
    for (uint64_t processIndex = 0ULL; processIndex < processCount; ++processIndex) {
        UserProgramSelection processSelection = selection;
        if (selection == UserProgramSelection::Smoke) {
            if (processIndex == OS_KERNEL_MAIN_FIRST_PROCESS_INDEX) {
                processSelection = UserProgramSelection::IpcProducer;
            } else if (processIndex == OS_KERNEL_MAIN_SECOND_PROCESS_INDEX) {
                processSelection = UserProgramSelection::IpcConsumer;
            } else {
                processSelection = UserProgramSelection::SchedulerWorker;
            }
        }
        CreateRequiredProcess(serialPort, processSelection);
    }
}

[[nodiscard]] bool IsExpectedProcessExecutionResult(const ProcessExecutionResult &result) noexcept {
    const bool exitedSuccessfully = result.terminationReason == ProcessTerminationReason::Exited &&
                                    result.exitCode == OS_KERNEL_MAIN_USER_EXPECTED_EXIT_CODE;
    if (result.selection == UserProgramSelection::IpcProducer) {
        return exitedSuccessfully &&
               result.pipeBytesRead == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.pipeBytesWritten == OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES;
    }
    if (result.selection == UserProgramSelection::IpcConsumer) {
        return exitedSuccessfully &&
               result.pipeBytesRead == OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES &&
               result.pipeBytesWritten == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT;
    }
    if (result.selection == UserProgramSelection::Smoke ||
        result.selection == UserProgramSelection::SchedulerWorker) {
        return exitedSuccessfully &&
               result.pipeBytesRead == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT &&
               result.pipeBytesWritten == OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT;
    }
    if (result.selection == UserProgramSelection::InvalidOpcode) {
        return result.terminationReason == ProcessTerminationReason::Exception &&
               result.exceptionVector == OS_KERNEL_MAIN_USER_INVALID_OPCODE_VECTOR;
    }
    if (result.selection == UserProgramSelection::PageFault) {
        return result.terminationReason == ProcessTerminationReason::Exception &&
               result.exceptionVector == OS_KERNEL_MAIN_USER_PAGE_FAULT_VECTOR &&
               result.exceptionErrorCode == OS_KERNEL_MAIN_USER_PAGE_FAULT_ERROR_CODE &&
               result.pageFaultAddress == OS_KERNEL_MAIN_USER_PAGE_FAULT_ADDRESS;
    }
    return false;
}

void WriteProcessExecutionResult(const SerialPort &serialPort,
                                 const ProcessExecutionResult &result) noexcept {
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PROCESS_ID_PREFIX, result.processId);
    if (result.terminationReason == ProcessTerminationReason::Exited) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_EXIT_CODE_PREFIX,
                             static_cast<uint64_t>(result.exitCode));
    } else if (result.terminationReason == ProcessTerminationReason::Exception) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_EXCEPTION_VECTOR_PREFIX,
                             result.exceptionVector);
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_EXCEPTION_ERROR_CODE_PREFIX,
                             result.exceptionErrorCode);
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_EXCEPTION_RIP_PREFIX,
                             result.exceptionInstructionPointer);
        if (result.exceptionVector == OS_KERNEL_MAIN_USER_PAGE_FAULT_VECTOR) {
            WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_PAGE_FAULT_ADDRESS_PREFIX,
                                 result.pageFaultAddress);
        }
    }
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_SYSTEM_CALL_COUNT_PREFIX,
                         result.systemCallCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PROCESS_RUN_TICKS_PREFIX, result.runTickCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PROCESS_DISPATCH_COUNT_PREFIX,
                         result.dispatchCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PROCESS_PIPE_READ_BYTES_PREFIX,
                         result.pipeBytesRead);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PROCESS_PIPE_WRITTEN_BYTES_PREFIX,
                         result.pipeBytesWritten);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_USER_TERMINATED_MESSAGE);

    if (!IsExpectedProcessExecutionResult(result)) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_USER_RESULT_INVALID_MESSAGE);
        HaltProcessor();
    }
}

[[nodiscard]] bool
ProcessResourcesWereReclaimed(const ProcessRuntimeStatistics &statistics) noexcept {
    return statistics.framesBeforeProcesses.managedFrameCount ==
               statistics.framesAfterProcesses.managedFrameCount &&
           statistics.framesBeforeProcesses.freeFrameCount ==
               statistics.framesAfterProcesses.freeFrameCount &&
           statistics.framesBeforeProcesses.allocatedFrameCount ==
               statistics.framesAfterProcesses.allocatedFrameCount &&
           statistics.framesBeforeProcesses.reservedFrameCount ==
               statistics.framesAfterProcesses.reservedFrameCount;
}

void ExecuteRequiredProcesses(const SerialPort &serialPort,
                              const UserProgramSelection selection) noexcept {
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_USER_RING3_ENTER_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_SCHEDULER_STARTED_MESSAGE);
    const ProcessRuntimeStatus runtimeStatus = ExecuteProcesses();
    if (runtimeStatus != ProcessRuntimeStatus::Succeeded) {
        WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_USER_EXECUTION_FAILED_PREFIX,
                             static_cast<uint64_t>(runtimeStatus));
        HaltProcessor();
    }

    const ProcessRuntimeStatistics statistics = GetProcessRuntimeStatistics();
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_SCHEDULER_CREATED_PROCESS_COUNT_PREFIX,
                         statistics.scheduler.createdProcessCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_SCHEDULER_TERMINATED_PROCESS_COUNT_PREFIX,
                         statistics.scheduler.terminatedProcessCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_SCHEDULER_TIMER_TICK_COUNT_PREFIX,
                         statistics.scheduler.timerTickCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_SCHEDULER_PREEMPTION_COUNT_PREFIX,
                         statistics.scheduler.preemptionCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_SCHEDULER_DISPATCH_COUNT_PREFIX,
                         statistics.scheduler.dispatchCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_SCHEDULER_BLOCK_COUNT_PREFIX,
                         statistics.scheduler.blockCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_SCHEDULER_WAKEUP_COUNT_PREFIX,
                         statistics.scheduler.wakeupCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIPE_CAPACITY_PREFIX,
                         OS_KERNEL_PIPE_CAPACITY_BYTES);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIPE_WRITTEN_BYTES_PREFIX,
                         statistics.ipc.pipe.bytesWritten);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIPE_READ_BYTES_PREFIX,
                         statistics.ipc.pipe.bytesRead);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIPE_READER_BLOCK_COUNT_PREFIX,
                         statistics.ipc.readerBlockCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIPE_WRITER_BLOCK_COUNT_PREFIX,
                         statistics.ipc.writerBlockCount);
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIPE_END_OF_FILE_COUNT_PREFIX,
                         statistics.ipc.endOfFileObservationCount);

    const uint64_t expectedProcessCount = selection == UserProgramSelection::Smoke
                                              ? OS_KERNEL_MAIN_NORMAL_PROCESS_COUNT
                                              : OS_KERNEL_MAIN_FAULT_PROCESS_COUNT;
    if (statistics.scheduler.createdProcessCount != expectedProcessCount ||
        statistics.scheduler.terminatedProcessCount != expectedProcessCount ||
        (selection == UserProgramSelection::Smoke &&
         (statistics.scheduler.preemptionCount < OS_KERNEL_MAIN_MINIMUM_PREEMPTION_COUNT ||
          statistics.scheduler.blockCount < OS_KERNEL_MAIN_MINIMUM_BLOCK_COUNT ||
          statistics.scheduler.wakeupCount != statistics.scheduler.blockCount ||
          statistics.ipc.pipe.bytesWritten != OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES ||
          statistics.ipc.pipe.bytesRead != OS_KERNEL_MAIN_EXPECTED_PIPE_TRANSFER_SIZE_BYTES ||
          statistics.ipc.pipe.bufferedByteCount != OS_KERNEL_MAIN_EXPECTED_EMPTY_PIPE_BYTE_COUNT ||
          !statistics.ipc.pipe.readerClosed || !statistics.ipc.pipe.writerClosed ||
          statistics.ipc.writerBlockCount < OS_KERNEL_MAIN_MINIMUM_BLOCK_COUNT ||
          statistics.ipc.endOfFileObservationCount !=
              OS_KERNEL_MAIN_EXPECTED_END_OF_FILE_OBSERVATION_COUNT ||
          statistics.ipc.brokenPipeObservationCount !=
              OS_KERNEL_MAIN_EXPECTED_BROKEN_PIPE_OBSERVATION_COUNT)) ||
        !ProcessResourcesWereReclaimed(statistics)) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_USER_RESULT_INVALID_MESSAGE);
        HaltProcessor();
    }

    for (uint64_t processIndex = 0ULL; processIndex < expectedProcessCount; ++processIndex) {
        WriteProcessExecutionResult(serialPort, statistics.processes[processIndex]);
    }
    if (selection == UserProgramSelection::Smoke) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PIPE_TRANSFER_VALID_MESSAGE);
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PIPE_ENDPOINTS_CLOSED_MESSAGE);
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PROCESS_RESOURCES_RECLAIMED_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_SCHEDULER_COMPLETE_MESSAGE);
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_USER_RETURNED_TO_KERNEL_MESSAGE);
}

void WriteKeyboardEvent(const SerialPort &serialPort, const KeyboardEvent &event) noexcept {
    WriteRequiredHexLine(serialPort, OS_KERNEL_MAIN_KEYBOARD_SCAN_CODE_PREFIX, event.scanCode);
    if (event.key == KeyboardKey::A && event.pressed) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_KEYBOARD_A_PRESSED_MESSAGE);
        return;
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_KEYBOARD_SUPPORTED_EVENT_MESSAGE);
}

[[noreturn]] void RunKernelEventLoop(const SerialPort &serialPort) noexcept {
    while (true) {
        WaitForInterrupt();
        KeyboardEvent event{};
        if (TryTakeKeyboardEvent(event)) {
            WriteKeyboardEvent(serialPort, event);
        }
    }
}

[[noreturn]] void ExecuteFaultInjection(const SerialPort &serialPort,
                                        const KernelFaultInjection faultInjection) noexcept {
    if (faultInjection == KernelFaultInjection::InvalidOpcode) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_INVALID_OPCODE_INJECTION_MESSAGE);
        TriggerInvalidOpcode();
    }
    if (faultInjection == KernelFaultInjection::WriteProtection) {
        WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_WRITE_PROTECTION_INJECTION_MESSAGE);
        TriggerWriteProtectionFault(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS);
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_PAGE_FAULT_INJECTION_MESSAGE);
    TriggerPageFault();
}

}

[[noreturn]] void RunKernel(const BootInfo *bootInfo, const KernelFaultInjection faultInjection,
                            const UserProgramSelection userProgramSelection) noexcept {
    const SerialPort serialPort{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    serialPort.Initialize();
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_ENTERED_MESSAGE);

    ValidateBootEnvironment(serialPort, bootInfo);
    InitializeKernelArchitecture(serialPort, *bootInfo);
    InitializeKernelMemorySubsystem(serialPort, *bootInfo);

    if (faultInjection != KernelFaultInjection::None) {
        ExecuteFaultInjection(serialPort, faultInjection);
    }

    PrepareRequiredProcesses(serialPort, userProgramSelection);
    InitializeKernelDevices(serialPort);
    ExecuteRequiredProcesses(serialPort, userProgramSelection);

    if (!serialPort.TryWriteHexLine(OS_KERNEL_MAIN_FILE_SIZE_PREFIX,
                                    bootInfo->kernelFileSizeBytes) ||
        !serialPort.TryWriteHexLine(OS_KERNEL_MAIN_LOAD_SEGMENT_COUNT_PREFIX,
                                    bootInfo->kernelLoadSegmentCount)) {
        HaltProcessor();
    }
    WriteRequiredMessage(serialPort, OS_KERNEL_MAIN_READY_MESSAGE);
    RunKernelEventLoop(serialPort);
}

}
