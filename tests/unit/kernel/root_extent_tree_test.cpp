#include <os/kernel/fs/root_extent_tree.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_EXTENT_TREE_SUITE_NAME = "kernel/root_extent_tree/unit";
constexpr std::string_view OS_TEST_ROOT_EXTENT_TREE_FORMAT_MESSAGE =
    "extent leaf/index 必须按 4 KiB 小端 CRC32C 盘面往返并拒绝损坏";
constexpr std::string_view OS_TEST_ROOT_EXTENT_TREE_SHAPE_MESSAGE =
    "256 extent 必须形成深度 3 的 4 路树，删除后收缩且保持 canonical 顺序";
constexpr std::string_view OS_TEST_ROOT_EXTENT_TREE_CONVERT_MESSAGE =
    "unwritten/initialized 转换必须 split 后可重新 merge，hole remove 返回物理交集";
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_EXTENT_TREE_UUID{
    .low = 0x455854454E545631ULL,
    .high = 0x1020304050607080ULL,
};
constexpr uint64_t OS_TEST_ROOT_EXTENT_TREE_FILE_SYSTEM_BLOCK_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_TREE_FILE_SYSTEM_INODE_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_TREE_JOURNAL_START_BLOCK = 16ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_TREE_INODE_NUMBER = 32ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_TREE_INODE_GENERATION = 7ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_TREE_EXTENT_COUNT = 256ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_TREE_PHYSICAL_BASE = 512ULL;
constexpr uint8_t OS_TEST_ROOT_EXTENT_TREE_CORRUPTION_MASK = 0x40U;

[[nodiscard]] os::kernel::fs::RootJournalV2Superblock MakeJournalSuperblock() noexcept {
    os::kernel::fs::RootJournalV2Superblock superblock{};
    static_cast<void>(os::kernel::fs::PlanRootJournalV2Superblock(
        os::kernel::fs::RootJournalV2FormatProfile{
            .file_system_total_block_count = OS_TEST_ROOT_EXTENT_TREE_FILE_SYSTEM_BLOCK_COUNT,
            .file_system_inode_count = OS_TEST_ROOT_EXTENT_TREE_FILE_SYSTEM_INODE_COUNT,
            .journal_start_relative_block = OS_TEST_ROOT_EXTENT_TREE_JOURNAL_START_BLOCK,
            .creation_time_nanoseconds = 1ULL,
            .file_system_uuid = OS_TEST_ROOT_EXTENT_TREE_UUID,
        },
        superblock));
    return superblock;
}

