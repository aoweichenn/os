#include <os/kernel/fs/root_file_system_v5_format.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint8_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_MAGIC[] = {'O', 'S', 'R', 'F', 'V', '0', '0', '5'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_MAGIC[] = {'O', 'S', 'G', 'D',
                                                                  'V', '0', '0', '5'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_INODE_MAGIC[] = {'O', 'S', 'I', 'N', 'V', '0', '0', '5'};
constexpr uint64_t OS_KERNEL_ROOTFS_V5_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_MINIMUM_BLOCKS_PER_GROUP = 256ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INITIAL_METADATA_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INITIAL_FORMAT_GENERATION = 1ULL;
constexpr uint32_t OS_KERNEL_ROOTFS_V5_CRC32C_INITIAL_VALUE = 0xFFFFFFFFU;
constexpr uint32_t OS_KERNEL_ROOTFS_V5_CRC32C_FINAL_XOR = 0xFFFFFFFFU;
constexpr uint32_t OS_KERNEL_ROOTFS_V5_CRC32C_REFLECTED_POLYNOMIAL = 0x82F63B78U;
constexpr uint32_t OS_KERNEL_ROOTFS_V5_CRC32C_LOW_BIT_MASK = 0x00000001U;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BLOCK_SIZE_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_SECTOR_SIZE_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_START_LBA_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DEVICE_SECTORS_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_TOTAL_BLOCKS_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BLOCKS_PER_GROUP_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_GROUP_COUNT_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_SIZE_OFFSET_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_START_OFFSET_BYTES = 88ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_BLOCKS_OFFSET_BYTES = 96ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODE_SIZE_OFFSET_BYTES = 104ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODES_PER_GROUP_OFFSET_BYTES = 112ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODE_COUNT_OFFSET_BYTES = 120ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_ROOT_INODE_OFFSET_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FIRST_USER_INODE_OFFSET_BYTES = 136ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_RESERVED_INODE_COUNT_OFFSET_BYTES = 144ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_STATE_OFFSET_BYTES = 152ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_COMPAT_OFFSET_BYTES = 160ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_READ_ONLY_COMPAT_OFFSET_BYTES = 168ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INCOMPAT_OFFSET_BYTES = 176ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_ALGORITHM_OFFSET_BYTES = 184ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BACKUP_POLICY_OFFSET_BYTES = 192ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CREATION_TIME_OFFSET_BYTES = 200ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FORMAT_GENERATION_OFFSET_BYTES = 208ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FREE_BLOCKS_OFFSET_BYTES = 216ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FREE_INODES_OFFSET_BYTES = 224ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DIRECTORY_COUNT_OFFSET_BYTES = 232ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_UUID_LOW_OFFSET_BYTES = 240ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_SUPERBLOCK_UUID_HIGH_OFFSET_BYTES = 248ULL;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_INDEX_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_FIRST_BLOCK_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_COUNT_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_FLAGS_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_SUPERBLOCK_COPY_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_COPY_START_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_COPY_COUNT_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_BITMAP_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_INODE_BITMAP_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_INODE_TABLE_OFFSET_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_INODE_TABLE_COUNT_OFFSET_BYTES = 88ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_DATA_START_OFFSET_BYTES = 96ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_DATA_COUNT_OFFSET_BYTES = 104ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_INODE_START_OFFSET_BYTES = 112ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_INODE_COUNT_OFFSET_BYTES = 120ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_FREE_BLOCKS_OFFSET_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_FREE_INODES_OFFSET_BYTES = 136ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_USED_DIRECTORIES_OFFSET_BYTES = 144ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_METADATA_GENERATION_OFFSET_BYTES = 152ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_BITMAP_CHECKSUM_OFFSET_BYTES = 160ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_INODE_BITMAP_CHECKSUM_OFFSET_BYTES = 164ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_GROUP_RESERVED_START_BYTES = 168ULL;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_NUMBER_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_GENERATION_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_TYPE_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_FLAGS_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_SIZE_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_ALLOCATED_BLOCKS_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_LINK_COUNT_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_PARENT_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_ACCESS_TIME_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_MODIFICATION_TIME_OFFSET_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_CHANGE_TIME_OFFSET_BYTES = 88ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_BIRTH_TIME_OFFSET_BYTES = 96ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_USER_OFFSET_BYTES = 104ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_GROUP_OFFSET_BYTES = 108ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_MODE_OFFSET_BYTES = 112ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_PROJECT_OFFSET_BYTES = 116ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_MAPPING_ROOT_OFFSET_BYTES = 120ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_RESERVED_OFFSET_BYTES = 248ULL;

void ClearBytes(uint8_t *const destination, const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = 0U;
    }
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

void WriteU32(uint8_t *const destination, const uint64_t offset_bytes,
              const uint32_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < sizeof(value);
         ++byte_index) {
        destination[offset_bytes + byte_index] =
            static_cast<uint8_t>((value >> (byte_index * 8ULL)) & 0xFFU);
    }
}

void WriteU64(uint8_t *const destination, const uint64_t offset_bytes,
              const uint64_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < sizeof(value);
         ++byte_index) {
        destination[offset_bytes + byte_index] =
            static_cast<uint8_t>((value >> (byte_index * 8ULL)) & 0xFFULL);
    }
}

