#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT = 0ULL;

extern "C" [[nodiscard]] int64_t
osUserInvokeSystemCall(uint64_t systemCallNumber, uint64_t argument0, uint64_t argument1) noexcept;

}

int64_t InvokeSystemCall(const uint64_t systemCallNumber, const uint64_t argument0,
                         const uint64_t argument1) noexcept {
    return osUserInvokeSystemCall(systemCallNumber, argument0, argument1);
}

int64_t WriteLog(const char *message, const uint64_t messageSizeBytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WriteLog),
                            reinterpret_cast<uint64_t>(message), messageSizeBytes);
}

[[noreturn]] void ExitProcess(const int64_t exitCode) noexcept {
    static_cast<void>(
        InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ExitProcess),
                         static_cast<uint64_t>(exitCode), OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT));
    while (true) {
        asm volatile("ud2");
    }
}

}
