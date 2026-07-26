#include "os/kernel/object/kernel_object.hpp"

#include "os/foundation/reference_counter.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_OBJECT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_OBJECT_INITIAL_REFERENCE_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_OBJECT_STORAGE_ALIGNMENT_BYTES = 64ULL;

enum class KernelObjectState : uint64_t {
    Inactive,
    Active,
    Finalizing,
};

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

}

struct alignas(OS_KERNEL_OBJECT_STORAGE_ALIGNMENT_BYTES) KernelObjectStorage final {
    KernelObjectStorage *previous;
    KernelObjectStorage *next;
    KernelObjectType type;
    KernelObjectState state;
    uint64_t variant;
    uint64_t generation;
    uint64_t strong_reference_count;
    uint64_t allocation_size_bytes;
    uint64_t payload_size_bytes;
    KernelObjectFinalizeOperation finalize_operation;
    void *finalize_context;
    SpinLock operation_lock;
};

KernelObjectReference::KernelObjectReference() noexcept
    : manager_{nullptr}, storage_{nullptr}, generation_{OS_KERNEL_OBJECT_EMPTY_VALUE} {}

KernelObjectReference::~KernelObjectReference() noexcept {
    if (!this->IsActive()) {
        return;
    }
    KernelObjectReleaseResult result{};
    static_cast<void>(this->manager_->ReleaseReference(*this, result));
}

KernelObjectReference::KernelObjectReference(KernelObjectReference &&other) noexcept
    : manager_{other.manager_}, storage_{other.storage_}, generation_{other.generation_} {
    other.manager_ = nullptr;
    other.storage_ = nullptr;
    other.generation_ = OS_KERNEL_OBJECT_EMPTY_VALUE;
}

bool KernelObjectReference::IsActive() const noexcept {
    return this->manager_ != nullptr && this->storage_ != nullptr &&
           this->generation_ != OS_KERNEL_OBJECT_EMPTY_VALUE;
}

KernelObjectStatus
KernelObjectReference::ReadIdentity(KernelObjectIdentity &identity) const noexcept {
    identity = KernelObjectIdentity{};
    if (!this->IsActive()) {
        return KernelObjectStatus::InvalidReference;
    }
    return this->manager_->ReadIdentity(*this, identity);
}

KernelObjectStatus KernelObjectReference::Reset() noexcept {
    KernelObjectReleaseResult result{};
    return this->manager_ == nullptr ? KernelObjectStatus::InvalidReference
                                     : this->manager_->ReleaseReference(*this, result);
}

bool KernelObjectHandle::IsEmpty() const noexcept {
    return this->storage_ == nullptr || this->generation_ == OS_KERNEL_OBJECT_EMPTY_VALUE;
}

KernelObjectStatus KernelObjectManager::Initialize(KernelHeap &heap) noexcept {
    if (this->initialized_) {
        return KernelObjectStatus::AlreadyInitialized;
    }
    if (heap.Validate() != KernelHeapStatus::Succeeded) {
        return KernelObjectStatus::InvalidDependency;
    }
    this->heap_ = &heap;
    this->active_head_ = nullptr;
    this->lock_ = SpinLock{};
    this->statistics_ = KernelObjectManagerStatistics{
        .active_object_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .active_file_description_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .active_strong_reference_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .successful_creation_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .destruction_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .successful_reference_acquisition_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .reference_release_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .failed_reference_acquisition_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .failed_finalization_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .peak_active_object_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .peak_strong_reference_count = OS_KERNEL_OBJECT_EMPTY_VALUE,
        .next_generation = OS_KERNEL_OBJECT_INITIAL_REFERENCE_COUNT,
    };
    this->initialized_ = true;
    return KernelObjectStatus::Succeeded;
}

