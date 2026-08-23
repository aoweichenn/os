#include <os/kernel/fs/vfs_namespace_backing.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE = 0ULL;

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE &&
           (value & (value - 1ULL)) == OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE;
}

[[nodiscard]] bool AlignUp(const uint64_t value, const uint64_t alignment,
                           uint64_t &aligned_value) noexcept {
    if (!IsPowerOfTwo(alignment)) {
        return false;
    }
    const uint64_t alignment_mask = alignment - 1ULL;
    if (value > UINT64_MAX - alignment_mask) {
        return false;
    }
    aligned_value = (value + alignment_mask) & ~alignment_mask;
    return true;
}

[[nodiscard]] bool ReserveArray(uint64_t &cursor_bytes, const uint64_t element_count,
                                const uint64_t element_size_bytes,
                                const uint64_t element_alignment_bytes,
                                uint64_t &offset_bytes) noexcept {
    if (element_count == OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE ||
        element_size_bytes == OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE ||
        element_count > UINT64_MAX / element_size_bytes ||
        !AlignUp(cursor_bytes, element_alignment_bytes, offset_bytes)) {
        return false;
    }
    const uint64_t size_bytes = element_count * element_size_bytes;
    if (offset_bytes > UINT64_MAX - size_bytes) {
        return false;
    }
    cursor_bytes = offset_bytes + size_bytes;
    return true;
}

[[nodiscard]] bool
ConfigurationIsValid(const VfsNamespaceBackingConfiguration &configuration) noexcept {
    return configuration.dentry_capacity != OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE &&
           configuration.inode_capacity != OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE &&
           configuration.preferred_dentry_bucket_capacity >= configuration.dentry_capacity &&
           configuration.preferred_inode_bucket_capacity >= configuration.inode_capacity &&
           configuration.compact_dentry_bucket_capacity >= configuration.dentry_capacity &&
           configuration.compact_inode_bucket_capacity >= configuration.inode_capacity &&
           configuration.preferred_dentry_bucket_capacity >=
               configuration.compact_dentry_bucket_capacity &&
           configuration.preferred_inode_bucket_capacity >=
               configuration.compact_inode_bucket_capacity &&
           configuration.resolution_context_capacity !=
               OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE &&
           IsPowerOfTwo(configuration.page_size_bytes) &&
           configuration.page_size_bytes >= alignof(VfsDentrySlot) &&
           configuration.page_size_bytes >= alignof(VfsInodeSlot) &&
           configuration.page_size_bytes >= alignof(VfsResolutionContext);
}

[[nodiscard]] bool RegionContains(const uint64_t offset_bytes, const uint64_t element_count,
                                  const uint64_t element_size_bytes,
                                  const uint64_t capacity_bytes) noexcept {
    return element_count <= UINT64_MAX / element_size_bytes && offset_bytes <= capacity_bytes &&
           element_count * element_size_bytes <= capacity_bytes - offset_bytes;
}

[[nodiscard]] bool LayoutsAreEqual(const VfsNamespaceBackingLayout &left,
                                   const VfsNamespaceBackingLayout &right) noexcept {
    return left.dentry_storage_offset_bytes == right.dentry_storage_offset_bytes &&
           left.inode_storage_offset_bytes == right.inode_storage_offset_bytes &&
           left.dentry_hash_entry_offset_bytes == right.dentry_hash_entry_offset_bytes &&
           left.inode_hash_entry_offset_bytes == right.inode_hash_entry_offset_bytes &&
           left.compact_dentry_bucket_offset_bytes == right.compact_dentry_bucket_offset_bytes &&
           left.compact_inode_bucket_offset_bytes == right.compact_inode_bucket_offset_bytes &&
           left.resolution_context_offset_bytes == right.resolution_context_offset_bytes &&
           left.stable_size_bytes == right.stable_size_bytes &&
           left.stable_page_count == right.stable_page_count &&
           left.preferred_dentry_bucket_offset_bytes ==
               right.preferred_dentry_bucket_offset_bytes &&
           left.preferred_inode_bucket_offset_bytes == right.preferred_inode_bucket_offset_bytes &&
           left.preferred_size_bytes == right.preferred_size_bytes &&
           left.preferred_page_count == right.preferred_page_count;
}

}

