#include <os/kernel/fs/root_delayed_allocation.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_DELAYED_SUITE_NAME =
    "kernel/root_delayed_allocation/integration";
constexpr std::string_view OS_TEST_ROOT_DELAYED_WRITEBACK_MESSAGE =
    "delalloc 必须到 writeback 才分配，data stable 后才能把 unwritten 转 initialized";
constexpr std::string_view OS_TEST_ROOT_DELAYED_RANGE_MESSAGE =
    "fallocate/punch/truncate/SEEK_DATA/SEEK_HOLE/range query 必须保持范围与 bitmap 一致";
constexpr std::string_view OS_TEST_ROOT_DELAYED_ROLLBACK_MESSAGE =
    "extent capacity 失败必须 abort allocator reservation 并保留 delayed range";
constexpr uint64_t OS_TEST_ROOT_DELAYED_TOTAL_BLOCK_COUNT = 1000ULL;
constexpr uint64_t OS_TEST_ROOT_DELAYED_BLOCKS_PER_GROUP = 256ULL;
constexpr uint64_t OS_TEST_ROOT_DELAYED_INODES_PER_GROUP = 64ULL;
constexpr uint64_t OS_TEST_ROOT_DELAYED_GROUP_COUNT = 4ULL;
constexpr uint64_t OS_TEST_ROOT_DELAYED_JOURNAL_START_BLOCK = 16ULL;
constexpr uint64_t OS_TEST_ROOT_DELAYED_JOURNAL_BLOCK_COUNT = 81ULL;
constexpr uint64_t OS_TEST_ROOT_DELAYED_BITMAP_STORAGE_SIZE_BYTES =
    OS_TEST_ROOT_DELAYED_GROUP_COUNT * os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_DELAYED_UUID{
    .low = 0x44454C41594C4F43ULL,
    .high = 0x1020304050607080ULL,
};

void SetBit(uint8_t *const bitmaps, const uint64_t group_index,
            const uint64_t group_block_index) noexcept {
    const uint64_t byte_offset =
        group_index * os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES +
        group_block_index / 8ULL;
    bitmaps[byte_offset] = static_cast<uint8_t>(
        bitmaps[byte_offset] | static_cast<uint8_t>(1U << (group_block_index % 8ULL)));
}

[[nodiscard]] os::kernel::fs::RootV5Superblock MakeSuperblock() noexcept {
    os::kernel::fs::RootV5Superblock superblock{};
    static_cast<void>(os::kernel::fs::PlanRootV5Superblock(
        os::kernel::fs::RootV5FormatProfile{
            .sector_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES,
            .block_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
            .file_system_start_lba = 8ULL,
            .device_sector_count = 8ULL + OS_TEST_ROOT_DELAYED_TOTAL_BLOCK_COUNT * 8ULL,
            .blocks_per_group = OS_TEST_ROOT_DELAYED_BLOCKS_PER_GROUP,
            .group_descriptor_size_bytes =
                os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES,
            .inode_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES,
            .inodes_per_group = OS_TEST_ROOT_DELAYED_INODES_PER_GROUP,
            .creation_time_nanoseconds = 1ULL,
            .uuid = OS_TEST_ROOT_DELAYED_UUID,
        },
        superblock));
    return superblock;
}

