#include "os/kernel/process_memory_layout.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_MEMORY_EMPTY_LENGTH_BYTES = 0ULL;

alignas(OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES) uint8_t
    processKernelStackStorage[OS_KERNEL_PROCESS_CAPACITY]
                             [OS_KERNEL_PROCESS_KERNEL_STACK_STORAGE_SIZE_BYTES];

}

uint64_t ProcessKernelStackGuardPageAddress(const uint64_t processIndex) noexcept {
    if (processIndex >= OS_KERNEL_PROCESS_CAPACITY) {
        return OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS;
    }
    return reinterpret_cast<uint64_t>(processKernelStackStorage[processIndex]);
}

uint64_t ProcessKernelStackTopAddress(const uint64_t processIndex) noexcept {
    const uint64_t guardPageAddress = ProcessKernelStackGuardPageAddress(processIndex);
    if (guardPageAddress == OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS) {
        return OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS;
    }
    return guardPageAddress + OS_KERNEL_PROCESS_KERNEL_STACK_STORAGE_SIZE_BYTES;
}

bool ProcessKernelStackContains(const uint64_t processIndex, const uint64_t address,
                                const uint64_t lengthBytes) noexcept {
    if (lengthBytes == OS_KERNEL_PROCESS_MEMORY_EMPTY_LENGTH_BYTES ||
        address > UINT64_MAX - lengthBytes) {
        return false;
    }
    const uint64_t guardPageAddress = ProcessKernelStackGuardPageAddress(processIndex);
    if (guardPageAddress == OS_KERNEL_PROCESS_MEMORY_INVALID_ADDRESS) {
        return false;
    }
    const uint64_t stackBeginAddress =
        guardPageAddress + OS_KERNEL_PROCESS_KERNEL_STACK_GUARD_SIZE_BYTES;
    const uint64_t stackTopAddress = ProcessKernelStackTopAddress(processIndex);
    return address >= stackBeginAddress && address + lengthBytes <= stackTopAddress;
}

}
