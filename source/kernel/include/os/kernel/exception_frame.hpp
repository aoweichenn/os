#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_EXCEPTION_FRAME_SIZE_BYTES = 160ULL;
inline constexpr uint64_t OS_KERNEL_EXCEPTION_ARCHITECTED_VECTOR_COUNT = 32ULL;

struct ExceptionFrame final {
    uint64_t registerR15;
    uint64_t registerR14;
    uint64_t registerR13;
    uint64_t registerR12;
    uint64_t registerR11;
    uint64_t registerR10;
    uint64_t registerR9;
    uint64_t registerR8;
    uint64_t registerRdi;
    uint64_t registerRsi;
    uint64_t registerRbp;
    uint64_t registerRdx;
    uint64_t registerRcx;
    uint64_t registerRbx;
    uint64_t registerRax;
    uint64_t vector;
    uint64_t errorCode;
    uint64_t instructionPointer;
    uint64_t codeSegment;
    uint64_t flags;
};

[[nodiscard]] bool exceptionPushesHardwareErrorCode(uint64_t vector) noexcept;
[[nodiscard]] bool isResumableKernelException(uint64_t vector) noexcept;

static_assert(sizeof(ExceptionFrame) == OS_KERNEL_EXCEPTION_FRAME_SIZE_BYTES);

}
