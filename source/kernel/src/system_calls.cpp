#include "os/kernel/system_calls.hpp"

#include "os/abi/system_call.hpp"
#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/memory_manager.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/serial_port.hpp"
#include "os/kernel/user_elf.hpp"
#include "os/kernel/user_memory.hpp"
#include "os/kernel/user_runtime.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_SIZE_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_SYSTEM_CALL_ADDRESS_PROBE_SIZE_BYTES = 1ULL;
constexpr int64_t OS_KERNEL_SYSTEM_CALL_EMPTY_WRITE_RESULT = 0LL;

[[nodiscard]] bool ValidateUserSystemCallFrame(const ExceptionFrame &frame) noexcept {
    if (!IsUserExecutionActive() || !FrameOriginatedFromUser(frame) ||
        frame.vector != os::abi::OS_ABI_SYSTEM_CALL_VECTOR ||
        frame.codeSegment != static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR)) {
        return false;
    }
    const UserPrivilegeFrame &userFrame = AsUserPrivilegeFrame(frame);
    if (userFrame.userStackSegment !=
            static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR) ||
        !IsUserVirtualAddressRange(frame.instructionPointer,
                                   OS_KERNEL_SYSTEM_CALL_ADDRESS_PROBE_SIZE_BYTES) ||
        userFrame.userStackPointer < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
        userFrame.userStackPointer >= OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return false;
    }
    PageMapping stackMapping{};
    return QueryKernelPage(userFrame.userStackPointer, stackMapping) ==
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
    for (uint64_t byteIndex = 0ULL; byteIndex < lengthBytes; ++byteIndex) {
        if (!serialPort.TryWriteByte(static_cast<char>(message[byteIndex]))) {
            return os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE;
        }
    }
    return static_cast<int64_t>(lengthBytes);
}

}

extern "C" void osKernelDispatchSystemCall(ExceptionFrame *frame) noexcept {
    if (frame == nullptr || !ValidateUserSystemCallFrame(*frame)) {
        HaltProcessor();
    }
    RecordUserSystemCall();

    const uint64_t systemCallNumber = frame->registerRax;
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::WriteLog)) {
        frame->registerRax =
            static_cast<uint64_t>(DispatchWriteLog(frame->registerRdi, frame->registerRsi));
        return;
    }
    if (systemCallNumber == static_cast<uint64_t>(os::abi::SystemCallNumber::ExitProcess)) {
        TerminateUserExecutionFromExit(static_cast<int64_t>(frame->registerRdi));
    }
    frame->registerRax = static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_UNKNOWN_NUMBER);
}

}
