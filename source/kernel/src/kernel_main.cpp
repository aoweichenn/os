#include "os/kernel/kernel_main.hpp"

#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/interrupt_runtime.hpp"
#include "os/kernel/memory_manager.hpp"
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
constexpr char OS_KERNEL_MAIN_FILE_SIZE_PREFIX[] = "[OS][KERNEL] FILE_SIZE=";
constexpr char OS_KERNEL_MAIN_LOAD_SEGMENT_COUNT_PREFIX[] = "[OS][KERNEL] LOAD_SEGMENTS=";
constexpr char OS_KERNEL_MAIN_READY_MESSAGE[] = "[OS][KERNEL] READY\r\n";
constexpr uint64_t OS_KERNEL_MAIN_TIMER_SELF_TEST_MINIMUM_TICK_COUNT = 16ULL;
constexpr uint64_t OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_EXPECTED_COUNT = 1ULL;

// 非零初值不能用于证明加载器执行了 p_memsz 对应的 BSS 清零。
uint64_t kernelMainBssProbe;

void writeRequiredMessage(const SerialPort &serialPort, const char *message) noexcept {
    if (!serialPort.tryWriteString(message)) {
        haltProcessor();
    }
}

void writeRequiredHexLine(const SerialPort &serialPort, const char *prefix,
                          const uint64_t value) noexcept {
    if (!serialPort.tryWriteHexLine(prefix, value)) {
        haltProcessor();
    }
}

void validateBootEnvironment(const SerialPort &serialPort, const BootInfo *bootInfo) noexcept {
    if (validateBootInfo(bootInfo) != BootInfoValidationStatus::Succeeded) {
        writeRequiredMessage(serialPort, OS_KERNEL_MAIN_BOOT_INFO_INVALID_MESSAGE);
        haltProcessor();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_BOOT_INFO_VALID_MESSAGE);

    if (kernelMainBssProbe != OS_KERNEL_MAIN_BSS_PROBE_ZERO_VALUE) {
        writeRequiredMessage(serialPort, OS_KERNEL_MAIN_BSS_INVALID_MESSAGE);
        haltProcessor();
    }
    kernelMainBssProbe = OS_KERNEL_MAIN_BSS_PROBE_WRITTEN_VALUE;
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_BSS_ZEROED_MESSAGE);

    if (readPageTableRoot() != bootInfo->pageTableRootPhysicalAddress) {
        writeRequiredMessage(serialPort, OS_KERNEL_MAIN_CR3_INVALID_MESSAGE);
        haltProcessor();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_CR3_VALID_MESSAGE);
}

void initializeKernelArchitecture(const SerialPort &serialPort, const BootInfo &bootInfo) noexcept {
    initializeGlobalDescriptorTable(bootInfo.kernelStackTopPhysicalAddress);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_GDT_READY_MESSAGE);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_TSS_READY_MESSAGE);

    initializeInterruptDescriptorTable();
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_IDT_READY_MESSAGE);
    if (validateDescriptorTables(bootInfo.kernelStackTopPhysicalAddress) !=
        DescriptorTableValidationStatus::Succeeded) {
        writeRequiredMessage(serialPort, OS_KERNEL_MAIN_DESCRIPTOR_TABLES_INVALID_MESSAGE);
        haltProcessor();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_DESCRIPTOR_TABLES_VALID_MESSAGE);

    triggerBreakpoint();
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_EXCEPTION_SELF_TEST_READY_MESSAGE);
}

void initializeKernelMemorySubsystem(const SerialPort &serialPort,
                                     const BootInfo &bootInfo) noexcept {
    const KernelMemoryInitializationStatus status = initializeKernelMemory(bootInfo);
    if (status != KernelMemoryInitializationStatus::Succeeded) {
        writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_INITIALIZATION_FAILED_PREFIX,
                             static_cast<uint64_t>(status));
        haltProcessor();
    }
    const KernelMemoryStatistics &statistics = kernelMemoryStatistics();
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_MEMORY_MAP_VALID_MESSAGE);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_MAP_ENTRY_COUNT_PREFIX,
                         statistics.memoryMapEntryCount);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_DESCRIBED_PREFIX,
                         statistics.describedAddressBytes);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_USABLE_PREFIX,
                         statistics.reportedUsableMemoryBytes);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_MEMORY_MANAGED_PREFIX,
                         statistics.managedUsableMemoryBytes);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_FRAME_ALLOCATOR_READY_MESSAGE);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_FREE_FRAME_COUNT_PREFIX,
                         statistics.freeFrameCount);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_ALLOCATED_FRAME_COUNT_PREFIX,
                         statistics.allocatedFrameCount);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_RESERVED_FRAME_COUNT_PREFIX,
                         statistics.reservedFrameCount);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_PAGING_READY_MESSAGE);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_PAGING_ROOT_PREFIX,
                         statistics.pageTableRootPhysicalAddress);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_MEMORY_PERMISSIONS_VALID_MESSAGE);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_HEAP_READY_MESSAGE);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_HEAP_CAPACITY_PREFIX,
                         statistics.heapCapacityBytes);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_HEAP_SELF_TEST_PASSED_MESSAGE);
}

