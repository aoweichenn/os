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
inline constexpr uint64_t OS_KERNEL_VFS_LOOKUP_LOCK_SHARD_COUNT = 64ULL;
inline constexpr uint64_t OS_KERNEL_VFS_METADATA_LOCK_SHARD_COUNT = 64ULL;
inline constexpr uint64_t OS_KERNEL_VFS_DEFAULT_RESOLUTION_CONTEXT_CAPACITY = 128ULL;

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

struct DirectoryHandle final {
    Path path;
    bool active;
};

struct RegularFileReadCacheObservation final {
    uint64_t first_page_index;
    uint64_t requested_page_count;
    uint64_t file_page_count;
    uint64_t cache_hit_page_count;
    uint64_t cache_miss_page_count;
    uint64_t prefetched_hit_page_count;
    bool cache_used;
};

using RegularFileReadCacheOperation =
    Status (*)(void *context, const OpenFile &open_file, uint64_t offset_bytes,
               uint8_t *destination, uint64_t capacity_bytes, uint64_t &read_bytes,
               RegularFileReadCacheObservation &observation) noexcept;
using RegularFileWriteCacheOperation = Status (*)(void *context, const OpenFile &open_file,
                                                  uint64_t offset_bytes, const uint8_t *source,
                                                  uint64_t length_bytes,
                                                  uint64_t &written_bytes) noexcept;
using RegularFileSizeCacheOperation = Status (*)(void *context, const Vnode &vnode,
                                                 uint64_t backend_size_bytes,
                                                 uint64_t &size_bytes) noexcept;
using RegularFileTruncateCacheOperation = Status (*)(void *context, const Vnode &vnode,
                                                     uint64_t size_bytes) noexcept;
using NamespaceBackingReleaseOperation = Status (*)(void *context,
                                                    uint64_t &released_page_count) noexcept;

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
    uint64_t dentry_positive_hit_count;
    uint64_t dentry_negative_hit_count;
    uint64_t dentry_miss_count;
    uint64_t dentry_publish_bypass_count;
    uint64_t namespace_reclaim_operation_count;
    uint64_t reclaimed_dentry_count;
    uint64_t reclaimed_inode_count;
    uint64_t namespace_retry_count;
    uint64_t peak_resolution_context_count;
    uint64_t namespace_hash_shrink_count;
    uint64_t released_namespace_page_count;
    uint64_t at_path_operation_count;
    uint64_t directory_handle_retain_count;
    uint64_t directory_handle_release_count;
    uint64_t active_directory_handle_count;
    uint64_t peak_directory_handle_count;
};

struct NamespaceCacheReclaimResult final {
    uint64_t dentry_count;
    uint64_t inode_count;
    uint64_t released_page_count;
    bool hash_tier_shrunk;
};

struct VfsResolutionContext final {
    uint8_t path_a[OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES];
    uint8_t path_b[OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES];
    bool active;
};

struct NamespaceBackingResourceUsage final {
    uint64_t page_count;
    uint64_t allocated_frame_count;
    uint64_t buddy_active_block_count;
    uint64_t virtual_address_allocated_page_count;
    uint64_t virtual_address_active_descriptor_count;
    uint64_t virtual_address_active_allocation_count;
};

struct ResourceUsage final {
    uint64_t heap_consumed_bytes;
    uint64_t heap_active_requested_bytes;
    uint64_t heap_allocation_count;
    uint64_t vnode_count;
    NamespaceBackingResourceUsage namespace_backing;
};

