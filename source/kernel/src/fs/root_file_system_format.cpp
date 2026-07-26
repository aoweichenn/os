#include "os/kernel/fs/root_file_system_format.hpp"

namespace os::kernel::fs {

namespace {

constexpr uint8_t OS_KERNEL_ROOTFS_FORMAT_MAGIC[] = {'O', 'S', 'R', 'F', 'V', '0', '0', '2'};
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FIELD_START_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FIELD_COUNT = 18ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_RESERVED_START_BYTES = 152ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES = 508ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_DIRECT_START_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_SINGLE_INDIRECT_OFFSET_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_DOUBLE_INDIRECT_OFFSET_BYTES = 136ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_TRIPLE_INDIRECT_OFFSET_BYTES = 144ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_RESERVED_START_BYTES = 152ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_CHECKSUM_OFFSET_BYTES = 252ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_TYPE_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_FLAGS_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_GENERATION_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_LINK_COUNT_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_DATA_BLOCK_COUNT_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_METADATA_BLOCK_COUNT_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_INODE_PARENT_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_NAME_START_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_CHECKSUM_OFFSET_BYTES = 288ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_RESERVED_START_BYTES = 292ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_INODE_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_GENERATION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_TYPE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_POINTER_CHECKSUM_OFFSET_BYTES = 504ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_POINTER_RESERVED_START_BYTES = 508ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_UINT32_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_ROOT_LINK_COUNT = 1ULL;
constexpr uint8_t OS_KERNEL_ROOTFS_FORMAT_ZERO_BYTE = 0U;
constexpr uint8_t OS_KERNEL_ROOTFS_FORMAT_PATH_SEPARATOR = static_cast<uint8_t>('/');
constexpr uint8_t OS_KERNEL_ROOTFS_FORMAT_DOT = static_cast<uint8_t>('.');
constexpr uint8_t OS_KERNEL_ROOTFS_FORMAT_MAXIMUM_CONTROL_CHARACTER = 0x1FU;
constexpr uint8_t OS_KERNEL_ROOTFS_FORMAT_DELETE_CONTROL_CHARACTER = 0x7FU;
constexpr uint32_t OS_KERNEL_ROOTFS_FORMAT_CRC32_INITIAL_VALUE = 0xFFFFFFFFU;
constexpr uint32_t OS_KERNEL_ROOTFS_FORMAT_CRC32_FINAL_XOR = 0xFFFFFFFFU;
constexpr uint32_t OS_KERNEL_ROOTFS_FORMAT_CRC32_REFLECTED_POLYNOMIAL = 0xEDB88320U;
constexpr uint32_t OS_KERNEL_ROOTFS_FORMAT_CRC32_LOW_BIT_MASK = 0x00000001U;
constexpr uint32_t OS_KERNEL_ROOTFS_FORMAT_ZERO_CRC32 = 0U;

constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_VERSION_FIELD_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_BLOCK_SIZE_FIELD_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_TOTAL_BLOCKS_FIELD_INDEX = 2ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_BITMAP_START_FIELD_INDEX = 3ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_BITMAP_COUNT_FIELD_INDEX = 4ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_TABLE_START_FIELD_INDEX = 5ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_TABLE_COUNT_FIELD_INDEX = 6ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_DATA_BITMAP_START_FIELD_INDEX = 7ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_DATA_BITMAP_COUNT_FIELD_INDEX = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_DATA_START_FIELD_INDEX = 9ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_DATA_COUNT_FIELD_INDEX = 10ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_COUNT_FIELD_INDEX = 11ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_ROOT_INODE_FIELD_INDEX = 12ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_MAXIMUM_FILE_SIZE_FIELD_INDEX = 13ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_TRANSACTION_STATE_FIELD_INDEX = 14ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_TRANSACTION_GENERATION_FIELD_INDEX = 15ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_NEXT_INODE_GENERATION_FIELD_INDEX = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FEATURE_FLAGS_FIELD_INDEX = 17ULL;

void ClearBytes(uint8_t *const bytes, const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        bytes[byte_index] = OS_KERNEL_ROOTFS_FORMAT_ZERO_BYTE;
    }
}

[[nodiscard]] bool BytesAreZero(const uint8_t *const bytes, const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        if (bytes[byte_index] != OS_KERNEL_ROOTFS_FORMAT_ZERO_BYTE) {
            return false;
        }
    }
    return true;
}

