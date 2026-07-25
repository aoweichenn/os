#include "os/kernel/process_memory_layout.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_MEMORY_EMPTY_LENGTH_BYTES = 0ULL;

alignas(OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES) uint8_t
    process_kernel_stack_storage[OS_KERNEL_PROCESS_CAPACITY]
                                [OS_KERNEL_PROCESS_KERNEL_STACK_STORAGE_SIZE_BYTES];
}

uint64_t ProcessKernelStackGuardPageAddress(const uint64_t process_index) noexcept {
    if (process_index >= OS_KERNEL_PROCESS_CAPACITY) {
        return OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS;
    }
    return reinterpret_cast<uint64_t>(process_kernel_stack_storage[process_index]);
}

uint64_t ProcessKernelStackTopAddress(const uint64_t process_index) noexcept {
    const uint64_t guard_page_address = ProcessKernelStackGuardPageAddress(process_index);
    if (guard_page_address == OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS) {
        return OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS;
    }
    return guard_page_address + OS_KERNEL_PROCESS_KERNEL_STACK_STORAGE_SIZE_BYTES;
}

bool ProcessKernelStackContains(const uint64_t process_index, const uint64_t address,
                                const uint64_t length_bytes) noexcept {
    if (length_bytes == OS_KERNEL_PROCESS_MEMORY_EMPTY_LENGTH_BYTES ||
        address > UINT64_MAX - length_bytes) {
        return false;
    }
    const uint64_t guard_page_address = ProcessKernelStackGuardPageAddress(process_index);
    if (guard_page_address == OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS) {
        return false;
    }
    const uint64_t stack_begin_address =
        guard_page_address + OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES;
    const uint64_t stack_top_address = ProcessKernelStackTopAddress(process_index);
    return address >= stack_begin_address && address + length_bytes <= stack_top_address;
}
}