void BuildAllocatorStorage(const os::kernel::fs::RootV5Superblock &superblock,
                           os::kernel::fs::RootV5GroupDescriptor *const descriptors,
                           uint8_t *const bitmaps, uint64_t *const free_counts) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_ROOT_DELAYED_BITMAP_STORAGE_SIZE_BYTES;
         ++byte_index) {
        bitmaps[byte_index] = 0U;
    }
    for (uint64_t group_index = 0ULL; group_index < superblock.group_count; ++group_index) {
        static_cast<void>(os::kernel::fs::BuildInitialRootV5GroupDescriptor(
            superblock, group_index, descriptors[group_index]));
        const os::kernel::fs::RootV5GroupDescriptor &descriptor = descriptors[group_index];
        const uint64_t data_begin = descriptor.data_start_block - descriptor.first_block;
        for (uint64_t group_block = 0ULL; group_block < data_begin; ++group_block) {
            SetBit(bitmaps, group_index, group_block);
        }
        for (uint64_t group_block = descriptor.block_count;
             group_block < superblock.blocks_per_group; ++group_block) {
            SetBit(bitmaps, group_index, group_block);
        }
        free_counts[group_index] = descriptor.data_block_count;
    }
    for (uint64_t physical_block = OS_TEST_ROOT_DELAYED_JOURNAL_START_BLOCK;
         physical_block <
         OS_TEST_ROOT_DELAYED_JOURNAL_START_BLOCK + OS_TEST_ROOT_DELAYED_JOURNAL_BLOCK_COUNT;
         ++physical_block) {
        const uint64_t group_index = physical_block / superblock.blocks_per_group;
        const uint64_t group_block = physical_block - descriptors[group_index].first_block;
        SetBit(bitmaps, group_index, group_block);
        --free_counts[group_index];
    }
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_DELAYED_SUITE_NAME};
    const os::kernel::fs::RootV5Superblock superblock = MakeSuperblock();
    os::kernel::fs::RootV5GroupDescriptor descriptors[OS_TEST_ROOT_DELAYED_GROUP_COUNT]{};
    uint8_t bitmaps[OS_TEST_ROOT_DELAYED_BITMAP_STORAGE_SIZE_BYTES]{};
    uint64_t free_counts[OS_TEST_ROOT_DELAYED_GROUP_COUNT]{};
    BuildAllocatorStorage(superblock, descriptors, bitmaps, free_counts);
    os::kernel::fs::RootBlockGroupAllocator allocator{};
    os::kernel::fs::RootExtentTree tree{};
    os::kernel::fs::RootDelayedAllocation delayed{};
    const uint64_t initial_free_count =
        free_counts[0] + free_counts[1] + free_counts[2] + free_counts[3];
    bool writeback_valid =
        allocator.Initialize(superblock, descriptors, OS_TEST_ROOT_DELAYED_GROUP_COUNT, bitmaps,
                             sizeof(bitmaps), free_counts, OS_TEST_ROOT_DELAYED_GROUP_COUNT,
                             OS_TEST_ROOT_DELAYED_JOURNAL_START_BLOCK,
                             OS_TEST_ROOT_DELAYED_JOURNAL_BLOCK_COUNT) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        tree.Initialize(superblock.total_block_count, OS_TEST_ROOT_DELAYED_JOURNAL_START_BLOCK,
                        OS_TEST_ROOT_DELAYED_JOURNAL_BLOCK_COUNT, 32ULL, 1ULL,
                        OS_TEST_ROOT_DELAYED_UUID) == os::kernel::fs::RootExtentStatus::Succeeded &&
        delayed.Initialize(0ULL) == os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.ReserveWrite(0ULL, 8ULL, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        tree.ExtentCount() == 0ULL && allocator.FreeBlockCount() == initial_free_count;
    os::kernel::fs::RootFileRange ranges[16]{};
    uint64_t range_count = 0ULL;
    uint64_t seek_result = 0ULL;
    writeback_valid =
        writeback_valid &&
        delayed.QueryRanges(0ULL, 8ULL, tree, ranges, 16ULL, range_count) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        range_count == 1ULL && ranges[0].kind == os::kernel::fs::RootFileRangeKind::Delayed &&
        ranges[0].physical_start_block == os::kernel::fs::OS_KERNEL_ROOTFS_V5_NO_BLOCK &&
        delayed.SeekData(0ULL, tree, seek_result) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        seek_result == 0ULL &&
        delayed.SeekHole(0ULL, tree, seek_result) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        seek_result == 8ULL;
    os::kernel::fs::RootWritebackToken writeback{};
    writeback_valid =
        writeback_valid &&
        delayed.BeginWriteback(0ULL, 8ULL, 0ULL, allocator, tree, writeback) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        writeback.active && tree.ExtentCount() == 1ULL &&
        delayed.Validate(tree) == os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.CompleteWriteback(writeback, allocator, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.DelayedRangeCount() == 0ULL &&
        allocator.FreeBlockCount() == initial_free_count - 8ULL;
    os::kernel::fs::RootExtent mapped{};
    writeback_valid =
        writeback_valid &&
        tree.Lookup(0ULL, mapped) == os::kernel::fs::RootExtentStatus::Succeeded &&
        mapped.state == os::kernel::fs::RootExtentState::Initialized &&
        delayed.QueryRanges(0ULL, 8ULL, tree, ranges, 16ULL, range_count) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        range_count == 1ULL && ranges[0].kind == os::kernel::fs::RootFileRangeKind::Initialized &&
        delayed.Validate(tree) == os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        allocator.Validate() == os::kernel::fs::RootBlockAllocatorStatus::Succeeded;
    context.Expect(writeback_valid, OS_TEST_ROOT_DELAYED_WRITEBACK_MESSAGE);

    os::kernel::fs::RootWritebackToken aborted_writeback{};
    bool ranges_valid =
        delayed.ReserveWrite(16ULL, 4ULL, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.BeginWriteback(16ULL, 4ULL, 0ULL, allocator, tree, aborted_writeback) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.AbortWriteback(aborted_writeback, allocator, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.DelayedRangeCount() == 1ULL &&
        delayed.PunchHole(16ULL, 4ULL, allocator, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.DelayedRangeCount() == 0ULL &&
        delayed.Fallocate(20ULL, 6ULL, true, 0ULL, allocator, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.FileSizeBlocks() == 20ULL &&
        delayed.Fallocate(8ULL, 4ULL, false, 0ULL, allocator, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.FileSizeBlocks() == 20ULL;
    ranges_valid =
        ranges_valid &&
        delayed.SeekData(8ULL, tree, seek_result) ==
            os::kernel::fs::RootDelayedAllocationStatus::NotFound &&
        delayed.SeekHole(8ULL, tree, seek_result) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        seek_result == 8ULL &&
        delayed.PunchHole(2ULL, 4ULL, allocator, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.SeekHole(0ULL, tree, seek_result) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        seek_result == 2ULL &&
        delayed.Truncate(4ULL, allocator, tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        delayed.FileSizeBlocks() == 4ULL && tree.ExtentCount() == 1ULL &&
        delayed.QueryRanges(0ULL, 4ULL, tree, ranges, 16ULL, range_count) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        range_count == 1ULL && ranges[0].logical_start_block == 0ULL &&
        ranges[0].block_count == 2ULL &&
        ranges[0].kind == os::kernel::fs::RootFileRangeKind::Initialized &&
        delayed.Validate(tree) == os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        allocator.Validate() == os::kernel::fs::RootBlockAllocatorStatus::Succeeded;
    context.Expect(ranges_valid, OS_TEST_ROOT_DELAYED_RANGE_MESSAGE);

    os::kernel::fs::RootExtentTree full_tree{};
    os::kernel::fs::RootDelayedAllocation rollback_delayed{};
    bool rollback_valid =
        full_tree.Initialize(superblock.total_block_count, OS_TEST_ROOT_DELAYED_JOURNAL_START_BLOCK,
                             OS_TEST_ROOT_DELAYED_JOURNAL_BLOCK_COUNT, 64ULL, 1ULL,
                             OS_TEST_ROOT_DELAYED_UUID) ==
        os::kernel::fs::RootExtentStatus::Succeeded;
    for (uint64_t extent_index = 0ULL;
         rollback_valid &&
         extent_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT;
         ++extent_index) {
        rollback_valid = full_tree.Insert(os::kernel::fs::RootExtent{
                             .logical_start_block = extent_index * 2ULL,
                             .physical_start_block = 400ULL + extent_index,
                             .block_count = 1ULL,
                             .state = os::kernel::fs::RootExtentState::Initialized,
                         }) == os::kernel::fs::RootExtentStatus::Succeeded;
    }
    const uint64_t free_before_failed_mapping = allocator.FreeBlockCount();
    os::kernel::fs::RootWritebackToken failed_token{};
    rollback_valid =
        rollback_valid &&
        rollback_delayed.Initialize(0ULL) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        rollback_delayed.ReserveWrite(1ULL, 1ULL, full_tree) ==
            os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
        rollback_delayed.BeginWriteback(1ULL, 1ULL, 0ULL, allocator, full_tree, failed_token) ==
            os::kernel::fs::RootDelayedAllocationStatus::MappingFailed &&
        !failed_token.active && rollback_delayed.DelayedRangeCount() == 1ULL &&
        allocator.FreeBlockCount() == free_before_failed_mapping &&
        allocator.Validate() == os::kernel::fs::RootBlockAllocatorStatus::Succeeded;
    context.Expect(rollback_valid, OS_TEST_ROOT_DELAYED_ROLLBACK_MESSAGE);
    return context.ExitCode();
}
