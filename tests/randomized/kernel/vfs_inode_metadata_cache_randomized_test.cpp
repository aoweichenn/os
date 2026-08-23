#include <os/kernel/fs/vfs_namespace_cache.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_INODE_METADATA_RANDOMIZED_SUITE_NAME =
    "kernel/vfs_inode_metadata_cache/randomized";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_RANDOMIZED_MESSAGE =
    "固定种子十万轮 load、竞争、完成、取消、失效和槽复用必须匹配生命周期 oracle";
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_SEED = 0x494E4F44454D4554ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_ACTION_COUNT = 3ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_COMPLETE_ACTION = 0ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CANCEL_ACTION = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_SUPERBLOCK_IDENTIFIER = 53ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_SUPERBLOCK_GENERATION = 11ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_NODE_GENERATION = 3ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_LINK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_LEFT_SHIFT = 13ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_RIGHT_SHIFT = 7ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_FINAL_LEFT_SHIFT = 17ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_RANDOMIZED_IDENTIFIER_MASK = UINT32_MAX;
constexpr os::abi::FileMode OS_TEST_VFS_INODE_METADATA_RANDOMIZED_MODE =
    os::abi::OS_ABI_FILE_MODE_REGULAR | 0000644U;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << OS_TEST_VFS_INODE_METADATA_RANDOMIZED_LEFT_SHIFT;
    state ^= state >> OS_TEST_VFS_INODE_METADATA_RANDOMIZED_RIGHT_SHIFT;
    state ^= state << OS_TEST_VFS_INODE_METADATA_RANDOMIZED_FINAL_LEFT_SHIFT;
    return state;
}

[[nodiscard]] os::kernel::fs::VfsInodeIdentity Identity(
    const uint64_t iteration) noexcept {
    return os::kernel::fs::VfsInodeIdentity{
        .superblock_identifier = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_SUPERBLOCK_IDENTIFIER,
        .superblock_generation = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_SUPERBLOCK_GENERATION,
        .node_identifier = iteration + OS_TEST_VFS_INODE_METADATA_RANDOMIZED_COUNTER_INCREMENT,
        .node_generation = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_NODE_GENERATION,
    };
}

