#include <os/kernel/fs/vfs_namespace_cache.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_SUITE_NAME =
    "kernel/vfs_namespace_cache/unit";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_LIFECYCLE_MESSAGE =
    "正负 dentry、共享 inode、Stale 引用和 LRU 必须保持唯一生命周期";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_CAPACITY_MESSAGE =
    "容量拒绝与 generation 复用必须保持原状态并拒绝旧 token";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_LRU_MESSAGE =
    "完整名称校验、inode 类型冲突和零引用 dentry/inode LRU 顺序必须稳定";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_HASH_MESSAGE =
    "hash storage 必须校验容量，并在在线重建后保持发布、命中、失效和回收一致";
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_DENTRY_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_INODE_CAPACITY = 6ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_SMALL_CAPACITY = 1ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_LRU_DENTRY_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_LRU_INODE_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT = 0ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_PREFERRED_HASH_CAPACITY = 4ULL;

[[nodiscard]] os::kernel::fs::VfsInodeIdentity Identity(const uint64_t node_identifier,
                                                        const uint64_t generation = 1ULL) noexcept {
    return os::kernel::fs::VfsInodeIdentity{
        .superblock_identifier = 7ULL,
        .superblock_generation = 3ULL,
        .node_identifier = node_identifier,
        .node_generation = generation,
    };
}

template <uint64_t Length>
[[nodiscard]] bool
BuildKey(const uint64_t mount_identifier, const os::kernel::fs::VfsInodeIdentity &parent,
         const char (&name)[Length], os::kernel::fs::VfsDentryKey &key) noexcept {
    return os::kernel::fs::BuildVfsDentryKey(
               mount_identifier, parent, reinterpret_cast<const uint8_t *>(name), Length - 1ULL,
               key) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
}