[[nodiscard]] uint32_t ReadU32(const uint8_t *const source, const uint64_t offset_bytes) noexcept {
    uint32_t value = 0U;
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < sizeof(value);
         ++byte_index) {
        value |= static_cast<uint32_t>(source[offset_bytes + byte_index]) << (byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] uint64_t ReadU64(const uint8_t *const source, const uint64_t offset_bytes) noexcept {
    uint64_t value = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < sizeof(value);
         ++byte_index) {
        value |= static_cast<uint64_t>(source[offset_bytes + byte_index]) << (byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] bool TryAdd(const uint64_t left, const uint64_t right, uint64_t &sum) noexcept {
    if (left > UINT64_MAX - right) {
        return false;
    }
    sum = left + right;
    return true;
}

[[nodiscard]] bool TryMultiply(const uint64_t left, const uint64_t right,
                               uint64_t &product) noexcept {
    if (left != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE && right > UINT64_MAX / left) {
        return false;
    }
    product = left * right;
    return true;
}

[[nodiscard]] bool TryCeilDivide(const uint64_t value, const uint64_t divisor,
                                 uint64_t &quotient) noexcept {
    if (divisor == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE || value > UINT64_MAX - (divisor - 1ULL)) {
        return false;
    }
    quotient = (value + divisor - 1ULL) / divisor;
    return true;
}

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE && (value & (value - 1ULL)) == 0ULL;
}

[[nodiscard]] bool IsPurePower(uint64_t value, const uint64_t base) noexcept {
    if (value < base) {
        return false;
    }
    while (value % base == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE) {
        value /= base;
    }
    return value == OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT;
}

[[nodiscard]] bool UuidIsValid(const RootV5Uuid uuid) noexcept {
    return uuid.low != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
           uuid.high != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
}

[[nodiscard]] RootV5FormatStatus ValidateFeatures(const RootV5Superblock &superblock) noexcept {
    if ((superblock.compatible_features & OS_KERNEL_ROOTFS_V5_REQUIRED_COMPAT_FEATURES) !=
        OS_KERNEL_ROOTFS_V5_REQUIRED_COMPAT_FEATURES) {
        return RootV5FormatStatus::InvalidFeatures;
    }
    if ((superblock.read_only_compatible_features &
         ~OS_KERNEL_ROOTFS_V5_SUPPORTED_READ_ONLY_COMPAT_FEATURES) !=
        OS_KERNEL_ROOTFS_V5_EMPTY_VALUE) {
        return RootV5FormatStatus::UnsupportedReadOnlyFeature;
    }
    if ((superblock.read_only_compatible_features &
         OS_KERNEL_ROOTFS_V5_READ_ONLY_COMPAT_METADATA_CRC32C) == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE) {
        return RootV5FormatStatus::InvalidFeatures;
    }
    if ((superblock.incompatible_features & ~OS_KERNEL_ROOTFS_V5_SUPPORTED_INCOMPAT_FEATURES) !=
        OS_KERNEL_ROOTFS_V5_EMPTY_VALUE) {
        return RootV5FormatStatus::UnsupportedRequiredFeature;
    }
    return (superblock.incompatible_features & OS_KERNEL_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES) ==
                   OS_KERNEL_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES
               ? RootV5FormatStatus::Succeeded
               : RootV5FormatStatus::InvalidFeatures;
}

[[nodiscard]] RootV5FormatStatus ValidateProfile(const RootV5FormatProfile &profile) noexcept {
    if (profile.block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidBlockSize;
    }
    if (profile.sector_size_bytes != OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidSectorSize;
    }
    if (profile.group_descriptor_size_bytes != OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES ||
        profile.inode_size_bytes != OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES ||
        !IsPowerOfTwo(profile.blocks_per_group) ||
        profile.blocks_per_group < OS_KERNEL_ROOTFS_V5_MINIMUM_BLOCKS_PER_GROUP ||
        profile.blocks_per_group > profile.block_size_bytes * OS_KERNEL_ROOTFS_V5_BITS_PER_BYTE ||
        profile.inodes_per_group < OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER ||
        profile.inodes_per_group > profile.block_size_bytes * OS_KERNEL_ROOTFS_V5_BITS_PER_BYTE ||
        profile.block_size_bytes % profile.sector_size_bytes != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        !UuidIsValid(profile.uuid)) {
        return RootV5FormatStatus::InvalidLayout;
    }
    const uint64_t sectors_per_block = profile.block_size_bytes / profile.sector_size_bytes;
    if (profile.file_system_start_lba % sectors_per_block != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        profile.file_system_start_lba >= profile.device_sector_count ||
        (profile.device_sector_count - profile.file_system_start_lba) % sectors_per_block !=
            OS_KERNEL_ROOTFS_V5_EMPTY_VALUE) {
        return RootV5FormatStatus::InvalidLayout;
    }
    return RootV5FormatStatus::Succeeded;
}

[[nodiscard]] RootV5FormatStatus
BuildGroupDescriptorGeometry(const RootV5Superblock &superblock, const uint64_t group_index,
                             RootV5GroupDescriptor &descriptor) noexcept {
    descriptor = RootV5GroupDescriptor{};
    if (group_index >= superblock.group_count) {
        return RootV5FormatStatus::InvalidGroup;
    }
    uint64_t first_block = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    if (!TryMultiply(group_index, superblock.blocks_per_group, first_block) ||
        first_block >= superblock.total_block_count) {
        return RootV5FormatStatus::ArithmeticOverflow;
    }
    const uint64_t remaining_blocks = superblock.total_block_count - first_block;
    const uint64_t block_count = remaining_blocks < superblock.blocks_per_group
                                     ? remaining_blocks
                                     : superblock.blocks_per_group;
    const bool has_superblock_copy = RootV5GroupHasSuperblockCopy(group_index);
    uint64_t cursor = first_block;
    uint64_t superblock_copy_block = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    uint64_t descriptor_copy_start_block = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    uint64_t descriptor_copy_block_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    if (has_superblock_copy) {
        superblock_copy_block = cursor;
        if (!TryAdd(cursor, OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT, cursor)) {
            return RootV5FormatStatus::ArithmeticOverflow;
        }
        descriptor_copy_start_block = cursor;
        descriptor_copy_block_count = superblock.group_descriptor_table_block_count;
        if (!TryAdd(cursor, descriptor_copy_block_count, cursor)) {
            return RootV5FormatStatus::ArithmeticOverflow;
        }
    }
    const uint64_t block_bitmap_block = cursor;
    if (!TryAdd(cursor, OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT, cursor)) {
        return RootV5FormatStatus::ArithmeticOverflow;
    }
    const uint64_t inode_bitmap_block = cursor;
    if (!TryAdd(cursor, OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT, cursor)) {
        return RootV5FormatStatus::ArithmeticOverflow;
    }
    const uint64_t inode_table_start_block = cursor;
    uint64_t inode_table_size_bytes = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    uint64_t inode_table_block_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    if (!TryMultiply(superblock.inodes_per_group, superblock.inode_size_bytes,
                     inode_table_size_bytes) ||
        !TryCeilDivide(inode_table_size_bytes, superblock.block_size_bytes,
                       inode_table_block_count) ||
        !TryAdd(cursor, inode_table_block_count, cursor)) {
        return RootV5FormatStatus::ArithmeticOverflow;
    }
    const uint64_t group_end_block = first_block + block_count;
    if (cursor > group_end_block) {
        return RootV5FormatStatus::InvalidLayout;
    }
    uint64_t inode_start_number = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    if (!TryMultiply(group_index, superblock.inodes_per_group, inode_start_number) ||
        !TryAdd(inode_start_number, OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT, inode_start_number)) {
        return RootV5FormatStatus::ArithmeticOverflow;
    }
    const uint64_t reserved_inode_count = group_index == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE
                                              ? superblock.reserved_inode_count
                                              : OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    if (reserved_inode_count > superblock.inodes_per_group) {
        return RootV5FormatStatus::InvalidLayout;
    }
    descriptor = RootV5GroupDescriptor{
        .group_index = group_index,
        .first_block = first_block,
        .block_count = block_count,
        .flags = OS_KERNEL_ROOTFS_V5_REQUIRED_GROUP_FLAGS |
                 (has_superblock_copy ? OS_KERNEL_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY
                                      : OS_KERNEL_ROOTFS_V5_EMPTY_VALUE),
        .superblock_copy_block = superblock_copy_block,
        .group_descriptor_copy_start_block = descriptor_copy_start_block,
        .group_descriptor_copy_block_count = descriptor_copy_block_count,
        .block_bitmap_block = block_bitmap_block,
        .inode_bitmap_block = inode_bitmap_block,
        .inode_table_start_block = inode_table_start_block,
        .inode_table_block_count = inode_table_block_count,
        .data_start_block = cursor,
        .data_block_count = group_end_block - cursor,
        .inode_start_number = inode_start_number,
        .inode_count = superblock.inodes_per_group,
        .free_block_count = group_end_block - cursor,
        .free_inode_count = superblock.inodes_per_group - reserved_inode_count,
        .used_directory_count = group_index == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE
                                    ? OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT
                                    : OS_KERNEL_ROOTFS_V5_EMPTY_VALUE,
        .metadata_generation = OS_KERNEL_ROOTFS_V5_INITIAL_METADATA_GENERATION,
        .block_bitmap_checksum = 0U,
        .inode_bitmap_checksum = 0U,
    };
    return RootV5FormatStatus::Succeeded;
}

[[nodiscard]] os::abi::FileMode ExpectedModeType(const RootV5NodeType type) noexcept {
    if (type == RootV5NodeType::RegularFile) {
        return os::abi::OS_ABI_FILE_MODE_REGULAR;
    }
    if (type == RootV5NodeType::Directory) {
        return os::abi::OS_ABI_FILE_MODE_DIRECTORY;
    }
    if (type == RootV5NodeType::SymbolicLink) {
        return os::abi::OS_ABI_FILE_MODE_SYMBOLIC_LINK;
    }
    return 0U;
}

}