VfsNamespaceBackingStatus
CalculateVfsNamespaceBackingLayout(const VfsNamespaceBackingConfiguration &configuration,
                                   VfsNamespaceBackingLayout &layout) noexcept {
    layout = VfsNamespaceBackingLayout{};
    if (!ConfigurationIsValid(configuration)) {
        return VfsNamespaceBackingStatus::InvalidConfiguration;
    }

    uint64_t stable_cursor_bytes = OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE;
    if (!ReserveArray(stable_cursor_bytes, configuration.dentry_capacity, sizeof(VfsDentrySlot),
                      alignof(VfsDentrySlot), layout.dentry_storage_offset_bytes) ||
        !ReserveArray(stable_cursor_bytes, configuration.inode_capacity, sizeof(VfsInodeSlot),
                      alignof(VfsInodeSlot), layout.inode_storage_offset_bytes) ||
        !ReserveArray(stable_cursor_bytes, configuration.dentry_capacity,
                      sizeof(VfsNamespaceHashEntry), alignof(VfsNamespaceHashEntry),
                      layout.dentry_hash_entry_offset_bytes) ||
        !ReserveArray(stable_cursor_bytes, configuration.inode_capacity,
                      sizeof(VfsNamespaceHashEntry), alignof(VfsNamespaceHashEntry),
                      layout.inode_hash_entry_offset_bytes) ||
        !ReserveArray(stable_cursor_bytes, configuration.compact_dentry_bucket_capacity,
                      sizeof(uint64_t), alignof(uint64_t),
                      layout.compact_dentry_bucket_offset_bytes) ||
        !ReserveArray(stable_cursor_bytes, configuration.compact_inode_bucket_capacity,
                      sizeof(uint64_t), alignof(uint64_t),
                      layout.compact_inode_bucket_offset_bytes) ||
        !ReserveArray(stable_cursor_bytes, configuration.resolution_context_capacity,
                      sizeof(VfsResolutionContext), alignof(VfsResolutionContext),
                      layout.resolution_context_offset_bytes) ||
        !AlignUp(stable_cursor_bytes, configuration.page_size_bytes, layout.stable_size_bytes)) {
        layout = VfsNamespaceBackingLayout{};
        return VfsNamespaceBackingStatus::ArithmeticOverflow;
    }
    layout.stable_page_count = layout.stable_size_bytes / configuration.page_size_bytes;

    uint64_t preferred_cursor_bytes = OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE;
    if (!ReserveArray(preferred_cursor_bytes, configuration.preferred_dentry_bucket_capacity,
                      sizeof(uint64_t), alignof(uint64_t),
                      layout.preferred_dentry_bucket_offset_bytes) ||
        !ReserveArray(preferred_cursor_bytes, configuration.preferred_inode_bucket_capacity,
                      sizeof(uint64_t), alignof(uint64_t),
                      layout.preferred_inode_bucket_offset_bytes) ||
        !AlignUp(preferred_cursor_bytes, configuration.page_size_bytes,
                 layout.preferred_size_bytes)) {
        layout = VfsNamespaceBackingLayout{};
        return VfsNamespaceBackingStatus::ArithmeticOverflow;
    }
    layout.preferred_page_count = layout.preferred_size_bytes / configuration.page_size_bytes;
    return VfsNamespaceBackingStatus::Succeeded;
}

