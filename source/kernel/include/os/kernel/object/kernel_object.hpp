#pragma once

#include "os/kernel/memory/kernel_heap.hpp"
#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

class FileDescriptionManager;
class FileTable;
class KernelObjectManager;
struct KernelObjectStorage;

enum class KernelObjectType : uint64_t {
    None,
    FileDescription,
};

enum class KernelObjectStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    NotInitialized,
    InvalidDependency,
    InvalidObjectType,
    EmptyPayload,
    SizeOverflow,
    AllocationFailed,
    GenerationOverflow,
    InvalidReference,
    ReferenceUnavailable,
    ReferenceOverflow,
    FinalizationFailed,
    ReleaseFailed,
    CorruptedState,
};

struct KernelObjectIdentity final {
    KernelObjectType type;
    uint64_t variant;
    uint64_t generation;
    uint64_t strong_reference_count;
};

struct KernelObjectReleaseResult final {
    KernelObjectType type;
    uint64_t variant;
    uint64_t generation;
    bool released_last_reference;
    bool finalization_succeeded;
};

struct KernelObjectManagerStatistics final {
    uint64_t active_object_count;
    uint64_t active_file_description_count;
    uint64_t active_strong_reference_count;
    uint64_t successful_creation_count;
    uint64_t destruction_count;
    uint64_t successful_reference_acquisition_count;
    uint64_t reference_release_count;
    uint64_t failed_reference_acquisition_count;
    uint64_t failed_finalization_count;
    uint64_t peak_active_object_count;
    uint64_t peak_strong_reference_count;
    uint64_t next_generation;
};

// Reference 是模块执行期间持有的临时强引用。它不可复制，并在析构或 Reset
// 时释放；因此跨模块接口不需要暴露对象存储地址。
class KernelObjectReference final {
  public:
    KernelObjectReference() noexcept;
    ~KernelObjectReference() noexcept;

    KernelObjectReference(const KernelObjectReference &) = delete;
    KernelObjectReference &operator=(const KernelObjectReference &) = delete;
    KernelObjectReference(KernelObjectReference &&other) noexcept;
    KernelObjectReference &operator=(KernelObjectReference &&other) = delete;

    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] KernelObjectStatus ReadIdentity(KernelObjectIdentity &identity) const noexcept;
    [[nodiscard]] KernelObjectStatus Reset() noexcept;

  private:
    friend class FileDescriptionManager;
    friend class FileTable;
    friend class KernelObjectManager;

    KernelObjectManager *manager_;
    KernelObjectStorage *storage_;
    uint64_t generation_;
};

// Handle 只允许 FileTable 等所有权容器持有。generation 与存储地址共同校验，
// 即使堆复用了同一地址，旧 handle 也不能取得新对象。
class KernelObjectHandle final {
  public:
    KernelObjectHandle() noexcept = default;
    [[nodiscard]] bool IsEmpty() const noexcept;

  private:
    friend class FileTable;
    friend class KernelObjectManager;

    KernelObjectStorage *storage_;
    uint64_t generation_;
};

using KernelObjectFinalizeOperation = bool (*)(void *payload, void *context) noexcept;

class KernelObjectManager final {
  public:
    KernelObjectManager() noexcept = default;
    KernelObjectManager(const KernelObjectManager &) = delete;
    KernelObjectManager &operator=(const KernelObjectManager &) = delete;

    [[nodiscard]] KernelObjectStatus Initialize(KernelHeap &heap) noexcept;
    [[nodiscard]] KernelObjectStatus Validate() noexcept;
    [[nodiscard]] KernelObjectManagerStatistics Statistics() noexcept;

  private:
    friend class FileDescriptionManager;
    friend class FileTable;
    friend class KernelObjectReference;

    [[nodiscard]] KernelObjectStatus CreateObject(KernelObjectType type, uint64_t variant,
                                                  uint64_t payload_size_bytes,
                                                  KernelObjectFinalizeOperation finalize_operation,
                                                  void *finalize_context,
                                                  KernelObjectReference &reference) noexcept;
    [[nodiscard]] KernelObjectStatus TryGetPayload(const KernelObjectReference &reference,
                                                   KernelObjectType expected_type, void *&payload,
                                                   SpinLock *&operation_lock) noexcept;
    [[nodiscard]] KernelObjectStatus AcquireHandle(const KernelObjectHandle &handle,
                                                   KernelObjectReference &reference) noexcept;
    [[nodiscard]] KernelObjectStatus DetachReference(KernelObjectReference &reference,
                                                     KernelObjectHandle &handle) noexcept;
    [[nodiscard]] KernelObjectStatus ReleaseHandle(KernelObjectHandle &handle,
                                                   KernelObjectReleaseResult &result) noexcept;
    [[nodiscard]] bool ValidateHandle(const KernelObjectHandle &handle) noexcept;
    [[nodiscard]] KernelObjectStatus ReadIdentity(const KernelObjectReference &reference,
                                                  KernelObjectIdentity &identity) noexcept;
    [[nodiscard]] KernelObjectStatus ReleaseReference(KernelObjectReference &reference,
                                                      KernelObjectReleaseResult &result) noexcept;
    [[nodiscard]] KernelObjectStatus ReleaseStorage(KernelObjectStorage *storage,
                                                    uint64_t generation,
                                                    KernelObjectReleaseResult &result) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool IsActiveStorage(const KernelObjectStorage *storage,
                                       uint64_t generation) const noexcept;
    void InsertActiveStorage(KernelObjectStorage &storage) noexcept;
    void RemoveActiveStorage(KernelObjectStorage &storage) noexcept;

    KernelHeap *heap_;
    // 活动链只包含至少有一个强引用的对象；最后引用释放前先从链中摘除。
    KernelObjectStorage *active_head_;
    SpinLock lock_;
    KernelObjectManagerStatistics statistics_;
    bool initialized_;
};

}