uint32_t CalculateRootV5Crc32c(const uint8_t *const bytes, const uint64_t length_bytes) noexcept {
    if (bytes == nullptr && length_bytes != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE) {
        return 0U;
    }
    uint32_t crc = OS_KERNEL_ROOTFS_V5_CRC32C_INITIAL_VALUE;
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        crc ^= bytes[byte_index];
        for (uint64_t bit_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
             bit_index < OS_KERNEL_ROOTFS_V5_BITS_PER_BYTE; ++bit_index) {
            const bool low_bit_set = (crc & OS_KERNEL_ROOTFS_V5_CRC32C_LOW_BIT_MASK) != 0U;
            crc >>= 1U;
            if (low_bit_set) {
                crc ^= OS_KERNEL_ROOTFS_V5_CRC32C_REFLECTED_POLYNOMIAL;
            }
        }
    }
    return crc ^ OS_KERNEL_ROOTFS_V5_CRC32C_FINAL_XOR;
}

RootV5FormatProfile MakeProductionRootV5FormatProfile(const uint64_t creation_time_nanoseconds,
                                                      const RootV5Uuid uuid) noexcept {
    return RootV5FormatProfile{
        .sector_size_bytes = OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES,
        .block_size_bytes = OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
        .file_system_start_lba = OS_KERNEL_ROOTFS_V5_FILE_SYSTEM_START_LBA,
        .device_sector_count = OS_KERNEL_ROOTFS_V5_DEVICE_SECTOR_COUNT,
        .blocks_per_group = OS_KERNEL_ROOTFS_V5_BLOCKS_PER_GROUP,
        .group_descriptor_size_bytes = OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES,
        .inode_size_bytes = OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES,
        .inodes_per_group = OS_KERNEL_ROOTFS_V5_INODES_PER_GROUP,
        .creation_time_nanoseconds = creation_time_nanoseconds,
        .uuid = uuid,
    };
}