KernelObjectStatus KernelObjectManager::CreateObject(
    const KernelObjectType type, const uint64_t variant, const uint64_t payload_size_bytes,
    const KernelObjectFinalizeOperation finalize_operation, void *const finalize_context,
    KernelObjectReference &reference) noexcept {
    if (!this->IsInitialized()) {
        return KernelObjectStatus::NotInitialized;
    }
    if (reference.IsActive()) {
        return KernelObjectStatus::InvalidReference;
    }
    if (type == KernelObjectType::None) {
        return KernelObjectStatus::InvalidObjectType;
    }
    if (payload_size_bytes == OS_KERNEL_OBJECT_EMPTY_VALUE) {
        return KernelObjectStatus::EmptyPayload;
    }
    if (payload_size_bytes > UINT64_MAX - sizeof(KernelObjectStorage)) {
        return KernelObjectStatus::SizeOverflow;
    }
    const uint64_t allocation_size_bytes = sizeof(KernelObjectStorage) + payload_size_bytes;
    void *allocation = nullptr;
    if (this->heap_->TryAllocate(allocation_size_bytes, OS_KERNEL_OBJECT_STORAGE_ALIGNMENT_BYTES,
                                 allocation) != KernelHeapStatus::Succeeded) {
        return KernelObjectStatus::AllocationFailed;
    }

    KernelObjectStorage *const storage = static_cast<KernelObjectStorage *>(allocation);
    storage->previous = nullptr;
    storage->next = nullptr;
    storage->type = type;
    storage->state = KernelObjectState::Inactive;
    storage->variant = variant;
    storage->generation = OS_KERNEL_OBJECT_EMPTY_VALUE;
    storage->strong_reference_count = OS_KERNEL_OBJECT_EMPTY_VALUE;
    storage->allocation_size_bytes = allocation_size_bytes;
    storage->payload_size_bytes = payload_size_bytes;
    storage->finalize_operation = finalize_operation;
    storage->finalize_context = finalize_context;
    storage->operation_lock = SpinLock{};
    uint8_t *const payload_bytes =
        reinterpret_cast<uint8_t *>(storage) + sizeof(KernelObjectStorage);
    for (uint64_t byte_index = OS_KERNEL_OBJECT_EMPTY_VALUE; byte_index < payload_size_bytes;
         ++byte_index) {
        payload_bytes[byte_index] = 0U;
    }

    this->lock_.Lock();
    if (this->statistics_.next_generation == UINT64_MAX ||
        this->statistics_.active_object_count == UINT64_MAX ||
        this->statistics_.active_strong_reference_count == UINT64_MAX ||
        this->statistics_.successful_creation_count == UINT64_MAX) {
        this->lock_.Unlock();
        static_cast<void>(this->heap_->TryRelease(allocation));
        return this->statistics_.next_generation == UINT64_MAX
                   ? KernelObjectStatus::GenerationOverflow
                   : KernelObjectStatus::ReferenceOverflow;
    }
    storage->generation = this->statistics_.next_generation;
    ++this->statistics_.next_generation;
    if (os::foundation::StartReferenceCount(storage->strong_reference_count,
                                            OS_KERNEL_OBJECT_INITIAL_REFERENCE_COUNT) !=
        os::foundation::ReferenceCounterStatus::Succeeded) {
        this->lock_.Unlock();
        static_cast<void>(this->heap_->TryRelease(allocation));
        return KernelObjectStatus::CorruptedState;
    }
    storage->state = KernelObjectState::Active;
    this->InsertActiveStorage(*storage);
    ++this->statistics_.active_object_count;
    ++this->statistics_.active_strong_reference_count;
    ++this->statistics_.successful_creation_count;
    if (type == KernelObjectType::FileDescription) {
        ++this->statistics_.active_file_description_count;
    }
    this->statistics_.peak_active_object_count =
        Maximum(this->statistics_.peak_active_object_count, this->statistics_.active_object_count);
    this->statistics_.peak_strong_reference_count =
        Maximum(this->statistics_.peak_strong_reference_count,
                this->statistics_.active_strong_reference_count);
    reference.manager_ = this;
    reference.storage_ = storage;
    reference.generation_ = storage->generation;
    this->lock_.Unlock();
    return KernelObjectStatus::Succeeded;
}