void StoreLittleEndian64(uint8_t *const bytes, const uint64_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES; ++byte_index) {
        bytes[byte_index] =
            static_cast<uint8_t>(value >> (byte_index * OS_KERNEL_ROOTFS_FORMAT_BITS_PER_BYTE));
    }
}

[[nodiscard]] uint64_t LoadLittleEndian64(const uint8_t *const bytes) noexcept {
    uint64_t value = OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE;
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES; ++byte_index) {
        value |= static_cast<uint64_t>(bytes[byte_index])
                 << (byte_index * OS_KERNEL_ROOTFS_FORMAT_BITS_PER_BYTE);
    }
    return value;
}

void StoreLittleEndian32(uint8_t *const bytes, const uint32_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_FORMAT_UINT32_SIZE_BYTES; ++byte_index) {
        bytes[byte_index] = static_cast<uint8_t>(
            value >> static_cast<uint32_t>(byte_index * OS_KERNEL_ROOTFS_FORMAT_BITS_PER_BYTE));
    }
}

[[nodiscard]] uint32_t LoadLittleEndian32(const uint8_t *const bytes) noexcept {
    uint32_t value = 0U;
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_FORMAT_UINT32_SIZE_BYTES; ++byte_index) {
        value |= static_cast<uint32_t>(bytes[byte_index])
                 << static_cast<uint32_t>(byte_index * OS_KERNEL_ROOTFS_FORMAT_BITS_PER_BYTE);
    }
    return value;
}

[[nodiscard]] bool NodeTypeIsValid(const RootNodeType type, const bool allow_unused) noexcept {
    return type == RootNodeType::RegularFile || type == RootNodeType::Directory ||
           (allow_unused && type == RootNodeType::Unused);
}

[[nodiscard]] bool BlockReferenceIsValid(const uint64_t relative_block) noexcept {
    return relative_block == OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
           (relative_block >= OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK &&
            relative_block < OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT);
}

[[nodiscard]] bool NameIsValid(const uint8_t *const name,
                               const uint64_t name_length_bytes) noexcept {
    if (name_length_bytes == OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
        name_length_bytes > OS_KERNEL_ROOTFS_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX; byte_index < name_length_bytes;
         ++byte_index) {
        const uint8_t value = name[byte_index];
        if (value <= OS_KERNEL_ROOTFS_FORMAT_MAXIMUM_CONTROL_CHARACTER ||
            value == OS_KERNEL_ROOTFS_FORMAT_DELETE_CONTROL_CHARACTER ||
            value == OS_KERNEL_ROOTFS_FORMAT_PATH_SEPARATOR) {
            return false;
        }
    }
    const bool dot = name_length_bytes == OS_KERNEL_ROOTFS_FORMAT_COUNTER_INCREMENT &&
                     name[OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX] == OS_KERNEL_ROOTFS_FORMAT_DOT;
    const bool dot_dot =
        name_length_bytes ==
            OS_KERNEL_ROOTFS_FORMAT_COUNTER_INCREMENT + OS_KERNEL_ROOTFS_FORMAT_COUNTER_INCREMENT &&
        name[OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX] == OS_KERNEL_ROOTFS_FORMAT_DOT &&
        name[OS_KERNEL_ROOTFS_FORMAT_COUNTER_INCREMENT] == OS_KERNEL_ROOTFS_FORMAT_DOT;
    return !dot && !dot_dot;
}

[[nodiscard]] bool SuperblockLayoutIsValid(const RootSuperblock &superblock) noexcept {
    return superblock.version == OS_KERNEL_ROOTFS_FORMAT_VERSION &&
           superblock.block_size_bytes == OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES &&
           superblock.total_block_count == OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT &&
           superblock.inode_bitmap_start_relative_block ==
               OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK &&
           superblock.inode_bitmap_block_count == OS_KERNEL_ROOTFS_INODE_BITMAP_BLOCK_COUNT &&
           superblock.inode_table_start_relative_block ==
               OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK &&
           superblock.inode_table_block_count == OS_KERNEL_ROOTFS_INODE_TABLE_BLOCK_COUNT &&
           superblock.data_bitmap_start_relative_block ==
               OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK &&
           superblock.data_bitmap_block_count == OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT &&
           superblock.data_start_relative_block == OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK &&
           superblock.data_block_count == OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT &&
           superblock.inode_count == OS_KERNEL_ROOTFS_INODE_COUNT &&
           superblock.root_inode_number == OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER &&
           superblock.maximum_file_size_bytes == OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES &&
           superblock.feature_flags == OS_KERNEL_ROOTFS_REQUIRED_FEATURES &&
           superblock.transaction_generation != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE &&
           superblock.next_inode_generation != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE;
}

