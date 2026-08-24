#include <os/kernel/fs/root_block_group_allocator.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_ALLOCATOR_SUITE_NAME =
    "kernel/root_block_group_allocator/unit";
constexpr std::string_view OS_TEST_ROOT_ALLOCATOR_RESERVATION_MESSAGE =
    "allocator reservation commit/abort/release 必须保持 bitmap、free count 和 stale token 守恒";
constexpr std::string_view OS_TEST_ROOT_ALLOCATOR_LOCALITY_MESSAGE =
    "allocator 必须优先组内连续 run，支持 partial，并在组满后循环 fallback";
constexpr std::string_view OS_TEST_ROOT_ALLOCATOR_ENOSPC_MESSAGE =
    "无满足 minimum 的连续 run 必须返回 ENOSPC 且不改变 bitmap";
constexpr uint64_t OS_TEST_ROOT_ALLOCATOR_TOTAL_BLOCK_COUNT = 1000ULL;
constexpr uint64_t OS_TEST_ROOT_ALLOCATOR_BLOCKS_PER_GROUP = 256ULL;
constexpr uint64_t OS_TEST_ROOT_ALLOCATOR_INODES_PER_GROUP = 64ULL;
constexpr uint64_t OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT = 4ULL;
constexpr uint64_t OS_TEST_ROOT_ALLOCATOR_JOURNAL_START_BLOCK = 16ULL;
constexpr uint64_t OS_TEST_ROOT_ALLOCATOR_JOURNAL_BLOCK_COUNT = 81ULL;
constexpr uint64_t OS_TEST_ROOT_ALLOCATOR_BITMAP_STORAGE_SIZE_BYTES =
    OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT * os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_ALLOCATOR_UUID{
    .low = 0x414C4C4F43415445ULL,
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
            .device_sector_count = 8ULL + OS_TEST_ROOT_ALLOCATOR_TOTAL_BLOCK_COUNT * 8ULL,
            .blocks_per_group = OS_TEST_ROOT_ALLOCATOR_BLOCKS_PER_GROUP,
            .group_descriptor_size_bytes =
                os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES,
            .inode_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES,
            .inodes_per_group = OS_TEST_ROOT_ALLOCATOR_INODES_PER_GROUP,
            .creation_time_nanoseconds = 1ULL,
            .uuid = OS_TEST_ROOT_ALLOCATOR_UUID,
        },
        superblock));
    return superblock;
}

