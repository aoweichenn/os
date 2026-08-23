#include <os/kernel/fs/vfs_namespace_cache.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_SUITE_NAME =
    "kernel/vfs_namespace_cache/randomized";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_MESSAGE =
    "固定种子十万轮正负发布、Stale 并存、ABA 和回收必须匹配独立生命周期 oracle";
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_SEED = 0x44454E5452594C52ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_DENTRY_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_INODE_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_MAXIMUM_REFERENCE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_NAME_LENGTH_BYTES = 16ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_HEX_RADIX = 16ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_DECIMAL_RADIX = 10ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_HEX_DIGIT_MASK = 0xFULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint8_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_NAME_PREFIX = static_cast<uint8_t>('n');
constexpr uint8_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_DECIMAL_ZERO = static_cast<uint8_t>('0');
constexpr uint8_t OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_HEX_ALPHA_BASE =
    static_cast<uint8_t>('a' - 10);

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << 13ULL;
    state ^= state >> 7ULL;
    state ^= state << 17ULL;
    return state;
}

[[nodiscard]] os::kernel::fs::VfsInodeIdentity Identity(const uint64_t node_identifier) noexcept {
    return os::kernel::fs::VfsInodeIdentity{
        .superblock_identifier = 23ULL,
        .superblock_generation = 7ULL,
        .node_identifier = node_identifier,
        .node_generation = 1ULL,
    };
}

