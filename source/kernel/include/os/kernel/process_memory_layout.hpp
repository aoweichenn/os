#pragma once

#include "os/kernel/physical_frame_allocator.hpp"
#include "os/kernel/process_scheduler.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_PAGE_COUNT = 1ULL;
inline constexpr uint64_t OS_KERNEL_PROCESS_KERNEL_STACK_PAGE_COUNT = 4ULL;
inline constexpr uint64_t OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES =
    OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_PAGE_COUNT *
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_PROCESS_KERNEL_STACK_SIZE_BYTES =
    OS_KERNEL_PROCESS_KERNEL_STACK_PAGE_COUNT * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_PROCESS_KERNEL_STACK_STORAGE_SIZE_BYTES =
    OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES +
    OS_KERNEL_PROCESS_KERNEL_STACK_SIZE_BYTES;

[[nodiscard]] uint64_t ProcessKernelStackGuardPageAddress(uint64_t processIndex) noexcept;
[[nodiscard]] uint64_t ProcessKernelStackTopAddress(uint64_t processIndex) noexcept;
[[nodiscard]] bool ProcessKernelStackContains(uint64_t processIndex, uint64_t address,
                                              uint64_t lengthBytes) noexcept;

}
