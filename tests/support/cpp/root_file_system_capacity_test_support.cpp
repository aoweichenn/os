#include "root_file_system_capacity_test_support.hpp"

#include "os/kernel/fs/root_file_system_format.hpp"

namespace os::test {

namespace {

constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_ROOT_INODE_NUMBER = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_FIRST_RESERVOIR_INODE_NUMBER = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_RESERVOIR_FILE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_TARGET_INODE_NUMBER = 6ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_ALLOCATED_INODE_COUNT = 6ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_NEXT_INODE_GENERATION = 7ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_ROOT_LINK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_FILE_LINK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_SINGLE_INDIRECT_LEVEL = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_DOUBLE_INDIRECT_LEVEL = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_TRIPLE_INDIRECT_LEVEL = 3ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_BINARY_SEARCH_DIVISOR = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_DIRECTORY_ENTRY_COUNT =
    OS_TEST_ROOTFS_CAPACITY_RESERVOIR_FILE_COUNT + OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_RESERVOIR_NAME_LENGTH_BYTES = 9ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_TARGET_NAME_LENGTH_BYTES = 8ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_INODE_TABLE_BLOCK_COUNT = 3ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_INITIAL_TRANSACTION_GENERATION = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_TOTAL_BLOCK_COUNT = 32768ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_INODE_COUNT = 128ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_INODE_BITMAP_START_RELATIVE_BLOCK =
    os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
    os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_INODE_BITMAP_BLOCK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_INODE_TABLE_START_RELATIVE_BLOCK =
    OS_TEST_ROOTFS_CAPACITY_INODE_BITMAP_START_RELATIVE_BLOCK +
    OS_TEST_ROOTFS_CAPACITY_INODE_BITMAP_BLOCK_COUNT;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_FULL_INODE_TABLE_BLOCK_COUNT = 64ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_START_RELATIVE_BLOCK =
    OS_TEST_ROOTFS_CAPACITY_INODE_TABLE_START_RELATIVE_BLOCK +
    OS_TEST_ROOTFS_CAPACITY_FULL_INODE_TABLE_BLOCK_COUNT;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_BLOCK_COUNT = 7ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_DATA_START_RELATIVE_BLOCK =
    OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_START_RELATIVE_BLOCK +
    OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_BLOCK_COUNT;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_DATA_BLOCK_COUNT =
    OS_TEST_ROOTFS_CAPACITY_TOTAL_BLOCK_COUNT - OS_TEST_ROOTFS_CAPACITY_DATA_START_RELATIVE_BLOCK;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_MAXIMUM_FILE_SIZE_BYTES =
    OS_TEST_ROOTFS_CAPACITY_DATA_BLOCK_COUNT * os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_RESERVOIR_MAXIMUM_DATA_BLOCK_COUNT = 8192ULL;
constexpr uint8_t OS_TEST_ROOTFS_CAPACITY_ZERO_BYTE = 0U;

constexpr uint8_t
    OS_TEST_ROOTFS_CAPACITY_RESERVOIR_NAMES[OS_TEST_ROOTFS_CAPACITY_RESERVOIR_FILE_COUNT]
                                           [OS_TEST_ROOTFS_CAPACITY_RESERVOIR_NAME_LENGTH_BYTES] = {
                                               {'r', 'e', 's', 'e', 'r', 'v', 'e', '-', 'a'},
                                               {'r', 'e', 's', 'e', 'r', 'v', 'e', '-', 'b'},
                                               {'r', 'e', 's', 'e', 'r', 'v', 'e', '-', 'c'},
                                               {'r', 'e', 's', 'e', 'r', 'v', 'e', '-', 'd'},
};
constexpr uint8_t
    OS_TEST_ROOTFS_CAPACITY_TARGET_NAME[OS_TEST_ROOTFS_CAPACITY_TARGET_NAME_LENGTH_BYTES] = {
        'c', 'a', 'p', 'a', 'c', 'i', 't', 'y',
};

struct CapacityBuilderState final {
    os::kernel::FileSystemBlockDevice *device;
    uint8_t *data_bitmap;
    uint64_t next_data_bit;
};

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] constexpr uint64_t DivideRoundUp(const uint64_t value,
                                               const uint64_t divisor) noexcept {
    return value == OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE
               ? OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE
               : (value - OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT) / divisor +
                     OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT;
}

void ClearBytes(uint8_t *const destination, const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = OS_TEST_ROOTFS_CAPACITY_ZERO_BYTE;
    }
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

void SetBitmapBit(uint8_t *const bitmap, const uint64_t bit_index) noexcept {
    const uint64_t byte_index = bit_index / OS_TEST_ROOTFS_CAPACITY_BITS_PER_BYTE;
    const uint64_t bit_offset = bit_index % OS_TEST_ROOTFS_CAPACITY_BITS_PER_BYTE;
    bitmap[byte_index] = static_cast<uint8_t>(
        bitmap[byte_index] |
        static_cast<uint8_t>(OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT << bit_offset));
}

[[nodiscard]] bool WriteRelativeBlock(os::kernel::FileSystemBlockDevice &device,
                                      const uint64_t relative_block,
                                      const uint8_t *const block) noexcept {
    return device.WriteBlock(os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA + relative_block, block,
                             os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) ==
           os::kernel::FileSystemBlockDeviceStatus::Succeeded;
}

[[nodiscard]] bool AllocateBlock(CapacityBuilderState &state, uint64_t &relative_block) noexcept {
    relative_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    if (state.device == nullptr || state.data_bitmap == nullptr ||
        state.next_data_bit >= OS_TEST_ROOTFS_CAPACITY_DATA_BLOCK_COUNT) {
        return false;
    }
    SetBitmapBit(state.data_bitmap, state.next_data_bit);
    relative_block = OS_TEST_ROOTFS_CAPACITY_DATA_START_RELATIVE_BLOCK + state.next_data_bit;
    ++state.next_data_bit;
    return true;
}

[[nodiscard]] uint64_t IndirectCapacity(const uint64_t level) noexcept {
    uint64_t capacity = OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT;
    for (uint64_t current_level = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX; current_level < level;
         ++current_level) {
        capacity *= os::kernel::fs::OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    }
    return capacity;
}

[[nodiscard]] uint64_t MetadataBlockCountForDataBlocks(const uint64_t data_block_count) noexcept {
    uint64_t remaining =
        data_block_count > os::kernel::fs::OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT
            ? data_block_count - os::kernel::fs::OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT
            : OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    uint64_t metadata_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    const uint64_t single_data_block_count =
        Minimum(remaining, IndirectCapacity(OS_TEST_ROOTFS_CAPACITY_SINGLE_INDIRECT_LEVEL));
    if (single_data_block_count != OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE) {
        ++metadata_block_count;
        remaining -= single_data_block_count;
    }
    const uint64_t double_data_block_count =
        Minimum(remaining, IndirectCapacity(OS_TEST_ROOTFS_CAPACITY_DOUBLE_INDIRECT_LEVEL));
    if (double_data_block_count != OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE) {
        metadata_block_count +=
            OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT +
            DivideRoundUp(double_data_block_count,
                          os::kernel::fs::OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK);
        remaining -= double_data_block_count;
    }
    if (remaining != OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE) {
        const uint64_t double_capacity =
            IndirectCapacity(OS_TEST_ROOTFS_CAPACITY_DOUBLE_INDIRECT_LEVEL);
        metadata_block_count +=
            OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT + DivideRoundUp(remaining, double_capacity) +
            DivideRoundUp(remaining, os::kernel::fs::OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK);
    }
    return metadata_block_count;
}

[[nodiscard]] bool BuildPointerTree(CapacityBuilderState &state, const uint64_t level,
                                    const uint64_t data_block_count, uint64_t &root_block,
                                    uint64_t &metadata_block_count,
                                    uint64_t &allocated_data_block_count) noexcept {
    root_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    if (level == OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE ||
        level > OS_TEST_ROOTFS_CAPACITY_TRIPLE_INDIRECT_LEVEL ||
        data_block_count == OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE ||
        data_block_count > IndirectCapacity(level) || !AllocateBlock(state, root_block)) {
        return false;
    }
    ++metadata_block_count;
    os::kernel::fs::RootPointerBlock pointer_block{};
    uint64_t remaining = data_block_count;
    const uint64_t child_capacity =
        IndirectCapacity(level - OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT);
    for (uint64_t pointer_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         pointer_index < os::kernel::fs::OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK &&
         remaining != OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
         ++pointer_index) {
        const uint64_t child_data_block_count = Minimum(remaining, child_capacity);
        uint64_t child_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
        if (level == OS_TEST_ROOTFS_CAPACITY_SINGLE_INDIRECT_LEVEL) {
            if (!AllocateBlock(state, child_block)) {
                return false;
            }
            ++allocated_data_block_count;
        } else if (!BuildPointerTree(state, level - OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT,
                                     child_data_block_count, child_block, metadata_block_count,
                                     allocated_data_block_count)) {
            return false;
        }
        pointer_block.pointers[pointer_index] = child_block;
        remaining -= child_data_block_count;
    }
    if (remaining != OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE) {
        return false;
    }
    uint8_t block[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    return os::kernel::fs::EncodeRootPointerBlock(pointer_block, block, sizeof(block)) ==
               os::kernel::fs::RootFormatStatus::Succeeded &&
           WriteRelativeBlock(*state.device, root_block, block);
}

[[nodiscard]] bool BuildFileInode(CapacityBuilderState &state, const uint64_t inode_number,
                                  const uint64_t data_block_count,
                                  os::kernel::fs::RootInode &inode) noexcept {
    inode = os::kernel::fs::RootInode{
        .type = os::kernel::fs::RootNodeType::RegularFile,
        .flags = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .size_bytes = data_block_count * os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
        .generation = inode_number,
        .link_count = OS_TEST_ROOTFS_CAPACITY_FILE_LINK_COUNT,
        .allocated_data_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .allocated_metadata_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .parent_inode_number = OS_TEST_ROOTFS_CAPACITY_ROOT_INODE_NUMBER,
        .direct_blocks = {},
        .single_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .double_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .triple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .quadruple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .quintuple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .access_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .modification_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .change_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .birth_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
    };
    uint64_t remaining = data_block_count;
    const uint64_t direct_data_block_count =
        Minimum(remaining, os::kernel::fs::OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT);
    for (uint64_t direct_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         direct_index < direct_data_block_count; ++direct_index) {
        if (!AllocateBlock(state, inode.direct_blocks[direct_index])) {
            return false;
        }
        ++inode.allocated_data_block_count;
    }
    remaining -= direct_data_block_count;

    const uint64_t single_data_block_count =
        Minimum(remaining, IndirectCapacity(OS_TEST_ROOTFS_CAPACITY_SINGLE_INDIRECT_LEVEL));
    if (single_data_block_count != OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE &&
        !BuildPointerTree(state, OS_TEST_ROOTFS_CAPACITY_SINGLE_INDIRECT_LEVEL,
                          single_data_block_count, inode.single_indirect_block,
                          inode.allocated_metadata_block_count, inode.allocated_data_block_count)) {
        return false;
    }
    remaining -= single_data_block_count;

    const uint64_t double_data_block_count =
        Minimum(remaining, IndirectCapacity(OS_TEST_ROOTFS_CAPACITY_DOUBLE_INDIRECT_LEVEL));
    if (double_data_block_count != OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE &&
        !BuildPointerTree(state, OS_TEST_ROOTFS_CAPACITY_DOUBLE_INDIRECT_LEVEL,
                          double_data_block_count, inode.double_indirect_block,
                          inode.allocated_metadata_block_count, inode.allocated_data_block_count)) {
        return false;
    }
    remaining -= double_data_block_count;

    if (remaining != OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE &&
        !BuildPointerTree(state, OS_TEST_ROOTFS_CAPACITY_TRIPLE_INDIRECT_LEVEL, remaining,
                          inode.triple_indirect_block, inode.allocated_metadata_block_count,
                          inode.allocated_data_block_count)) {
        return false;
    }
    return inode.allocated_data_block_count == data_block_count &&
           inode.allocated_metadata_block_count ==
               MetadataBlockCountForDataBlocks(data_block_count);
}

[[nodiscard]] uint64_t SelectLastReservoirDataBlockCount(const uint64_t block_budget) noexcept {
    const uint64_t maximum_data_block_count =
        OS_TEST_ROOTFS_CAPACITY_RESERVOIR_MAXIMUM_DATA_BLOCK_COUNT;
    uint64_t lower = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    uint64_t upper = maximum_data_block_count;
    uint64_t selected = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    while (lower <= upper) {
        const uint64_t candidate =
            lower + (upper - lower) / OS_TEST_ROOTFS_CAPACITY_BINARY_SEARCH_DIVISOR;
        const uint64_t required = candidate + MetadataBlockCountForDataBlocks(candidate);
        if (required <= block_budget) {
            selected = candidate;
            lower = candidate + OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT;
        } else {
            if (candidate == OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE) {
                break;
            }
            upper = candidate - OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT;
        }
    }
    return selected;
}

[[nodiscard]] bool EncodeDirectory(os::kernel::fs::RootInode &root_inode,
                                   CapacityBuilderState &state) noexcept {
    constexpr uint64_t directory_size_bytes =
        OS_TEST_ROOTFS_CAPACITY_DIRECTORY_ENTRY_COUNT *
        os::kernel::fs::OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES;
    constexpr uint64_t directory_block_count =
        DivideRoundUp(directory_size_bytes, os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    static_assert(directory_block_count <= os::kernel::fs::OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT);
    uint8_t directory_bytes[directory_block_count *
                            os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    for (uint64_t entry_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         entry_index < OS_TEST_ROOTFS_CAPACITY_DIRECTORY_ENTRY_COUNT; ++entry_index) {
        const bool target = entry_index == OS_TEST_ROOTFS_CAPACITY_RESERVOIR_FILE_COUNT;
        const uint64_t inode_number =
            target ? OS_TEST_ROOTFS_CAPACITY_TARGET_INODE_NUMBER
                   : OS_TEST_ROOTFS_CAPACITY_FIRST_RESERVOIR_INODE_NUMBER + entry_index;
        const uint8_t *const name = target ? OS_TEST_ROOTFS_CAPACITY_TARGET_NAME
                                           : OS_TEST_ROOTFS_CAPACITY_RESERVOIR_NAMES[entry_index];
        const uint64_t name_length_bytes =
            target ? OS_TEST_ROOTFS_CAPACITY_TARGET_NAME_LENGTH_BYTES
                   : OS_TEST_ROOTFS_CAPACITY_RESERVOIR_NAME_LENGTH_BYTES;
        os::kernel::fs::RootDirectoryEntry entry{
            .inode_number = inode_number,
            .inode_generation = inode_number,
            .type = os::kernel::fs::RootNodeType::RegularFile,
            .name_length_bytes = name_length_bytes,
            .name = {},
        };
        CopyBytes(entry.name, name, name_length_bytes);
        uint8_t encoded[os::kernel::fs::OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES]{};
        if (os::kernel::fs::EncodeRootDirectoryEntry(entry, encoded, sizeof(encoded)) !=
            os::kernel::fs::RootFormatStatus::Succeeded) {
            return false;
        }
        CopyBytes(directory_bytes +
                      entry_index * os::kernel::fs::OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES,
                  encoded, sizeof(encoded));
    }

    root_inode.size_bytes = directory_size_bytes;
    root_inode.allocated_data_block_count = directory_block_count;
    for (uint64_t block_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         block_index < directory_block_count; ++block_index) {
        if (!AllocateBlock(state, root_inode.direct_blocks[block_index]) ||
            !WriteRelativeBlock(*state.device, root_inode.direct_blocks[block_index],
                                directory_bytes +
                                    block_index *
                                        os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool WriteInodeTable(os::kernel::FileSystemBlockDevice &device,
                                   const os::kernel::fs::RootInode *const inodes) noexcept {
    uint8_t inode_table[OS_TEST_ROOTFS_CAPACITY_INODE_TABLE_BLOCK_COUNT]
                       [os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    for (uint64_t inode_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         inode_index < OS_TEST_ROOTFS_CAPACITY_ALLOCATED_INODE_COUNT; ++inode_index) {
        const uint64_t block_index =
            inode_index / os::kernel::fs::OS_KERNEL_ROOTFS_INODES_PER_BLOCK;
        const uint64_t block_offset_bytes =
            (inode_index % os::kernel::fs::OS_KERNEL_ROOTFS_INODES_PER_BLOCK) *
            os::kernel::fs::OS_KERNEL_ROOTFS_INODE_SIZE_BYTES;
        if (os::kernel::fs::EncodeRootInode(inodes[inode_index],
                                            inode_table[block_index] + block_offset_bytes,
                                            os::kernel::fs::OS_KERNEL_ROOTFS_INODE_SIZE_BYTES) !=
            os::kernel::fs::RootFormatStatus::Succeeded) {
            return false;
        }
    }
    for (uint64_t block_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         block_index < OS_TEST_ROOTFS_CAPACITY_INODE_TABLE_BLOCK_COUNT; ++block_index) {
        if (!WriteRelativeBlock(
                device, OS_TEST_ROOTFS_CAPACITY_INODE_TABLE_START_RELATIVE_BLOCK + block_index,
                inode_table[block_index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool WriteBitmaps(os::kernel::FileSystemBlockDevice &device,
                                const uint8_t *const data_bitmap) noexcept {
    uint8_t inode_bitmap[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    for (uint64_t inode_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         inode_index < OS_TEST_ROOTFS_CAPACITY_ALLOCATED_INODE_COUNT; ++inode_index) {
        SetBitmapBit(inode_bitmap, inode_index);
    }
    if (!WriteRelativeBlock(device, OS_TEST_ROOTFS_CAPACITY_INODE_BITMAP_START_RELATIVE_BLOCK,
                            inode_bitmap)) {
        return false;
    }
    for (uint64_t block_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         block_index < OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_BLOCK_COUNT; ++block_index) {
        if (!WriteRelativeBlock(
                device, OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_START_RELATIVE_BLOCK + block_index,
                data_bitmap + block_index * os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool WriteSuperblock(os::kernel::FileSystemBlockDevice &device,
                                   const os::kernel::fs::RootInode *const inodes) noexcept {
    uint64_t allocated_data_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    uint64_t allocated_metadata_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    for (uint64_t inode_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         inode_index < OS_TEST_ROOTFS_CAPACITY_ALLOCATED_INODE_COUNT; ++inode_index) {
        allocated_data_block_count += inodes[inode_index].allocated_data_block_count;
        allocated_metadata_block_count += inodes[inode_index].allocated_metadata_block_count;
    }
    const os::kernel::fs::RootSuperblock superblock{
        .version = os::kernel::fs::OS_KERNEL_ROOTFS_FORMAT_VERSION,
        .block_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
        .total_block_count = OS_TEST_ROOTFS_CAPACITY_TOTAL_BLOCK_COUNT,
        .journal_start_relative_block =
            os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK,
        .journal_block_count = os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT,
        .inode_bitmap_start_relative_block =
            OS_TEST_ROOTFS_CAPACITY_INODE_BITMAP_START_RELATIVE_BLOCK,
        .inode_bitmap_block_count = OS_TEST_ROOTFS_CAPACITY_INODE_BITMAP_BLOCK_COUNT,
        .inode_table_start_relative_block =
            OS_TEST_ROOTFS_CAPACITY_INODE_TABLE_START_RELATIVE_BLOCK,
        .inode_table_block_count = OS_TEST_ROOTFS_CAPACITY_FULL_INODE_TABLE_BLOCK_COUNT,
        .data_bitmap_start_relative_block =
            OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_START_RELATIVE_BLOCK,
        .data_bitmap_block_count = OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_BLOCK_COUNT,
        .data_start_relative_block = OS_TEST_ROOTFS_CAPACITY_DATA_START_RELATIVE_BLOCK,
        .data_block_count = OS_TEST_ROOTFS_CAPACITY_DATA_BLOCK_COUNT,
        .inode_count = OS_TEST_ROOTFS_CAPACITY_INODE_COUNT,
        .root_inode_number = os::kernel::fs::OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER,
        .maximum_file_size_bytes = OS_TEST_ROOTFS_CAPACITY_MAXIMUM_FILE_SIZE_BYTES,
        .transaction_state = os::kernel::fs::RootTransactionState::Clean,
        .transaction_generation = OS_TEST_ROOTFS_CAPACITY_INITIAL_TRANSACTION_GENERATION,
        .next_inode_generation = OS_TEST_ROOTFS_CAPACITY_NEXT_INODE_GENERATION,
        .feature_flags = os::kernel::fs::OS_KERNEL_ROOTFS_REQUIRED_FEATURES,
        .allocated_inode_count = OS_TEST_ROOTFS_CAPACITY_ALLOCATED_INODE_COUNT,
        .allocated_data_block_count = allocated_data_block_count,
        .allocated_metadata_block_count = allocated_metadata_block_count,
    };
    uint8_t block[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    return os::kernel::fs::EncodeRootSuperblock(superblock, block, sizeof(block)) ==
               os::kernel::fs::RootFormatStatus::Succeeded &&
           WriteRelativeBlock(device, os::kernel::fs::OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK,
                              block);
}

}

bool FormatNearCapacityRootFileSystem(
    os::kernel::FileSystemBlockDevice &device, const uint64_t requested_free_data_block_count,
    RootFileSystemCapacityImageInformation &information) noexcept {
    information = RootFileSystemCapacityImageInformation{};
    if (requested_free_data_block_count == OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE ||
        requested_free_data_block_count >= OS_TEST_ROOTFS_CAPACITY_DATA_BLOCK_COUNT) {
        return false;
    }
    uint8_t data_bitmap[OS_TEST_ROOTFS_CAPACITY_DATA_BITMAP_BLOCK_COUNT *
                        os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    ClearBytes(data_bitmap, sizeof(data_bitmap));
    CapacityBuilderState state{
        .device = &device,
        .data_bitmap = data_bitmap,
        .next_data_bit = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
    };
    os::kernel::fs::RootInode inodes[OS_TEST_ROOTFS_CAPACITY_ALLOCATED_INODE_COUNT]{};
    inodes[OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX] = os::kernel::fs::RootInode{
        .type = os::kernel::fs::RootNodeType::Directory,
        .flags = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .size_bytes = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .generation = OS_TEST_ROOTFS_CAPACITY_ROOT_INODE_NUMBER,
        .link_count = OS_TEST_ROOTFS_CAPACITY_ROOT_LINK_COUNT,
        .allocated_data_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .allocated_metadata_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .parent_inode_number = OS_TEST_ROOTFS_CAPACITY_ROOT_INODE_NUMBER,
        .direct_blocks = {},
        .single_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .double_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .triple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .quadruple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .quintuple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .access_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .modification_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .change_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .birth_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
    };
    if (!EncodeDirectory(inodes[OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX], state)) {
        return false;
    }

    const uint64_t maximum_data_block_count =
        OS_TEST_ROOTFS_CAPACITY_RESERVOIR_MAXIMUM_DATA_BLOCK_COUNT;
    for (uint64_t reservoir_index = OS_TEST_ROOTFS_CAPACITY_FIRST_INDEX;
         reservoir_index <
         OS_TEST_ROOTFS_CAPACITY_RESERVOIR_FILE_COUNT - OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT;
         ++reservoir_index) {
        const uint64_t inode_number =
            OS_TEST_ROOTFS_CAPACITY_FIRST_RESERVOIR_INODE_NUMBER + reservoir_index;
        if (!BuildFileInode(state, inode_number, maximum_data_block_count,
                            inodes[inode_number - OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT])) {
            return false;
        }
    }
    if (state.next_data_bit >
        OS_TEST_ROOTFS_CAPACITY_DATA_BLOCK_COUNT - requested_free_data_block_count) {
        return false;
    }
    const uint64_t last_budget = OS_TEST_ROOTFS_CAPACITY_DATA_BLOCK_COUNT -
                                 requested_free_data_block_count - state.next_data_bit;
    const uint64_t last_data_block_count = SelectLastReservoirDataBlockCount(last_budget);
    const uint64_t last_inode_number = OS_TEST_ROOTFS_CAPACITY_FIRST_RESERVOIR_INODE_NUMBER +
                                       OS_TEST_ROOTFS_CAPACITY_RESERVOIR_FILE_COUNT -
                                       OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT;
    if (last_data_block_count == OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE ||
        !BuildFileInode(state, last_inode_number, last_data_block_count,
                        inodes[last_inode_number - OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT])) {
        return false;
    }
    inodes[OS_TEST_ROOTFS_CAPACITY_TARGET_INODE_NUMBER -
           OS_TEST_ROOTFS_CAPACITY_COUNTER_INCREMENT] = os::kernel::fs::RootInode{
        .type = os::kernel::fs::RootNodeType::RegularFile,
        .flags = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .size_bytes = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .generation = OS_TEST_ROOTFS_CAPACITY_TARGET_INODE_NUMBER,
        .link_count = OS_TEST_ROOTFS_CAPACITY_FILE_LINK_COUNT,
        .allocated_data_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .allocated_metadata_block_count = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .parent_inode_number = OS_TEST_ROOTFS_CAPACITY_ROOT_INODE_NUMBER,
        .direct_blocks = {},
        .single_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .double_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .triple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .quadruple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .quintuple_indirect_block = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .access_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .modification_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .change_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
        .birth_time_nanoseconds = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE,
    };

    if (!WriteInodeTable(device, inodes) || !WriteBitmaps(device, data_bitmap) ||
        !WriteSuperblock(device, inodes) ||
        device.Flush() != os::kernel::FileSystemBlockDeviceStatus::Succeeded) {
        return false;
    }
    information = RootFileSystemCapacityImageInformation{
        .free_data_block_count = OS_TEST_ROOTFS_CAPACITY_DATA_BLOCK_COUNT - state.next_data_bit,
        .reservoir_file_count = OS_TEST_ROOTFS_CAPACITY_RESERVOIR_FILE_COUNT,
        .allocated_inode_count = OS_TEST_ROOTFS_CAPACITY_ALLOCATED_INODE_COUNT,
    };
    return information.free_data_block_count >= requested_free_data_block_count &&
           information.free_data_block_count <
               requested_free_data_block_count + OS_TEST_ROOTFS_CAPACITY_DOUBLE_INDIRECT_LEVEL;
}

}