class VfsNamespaceCache;

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
    [[nodiscard]] Status ResolveAt(const FsContext &context, const DirectoryHandle *directory,
                                   const uint8_t *path, uint64_t path_length_bytes,
                                   Path &resolved_path) noexcept;
    [[nodiscard]] Status CreateDirectory(const FsContext &context, const uint8_t *path,
                                         uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status CreateDirectoryAt(const FsContext &context,
                                           const DirectoryHandle *directory, const uint8_t *path,
                                           uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status RemoveFile(const FsContext &context, const uint8_t *path,
                                    uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status RemoveDirectory(const FsContext &context, const uint8_t *path,
                                         uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status RemoveFileAt(const FsContext &context, const DirectoryHandle *directory,
                                      const uint8_t *path, uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status RemoveDirectoryAt(const FsContext &context,
                                           const DirectoryHandle *directory, const uint8_t *path,
                                           uint64_t path_length_bytes) noexcept;
    [[nodiscard]] Status Rename(const FsContext &context, const uint8_t *source_path,
                                uint64_t source_path_length_bytes, const uint8_t *destination_path,
                                uint64_t destination_path_length_bytes, bool replace) noexcept;
    [[nodiscard]] Status RenameAt(const FsContext &context, const DirectoryHandle *source_directory,
                                  const uint8_t *source_path, uint64_t source_path_length_bytes,
                                  const DirectoryHandle *destination_directory,
                                  const uint8_t *destination_path,
                                  uint64_t destination_path_length_bytes, bool replace) noexcept;
    [[nodiscard]] Status Link(const FsContext &context, const uint8_t *source_path,
                              uint64_t source_path_length_bytes, const uint8_t *destination_path,
                              uint64_t destination_path_length_bytes) noexcept;
    [[nodiscard]] Status LinkAt(const FsContext &context, const DirectoryHandle *source_directory,
                                const uint8_t *source_path, uint64_t source_path_length_bytes,
                                const DirectoryHandle *destination_directory,
                                const uint8_t *destination_path,
                                uint64_t destination_path_length_bytes) noexcept;
    [[nodiscard]] Status CreateSymbolicLink(const FsContext &context, const uint8_t *target,
                                            uint64_t target_length_bytes,
                                            const uint8_t *destination_path,
                                            uint64_t destination_path_length_bytes) noexcept;
    [[nodiscard]] Status CreateSymbolicLinkAt(const FsContext &context, const uint8_t *target,
                                              uint64_t target_length_bytes,
                                              const DirectoryHandle *destination_directory,
                                              const uint8_t *destination_path,
                                              uint64_t destination_path_length_bytes) noexcept;
    [[nodiscard]] Status ReadSymbolicLink(const FsContext &context, const uint8_t *path,
                                          uint64_t path_length_bytes, uint8_t *destination,
                                          uint64_t capacity_bytes,
                                          uint64_t &target_length_bytes) noexcept;
    [[nodiscard]] Status ReadSymbolicLinkAt(const FsContext &context,
                                            const DirectoryHandle *directory, const uint8_t *path,
                                            uint64_t path_length_bytes, uint8_t *destination,
                                            uint64_t capacity_bytes,
                                            uint64_t &target_length_bytes) noexcept;
    [[nodiscard]] Status Truncate(const FsContext &context, const uint8_t *path,
                                  uint64_t path_length_bytes, uint64_t size_bytes) noexcept;
    [[nodiscard]] Status TruncateOpenFile(const OpenFile &open_file, uint64_t size_bytes) noexcept;
    [[nodiscard]] Status Stat(const FsContext &context, const uint8_t *path,
                              uint64_t path_length_bytes, NodeInformation &information) noexcept;
    [[nodiscard]] Status StatAt(const FsContext &context, const DirectoryHandle *directory,
                                const uint8_t *path, uint64_t path_length_bytes,
                                bool follow_final_link, NodeInformation &information) noexcept;
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
    [[nodiscard]] Status OpenAt(const FsContext &context, const DirectoryHandle *directory,
                                const uint8_t *path, uint64_t path_length_bytes,
                                const OpenOptions &options, OpenFile &open_file) noexcept;
    [[nodiscard]] Status OpenExecutable(const FsContext &context, const uint8_t *path,
                                        uint64_t path_length_bytes, OpenFile &open_file) noexcept;
    [[nodiscard]] Status OpenDirectory(const FsContext &context, const uint8_t *path,
                                       uint64_t path_length_bytes, OpenFile &open_file) noexcept;
    [[nodiscard]] Status OpenDirectoryAt(const FsContext &context, const DirectoryHandle *directory,
                                         const uint8_t *path, uint64_t path_length_bytes,
                                         OpenFile &open_file) noexcept;
    [[nodiscard]] Status RetainOpenFile(const OpenFile &source, OpenFile &retained_file) noexcept;
    [[nodiscard]] Status RetainDirectoryHandle(const OpenFile &source,
                                               DirectoryHandle &handle) noexcept;
    [[nodiscard]] Status ReleaseDirectoryHandle(DirectoryHandle &handle) noexcept;
    [[nodiscard]] Status
    ConfigureRegularFileDataCache(void *context, RegularFileReadCacheOperation read_operation,
                                  RegularFileWriteCacheOperation write_operation,
                                  RegularFileSizeCacheOperation size_operation,
                                  RegularFileTruncateCacheOperation truncate_operation) noexcept;
    [[nodiscard]] Status ConfigureNamespaceCache(VfsNamespaceCache &cache) noexcept;
    [[nodiscard]] Status ConfigureResolutionContexts(VfsResolutionContext *contexts,
                                                     uint64_t capacity) noexcept;
    [[nodiscard]] Status ConfigureNamespaceCacheShrinkTier(
        uint64_t *dentry_buckets, uint64_t dentry_bucket_capacity, uint64_t *inode_buckets,
        uint64_t inode_bucket_capacity, const NamespaceBackingResourceUsage &stable_usage,
        const NamespaceBackingResourceUsage &preferred_usage, void *backing_context,
        NamespaceBackingReleaseOperation release_operation) noexcept;
    [[nodiscard]] Status ReclaimNamespaceCache(uint64_t maximum_dentry_count,
                                               uint64_t maximum_inode_count,
                                               NamespaceCacheReclaimResult &result) noexcept;
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
    [[nodiscard]] Status ReadObserved(OpenFile &open_file, uint8_t *destination,
                                      uint64_t capacity_bytes, uint64_t &read_bytes,
                                      RegularFileReadCacheObservation &observation) noexcept;
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
    class NamespaceMutationGuard final {
      public:
        NamespaceMutationGuard(Vfs &vfs, uint64_t expected_sequence) noexcept;
        ~NamespaceMutationGuard() noexcept;
        [[nodiscard]] bool Active() const noexcept;

        NamespaceMutationGuard(const NamespaceMutationGuard &) = delete;
        NamespaceMutationGuard &operator=(const NamespaceMutationGuard &) = delete;

      private:
        Vfs &vfs_;
        bool active_;
    };

    class MetadataLockGuard final {
      public:
        MetadataLockGuard(Vfs &vfs, const Vnode *vnodes, uint64_t vnode_count) noexcept;
        ~MetadataLockGuard() noexcept;
        [[nodiscard]] bool Active() const noexcept;

        MetadataLockGuard(const MetadataLockGuard &) = delete;
        MetadataLockGuard &operator=(const MetadataLockGuard &) = delete;

      private:
        static constexpr uint64_t OS_KERNEL_VFS_METADATA_GUARD_MAXIMUM_VNODE_COUNT = 4ULL;

        Vfs &vfs_;
        uint64_t shard_indices_[OS_KERNEL_VFS_METADATA_GUARD_MAXIMUM_VNODE_COUNT]{};
        uint64_t shard_count_{};
        bool active_{};
    };

    struct ParentResolution final {
        Path parent;
        uint8_t name[OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES];
        uint64_t name_length_bytes;
    };

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool PathIsValid(const Path &path) const noexcept;
    [[nodiscard]] bool PathsAreEqual(const Path &left, const Path &right) const noexcept;
    [[nodiscard]] Status BuildAtContext(const FsContext &context, const DirectoryHandle *directory,
                                        const uint8_t *path, uint64_t path_length_bytes,
                                        FsContext &at_context) const noexcept;
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
                                         VfsResolutionContext &resolution_context,
                                         Path &resolved_path) noexcept;
    [[nodiscard]] Status ResolveWithFinalLinkOption(const FsContext &context, const uint8_t *path,
                                                    uint64_t path_length_bytes,
                                                    bool follow_final_link,
                                                    Path &resolved_path) noexcept;
    [[nodiscard]] Status ReadPathName(const Path &path, uint8_t *name, uint64_t name_capacity_bytes,
                                      uint64_t &name_length_bytes) noexcept;
    [[nodiscard]] Status ValidateSuperblock(const Superblock &superblock) const noexcept;
    [[nodiscard]] Status Remove(const FsContext &context, const uint8_t *path,
                                uint64_t path_length_bytes, NodeType expected_type) noexcept;
    [[nodiscard]] Status
    RenameBetween(const FsContext &process_context, const FsContext &source_context,
                  const uint8_t *source_path, uint64_t source_path_length_bytes,
                  const FsContext &destination_context, const uint8_t *destination_path,
                  uint64_t destination_path_length_bytes, bool replace) noexcept;
    [[nodiscard]] Status LinkBetween(const FsContext &process_context,
                                     const FsContext &source_context, const uint8_t *source_path,
                                     uint64_t source_path_length_bytes,
                                     const FsContext &destination_context,
                                     const uint8_t *destination_path,
                                     uint64_t destination_path_length_bytes) noexcept;
    [[nodiscard]] Status ReadNodeInformation(const Path &path,
                                             BackendNodeInformation &information) noexcept;
    [[nodiscard]] Status ReadNodeInformationUncached(const Path &path,
                                                     BackendNodeInformation &information) noexcept;
    [[nodiscard]] Status InvalidateNodeInformation(const Vnode &vnode) noexcept;
    [[nodiscard]] Status LookupChild(const Path &parent, const uint8_t *name,
                                     uint64_t name_length_bytes, Vnode &child) noexcept;
    [[nodiscard]] Status InvalidateDentryInformation(const Path &parent, const uint8_t *name,
                                                     uint64_t name_length_bytes) noexcept;
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
    void RecordAtPathOperation() noexcept;
    [[nodiscard]] VfsResolutionContext *AcquireResolutionContext() noexcept;
    void ReleaseResolutionContext(VfsResolutionContext &context) noexcept;
    [[nodiscard]] uint64_t ReadNamespaceSequence() const noexcept;
    [[nodiscard]] bool BeginNamespaceMutation(uint64_t expected_sequence) noexcept;
    [[nodiscard]] bool EndNamespaceMutation() noexcept;

    Mount *mounts_{nullptr};
    uint64_t mount_capacity_{};
    uint64_t mount_count_{};
    mutable SpinLock lock_{};
    mutable RuntimeMutex resolution_lock_{};
    mutable RuntimeMutex metadata_locks_[OS_KERNEL_VFS_METADATA_LOCK_SHARD_COUNT]{};
    mutable RuntimeMutex namespace_mutation_lock_{};
    mutable RuntimeMutex namespace_reclaim_lock_{};
    mutable RuntimeMutex lookup_locks_[OS_KERNEL_VFS_LOOKUP_LOCK_SHARD_COUNT]{};
    mutable SpinLock resolution_context_lock_{};
    VfsResolutionContext fallback_resolution_context_{};
    VfsResolutionContext *resolution_contexts_{};
    uint64_t resolution_context_capacity_{};
    uint64_t active_resolution_context_count_{};
    uint64_t peak_resolution_context_count_{};
    uint64_t namespace_sequence_{};
    Statistics statistics_{};
    void *regular_file_data_cache_context_{nullptr};
    RegularFileReadCacheOperation regular_file_read_cache_operation_{nullptr};
    RegularFileWriteCacheOperation regular_file_write_cache_operation_{nullptr};
    RegularFileSizeCacheOperation regular_file_size_cache_operation_{nullptr};
    RegularFileTruncateCacheOperation regular_file_truncate_cache_operation_{nullptr};
    VfsNamespaceCache *namespace_cache_{nullptr};
    uint64_t *compact_dentry_hash_buckets_{};
    uint64_t compact_dentry_hash_bucket_capacity_{};
    uint64_t *compact_inode_hash_buckets_{};
    uint64_t compact_inode_hash_bucket_capacity_{};
    void *namespace_backing_context_{};
    NamespaceBackingReleaseOperation namespace_backing_release_operation_{};
    NamespaceBackingResourceUsage active_namespace_backing_usage_{};
    NamespaceBackingResourceUsage preferred_namespace_backing_usage_{};
    bool namespace_hash_tier_shrunk_{};
    bool initialized_{};
};

}