RootV5FormatStatus PlanRootV5Superblock(const RootV5FormatProfile &profile,
                                        RootV5Superblock &superblock) noexcept {
    superblock = RootV5Superblock{};
    const RootV5FormatStatus profile_status = ValidateProfile(profile);
    if (profile_status != RootV5FormatStatus::Succeeded) {
        return profile_status;
    }
    const uint64_t sectors_per_block = profile.block_size_bytes / profile.sector_size_bytes;
    const uint64_t total_block_count =
        (profile.device_sector_count - profile.file_system_start_lba) / sectors_per_block;
    uint64_t group_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    uint64_t descriptor_table_size_bytes = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    uint64_t descriptor_table_block_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    uint64_t inode_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    if (!TryCeilDivide(total_block_count, profile.blocks_per_group, group_count) ||
        !TryMultiply(group_count, profile.group_descriptor_size_bytes,
                     descriptor_table_size_bytes) ||
        !TryCeilDivide(descriptor_table_size_bytes, profile.block_size_bytes,
                       descriptor_table_block_count) ||
        !TryMultiply(group_count, profile.inodes_per_group, inode_count) ||
        group_count == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        descriptor_table_block_count == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        profile.inodes_per_group < OS_KERNEL_ROOTFS_V5_RESERVED_INODE_COUNT) {
        return RootV5FormatStatus::ArithmeticOverflow;
    }
    superblock = RootV5Superblock{
        .version = OS_KERNEL_ROOTFS_V5_FORMAT_VERSION,
        .header_size_bytes = OS_KERNEL_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES,
        .block_size_bytes = profile.block_size_bytes,
        .sector_size_bytes = profile.sector_size_bytes,
        .file_system_start_lba = profile.file_system_start_lba,
        .device_sector_count = profile.device_sector_count,
        .total_block_count = total_block_count,
        .blocks_per_group = profile.blocks_per_group,
        .group_count = group_count,
        .group_descriptor_size_bytes = profile.group_descriptor_size_bytes,
        .group_descriptor_table_start_block =
            OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_TABLE_START_BLOCK,
        .group_descriptor_table_block_count = descriptor_table_block_count,
        .inode_size_bytes = profile.inode_size_bytes,
        .inodes_per_group = profile.inodes_per_group,
        .inode_count = inode_count,
        .root_inode_number = OS_KERNEL_ROOTFS_V5_ROOT_INODE_NUMBER,
        .first_user_inode_number = OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER,
        .reserved_inode_count = OS_KERNEL_ROOTFS_V5_RESERVED_INODE_COUNT,
        .state = RootV5FileSystemState::Clean,
        .compatible_features = OS_KERNEL_ROOTFS_V5_REQUIRED_COMPAT_FEATURES,
        .read_only_compatible_features = OS_KERNEL_ROOTFS_V5_SUPPORTED_READ_ONLY_COMPAT_FEATURES,
        .incompatible_features = OS_KERNEL_ROOTFS_V5_REQUIRED_INCOMPAT_FEATURES,
        .checksum_algorithm = OS_KERNEL_ROOTFS_V5_CRC32C_ALGORITHM,
        .backup_policy = OS_KERNEL_ROOTFS_V5_SPARSE_BACKUP_POLICY,
        .creation_time_nanoseconds = profile.creation_time_nanoseconds,
        .format_generation = OS_KERNEL_ROOTFS_V5_INITIAL_FORMAT_GENERATION,
        .free_block_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE,
        .free_inode_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE,
        .allocated_directory_count = OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT,
        .uuid = profile.uuid,
    };
    uint64_t free_block_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    uint64_t free_inode_count = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE;
    for (uint64_t group_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; group_index < group_count;
         ++group_index) {
        RootV5GroupDescriptor descriptor{};
        const RootV5FormatStatus status =
            BuildGroupDescriptorGeometry(superblock, group_index, descriptor);
        if (status != RootV5FormatStatus::Succeeded ||
            !TryAdd(free_block_count, descriptor.free_block_count, free_block_count) ||
            !TryAdd(free_inode_count, descriptor.free_inode_count, free_inode_count)) {
            superblock = RootV5Superblock{};
            return status == RootV5FormatStatus::Succeeded ? RootV5FormatStatus::ArithmeticOverflow
                                                           : status;
        }
    }
    superblock.free_block_count = free_block_count;
    superblock.free_inode_count = free_inode_count;
    return RootV5FormatStatus::Succeeded;
}

bool RootV5GroupHasSuperblockCopy(const uint64_t group_index) noexcept {
    return group_index == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
           group_index == OS_KERNEL_ROOTFS_V5_COUNTER_INCREMENT || IsPurePower(group_index, 3ULL) ||
           IsPurePower(group_index, 5ULL) || IsPurePower(group_index, 7ULL);
}

RootV5FormatStatus BuildInitialRootV5GroupDescriptor(const RootV5Superblock &superblock,
                                                     const uint64_t group_index,
                                                     RootV5GroupDescriptor &descriptor) noexcept {
    const RootV5FormatStatus superblock_status = ValidateRootV5Superblock(superblock);
    return superblock_status == RootV5FormatStatus::Succeeded
               ? BuildGroupDescriptorGeometry(superblock, group_index, descriptor)
               : superblock_status;
}

RootV5FormatStatus ValidateRootV5Superblock(const RootV5Superblock &superblock) noexcept {
    if (superblock.version != OS_KERNEL_ROOTFS_V5_FORMAT_VERSION) {
        return RootV5FormatStatus::InvalidVersion;
    }
    if (superblock.header_size_bytes != OS_KERNEL_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidHeaderSize;
    }
    if (superblock.block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidBlockSize;
    }
    if (superblock.sector_size_bytes != OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidSectorSize;
    }
    const RootV5FormatStatus feature_status = ValidateFeatures(superblock);
    if (feature_status != RootV5FormatStatus::Succeeded) {
        return feature_status;
    }
    if (superblock.checksum_algorithm != OS_KERNEL_ROOTFS_V5_CRC32C_ALGORITHM) {
        return RootV5FormatStatus::InvalidChecksumAlgorithm;
    }
    if (superblock.state != RootV5FileSystemState::Clean) {
        return RootV5FormatStatus::InvalidState;
    }
    if (superblock.backup_policy != OS_KERNEL_ROOTFS_V5_SPARSE_BACKUP_POLICY ||
        !UuidIsValid(superblock.uuid)) {
        return RootV5FormatStatus::InvalidLayout;
    }
    const RootV5FormatProfile profile{
        .sector_size_bytes = superblock.sector_size_bytes,
        .block_size_bytes = superblock.block_size_bytes,
        .file_system_start_lba = superblock.file_system_start_lba,
        .device_sector_count = superblock.device_sector_count,
        .blocks_per_group = superblock.blocks_per_group,
        .group_descriptor_size_bytes = superblock.group_descriptor_size_bytes,
        .inode_size_bytes = superblock.inode_size_bytes,
        .inodes_per_group = superblock.inodes_per_group,
        .creation_time_nanoseconds = superblock.creation_time_nanoseconds,
        .uuid = superblock.uuid,
    };
    RootV5Superblock expected{};
    const RootV5FormatStatus plan_status = PlanRootV5Superblock(profile, expected);
    if (plan_status != RootV5FormatStatus::Succeeded) {
        return plan_status;
    }
    return superblock.total_block_count == expected.total_block_count &&
                   superblock.group_count == expected.group_count &&
                   superblock.group_descriptor_table_start_block ==
                       expected.group_descriptor_table_start_block &&
                   superblock.group_descriptor_table_block_count ==
                       expected.group_descriptor_table_block_count &&
                   superblock.inode_count == expected.inode_count &&
                   superblock.root_inode_number == expected.root_inode_number &&
                   superblock.first_user_inode_number == expected.first_user_inode_number &&
                   superblock.reserved_inode_count == expected.reserved_inode_count &&
                   superblock.format_generation == expected.format_generation &&
                   superblock.free_block_count == expected.free_block_count &&
                   superblock.free_inode_count == expected.free_inode_count &&
                   superblock.allocated_directory_count == expected.allocated_directory_count
               ? RootV5FormatStatus::Succeeded
               : RootV5FormatStatus::InvalidLayout;
}