[[nodiscard]] bool RunLifecycleScenario() noexcept {
    os::kernel::fs::VfsDentrySlot dentry_storage[OS_TEST_VFS_NAMESPACE_CACHE_DENTRY_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot inode_storage[OS_TEST_VFS_NAMESPACE_CACHE_INODE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    const os::kernel::fs::VfsInodeIdentity root = Identity(1ULL);
    const os::kernel::fs::VfsInodeIdentity file = Identity(2ULL);
    const os::kernel::fs::VfsInodeIdentity directory = Identity(3ULL);
    const os::kernel::fs::VfsInodeIdentity child = Identity(4ULL);
    os::kernel::fs::VfsDentryKey file_key{};
    os::kernel::fs::VfsDentryKey alias_key{};
    os::kernel::fs::VfsDentryKey missing_key{};
    os::kernel::fs::VfsDentryKey directory_key{};
    os::kernel::fs::VfsDentryKey child_key{};
    os::kernel::fs::VfsDentryKey child_missing_key{};
    if (!BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "file", file_key) ||
        !BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "alias", alias_key) ||
        !BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "missing", missing_key) ||
        !BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "directory", directory_key) ||
        !BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, directory, "child", child_key) ||
        !BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, directory, "absent", child_missing_key)) {
        return false;
    }
    os::kernel::fs::VfsDentryToken file_token{};
    os::kernel::fs::VfsDentryToken alias_token{};
    os::kernel::fs::VfsDentryToken missing_token{};
    os::kernel::fs::VfsDentrySnapshot file_snapshot{};
    os::kernel::fs::VfsDentrySnapshot missing_snapshot{};
    os::kernel::fs::VfsInodeSnapshot inode_snapshot{};
    bool consistent =
        cache.Initialize(dentry_storage, OS_TEST_VFS_NAMESPACE_CACHE_DENTRY_CAPACITY, inode_storage,
                         OS_TEST_VFS_NAMESPACE_CACHE_INODE_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(file_key, file, os::kernel::fs::NodeType::RegularFile, file_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(alias_key, file, os::kernel::fs::NodeType::RegularFile,
                              alias_token) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishNegative(missing_key, missing_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(file_key, file_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(file_key, file_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        file_snapshot.external_reference_count == 2ULL &&
        file_snapshot.kind == os::kernel::fs::VfsDentryKind::Positive &&
        cache.AcquireDentry(missing_key, missing_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        missing_snapshot.kind == os::kernel::fs::VfsDentryKind::Negative &&
        cache.AcquireInode(file, inode_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        inode_snapshot.dentry_reference_count == 2ULL &&
        cache.InvalidateDentry(file_key) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(file_key, file_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound;
    const os::kernel::fs::VfsDentryToken stale_file_token = file_token;
    os::kernel::fs::VfsDentryToken replacement_token{};
    consistent =
        consistent &&
        cache.PublishNegative(file_key, replacement_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReadDentry(stale_file_token, file_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        file_snapshot.state == os::kernel::fs::VfsNamespaceEntryState::Stale &&
        cache.ReleaseDentry(stale_file_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReleaseDentry(stale_file_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReadDentry(stale_file_token, file_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidToken &&
        cache.ReleaseDentry(missing_snapshot.token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReleaseInode(inode_snapshot.token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(file_key, file, os::kernel::fs::NodeType::RegularFile, file_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::EntryConflict;

    os::kernel::fs::VfsDentryToken directory_token{};
    os::kernel::fs::VfsDentryToken child_token{};
    os::kernel::fs::VfsDentryToken child_missing_token{};
    os::kernel::fs::VfsDentrySnapshot directory_snapshot{};
    os::kernel::fs::VfsInodeSnapshot directory_inode_snapshot{};
    consistent =
        consistent &&
        cache.PublishPositive(directory_key, directory, os::kernel::fs::NodeType::Directory,
                              directory_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(child_key, child, os::kernel::fs::NodeType::RegularFile,
                              child_token) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishNegative(child_missing_key, child_missing_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(directory_key, directory_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireInode(directory, directory_inode_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.InvalidateInode(directory) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(directory_key, file_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound &&
        cache.AcquireDentry(child_key, file_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound &&
        cache.AcquireDentry(child_missing_key, file_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound &&
        cache.ReadDentry(directory_snapshot.token, directory_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        directory_snapshot.state == os::kernel::fs::VfsNamespaceEntryState::Stale &&
        cache.ReadInode(directory_inode_snapshot.token, directory_inode_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        directory_inode_snapshot.state == os::kernel::fs::VfsNamespaceEntryState::Stale &&
        cache.ReleaseDentry(directory_snapshot.token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReleaseInode(directory_inode_snapshot.token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;

    uint64_t evicted_dentry_count = OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t evicted_inode_count = OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    consistent =
        consistent &&
        cache.EvictDentries(UINT64_MAX, evicted_dentry_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        evicted_dentry_count != OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
        cache.EvictInodes(UINT64_MAX, evicted_inode_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        evicted_inode_count != OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
        cache.Statistics().active_dentry_count == OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
        cache.Statistics().active_inode_count == OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
        cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    return consistent;
}

[[nodiscard]] bool RunCapacityScenario() noexcept {
    os::kernel::fs::VfsDentrySlot dentry_storage[OS_TEST_VFS_NAMESPACE_CACHE_SMALL_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot inode_storage[OS_TEST_VFS_NAMESPACE_CACHE_SMALL_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    const os::kernel::fs::VfsInodeIdentity root = Identity(11ULL);
    const os::kernel::fs::VfsInodeIdentity first_inode = Identity(12ULL);
    os::kernel::fs::VfsDentryKey first_key{};
    os::kernel::fs::VfsDentryKey second_key{};
    if (!BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "first", first_key) ||
        !BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "second", second_key)) {
        return false;
    }
    os::kernel::fs::VfsDentryToken first_token{};
    os::kernel::fs::VfsDentryToken rejected_token{};
    os::kernel::fs::VfsDentrySnapshot snapshot{};
    bool consistent =
        cache.Initialize(dentry_storage, OS_TEST_VFS_NAMESPACE_CACHE_SMALL_CAPACITY, inode_storage,
                         OS_TEST_VFS_NAMESPACE_CACHE_SMALL_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(first_key, first_inode, os::kernel::fs::NodeType::RegularFile,
                              first_token) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishNegative(second_key, rejected_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::CapacityExhausted &&
        cache.Statistics().active_dentry_count == 1ULL &&
        cache.Statistics().active_inode_count == 1ULL &&
        cache.AcquireDentry(first_key, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.InvalidateDentry(first_key) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishNegative(first_key, rejected_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::CapacityExhausted &&
        cache.ReleaseDentry(snapshot.token) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReadDentry(first_token, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidToken &&
        cache.PublishNegative(first_key, rejected_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReadDentry(first_token, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidToken &&
        cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    uint64_t evicted_count = OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    consistent = consistent &&
                 cache.EvictDentries(UINT64_MAX, evicted_count) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 evicted_count == 1ULL &&
                 cache.EvictInodes(UINT64_MAX, evicted_count) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 evicted_count == 1ULL &&
                 cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    return consistent;
}

[[nodiscard]] bool RunKeyAndLruScenario() noexcept {
    os::kernel::fs::VfsDentrySlot dentry_storage[OS_TEST_VFS_NAMESPACE_CACHE_LRU_DENTRY_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot inode_storage[OS_TEST_VFS_NAMESPACE_CACHE_LRU_INODE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    const os::kernel::fs::VfsInodeIdentity root = Identity(21ULL);
    const os::kernel::fs::VfsInodeIdentity file = Identity(22ULL);
    os::kernel::fs::VfsDentryKey first_key{};
    os::kernel::fs::VfsDentryKey second_key{};
    os::kernel::fs::VfsDentryKey third_key{};
    os::kernel::fs::VfsDentryKey invalid_key{};
    uint8_t maximum_name[os::kernel::fs::OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES]{};
    for (uint64_t byte_index = OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         byte_index < sizeof(maximum_name); ++byte_index) {
        maximum_name[byte_index] = static_cast<uint8_t>('a');
    }
    const uint8_t dot_name[]{static_cast<uint8_t>('.')};
    const uint8_t separator_name[]{static_cast<uint8_t>('a'), static_cast<uint8_t>('/'),
                                   static_cast<uint8_t>('b')};
    bool consistent =
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "first", first_key) &&
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "second", second_key) &&
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "third", third_key) &&
        os::kernel::fs::BuildVfsDentryKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root,
                                          maximum_name, sizeof(maximum_name), invalid_key) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        os::kernel::fs::BuildVfsDentryKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, dot_name,
                                          sizeof(dot_name), invalid_key) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidKey &&
        os::kernel::fs::BuildVfsDentryKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root,
                                          separator_name, sizeof(separator_name), invalid_key) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidKey &&
        cache.Initialize(dentry_storage, OS_TEST_VFS_NAMESPACE_CACHE_LRU_DENTRY_CAPACITY,
                         inode_storage, OS_TEST_VFS_NAMESPACE_CACHE_LRU_INODE_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    os::kernel::fs::VfsDentryToken first_token{};
    os::kernel::fs::VfsDentryToken second_token{};
    os::kernel::fs::VfsDentryToken third_token{};
    os::kernel::fs::VfsDentryToken repeated_token{};
    os::kernel::fs::VfsDentrySnapshot snapshot{};
    consistent =
        consistent &&
        cache.PublishNegative(first_key, first_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(second_key, file, os::kernel::fs::NodeType::RegularFile,
                              second_token) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(second_key, file, os::kernel::fs::NodeType::RegularFile,
                              repeated_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::AlreadyCached &&
        repeated_token.slot_index == second_token.slot_index &&
        repeated_token.generation == second_token.generation &&
        cache.PublishPositive(third_key, file, os::kernel::fs::NodeType::Directory,
                              repeated_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::EntryConflict &&
        cache.PublishNegative(third_key, third_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(first_key, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReleaseDentry(snapshot.token) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    uint64_t evicted_count = OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    consistent =
        consistent &&
        cache.EvictDentries(1ULL, evicted_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        evicted_count == 1ULL &&
        cache.AcquireDentry(second_key, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound &&
        cache.AcquireDentry(first_key, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReleaseDentry(snapshot.token) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(third_key, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReleaseDentry(snapshot.token) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.EvictDentries(UINT64_MAX, evicted_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        evicted_count == 2ULL &&
        cache.EvictInodes(1ULL, evicted_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        evicted_count == 1ULL &&
        cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    return consistent;
}

[[nodiscard]] bool RunHashConfigurationScenario() noexcept {
    os::kernel::fs::VfsDentrySlot dentry_storage[OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot inode_storage[OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY]{};
    os::kernel::fs::VfsNamespaceHashEntry
        dentry_hash_entries[OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY]{};
    uint64_t dentry_hash_buckets[OS_TEST_VFS_NAMESPACE_CACHE_PREFERRED_HASH_CAPACITY]{};
    uint64_t compact_dentry_hash_buckets[OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY]{};
    os::kernel::fs::VfsNamespaceHashEntry
        inode_hash_entries[OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY]{};
    uint64_t inode_hash_buckets[OS_TEST_VFS_NAMESPACE_CACHE_PREFERRED_HASH_CAPACITY]{};
    uint64_t compact_inode_hash_buckets[OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    const os::kernel::fs::VfsInodeIdentity root = Identity(31ULL);
    const os::kernel::fs::VfsInodeIdentity file = Identity(32ULL);
    os::kernel::fs::VfsDentryKey positive_key{};
    os::kernel::fs::VfsDentryKey negative_key{};
    os::kernel::fs::VfsDentryToken token{};
    bool consistent =
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "indexed", positive_key) &&
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ROOT_MOUNT, root, "absent", negative_key) &&
        cache.Initialize(dentry_storage, OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY, inode_storage,
                         OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ConfigureHashIndex(dentry_hash_entries, OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY,
                                 dentry_hash_buckets, OS_TEST_VFS_NAMESPACE_CACHE_SMALL_CAPACITY,
                                 inode_hash_entries, OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY,
                                 inode_hash_buckets, OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidCapacity &&
        cache.ConfigureHashIndex(
            dentry_hash_entries, OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY, dentry_hash_buckets,
            OS_TEST_VFS_NAMESPACE_CACHE_PREFERRED_HASH_CAPACITY, inode_hash_entries,
            OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY, inode_hash_buckets,
            OS_TEST_VFS_NAMESPACE_CACHE_PREFERRED_HASH_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ConfigureHashIndex(
            dentry_hash_entries, OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY, dentry_hash_buckets,
            OS_TEST_VFS_NAMESPACE_CACHE_PREFERRED_HASH_CAPACITY, inode_hash_entries,
            OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY, inode_hash_buckets,
            OS_TEST_VFS_NAMESPACE_CACHE_PREFERRED_HASH_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::AlreadyInitialized &&
        cache.PublishPositive(positive_key, file, os::kernel::fs::NodeType::RegularFile, token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishNegative(negative_key, token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.RebuildHashBuckets(
            compact_dentry_hash_buckets, OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY,
            compact_inode_hash_buckets, OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Statistics().hash_rebuild_count == 1ULL &&
        cache.Statistics().dentry_hash_bucket_capacity ==
            OS_TEST_VFS_NAMESPACE_CACHE_HASH_CAPACITY &&
        cache.InvalidateDentry(positive_key) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    uint64_t evicted_count = OS_TEST_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    consistent = consistent &&
                 cache.EvictDentries(UINT64_MAX, evicted_count) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.EvictInodes(UINT64_MAX, evicted_count) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    return consistent;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_NAMESPACE_CACHE_SUITE_NAME};
    test_context.Expect(RunLifecycleScenario(), OS_TEST_VFS_NAMESPACE_CACHE_LIFECYCLE_MESSAGE);
    test_context.Expect(RunCapacityScenario(), OS_TEST_VFS_NAMESPACE_CACHE_CAPACITY_MESSAGE);
    test_context.Expect(RunKeyAndLruScenario(), OS_TEST_VFS_NAMESPACE_CACHE_LRU_MESSAGE);
    test_context.Expect(RunHashConfigurationScenario(), OS_TEST_VFS_NAMESPACE_CACHE_HASH_MESSAGE);
    return test_context.ExitCode();
}
