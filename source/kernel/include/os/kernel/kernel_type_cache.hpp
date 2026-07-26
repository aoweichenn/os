#pragma once

#include "os/kernel/kernel_heap.hpp"

#include <stdint.h>

namespace os::kernel {

enum class KernelTypeCacheStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    NotInitialized,
    EmptyObjectSize,
    InvalidObjectAlignment,
    EmptyCapacity,
    SizeOverflow,
    BackingAllocationFailed,
    OutOfObjects,
    NullObject,
    InvalidObject,
    ObjectNotActive,
    ActiveObjectsRemain,
    BackingReleaseFailed,
    CounterOverflow,
    CorruptedState,
};

struct KernelTypeCacheStatistics final {
    uint64_t object_size_bytes;
    uint64_t object_alignment_bytes;
    uint64_t slot_stride_bytes;
    uint64_t capacity;
    uint64_t active_object_count;
    uint64_t free_object_count;
    uint64_t successful_allocation_count;
    uint64_t release_count;
    uint64_t peak_active_object_count;
    uint64_t backing_storage_size_bytes;
};

class KernelFixedObjectCache final {
  public:
    KernelFixedObjectCache() noexcept;
    KernelFixedObjectCache(const KernelFixedObjectCache &) = delete;
    KernelFixedObjectCache &operator=(const KernelFixedObjectCache &) = delete;

    // 缓存一次性从通用堆取得位图和全部槽位；槽位生命周期中不再调用堆。
    [[nodiscard]] KernelTypeCacheStatus Initialize(KernelHeap &heap, uint64_t object_size_bytes,
                                                   uint64_t object_alignment_bytes,
                                                   uint64_t capacity) noexcept;
    [[nodiscard]] KernelTypeCacheStatus TryAcquire(void *&object_storage) noexcept;
    [[nodiscard]] KernelTypeCacheStatus TryRelease(void *object_storage) noexcept;
    [[nodiscard]] KernelTypeCacheStatus Destroy() noexcept;
    [[nodiscard]] KernelTypeCacheStatus Validate() const noexcept;
    [[nodiscard]] KernelTypeCacheStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] uint64_t ObjectAddress(uint64_t object_index) const noexcept;
    [[nodiscard]] bool IsObjectAllocated(uint64_t object_index) const noexcept;
    void SetObjectAllocated(uint64_t object_index, bool allocated) noexcept;
    [[nodiscard]] uint64_t ReadFreeNextIndex(uint64_t object_index) const noexcept;
    void WriteFreeNextIndex(uint64_t object_index, uint64_t next_object_index) noexcept;
    void ResetState() noexcept;

    KernelHeap *heap_;
    void *backing_allocation_;
    uint8_t *allocation_bitmap_;
    uint64_t object_storage_base_address_;
    uint64_t object_size_bytes_;
    uint64_t object_alignment_bytes_;
    uint64_t storage_alignment_bytes_;
    uint64_t slot_stride_bytes_;
    uint64_t capacity_;
    uint64_t allocation_bitmap_size_bytes_;
    uint64_t backing_storage_size_bytes_;
    uint64_t free_list_head_index_;
    uint64_t active_object_count_;
    uint64_t free_object_count_;
    uint64_t successful_allocation_count_;
    uint64_t release_count_;
    uint64_t peak_active_object_count_;
};

template <typename ObjectType> class KernelTypeCache final {
  public:
    KernelTypeCache() noexcept = default;
    KernelTypeCache(const KernelTypeCache &) = delete;
    KernelTypeCache &operator=(const KernelTypeCache &) = delete;

    // 接口只管理类型化存储，不隐式调用构造或析构；对象模型由具体调用方决定。
    [[nodiscard]] KernelTypeCacheStatus Initialize(KernelHeap &heap, uint64_t capacity) noexcept;
    [[nodiscard]] KernelTypeCacheStatus TryAcquire(ObjectType *&object_storage) noexcept;
    [[nodiscard]] KernelTypeCacheStatus TryRelease(ObjectType *object_storage) noexcept;
    [[nodiscard]] KernelTypeCacheStatus Destroy() noexcept;
    [[nodiscard]] KernelTypeCacheStatus Validate() const noexcept;
    [[nodiscard]] KernelTypeCacheStatistics Statistics() const noexcept;

  private:
    KernelFixedObjectCache cache_{};
};

}

#include "os/kernel/kernel_type_cache.tpp"