void BuildAllocatorStorage(const os::kernel::fs::RootV5Superblock &superblock,
                           os::kernel::fs::RootV5GroupDescriptor *const descriptors,
                           uint8_t *const bitmaps, uint64_t *const free_counts,
                           const bool allocate_all_data) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_ROOT_ALLOCATOR_BITMAP_STORAGE_SIZE_BYTES;
         ++byte_index) {
        bitmaps[byte_index] = 0U;
    }
    for (uint64_t group_index = 0ULL; group_index < superblock.group_count; ++group_index) {
        static_cast<void>(os::kernel::fs::BuildInitialRootV5GroupDescriptor(
            superblock, group_index, descriptors[group_index]));
        const os::kernel::fs::RootV5GroupDescriptor &descriptor = descriptors[group_index];
        const uint64_t data_begin = descriptor.data_start_block - descriptor.first_block;
        const uint64_t data_end = data_begin + descriptor.data_block_count;
        for (uint64_t group_block = 0ULL; group_block < data_begin; ++group_block) {
            SetBit(bitmaps, group_index, group_block);
        }
        for (uint64_t group_block = descriptor.block_count;
             group_block < superblock.blocks_per_group; ++group_block) {
            SetBit(bitmaps, group_index, group_block);
        }
        if (allocate_all_data) {
            for (uint64_t group_block = data_begin; group_block < data_end; ++group_block) {
                SetBit(bitmaps, group_index, group_block);
            }
            free_counts[group_index] = 0ULL;
        } else {
            free_counts[group_index] = descriptor.data_block_count;
        }
    }
    if (!allocate_all_data) {
        for (uint64_t physical_block = OS_TEST_ROOT_ALLOCATOR_JOURNAL_START_BLOCK;
             physical_block < OS_TEST_ROOT_ALLOCATOR_JOURNAL_START_BLOCK +
                                  OS_TEST_ROOT_ALLOCATOR_JOURNAL_BLOCK_COUNT;
             ++physical_block) {
            const uint64_t group_index = physical_block / superblock.blocks_per_group;
            const uint64_t group_block = physical_block - descriptors[group_index].first_block;
            SetBit(bitmaps, group_index, group_block);
            --free_counts[group_index];
        }
    }
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_ALLOCATOR_SUITE_NAME};
    const os::kernel::fs::RootV5Superblock superblock = MakeSuperblock();
    os::kernel::fs::RootV5GroupDescriptor descriptors[OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT]{};
    uint8_t bitmaps[OS_TEST_ROOT_ALLOCATOR_BITMAP_STORAGE_SIZE_BYTES]{};
    uint64_t free_counts[OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT]{};
    BuildAllocatorStorage(superblock, descriptors, bitmaps, free_counts, false);
    os::kernel::fs::RootBlockGroupAllocator allocator{};
    const uint64_t initial_free_count =
        free_counts[0] + free_counts[1] + free_counts[2] + free_counts[3];
    os::kernel::fs::RootBlockAllocation allocation{};
    os::kernel::fs::RootBlockReservationToken token{};
    const bool reservation_valid =
        allocator.Initialize(superblock, descriptors, OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT, bitmaps,
                             sizeof(bitmaps), free_counts, OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT,
                             OS_TEST_ROOT_ALLOCATOR_JOURNAL_START_BLOCK,
                             OS_TEST_ROOT_ALLOCATOR_JOURNAL_BLOCK_COUNT) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Reserve(32ULL, 32ULL, 0ULL, allocation, token) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocation.group_index == 0ULL && allocation.physical_start_block == 97ULL &&
        allocator.Commit(token) == os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Abort(token) == os::kernel::fs::RootBlockAllocatorStatus::InvalidReservation &&
        allocator.CanRelease(allocation.physical_start_block, allocation.block_count) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Release(allocation.physical_start_block, allocation.block_count) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Release(allocation.physical_start_block, allocation.block_count) ==
            os::kernel::fs::RootBlockAllocatorStatus::NotAllocated &&
        allocator.Reserve(16ULL, 16ULL, 0ULL, allocation, token) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Abort(token) == os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.FreeBlockCount() == initial_free_count &&
        allocator.CanRelease(OS_TEST_ROOT_ALLOCATOR_JOURNAL_START_BLOCK, 1ULL) ==
            os::kernel::fs::RootBlockAllocatorStatus::ProtectedRange &&
        allocator.Validate() == os::kernel::fs::RootBlockAllocatorStatus::Succeeded;
    context.Expect(reservation_valid, OS_TEST_ROOT_ALLOCATOR_RESERVATION_MESSAGE);

    os::kernel::fs::RootBlockAllocation partial{};
    os::kernel::fs::RootBlockReservationToken partial_token{};
    bool locality_valid =
        allocator.Reserve(300ULL, 16ULL, 1ULL, partial, partial_token) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        partial.group_index == 1ULL && partial.block_count < 300ULL &&
        allocator.Abort(partial_token) == os::kernel::fs::RootBlockAllocatorStatus::Succeeded;
    os::kernel::fs::RootBlockAllocation group_zero_tail{};
    os::kernel::fs::RootBlockReservationToken group_zero_tail_token{};
    os::kernel::fs::RootBlockAllocation group_zero_head{};
    os::kernel::fs::RootBlockReservationToken group_zero_head_token{};
    os::kernel::fs::RootBlockAllocation fallback{};
    os::kernel::fs::RootBlockReservationToken fallback_token{};
    locality_valid =
        locality_valid &&
        allocator.Reserve(159ULL, 159ULL, 0ULL, group_zero_tail, group_zero_tail_token) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Commit(group_zero_tail_token) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Reserve(8ULL, 8ULL, 0ULL, group_zero_head, group_zero_head_token) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Commit(group_zero_head_token) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Reserve(16ULL, 16ULL, 0ULL, fallback, fallback_token) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        fallback.group_index == 1ULL &&
        allocator.Abort(fallback_token) == os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Release(group_zero_tail.physical_start_block, group_zero_tail.block_count) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Release(group_zero_head.physical_start_block, group_zero_head.block_count) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        allocator.Statistics().partial_allocation_count != 0ULL &&
        allocator.Statistics().group_fallback_count != 0ULL &&
        allocator.Validate() == os::kernel::fs::RootBlockAllocatorStatus::Succeeded;
    context.Expect(locality_valid, OS_TEST_ROOT_ALLOCATOR_LOCALITY_MESSAGE);

    uint8_t full_bitmaps[OS_TEST_ROOT_ALLOCATOR_BITMAP_STORAGE_SIZE_BYTES]{};
    uint64_t no_free_counts[OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT]{};
    os::kernel::fs::RootV5GroupDescriptor full_descriptors[OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT]{};
    BuildAllocatorStorage(superblock, full_descriptors, full_bitmaps, no_free_counts, true);
    os::kernel::fs::RootBlockGroupAllocator full_allocator{};
    const bool enospc_valid =
        full_allocator.Initialize(superblock, full_descriptors, OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT,
                                  full_bitmaps, sizeof(full_bitmaps), no_free_counts,
                                  OS_TEST_ROOT_ALLOCATOR_GROUP_COUNT, 0ULL,
                                  0ULL) == os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        full_allocator.Reserve(1ULL, 1ULL, 0ULL, allocation, token) ==
            os::kernel::fs::RootBlockAllocatorStatus::CapacityExhausted &&
        full_allocator.FreeBlockCount() == 0ULL &&
        full_allocator.Validate() == os::kernel::fs::RootBlockAllocatorStatus::Succeeded;
    context.Expect(enospc_valid, OS_TEST_ROOT_ALLOCATOR_ENOSPC_MESSAGE);
    return context.ExitCode();
}
