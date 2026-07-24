#include "os/kernel/panic.hpp"

#include "os/kernel/processor.hpp"
#include "os/kernel/serial_port.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PANIC_STATE_INACTIVE = 0ULL;
constexpr uint64_t OS_KERNEL_PANIC_STATE_ACTIVE = 1ULL;
constexpr uint64_t OS_KERNEL_PANIC_PAGE_FAULT_VECTOR = 14ULL;
constexpr char OS_KERNEL_PANIC_EXCEPTION_MESSAGE[] = "[OS][KERNEL] EXCEPTION\r\n";
constexpr char OS_KERNEL_PANIC_VECTOR_PREFIX[] = "[OS][KERNEL] EXCEPTION_VECTOR=";
constexpr char OS_KERNEL_PANIC_ERROR_CODE_PREFIX[] = "[OS][KERNEL] EXCEPTION_ERROR_CODE=";
constexpr char OS_KERNEL_PANIC_INSTRUCTION_POINTER_PREFIX[] = "[OS][KERNEL] EXCEPTION_RIP=";
constexpr char OS_KERNEL_PANIC_CODE_SEGMENT_PREFIX[] = "[OS][KERNEL] EXCEPTION_CS=";
constexpr char OS_KERNEL_PANIC_FLAGS_PREFIX[] = "[OS][KERNEL] EXCEPTION_RFLAGS=";
constexpr char OS_KERNEL_PANIC_PAGE_FAULT_ADDRESS_PREFIX[] = "[OS][KERNEL] PAGE_FAULT_ADDRESS=";
constexpr char OS_KERNEL_PANIC_TERMINAL_MESSAGE[] = "[OS][KERNEL] PANIC\r\n";

uint64_t kernelPanicState;

void tryWritePanicReport(const SerialPort &serialPort, const ExceptionFrame &frame) noexcept {
    if (!serialPort.tryWriteString(OS_KERNEL_PANIC_EXCEPTION_MESSAGE) ||
        !serialPort.tryWriteHexLine(OS_KERNEL_PANIC_VECTOR_PREFIX, frame.vector) ||
        !serialPort.tryWriteHexLine(OS_KERNEL_PANIC_ERROR_CODE_PREFIX, frame.errorCode) ||
        !serialPort.tryWriteHexLine(OS_KERNEL_PANIC_INSTRUCTION_POINTER_PREFIX,
                                    frame.instructionPointer) ||
        !serialPort.tryWriteHexLine(OS_KERNEL_PANIC_CODE_SEGMENT_PREFIX, frame.codeSegment) ||
        !serialPort.tryWriteHexLine(OS_KERNEL_PANIC_FLAGS_PREFIX, frame.flags)) {
        return;
    }
    if (frame.vector == OS_KERNEL_PANIC_PAGE_FAULT_VECTOR &&
        !serialPort.tryWriteHexLine(OS_KERNEL_PANIC_PAGE_FAULT_ADDRESS_PREFIX,
                                    readPageFaultLinearAddress())) {
        return;
    }
    static_cast<void>(serialPort.tryWriteString(OS_KERNEL_PANIC_TERMINAL_MESSAGE));
}

}

[[noreturn]] void panicFromException(const ExceptionFrame &frame) noexcept {
    asm volatile("cli");
    if (kernelPanicState != OS_KERNEL_PANIC_STATE_INACTIVE) {
        haltProcessor();
    }
    kernelPanicState = OS_KERNEL_PANIC_STATE_ACTIVE;

    const SerialPort serialPort{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    serialPort.initialize();
    tryWritePanicReport(serialPort, frame);
    haltProcessor();
}

}
