#include "os/kernel/exceptions.hpp"

#include "os/kernel/panic.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/serial_port.hpp"
#include "os/kernel/user_runtime.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_EXCEPTION_NORMALIZED_ERROR_CODE = 0ULL;
constexpr uint64_t OS_KERNEL_EXCEPTION_PAGE_FAULT_VECTOR = 14ULL;
constexpr uint64_t OS_KERNEL_EXCEPTION_NON_PAGE_FAULT_ADDRESS = 0ULL;
constexpr char OS_KERNEL_EXCEPTION_BREAKPOINT_HANDLED_MESSAGE[] =
    "[OS][KERNEL] BREAKPOINT_HANDLED\r\n";

void WriteBreakpointHandled() noexcept {
    const SerialPort serialPort{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    if (!serialPort.TryWriteString(OS_KERNEL_EXCEPTION_BREAKPOINT_HANDLED_MESSAGE)) {
        HaltProcessor();
    }
}

}

extern "C" void osKernelDispatchException(ExceptionFrame *frame) noexcept {
    if (frame == nullptr) {
        HaltProcessor();
    }
    if (FrameOriginatedFromUser(*frame)) {
        const uint64_t pageFaultAddress = frame->vector == OS_KERNEL_EXCEPTION_PAGE_FAULT_VECTOR
                                              ? ReadPageFaultLinearAddress()
                                              : OS_KERNEL_EXCEPTION_NON_PAGE_FAULT_ADDRESS;
        TerminateUserExecutionFromException(*frame, pageFaultAddress);
    }
    if (IsResumableKernelException(frame->vector) &&
        frame->errorCode == OS_KERNEL_EXCEPTION_NORMALIZED_ERROR_CODE) {
        WriteBreakpointHandled();
        return;
    }
    PanicFromException(*frame);
}

}