RootV5FormatStatus ValidateRootV5GroupDescriptor(const RootV5Superblock &superblock,
                                                 const RootV5GroupDescriptor &descriptor) noexcept {
    const RootV5FormatStatus superblock_status = ValidateRootV5Superblock(superblock);
    if (superblock_status != RootV5FormatStatus::Succeeded) {
        return superblock_status;
    }
    if ((descriptor.flags & ~OS_KERNEL_ROOTFS_V5_SUPPORTED_GROUP_FLAGS) !=
            OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        (descriptor.flags & OS_KERNEL_ROOTFS_V5_REQUIRED_GROUP_FLAGS) !=
            OS_KERNEL_ROOTFS_V5_REQUIRED_GROUP_FLAGS) {
        return RootV5FormatStatus::InvalidGroup;
    }
    RootV5GroupDescriptor expected{};
    const RootV5FormatStatus expected_status =
        BuildGroupDescriptorGeometry(superblock, descriptor.group_index, expected);
    if (expected_status != RootV5FormatStatus::Succeeded) {
        return expected_status;
    }
    return descriptor.first_block == expected.first_block &&
                   descriptor.block_count == expected.block_count &&
                   descriptor.flags == expected.flags &&
                   descriptor.superblock_copy_block == expected.superblock_copy_block &&
                   descriptor.group_descriptor_copy_start_block ==
                       expected.group_descriptor_copy_start_block &&
                   descriptor.group_descriptor_copy_block_count ==
                       expected.group_descriptor_copy_block_count &&
                   descriptor.block_bitmap_block == expected.block_bitmap_block &&
                   descriptor.inode_bitmap_block == expected.inode_bitmap_block &&
                   descriptor.inode_table_start_block == expected.inode_table_start_block &&
                   descriptor.inode_table_block_count == expected.inode_table_block_count &&
                   descriptor.data_start_block == expected.data_start_block &&
                   descriptor.data_block_count == expected.data_block_count &&
                   descriptor.inode_start_number == expected.inode_start_number &&
                   descriptor.inode_count == expected.inode_count &&
                   descriptor.free_block_count == expected.free_block_count &&
                   descriptor.free_inode_count == expected.free_inode_count &&
                   descriptor.used_directory_count == expected.used_directory_count &&
                   descriptor.metadata_generation == expected.metadata_generation
               ? RootV5FormatStatus::Succeeded
               : RootV5FormatStatus::InvalidGroup;
}

RootV5FormatStatus ValidateRootV5Inode(const RootV5Superblock &superblock,
                                       const RootV5Inode &inode) noexcept {
    const RootV5FormatStatus superblock_status = ValidateRootV5Superblock(superblock);
    if (superblock_status != RootV5FormatStatus::Succeeded) {
        return superblock_status;
    }
    if (inode.type == RootV5NodeType::Unused) {
        return inode.inode_number == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.generation == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.flags == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.size_bytes == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.allocated_block_count == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.link_count == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.parent_inode_number == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.access_time_nanoseconds == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.modification_time_nanoseconds == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.change_time_nanoseconds == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.birth_time_nanoseconds == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.owner_user_identifier == os::abi::OS_ABI_ROOT_USER_IDENTIFIER &&
                       inode.owner_group_identifier == os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER &&
                       inode.mode == 0U && inode.project_identifier == 0U &&
                       RootV5BytesAreZero(inode.mapping_root, sizeof(inode.mapping_root))
                   ? RootV5FormatStatus::Succeeded
                   : RootV5FormatStatus::InvalidInode;
    }
    if (inode.inode_number == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        inode.inode_number > superblock.inode_count ||
        inode.generation == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        inode.flags != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        inode.size_bytes != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        inode.allocated_block_count != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        !RootV5BytesAreZero(inode.mapping_root, sizeof(inode.mapping_root))) {
        return RootV5FormatStatus::InvalidInode;
    }
    const bool root_inode = inode.inode_number == superblock.root_inode_number;
    const bool reserved_inode = inode.inode_number < superblock.first_user_inode_number;
    if (reserved_inode && !root_inode) {
        return inode.type == RootV5NodeType::Reserved &&
                       inode.link_count == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.parent_inode_number == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE &&
                       inode.owner_user_identifier == os::abi::OS_ABI_ROOT_USER_IDENTIFIER &&
                       inode.owner_group_identifier == os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER &&
                       inode.mode == 0U
                   ? RootV5FormatStatus::Succeeded
                   : RootV5FormatStatus::InvalidInode;
    }
    const os::abi::FileMode expected_mode = ExpectedModeType(inode.type);
    if (expected_mode == 0U || inode.link_count == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        (inode.mode & os::abi::OS_ABI_FILE_MODE_TYPE_MASK) != expected_mode ||
        (inode.mode & ~(os::abi::OS_ABI_FILE_MODE_TYPE_MASK |
                        os::abi::OS_ABI_FILE_MODE_CHANGEABLE_MASK)) != 0U ||
        inode.parent_inode_number == OS_KERNEL_ROOTFS_V5_EMPTY_VALUE ||
        inode.parent_inode_number > superblock.inode_count) {
        return RootV5FormatStatus::InvalidInode;
    }
    return !root_inode || (inode.type == RootV5NodeType::Directory &&
                           inode.parent_inode_number == superblock.root_inode_number)
               ? RootV5FormatStatus::Succeeded
               : RootV5FormatStatus::InvalidInode;
}

