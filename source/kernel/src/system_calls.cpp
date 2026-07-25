#include "os/kernel/system_calls.hpp"

#include "os/abi/system_call.hpp"
#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/memory_manager.hpp"
#include "os/kernel/process_runtime.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/serial_port.hpp"
#include "os/kernel/user_elf.hpp"
#include "os/kernel/user_memory.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_SIZE_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WAKE_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_FIRST_BYTE_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_ADDRESS_PROBE_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_WAKE_ALL_PROCESS_COUNT = OS_KERNEL_PROCESS_CAPACITY;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_RESULT = 0LL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT = 0LL;

[[nodiscard]] int64_t MapPipeStatus(const PipeStatus status,
                                    const uint64_t transferredBytes) noexcept {
    if (status == PipeStatus::Succeeded) {
        return static_cast<int64_t>(transferredBytes);
    }
    if (status == PipeStatus::WouldBlock) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK;
    }
    if (status == PipeStatus::EndOfFile) {
        return OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT;
    }
    if (status == PipeStatus::BrokenPipe) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_BROKEN_PIPE;
    }
    if (status == PipeStatus::AlreadyClosed) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_ENDPOINT_CLOSED;
    }
    return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
}

[[nodiscard]] bool ValidateUserSystemCallFrame(const ExceptionFrame &frame) noexcept {
    if (!IsProcessSchedulingActive() || !FrameOriginatedFromUser(frame) ||
        frame.vector != os::abi::OS_ABI_SYSTEM_CALL_VECTOR ||
        frame.codeSegment != static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR)) {
        return false;
    }
    const UserPrivilegeFrame &userFrame = AsUserPrivilegeFrame(frame);
    if (userFrame.userStackSegment !=
            static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR) ||
        !IsUserProgramVirtualAddressRange(frame.instructionPointer,
                                          OS_KERNEL_SYSTEM_CALL_ADDRESS_PROBE_SIZE_BYTES) ||
        userFrame.userStackPointer < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
        userFrame.userStackPointer >= OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return false;
    }
    PageMapping instructionMapping{};
    PageMapping stackMapping{};
    return QueryActivePage(frame.instructionPointer, instructionMapping) ==
               PageTableStatus::Succeeded &&
           instructionMapping.permissions.userAccessible &&
           instructionMapping.permissions.executable && !instructionMapping.permissions.writable &&
           QueryActivePage(userFrame.userStackPointer, stackMapping) ==
               PageTableStatus::Succeeded &&
           stackMapping.permissions.userAccessible && stackMapping.permissions.writable &&
           !stackMapping.permissions.executable;
}

[[nodiscard]] int64_t DispatchWriteLog(const uint64_t userAddress,
                                       const uint64_t lengthBytes) noexcept {
    if (lengthBytes == OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_SIZE_BYTES) {
        return OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_RESULT;
    }
    if (lengthBytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_WRITE_TOO_LARGE;
    }
    uint8_t message[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES]{};
    if (CopyFromUser(userAddress, lengthBytes, message,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_WRITE_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    const SerialPort serialPort{OS_KERNEL_SERIAL_COM1_BASE_PORT};
    for (uint64_t byteIndex = OS_KERNEL_SYSTEM_CALL_FIRST_BYTE_INDEX; byteIndex < lengthBytes;
         ++byteIndex) {
        if (!serialPort.TryWriteByte(static_cast<char>(message[byteIndex]))) {
            return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE;
        }
    }
    return static_cast<int64_t>(lengthBytes);
}

void WakePipeWaiters(const ProcessWaitReason waitReason) noexcept {
    uint64_t wokenProcessCount = OS_KERNEL_SYSTEM_CALL_EMPTY_WAKE_COUNT;
    if (WakeProcesses(waitReason, OS_KERNEL_SYSTEM_CALL_WAKE_ALL_PROCESS_COUNT,
                      wokenProcessCount) != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
}

[[nodiscard]] int64_t DispatchTryReadPipe(const uint64_t userAddress,
                                          const uint64_t capacityBytes) noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED;
    }
    if (capacityBytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (capacityBytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_TRANSFER_TOO_LARGE;
    }
    if (ValidateUserWritableMemory(userAddress, capacityBytes) != UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }

    uint8_t buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES]{};
    uint64_t readBytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const PipeStatus status = TryReadCurrentProcessPipe(buffer, capacityBytes, readBytes);
    if (status == PipeStatus::Succeeded) {
        if (CopyToUser(userAddress, readBytes, buffer, capacityBytes) !=
            UserMemoryCopyStatus::Succeeded) {
            HaltProcessor();
        }
        WakePipeWaiters(ProcessWaitReason::PipeWritable);
    }
    return MapPipeStatus(status, readBytes);
}

[[nodiscard]] int64_t DispatchTryWritePipe(const uint64_t userAddress,
                                           const uint64_t lengthBytes) noexcept {
    if (!CurrentProcessCanWritePipe()) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED;
    }
    if (lengthBytes == OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    if (lengthBytes > os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_TRANSFER_TOO_LARGE;
    }

    uint8_t buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES]{};
    if (CopyFromUser(userAddress, lengthBytes, buffer,
                     os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES) !=
        UserMemoryCopyStatus::Succeeded) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY;
    }
    uint64_t writtenBytes = OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    const PipeStatus status = TryWriteCurrentProcessPipe(buffer, lengthBytes, writtenBytes);
    if (status == PipeStatus::Succeeded) {
        WakePipeWaiters(ProcessWaitReason::PipeReadable);
    }
    return MapPipeStatus(status, writtenBytes);
}

