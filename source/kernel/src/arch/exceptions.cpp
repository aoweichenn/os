#include "os/kernel/arch/exceptions.hpp"

#include "os/kernel/arch/panic.hpp"
#include "os/kernel/arch/processor.hpp"
#include "os/kernel/device/serial_port.hpp"
#include "os/kernel/process/process_runtime.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_EXCEPTION_NORMALIZED_ERROR_CODE = 0ULL;
constexpr uint64_t OS_KERNEL_EXCEPTION_PAGE_FAULT_VECTOR = 14ULL;
constexpr uint64_t OS_KERNEL_EXCEPTION_NON_PAGE_FAULT_ADDRESS = 0ULL;
constexpr char OS_KERNEL_EXCEPTION_BREAKPOINT_HANDLED_MESSAGE[] =
    "[OS][KERNEL] BREAKPOINT_HANDLED\r\n";

void WriteBreakpointHandled() noexcept {
    const SerialPort serial_port{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    if (!serial_port.TryWriteString(OS_KERNEL_EXCEPTION_BREAKPOINT_HANDLED_MESSAGE)) {
        HaltProcessor();
    }
}

}

extern "C" ExceptionFrame *OsKernelDispatchException(ExceptionFrame *frame) noexcept {
    if (frame == nullptr) {
        HaltProcessor();
    }
    if (FrameOriginatedFromUser(*frame)) {
        const uint64_t page_fault_address = frame->vector == OS_KERNEL_EXCEPTION_PAGE_FAULT_VECTOR
                                                ? ReadPageFaultLinearAddress()
                                                : OS_KERNEL_EXCEPTION_NON_PAGE_FAULT_ADDRESS;
        if (frame->vector == OS_KERNEL_EXCEPTION_PAGE_FAULT_VECTOR &&
            HandleCurrentProcessPageFault(*frame, page_fault_address)) {
            return frame;
        }
        return TerminateCurrentProcessFromException(*frame, page_fault_address);
    }
    if (IsResumableKernelException(frame->vector) &&
        frame->error_code == OS_KERNEL_EXCEPTION_NORMALIZED_ERROR_CODE) {
        WriteBreakpointHandled();
        return frame;
    }
    PanicFromException(*frame);
}

}