[[nodiscard]] bool InodeIsValid(const RootInode &inode, const bool allow_unused) noexcept {
    if (!NodeTypeIsValid(inode.type, allow_unused)) {
        return false;
    }
    if (inode.type == RootNodeType::Unused) {
        if (inode.flags != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.size_bytes != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.generation != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.link_count != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.allocated_data_block_count != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.allocated_metadata_block_count != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.parent_inode_number != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.single_indirect_block != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.double_indirect_block != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            inode.triple_indirect_block != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE) {
            return false;
        }
        for (uint64_t block_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
             block_index < OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT; ++block_index) {
            if (inode.direct_blocks[block_index] != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE) {
                return false;
            }
        }
        return true;
    }
    if (inode.flags != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
        inode.size_bytes > OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES ||
        inode.generation == OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
        inode.link_count < OS_KERNEL_ROOTFS_FORMAT_ROOT_LINK_COUNT ||
        inode.parent_inode_number == OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
        inode.parent_inode_number > OS_KERNEL_ROOTFS_INODE_COUNT ||
        inode.allocated_data_block_count > OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT ||
        inode.allocated_metadata_block_count > OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT ||
        !BlockReferenceIsValid(inode.single_indirect_block) ||
        !BlockReferenceIsValid(inode.double_indirect_block) ||
        !BlockReferenceIsValid(inode.triple_indirect_block)) {
        return false;
    }
    if (inode.type == RootNodeType::Directory &&
        inode.size_bytes % OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES !=
            OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE) {
        return false;
    }
    for (uint64_t block_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         block_index < OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT; ++block_index) {
        if (!BlockReferenceIsValid(inode.direct_blocks[block_index])) {
            return false;
        }
    }
    return true;
}

}

uint32_t CalculateRootCrc32(const uint8_t *const bytes, const uint64_t length_bytes) noexcept {
    if (bytes == nullptr) {
        return OS_KERNEL_ROOTFS_FORMAT_ZERO_CRC32;
    }
    uint32_t crc = OS_KERNEL_ROOTFS_FORMAT_CRC32_INITIAL_VALUE;
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        crc ^= static_cast<uint32_t>(bytes[byte_index]);
        for (uint64_t bit_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
             bit_index < OS_KERNEL_ROOTFS_FORMAT_BITS_PER_BYTE; ++bit_index) {
            const bool low_bit_set = (crc & OS_KERNEL_ROOTFS_FORMAT_CRC32_LOW_BIT_MASK) !=
                                     OS_KERNEL_ROOTFS_FORMAT_ZERO_CRC32;
            crc >>= OS_KERNEL_ROOTFS_FORMAT_COUNTER_INCREMENT;
            if (low_bit_set) {
                crc ^= OS_KERNEL_ROOTFS_FORMAT_CRC32_REFLECTED_POLYNOMIAL;
            }
        }
    }
    return crc ^ OS_KERNEL_ROOTFS_FORMAT_CRC32_FINAL_XOR;
}

