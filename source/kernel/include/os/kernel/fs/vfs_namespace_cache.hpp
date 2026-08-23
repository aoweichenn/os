#pragma once

#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX = UINT64_MAX;

struct VfsInodeIdentity final {
    uint64_t superblock_identifier;
    uint64_t superblock_generation;
    uint64_t node_identifier;
    uint64_t node_generation;
};

struct VfsDentryKey final {
    uint64_t mount_identifier;
    VfsInodeIdentity parent;
    uint64_t name_length_bytes;
    uint8_t name[OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES];
};

enum class VfsDentryKind : uint64_t {
    None,
    Positive,
    Negative,
};

enum class VfsNamespaceEntryState : uint64_t {
    Free,
    Cached,
    Stale,
};

enum class VfsInodeMetadataState : uint64_t {
    Empty,
    Loading,
    Ready,
};

struct VfsDentryToken final {
    uint64_t slot_index;
    uint64_t generation;
};

struct VfsInodeToken final {
    uint64_t slot_index;
    uint64_t generation;
};

struct VfsInodeMetadataToken final {
    VfsInodeToken inode_token;
    uint64_t metadata_generation;
};

struct VfsNamespaceHashEntry final {
    uint64_t hash_value;
    uint64_t next_slot_index;
    bool indexed;
};

struct VfsDentrySlot final {
    VfsDentryKey key;
    VfsInodeToken inode_token;
    uint64_t access_generation;
    uint64_t external_reference_count;
    uint64_t generation;
    VfsDentryKind kind;
    VfsNamespaceEntryState state;
};

struct VfsInodeSlot final {
    VfsInodeIdentity identity;
    BackendNodeInformation metadata;
    uint64_t access_generation;
    uint64_t dentry_reference_count;
    uint64_t external_reference_count;
    uint64_t generation;
    uint64_t metadata_generation;
    NodeType type;
    VfsNamespaceEntryState state;
    VfsInodeMetadataState metadata_state;
};

struct VfsDentrySnapshot final {
    VfsDentryToken token;
    VfsDentryKey key;
    VfsInodeToken inode_token;
    VfsInodeIdentity inode_identity;
    uint64_t access_generation;
    uint64_t external_reference_count;
    NodeType inode_type;
    VfsDentryKind kind;
    VfsNamespaceEntryState state;
};

struct VfsInodeSnapshot final {
    VfsInodeToken token;
    VfsInodeIdentity identity;
    uint64_t access_generation;
    uint64_t dentry_reference_count;
    uint64_t external_reference_count;
    NodeType type;
    VfsNamespaceEntryState state;
};

struct VfsInodeMetadataSnapshot final {
    VfsInodeMetadataToken token;
    VfsInodeIdentity identity;
    BackendNodeInformation metadata;
    uint64_t access_generation;
    NodeType type;
    VfsInodeMetadataState state;
};

struct VfsNamespaceCacheStatistics final {
    uint64_t dentry_capacity;
    uint64_t inode_capacity;
    uint64_t dentry_hash_bucket_capacity;
    uint64_t inode_hash_bucket_capacity;
    uint64_t active_dentry_count;
    uint64_t cached_positive_dentry_count;
    uint64_t cached_negative_dentry_count;
    uint64_t stale_dentry_count;
    uint64_t referenced_dentry_count;
    uint64_t active_dentry_reference_count;
    uint64_t peak_active_dentry_count;
    uint64_t peak_active_dentry_reference_count;
    uint64_t active_inode_count;
    uint64_t cached_inode_count;
    uint64_t stale_inode_count;
    uint64_t referenced_inode_count;
    uint64_t inode_dentry_reference_count;
    uint64_t active_inode_external_reference_count;
    uint64_t loading_inode_metadata_count;
    uint64_t ready_inode_metadata_count;
    uint64_t peak_active_inode_count;
    uint64_t peak_inode_reference_count;
    uint64_t positive_publish_count;
    uint64_t negative_publish_count;
    uint64_t already_cached_count;
    uint64_t conflict_count;
    uint64_t positive_hit_count;
    uint64_t negative_hit_count;
    uint64_t dentry_miss_count;
    uint64_t dentry_release_count;
    uint64_t inode_hit_count;
    uint64_t inode_miss_count;
    uint64_t inode_release_count;
    uint64_t dentry_invalidation_count;
    uint64_t inode_invalidation_count;
    uint64_t cascaded_dentry_invalidation_count;
    uint64_t dentry_eviction_count;
    uint64_t inode_eviction_count;
    uint64_t stale_dentry_release_count;
    uint64_t stale_inode_release_count;
    uint64_t capacity_rejection_count;
    uint64_t inode_metadata_hit_count;
    uint64_t inode_metadata_miss_count;
    uint64_t inode_metadata_load_start_count;
    uint64_t inode_metadata_load_completion_count;
    uint64_t inode_metadata_load_cancellation_count;
    uint64_t inode_metadata_load_contention_count;
    uint64_t inode_metadata_invalidation_count;
    uint64_t hash_rebuild_count;
};

