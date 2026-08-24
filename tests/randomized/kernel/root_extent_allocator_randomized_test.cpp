#include <os/kernel/fs/root_delayed_allocation.hpp>
#include <test_context.hpp>

#include <array>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_EXTENT_RANDOM_SUITE_NAME =
    "kernel/root_extent_allocator/randomized";
constexpr std::string_view OS_TEST_ROOT_EXTENT_RANDOM_MESSAGE =
    "十万步 delalloc/extent/allocator/range 操作必须与逐逻辑块独立 oracle 一致";
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_SEED = 0x455854414C4C4F43ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_LEFT_SHIFT = 13ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_RIGHT_SHIFT = 7ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_FINAL_LEFT_SHIFT = 17ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_OPERATION_MODULUS = 1000ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_RESERVE_THRESHOLD = 15ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_COMPLETE_THRESHOLD = 25ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_ABORT_THRESHOLD = 30ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_FALLOCATE_THRESHOLD = 40ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_PUNCH_THRESHOLD = 50ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_CONVERT_THRESHOLD = 55ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_TRUNCATE_THRESHOLD = 58ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_OBSERVATION_INTERVAL = 64ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT = 64ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_TOTAL_BLOCK_COUNT = 1000ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_BLOCKS_PER_GROUP = 256ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_INODES_PER_GROUP = 64ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_GROUP_COUNT = 4ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_START_BLOCK = 16ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_BLOCK_COUNT = 81ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_RANDOM_BITMAP_STORAGE_SIZE_BYTES =
    OS_TEST_ROOT_EXTENT_RANDOM_GROUP_COUNT * os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
constexpr uint8_t OS_TEST_ROOT_EXTENT_RANDOM_STATE_HOLE = 0U;
constexpr uint8_t OS_TEST_ROOT_EXTENT_RANDOM_STATE_DELAYED = 1U;
constexpr uint8_t OS_TEST_ROOT_EXTENT_RANDOM_STATE_UNWRITTEN = 2U;
constexpr uint8_t OS_TEST_ROOT_EXTENT_RANDOM_STATE_INITIALIZED = 3U;
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_EXTENT_RANDOM_UUID{
    .low = 0x52414E4445585431ULL,
    .high = 0x1020304050607080ULL,
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << OS_TEST_ROOT_EXTENT_RANDOM_LEFT_SHIFT;
    state ^= state >> OS_TEST_ROOT_EXTENT_RANDOM_RIGHT_SHIFT;
    state ^= state << OS_TEST_ROOT_EXTENT_RANDOM_FINAL_LEFT_SHIFT;
    return state;
}

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
            .device_sector_count = 8ULL + OS_TEST_ROOT_EXTENT_RANDOM_TOTAL_BLOCK_COUNT * 8ULL,
            .blocks_per_group = OS_TEST_ROOT_EXTENT_RANDOM_BLOCKS_PER_GROUP,
            .group_descriptor_size_bytes =
                os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES,
            .inode_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES,
            .inodes_per_group = OS_TEST_ROOT_EXTENT_RANDOM_INODES_PER_GROUP,
            .creation_time_nanoseconds = 1ULL,
            .uuid = OS_TEST_ROOT_EXTENT_RANDOM_UUID,
        },
        superblock));
    return superblock;
}