[[nodiscard]] ExceptionFrame *DispatchWaitPipe(ExceptionFrame &frame,
                                               const ProcessWaitReason waitReason) noexcept {
    const bool canProgress = waitReason == ProcessWaitReason::PipeReadable
                                 ? ProcessPipeReadCanProgress()
                                 : ProcessPipeWriteCanProgress();
    if (canProgress) {
        frame.registerRax = static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT);
        return &frame;
    }

    frame.registerRax = static_cast<uint64_t>(OS_KERNEL_SYSTEM_CALL_PIPE_SUCCESS_RESULT);
    ExceptionFrame *resumeFrame = &frame;
    const ProcessRuntimeStatus status = BlockCurrentProcess(frame, waitReason, resumeFrame);
    if (status == ProcessRuntimeStatus::NoReadyProcess) {
        frame.registerRax =
            static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_NO_READY_PROCESS);
        return &frame;
    }
    if (status != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
    return resumeFrame;
}

[[nodiscard]] int64_t DispatchClosePipeReader() noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED;
    }
    const PipeStatus status = CloseCurrentProcessPipeReader();
    return MapPipeStatus(status, OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES);
}

[[nodiscard]] int64_t DispatchClosePipeWriter() noexcept {
    if (!CurrentProcessCanWritePipe()) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED;
    }
    const PipeStatus status = CloseCurrentProcessPipeWriter();
    return MapPipeStatus(status, OS_KERNEL_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES);
}

}

extern "C" ExceptionFrame *osKernelDispatchSystemCall(ExceptionFrame *frame) noexcept {
    if (frame == nullptr || !ValidateUserSystemCallFrame(*frame)) {
        HaltProcessor();
    }
    RecordCurrentProcessSystemCall();

    const uint64_t systemCallNumber = frame->registerRax;
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::WriteLog)) {
        frame->registerRax =
            static_cast<uint64_t>(DispatchWriteLog(frame->registerRdi, frame->registerRsi));
        return frame;
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::ExitProcess)) {
        return TerminateCurrentProcessFromExit(*frame, static_cast<int64_t>(frame->registerRdi));
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessId)) {
        frame->registerRax = CurrentProcessId();
        return frame;
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::TryReadPipe)) {
        frame->registerRax =
            static_cast<uint64_t>(DispatchTryReadPipe(frame->registerRdi, frame->registerRsi));
        return frame;
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::TryWritePipe)) {
        frame->registerRax =
            static_cast<uint64_t>(DispatchTryWritePipe(frame->registerRdi, frame->registerRsi));
        return frame;
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPipeReadable)) {
        if (!CurrentProcessCanReadPipe()) {
            frame->registerRax =
                static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED);
            return frame;
        }
        return DispatchWaitPipe(*frame, ProcessWaitReason::PipeReadable);
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPipeWritable)) {
        if (!CurrentProcessCanWritePipe()) {
            frame->registerRax =
                static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_PIPE_PERMISSION_DENIED);
            return frame;
        }
        return DispatchWaitPipe(*frame, ProcessWaitReason::PipeWritable);
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::ClosePipeReader)) {
        frame->registerRax = static_cast<uint64_t>(DispatchClosePipeReader());
        return frame;
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::ClosePipeWriter)) {
        frame->registerRax = static_cast<uint64_t>(DispatchClosePipeWriter());
        return frame;
    }
    frame->registerRax = static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER);
    return frame;
}

}