RootFormatStatus DecodeRootSuperblock(const uint8_t *const block, const uint64_t block_size_bytes,
                                      RootSuperblock &superblock) noexcept {
    superblock = RootSuperblock{};
    if (block == nullptr) {
        return RootFormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) {
        return RootFormatStatus::InvalidBufferSize;
    }
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_FORMAT_MAGIC_SIZE_BYTES; ++byte_index) {
        if (block[byte_index] != OS_KERNEL_ROOTFS_FORMAT_MAGIC[byte_index]) {
            return RootFormatStatus::InvalidMagic;
        }
    }
    const uint32_t stored_checksum =
        LoadLittleEndian32(block + OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    const uint32_t calculated_checksum =
        CalculateRootCrc32(block, OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    if (stored_checksum != calculated_checksum) {
        return RootFormatStatus::InvalidChecksum;
    }
    if (!BytesAreZero(block + OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_RESERVED_START_BYTES,
                      OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES -
                          OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_RESERVED_START_BYTES)) {
        return RootFormatStatus::NonZeroReservedBytes;
    }
    uint64_t fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FIELD_COUNT]{};
    for (uint64_t field_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         field_index < OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FIELD_COUNT; ++field_index) {
        fields[field_index] =
            LoadLittleEndian64(block + OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FIELD_START_BYTES +
                               field_index * OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES);
    }
    RootSuperblock decoded{
        .version = fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_VERSION_FIELD_INDEX],
        .block_size_bytes = fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_BLOCK_SIZE_FIELD_INDEX],
        .total_block_count = fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_TOTAL_BLOCKS_FIELD_INDEX],
        .inode_bitmap_start_relative_block =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_BITMAP_START_FIELD_INDEX],
        .inode_bitmap_block_count =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_BITMAP_COUNT_FIELD_INDEX],
        .inode_table_start_relative_block =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_TABLE_START_FIELD_INDEX],
        .inode_table_block_count =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_TABLE_COUNT_FIELD_INDEX],
        .data_bitmap_start_relative_block =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_DATA_BITMAP_START_FIELD_INDEX],
        .data_bitmap_block_count =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_DATA_BITMAP_COUNT_FIELD_INDEX],
        .data_start_relative_block =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_DATA_START_FIELD_INDEX],
        .data_block_count = fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_DATA_COUNT_FIELD_INDEX],
        .inode_count = fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_INODE_COUNT_FIELD_INDEX],
        .root_inode_number = fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_ROOT_INODE_FIELD_INDEX],
        .maximum_file_size_bytes =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_MAXIMUM_FILE_SIZE_FIELD_INDEX],
        .transaction_state = static_cast<RootTransactionState>(
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_TRANSACTION_STATE_FIELD_INDEX]),
        .transaction_generation =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_TRANSACTION_GENERATION_FIELD_INDEX],
        .next_inode_generation =
            fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_NEXT_INODE_GENERATION_FIELD_INDEX],
        .feature_flags = fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FEATURE_FLAGS_FIELD_INDEX],
    };
    if (decoded.version != OS_KERNEL_ROOTFS_FORMAT_VERSION) {
        return RootFormatStatus::InvalidVersion;
    }
    if (!SuperblockLayoutIsValid(decoded)) {
        return RootFormatStatus::InvalidLayout;
    }
    if (decoded.transaction_state != RootTransactionState::Clean &&
        decoded.transaction_state != RootTransactionState::Dirty) {
        return RootFormatStatus::InvalidTransactionState;
    }
    superblock = decoded;
    return RootFormatStatus::Succeeded;
}

RootFormatStatus EncodeRootSuperblock(const RootSuperblock &superblock, uint8_t *const block,
                                      const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootFormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) {
        return RootFormatStatus::InvalidBufferSize;
    }
    if (!SuperblockLayoutIsValid(superblock)) {
        return RootFormatStatus::InvalidLayout;
    }
    if (superblock.transaction_state != RootTransactionState::Clean &&
        superblock.transaction_state != RootTransactionState::Dirty) {
        return RootFormatStatus::InvalidTransactionState;
    }
    ClearBytes(block, block_size_bytes);
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_FORMAT_MAGIC_SIZE_BYTES; ++byte_index) {
        block[byte_index] = OS_KERNEL_ROOTFS_FORMAT_MAGIC[byte_index];
    }
    const uint64_t fields[OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FIELD_COUNT] = {
        superblock.version,
        superblock.block_size_bytes,
        superblock.total_block_count,
        superblock.inode_bitmap_start_relative_block,
        superblock.inode_bitmap_block_count,
        superblock.inode_table_start_relative_block,
        superblock.inode_table_block_count,
        superblock.data_bitmap_start_relative_block,
        superblock.data_bitmap_block_count,
        superblock.data_start_relative_block,
        superblock.data_block_count,
        superblock.inode_count,
        superblock.root_inode_number,
        superblock.maximum_file_size_bytes,
        static_cast<uint64_t>(superblock.transaction_state),
        superblock.transaction_generation,
        superblock.next_inode_generation,
        superblock.feature_flags,
    };
    for (uint64_t field_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         field_index < OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FIELD_COUNT; ++field_index) {
        StoreLittleEndian64(block + OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_FIELD_START_BYTES +
                                field_index * OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES,
                            fields[field_index]);
    }
    const uint32_t checksum =
        CalculateRootCrc32(block, OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    StoreLittleEndian32(block + OS_KERNEL_ROOTFS_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES, checksum);
    return RootFormatStatus::Succeeded;
}

