#pragma once

#include "os/kernel/fs/vfs.hpp"

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_PROCFS_FILE_COUNT = 6ULL;
inline constexpr uint64_t OS_KERNEL_PROCFS_MAXIMUM_SNAPSHOT_SIZE_BYTES = 256ULL;

struct ProcfsSnapshot final {
    uint64_t monotonic_nanoseconds;
    uint64_t managed_memory_bytes;
    uint64_t free_memory_bytes;
    uint64_t allocated_memory_bytes;
    uint64_t active_process_count;
    uint64_t active_thread_count;
    uint64_t process_capacity;
    uint64_t thread_capacity;
    uint64_t current_process_id;
    uint64_t heap_consumed_bytes;
    uint64_t active_file_description_count;
    uint64_t active_pipe_count;
    uint64_t mount_count;
    uint64_t vnode_count;
    uint64_t journal_commit_count;
};

using ProcfsSnapshotOperation =
    bool (*)(void *context, ProcfsSnapshot &snapshot) noexcept;

struct ProcfsStatistics final {
    uint64_t active_open_count;
    uint64_t successful_open_count;
    uint64_t directory_read_count;
    uint64_t snapshot_read_count;
    uint64_t snapshot_failure_count;
    uint64_t bytes_read;
};

class Procfs final {
  public:
    Procfs() noexcept = default;
    Procfs(const Procfs &) = delete;
    Procfs &operator=(const Procfs &) = delete;

    [[nodiscard]] Status Initialize(uint64_t superblock_identifier,
                                    ProcfsSnapshotOperation snapshot_operation,
                                    void *snapshot_context) noexcept;
    [[nodiscard]] Superblock &GetSuperblock() noexcept;
    [[nodiscard]] const Superblock &GetSuperblock() const noexcept;
    [[nodiscard]] ProcfsStatistics ReadStatistics() const noexcept;
    [[nodiscard]] Status Validate() const noexcept;

  private:
    enum class NodeKind : uint64_t {
        Root = 0ULL,
        Version = 1ULL,
        Uptime = 2ULL,
        MemoryInformation = 3ULL,
        Processes = 4ULL,
        Resources = 5ULL,
        Mounts = 6ULL,
    };

    [[nodiscard]] static Status LookupOperation(void *context, const Vnode &directory,
                                                const uint8_t *name,
                                                uint64_t name_length_bytes,
                                                Vnode &vnode) noexcept;
    [[nodiscard]] static Status CreateOperation(void *context, const Vnode &directory,
                                                const uint8_t *name,
                                                uint64_t name_length_bytes, NodeType type,
                                                Vnode &vnode) noexcept;
    [[nodiscard]] static Status OpenOperation(void *context,
                                              const Vnode &vnode) noexcept;
    [[nodiscard]] static Status CloseOperation(void *context,
                                               const Vnode &vnode) noexcept;
    [[nodiscard]] static Status RemoveOperation(void *context, const Vnode &directory,
                                                const uint8_t *name,
                                                uint64_t name_length_bytes,
                                                NodeType expected_type) noexcept;
    [[nodiscard]] static Status RenameOperation(
        void *context, const Vnode &source_directory, const uint8_t *source_name,
        uint64_t source_name_length_bytes, const Vnode &destination_directory,
        const uint8_t *destination_name, uint64_t destination_name_length_bytes,
        bool replace) noexcept;
    [[nodiscard]] static Status ParentOperation(void *context, const Vnode &vnode,
                                                Vnode &parent) noexcept;
    [[nodiscard]] static Status ReadOperation(void *context, const Vnode &vnode,
                                              uint64_t offset_bytes, uint8_t *destination,
                                              uint64_t capacity_bytes,
                                              uint64_t &read_bytes) noexcept;
    [[nodiscard]] static Status WriteOperation(void *context, const Vnode &vnode,
                                               uint64_t offset_bytes, const uint8_t *source,
                                               uint64_t length_bytes,
                                               uint64_t &written_bytes) noexcept;
    [[nodiscard]] static Status TruncateOperation(void *context, const Vnode &vnode,
                                                  uint64_t size_bytes) noexcept;
    [[nodiscard]] static Status ReadDirectoryOperation(
        void *context, const Vnode &directory, uint64_t &cursor, DirectoryEntry &entry,
        bool &end_of_directory) noexcept;
    [[nodiscard]] static Status GetNameOperation(void *context, const Vnode &vnode,
                                                 uint8_t *name,
                                                 uint64_t name_capacity_bytes,
                                                 uint64_t &name_length_bytes) noexcept;
    [[nodiscard]] static Status StatOperation(void *context, const Vnode &vnode,
                                              BackendNodeInformation &information) noexcept;
    [[nodiscard]] static Status SyncOperation(void *context) noexcept;
    [[nodiscard]] static Status ValidateOperation(void *context) noexcept;
    [[nodiscard]] static Status ReadResourceUsageOperation(void *context,
                                                           ResourceUsage &usage) noexcept;

    [[nodiscard]] Status CaptureSnapshot(ProcfsSnapshot &snapshot) noexcept;
    [[nodiscard]] Status Render(NodeKind kind, uint8_t *destination,
                                uint64_t capacity_bytes,
                                uint64_t &rendered_bytes) noexcept;
    [[nodiscard]] Vnode MakeVnode(NodeKind kind) noexcept;
    [[nodiscard]] bool VnodeIsValid(const Vnode &vnode) const noexcept;
    [[nodiscard]] static NodeKind KindFromVnode(const Vnode &vnode) noexcept;

    static const BackendOperations operations;

    Superblock superblock_{};
    ProcfsSnapshotOperation snapshot_operation_{};
    void *snapshot_context_{};
    mutable SpinLock lock_{};
    ProcfsStatistics statistics_{};
    bool initialized_{};
};

}