KernelObjectStatus KernelObjectManager::TryGetPayload(const KernelObjectReference &reference,
                                                      const KernelObjectType expected_type,
                                                      void *&payload,
                                                      SpinLock *&operation_lock) noexcept {
    payload = nullptr;
    operation_lock = nullptr;
    if (!this->IsInitialized()) {
        return KernelObjectStatus::NotInitialized;
    }
    this->lock_.Lock();
    if (reference.manager_ != this ||
        !this->IsActiveStorage(reference.storage_, reference.generation_) ||
        reference.storage_->type != expected_type) {
        this->lock_.Unlock();
        return KernelObjectStatus::InvalidReference;
    }
    payload = reinterpret_cast<uint8_t *>(reference.storage_) + sizeof(KernelObjectStorage);
    operation_lock = &reference.storage_->operation_lock;
    this->lock_.Unlock();
    return KernelObjectStatus::Succeeded;
}

KernelObjectStatus KernelObjectManager::AcquireHandle(const KernelObjectHandle &handle,
                                                      KernelObjectReference &reference) noexcept {
    if (!this->IsInitialized()) {
        return KernelObjectStatus::NotInitialized;
    }
    if (reference.IsActive()) {
        return KernelObjectStatus::InvalidReference;
    }
    this->lock_.Lock();
    if (!this->IsActiveStorage(handle.storage_, handle.generation_)) {
        ++this->statistics_.failed_reference_acquisition_count;
        this->lock_.Unlock();
        return KernelObjectStatus::ReferenceUnavailable;
    }
    if (this->statistics_.active_strong_reference_count == UINT64_MAX ||
        os::foundation::TryAcquireReference(handle.storage_->strong_reference_count) !=
            os::foundation::ReferenceCounterStatus::Succeeded) {
        ++this->statistics_.failed_reference_acquisition_count;
        this->lock_.Unlock();
        return KernelObjectStatus::ReferenceOverflow;
    }
    ++this->statistics_.active_strong_reference_count;
    ++this->statistics_.successful_reference_acquisition_count;
    this->statistics_.peak_strong_reference_count =
        Maximum(this->statistics_.peak_strong_reference_count,
                this->statistics_.active_strong_reference_count);
    reference.manager_ = this;
    reference.storage_ = handle.storage_;
    reference.generation_ = handle.generation_;
    this->lock_.Unlock();
    return KernelObjectStatus::Succeeded;
}

KernelObjectStatus KernelObjectManager::DetachReference(KernelObjectReference &reference,
                                                        KernelObjectHandle &handle) noexcept {
    handle = KernelObjectHandle{};
    if (!this->IsInitialized()) {
        return KernelObjectStatus::NotInitialized;
    }
    this->lock_.Lock();
    if (reference.manager_ != this ||
        !this->IsActiveStorage(reference.storage_, reference.generation_)) {
        this->lock_.Unlock();
        return KernelObjectStatus::InvalidReference;
    }
    handle.storage_ = reference.storage_;
    handle.generation_ = reference.generation_;
    reference.manager_ = nullptr;
    reference.storage_ = nullptr;
    reference.generation_ = OS_KERNEL_OBJECT_EMPTY_VALUE;
    this->lock_.Unlock();
    return KernelObjectStatus::Succeeded;
}

KernelObjectStatus KernelObjectManager::ReleaseHandle(KernelObjectHandle &handle,
                                                      KernelObjectReleaseResult &result) noexcept {
    result = KernelObjectReleaseResult{};
    if (handle.IsEmpty()) {
        return KernelObjectStatus::InvalidReference;
    }
    KernelObjectStorage *const storage = handle.storage_;
    const uint64_t generation = handle.generation_;
    handle = KernelObjectHandle{};
    return this->ReleaseStorage(storage, generation, result);
}

bool KernelObjectManager::ValidateHandle(const KernelObjectHandle &handle) noexcept {
    if (!this->IsInitialized() || handle.IsEmpty()) {
        return false;
    }
    this->lock_.Lock();
    const bool valid = this->IsActiveStorage(handle.storage_, handle.generation_);
    this->lock_.Unlock();
    return valid;
}

