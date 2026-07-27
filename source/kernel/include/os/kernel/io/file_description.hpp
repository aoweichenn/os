#pragma once

#include "os/kernel/fs/file_system.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/io/console_input.hpp"
#include "os/kernel/ipc/pipe.hpp"
#include "os/kernel/ipc/pipe_manager.hpp"
#include "os/kernel/object/kernel_object.hpp"

#include <stdint.h>

namespace os::kernel {

using FileDescriptionDeviceWriteOperation = bool (*)(void *context, const uint8_t *source,
                                                     uint64_t length_bytes,
                                                     uint64_t &written_bytes) noexcept;

inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG = 1ULL << 1ULL;
inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTION_VALID_STATUS_FLAG_MASK =
    OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG |
    OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG;

enum class FileDescriptionKind : uint64_t {
    None,
    ConsoleInput,
    ConsoleOutput,
    ConsoleError,
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
    ConsoleInput *console_input;
    FileDescriptionDeviceWriteOperation device_write_operation;
    void *device_write_context;
    Pipe *pipe;
    PipeManager *pipe_manager;
    fs::Vfs *vfs;
    fs::OpenFile open_file;
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
};

// FileDescription 是 fd 背后的共享状态。duplicate 只增加对象强引用，因此
// 文件偏移和 file status flags 共享；fd flags 则留在各自 FileTableEntry。
class FileDescriptionManager final {
  public:
    FileDescriptionManager() noexcept = default;
    FileDescriptionManager(const FileDescriptionManager &) = delete;
    FileDescriptionManager &operator=(const FileDescriptionManager &) = delete;

    [[nodiscard]] FileDescriptionStatus Initialize(KernelObjectManager &object_manager) noexcept;
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
    [[nodiscard]] FileDescriptionStatus ReadCanProgress(const KernelObjectReference &reference,
                                                        bool &can_progress) noexcept;
    [[nodiscard]] FileDescriptionStatus WriteCanProgress(const KernelObjectReference &reference,
                                                         bool &can_progress) noexcept;
    [[nodiscard]] FileDescriptionManagerStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] static bool FinalizePayload(void *payload, void *context) noexcept;
    [[nodiscard]] bool Finalize(void *payload) noexcept;
    [[nodiscard]] bool IsRequestValid(const FileDescriptionCreateRequest &request) const noexcept;

    KernelObjectManager *object_manager_;
    mutable SpinLock statistics_lock_;
    FileDescriptionManagerStatistics statistics_;
    bool initialized_;
};

}