[[nodiscard]] os::kernel::fs::RootExtentStatus
InitializeTree(os::kernel::fs::RootExtentTree &tree) noexcept {
    return tree.Initialize(OS_TEST_ROOT_EXTENT_TREE_FILE_SYSTEM_BLOCK_COUNT,
                           OS_TEST_ROOT_EXTENT_TREE_JOURNAL_START_BLOCK,
                           os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT,
                           OS_TEST_ROOT_EXTENT_TREE_INODE_NUMBER,
                           OS_TEST_ROOT_EXTENT_TREE_INODE_GENERATION,
                           OS_TEST_ROOT_EXTENT_TREE_UUID);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_EXTENT_TREE_SUITE_NAME};
    const os::kernel::fs::RootJournalV2Superblock journal_superblock = MakeJournalSuperblock();
    os::kernel::fs::RootExtentNode leaf{
        .tree_generation = 3ULL,
        .inode_number = OS_TEST_ROOT_EXTENT_TREE_INODE_NUMBER,
        .inode_generation = OS_TEST_ROOT_EXTENT_TREE_INODE_GENERATION,
        .depth = 0ULL,
        .entry_count = 2ULL,
        .file_system_uuid = OS_TEST_ROOT_EXTENT_TREE_UUID,
        .entries = {},
    };
    leaf.entries[0] = os::kernel::fs::RootExtentNodeEntry{
        .logical_start_block = 0ULL,
        .physical_or_child_block = 512ULL,
        .block_count_or_generation = 8ULL,
        .state_or_covered_block_count =
            static_cast<uint64_t>(os::kernel::fs::RootExtentState::Initialized),
    };
    leaf.entries[1] = os::kernel::fs::RootExtentNodeEntry{
        .logical_start_block = 16ULL,
        .physical_or_child_block = 640ULL,
        .block_count_or_generation = 4ULL,
        .state_or_covered_block_count =
            static_cast<uint64_t>(os::kernel::fs::RootExtentState::Unwritten),
    };
    uint8_t leaf_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootExtentNode decoded_leaf{};
    os::kernel::fs::RootExtentNode index = leaf;
    index.depth = 1ULL;
    index.entry_count = 1ULL;
    index.entries[0] = os::kernel::fs::RootExtentNodeEntry{
        .logical_start_block = 0ULL,
        .physical_or_child_block = 320ULL,
        .block_count_or_generation = 3ULL,
        .state_or_covered_block_count = 20ULL,
    };
    index.entries[1] = os::kernel::fs::RootExtentNodeEntry{};
    uint8_t index_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootExtentNode decoded_index{};
    os::kernel::fs::RootExtentNode duplicate_leaf = leaf;
    duplicate_leaf.entries[1].physical_or_child_block = 516ULL;
    const bool format_valid =
        os::kernel::fs::EncodeRootExtentLeafNode(journal_superblock, leaf, leaf_bytes,
                                                 sizeof(leaf_bytes)) ==
            os::kernel::fs::RootExtentStatus::Succeeded &&
        os::kernel::fs::DecodeRootExtentLeafNode(journal_superblock, leaf_bytes, sizeof(leaf_bytes),
                                                 decoded_leaf) ==
            os::kernel::fs::RootExtentStatus::Succeeded &&
        decoded_leaf.entries[1].state_or_covered_block_count ==
            static_cast<uint64_t>(os::kernel::fs::RootExtentState::Unwritten) &&
        os::kernel::fs::EncodeRootExtentIndexNode(journal_superblock, index, index_bytes,
                                                  sizeof(index_bytes)) ==
            os::kernel::fs::RootExtentStatus::Succeeded &&
        os::kernel::fs::DecodeRootExtentIndexNode(journal_superblock, index_bytes,
                                                  sizeof(index_bytes), decoded_index) ==
            os::kernel::fs::RootExtentStatus::Succeeded &&
        decoded_index.entries[0].physical_or_child_block == 320ULL &&
        os::kernel::fs::EncodeRootExtentLeafNode(journal_superblock, duplicate_leaf, leaf_bytes,
                                                 sizeof(leaf_bytes)) ==
            os::kernel::fs::RootExtentStatus::Overlap &&
        os::kernel::fs::EncodeRootExtentLeafNode(journal_superblock, leaf, leaf_bytes,
                                                 sizeof(leaf_bytes)) ==
            os::kernel::fs::RootExtentStatus::Succeeded;
    leaf_bytes[64] ^= OS_TEST_ROOT_EXTENT_TREE_CORRUPTION_MASK;
    index_bytes[64] ^= OS_TEST_ROOT_EXTENT_TREE_CORRUPTION_MASK;
    context.Expect(format_valid &&
                       os::kernel::fs::DecodeRootExtentLeafNode(journal_superblock, leaf_bytes,
                                                                sizeof(leaf_bytes), decoded_leaf) ==
                           os::kernel::fs::RootExtentStatus::InvalidChecksum &&
                       os::kernel::fs::DecodeRootExtentIndexNode(
                           journal_superblock, index_bytes, sizeof(index_bytes), decoded_index) ==
                           os::kernel::fs::RootExtentStatus::InvalidChecksum,
                   OS_TEST_ROOT_EXTENT_TREE_FORMAT_MESSAGE);

    os::kernel::fs::RootExtentTree shape_tree{};
    bool shape_valid = InitializeTree(shape_tree) == os::kernel::fs::RootExtentStatus::Succeeded;
    for (uint64_t extent_index = 0ULL;
         shape_valid && extent_index < OS_TEST_ROOT_EXTENT_TREE_EXTENT_COUNT; ++extent_index) {
        shape_valid = shape_tree.Insert(os::kernel::fs::RootExtent{
                          .logical_start_block = extent_index * 2ULL,
                          .physical_start_block =
                              OS_TEST_ROOT_EXTENT_TREE_PHYSICAL_BASE + extent_index * 2ULL,
                          .block_count = 1ULL,
                          .state = os::kernel::fs::RootExtentState::Initialized,
                      }) == os::kernel::fs::RootExtentStatus::Succeeded;
    }
    const os::kernel::fs::RootExtentTreeStatistics full_statistics = shape_tree.Statistics();
    os::kernel::fs::RootExtent removed[OS_TEST_ROOT_EXTENT_TREE_EXTENT_COUNT]{};
    uint64_t removed_count = 0ULL;
    shape_valid = shape_valid &&
                  shape_tree.ExtentCount() == OS_TEST_ROOT_EXTENT_TREE_EXTENT_COUNT &&
                  full_statistics.current_depth ==
                      os::kernel::fs::OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_DEPTH &&
                  full_statistics.current_node_count ==
                      os::kernel::fs::OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_NODE_COUNT &&
                  shape_tree.Remove(0ULL, 500ULL, removed, OS_TEST_ROOT_EXTENT_TREE_EXTENT_COUNT,
                                    removed_count) == os::kernel::fs::RootExtentStatus::Succeeded &&
                  removed_count == 250ULL && shape_tree.ExtentCount() == 6ULL &&
                  shape_tree.Statistics().current_depth == 1ULL &&
                  shape_tree.Statistics().depth_shrink_count != 0ULL &&
                  shape_tree.Validate() == os::kernel::fs::RootExtentStatus::Succeeded;
    context.Expect(shape_valid, OS_TEST_ROOT_EXTENT_TREE_SHAPE_MESSAGE);

    os::kernel::fs::RootExtentTree conversion_tree{};
    os::kernel::fs::RootExtent lookup{};
    uint64_t conversion_removed_count = 0ULL;
    os::kernel::fs::RootExtent conversion_removed[2]{};
    const bool conversion_valid =
        InitializeTree(conversion_tree) == os::kernel::fs::RootExtentStatus::Succeeded &&
        conversion_tree.Insert(os::kernel::fs::RootExtent{
            .logical_start_block = 0ULL,
            .physical_start_block = 1024ULL,
            .block_count = 16ULL,
            .state = os::kernel::fs::RootExtentState::Unwritten,
        }) == os::kernel::fs::RootExtentStatus::Succeeded &&
        conversion_tree.Convert(4ULL, 8ULL, os::kernel::fs::RootExtentState::Unwritten,
                                os::kernel::fs::RootExtentState::Initialized) ==
            os::kernel::fs::RootExtentStatus::Succeeded &&
        conversion_tree.ExtentCount() == 3ULL &&
        conversion_tree.Lookup(6ULL, lookup) == os::kernel::fs::RootExtentStatus::Succeeded &&
        lookup.state == os::kernel::fs::RootExtentState::Initialized &&
        conversion_tree.Convert(4ULL, 8ULL, os::kernel::fs::RootExtentState::Initialized,
                                os::kernel::fs::RootExtentState::Unwritten) ==
            os::kernel::fs::RootExtentStatus::Succeeded &&
        conversion_tree.ExtentCount() == 1ULL &&
        conversion_tree.Remove(6ULL, 2ULL, conversion_removed, 2ULL, conversion_removed_count) ==
            os::kernel::fs::RootExtentStatus::Succeeded &&
        conversion_removed_count == 1ULL && conversion_removed[0].physical_start_block == 1030ULL &&
        conversion_tree.ExtentCount() == 2ULL &&
        conversion_tree.Insert(os::kernel::fs::RootExtent{
            .logical_start_block = 5ULL,
            .physical_start_block = 2000ULL,
            .block_count = 4ULL,
            .state = os::kernel::fs::RootExtentState::Initialized,
        }) == os::kernel::fs::RootExtentStatus::Overlap &&
        conversion_tree.Insert(os::kernel::fs::RootExtent{
            .logical_start_block = 20ULL,
            .physical_start_block = 1024ULL,
            .block_count = 1ULL,
            .state = os::kernel::fs::RootExtentState::Initialized,
        }) == os::kernel::fs::RootExtentStatus::Overlap;
    context.Expect(conversion_valid, OS_TEST_ROOT_EXTENT_TREE_CONVERT_MESSAGE);
    return context.ExitCode();
}