RootFormatStatus DecodeRootInode(const uint8_t *const bytes, const uint64_t byte_count,
                                 RootInode &inode) noexcept {
    inode = RootInode{};
    if (bytes == nullptr) {
        return RootFormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_INODE_SIZE_BYTES) {
        return RootFormatStatus::InvalidBufferSize;
    }
    if (BytesAreZero(bytes, byte_count)) {
        inode.type = RootNodeType::Unused;
        return RootFormatStatus::Succeeded;
    }
    const uint32_t stored_checksum =
        LoadLittleEndian32(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    const uint32_t calculated_checksum =
        CalculateRootCrc32(bytes, OS_KERNEL_ROOTFS_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    if (stored_checksum != calculated_checksum) {
        return RootFormatStatus::InvalidChecksum;
    }
    if (!BytesAreZero(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_RESERVED_START_BYTES,
                      OS_KERNEL_ROOTFS_FORMAT_INODE_CHECKSUM_OFFSET_BYTES -
                          OS_KERNEL_ROOTFS_FORMAT_INODE_RESERVED_START_BYTES)) {
        return RootFormatStatus::NonZeroReservedBytes;
    }
    RootInode decoded{
        .type = static_cast<RootNodeType>(
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_TYPE_OFFSET_BYTES)),
        .flags = LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_FLAGS_OFFSET_BYTES),
        .size_bytes = LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_SIZE_OFFSET_BYTES),
        .generation =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_GENERATION_OFFSET_BYTES),
        .link_count =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_LINK_COUNT_OFFSET_BYTES),
        .allocated_data_block_count =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_DATA_BLOCK_COUNT_OFFSET_BYTES),
        .allocated_metadata_block_count = LoadLittleEndian64(
            bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_METADATA_BLOCK_COUNT_OFFSET_BYTES),
        .parent_inode_number =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_PARENT_OFFSET_BYTES),
        .direct_blocks = {},
        .single_indirect_block =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_SINGLE_INDIRECT_OFFSET_BYTES),
        .double_indirect_block =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_DOUBLE_INDIRECT_OFFSET_BYTES),
        .triple_indirect_block =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_TRIPLE_INDIRECT_OFFSET_BYTES),
    };
    for (uint64_t block_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         block_index < OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT; ++block_index) {
        decoded.direct_blocks[block_index] =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_DIRECT_START_BYTES +
                               block_index * OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES);
    }
    if (!InodeIsValid(decoded, false)) {
        return RootFormatStatus::InvalidInode;
    }
    inode = decoded;
    return RootFormatStatus::Succeeded;
}

