#include "os/kernel/exceptions.hpp"

#include "os/kernel/panic.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/serial_port.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_EXCEPTION_NORMALIZED_ERROR_CODE = 0ULL;
constexpr char OS_KERNEL_EXCEPTION_BREAKPOINT_HANDLED_MESSAGE[] =
    "[OS][KERNEL] BREAKPOINT_HANDLED\r\n";

void writeBreakpointHandled() noexcept {
    const SerialPort serialPort{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    if (!serialPort.tryWriteString(OS_KERNEL_EXCEPTION_BREAKPOINT_HANDLED_MESSAGE)) {
        haltProcessor();
    }
}

}

extern "C" void osKernelDispatchException(ExceptionFrame *frame) noexcept {
    if (frame == nullptr) {
        haltProcessor();
    }
    if (isResumableKernelException(frame->vector) &&
        frame->errorCode == OS_KERNEL_EXCEPTION_NORMALIZED_ERROR_CODE) {
        writeBreakpointHandled();
        return;
    }
    panicFromException(*frame);
}

}
