#include "os/kernel/entry.hpp"
#include "os/kernel/serial_port.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_ENTRY_BSS_PROBE_WRITTEN_VALUE = 0xB007B007B007B007ULL;
constexpr char OS_KERNEL_ENTRY_ENTERED_MESSAGE[] = "[OS][KERNEL] ENTERED\r\n";
constexpr char OS_KERNEL_ENTRY_BOOT_INFO_VALID_MESSAGE[] = "[OS][KERNEL] BOOT_INFO_VALID\r\n";
constexpr char OS_KERNEL_ENTRY_BOOT_INFO_INVALID_MESSAGE[] = "[OS][KERNEL] BOOT_INFO_INVALID\r\n";
constexpr char OS_KERNEL_ENTRY_BSS_ZEROED_MESSAGE[] = "[OS][KERNEL] BSS_ZEROED\r\n";
constexpr char OS_KERNEL_ENTRY_BSS_INVALID_MESSAGE[] = "[OS][KERNEL] BSS_INVALID\r\n";
constexpr char OS_KERNEL_ENTRY_CR3_VALID_MESSAGE[] = "[OS][KERNEL] CR3_VALID\r\n";
constexpr char OS_KERNEL_ENTRY_CR3_INVALID_MESSAGE[] = "[OS][KERNEL] CR3_INVALID\r\n";
constexpr char OS_KERNEL_ENTRY_FILE_SIZE_PREFIX[] = "[OS][KERNEL] FILE_SIZE=";
constexpr char OS_KERNEL_ENTRY_LOAD_SEGMENT_COUNT_PREFIX[] = "[OS][KERNEL] LOAD_SEGMENTS=";
constexpr char OS_KERNEL_ENTRY_READY_MESSAGE[] = "[OS][KERNEL] READY\r\n";

// 非零初值不能用于证明加载器执行了 p_memsz 对应的 BSS 清零。
uint64_t kernelEntryBssProbe;

[[noreturn]] void haltForever() noexcept {
    while (true) {
        asm volatile("hlt");
    }
}

[[nodiscard]] uint64_t readPageTableRoot() noexcept {
    uint64_t pageTableRoot = 0ULL;
    asm volatile("mov %0, cr3" : "=r"(pageTableRoot));
    return pageTableRoot;
}

void writeRequiredMessage(const SerialPort &serialPort, const char *message) noexcept {
    if (!serialPort.tryWriteString(message)) {
        haltForever();
    }
}

}

extern "C" [[noreturn, gnu::section(".text.os_kernel_entry")]]
void osKernelEntry(const BootInfo *bootInfo) noexcept {
    const SerialPort serialPort{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    serialPort.initialize();
    writeRequiredMessage(serialPort, OS_KERNEL_ENTRY_ENTERED_MESSAGE);

    if (validateBootInfo(bootInfo) != BootInfoValidationStatus::Succeeded) {
        writeRequiredMessage(serialPort, OS_KERNEL_ENTRY_BOOT_INFO_INVALID_MESSAGE);
        haltForever();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_ENTRY_BOOT_INFO_VALID_MESSAGE);

    if (kernelEntryBssProbe != 0ULL) {
        writeRequiredMessage(serialPort, OS_KERNEL_ENTRY_BSS_INVALID_MESSAGE);
        haltForever();
    }
    kernelEntryBssProbe = OS_KERNEL_ENTRY_BSS_PROBE_WRITTEN_VALUE;
    writeRequiredMessage(serialPort, OS_KERNEL_ENTRY_BSS_ZEROED_MESSAGE);

    if (readPageTableRoot() != bootInfo->pageTableRootPhysicalAddress) {
        writeRequiredMessage(serialPort, OS_KERNEL_ENTRY_CR3_INVALID_MESSAGE);
        haltForever();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_ENTRY_CR3_VALID_MESSAGE);

    if (!serialPort.tryWriteHexLine(OS_KERNEL_ENTRY_FILE_SIZE_PREFIX,
                                    bootInfo->kernelFileSizeBytes) ||
        !serialPort.tryWriteHexLine(OS_KERNEL_ENTRY_LOAD_SEGMENT_COUNT_PREFIX,
                                    bootInfo->kernelLoadSegmentCount)) {
        haltForever();
    }
    writeRequiredMessage(serialPort, OS_KERNEL_ENTRY_READY_MESSAGE);
    haltForever();
}

}