void initializeKernelDevices(const SerialPort &serialPort) noexcept {
    const InterruptRuntimeStatus status = initializeInterruptRuntime();
    if (status != InterruptRuntimeStatus::Succeeded) {
        writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_DEVICE_INITIALIZATION_FAILED_PREFIX,
                             static_cast<uint64_t>(status));
        haltProcessor();
    }

    InterruptRuntimeStatistics statistics = interruptRuntimeStatistics();
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_LEGACY_INTERRUPT_ROUTING_READY_MESSAGE);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_PIC_READY_MESSAGE);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIC_MASK_PREFIX, statistics.picMask);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_PIT_READY_MESSAGE);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIT_DIVISOR_PREFIX, statistics.pitDivisor);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_PIT_FREQUENCY_PREFIX,
                         statistics.pitActualFrequencyHz);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_PS2_KEYBOARD_READY_MESSAGE);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_ATA_PIO_READY_MESSAGE);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_ATA_BOOT_DESCRIPTOR_VALID_MESSAGE);

    triggerLegacyPicSpuriousInterrupt();
    statistics = interruptRuntimeStatistics();
    if (statistics.spuriousInterruptCount != OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_EXPECTED_COUNT) {
        writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_DEVICE_INITIALIZATION_FAILED_PREFIX,
                             statistics.spuriousInterruptCount);
        haltProcessor();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_PIC_SPURIOUS_SELF_TEST_PASSED_MESSAGE);

    enableInterrupts();
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_INTERRUPTS_ENABLED_MESSAGE);
    do {
        waitForInterrupt();
        statistics = interruptRuntimeStatistics();
    } while (statistics.timerTickCount < OS_KERNEL_MAIN_TIMER_SELF_TEST_MINIMUM_TICK_COUNT);

    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_TIMER_TICK_COUNT_PREFIX,
                         statistics.timerTickCount);
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_MONOTONIC_MILLISECONDS_PREFIX,
                         statistics.monotonicMilliseconds);
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_TIMER_SELF_TEST_PASSED_MESSAGE);
}

void writeKeyboardEvent(const SerialPort &serialPort, const KeyboardEvent &event) noexcept {
    writeRequiredHexLine(serialPort, OS_KERNEL_MAIN_KEYBOARD_SCAN_CODE_PREFIX, event.scanCode);
    if (event.key == KeyboardKey::A && event.pressed) {
        writeRequiredMessage(serialPort, OS_KERNEL_MAIN_KEYBOARD_A_PRESSED_MESSAGE);
        return;
    }
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_KEYBOARD_SUPPORTED_EVENT_MESSAGE);
}

[[noreturn]] void runKernelEventLoop(const SerialPort &serialPort) noexcept {
    while (true) {
        waitForInterrupt();
        KeyboardEvent event{};
        if (tryTakeKeyboardEvent(event)) {
            writeKeyboardEvent(serialPort, event);
        }
    }
}

[[noreturn]] void executeFaultInjection(const SerialPort &serialPort,
                                        const KernelFaultInjection faultInjection) noexcept {
    if (faultInjection == KernelFaultInjection::InvalidOpcode) {
        writeRequiredMessage(serialPort, OS_KERNEL_MAIN_INVALID_OPCODE_INJECTION_MESSAGE);
        triggerInvalidOpcode();
    }
    if (faultInjection == KernelFaultInjection::WriteProtection) {
        writeRequiredMessage(serialPort, OS_KERNEL_MAIN_WRITE_PROTECTION_INJECTION_MESSAGE);
        triggerWriteProtectionFault(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS);
    }
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_PAGE_FAULT_INJECTION_MESSAGE);
    triggerPageFault();
}

}

[[noreturn]] void runKernel(const BootInfo *bootInfo,
                            const KernelFaultInjection faultInjection) noexcept {
    const SerialPort serialPort{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    serialPort.initialize();
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_ENTERED_MESSAGE);

    validateBootEnvironment(serialPort, bootInfo);
    initializeKernelArchitecture(serialPort, *bootInfo);
    initializeKernelMemorySubsystem(serialPort, *bootInfo);

    if (faultInjection != KernelFaultInjection::None) {
        executeFaultInjection(serialPort, faultInjection);
    }

    initializeKernelDevices(serialPort);

    if (!serialPort.tryWriteHexLine(OS_KERNEL_MAIN_FILE_SIZE_PREFIX,
                                    bootInfo->kernelFileSizeBytes) ||
        !serialPort.tryWriteHexLine(OS_KERNEL_MAIN_LOAD_SEGMENT_COUNT_PREFIX,
                                    bootInfo->kernelLoadSegmentCount)) {
        haltProcessor();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_READY_MESSAGE);
    runKernelEventLoop(serialPort);
}

}