[[nodiscard]] bool MakeKey(const uint64_t iteration, os::kernel::fs::VfsDentryKey &key) noexcept {
    uint8_t name[OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_NAME_LENGTH_BYTES]{};
    name[OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE] =
        OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_NAME_PREFIX;
    uint64_t remaining = iteration + 1ULL;
    for (uint64_t byte_index = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_NAME_LENGTH_BYTES;
         byte_index > 1ULL; --byte_index) {
        const uint64_t digit = remaining & OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_HEX_DIGIT_MASK;
        name[byte_index - 1ULL] =
            digit < OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_DECIMAL_RADIX
                ? static_cast<uint8_t>(OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_DECIMAL_ZERO + digit)
                : static_cast<uint8_t>(OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_HEX_ALPHA_BASE +
                                       digit);
        remaining /= OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_HEX_RADIX;
    }
    return os::kernel::fs::BuildVfsDentryKey(iteration & 1ULL, Identity(1ULL), name, sizeof(name),
                                             key) ==
           os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_SUITE_NAME};
    os::kernel::fs::VfsDentrySlot
        dentry_storage[OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_DENTRY_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot
        inode_storage[OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_INODE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache cache{};
    bool consistent =
        cache.Initialize(dentry_storage, OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_DENTRY_CAPACITY,
                         inode_storage, OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_INODE_CAPACITY) ==
        os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    uint64_t random_state = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_SEED;
    uint64_t expected_positive_publish_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_negative_publish_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_positive_hit_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_negative_hit_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_release_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_dentry_invalidation_count =
        OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_inode_invalidation_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_cascaded_invalidation_count =
        OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    uint64_t expected_inode_eviction_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
    for (uint64_t iteration = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
         consistent && iteration < OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_ITERATION_COUNT;
         ++iteration) {
        os::kernel::fs::VfsDentryKey key{};
        consistent = MakeKey(iteration, key);
        const bool initial_positive = (NextRandom(random_state) & 1ULL) != 0ULL;
        const os::kernel::fs::VfsInodeIdentity initial_inode = Identity(2ULL * iteration + 2ULL);
        os::kernel::fs::VfsDentryToken initial_token{};
        const os::kernel::fs::VfsNamespaceCacheStatus initial_publish_status =
            initial_positive
                ? cache.PublishPositive(key, initial_inode, os::kernel::fs::NodeType::RegularFile,
                                        initial_token)
                : cache.PublishNegative(key, initial_token);
        consistent = consistent &&
                     initial_publish_status == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
        if (initial_positive) {
            ++expected_positive_publish_count;
        } else {
            ++expected_negative_publish_count;
        }
        const uint64_t initial_reference_count =
            NextRandom(random_state) %
            (OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_MAXIMUM_REFERENCE_COUNT + 1ULL);
        for (uint64_t reference_index = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
             consistent && reference_index < initial_reference_count; ++reference_index) {
            os::kernel::fs::VfsDentrySnapshot snapshot{};
            consistent =
                cache.AcquireDentry(key, snapshot) ==
                    os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                snapshot.token.slot_index == initial_token.slot_index &&
                snapshot.token.generation == initial_token.generation &&
                snapshot.kind == (initial_positive ? os::kernel::fs::VfsDentryKind::Positive
                                                   : os::kernel::fs::VfsDentryKind::Negative);
            if (initial_positive) {
                ++expected_positive_hit_count;
            } else {
                ++expected_negative_hit_count;
            }
        }
        const bool invalidate_inode = initial_positive && (NextRandom(random_state) & 1ULL) != 0ULL;
        consistent = consistent && (invalidate_inode ? cache.InvalidateInode(initial_inode)
                                                     : cache.InvalidateDentry(key)) ==
                                       os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
        if (invalidate_inode) {
            ++expected_inode_invalidation_count;
            ++expected_cascaded_invalidation_count;
        } else {
            ++expected_dentry_invalidation_count;
        }
        os::kernel::fs::VfsDentrySnapshot missing_snapshot{};
        consistent = consistent && cache.AcquireDentry(key, missing_snapshot) ==
                                       os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound;

        const bool replacement_positive = (NextRandom(random_state) & 1ULL) != 0ULL;
        const bool reuse_inode = initial_positive && (NextRandom(random_state) & 1ULL) != 0ULL;
        const os::kernel::fs::VfsInodeIdentity replacement_inode =
            reuse_inode ? initial_inode : Identity(2ULL * iteration + 3ULL);
        os::kernel::fs::VfsDentryToken replacement_token{};
        const os::kernel::fs::VfsNamespaceCacheStatus replacement_publish_status =
            replacement_positive
                ? cache.PublishPositive(key, replacement_inode,
                                        os::kernel::fs::NodeType::RegularFile, replacement_token)
                : cache.PublishNegative(key, replacement_token);
        consistent = consistent && replacement_publish_status ==
                                       os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
        if (replacement_positive) {
            ++expected_positive_publish_count;
        } else {
            ++expected_negative_publish_count;
        }
        const bool acquire_replacement = (NextRandom(random_state) & 1ULL) != 0ULL;
        if (acquire_replacement) {
            os::kernel::fs::VfsDentrySnapshot snapshot{};
            consistent = consistent &&
                         cache.AcquireDentry(key, snapshot) ==
                             os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                         snapshot.token.slot_index == replacement_token.slot_index &&
                         snapshot.token.generation == replacement_token.generation;
            if (replacement_positive) {
                ++expected_positive_hit_count;
            } else {
                ++expected_negative_hit_count;
            }
        }
        for (uint64_t release_index = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
             consistent && release_index < initial_reference_count; ++release_index) {
            consistent = cache.ReleaseDentry(initial_token) ==
                         os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
            ++expected_release_count;
        }
        consistent = consistent && cache.ReadDentry(initial_token, missing_snapshot) ==
                                       os::kernel::fs::VfsNamespaceCacheStatus::InvalidToken;
        if (acquire_replacement) {
            consistent = consistent && cache.ReleaseDentry(replacement_token) ==
                                           os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
            ++expected_release_count;
        }
        uint64_t evicted_dentry_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
        uint64_t evicted_inode_count = OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE;
        consistent = consistent &&
                     cache.EvictDentries(UINT64_MAX, evicted_dentry_count) ==
                         os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
                     evicted_dentry_count == 1ULL &&
                     cache.EvictInodes(UINT64_MAX, evicted_inode_count) ==
                         os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
        expected_inode_eviction_count += evicted_inode_count;
        consistent = consistent &&
                     cache.Statistics().active_dentry_count ==
                         OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE &&
                     cache.Statistics().active_inode_count ==
                         OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE &&
                     cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    }
    const os::kernel::fs::VfsNamespaceCacheStatistics statistics = cache.Statistics();
    consistent =
        consistent && statistics.positive_publish_count == expected_positive_publish_count &&
        statistics.negative_publish_count == expected_negative_publish_count &&
        statistics.positive_hit_count == expected_positive_hit_count &&
        statistics.negative_hit_count == expected_negative_hit_count &&
        statistics.dentry_miss_count == OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_ITERATION_COUNT &&
        statistics.dentry_release_count == expected_release_count &&
        statistics.dentry_invalidation_count == expected_dentry_invalidation_count &&
        statistics.inode_invalidation_count == expected_inode_invalidation_count &&
        statistics.cascaded_dentry_invalidation_count == expected_cascaded_invalidation_count &&
        statistics.stale_dentry_release_count ==
            OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_ITERATION_COUNT &&
        statistics.dentry_eviction_count ==
            OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_ITERATION_COUNT &&
        statistics.inode_eviction_count == expected_inode_eviction_count &&
        statistics.capacity_rejection_count == OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_EMPTY_VALUE &&
        cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    test_context.Expect(consistent, OS_TEST_VFS_NAMESPACE_CACHE_RANDOMIZED_MESSAGE);
    return test_context.ExitCode();
}
