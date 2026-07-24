#pragma once

#include "os/kernel/boot_info.hpp"
#include "os/kernel/kernel_heap.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE = 0xFFFF800000000000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SIZE_BYTES = 64ULL * 1024ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS =
    0xFFFF800000100000ULL;

struct KernelMemoryStatistics final {
    uint64_t memoryMapEntryCount;
    uint64_t describedAddressBytes;
    uint64_t reportedUsableMemoryBytes;
    uint64_t managedUsableMemoryBytes;
    uint64_t freeFrameCount;
    uint64_t allocatedFrameCount;
    uint64_t reservedFrameCount;
    uint64_t pageTableRootPhysicalAddress;
    uint64_t heapCapacityBytes;
};

enum class KernelMemoryInitializationStatus : uint64_t {
    Succeeded,
    InvalidMemoryMap,
    FrameAllocatorInitializationFailed,
    ReservationFailed,
    PageTableInitializationFailed,
    IdentityMappingFailed,
    HeapMappingFailed,
    ProtectionTestMappingFailed,
    MemoryProtectionUnsupported,
    PageTableActivationFailed,
    PermissionValidationFailed,
    HeapInitializationFailed,
    HeapSelfTestFailed,
};

[[nodiscard]] KernelMemoryInitializationStatus
initializeKernelMemory(const BootInfo &bootInfo) noexcept;
[[nodiscard]] const KernelMemoryStatistics &kernelMemoryStatistics() noexcept;
[[nodiscard]] KernelHeap &kernelHeap() noexcept;

}
