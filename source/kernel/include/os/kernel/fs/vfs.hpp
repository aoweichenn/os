#pragma once

#include <os/abi/security.hpp>
#include <os/kernel/security/credentials.hpp>
#include <os/kernel/sync/runtime_mutex.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES = 4096ULL;
inline constexpr uint64_t OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES = 255ULL;
inline constexpr uint64_t OS_KERNEL_VFS_DEFAULT_MOUNT_CAPACITY = 64ULL;
inline constexpr uint64_t OS_KERNEL_VFS_INVALID_MOUNT_IDENTIFIER = UINT64_MAX;

enum class Status : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    InvalidPath,
    PathTooLong,
    NameTooLong,
    NotFound,
    AlreadyExists,
    NotDirectory,
    IsDirectory,
    PermissionDenied,
    InvalidHandle,
    CapacityExhausted,
    FileTooLarge,
    Corrupt,
    IncompleteTransaction,
    DeviceFailure,
    ReadOnly,
    MountPointBusy,
    MountCapacityExhausted,
    AlreadyMounted,
    LoopDetected,
    DirectoryNotEmpty,
    CrossDevice,
    Busy,
    Unsupported,
};

enum class NodeType : uint64_t {
    None,
    RegularFile,
    Directory,
    CharacterDevice,
    SymbolicLink,
};

enum class BackendKind : uint64_t {
    None,
    Legacy,
    Memory,
    Root,
    Device,
    Process,
};

struct Superblock;
struct ResourceUsage;

struct Vnode final {
    Superblock *superblock;
    uint64_t identifier;
    uint64_t generation;
    NodeType type;
};

struct DirectoryEntry final {
    uint64_t node_identifier;
    NodeType type;
    uint64_t name_length_bytes;
    uint8_t name[OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES];
};

struct OpenOptions final {
    bool readable;
    bool writable;
    bool create;
    bool truncate;
    bool append;
};

struct Path final {
    uint64_t mount_identifier;
    Vnode vnode;
};

struct OpenFile final {
    Path path;
    uint64_t offset_bytes;
    bool readable;
    bool writable;
    bool open;
};

using RegularFileReadCacheOperation = Status (*)(void *context, const OpenFile &open_file,
                                                  uint64_t offset_bytes, uint8_t *destination,
                                                  uint64_t capacity_bytes,
                                                  uint64_t &read_bytes) noexcept;
using RegularFileWriteCacheOperation = Status (*)(void *context, const OpenFile &open_file,
                                                   uint64_t offset_bytes, const uint8_t *source,
                                                   uint64_t length_bytes,
                                                   uint64_t &written_bytes) noexcept;
using RegularFileSizeCacheOperation = Status (*)(void *context, const Vnode &vnode,
                                                  uint64_t backend_size_bytes,
                                                  uint64_t &size_bytes) noexcept;
using RegularFileTruncateCacheOperation = Status (*)(void *context, const Vnode &vnode,
                                                      uint64_t size_bytes) noexcept;

struct BackendNodeInformation final {
    uint64_t size_bytes;
    uint64_t allocated_size_bytes;
    uint64_t link_count;
    uint64_t access_time_nanoseconds;
    uint64_t modification_time_nanoseconds;
    uint64_t change_time_nanoseconds;
    uint64_t birth_time_nanoseconds;
    os::abi::UserIdentifier owner_user_identifier;
    os::abi::GroupIdentifier owner_group_identifier;
    os::abi::FileMode mode;
};

struct NodeInformation final {
    uint64_t mount_identifier;
    uint64_t superblock_identifier;
    uint64_t superblock_generation;
    uint64_t node_identifier;
    uint64_t generation;
    NodeType type;
    uint64_t size_bytes;
    uint64_t allocated_size_bytes;
    uint64_t link_count;
    uint64_t access_time_nanoseconds;
    uint64_t modification_time_nanoseconds;
    uint64_t change_time_nanoseconds;
    uint64_t birth_time_nanoseconds;
    os::abi::UserIdentifier owner_user_identifier;
    os::abi::GroupIdentifier owner_group_identifier;
    os::abi::FileMode mode;
};

struct FsContext final {
    Path root;
    Path current_working_directory;
    security::Credentials credentials;
    os::abi::FileMode creation_mask;
    bool initialized;
};

struct NodeCreationAttributes final {
    os::abi::UserIdentifier owner_user_identifier;
    os::abi::GroupIdentifier owner_group_identifier;
    os::abi::FileMode mode;
};

