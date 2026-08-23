#include <os/kernel/fs/vfs_namespace_cache.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_INODE_METADATA_CACHE_SUITE_NAME =
    "kernel/vfs_inode_metadata_cache/unit";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_CACHE_LOAD_MESSAGE =
    "元数据 owner、并发加载、提交、命中、失效和 ABA 必须保持单一票据";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_CACHE_DENTRY_MESSAGE =
    "metadata 失效不得破坏正 dentry 持有的 inode identity";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_CACHE_CAPACITY_MESSAGE =
    "Loading inode 不得被回收，容量拒绝与取消必须保持失败原子性";
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_DENTRY_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_INODE_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_SINGLE_CAPACITY = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_ONE_ENTRY = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_SUPERBLOCK_IDENTIFIER = 31ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_SUPERBLOCK_GENERATION = 7ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_ROOT_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_FILE_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_SECOND_FILE_IDENTIFIER = 3ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_NODE_GENERATION = 5ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_ALLOCATED_SIZE_BYTES = 8192ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_LINK_COUNT = 2ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_ACCESS_TIME_NANOSECONDS = 11ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_MODIFICATION_TIME_NANOSECONDS = 12ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_CHANGE_TIME_NANOSECONDS = 13ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_CACHE_BIRTH_TIME_NANOSECONDS = 10ULL;
constexpr os::abi::UserIdentifier OS_TEST_VFS_INODE_METADATA_CACHE_USER_IDENTIFIER = 1000U;
constexpr os::abi::GroupIdentifier OS_TEST_VFS_INODE_METADATA_CACHE_GROUP_IDENTIFIER = 100U;
constexpr os::abi::FileMode OS_TEST_VFS_INODE_METADATA_CACHE_FILE_MODE =
    os::abi::OS_ABI_FILE_MODE_REGULAR | 0000640U;
constexpr os::abi::FileMode OS_TEST_VFS_INODE_METADATA_CACHE_INVALID_MODE =
    os::abi::OS_ABI_FILE_MODE_DIRECTORY | 0000750U;

[[nodiscard]] os::kernel::fs::VfsInodeIdentity Identity(
    const uint64_t node_identifier) noexcept {
    return os::kernel::fs::VfsInodeIdentity{
        .superblock_identifier = OS_TEST_VFS_INODE_METADATA_CACHE_SUPERBLOCK_IDENTIFIER,
        .superblock_generation = OS_TEST_VFS_INODE_METADATA_CACHE_SUPERBLOCK_GENERATION,
        .node_identifier = node_identifier,
        .node_generation = OS_TEST_VFS_INODE_METADATA_CACHE_NODE_GENERATION,
    };
}

[[nodiscard]] os::kernel::fs::BackendNodeInformation Metadata(
    const os::abi::FileMode mode = OS_TEST_VFS_INODE_METADATA_CACHE_FILE_MODE) noexcept {
    return os::kernel::fs::BackendNodeInformation{
        .size_bytes = OS_TEST_VFS_INODE_METADATA_CACHE_SIZE_BYTES,
        .allocated_size_bytes = OS_TEST_VFS_INODE_METADATA_CACHE_ALLOCATED_SIZE_BYTES,
        .link_count = OS_TEST_VFS_INODE_METADATA_CACHE_LINK_COUNT,
        .access_time_nanoseconds = OS_TEST_VFS_INODE_METADATA_CACHE_ACCESS_TIME_NANOSECONDS,
        .modification_time_nanoseconds =
            OS_TEST_VFS_INODE_METADATA_CACHE_MODIFICATION_TIME_NANOSECONDS,
        .change_time_nanoseconds = OS_TEST_VFS_INODE_METADATA_CACHE_CHANGE_TIME_NANOSECONDS,
        .birth_time_nanoseconds = OS_TEST_VFS_INODE_METADATA_CACHE_BIRTH_TIME_NANOSECONDS,
        .owner_user_identifier = OS_TEST_VFS_INODE_METADATA_CACHE_USER_IDENTIFIER,
        .owner_group_identifier = OS_TEST_VFS_INODE_METADATA_CACHE_GROUP_IDENTIFIER,
        .mode = mode,
    };
}