KernelObjectStatus KernelObjectManager::ReadIdentity(const KernelObjectReference &reference,
                                                     KernelObjectIdentity &identity) noexcept {
    identity = KernelObjectIdentity{};
    if (!this->IsInitialized()) {
        return KernelObjectStatus::NotInitialized;
    }
    this->lock_.Lock();
    if (reference.manager_ != this ||
        !this->IsActiveStorage(reference.storage_, reference.generation_)) {
        this->lock_.Unlock();
        return KernelObjectStatus::InvalidReference;
    }
    identity = KernelObjectIdentity{
        .type = reference.storage_->type,
        .variant = reference.storage_->variant,
        .generation = reference.storage_->generation,
        .strong_reference_count = reference.storage_->strong_reference_count,
    };
    this->lock_.Unlock();
    return KernelObjectStatus::Succeeded;
}

KernelObjectStatus
KernelObjectManager::ReleaseReference(KernelObjectReference &reference,
                                      KernelObjectReleaseResult &result) noexcept {
    result = KernelObjectReleaseResult{};
    if (reference.manager_ != this || !reference.IsActive()) {
        return KernelObjectStatus::InvalidReference;
    }
    KernelObjectStorage *const storage = reference.storage_;
    const uint64_t generation = reference.generation_;
    reference.manager_ = nullptr;
    reference.storage_ = nullptr;
    reference.generation_ = OS_KERNEL_OBJECT_EMPTY_VALUE;
    return this->ReleaseStorage(storage, generation, result);
}

KernelObjectStatus KernelObjectManager::ReleaseStorage(KernelObjectStorage *const storage,
                                                       const uint64_t generation,
                                                       KernelObjectReleaseResult &result) noexcept {
    result = KernelObjectReleaseResult{};
    if (!this->IsInitialized()) {
        return KernelObjectStatus::NotInitialized;
    }
    this->lock_.Lock();
    if (!this->IsActiveStorage(storage, generation)) {
        this->lock_.Unlock();
        return KernelObjectStatus::ReferenceUnavailable;
    }
    if (this->statistics_.active_strong_reference_count == OS_KERNEL_OBJECT_EMPTY_VALUE) {
        this->lock_.Unlock();
        return KernelObjectStatus::CorruptedState;
    }
    bool released_last_reference = false;
    if (os::foundation::TryReleaseReference(storage->strong_reference_count,
                                            released_last_reference) !=
        os::foundation::ReferenceCounterStatus::Succeeded) {
        this->lock_.Unlock();
        return KernelObjectStatus::CorruptedState;
    }
    --this->statistics_.active_strong_reference_count;
    ++this->statistics_.reference_release_count;
    result = KernelObjectReleaseResult{
        .type = storage->type,
        .variant = storage->variant,
        .generation = storage->generation,
        .released_last_reference = released_last_reference,
        .finalization_succeeded = true,
    };
    if (!released_last_reference) {
        this->lock_.Unlock();
        return KernelObjectStatus::Succeeded;
    }

    // 从活动集合摘除后才在管理器锁外执行模块 finalizer，避免锁顺序反转。
    // 最后一个强引用已经归零，此时不会再有合法操作访问 payload。
    storage->state = KernelObjectState::Finalizing;
    this->RemoveActiveStorage(*storage);
    --this->statistics_.active_object_count;
    if (storage->type == KernelObjectType::FileDescription) {
        --this->statistics_.active_file_description_count;
    }
    this->lock_.Unlock();

    void *const payload = reinterpret_cast<uint8_t *>(storage) + sizeof(KernelObjectStorage);
    const bool finalized = storage->finalize_operation == nullptr ||
                           storage->finalize_operation(payload, storage->finalize_context);
    result.finalization_succeeded = finalized;
    const KernelHeapStatus release_status = this->heap_->TryRelease(storage);

    this->lock_.Lock();
    ++this->statistics_.destruction_count;
    if (!finalized) {
        ++this->statistics_.failed_finalization_count;
    }
    this->lock_.Unlock();
    if (release_status != KernelHeapStatus::Succeeded) {
        return KernelObjectStatus::ReleaseFailed;
    }
    return finalized ? KernelObjectStatus::Succeeded : KernelObjectStatus::FinalizationFailed;
}