enum class VfsNamespaceCacheStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidKey,
    InvalidIdentity,
    InvalidNodeType,
    InvalidMetadata,
    DentryNotFound,
    InodeNotFound,
    InodeMetadataNotFound,
    InodeMetadataLoadRequired,
    InodeMetadataLoadInProgress,
    AlreadyCached,
    EntryConflict,
    CapacityExhausted,
    InvalidToken,
    InvalidState,
    GenerationExhausted,
    CounterOverflow,
    EntriesRemain,
    Corrupt,
};

class VfsNamespaceCache final {
  public:
    [[nodiscard]] VfsNamespaceCacheStatus Initialize(VfsDentrySlot *dentry_storage,
                                                     uint64_t dentry_capacity,
                                                     VfsInodeSlot *inode_storage,
                                                     uint64_t inode_capacity) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus
    ConfigureHashIndex(VfsNamespaceHashEntry *dentry_entries, uint64_t dentry_entry_capacity,
                       uint64_t *dentry_buckets, uint64_t dentry_bucket_capacity,
                       VfsNamespaceHashEntry *inode_entries, uint64_t inode_entry_capacity,
                       uint64_t *inode_buckets, uint64_t inode_bucket_capacity) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus
    RebuildHashBuckets(uint64_t *dentry_buckets, uint64_t dentry_bucket_capacity,
                       uint64_t *inode_buckets, uint64_t inode_bucket_capacity) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus PublishPositive(const VfsDentryKey &key,
                                                          const VfsInodeIdentity &inode_identity,
                                                          NodeType inode_type,
                                                          VfsDentryToken &token) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus PublishNegative(const VfsDentryKey &key,
                                                          VfsDentryToken &token) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus AcquireDentry(const VfsDentryKey &key,
                                                        VfsDentrySnapshot &snapshot) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus ReleaseDentry(VfsDentryToken token) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus AcquireInode(const VfsInodeIdentity &identity,
                                                       VfsInodeSnapshot &snapshot) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus ReleaseInode(VfsInodeToken token) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus
    PrepareInodeMetadata(const VfsInodeIdentity &identity, NodeType type,
                         VfsInodeMetadataToken &token, VfsInodeMetadataSnapshot &snapshot) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus
    CompleteInodeMetadata(VfsInodeMetadataToken token,
                          const BackendNodeInformation &metadata) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus CancelInodeMetadata(VfsInodeMetadataToken token) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus
    InvalidateInodeMetadata(const VfsInodeIdentity &identity) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus InvalidateDentry(const VfsDentryKey &key) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus
    InvalidateInode(const VfsInodeIdentity &identity) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus EvictDentries(uint64_t maximum_entry_count,
                                                        uint64_t &evicted_entry_count) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus EvictInodes(uint64_t maximum_entry_count,
                                                      uint64_t &evicted_entry_count) noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus ReadDentry(VfsDentryToken token,
                                                     VfsDentrySnapshot &snapshot) const noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus ReadInode(VfsInodeToken token,
                                                    VfsInodeSnapshot &snapshot) const noexcept;
    [[nodiscard]] VfsNamespaceCacheStatistics Statistics() const noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus Validate() const noexcept;
    [[nodiscard]] VfsNamespaceCacheStatus Destroy() noexcept;

