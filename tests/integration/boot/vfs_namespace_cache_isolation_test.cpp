#include <os/kernel/fs/vfs_namespace_cache.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_SUITE_NAME =
    "kernel/vfs_namespace_cache_isolation/integration";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_MOUNT_MESSAGE =
    "相同 parent/name 必须按 mount 隔离，同时共享同一 superblock inode 身份";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_RENAME_MESSAGE =
    "rename 风格失效必须允许旧 Stale token 与新正负 dentry 同时存在";
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_DENTRY_CAPACITY = 12ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_INODE_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_FIRST_MOUNT = 0ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_SECOND_MOUNT = 1ULL;

[[nodiscard]] os::kernel::fs::VfsInodeIdentity Identity(const uint64_t node_identifier,
                                                        const uint64_t generation = 1ULL) noexcept {
    return os::kernel::fs::VfsInodeIdentity{
        .superblock_identifier = 17ULL,
        .superblock_generation = 5ULL,
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

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_SUITE_NAME};
    os::kernel::fs::VfsDentrySlot
        dentry_storage[OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_DENTRY_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot
        inode_storage[OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_INODE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    const os::kernel::fs::VfsInodeIdentity root = Identity(1ULL);
    const os::kernel::fs::VfsInodeIdentity shared_file = Identity(2ULL);
    const os::kernel::fs::VfsInodeIdentity replacement_file = Identity(3ULL);
    os::kernel::fs::VfsDentryKey mount_zero_key{};
    os::kernel::fs::VfsDentryKey mount_one_key{};
    os::kernel::fs::VfsDentryKey renamed_key{};
    os::kernel::fs::VfsDentryKey negative_mount_zero_key{};
    os::kernel::fs::VfsDentryKey negative_mount_one_key{};
    bool consistent =
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_FIRST_MOUNT, root, "shared",
                 mount_zero_key) &&
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_SECOND_MOUNT, root, "shared",
                 mount_one_key) &&
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_FIRST_MOUNT, root, "renamed",
                 renamed_key) &&
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_FIRST_MOUNT, root, "absent",
                 negative_mount_zero_key) &&
        BuildKey(OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_SECOND_MOUNT, root, "absent",
                 negative_mount_one_key) &&
        cache.Initialize(dentry_storage, OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_DENTRY_CAPACITY,
                         inode_storage, OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_INODE_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    os::kernel::fs::VfsDentryToken mount_zero_token{};
    os::kernel::fs::VfsDentryToken mount_one_token{};
    os::kernel::fs::VfsDentryToken negative_token{};
    os::kernel::fs::VfsDentrySnapshot mount_zero_snapshot{};
    os::kernel::fs::VfsDentrySnapshot mount_one_snapshot{};
    consistent =
        consistent &&
        cache.PublishPositive(mount_zero_key, shared_file, os::kernel::fs::NodeType::RegularFile,
                              mount_zero_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(mount_one_key, shared_file, os::kernel::fs::NodeType::RegularFile,
                              mount_one_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishNegative(negative_mount_zero_key, negative_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(mount_zero_key, mount_zero_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(mount_one_key, mount_one_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        mount_zero_snapshot.token.slot_index != mount_one_snapshot.token.slot_index &&
        mount_zero_snapshot.inode_token.slot_index == mount_one_snapshot.inode_token.slot_index &&
        mount_zero_snapshot.inode_token.generation == mount_one_snapshot.inode_token.generation &&
        cache.AcquireDentry(negative_mount_one_key, mount_one_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound &&
        cache.InvalidateDentry(negative_mount_zero_key) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(negative_mount_one_key, mount_one_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound;
    test_context.Expect(consistent, OS_TEST_VFS_NAMESPACE_CACHE_MOUNT_MESSAGE);

    const os::kernel::fs::VfsDentryToken stale_source_token = mount_zero_snapshot.token;
    os::kernel::fs::VfsDentryToken renamed_token{};
    os::kernel::fs::VfsDentryToken old_negative_token{};
    os::kernel::fs::VfsDentrySnapshot stale_snapshot{};
    consistent = consistent &&
                 cache.InvalidateDentry(mount_zero_key) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.PublishNegative(mount_zero_key, old_negative_token) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.PublishPositive(renamed_key, replacement_file,
                                       os::kernel::fs::NodeType::RegularFile, renamed_token) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.ReadDentry(stale_source_token, stale_snapshot) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 stale_snapshot.state == os::kernel::fs::VfsNamespaceEntryState::Stale &&
                 stale_snapshot.kind == os::kernel::fs::VfsDentryKind::Positive &&
                 cache.AcquireDentry(mount_zero_key, stale_snapshot) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 stale_snapshot.kind == os::kernel::fs::VfsDentryKind::Negative &&
                 cache.AcquireDentry(renamed_key, stale_snapshot) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 stale_snapshot.kind == os::kernel::fs::VfsDentryKind::Positive &&
                 cache.ReleaseDentry(stale_source_token) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.ReleaseDentry(mount_one_snapshot.token) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::InvalidToken;
    // mount-one 的正项引用保存在第一次 snapshot，第二次 miss 已覆盖变量，因此按原 token 释放。
    consistent = consistent &&
                 cache.ReleaseDentry(mount_one_token) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.ReleaseDentry(stale_snapshot.token) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.ReleaseDentry(old_negative_token) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.AcquireDentry(mount_zero_key, stale_snapshot) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.ReleaseDentry(stale_snapshot.token) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    test_context.Expect(consistent, OS_TEST_VFS_NAMESPACE_CACHE_RENAME_MESSAGE);

    uint64_t evicted_count = OS_TEST_VFS_NAMESPACE_CACHE_ISOLATION_EMPTY_VALUE;
    consistent = consistent &&
                 cache.EvictDentries(UINT64_MAX, evicted_count) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.EvictInodes(UINT64_MAX, evicted_count) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    test_context.Expect(consistent, OS_TEST_VFS_NAMESPACE_CACHE_RENAME_MESSAGE);
    return test_context.ExitCode();
}
