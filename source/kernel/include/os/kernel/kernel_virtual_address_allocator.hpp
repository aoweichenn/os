#pragma once

#include "os/kernel/physical_frame_allocator.hpp"

#include <stdint.h>

namespace os::kernel {

enum class KernelVirtualAddressRangeKind : uint64_t {
    Unused,
    Allocation,
    Reservation,
};

struct KernelVirtualAddressRange final {
    uint64_t begin_address;
    uint64_t page_count;
};

// 描述符存储由调用方长期持有。分配器只使用前 active_descriptor_count 个元素，
// 并始终按 begin_address 递增排列；其余元素保持 Unused。
struct KernelVirtualAddressRangeDescriptor final {
    uint64_t begin_address;
    uint64_t page_count;
    KernelVirtualAddressRangeKind kind;
};

struct KernelVirtualAddressAllocatorStatistics final {
    uint64_t window_begin_address;
    uint64_t window_size_bytes;
    uint64_t capacity_page_count;
    uint64_t free_page_count;
    uint64_t allocated_page_count;
    uint64_t reserved_page_count;
    uint64_t descriptor_capacity;
    uint64_t active_descriptor_count;
    uint64_t active_allocation_count;
    uint64_t reservation_count;
    uint64_t successful_allocation_count;
    uint64_t release_count;
    uint64_t peak_allocated_page_count;
    uint64_t peak_active_allocation_count;
    uint64_t largest_free_range_page_count;
};

enum class KernelVirtualAddressAllocatorStatus : uint64_t {
    Succeeded,
    NullDescriptorStorage,
    EmptyDescriptorCapacity,
    AlreadyInitialized,
    NotInitialized,
    InvalidWindow,
    InvalidPageCount,
    InvalidAlignment,
    InvalidRange,
    RangeOverlap,
    MetadataExhausted,
    OutOfVirtualAddressSpace,
    ReservedRange,
    AllocationNotFound,
    AllocationSizeMismatch,
    CounterOverflow,
    CorruptedState,
};

class KernelVirtualAddressAllocator final {
  public:
    KernelVirtualAddressAllocator() noexcept;
    KernelVirtualAddressAllocator(const KernelVirtualAddressAllocator &) = delete;
    KernelVirtualAddressAllocator &operator=(const KernelVirtualAddressAllocator &) = delete;

    // 窗口和描述符存储均由调用方拥有；本对象只记录所有权区间，不创建页表映射。
    [[nodiscard]] KernelVirtualAddressAllocatorStatus
    Initialize(uint64_t window_begin_address, uint64_t window_page_count,
               KernelVirtualAddressRangeDescriptor *descriptor_storage,
               uint64_t descriptor_capacity) noexcept;
    [[nodiscard]] KernelVirtualAddressAllocatorStatus ReserveRange(uint64_t begin_address,
                                                                   uint64_t page_count) noexcept;
    [[nodiscard]] KernelVirtualAddressAllocatorStatus
    TryAllocate(uint64_t page_count, uint64_t alignment_page_count,
                KernelVirtualAddressRange &range) noexcept;
    [[nodiscard]] KernelVirtualAddressAllocatorStatus
    TryRelease(KernelVirtualAddressRange range) noexcept;
    [[nodiscard]] KernelVirtualAddressAllocatorStatus Validate() const noexcept;
    [[nodiscard]] KernelVirtualAddressAllocatorStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsRangeInsideWindow(uint64_t begin_address,
                                           uint64_t page_count) const noexcept;
    [[nodiscard]] uint64_t WindowEndAddress() const noexcept;
    [[nodiscard]] uint64_t
    RangeEndAddress(const KernelVirtualAddressRangeDescriptor &descriptor) const noexcept;
    [[nodiscard]] uint64_t FindInsertionIndex(uint64_t begin_address) const noexcept;
    [[nodiscard]] uint64_t CalculateLargestFreeRangePageCount() const noexcept;
    void InsertDescriptor(uint64_t descriptor_index,
                          KernelVirtualAddressRangeDescriptor descriptor) noexcept;
    void RemoveDescriptor(uint64_t descriptor_index) noexcept;

    uint64_t window_begin_address_;
    uint64_t window_page_count_;
    KernelVirtualAddressRangeDescriptor *descriptors_;
    uint64_t descriptor_capacity_;
    uint64_t active_descriptor_count_;
    uint64_t allocated_page_count_;
    uint64_t reserved_page_count_;
    uint64_t active_allocation_count_;
    uint64_t reservation_count_;
    uint64_t successful_allocation_count_;
    uint64_t release_count_;
    uint64_t peak_allocated_page_count_;
    uint64_t peak_active_allocation_count_;
};

}