void BuildAllocatorStorage(const os::kernel::fs::RootV5Superblock &superblock,
                           os::kernel::fs::RootV5GroupDescriptor *const descriptors,
                           uint8_t *const bitmaps, uint64_t *const free_counts) noexcept {
    for (uint64_t byte_index = 0ULL;
         byte_index < OS_TEST_ROOT_EXTENT_RANDOM_BITMAP_STORAGE_SIZE_BYTES; ++byte_index) {
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
    for (uint64_t physical_block = OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_START_BLOCK;
         physical_block < OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_START_BLOCK +
                              OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_BLOCK_COUNT;
         ++physical_block) {
        const uint64_t group_index = physical_block / superblock.blocks_per_group;
        const uint64_t group_block = physical_block - descriptors[group_index].first_block;
        SetBit(bitmaps, group_index, group_block);
        --free_counts[group_index];
    }
}

[[nodiscard]] bool ObserveState(
    os::kernel::fs::RootDelayedAllocation &delayed, os::kernel::fs::RootExtentTree &tree,
    os::kernel::fs::RootBlockGroupAllocator &allocator,
    const std::array<uint8_t, OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT> &states,
    const std::array<uint64_t, OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT> &physical_blocks,
    const uint64_t expected_file_size, const uint64_t initial_free_count,
    uint64_t &random_state) noexcept {
    if (delayed.FileSizeBlocks() != expected_file_size ||
        delayed.Validate(tree) != os::kernel::fs::RootDelayedAllocationStatus::Succeeded ||
        tree.Validate() != os::kernel::fs::RootExtentStatus::Succeeded ||
        allocator.Validate() != os::kernel::fs::RootBlockAllocatorStatus::Succeeded) {
        return false;
    }
    uint64_t mapped_count = 0ULL;
    for (uint64_t block_index = 0ULL; block_index < states.size(); ++block_index) {
        if (states[block_index] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_UNWRITTEN ||
            states[block_index] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_INITIALIZED) {
            ++mapped_count;
        }
    }
    if (allocator.FreeBlockCount() != initial_free_count - mapped_count) {
        return false;
    }
    const uint64_t observed_block =
        NextRandom(random_state) % OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT;
    os::kernel::fs::RootExtent extent{};
    os::kernel::fs::RootFileRange range{};
    uint64_t range_count = 0ULL;
    const os::kernel::fs::RootExtentStatus lookup_status = tree.Lookup(observed_block, extent);
    if (states[observed_block] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_UNWRITTEN ||
        states[observed_block] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_INITIALIZED) {
        const os::kernel::fs::RootExtentState expected_state =
            states[observed_block] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_UNWRITTEN
                ? os::kernel::fs::RootExtentState::Unwritten
                : os::kernel::fs::RootExtentState::Initialized;
        if (lookup_status != os::kernel::fs::RootExtentStatus::Succeeded ||
            extent.state != expected_state ||
            extent.physical_start_block + observed_block - extent.logical_start_block !=
                physical_blocks[observed_block]) {
            return false;
        }
    } else if (lookup_status != os::kernel::fs::RootExtentStatus::NotFound) {
        return false;
    }
    if (delayed.QueryRanges(observed_block, 1ULL, tree, &range, 1ULL, range_count) !=
        os::kernel::fs::RootDelayedAllocationStatus::Succeeded) {
        return false;
    }
    const uint8_t state = states[observed_block];
    if (state == OS_TEST_ROOT_EXTENT_RANDOM_STATE_HOLE && range_count != 0ULL) {
        return false;
    }
    if (state != OS_TEST_ROOT_EXTENT_RANDOM_STATE_HOLE && range_count != 1ULL) {
        return false;
    }
    return state != OS_TEST_ROOT_EXTENT_RANDOM_STATE_DELAYED ||
           range.kind == os::kernel::fs::RootFileRangeKind::Delayed;
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_EXTENT_RANDOM_SUITE_NAME};
    const os::kernel::fs::RootV5Superblock superblock = MakeSuperblock();
    os::kernel::fs::RootV5GroupDescriptor descriptors[OS_TEST_ROOT_EXTENT_RANDOM_GROUP_COUNT]{};
    uint8_t bitmaps[OS_TEST_ROOT_EXTENT_RANDOM_BITMAP_STORAGE_SIZE_BYTES]{};
    uint64_t free_counts[OS_TEST_ROOT_EXTENT_RANDOM_GROUP_COUNT]{};
    BuildAllocatorStorage(superblock, descriptors, bitmaps, free_counts);
    const uint64_t initial_free_count =
        free_counts[0] + free_counts[1] + free_counts[2] + free_counts[3];
    os::kernel::fs::RootBlockGroupAllocator allocator{};
    os::kernel::fs::RootExtentTree tree{};
    os::kernel::fs::RootDelayedAllocation delayed{};
    bool consistent =
        allocator.Initialize(superblock, descriptors, OS_TEST_ROOT_EXTENT_RANDOM_GROUP_COUNT,
                             bitmaps, sizeof(bitmaps), free_counts,
                             OS_TEST_ROOT_EXTENT_RANDOM_GROUP_COUNT,
                             OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_START_BLOCK,
                             OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_BLOCK_COUNT) ==
            os::kernel::fs::RootBlockAllocatorStatus::Succeeded &&
        tree.Initialize(
            superblock.total_block_count, OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_START_BLOCK,
            OS_TEST_ROOT_EXTENT_RANDOM_JOURNAL_BLOCK_COUNT, 32ULL, 1ULL,
            OS_TEST_ROOT_EXTENT_RANDOM_UUID) == os::kernel::fs::RootExtentStatus::Succeeded &&
        delayed.Initialize(0ULL) == os::kernel::fs::RootDelayedAllocationStatus::Succeeded;
    std::array<uint8_t, OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT> states{};
    std::array<uint64_t, OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT> physical_blocks{};
    uint64_t expected_file_size = 0ULL;
    uint64_t random_state = OS_TEST_ROOT_EXTENT_RANDOM_SEED;

    for (uint64_t step = 0ULL; consistent && step < OS_TEST_ROOT_EXTENT_RANDOM_STEP_COUNT; ++step) {
        const uint64_t operation =
            NextRandom(random_state) % OS_TEST_ROOT_EXTENT_RANDOM_OPERATION_MODULUS;
        const uint64_t block_index =
            NextRandom(random_state) % OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT;
        if (operation < OS_TEST_ROOT_EXTENT_RANDOM_RESERVE_THRESHOLD &&
            states[block_index] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_HOLE) {
            consistent = delayed.ReserveWrite(block_index, 1ULL, tree) ==
                         os::kernel::fs::RootDelayedAllocationStatus::Succeeded;
            if (consistent) {
                states[block_index] = OS_TEST_ROOT_EXTENT_RANDOM_STATE_DELAYED;
                if (block_index + 1ULL > expected_file_size) {
                    expected_file_size = block_index + 1ULL;
                }
            }
        } else if (operation < OS_TEST_ROOT_EXTENT_RANDOM_COMPLETE_THRESHOLD &&
                   states[block_index] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_DELAYED) {
            os::kernel::fs::RootWritebackToken token{};
            consistent = delayed.BeginWriteback(
                             block_index, 1ULL,
                             block_index % OS_TEST_ROOT_EXTENT_RANDOM_GROUP_COUNT, allocator, tree,
                             token) == os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
                         delayed.CompleteWriteback(token, allocator, tree) ==
                             os::kernel::fs::RootDelayedAllocationStatus::Succeeded;
            if (consistent) {
                states[block_index] = OS_TEST_ROOT_EXTENT_RANDOM_STATE_INITIALIZED;
                physical_blocks[block_index] = token.allocation.physical_start_block;
            }
        } else if (operation < OS_TEST_ROOT_EXTENT_RANDOM_ABORT_THRESHOLD &&
                   states[block_index] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_DELAYED) {
            os::kernel::fs::RootWritebackToken token{};
            consistent = delayed.BeginWriteback(block_index, 1ULL, 0ULL, allocator, tree, token) ==
                             os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
                         delayed.AbortWriteback(token, allocator, tree) ==
                             os::kernel::fs::RootDelayedAllocationStatus::Succeeded;
        } else if (operation < OS_TEST_ROOT_EXTENT_RANDOM_FALLOCATE_THRESHOLD &&
                   states[block_index] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_HOLE) {
            consistent =
                delayed.Fallocate(block_index, 1ULL, false,
                                  block_index % OS_TEST_ROOT_EXTENT_RANDOM_GROUP_COUNT, allocator,
                                  tree) == os::kernel::fs::RootDelayedAllocationStatus::Succeeded;
            if (consistent) {
                os::kernel::fs::RootExtent extent{};
                consistent =
                    tree.Lookup(block_index, extent) == os::kernel::fs::RootExtentStatus::Succeeded;
                states[block_index] = OS_TEST_ROOT_EXTENT_RANDOM_STATE_UNWRITTEN;
                physical_blocks[block_index] =
                    extent.physical_start_block + block_index - extent.logical_start_block;
                if (block_index + 1ULL > expected_file_size) {
                    expected_file_size = block_index + 1ULL;
                }
            }
        } else if (operation < OS_TEST_ROOT_EXTENT_RANDOM_PUNCH_THRESHOLD &&
                   states[block_index] != OS_TEST_ROOT_EXTENT_RANDOM_STATE_HOLE) {
            consistent = delayed.PunchHole(block_index, 1ULL, allocator, tree) ==
                         os::kernel::fs::RootDelayedAllocationStatus::Succeeded;
            if (consistent) {
                states[block_index] = OS_TEST_ROOT_EXTENT_RANDOM_STATE_HOLE;
                physical_blocks[block_index] = 0ULL;
            }
        } else if (operation < OS_TEST_ROOT_EXTENT_RANDOM_CONVERT_THRESHOLD &&
                   states[block_index] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_UNWRITTEN) {
            consistent = tree.Convert(block_index, 1ULL, os::kernel::fs::RootExtentState::Unwritten,
                                      os::kernel::fs::RootExtentState::Initialized) ==
                         os::kernel::fs::RootExtentStatus::Succeeded;
            if (consistent) {
                states[block_index] = OS_TEST_ROOT_EXTENT_RANDOM_STATE_INITIALIZED;
            }
        } else if (operation < OS_TEST_ROOT_EXTENT_RANDOM_TRUNCATE_THRESHOLD) {
            const uint64_t new_size =
                NextRandom(random_state) % (OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT + 1ULL);
            consistent = delayed.Truncate(new_size, allocator, tree) ==
                         os::kernel::fs::RootDelayedAllocationStatus::Succeeded;
            if (consistent) {
                if (new_size < expected_file_size) {
                    for (uint64_t clear_index = new_size;
                         clear_index < OS_TEST_ROOT_EXTENT_RANDOM_LOGICAL_BLOCK_COUNT;
                         ++clear_index) {
                        states[clear_index] = OS_TEST_ROOT_EXTENT_RANDOM_STATE_HOLE;
                        physical_blocks[clear_index] = 0ULL;
                    }
                }
                expected_file_size = new_size;
            }
        }
        if (consistent && step % OS_TEST_ROOT_EXTENT_RANDOM_OBSERVATION_INTERVAL == 0ULL) {
            consistent = ObserveState(delayed, tree, allocator, states, physical_blocks,
                                      expected_file_size, initial_free_count, random_state);
            if (consistent && expected_file_size != 0ULL) {
                const uint64_t seek_start = NextRandom(random_state) % expected_file_size;
                uint64_t expected_data = expected_file_size;
                uint64_t expected_hole = expected_file_size;
                for (uint64_t logical_block = seek_start; logical_block < expected_file_size;
                     ++logical_block) {
                    const bool data =
                        states[logical_block] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_DELAYED ||
                        states[logical_block] == OS_TEST_ROOT_EXTENT_RANDOM_STATE_INITIALIZED;
                    if (data && expected_data == expected_file_size) {
                        expected_data = logical_block;
                    }
                    if (!data && expected_hole == expected_file_size) {
                        expected_hole = logical_block;
                    }
                }
                uint64_t observed = 0ULL;
                const os::kernel::fs::RootDelayedAllocationStatus data_status =
                    delayed.SeekData(seek_start, tree, observed);
                consistent =
                    expected_data == expected_file_size
                        ? data_status == os::kernel::fs::RootDelayedAllocationStatus::NotFound
                        : data_status == os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
                              observed == expected_data;
                if (consistent) {
                    consistent = delayed.SeekHole(seek_start, tree, observed) ==
                                     os::kernel::fs::RootDelayedAllocationStatus::Succeeded &&
                                 observed == expected_hole;
                }
            }
        }
    }
    consistent = consistent && ObserveState(delayed, tree, allocator, states, physical_blocks,
                                            expected_file_size, initial_free_count, random_state);
    context.ExpectRandom(consistent, OS_TEST_ROOT_EXTENT_RANDOM_MESSAGE,
                         OS_TEST_ROOT_EXTENT_RANDOM_SEED, OS_TEST_ROOT_EXTENT_RANDOM_STEP_COUNT);
    return context.ExitCode();
}