RootFormatStatus EncodeRootInode(const RootInode &inode, uint8_t *const bytes,
                                 const uint64_t byte_count) noexcept {
    if (bytes == nullptr) {
        return RootFormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_INODE_SIZE_BYTES) {
        return RootFormatStatus::InvalidBufferSize;
    }
    if (!InodeIsValid(inode, true)) {
        return RootFormatStatus::InvalidInode;
    }
    ClearBytes(bytes, byte_count);
    if (inode.type == RootNodeType::Unused) {
        return RootFormatStatus::Succeeded;
    }
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_TYPE_OFFSET_BYTES,
                        static_cast<uint64_t>(inode.type));
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_FLAGS_OFFSET_BYTES, inode.flags);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_SIZE_OFFSET_BYTES, inode.size_bytes);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_GENERATION_OFFSET_BYTES,
                        inode.generation);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_LINK_COUNT_OFFSET_BYTES,
                        inode.link_count);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_DATA_BLOCK_COUNT_OFFSET_BYTES,
                        inode.allocated_data_block_count);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_METADATA_BLOCK_COUNT_OFFSET_BYTES,
                        inode.allocated_metadata_block_count);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_PARENT_OFFSET_BYTES,
                        inode.parent_inode_number);
    for (uint64_t block_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         block_index < OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT; ++block_index) {
        StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_DIRECT_START_BYTES +
                                block_index * OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES,
                            inode.direct_blocks[block_index]);
    }
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_SINGLE_INDIRECT_OFFSET_BYTES,
                        inode.single_indirect_block);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_DOUBLE_INDIRECT_OFFSET_BYTES,
                        inode.double_indirect_block);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_TRIPLE_INDIRECT_OFFSET_BYTES,
                        inode.triple_indirect_block);
    const uint32_t checksum =
        CalculateRootCrc32(bytes, OS_KERNEL_ROOTFS_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    StoreLittleEndian32(bytes + OS_KERNEL_ROOTFS_FORMAT_INODE_CHECKSUM_OFFSET_BYTES, checksum);
    return RootFormatStatus::Succeeded;
}

RootFormatStatus DecodeRootDirectoryEntry(const uint8_t *const bytes, const uint64_t byte_count,
                                          RootDirectoryEntry &entry) noexcept {
    entry = RootDirectoryEntry{};
    if (bytes == nullptr) {
        return RootFormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        return RootFormatStatus::InvalidBufferSize;
    }
    if (BytesAreZero(bytes, byte_count)) {
        entry.type = RootNodeType::Unused;
        return RootFormatStatus::Succeeded;
    }
    const uint32_t stored_checksum =
        LoadLittleEndian32(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_CHECKSUM_OFFSET_BYTES);
    const uint32_t calculated_checksum =
        CalculateRootCrc32(bytes, OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_CHECKSUM_OFFSET_BYTES);
    if (stored_checksum != calculated_checksum) {
        return RootFormatStatus::InvalidChecksum;
    }
    if (!BytesAreZero(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_RESERVED_START_BYTES,
                      OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES -
                          OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_RESERVED_START_BYTES)) {
        return RootFormatStatus::NonZeroReservedBytes;
    }
    RootDirectoryEntry decoded{
        .inode_number =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_INODE_OFFSET_BYTES),
        .inode_generation =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_GENERATION_OFFSET_BYTES),
        .type = static_cast<RootNodeType>(
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_TYPE_OFFSET_BYTES)),
        .name_length_bytes =
            LoadLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_OFFSET_BYTES),
        .name = {},
    };
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_NAME_STORAGE_SIZE_BYTES; ++byte_index) {
        decoded.name[byte_index] =
            bytes[OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_NAME_START_BYTES + byte_index];
    }
    if (decoded.inode_number == OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
        decoded.inode_number > OS_KERNEL_ROOTFS_INODE_COUNT ||
        decoded.inode_generation == OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
        !NodeTypeIsValid(decoded.type, false) ||
        !NameIsValid(decoded.name, decoded.name_length_bytes) ||
        !BytesAreZero(decoded.name + decoded.name_length_bytes,
                      OS_KERNEL_ROOTFS_NAME_STORAGE_SIZE_BYTES - decoded.name_length_bytes)) {
        return RootFormatStatus::InvalidDirectoryEntry;
    }
    entry = decoded;
    return RootFormatStatus::Succeeded;
}

