#pragma once

#include "os/kernel/memory/kernel_virtual_address_allocator.hpp"
#include "os/kernel/memory/page_table.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_STACK_LOWER_GUARD_PAGE_COUNT = 1ULL;
inline constexpr uint64_t OS_KERNEL_STACK_MAPPED_PAGE_COUNT = 4ULL;
inline constexpr uint64_t OS_KERNEL_STACK_UPPER_GUARD_PAGE_COUNT = 1ULL;
inline constexpr uint64_t OS_KERNEL_STACK_GUARD_PAGE_COUNT =
    OS_KERNEL_STACK_LOWER_GUARD_PAGE_COUNT + OS_KERNEL_STACK_UPPER_GUARD_PAGE_COUNT;
inline constexpr uint64_t OS_KERNEL_STACK_RANGE_PAGE_COUNT =
    OS_KERNEL_STACK_LOWER_GUARD_PAGE_COUNT + OS_KERNEL_STACK_MAPPED_PAGE_COUNT +
    OS_KERNEL_STACK_UPPER_GUARD_PAGE_COUNT;
inline constexpr uint64_t OS_KERNEL_STACK_SIZE_BYTES =
    OS_KERNEL_STACK_MAPPED_PAGE_COUNT * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_STACK_RANGE_SIZE_BYTES =
    OS_KERNEL_STACK_RANGE_PAGE_COUNT * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;

struct KernelStack final {
    KernelVirtualAddressRange virtual_range;
    PhysicalFrame physical_frames[OS_KERNEL_STACK_MAPPED_PAGE_COUNT];
    uint64_t mapped_page_count;
    bool active;
};

struct KernelStackMemoryAccess final {
    uint64_t physical_memory_virtual_base;
    uint64_t maximum_physical_address_exclusive;
};

struct KernelStackManagerStatistics final {
    uint64_t slot_capacity;
    uint64_t active_stack_count;
    uint64_t active_mapped_page_count;
    uint64_t active_guard_page_count;
    uint64_t successful_creation_count;
    uint64_t destruction_count;
    uint64_t peak_active_stack_count;
    uint64_t peak_active_mapped_page_count;
};

enum class KernelStackManagerStatus : uint64_t {
    Succeeded,
    NullStackStorage,
    EmptySlotCapacity,
    InvalidSlotCapacity,
    InvalidMemoryAccess,
    InvalidDependencyState,
    AlreadyInitialized,
    NotInitialized,
    InvalidSlotIndex,
    SlotAlreadyActive,
    SlotNotActive,
    VirtualAddressAllocationFailed,
    VirtualRangeNotClear,
    FrameAllocationFailed,
    PageMappingFailed,
    RollbackFailed,
    MappingValidationFailed,
    PageUnmappingFailed,
    FrameReleaseFailed,
    VirtualAddressReleaseFailed,
    CounterOverflow,
    CorruptedState,
};

[[nodiscard]] uint64_t KernelStackLowerGuardAddress(const KernelStack &stack) noexcept;
[[nodiscard]] uint64_t KernelStackMappedBeginAddress(const KernelStack &stack) noexcept;
[[nodiscard]] uint64_t KernelStackTopAddress(const KernelStack &stack) noexcept;
[[nodiscard]] uint64_t KernelStackUpperGuardAddress(const KernelStack &stack) noexcept;

class KernelStackManager final {
  public:
    KernelStackManager(PhysicalFrameAllocator &frame_allocator,
                       KernelVirtualAddressAllocator &virtual_address_allocator,
                       PageTableManager &page_table_manager,
                       KernelStackMemoryAccess memory_access) noexcept;
    KernelStackManager(const KernelStackManager &) = delete;
    KernelStackManager &operator=(const KernelStackManager &) = delete;

    // 存储由调用方长期持有。管理器只在安全栈上创建或销毁目标栈，不负责调度切换。
    [[nodiscard]] KernelStackManagerStatus Initialize(KernelStack *stack_storage,
                                                      uint64_t slot_capacity) noexcept;
    [[nodiscard]] KernelStackManagerStatus TryCreate(uint64_t slot_index) noexcept;
    [[nodiscard]] KernelStackManagerStatus TryDestroy(uint64_t slot_index) noexcept;
    [[nodiscard]] KernelStackManagerStatus Read(uint64_t slot_index,
                                                KernelStack &stack) const noexcept;
    [[nodiscard]] bool Contains(uint64_t slot_index, uint64_t address,
                                uint64_t length_bytes) const noexcept;
    [[nodiscard]] KernelStackManagerStatus Validate() const noexcept;
    [[nodiscard]] KernelStackManagerStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsMemoryAccessValid() const noexcept;
    [[nodiscard]] bool IsStackCleared(const KernelStack &stack) const noexcept;
    [[nodiscard]] bool IsPhysicalFrameAccessible(PhysicalFrame frame) const noexcept;
    [[nodiscard]] KernelStackManagerStatus ValidateStack(const KernelStack &stack) const noexcept;
    [[nodiscard]] uint64_t DataPageVirtualAddress(const KernelStack &stack,
                                                  uint64_t data_page_index) const noexcept;
    void ZeroPhysicalFrame(PhysicalFrame frame) const noexcept;

    PhysicalFrameAllocator *frame_allocator_;
    KernelVirtualAddressAllocator *virtual_address_allocator_;
    PageTableManager *page_table_manager_;
    KernelStackMemoryAccess memory_access_;
    KernelStack *stacks_;
    uint64_t slot_capacity_;
    uint64_t active_stack_count_;
    uint64_t active_mapped_page_count_;
    uint64_t successful_creation_count_;
    uint64_t destruction_count_;
    uint64_t peak_active_stack_count_;
    uint64_t peak_active_mapped_page_count_;
};

}
