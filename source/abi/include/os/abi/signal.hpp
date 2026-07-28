#pragma once

#include <stdint.h>

namespace os::abi {

inline constexpr uint64_t OS_ABI_SIGNAL_MINIMUM_NUMBER = 1ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_MAXIMUM_NUMBER = 63ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_INTERRUPT_NUMBER = 2ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_KILL_NUMBER = 9ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_CHILD_NUMBER = 17ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_CONTINUE_NUMBER = 18ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_STOP_NUMBER = 19ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER = 20ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_TERMINATE_NUMBER = 15ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_USER1_NUMBER = 10ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_USER2_NUMBER = 12ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_FRAME_MAGIC = 0x4F5353494746524DULL;
inline constexpr uint64_t OS_ABI_SIGNAL_FRAME_VERSION = 1ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_ACTION_RESTART_WAIT_FLAG = 1ULL << 0ULL;
inline constexpr uint64_t OS_ABI_SIGNAL_ACTION_VALID_FLAG_MASK =
    OS_ABI_SIGNAL_ACTION_RESTART_WAIT_FLAG;
inline constexpr uint64_t OS_ABI_SIGNAL_VALID_SET = UINT64_MAX >> 1ULL;

enum class SignalDisposition : uint64_t {
    Default = 0ULL,
    Ignore = 1ULL,
    Handler = 2ULL,
};

struct SignalAction final {
    SignalDisposition disposition;
    uint64_t handler_address;
    uint64_t restorer_address;
    uint64_t additional_mask;
    uint64_t flags;
};

inline constexpr uint64_t OS_ABI_SIGNAL_ACTION_SIZE_BYTES = 40ULL;
static_assert(sizeof(SignalAction) == OS_ABI_SIGNAL_ACTION_SIZE_BYTES);

// 该结构逐字段冻结 x86-64 用户返回现场，不依赖编译器私有的寄存器类型。
struct SignalUserContext final {
    uint64_t register_r15;
    uint64_t register_r14;
    uint64_t register_r13;
    uint64_t register_r12;
    uint64_t register_r11;
    uint64_t register_r10;
    uint64_t register_r9;
    uint64_t register_r8;
    uint64_t register_rdi;
    uint64_t register_rsi;
    uint64_t register_rbp;
    uint64_t register_rdx;
    uint64_t register_rcx;
    uint64_t register_rbx;
    uint64_t register_rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t flags;
    uint64_t stack_pointer;
    uint64_t stack_segment;
};

inline constexpr uint64_t OS_ABI_SIGNAL_USER_CONTEXT_SIZE_BYTES = 176ULL;
static_assert(sizeof(SignalUserContext) == OS_ABI_SIGNAL_USER_CONTEXT_SIZE_BYTES);

struct SignalFrame final {
    uint64_t magic;
    uint64_t version;
    uint64_t size_bytes;
    uint64_t cookie;
    uint64_t signal_number;
    uint64_t previous_mask;
    uint64_t restorer_address;
    uint64_t reserved;
    SignalUserContext context;
};

inline constexpr uint64_t OS_ABI_SIGNAL_FRAME_SIZE_BYTES = 240ULL;
static_assert(sizeof(SignalFrame) == OS_ABI_SIGNAL_FRAME_SIZE_BYTES);

[[nodiscard]] constexpr uint64_t SignalBit(const uint64_t signal_number) noexcept {
    return signal_number >= OS_ABI_SIGNAL_MINIMUM_NUMBER &&
                   signal_number <= OS_ABI_SIGNAL_MAXIMUM_NUMBER
               ? 1ULL << (signal_number - OS_ABI_SIGNAL_MINIMUM_NUMBER)
               : 0ULL;
}

inline constexpr uint64_t OS_ABI_SIGNAL_UNMASKABLE_SET =
    SignalBit(OS_ABI_SIGNAL_KILL_NUMBER) | SignalBit(OS_ABI_SIGNAL_STOP_NUMBER);

}