RootV5FormatStatus EncodeRootV5Superblock(const RootV5Superblock &superblock, uint8_t *const block,
                                          const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootV5FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidBufferSize;
    }
    const RootV5FormatStatus status = ValidateRootV5Superblock(superblock);
    if (status != RootV5FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(block, block_size_bytes);
    CopyBytes(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_MAGIC, OS_KERNEL_ROOTFS_V5_MAGIC_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_VERSION_OFFSET_BYTES, superblock.version);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_OFFSET_BYTES,
             superblock.header_size_bytes);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BLOCK_SIZE_OFFSET_BYTES,
             superblock.block_size_bytes);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_SECTOR_SIZE_OFFSET_BYTES,
             superblock.sector_size_bytes);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_START_LBA_OFFSET_BYTES,
             superblock.file_system_start_lba);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DEVICE_SECTORS_OFFSET_BYTES,
             superblock.device_sector_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_TOTAL_BLOCKS_OFFSET_BYTES,
             superblock.total_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BLOCKS_PER_GROUP_OFFSET_BYTES,
             superblock.blocks_per_group);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_GROUP_COUNT_OFFSET_BYTES,
             superblock.group_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_SIZE_OFFSET_BYTES,
             superblock.group_descriptor_size_bytes);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_START_OFFSET_BYTES,
             superblock.group_descriptor_table_start_block);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_BLOCKS_OFFSET_BYTES,
             superblock.group_descriptor_table_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODE_SIZE_OFFSET_BYTES,
             superblock.inode_size_bytes);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODES_PER_GROUP_OFFSET_BYTES,
             superblock.inodes_per_group);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODE_COUNT_OFFSET_BYTES,
             superblock.inode_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_ROOT_INODE_OFFSET_BYTES,
             superblock.root_inode_number);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FIRST_USER_INODE_OFFSET_BYTES,
             superblock.first_user_inode_number);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_RESERVED_INODE_COUNT_OFFSET_BYTES,
             superblock.reserved_inode_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_STATE_OFFSET_BYTES,
             static_cast<uint64_t>(superblock.state));
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_COMPAT_OFFSET_BYTES,
             superblock.compatible_features);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_READ_ONLY_COMPAT_OFFSET_BYTES,
             superblock.read_only_compatible_features);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INCOMPAT_OFFSET_BYTES,
             superblock.incompatible_features);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_ALGORITHM_OFFSET_BYTES,
             superblock.checksum_algorithm);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BACKUP_POLICY_OFFSET_BYTES,
             superblock.backup_policy);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CREATION_TIME_OFFSET_BYTES,
             superblock.creation_time_nanoseconds);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FORMAT_GENERATION_OFFSET_BYTES,
             superblock.format_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FREE_BLOCKS_OFFSET_BYTES,
             superblock.free_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FREE_INODES_OFFSET_BYTES,
             superblock.free_inode_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DIRECTORY_COUNT_OFFSET_BYTES,
             superblock.allocated_directory_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_UUID_LOW_OFFSET_BYTES, superblock.uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_UUID_HIGH_OFFSET_BYTES, superblock.uuid.high);
    WriteU32(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
             CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES));
    return RootV5FormatStatus::Succeeded;
}

RootV5FormatStatus DecodeRootV5Superblock(const uint8_t *const block,
                                          const uint64_t block_size_bytes,
                                          RootV5Superblock &superblock) noexcept {
    superblock = RootV5Superblock{};
    if (block == nullptr) {
        return RootV5FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidBufferSize;
    }
    if (!BytesEqual(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_MAGIC,
                    OS_KERNEL_ROOTFS_V5_MAGIC_SIZE_BYTES)) {
        return RootV5FormatStatus::InvalidMagic;
    }
    if (ReadU32(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES) !=
        CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES)) {
        return RootV5FormatStatus::InvalidChecksum;
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES,
                            OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_BYTES)) {
        return RootV5FormatStatus::NonZeroReservedBytes;
    }
    superblock = RootV5Superblock{
        .version = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_VERSION_OFFSET_BYTES),
        .header_size_bytes =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_HEADER_SIZE_OFFSET_BYTES),
        .block_size_bytes = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BLOCK_SIZE_OFFSET_BYTES),
        .sector_size_bytes =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_SECTOR_SIZE_OFFSET_BYTES),
        .file_system_start_lba =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_START_LBA_OFFSET_BYTES),
        .device_sector_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DEVICE_SECTORS_OFFSET_BYTES),
        .total_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_TOTAL_BLOCKS_OFFSET_BYTES),
        .blocks_per_group =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BLOCKS_PER_GROUP_OFFSET_BYTES),
        .group_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_GROUP_COUNT_OFFSET_BYTES),
        .group_descriptor_size_bytes =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_SIZE_OFFSET_BYTES),
        .group_descriptor_table_start_block =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_START_OFFSET_BYTES),
        .group_descriptor_table_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DESCRIPTOR_BLOCKS_OFFSET_BYTES),
        .inode_size_bytes = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODE_SIZE_OFFSET_BYTES),
        .inodes_per_group =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODES_PER_GROUP_OFFSET_BYTES),
        .inode_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INODE_COUNT_OFFSET_BYTES),
        .root_inode_number = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_ROOT_INODE_OFFSET_BYTES),
        .first_user_inode_number =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FIRST_USER_INODE_OFFSET_BYTES),
        .reserved_inode_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_RESERVED_INODE_COUNT_OFFSET_BYTES),
        .state = static_cast<RootV5FileSystemState>(
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_STATE_OFFSET_BYTES)),
        .compatible_features = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_COMPAT_OFFSET_BYTES),
        .read_only_compatible_features =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_READ_ONLY_COMPAT_OFFSET_BYTES),
        .incompatible_features =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_INCOMPAT_OFFSET_BYTES),
        .checksum_algorithm =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_ALGORITHM_OFFSET_BYTES),
        .backup_policy = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_BACKUP_POLICY_OFFSET_BYTES),
        .creation_time_nanoseconds =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CREATION_TIME_OFFSET_BYTES),
        .format_generation =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FORMAT_GENERATION_OFFSET_BYTES),
        .free_block_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FREE_BLOCKS_OFFSET_BYTES),
        .free_inode_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_FREE_INODES_OFFSET_BYTES),
        .allocated_directory_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_DIRECTORY_COUNT_OFFSET_BYTES),
        .uuid =
            RootV5Uuid{
                .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_UUID_LOW_OFFSET_BYTES),
                .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_SUPERBLOCK_UUID_HIGH_OFFSET_BYTES),
            },
    };
    return ValidateRootV5Superblock(superblock);
}

