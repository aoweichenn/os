#include "os/kernel/kernel_main.hpp"

#include "os/kernel/descriptor_tables.hpp"
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
constexpr char OS_KERNEL_MAIN_INVALID_OPCODE_INJECTION_MESSAGE[] =
    "[OS][KERNEL] FAULT_INJECTION=INVALID_OPCODE\r\n";
constexpr char OS_KERNEL_MAIN_PAGE_FAULT_INJECTION_MESSAGE[] =
    "[OS][KERNEL] FAULT_INJECTION=PAGE_FAULT\r\n";
constexpr char OS_KERNEL_MAIN_WRITE_PROTECTION_INJECTION_MESSAGE[] =
    "[OS][KERNEL] FAULT_INJECTION=WRITE_PROTECTION\r\n";
constexpr char OS_KERNEL_MAIN_FILE_SIZE_PREFIX[] = "[OS][KERNEL] FILE_SIZE=";
constexpr char OS_KERNEL_MAIN_LOAD_SEGMENT_COUNT_PREFIX[] = "[OS][KERNEL] LOAD_SEGMENTS=";
constexpr char OS_KERNEL_MAIN_READY_MESSAGE[] = "[OS][KERNEL] READY\r\n";

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

    if (!serialPort.tryWriteHexLine(OS_KERNEL_MAIN_FILE_SIZE_PREFIX,
                                    bootInfo->kernelFileSizeBytes) ||
        !serialPort.tryWriteHexLine(OS_KERNEL_MAIN_LOAD_SEGMENT_COUNT_PREFIX,
                                    bootInfo->kernelLoadSegmentCount)) {
        haltProcessor();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_MAIN_READY_MESSAGE);
    haltProcessor();
}

}
