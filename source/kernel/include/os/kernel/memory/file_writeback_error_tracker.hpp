#pragma once

#include <os/kernel/memory/file_cache_identity.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

enum class FileWritebackError : uint64_t {
    None,
    InputOutput,
};

enum class FileWritebackErrorTrackerStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidIdentity,
    InvalidError,
    NotFound,
    AllocationFailed,
    ReferenceOverflow,
    ReferenceUnderflow,
    SequenceOverflow,
    MetadataReleaseFailed,
    RecordsRemain,
    Corrupt,
};

struct FileWritebackErrorTrackerStatistics final {
    uint64_t active_record_count;
    uint64_t active_open_description_count;
    uint64_t peak_record_count;
    uint64_t peak_open_description_count;
    uint64_t registration_count;
    uint64_t unregistration_count;
    uint64_t recorded_error_count;
    uint64_t unobserved_error_count;
    uint64_t reported_error_count;
};

class FileWritebackErrorTracker final {
  public:
    FileWritebackErrorTracker() noexcept = default;
    FileWritebackErrorTracker(const FileWritebackErrorTracker &) = delete;
    FileWritebackErrorTracker &operator=(const FileWritebackErrorTracker &) = delete;

    [[nodiscard]] FileWritebackErrorTrackerStatus Initialize(KernelHeap &heap) noexcept;
    [[nodiscard]] FileWritebackErrorTrackerStatus Register(const FileCacheIdentity &identity,
                                                            uint64_t &sampled_sequence) noexcept;
    [[nodiscard]] FileWritebackErrorTrackerStatus
    Unregister(const FileCacheIdentity &identity) noexcept;
    [[nodiscard]] FileWritebackErrorTrackerStatus Record(const FileCacheIdentity &identity,
                                                          FileWritebackError error) noexcept;
    [[nodiscard]] FileWritebackErrorTrackerStatus
    Check(const FileCacheIdentity &identity, uint64_t sampled_sequence,
          uint64_t &current_sequence, FileWritebackError &error) noexcept;
    [[nodiscard]] FileWritebackErrorTrackerStatistics Statistics() const noexcept;
    [[nodiscard]] FileWritebackErrorTrackerStatus Validate() const noexcept;
    [[nodiscard]] FileWritebackErrorTrackerStatus Destroy() noexcept;

  private:
    struct ErrorRecord;

    [[nodiscard]] ErrorRecord *Find(const FileCacheIdentity &identity) noexcept;
    [[nodiscard]] const ErrorRecord *Find(const FileCacheIdentity &identity) const noexcept;

    KernelHeap *heap_{nullptr};
    ErrorRecord *records_{nullptr};
    FileWritebackErrorTrackerStatistics statistics_{};
    mutable SpinLock lock_{};
    bool initialized_{};
};

}
