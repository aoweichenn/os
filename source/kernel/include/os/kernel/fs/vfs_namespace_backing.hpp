#pragma once

#include <os/kernel/fs/vfs_namespace_cache.hpp>

#include <stdint.h>

namespace os::kernel::fs {

struct VfsNamespaceBackingConfiguration final {
    uint64_t dentry_capacity;
    uint64_t inode_capacity;
    uint64_t preferred_dentry_bucket_capacity;
    uint64_t preferred_inode_bucket_capacity;
    uint64_t compact_dentry_bucket_capacity;
    uint64_t compact_inode_bucket_capacity;
    uint64_t resolution_context_capacity;
    uint64_t page_size_bytes;
};

struct VfsNamespaceBackingLayout final {
    uint64_t dentry_storage_offset_bytes;
    uint64_t inode_storage_offset_bytes;
    uint64_t dentry_hash_entry_offset_bytes;
    uint64_t inode_hash_entry_offset_bytes;
    uint64_t compact_dentry_bucket_offset_bytes;
    uint64_t compact_inode_bucket_offset_bytes;
    uint64_t resolution_context_offset_bytes;
    uint64_t stable_size_bytes;
    uint64_t stable_page_count;
    uint64_t preferred_dentry_bucket_offset_bytes;
    uint64_t preferred_inode_bucket_offset_bytes;
    uint64_t preferred_size_bytes;
    uint64_t preferred_page_count;
};

struct VfsNamespaceBackingView final {
    VfsDentrySlot *dentry_storage;
    VfsInodeSlot *inode_storage;
    VfsNamespaceHashEntry *dentry_hash_entries;
    VfsNamespaceHashEntry *inode_hash_entries;
    uint64_t *compact_dentry_hash_buckets;
    uint64_t *compact_inode_hash_buckets;
    VfsResolutionContext *resolution_contexts;
    uint64_t *preferred_dentry_hash_buckets;
    uint64_t *preferred_inode_hash_buckets;
};

enum class VfsNamespaceBackingStatus : uint64_t {
    Succeeded,
    InvalidConfiguration,
    ArithmeticOverflow,
    InvalidStorage,
    InsufficientStorage,
    MisalignedStorage,
};

[[nodiscard]] VfsNamespaceBackingStatus
CalculateVfsNamespaceBackingLayout(const VfsNamespaceBackingConfiguration &configuration,
                                   VfsNamespaceBackingLayout &layout) noexcept;
[[nodiscard]] VfsNamespaceBackingStatus BuildVfsNamespaceBackingView(
    const VfsNamespaceBackingConfiguration &configuration, const VfsNamespaceBackingLayout &layout,
    uint64_t stable_address, uint64_t stable_capacity_bytes, uint64_t preferred_address,
    uint64_t preferred_capacity_bytes, VfsNamespaceBackingView &view) noexcept;

}
