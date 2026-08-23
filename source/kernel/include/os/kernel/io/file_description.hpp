#pragma once

#include <os/kernel/fs/file_system.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/io/terminal.hpp>
#include <os/kernel/ipc/pipe.hpp>
#include <os/kernel/ipc/pipe_manager.hpp>
#include <os/kernel/memory/file_cache_identity.hpp>
#include <os/kernel/memory/file_readahead.hpp>
#include <os/kernel/memory/file_readahead_feedback.hpp>
#include <os/kernel/object/kernel_object.hpp>

#include <stdint.h>

namespace os::kernel {

using FileDescriptionDeviceWriteOperation = TerminalDeviceWriteOperation;
using FileDescriptionWritebackErrorRegisterOperation =
    bool (*)(const FileCacheIdentity &identity, uint64_t &sampled_sequence) noexcept;
using FileDescriptionWritebackErrorUnregisterOperation =
    bool (*)(const FileCacheIdentity &identity) noexcept;
using FileDescriptionReadaheadPressureOperation =
    bool (*)(void *context, MemoryPressureLevel &pressure_level) noexcept;
using FileDescriptionReadaheadRegisterOperation = bool (*)(
    void *context, const FileCacheIdentity &identity, FileReadaheadStreamToken &stream) noexcept;
using FileDescriptionReadaheadFeedbackOperation = bool (*)(
    void *context, FileReadaheadStreamToken stream, FileReadaheadFeedback &feedback) noexcept;
using FileDescriptionReadaheadCancelOperation = bool (*)(
    void *context, FileReadaheadStreamToken stream, uint64_t maximum_policy_generation) noexcept;
using FileDescriptionReadaheadRetireOperation = bool (*)(void *context,
                                                         FileReadaheadStreamToken stream) noexcept;
using FileDescriptionReadaheadScheduleOperation =
    bool (*)(void *context, fs::Vfs &vfs, const fs::OpenFile &open_file,
             FileReadaheadStreamToken stream, const FileReadaheadDecision &decision) noexcept;

struct FileDescriptionReadaheadOperations final {
    void *context;
    FileDescriptionReadaheadRegisterOperation register_stream;
    FileDescriptionReadaheadFeedbackOperation take_feedback;
    FileDescriptionReadaheadCancelOperation cancel;
    FileDescriptionReadaheadRetireOperation retire_stream;
    FileDescriptionReadaheadPressureOperation pressure;
    FileDescriptionReadaheadScheduleOperation schedule;
};

inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG = 1ULL << 1ULL;
inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTION_APPEND_STATUS_FLAG = 1ULL << 2ULL;
inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTION_VALID_STATUS_FLAG_MASK =
    OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG |
    OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG | OS_KERNEL_FILE_DESCRIPTION_APPEND_STATUS_FLAG;

enum class FileDescriptionKind : uint64_t {
    None,
    TerminalInput,
    TerminalOutput,
    TerminalError,
    TerminalDevice,
    RegularFile,
    Directory,
    PipeReader,
    PipeWriter,
};

enum class FileDescriptionStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    NotInitialized,
    InvalidDependency,
    InvalidReference,
    InvalidConfiguration,
    InvalidArgument,
    PermissionDenied,
    WouldBlock,
    EndOfFile,
    BrokenPipe,
    DeviceFailure,
    FileSystemFailure,
    ObjectFailure,
};

struct FileDescriptionCreateRequest final {
    FileDescriptionKind kind;
    uint64_t file_status_flags;
    Terminal *terminal;
    FileDescriptionDeviceWriteOperation device_write_operation;
    void *device_write_context;
    Pipe *pipe;
    PipeManager *pipe_manager;
    fs::Vfs *vfs;
    fs::OpenFile open_file;
    FileCacheIdentity writeback_identity{};
    FileDescriptionWritebackErrorRegisterOperation writeback_error_register_operation{nullptr};
    FileDescriptionWritebackErrorUnregisterOperation writeback_error_unregister_operation{nullptr};
};

struct FileDescriptionSnapshot final {
    FileDescriptionKind kind;
    uint64_t file_status_flags;
    uint64_t offset_bytes;
    uint64_t generation;
    uint64_t strong_reference_count;
    uint64_t superblock_identifier;
    uint64_t superblock_generation;
    uint64_t node_identifier;
    uint64_t node_generation;
    uint64_t size_bytes;
    uint64_t writeback_error_cursor;
    FileReadaheadStreamToken readahead_stream;
    FileReadaheadStatistics readahead;
};

