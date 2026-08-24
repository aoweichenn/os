#include <os/kernel/fs/root_file_system_v5_format.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOTFS_V5_RANDOMIZED_SUITE_NAME =
    "kernel/root_file_system_v5_format/randomized";
constexpr std::string_view OS_TEST_ROOTFS_V5_RANDOMIZED_MESSAGE =
    "固定种子十万组小几何必须保持 group 区间、备份策略、inode 范围和编解码一致";
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_SEED = 0x524F4F5456354752ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_LEFT_SHIFT = 13ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_RIGHT_SHIFT = 7ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_FINAL_LEFT_SHIFT = 17ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_MINIMUM_GROUP_COUNT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_GROUP_COUNT_SPAN = 32ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_MINIMUM_INODES_PER_GROUP = 16ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_INODE_COUNT_SPAN = 240ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_START_LBA = 8ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_MINIMUM_BLOCK_SHIFT = 8ULL;
constexpr uint64_t OS_TEST_ROOTFS_V5_RANDOMIZED_BLOCK_SHIFT_SPAN = 5ULL;
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOTFS_V5_RANDOMIZED_UUID{
    .low = 0x1122334455667788ULL,
    .high = 0x8877665544332211ULL,
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << OS_TEST_ROOTFS_V5_RANDOMIZED_LEFT_SHIFT;
    state ^= state >> OS_TEST_ROOTFS_V5_RANDOMIZED_RIGHT_SHIFT;
    state ^= state << OS_TEST_ROOTFS_V5_RANDOMIZED_FINAL_LEFT_SHIFT;
    return state;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_ROOTFS_V5_RANDOMIZED_SUITE_NAME};
    uint64_t random_state = OS_TEST_ROOTFS_V5_RANDOMIZED_SEED;
    bool consistent = true;
    uint8_t descriptor_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES]{};
    for (uint64_t iteration = 0ULL;
         consistent && iteration < OS_TEST_ROOTFS_V5_RANDOMIZED_ITERATION_COUNT; ++iteration) {
        const uint64_t random_value = NextRandom(random_state);
        const uint64_t blocks_per_group =
            1ULL << (OS_TEST_ROOTFS_V5_RANDOMIZED_MINIMUM_BLOCK_SHIFT +
                     random_value % OS_TEST_ROOTFS_V5_RANDOMIZED_BLOCK_SHIFT_SPAN);
        const uint64_t group_count =
            OS_TEST_ROOTFS_V5_RANDOMIZED_MINIMUM_GROUP_COUNT +
            NextRandom(random_state) % OS_TEST_ROOTFS_V5_RANDOMIZED_GROUP_COUNT_SPAN;
        const uint64_t maximum_tail = blocks_per_group / 8ULL;
        const uint64_t tail_reduction =
            group_count == OS_TEST_ROOTFS_V5_RANDOMIZED_MINIMUM_GROUP_COUNT
                ? 0ULL
                : NextRandom(random_state) % maximum_tail;
        const uint64_t total_block_count = group_count * blocks_per_group - tail_reduction;
        const uint64_t inodes_per_group =
            OS_TEST_ROOTFS_V5_RANDOMIZED_MINIMUM_INODES_PER_GROUP +
            NextRandom(random_state) % OS_TEST_ROOTFS_V5_RANDOMIZED_INODE_COUNT_SPAN;
        const os::kernel::fs::RootV5FormatProfile profile{
            .sector_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES,
            .block_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
            .file_system_start_lba = OS_TEST_ROOTFS_V5_RANDOMIZED_START_LBA,
            .device_sector_count =
                OS_TEST_ROOTFS_V5_RANDOMIZED_START_LBA +
                total_block_count * os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK,
            .blocks_per_group = blocks_per_group,
            .group_descriptor_size_bytes =
                os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES,
            .inode_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES,
            .inodes_per_group = inodes_per_group,
            .creation_time_nanoseconds = iteration,
            .uuid = OS_TEST_ROOTFS_V5_RANDOMIZED_UUID,
        };
        os::kernel::fs::RootV5Superblock superblock{};
        consistent = os::kernel::fs::PlanRootV5Superblock(profile, superblock) ==
                         os::kernel::fs::RootV5FormatStatus::Succeeded &&
                     superblock.group_count == group_count &&
                     superblock.total_block_count == total_block_count &&
                     superblock.inode_count == group_count * inodes_per_group;
        if (!consistent) {
            break;
        }
        const uint64_t group_index = NextRandom(random_state) % group_count;
        os::kernel::fs::RootV5GroupDescriptor descriptor{};
        consistent = os::kernel::fs::BuildInitialRootV5GroupDescriptor(superblock, group_index,
                                                                       descriptor) ==
                         os::kernel::fs::RootV5FormatStatus::Succeeded &&
                     descriptor.first_block == group_index * blocks_per_group &&
                     descriptor.block_count <= blocks_per_group &&
                     descriptor.data_start_block >= descriptor.first_block &&
                     descriptor.data_start_block + descriptor.data_block_count ==
                         descriptor.first_block + descriptor.block_count &&
                     descriptor.inode_start_number == group_index * inodes_per_group + 1ULL &&
                     descriptor.inode_count == inodes_per_group;
        descriptor.block_bitmap_checksum = static_cast<uint32_t>(NextRandom(random_state));
        descriptor.inode_bitmap_checksum = static_cast<uint32_t>(NextRandom(random_state));
        os::kernel::fs::RootV5GroupDescriptor decoded{};
        consistent = consistent &&
                     os::kernel::fs::EncodeRootV5GroupDescriptor(
                         superblock, descriptor, descriptor_bytes, sizeof(descriptor_bytes)) ==
                         os::kernel::fs::RootV5FormatStatus::Succeeded &&
                     os::kernel::fs::DecodeRootV5GroupDescriptor(
                         superblock, descriptor_bytes, sizeof(descriptor_bytes), decoded) ==
                         os::kernel::fs::RootV5FormatStatus::Succeeded &&
                     decoded.group_index == group_index &&
                     decoded.block_bitmap_checksum == descriptor.block_bitmap_checksum;
        if (iteration % 1000ULL == 0ULL) {
            os::kernel::fs::RootV5GroupDescriptor corrupt = descriptor;
            ++corrupt.data_start_block;
            consistent =
                consistent && os::kernel::fs::ValidateRootV5GroupDescriptor(superblock, corrupt) ==
                                  os::kernel::fs::RootV5FormatStatus::InvalidGroup;
        }
    }
    test_context.ExpectRandom(consistent, OS_TEST_ROOTFS_V5_RANDOMIZED_MESSAGE,
                              OS_TEST_ROOTFS_V5_RANDOMIZED_SEED,
                              OS_TEST_ROOTFS_V5_RANDOMIZED_ITERATION_COUNT);
    return test_context.ExitCode();
}