RootV5FormatStatus EncodeRootV5GroupDescriptor(const RootV5Superblock &superblock,
                                               const RootV5GroupDescriptor &descriptor,
                                               uint8_t *const bytes,
                                               const uint64_t byte_count) noexcept {
    if (bytes == nullptr) {
        return RootV5FormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidBufferSize;
    }
    const RootV5FormatStatus status = ValidateRootV5GroupDescriptor(superblock, descriptor);
    if (status != RootV5FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(bytes, byte_count);
    CopyBytes(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_MAGIC,
              OS_KERNEL_ROOTFS_V5_MAGIC_SIZE_BYTES);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INDEX_OFFSET_BYTES, descriptor.group_index);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_FIRST_BLOCK_OFFSET_BYTES, descriptor.first_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_COUNT_OFFSET_BYTES, descriptor.block_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_FLAGS_OFFSET_BYTES, descriptor.flags);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_SUPERBLOCK_COPY_OFFSET_BYTES,
             descriptor.superblock_copy_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_COPY_START_OFFSET_BYTES,
             descriptor.group_descriptor_copy_start_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_COPY_COUNT_OFFSET_BYTES,
             descriptor.group_descriptor_copy_block_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_BITMAP_OFFSET_BYTES,
             descriptor.block_bitmap_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_BITMAP_OFFSET_BYTES,
             descriptor.inode_bitmap_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_TABLE_OFFSET_BYTES,
             descriptor.inode_table_start_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_TABLE_COUNT_OFFSET_BYTES,
             descriptor.inode_table_block_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DATA_START_OFFSET_BYTES, descriptor.data_start_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DATA_COUNT_OFFSET_BYTES, descriptor.data_block_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_START_OFFSET_BYTES,
             descriptor.inode_start_number);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_COUNT_OFFSET_BYTES, descriptor.inode_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_FREE_BLOCKS_OFFSET_BYTES,
             descriptor.free_block_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_FREE_INODES_OFFSET_BYTES,
             descriptor.free_inode_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_USED_DIRECTORIES_OFFSET_BYTES,
             descriptor.used_directory_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_METADATA_GENERATION_OFFSET_BYTES,
             descriptor.metadata_generation);
    WriteU32(bytes, OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_BITMAP_CHECKSUM_OFFSET_BYTES,
             descriptor.block_bitmap_checksum);
    WriteU32(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_BITMAP_CHECKSUM_OFFSET_BYTES,
             descriptor.inode_bitmap_checksum);
    WriteU32(
        bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES,
        CalculateRootV5Crc32c(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES));
    return RootV5FormatStatus::Succeeded;
}

RootV5FormatStatus DecodeRootV5GroupDescriptor(const RootV5Superblock &superblock,
                                               const uint8_t *const bytes,
                                               const uint64_t byte_count,
                                               RootV5GroupDescriptor &descriptor) noexcept {
    descriptor = RootV5GroupDescriptor{};
    if (bytes == nullptr) {
        return RootV5FormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidBufferSize;
    }
    if (!BytesEqual(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_MAGIC,
                    OS_KERNEL_ROOTFS_V5_MAGIC_SIZE_BYTES)) {
        return RootV5FormatStatus::InvalidMagic;
    }
    if (ReadU32(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES) !=
        CalculateRootV5Crc32c(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES)) {
        return RootV5FormatStatus::InvalidChecksum;
    }
    if (!RootV5BytesAreZero(bytes + OS_KERNEL_ROOTFS_V5_GROUP_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_GROUP_RESERVED_START_BYTES)) {
        return RootV5FormatStatus::NonZeroReservedBytes;
    }
    descriptor = RootV5GroupDescriptor{
        .group_index = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INDEX_OFFSET_BYTES),
        .first_block = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_FIRST_BLOCK_OFFSET_BYTES),
        .block_count = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_COUNT_OFFSET_BYTES),
        .flags = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_FLAGS_OFFSET_BYTES),
        .superblock_copy_block =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_SUPERBLOCK_COPY_OFFSET_BYTES),
        .group_descriptor_copy_start_block =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_COPY_START_OFFSET_BYTES),
        .group_descriptor_copy_block_count =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_COPY_COUNT_OFFSET_BYTES),
        .block_bitmap_block = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_BITMAP_OFFSET_BYTES),
        .inode_bitmap_block = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_BITMAP_OFFSET_BYTES),
        .inode_table_start_block =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_TABLE_OFFSET_BYTES),
        .inode_table_block_count =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_TABLE_COUNT_OFFSET_BYTES),
        .data_start_block = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DATA_START_OFFSET_BYTES),
        .data_block_count = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_DATA_COUNT_OFFSET_BYTES),
        .inode_start_number = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_START_OFFSET_BYTES),
        .inode_count = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_COUNT_OFFSET_BYTES),
        .free_block_count = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_FREE_BLOCKS_OFFSET_BYTES),
        .free_inode_count = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_FREE_INODES_OFFSET_BYTES),
        .used_directory_count =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_USED_DIRECTORIES_OFFSET_BYTES),
        .metadata_generation =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_GROUP_METADATA_GENERATION_OFFSET_BYTES),
        .block_bitmap_checksum =
            ReadU32(bytes, OS_KERNEL_ROOTFS_V5_GROUP_BLOCK_BITMAP_CHECKSUM_OFFSET_BYTES),
        .inode_bitmap_checksum =
            ReadU32(bytes, OS_KERNEL_ROOTFS_V5_GROUP_INODE_BITMAP_CHECKSUM_OFFSET_BYTES),
    };
    return ValidateRootV5GroupDescriptor(superblock, descriptor);
}