VfsNamespaceBackingStatus
BuildVfsNamespaceBackingView(const VfsNamespaceBackingConfiguration &configuration,
                             const VfsNamespaceBackingLayout &layout, const uint64_t stable_address,
                             const uint64_t stable_capacity_bytes, const uint64_t preferred_address,
                             const uint64_t preferred_capacity_bytes,
                             VfsNamespaceBackingView &view) noexcept {
    view = VfsNamespaceBackingView{};
    if (!ConfigurationIsValid(configuration)) {
        return VfsNamespaceBackingStatus::InvalidConfiguration;
    }
    VfsNamespaceBackingLayout expected_layout{};
    if (CalculateVfsNamespaceBackingLayout(configuration, expected_layout) !=
        VfsNamespaceBackingStatus::Succeeded) {
        return VfsNamespaceBackingStatus::ArithmeticOverflow;
    }
    if (stable_address == OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE ||
        preferred_address == OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE) {
        return VfsNamespaceBackingStatus::InvalidStorage;
    }
    if (stable_address % configuration.page_size_bytes !=
            OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE ||
        preferred_address % configuration.page_size_bytes !=
            OS_KERNEL_VFS_NAMESPACE_BACKING_EMPTY_VALUE) {
        return VfsNamespaceBackingStatus::MisalignedStorage;
    }
    if (!LayoutsAreEqual(layout, expected_layout) ||
        stable_capacity_bytes < layout.stable_size_bytes ||
        preferred_capacity_bytes < layout.preferred_size_bytes) {
        return VfsNamespaceBackingStatus::InsufficientStorage;
    }
    if (stable_address > UINT64_MAX - stable_capacity_bytes ||
        preferred_address > UINT64_MAX - preferred_capacity_bytes) {
        return VfsNamespaceBackingStatus::ArithmeticOverflow;
    }
    if (!RegionContains(layout.dentry_storage_offset_bytes, configuration.dentry_capacity,
                        sizeof(VfsDentrySlot), stable_capacity_bytes) ||
        !RegionContains(layout.inode_storage_offset_bytes, configuration.inode_capacity,
                        sizeof(VfsInodeSlot), stable_capacity_bytes) ||
        !RegionContains(layout.dentry_hash_entry_offset_bytes, configuration.dentry_capacity,
                        sizeof(VfsNamespaceHashEntry), stable_capacity_bytes) ||
        !RegionContains(layout.inode_hash_entry_offset_bytes, configuration.inode_capacity,
                        sizeof(VfsNamespaceHashEntry), stable_capacity_bytes) ||
        !RegionContains(layout.compact_dentry_bucket_offset_bytes,
                        configuration.compact_dentry_bucket_capacity, sizeof(uint64_t),
                        stable_capacity_bytes) ||
        !RegionContains(layout.compact_inode_bucket_offset_bytes,
                        configuration.compact_inode_bucket_capacity, sizeof(uint64_t),
                        stable_capacity_bytes) ||
        !RegionContains(layout.resolution_context_offset_bytes,
                        configuration.resolution_context_capacity, sizeof(VfsResolutionContext),
                        stable_capacity_bytes) ||
        !RegionContains(layout.preferred_dentry_bucket_offset_bytes,
                        configuration.preferred_dentry_bucket_capacity, sizeof(uint64_t),
                        preferred_capacity_bytes) ||
        !RegionContains(layout.preferred_inode_bucket_offset_bytes,
                        configuration.preferred_inode_bucket_capacity, sizeof(uint64_t),
                        preferred_capacity_bytes)) {
        return VfsNamespaceBackingStatus::InsufficientStorage;
    }

    view = VfsNamespaceBackingView{
        .dentry_storage =
            reinterpret_cast<VfsDentrySlot *>(stable_address + layout.dentry_storage_offset_bytes),
        .inode_storage =
            reinterpret_cast<VfsInodeSlot *>(stable_address + layout.inode_storage_offset_bytes),
        .dentry_hash_entries = reinterpret_cast<VfsNamespaceHashEntry *>(
            stable_address + layout.dentry_hash_entry_offset_bytes),
        .inode_hash_entries = reinterpret_cast<VfsNamespaceHashEntry *>(
            stable_address + layout.inode_hash_entry_offset_bytes),
        .compact_dentry_hash_buckets = reinterpret_cast<uint64_t *>(
            stable_address + layout.compact_dentry_bucket_offset_bytes),
        .compact_inode_hash_buckets =
            reinterpret_cast<uint64_t *>(stable_address + layout.compact_inode_bucket_offset_bytes),
        .resolution_contexts = reinterpret_cast<VfsResolutionContext *>(
            stable_address + layout.resolution_context_offset_bytes),
        .preferred_dentry_hash_buckets = reinterpret_cast<uint64_t *>(
            preferred_address + layout.preferred_dentry_bucket_offset_bytes),
        .preferred_inode_hash_buckets = reinterpret_cast<uint64_t *>(
            preferred_address + layout.preferred_inode_bucket_offset_bytes),
    };
    return VfsNamespaceBackingStatus::Succeeded;
}

}