struct BackendOperations final {
    Status (*lookup)(void *context, const Vnode &directory, const uint8_t *name,
                     uint64_t name_length_bytes, Vnode &vnode) noexcept;
    Status (*create)(void *context, const Vnode &directory, const uint8_t *name,
                     uint64_t name_length_bytes, NodeType type,
                     const NodeCreationAttributes &attributes, Vnode &vnode) noexcept;
    Status (*open)(void *context, const Vnode &vnode) noexcept;
    Status (*close)(void *context, const Vnode &vnode) noexcept;
    Status (*remove)(void *context, const Vnode &directory, const uint8_t *name,
                     uint64_t name_length_bytes, NodeType expected_type) noexcept;
    Status (*rename)(void *context, const Vnode &source_directory, const uint8_t *source_name,
                     uint64_t source_name_length_bytes, const Vnode &destination_directory,
                     const uint8_t *destination_name, uint64_t destination_name_length_bytes,
                     bool replace) noexcept;
    Status (*link)(void *context, const Vnode &source, const Vnode &destination_directory,
                   const uint8_t *destination_name,
                   uint64_t destination_name_length_bytes) noexcept;
    Status (*create_symbolic_link)(void *context, const Vnode &destination_directory,
                                   const uint8_t *destination_name,
                                   uint64_t destination_name_length_bytes, const uint8_t *target,
                                   uint64_t target_length_bytes,
                                   const NodeCreationAttributes &attributes, Vnode &vnode) noexcept;
    Status (*read_symbolic_link)(void *context, const Vnode &vnode, uint8_t *destination,
                                 uint64_t capacity_bytes, uint64_t &target_length_bytes) noexcept;
    Status (*parent)(void *context, const Vnode &vnode, Vnode &parent) noexcept;
    Status (*read)(void *context, const Vnode &vnode, uint64_t offset_bytes, uint8_t *destination,
                   uint64_t capacity_bytes, uint64_t &read_bytes) noexcept;
    Status (*write)(void *context, const Vnode &vnode, uint64_t offset_bytes, const uint8_t *source,
                    uint64_t length_bytes, uint64_t &written_bytes) noexcept;
    Status (*truncate)(void *context, const Vnode &vnode, uint64_t size_bytes) noexcept;
    Status (*read_directory)(void *context, const Vnode &directory, uint64_t &cursor,
                             DirectoryEntry &entry, bool &end_of_directory) noexcept;
    Status (*get_name)(void *context, const Vnode &vnode, uint8_t *name,
                       uint64_t name_capacity_bytes, uint64_t &name_length_bytes) noexcept;
    Status (*stat)(void *context, const Vnode &vnode, BackendNodeInformation &information) noexcept;
    Status (*change_mode)(void *context, const Vnode &vnode, os::abi::FileMode mode) noexcept;
    Status (*change_owner)(void *context, const Vnode &vnode,
                           os::abi::UserIdentifier user_identifier,
                           os::abi::GroupIdentifier group_identifier) noexcept;
    Status (*sync)(void *context) noexcept;
    Status (*validate)(void *context) noexcept;
    Status (*read_resource_usage)(void *context, ResourceUsage &usage) noexcept;
};

struct Superblock final {
    BackendKind backend_kind;
    uint64_t identifier;
    uint64_t generation;
    Vnode root;
    const BackendOperations *operations;
    void *backend_context;
    uint64_t maximum_name_length_bytes;
    bool cache_regular_file_data;
    bool read_only;
    bool initialized;
};

struct Mount final {
    uint64_t identifier;
    uint64_t parent_mount_identifier;
    Path mount_point;
    Superblock *superblock;
    bool active;
};

struct Statistics final {
    uint64_t mount_count;
    uint64_t path_resolution_count;
    uint64_t failed_path_resolution_count;
    uint64_t component_lookup_count;
    uint64_t mount_transition_count;
    uint64_t root_clamp_count;
    uint64_t opened_file_count;
    uint64_t opened_directory_count;
    uint64_t bytes_read;
    uint64_t bytes_written;
};

struct ResourceUsage final {
    uint64_t heap_consumed_bytes;
    uint64_t heap_active_requested_bytes;
    uint64_t heap_allocation_count;
    uint64_t vnode_count;
};

class Vfs final {
  public:
    Vfs() noexcept = default;