RootFormatStatus EncodeRootDirectoryEntry(const RootDirectoryEntry &entry, uint8_t *const bytes,
                                          const uint64_t byte_count) noexcept {
    if (bytes == nullptr) {
        return RootFormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        return RootFormatStatus::InvalidBufferSize;
    }
    ClearBytes(bytes, byte_count);
    if (entry.type == RootNodeType::Unused) {
        if (entry.inode_number != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            entry.inode_generation != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            entry.name_length_bytes != OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
            !BytesAreZero(entry.name, OS_KERNEL_ROOTFS_NAME_STORAGE_SIZE_BYTES)) {
            return RootFormatStatus::InvalidDirectoryEntry;
        }
        return RootFormatStatus::Succeeded;
    }
    if (entry.inode_number == OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
        entry.inode_number > OS_KERNEL_ROOTFS_INODE_COUNT ||
        entry.inode_generation == OS_KERNEL_ROOTFS_FORMAT_EMPTY_VALUE ||
        !NodeTypeIsValid(entry.type, false) || !NameIsValid(entry.name, entry.name_length_bytes)) {
        return RootFormatStatus::InvalidDirectoryEntry;
    }
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_INODE_OFFSET_BYTES,
                        entry.inode_number);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_GENERATION_OFFSET_BYTES,
                        entry.inode_generation);
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_TYPE_OFFSET_BYTES,
                        static_cast<uint64_t>(entry.type));
    StoreLittleEndian64(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_OFFSET_BYTES,
                        entry.name_length_bytes);
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         byte_index < entry.name_length_bytes; ++byte_index) {
        bytes[OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_NAME_START_BYTES + byte_index] =
            entry.name[byte_index];
    }
    const uint32_t checksum =
        CalculateRootCrc32(bytes, OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_CHECKSUM_OFFSET_BYTES);
    StoreLittleEndian32(bytes + OS_KERNEL_ROOTFS_FORMAT_DIRECTORY_CHECKSUM_OFFSET_BYTES, checksum);
    return RootFormatStatus::Succeeded;
}

RootFormatStatus DecodeRootPointerBlock(const uint8_t *const block, const uint64_t block_size_bytes,
                                        RootPointerBlock &pointer_block) noexcept {
    pointer_block = RootPointerBlock{};
    if (block == nullptr) {
        return RootFormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) {
        return RootFormatStatus::InvalidBufferSize;
    }
    const uint32_t stored_checksum =
        LoadLittleEndian32(block + OS_KERNEL_ROOTFS_FORMAT_POINTER_CHECKSUM_OFFSET_BYTES);
    const uint32_t calculated_checksum =
        CalculateRootCrc32(block, OS_KERNEL_ROOTFS_FORMAT_POINTER_CHECKSUM_OFFSET_BYTES);
    if (stored_checksum != calculated_checksum) {
        return RootFormatStatus::InvalidChecksum;
    }
    if (!BytesAreZero(block + OS_KERNEL_ROOTFS_FORMAT_POINTER_RESERVED_START_BYTES,
                      OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES -
                          OS_KERNEL_ROOTFS_FORMAT_POINTER_RESERVED_START_BYTES)) {
        return RootFormatStatus::NonZeroReservedBytes;
    }
    RootPointerBlock decoded{};
    for (uint64_t pointer_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         pointer_index < OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK; ++pointer_index) {
        decoded.pointers[pointer_index] =
            LoadLittleEndian64(block + pointer_index * OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES);
        if (!BlockReferenceIsValid(decoded.pointers[pointer_index])) {
            return RootFormatStatus::InvalidPointerBlock;
        }
    }
    pointer_block = decoded;
    return RootFormatStatus::Succeeded;
}

RootFormatStatus EncodeRootPointerBlock(const RootPointerBlock &pointer_block, uint8_t *const block,
                                        const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootFormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) {
        return RootFormatStatus::InvalidBufferSize;
    }
    ClearBytes(block, block_size_bytes);
    for (uint64_t pointer_index = OS_KERNEL_ROOTFS_FORMAT_FIRST_INDEX;
         pointer_index < OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK; ++pointer_index) {
        if (!BlockReferenceIsValid(pointer_block.pointers[pointer_index])) {
            return RootFormatStatus::InvalidPointerBlock;
        }
        StoreLittleEndian64(block + pointer_index * OS_KERNEL_ROOTFS_FORMAT_UINT64_SIZE_BYTES,
                            pointer_block.pointers[pointer_index]);
    }
    const uint32_t checksum =
        CalculateRootCrc32(block, OS_KERNEL_ROOTFS_FORMAT_POINTER_CHECKSUM_OFFSET_BYTES);
    StoreLittleEndian32(block + OS_KERNEL_ROOTFS_FORMAT_POINTER_CHECKSUM_OFFSET_BYTES, checksum);
    return RootFormatStatus::Succeeded;
}

bool RootBlockIsZero(const uint8_t *const block, const uint64_t block_size_bytes) noexcept {
    return block != nullptr && block_size_bytes == OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES &&
           BytesAreZero(block, block_size_bytes);
}

}
