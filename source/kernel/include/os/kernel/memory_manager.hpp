#pragma once

#include "os/kernel/boot_info.hpp"
#include "os/kernel/kernel_heap.hpp"
#include "os/kernel/page_table.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE = 0xFFFF800000000000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SIZE_BYTES = 64ULL * 1024ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS =
    0xFFFF800000100000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE = 0xFFFF888000000000ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_DIRECT_MAP_CAPACITY_BYTES =
    64ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;

struct KernelMemoryStatistics final {
    uint64_t memoryMapEntryCount;
    uint64_t describedAddressBytes;
    uint64_t reportedUsableMemoryBytes;
    uint64_t managedUsableMemoryBytes;
    uint64_t managedPhysicalAddressLimit;
    uint64_t physicalAddressWidthBits;
    uint64_t virtualAddressWidthBits;
    uint64_t fiveLevelPagingSupported;
    uint64_t frameStateStoragePhysicalAddress;
    uint64_t frameStateStorageSizeBytes;
    uint64_t directMapMappedBytes;
    uint64_t directMapLargePageCount;
    uint64_t directMapSmallPageCount;
    uint64_t highMemoryTestPhysicalAddress;
    uint64_t freeFrameCount;
    uint64_t allocatedFrameCount;
    uint64_t reservedFrameCount;
    uint64_t pageTableRootPhysicalAddress;
    uint64_t heapCapacityBytes;
};

enum class KernelMemoryInitializationStatus : uint64_t {
    Succeeded,
    InvalidMemoryMap,
    InvalidProcessorAddressWidth,
    InvalidManagedPhysicalAddressLimit,
    FrameStateStorageUnavailable,
    FrameAllocatorConfigurationFailed,
    FrameAllocatorInitializationFailed,
    ReservationFailed,
    PageTableInitializationFailed,
    IdentityMappingFailed,
    DirectMapMappingFailed,
    HeapMappingFailed,
    ProtectionTestMappingFailed,
    MemoryProtectionUnsupported,
    PageTableActivationFailed,
    PageTableMemoryAccessFailed,
    PermissionValidationFailed,
    HeapInitializationFailed,
    HeapSelfTestFailed,
    HighMemorySelfTestFailed,
    LocalApicMappingFailed,
};

enum class KernelUserPageStatus : uint64_t {
    Succeeded,
    InvalidVirtualAddress,
    InvalidPermissions,
    InvalidPageTableRoot,
    PageTableCreationFailed,
    FrameAllocationFailed,
    PageMappingFailed,
    PageNotMapped,
    NotUserAccessible,
    PageUnmappingFailed,
    FrameReleaseFailed,
    PageTableDestructionFailed,
};

[[nodiscard]] KernelMemoryInitializationStatus
InitializeKernelMemory(const BootInfo &bootInfo) noexcept;
[[nodiscard]] const KernelMemoryStatistics &GetKernelMemoryStatistics() noexcept;
[[nodiscard]] PhysicalFrameAllocatorStatistics GetPhysicalFrameAllocatorStatistics() noexcept;
[[nodiscard]] uint64_t PhysicalMemoryDirectMapAddress(uint64_t physicalAddress) noexcept;
[[nodiscard]] KernelHeap &GetKernelHeap() noexcept;
[[nodiscard]] KernelUserPageStatus CreateUserPageTable(uint64_t &rootPhysicalAddress) noexcept;
[[nodiscard]] KernelUserPageStatus DestroyUserPageTable(uint64_t rootPhysicalAddress) noexcept;
[[nodiscard]] KernelUserPageStatus AllocateAndMapUserPage(uint64_t rootPhysicalAddress,
                                                          uint64_t virtualAddress, bool writable,
                                                          bool executable,
                                                          uint64_t &physicalAddress) noexcept;
[[nodiscard]] KernelUserPageStatus ReleaseUserPage(uint64_t rootPhysicalAddress,
                                                   uint64_t virtualAddress) noexcept;
[[nodiscard]] PageTableStatus QueryAddressSpacePage(uint64_t rootPhysicalAddress,
                                                    uint64_t virtualAddress,
                                                    PageMapping &mapping) noexcept;
[[nodiscard]] PageTableStatus QueryActivePage(uint64_t virtualAddress,
                                              PageMapping &mapping) noexcept;
[[nodiscard]] uint64_t GetKernelPageTableRoot() noexcept;
[[nodiscard]] bool ActivateUserPageTable(uint64_t rootPhysicalAddress) noexcept;
void ActivateKernelPageTable() noexcept;

}