  private:
    [[nodiscard]] uint64_t FindCachedDentry(const VfsDentryKey &key) const noexcept;
    [[nodiscard]] uint64_t FindCachedInode(const VfsInodeIdentity &identity) const noexcept;
    [[nodiscard]] uint64_t FindFreeDentry() const noexcept;
    [[nodiscard]] uint64_t FindFreeInode() const noexcept;
    [[nodiscard]] uint64_t FindDentryEvictionCandidate() const noexcept;
    [[nodiscard]] uint64_t FindInodeEvictionCandidate() const noexcept;
    [[nodiscard]] bool InsertDentryIndex(uint64_t slot_index) noexcept;
    [[nodiscard]] bool InsertInodeIndex(uint64_t slot_index) noexcept;
    [[nodiscard]] bool RemoveDentryIndex(uint64_t slot_index) noexcept;
    [[nodiscard]] bool RemoveInodeIndex(uint64_t slot_index) noexcept;
    [[nodiscard]] bool HashIndexIsConfigured() const noexcept;
    [[nodiscard]] bool DentryTokenIsValid(VfsDentryToken token) const noexcept;
    [[nodiscard]] bool InodeTokenIsValid(VfsInodeToken token) const noexcept;
    [[nodiscard]] bool InodeMetadataTokenIsValid(VfsInodeMetadataToken token) const noexcept;
    [[nodiscard]] bool TouchDentry(VfsDentrySlot &dentry) noexcept;
    [[nodiscard]] bool TouchInode(VfsInodeSlot &inode) noexcept;
    [[nodiscard]] bool MarkDentryStale(uint64_t slot_index, bool cascaded) noexcept;
    [[nodiscard]] bool ReleaseDentrySlot(uint64_t slot_index, bool stale_release,
                                         bool eviction) noexcept;
    [[nodiscard]] bool ReleaseInodeSlot(uint64_t slot_index, bool stale_release,
                                        bool eviction) noexcept;
    [[nodiscard]] VfsDentrySnapshot SnapshotDentry(uint64_t slot_index) const noexcept;
    [[nodiscard]] VfsInodeSnapshot SnapshotInode(uint64_t slot_index) const noexcept;
    [[nodiscard]] VfsInodeMetadataSnapshot
    SnapshotInodeMetadata(uint64_t slot_index) const noexcept;
    [[nodiscard]] uint64_t NextAccessGeneration() noexcept;

    mutable SpinLock lock_{};
    VfsDentrySlot *dentries_{};
    uint64_t dentry_capacity_{};
    VfsInodeSlot *inodes_{};
    uint64_t inode_capacity_{};
    VfsNamespaceHashEntry *dentry_hash_entries_{};
    uint64_t *dentry_hash_buckets_{};
    uint64_t dentry_hash_bucket_capacity_{};
    VfsNamespaceHashEntry *inode_hash_entries_{};
    uint64_t *inode_hash_buckets_{};
    uint64_t inode_hash_bucket_capacity_{};
    uint64_t access_generation_{};
    VfsNamespaceCacheStatistics statistics_{};
    bool initialized_{};
};

[[nodiscard]] bool VfsInodeIdentityIsValid(const VfsInodeIdentity &identity) noexcept;
[[nodiscard]] bool VfsInodeIdentitiesEqual(const VfsInodeIdentity &left,
                                           const VfsInodeIdentity &right) noexcept;
[[nodiscard]] bool VfsInodeMetadataIsValid(const BackendNodeInformation &metadata,
                                           NodeType type) noexcept;
[[nodiscard]] uint64_t VfsInodeIdentityHash(const VfsInodeIdentity &identity) noexcept;
[[nodiscard]] bool VfsDentryKeyIsValid(const VfsDentryKey &key) noexcept;
[[nodiscard]] bool VfsDentryKeysEqual(const VfsDentryKey &left, const VfsDentryKey &right) noexcept;
[[nodiscard]] uint64_t VfsDentryKeyHash(const VfsDentryKey &key) noexcept;
[[nodiscard]] VfsNamespaceCacheStatus
BuildVfsDentryKey(uint64_t mount_identifier, const VfsInodeIdentity &parent, const uint8_t *name,
                  uint64_t name_length_bytes, VfsDentryKey &key) noexcept;

}
