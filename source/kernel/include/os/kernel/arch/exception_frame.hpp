#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_EXCEPTION_FRAME_SIZE_BYTES = 160ULL;
inline constexpr uint64_t OS_KERNEL_EXCEPTION_ARCHITECTED_VECTOR_COUNT = 32ULL;

struct ExceptionFrame final {
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
};

[[nodiscard]] bool ExceptionPushesHardwareErrorCode(uint64_t vector) noexcept;
[[nodiscard]] bool IsResumableKernelException(uint64_t vector) noexcept;
[[nodiscard]] bool FrameOriginatedFromUser(const ExceptionFrame &frame) noexcept;

static_assert(sizeof(ExceptionFrame) == OS_KERNEL_EXCEPTION_FRAME_SIZE_BYTES);

}