[[nodiscard]] bool RunLoadLifecycleScenario() noexcept {
    os::kernel::fs::VfsDentrySlot
        dentry_storage[OS_TEST_VFS_INODE_METADATA_CACHE_DENTRY_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot
        inode_storage[OS_TEST_VFS_INODE_METADATA_CACHE_INODE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    const os::kernel::fs::VfsInodeIdentity file =
        Identity(OS_TEST_VFS_INODE_METADATA_CACHE_FILE_IDENTIFIER);
    os::kernel::fs::VfsInodeMetadataToken first_token{};
    os::kernel::fs::VfsInodeMetadataToken contention_token{};
    os::kernel::fs::VfsInodeMetadataSnapshot snapshot{};
    bool consistent =
        cache.Initialize(dentry_storage, OS_TEST_VFS_INODE_METADATA_CACHE_DENTRY_CAPACITY,
                         inode_storage, OS_TEST_VFS_INODE_METADATA_CACHE_INODE_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PrepareInodeMetadata(file, os::kernel::fs::NodeType::RegularFile, first_token,
                                   snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadRequired &&
        cache.PrepareInodeMetadata(file, os::kernel::fs::NodeType::RegularFile,
                                   contention_token, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadInProgress &&
        cache.CompleteInodeMetadata(first_token,
                                    Metadata(OS_TEST_VFS_INODE_METADATA_CACHE_INVALID_MODE)) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidMetadata &&
        cache.CompleteInodeMetadata(first_token, Metadata()) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PrepareInodeMetadata(file, os::kernel::fs::NodeType::RegularFile,
                                   contention_token, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::AlreadyCached &&
        snapshot.state == os::kernel::fs::VfsInodeMetadataState::Ready &&
        snapshot.metadata.size_bytes == OS_TEST_VFS_INODE_METADATA_CACHE_SIZE_BYTES &&
        snapshot.metadata.mode == OS_TEST_VFS_INODE_METADATA_CACHE_FILE_MODE &&
        cache.InvalidateInodeMetadata(file) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.CompleteInodeMetadata(first_token, Metadata()) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidToken;

    os::kernel::fs::VfsInodeMetadataToken replacement_token{};
    consistent =
        consistent &&
        cache.PrepareInodeMetadata(file, os::kernel::fs::NodeType::RegularFile,
                                   replacement_token, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadRequired &&
        (replacement_token.inode_token.generation != first_token.inode_token.generation ||
         replacement_token.metadata_generation != first_token.metadata_generation) &&
        cache.CompleteInodeMetadata(replacement_token, Metadata()) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    uint64_t evicted_count = OS_TEST_VFS_INODE_METADATA_CACHE_EMPTY_VALUE;
    consistent = consistent &&
                 cache.EvictInodes(OS_TEST_VFS_INODE_METADATA_CACHE_ONE_ENTRY, evicted_count) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                 evicted_count == OS_TEST_VFS_INODE_METADATA_CACHE_ONE_ENTRY &&
                 cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    return consistent;
}

[[nodiscard]] bool RunDentryRetentionScenario() noexcept {
    os::kernel::fs::VfsDentrySlot
        dentry_storage[OS_TEST_VFS_INODE_METADATA_CACHE_DENTRY_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot
        inode_storage[OS_TEST_VFS_INODE_METADATA_CACHE_INODE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    const os::kernel::fs::VfsInodeIdentity root =
        Identity(OS_TEST_VFS_INODE_METADATA_CACHE_ROOT_IDENTIFIER);
    const os::kernel::fs::VfsInodeIdentity file =
        Identity(OS_TEST_VFS_INODE_METADATA_CACHE_FILE_IDENTIFIER);
    constexpr uint8_t OS_TEST_VFS_INODE_METADATA_CACHE_NAME[] = {
        static_cast<uint8_t>('f'),
        static_cast<uint8_t>('i'),
        static_cast<uint8_t>('l'),
        static_cast<uint8_t>('e'),
    };
    os::kernel::fs::VfsDentryKey key{};
    os::kernel::fs::VfsDentryToken dentry_token{};
    os::kernel::fs::VfsInodeMetadataToken first_token{};
    os::kernel::fs::VfsInodeMetadataToken second_token{};
    os::kernel::fs::VfsInodeMetadataSnapshot snapshot{};
    os::kernel::fs::VfsDentrySnapshot dentry_snapshot{};
    bool consistent =
        cache.Initialize(dentry_storage, OS_TEST_VFS_INODE_METADATA_CACHE_DENTRY_CAPACITY,
                         inode_storage, OS_TEST_VFS_INODE_METADATA_CACHE_INODE_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        os::kernel::fs::BuildVfsDentryKey(
            OS_TEST_VFS_INODE_METADATA_CACHE_EMPTY_VALUE, root,
            OS_TEST_VFS_INODE_METADATA_CACHE_NAME,
            sizeof(OS_TEST_VFS_INODE_METADATA_CACHE_NAME), key) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PublishPositive(key, file, os::kernel::fs::NodeType::RegularFile, dentry_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PrepareInodeMetadata(file, os::kernel::fs::NodeType::RegularFile, first_token,
                                   snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadRequired &&
        cache.CompleteInodeMetadata(first_token, Metadata()) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.InvalidateInodeMetadata(file) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.AcquireDentry(key, dentry_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ReleaseDentry(dentry_snapshot.token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PrepareInodeMetadata(file, os::kernel::fs::NodeType::RegularFile, second_token,
                                   snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadRequired &&
        second_token.inode_token.generation == first_token.inode_token.generation &&
        second_token.metadata_generation != first_token.metadata_generation &&
        cache.InvalidateInodeMetadata(file) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.CompleteInodeMetadata(second_token, Metadata()) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InvalidToken &&
        cache.InvalidateInode(file) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    return consistent;
}

[[nodiscard]] bool RunCapacityScenario() noexcept {
    os::kernel::fs::VfsDentrySlot
        dentry_storage[OS_TEST_VFS_INODE_METADATA_CACHE_SINGLE_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot inode_storage[OS_TEST_VFS_INODE_METADATA_CACHE_SINGLE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    os::kernel::fs::VfsInodeMetadataToken loading_token{};
    os::kernel::fs::VfsInodeMetadataToken rejected_token{};
    os::kernel::fs::VfsInodeMetadataSnapshot snapshot{};
    uint64_t evicted_count = OS_TEST_VFS_INODE_METADATA_CACHE_EMPTY_VALUE;
    const bool consistent =
        cache.Initialize(dentry_storage, OS_TEST_VFS_INODE_METADATA_CACHE_SINGLE_CAPACITY,
                         inode_storage, OS_TEST_VFS_INODE_METADATA_CACHE_SINGLE_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.PrepareInodeMetadata(Identity(OS_TEST_VFS_INODE_METADATA_CACHE_FILE_IDENTIFIER),
                                   os::kernel::fs::NodeType::RegularFile, loading_token,
                                   snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadRequired &&
        cache.EvictInodes(UINT64_MAX, evicted_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        evicted_count == OS_TEST_VFS_INODE_METADATA_CACHE_EMPTY_VALUE &&
        cache.PrepareInodeMetadata(
            Identity(OS_TEST_VFS_INODE_METADATA_CACHE_SECOND_FILE_IDENTIFIER),
            os::kernel::fs::NodeType::RegularFile, rejected_token, snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::CapacityExhausted &&
        cache.CancelInodeMetadata(loading_token) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Statistics().active_inode_count == OS_TEST_VFS_INODE_METADATA_CACHE_EMPTY_VALUE &&
        cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    return consistent;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_INODE_METADATA_CACHE_SUITE_NAME};
    test_context.Expect(RunLoadLifecycleScenario(),
                        OS_TEST_VFS_INODE_METADATA_CACHE_LOAD_MESSAGE);
    test_context.Expect(RunDentryRetentionScenario(),
                        OS_TEST_VFS_INODE_METADATA_CACHE_DENTRY_MESSAGE);
    test_context.Expect(RunCapacityScenario(), OS_TEST_VFS_INODE_METADATA_CACHE_CAPACITY_MESSAGE);
    return test_context.ExitCode();
}