RootV5FormatStatus EncodeRootV5Inode(const RootV5Superblock &superblock, const RootV5Inode &inode,
                                     uint8_t *const bytes, const uint64_t byte_count) noexcept {
    if (bytes == nullptr) {
        return RootV5FormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidBufferSize;
    }
    const RootV5FormatStatus status = ValidateRootV5Inode(superblock, inode);
    if (status != RootV5FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(bytes, byte_count);
    if (inode.type == RootV5NodeType::Unused) {
        return RootV5FormatStatus::Succeeded;
    }
    CopyBytes(bytes, OS_KERNEL_ROOTFS_V5_INODE_MAGIC, OS_KERNEL_ROOTFS_V5_MAGIC_SIZE_BYTES);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_NUMBER_OFFSET_BYTES, inode.inode_number);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_GENERATION_OFFSET_BYTES, inode.generation);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_TYPE_OFFSET_BYTES, static_cast<uint64_t>(inode.type));
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_FLAGS_OFFSET_BYTES, inode.flags);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_SIZE_OFFSET_BYTES, inode.size_bytes);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_ALLOCATED_BLOCKS_OFFSET_BYTES,
             inode.allocated_block_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_LINK_COUNT_OFFSET_BYTES, inode.link_count);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_PARENT_OFFSET_BYTES, inode.parent_inode_number);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_ACCESS_TIME_OFFSET_BYTES,
             inode.access_time_nanoseconds);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_MODIFICATION_TIME_OFFSET_BYTES,
             inode.modification_time_nanoseconds);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_CHANGE_TIME_OFFSET_BYTES,
             inode.change_time_nanoseconds);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_BIRTH_TIME_OFFSET_BYTES,
             inode.birth_time_nanoseconds);
    WriteU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_USER_OFFSET_BYTES, inode.owner_user_identifier);
    WriteU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_GROUP_OFFSET_BYTES, inode.owner_group_identifier);
    WriteU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_MODE_OFFSET_BYTES, inode.mode);
    WriteU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_PROJECT_OFFSET_BYTES, inode.project_identifier);
    CopyBytes(bytes + OS_KERNEL_ROOTFS_V5_INODE_MAPPING_ROOT_OFFSET_BYTES, inode.mapping_root,
              sizeof(inode.mapping_root));
    WriteU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES,
             CalculateRootV5Crc32c(bytes, OS_KERNEL_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES));
    return RootV5FormatStatus::Succeeded;
}

RootV5FormatStatus DecodeRootV5Inode(const RootV5Superblock &superblock, const uint8_t *const bytes,
                                     const uint64_t byte_count, RootV5Inode &inode) noexcept {
    inode = RootV5Inode{};
    if (bytes == nullptr) {
        return RootV5FormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES) {
        return RootV5FormatStatus::InvalidBufferSize;
    }
    if (RootV5BytesAreZero(bytes, byte_count)) {
        return RootV5FormatStatus::Succeeded;
    }
    if (!BytesEqual(bytes, OS_KERNEL_ROOTFS_V5_INODE_MAGIC, OS_KERNEL_ROOTFS_V5_MAGIC_SIZE_BYTES)) {
        return RootV5FormatStatus::InvalidMagic;
    }
    if (ReadU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES) !=
        CalculateRootV5Crc32c(bytes, OS_KERNEL_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES)) {
        return RootV5FormatStatus::InvalidChecksum;
    }
    if (ReadU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_RESERVED_OFFSET_BYTES) != 0U) {
        return RootV5FormatStatus::NonZeroReservedBytes;
    }
    inode = RootV5Inode{
        .inode_number = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_NUMBER_OFFSET_BYTES),
        .generation = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_GENERATION_OFFSET_BYTES),
        .type = static_cast<RootV5NodeType>(
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_TYPE_OFFSET_BYTES)),
        .flags = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_FLAGS_OFFSET_BYTES),
        .size_bytes = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_SIZE_OFFSET_BYTES),
        .allocated_block_count =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_ALLOCATED_BLOCKS_OFFSET_BYTES),
        .link_count = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_LINK_COUNT_OFFSET_BYTES),
        .parent_inode_number = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_PARENT_OFFSET_BYTES),
        .access_time_nanoseconds =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_ACCESS_TIME_OFFSET_BYTES),
        .modification_time_nanoseconds =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_MODIFICATION_TIME_OFFSET_BYTES),
        .change_time_nanoseconds =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_CHANGE_TIME_OFFSET_BYTES),
        .birth_time_nanoseconds = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_BIRTH_TIME_OFFSET_BYTES),
        .owner_user_identifier = ReadU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_USER_OFFSET_BYTES),
        .owner_group_identifier = ReadU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_GROUP_OFFSET_BYTES),
        .mode = ReadU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_MODE_OFFSET_BYTES),
        .project_identifier = ReadU32(bytes, OS_KERNEL_ROOTFS_V5_INODE_PROJECT_OFFSET_BYTES),
        .mapping_root = {},
    };
    CopyBytes(inode.mapping_root, bytes + OS_KERNEL_ROOTFS_V5_INODE_MAPPING_ROOT_OFFSET_BYTES,
              sizeof(inode.mapping_root));
    return ValidateRootV5Inode(superblock, inode);
}

bool RootV5BytesAreZero(const uint8_t *const bytes, const uint64_t byte_count) noexcept {
    if (bytes == nullptr && byte_count != OS_KERNEL_ROOTFS_V5_EMPTY_VALUE) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_V5_EMPTY_VALUE; byte_index < byte_count;
         ++byte_index) {
        if (bytes[byte_index] != 0U) {
            return false;
        }
    }
    return true;
}

}