KernelObjectStatus KernelObjectManager::Validate() noexcept {
    if (!this->IsInitialized()) {
        return KernelObjectStatus::NotInitialized;
    }
    this->lock_.Lock();
    uint64_t observed_object_count = OS_KERNEL_OBJECT_EMPTY_VALUE;
    uint64_t observed_file_description_count = OS_KERNEL_OBJECT_EMPTY_VALUE;
    uint64_t observed_reference_count = OS_KERNEL_OBJECT_EMPTY_VALUE;
    KernelObjectStorage *previous = nullptr;
    // 重新遍历活动链，而不是只相信累计统计，能同时发现断链和引用计数漂移。
    for (KernelObjectStorage *storage = this->active_head_; storage != nullptr;
         storage = storage->next) {
        if (storage->previous != previous || storage->state != KernelObjectState::Active ||
            storage->type == KernelObjectType::None ||
            storage->generation == OS_KERNEL_OBJECT_EMPTY_VALUE ||
            !os::foundation::IsReferenceCountActive(storage->strong_reference_count) ||
            observed_reference_count > UINT64_MAX - storage->strong_reference_count) {
            this->lock_.Unlock();
            return KernelObjectStatus::CorruptedState;
        }
        ++observed_object_count;
        observed_reference_count += storage->strong_reference_count;
        if (storage->type == KernelObjectType::FileDescription) {
            ++observed_file_description_count;
        }
        previous = storage;
    }
    const bool valid =
        observed_object_count == this->statistics_.active_object_count &&
        observed_file_description_count == this->statistics_.active_file_description_count &&
        observed_reference_count == this->statistics_.active_strong_reference_count &&
        this->statistics_.peak_active_object_count >= this->statistics_.active_object_count &&
        this->statistics_.peak_strong_reference_count >=
            this->statistics_.active_strong_reference_count;
    this->lock_.Unlock();
    return valid ? KernelObjectStatus::Succeeded : KernelObjectStatus::CorruptedState;
}

KernelObjectManagerStatistics KernelObjectManager::Statistics() noexcept {
    this->lock_.Lock();
    const KernelObjectManagerStatistics statistics = this->statistics_;
    this->lock_.Unlock();
    return statistics;
}

bool KernelObjectManager::IsInitialized() const noexcept {
    return this->initialized_ && this->heap_ != nullptr;
}

bool KernelObjectManager::IsActiveStorage(const KernelObjectStorage *const storage,
                                          const uint64_t generation) const noexcept {
    if (storage == nullptr || generation == OS_KERNEL_OBJECT_EMPTY_VALUE) {
        return false;
    }
    // 先通过活动链证明地址归当前管理器所有，再解引用候选对象。这样旧 handle
    // 即使指向已经被 heap 复用的普通块，也不会把普通块内容解释成对象头。
    for (const KernelObjectStorage *candidate = this->active_head_; candidate != nullptr;
         candidate = candidate->next) {
        if (candidate != storage) {
            continue;
        }
        return candidate->state == KernelObjectState::Active && candidate->generation == generation;
    }
    return false;
}

void KernelObjectManager::InsertActiveStorage(KernelObjectStorage &storage) noexcept {
    storage.previous = nullptr;
    storage.next = this->active_head_;
    if (this->active_head_ != nullptr) {
        this->active_head_->previous = &storage;
    }
    this->active_head_ = &storage;
}

void KernelObjectManager::RemoveActiveStorage(KernelObjectStorage &storage) noexcept {
    if (storage.previous != nullptr) {
        storage.previous->next = storage.next;
    } else {
        this->active_head_ = storage.next;
    }
    if (storage.next != nullptr) {
        storage.next->previous = storage.previous;
    }
    storage.previous = nullptr;
    storage.next = nullptr;
}

}