struct RetainedRegularFile final {
    fs::Vfs *vfs;
    fs::OpenFile open_file;
    uint64_t file_status_flags;
};

struct FileDescriptionManagerStatistics final {
    uint64_t read_operation_count;
    uint64_t write_operation_count;
    uint64_t directory_read_operation_count;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t successful_finalization_count;
    uint64_t failed_finalization_count;
    uint64_t readahead_observation_count;
    uint64_t readahead_decision_count;
    uint64_t readahead_schedule_count;
    uint64_t readahead_schedule_rejection_count;
    uint64_t readahead_useful_page_count;
    uint64_t readahead_wasted_page_count;
    uint64_t readahead_feedback_application_count;
    uint64_t readahead_cancellation_count;
    uint64_t readahead_cancellation_failure_count;
};

// FileDescription 是 fd 背后的共享状态。duplicate 只增加对象强引用，因此
// 文件偏移和 file status flags 共享；fd flags 则留在各自 FileTableEntry。
class FileDescriptionManager final {
  public:
    FileDescriptionManager() noexcept = default;
    FileDescriptionManager(const FileDescriptionManager &) = delete;
    FileDescriptionManager &operator=(const FileDescriptionManager &) = delete;

    [[nodiscard]] FileDescriptionStatus Initialize(KernelObjectManager &object_manager) noexcept;
    [[nodiscard]] FileDescriptionStatus
    ConfigureReadahead(const FileDescriptionReadaheadOperations &operations) noexcept;
    [[nodiscard]] FileDescriptionStatus Create(const FileDescriptionCreateRequest &request,
                                               KernelObjectReference &reference) noexcept;
    [[nodiscard]] FileDescriptionStatus ReadSnapshot(const KernelObjectReference &reference,
                                                     FileDescriptionSnapshot &snapshot) noexcept;
    [[nodiscard]] FileDescriptionStatus
    RetainRegularFile(const KernelObjectReference &reference,
                      RetainedRegularFile &retained_file) noexcept;
    [[nodiscard]] FileDescriptionStatus TryRead(const KernelObjectReference &reference,
                                                uint8_t *destination, uint64_t capacity_bytes,
                                                uint64_t &read_bytes,
                                                FileSystemStatus &file_system_status,
                                                PipeStatus &pipe_status) noexcept;
    [[nodiscard]] FileDescriptionStatus TryWrite(const KernelObjectReference &reference,
                                                 const uint8_t *source, uint64_t length_bytes,
                                                 uint64_t &written_bytes,
                                                 FileSystemStatus &file_system_status,
                                                 PipeStatus &pipe_status) noexcept;
    [[nodiscard]] FileDescriptionStatus
    ReadDirectory(const KernelObjectReference &reference, fs::DirectoryEntry &entry,
                  bool &end_of_directory, FileSystemStatus &file_system_status) noexcept;
    [[nodiscard]] FileDescriptionStatus
    ReadWritebackErrorCursor(const KernelObjectReference &reference,
                             uint64_t &writeback_error_cursor) noexcept;
    [[nodiscard]] FileDescriptionStatus
    ReadSynchronizationState(const KernelObjectReference &reference, FileCacheIdentity &identity,
                             uint64_t &writeback_error_cursor) noexcept;
    [[nodiscard]] FileDescriptionStatus
    AdvanceWritebackErrorCursor(const KernelObjectReference &reference,
                                uint64_t writeback_error_sequence) noexcept;
    [[nodiscard]] FileDescriptionStatus ReadCanProgress(const KernelObjectReference &reference,
                                                        bool &can_progress) noexcept;
    [[nodiscard]] FileDescriptionStatus WriteCanProgress(const KernelObjectReference &reference,
                                                         bool &can_progress) noexcept;
    [[nodiscard]] FileDescriptionManagerStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] static bool FinalizePayload(void *payload, void *context) noexcept;
    [[nodiscard]] bool Finalize(void *payload) noexcept;
    [[nodiscard]] bool IsRequestValid(const FileDescriptionCreateRequest &request) const noexcept;
    [[nodiscard]] bool ApplyPendingReadaheadFeedback(FileReadaheadPolicy &policy,
                                                     FileReadaheadStreamToken stream) noexcept;

    KernelObjectManager *object_manager_;
    FileDescriptionReadaheadOperations readahead_operations_{};
    mutable SpinLock statistics_lock_;
    FileDescriptionManagerStatistics statistics_;
    bool initialized_;
    bool readahead_configured_{};
};

}
