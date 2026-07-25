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

uint64_t kernel_panic_state;

void TryWritePanicReport(const SerialPort &serial_port, const ExceptionFrame &frame) noexcept {
    if (!serial_port.TryWriteString(OS_KERNEL_PANIC_EXCEPTION_MESSAGE) ||
        !serial_port.TryWriteHexLine(OS_KERNEL_PANIC_VECTOR_PREFIX, frame.vector) ||
        !serial_port.TryWriteHexLine(OS_KERNEL_PANIC_ERROR_CODE_PREFIX, frame.error_code) ||
        !serial_port.TryWriteHexLine(OS_KERNEL_PANIC_INSTRUCTION_POINTER_PREFIX,
                                     frame.instruction_pointer) ||
        !serial_port.TryWriteHexLine(OS_KERNEL_PANIC_CODE_SEGMENT_PREFIX, frame.code_segment) ||
        !serial_port.TryWriteHexLine(OS_KERNEL_PANIC_FLAGS_PREFIX, frame.flags)) {
        return;
    }
    if (frame.vector == OS_KERNEL_PANIC_PAGE_FAULT_VECTOR &&
        !serial_port.TryWriteHexLine(OS_KERNEL_PANIC_PAGE_FAULT_ADDRESS_PREFIX,
                                     ReadPageFaultLinearAddress())) {
        return;
    }
    static_cast<void>(serial_port.TryWriteString(OS_KERNEL_PANIC_TERMINAL_MESSAGE));
}
}

[[noreturn]] void PanicFromException(const ExceptionFrame &frame) noexcept {
    asm volatile("cli");
    if (kernel_panic_state != OS_KERNEL_PANIC_STATE_INACTIVE) {
        HaltProcessor();
    }
    kernel_panic_state = OS_KERNEL_PANIC_STATE_ACTIVE;

    const SerialPort serial_port{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    serial_port.Initialize();
    TryWritePanicReport(serial_port, frame);
    HaltProcessor();
}

}