    // Mount 存储由调用方长期持有，VFS 不在路径解析期间分配内存。
    [[nodiscard]] Status Initialize(Mount *mount_storage, uint64_t mount_capacity,
                                    Superblock &root_superblock) noexcept;
    [[nodiscard]] Status InitializeContext(FsContext &context) const noexcept;
    [[nodiscard]] Status CloneContext(const FsContext &source, FsContext &context) const noexcept;
    [[nodiscard]] Status ReleaseContext(FsContext &context) const noexcept;
    [[nodiscard]] Status MountAt(const FsContext &context, const uint8_t *path,
                                 uint64_t path_length_bytes, Superblock &superblock) noexcept;
    [[nodiscard]] Status Resolve(const FsContext &context, const uint8_t *path,
                                 uint64_t path_length_bytes, Path &resolved_path) noexcept;
    [[nodiscard]] Status CreateDirectory(const FsContext &context, const uint8_t *path,
                                         uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status RemoveFile(const FsContext &context, const uint8_t *path,
                                    uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status RemoveDirectory(const FsContext &context, const uint8_t *path,
                                         uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status Rename(const FsContext &context, const uint8_t *source_path,
                                uint64_t source_path_length_bytes, const uint8_t *destination_path,
                                uint64_t destination_path_length_bytes, bool replace) noexcept;
    [[nodiscard]] Status Link(const FsContext &context, const uint8_t *source_path,
                              uint64_t source_path_length_bytes, const uint8_t *destination_path,
                              uint64_t destination_path_length_bytes) noexcept;
    [[nodiscard]] Status CreateSymbolicLink(const FsContext &context, const uint8_t *target,
                                            uint64_t target_length_bytes,
                                            const uint8_t *destination_path,
                                            uint64_t destination_path_length_bytes) noexcept;
    [[nodiscard]] Status ReadSymbolicLink(const FsContext &context, const uint8_t *path,
                                          uint64_t path_length_bytes, uint8_t *destination,
                                          uint64_t capacity_bytes,
                                          uint64_t &target_length_bytes) noexcept;
    [[nodiscard]] Status Truncate(const FsContext &context, const uint8_t *path,
                                  uint64_t path_length_bytes, uint64_t size_bytes) noexcept;
    [[nodiscard]] Status TruncateOpenFile(const OpenFile &open_file,
                                          uint64_t size_bytes) noexcept;
    [[nodiscard]] Status Stat(const FsContext &context, const uint8_t *path,
                              uint64_t path_length_bytes, NodeInformation &information) noexcept;
    [[nodiscard]] Status CheckAccess(const FsContext &context, const uint8_t *path,
                                     uint64_t path_length_bytes,
                                     uint32_t requested_access) noexcept;
    [[nodiscard]] Status ChangeMode(const FsContext &context, const uint8_t *path,
                                    uint64_t path_length_bytes, os::abi::FileMode mode) noexcept;
    [[nodiscard]] Status ChangeOwner(const FsContext &context, const uint8_t *path,
                                     uint64_t path_length_bytes,
                                     os::abi::UserIdentifier user_identifier,
                                     os::abi::GroupIdentifier group_identifier) noexcept;
    [[nodiscard]] Status Open(const FsContext &context, const uint8_t *path,
                              uint64_t path_length_bytes, const OpenOptions &options,
                              OpenFile &open_file) noexcept;
    [[nodiscard]] Status OpenExecutable(const FsContext &context, const uint8_t *path,
                                        uint64_t path_length_bytes, OpenFile &open_file) noexcept;
    [[nodiscard]] Status OpenDirectory(const FsContext &context, const uint8_t *path,
                                       uint64_t path_length_bytes, OpenFile &open_file) noexcept;
    [[nodiscard]] Status RetainOpenFile(const OpenFile &source, OpenFile &retained_file) noexcept;
    [[nodiscard]] Status ConfigureRegularFileDataCache(
        void *context, RegularFileReadCacheOperation read_operation,
        RegularFileWriteCacheOperation write_operation,
        RegularFileSizeCacheOperation size_operation,
        RegularFileTruncateCacheOperation truncate_operation) noexcept;
    [[nodiscard]] Status StatOpenFile(const OpenFile &open_file,
                                      NodeInformation &information) noexcept;
    [[nodiscard]] Status StatOpenFileUncached(const OpenFile &open_file,
                                              NodeInformation &information) noexcept;
    [[nodiscard]] Status ReadAt(const OpenFile &open_file, uint64_t offset_bytes,
                                uint8_t *destination, uint64_t capacity_bytes,
                                uint64_t &read_bytes) noexcept;
    [[nodiscard]] Status ReadUncachedAt(const OpenFile &open_file, uint64_t offset_bytes,
                                        uint8_t *destination, uint64_t capacity_bytes,
                                        uint64_t &read_bytes) noexcept;
    [[nodiscard]] Status WriteAt(const OpenFile &open_file, uint64_t offset_bytes,
                                 const uint8_t *source, uint64_t length_bytes,
                                 uint64_t &written_bytes) noexcept;
    [[nodiscard]] Status WriteUncachedAt(const OpenFile &open_file, uint64_t offset_bytes,
                                         const uint8_t *source, uint64_t length_bytes,
                                         uint64_t &written_bytes) noexcept;
    [[nodiscard]] Status Read(OpenFile &open_file, uint8_t *destination, uint64_t capacity_bytes,
                              uint64_t &read_bytes) noexcept;
    [[nodiscard]] Status Write(OpenFile &open_file, const uint8_t *source, uint64_t length_bytes,
                               uint64_t &written_bytes) noexcept;
    [[nodiscard]] Status ReadDirectory(OpenFile &open_file, DirectoryEntry &entry,
                                       bool &end_of_directory) noexcept;
    [[nodiscard]] Status Close(OpenFile &open_file) noexcept;
    [[nodiscard]] Status ChangeDirectory(FsContext &context, const uint8_t *path,
                                         uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status GetWorkingDirectory(const FsContext &context, uint8_t *destination,
                                             uint64_t capacity_bytes,
                                             uint64_t &path_length_bytes) noexcept;
    [[nodiscard]] Status Sync() noexcept;
    [[nodiscard]] Status Validate() noexcept;
    [[nodiscard]] Statistics ReadStatistics() const noexcept;
    [[nodiscard]] Status ReadResourceUsage(ResourceUsage &usage) const noexcept;

  private:
    struct ParentResolution final {
        Path parent;
        uint8_t name[OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES];
        uint64_t name_length_bytes;
    };

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool PathIsValid(const Path &path) const noexcept;
    [[nodiscard]] bool PathsAreEqual(const Path &left, const Path &right) const noexcept;
    [[nodiscard]] bool VnodesAreEqual(const Vnode &left, const Vnode &right) const noexcept;
    [[nodiscard]] Mount *FindMount(uint64_t mount_identifier) noexcept;
    [[nodiscard]] const Mount *FindMount(uint64_t mount_identifier) const noexcept;
    [[nodiscard]] Mount *FindChildMount(const Path &mount_point) noexcept;
    [[nodiscard]] Status FollowMounts(Path &path) noexcept;
    [[nodiscard]] Status MoveToParent(const FsContext &context, Path &path) noexcept;
    [[nodiscard]] Status ResolveParent(const FsContext &context, const uint8_t *path,
                                       uint64_t path_length_bytes,
                                       ParentResolution &resolution) noexcept;
    [[nodiscard]] Status ResolveInternal(const FsContext &context, const uint8_t *path,
                                         uint64_t path_length_bytes, bool follow_final_link,
                                         Path &resolved_path) noexcept;
    [[nodiscard]] Status ReadPathName(const Path &path, uint8_t *name, uint64_t name_capacity_bytes,
                                      uint64_t &name_length_bytes) noexcept;
    [[nodiscard]] Status ValidateSuperblock(const Superblock &superblock) const noexcept;
    [[nodiscard]] Status Remove(const FsContext &context, const uint8_t *path,
                                uint64_t path_length_bytes, NodeType expected_type) noexcept;
    [[nodiscard]] Status ReadNodeInformation(const Path &path,
                                             BackendNodeInformation &information) noexcept;
    [[nodiscard]] Status ApplyRegularFileCachedSize(const Vnode &vnode,
                                                    BackendNodeInformation &information) noexcept;
    [[nodiscard]] Status TruncateNode(const Vnode &vnode, uint64_t size_bytes) noexcept;
    [[nodiscard]] Status RequireAccess(const FsContext &context, const Path &path,
                                       uint32_t requested_access) noexcept;
    [[nodiscard]] Status RequireParentMutationAccess(const FsContext &context,
                                                     const Path &parent) noexcept;
    [[nodiscard]] Status CheckStickyRemoval(const FsContext &context, const Path &directory,
                                            const Path &target) noexcept;
    [[nodiscard]] Status MakeCreationAttributes(const FsContext &context, const Path &parent,
                                                NodeType type, os::abi::FileMode requested_mode,
                                                NodeCreationAttributes &attributes) noexcept;
    void RecordResolution(Status status) noexcept;

    Mount *mounts_{nullptr};
    uint64_t mount_capacity_{};
    uint64_t mount_count_{};
    mutable SpinLock lock_{};
    mutable RuntimeMutex resolution_lock_{};
    uint8_t resolution_path_a_[OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES]{};
    uint8_t resolution_path_b_[OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES]{};
    Statistics statistics_{};
    void *regular_file_data_cache_context_{nullptr};
    RegularFileReadCacheOperation regular_file_read_cache_operation_{nullptr};
    RegularFileWriteCacheOperation regular_file_write_cache_operation_{nullptr};
    RegularFileSizeCacheOperation regular_file_size_cache_operation_{nullptr};
    RegularFileTruncateCacheOperation regular_file_truncate_cache_operation_{nullptr};
    bool initialized_{};
};

}
