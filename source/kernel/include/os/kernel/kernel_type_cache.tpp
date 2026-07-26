namespace os::kernel {

template <typename ObjectType>
KernelTypeCacheStatus KernelTypeCache<ObjectType>::Initialize(KernelHeap &heap,
                                                              const uint64_t capacity) noexcept {
    return this->cache_.Initialize(heap, sizeof(ObjectType), alignof(ObjectType), capacity);
}

template <typename ObjectType>
KernelTypeCacheStatus
KernelTypeCache<ObjectType>::TryAcquire(ObjectType *&object_storage) noexcept {
    void *untyped_storage = static_cast<void *>(object_storage);
    const KernelTypeCacheStatus status = this->cache_.TryAcquire(untyped_storage);
    if (status == KernelTypeCacheStatus::Succeeded) {
        object_storage = static_cast<ObjectType *>(untyped_storage);
    }
    return status;
}

template <typename ObjectType>
KernelTypeCacheStatus
KernelTypeCache<ObjectType>::TryRelease(ObjectType *const object_storage) noexcept {
    return this->cache_.TryRelease(static_cast<void *>(object_storage));
}

template <typename ObjectType>
KernelTypeCacheStatus KernelTypeCache<ObjectType>::Destroy() noexcept {
    return this->cache_.Destroy();
}

template <typename ObjectType>
KernelTypeCacheStatus KernelTypeCache<ObjectType>::Validate() const noexcept {
    return this->cache_.Validate();
}

template <typename ObjectType>
KernelTypeCacheStatistics KernelTypeCache<ObjectType>::Statistics() const noexcept {
    return this->cache_.Statistics();
}

}