[[nodiscard]] os::kernel::fs::BackendNodeInformation Metadata(
    const uint64_t random_value) noexcept {
    return os::kernel::fs::BackendNodeInformation{
        .size_bytes = random_value,
        .allocated_size_bytes = random_value,
        .link_count = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_LINK_COUNT,
        .access_time_nanoseconds = random_value,
        .modification_time_nanoseconds = random_value,
        .change_time_nanoseconds = random_value,
        .birth_time_nanoseconds = random_value,
        .owner_user_identifier = static_cast<os::abi::UserIdentifier>(
            random_value & OS_TEST_VFS_INODE_METADATA_RANDOMIZED_IDENTIFIER_MASK),
        .owner_group_identifier = static_cast<os::abi::GroupIdentifier>(
            random_value & OS_TEST_VFS_INODE_METADATA_RANDOMIZED_IDENTIFIER_MASK),
        .mode = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_MODE,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_INODE_METADATA_RANDOMIZED_SUITE_NAME};
    os::kernel::fs::VfsDentrySlot
        dentry_storage[OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot
        inode_storage[OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY]{};
    os::kernel::fs::VfsNamespaceHashEntry
        dentry_hash_entries[OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY]{};
    uint64_t dentry_hash_buckets[OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY]{};
    os::kernel::fs::VfsNamespaceHashEntry
        inode_hash_entries[OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY]{};
    uint64_t inode_hash_buckets[OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    bool consistent =
        cache.Initialize(dentry_storage, OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY,
                         inode_storage, OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        cache.ConfigureHashIndex(
            dentry_hash_entries, OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY,
            dentry_hash_buckets, OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY,
            inode_hash_entries, OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY,
            inode_hash_buckets, OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    uint64_t random_state = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_SEED;
    uint64_t expected_completion_count = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_cancellation_count = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_contention_count = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_invalidation_count = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE;
    for (uint64_t iteration = OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE;
         consistent && iteration < OS_TEST_VFS_INODE_METADATA_RANDOMIZED_ITERATION_COUNT;
         ++iteration) {
        const os::kernel::fs::VfsInodeIdentity identity = Identity(iteration);
        os::kernel::fs::VfsInodeMetadataToken token{};
        os::kernel::fs::VfsInodeMetadataSnapshot snapshot{};
        consistent = cache.PrepareInodeMetadata(identity, os::kernel::fs::NodeType::RegularFile,
                                                token, snapshot) ==
                     os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadRequired;
        if ((NextRandom(random_state) & OS_TEST_VFS_INODE_METADATA_RANDOMIZED_COUNTER_INCREMENT) !=
            OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE) {
            os::kernel::fs::VfsInodeMetadataToken contention_token{};
            consistent = consistent &&
                         cache.PrepareInodeMetadata(identity,
                                                    os::kernel::fs::NodeType::RegularFile,
                                                    contention_token, snapshot) ==
                             os::kernel::fs::VfsNamespaceCacheStatus::
                                 InodeMetadataLoadInProgress;
            ++expected_contention_count;
        }
        const uint64_t action =
            NextRandom(random_state) % OS_TEST_VFS_INODE_METADATA_RANDOMIZED_ACTION_COUNT;
        if (action == OS_TEST_VFS_INODE_METADATA_RANDOMIZED_COMPLETE_ACTION) {
            const os::kernel::fs::BackendNodeInformation metadata = Metadata(random_state);
            consistent =
                consistent &&
                cache.CompleteInodeMetadata(token, metadata) ==
                    os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                cache.PrepareInodeMetadata(identity, os::kernel::fs::NodeType::RegularFile, token,
                                           snapshot) ==
                    os::kernel::fs::VfsNamespaceCacheStatus::AlreadyCached &&
                snapshot.metadata.size_bytes == metadata.size_bytes &&
                cache.InvalidateInodeMetadata(identity) ==
                    os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
            ++expected_completion_count;
            ++expected_invalidation_count;
        } else if (action == OS_TEST_VFS_INODE_METADATA_RANDOMIZED_CANCEL_ACTION) {
            consistent = consistent &&
                         cache.CancelInodeMetadata(token) ==
                             os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
            ++expected_cancellation_count;
        } else {
            consistent = consistent &&
                         cache.InvalidateInodeMetadata(identity) ==
                             os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                         cache.CompleteInodeMetadata(token, Metadata(random_state)) ==
                             os::kernel::fs::VfsNamespaceCacheStatus::InvalidToken;
            ++expected_invalidation_count;
        }
        consistent =
            consistent &&
            cache.Statistics().active_inode_count ==
                OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE &&
            cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    }
    const os::kernel::fs::VfsNamespaceCacheStatistics statistics = cache.Statistics();
    consistent =
        consistent &&
        statistics.inode_metadata_miss_count ==
            OS_TEST_VFS_INODE_METADATA_RANDOMIZED_ITERATION_COUNT &&
        statistics.inode_metadata_load_start_count ==
            OS_TEST_VFS_INODE_METADATA_RANDOMIZED_ITERATION_COUNT &&
        statistics.inode_metadata_load_completion_count == expected_completion_count &&
        statistics.inode_metadata_hit_count == expected_completion_count &&
        statistics.inode_metadata_load_cancellation_count == expected_cancellation_count &&
        statistics.inode_metadata_load_contention_count == expected_contention_count &&
        statistics.inode_metadata_invalidation_count == expected_invalidation_count &&
        statistics.capacity_rejection_count ==
            OS_TEST_VFS_INODE_METADATA_RANDOMIZED_EMPTY_VALUE &&
        cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    test_context.Expect(consistent, OS_TEST_VFS_INODE_METADATA_RANDOMIZED_MESSAGE);
    return test_context.ExitCode();
}
