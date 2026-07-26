#include "os/kernel/fs/file_system_format.hpp"

namespace os::kernel {

namespace {

constexpr uint8_t OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC[] = {'O', 'S', 'F', 'S', 'V', '0', '0', '1'};
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_BLOCK_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_TOTAL_BLOCK_COUNT_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_BITMAP_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_START_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_COUNT_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BITMAP_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_START_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_COUNT_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BLOCK_COUNT_OFFSET_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_ROOT_INODE_OFFSET_BYTES = 88ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_STATE_OFFSET_BYTES = 96ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_GENERATION_OFFSET_BYTES = 104ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES = 508ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TYPE_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_SIZE_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_GENERATION_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_LINK_COUNT_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_ALLOCATED_BLOCK_COUNT_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_DIRECT_BLOCKS_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_RESERVED_OFFSET_BYTES = 120ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES = 124ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_INODE_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_TYPE_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_LENGTH_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_UINT32_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE = 8ULL;
constexpr uint32_t OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_INITIAL_VALUE = 0xFFFFFFFFU;
constexpr uint32_t OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_FINAL_XOR = 0xFFFFFFFFU;
constexpr uint32_t OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_REFLECTED_POLYNOMIAL = 0xEDB88320U;
constexpr uint32_t OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_LOW_BIT_MASK = 0x00000001U;
constexpr uint8_t OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_BYTE = 0U;
constexpr uint32_t OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32 = 0U;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_ROOT_LINK_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FORMAT_INITIAL_GENERATION = 1ULL;

void ClearBytes(uint8_t *bytes, const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < byte_count; ++byte_index) {
        bytes[byte_index] = OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_BYTE;
    }
}

void StoreLittleEndian64(uint8_t *bytes, const uint64_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES; ++byte_index) {
        bytes[byte_index] = static_cast<uint8_t>(
            value >> (byte_index * OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE));
    }
}

[[nodiscard]] uint64_t LoadLittleEndian64(const uint8_t *bytes) noexcept {
    uint64_t value = OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64;
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES; ++byte_index) {
        value |= static_cast<uint64_t>(bytes[byte_index])
                 << (byte_index * OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE);
    }
    return value;
}

void StoreLittleEndian32(uint8_t *bytes, const uint32_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < OS_KERNEL_FILE_SYSTEM_FORMAT_UINT32_SIZE_BYTES; ++byte_index) {
        bytes[byte_index] = static_cast<uint8_t>(
            value >>
            static_cast<uint32_t>(byte_index * OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE));
    }
}

[[nodiscard]] uint32_t LoadLittleEndian32(const uint8_t *bytes) noexcept {
    uint32_t value = OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32;
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < OS_KERNEL_FILE_SYSTEM_FORMAT_UINT32_SIZE_BYTES; ++byte_index) {
        value |= static_cast<uint32_t>(bytes[byte_index])
                 << static_cast<uint32_t>(byte_index * OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE);
    }
    return value;
}

[[nodiscard]] bool NodeTypeIsValid(const FileSystemNodeType type,
                                   const bool allow_unused) noexcept {
    return type == FileSystemNodeType::RegularFile || type == FileSystemNodeType::Directory ||
           (allow_unused && type == FileSystemNodeType::Unused);
}

[[nodiscard]] bool SuperblockLayoutIsValid(const FileSystemSuperblock &superblock) noexcept {
    return superblock.version == OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION &&
           superblock.block_size_bytes == OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES &&
           superblock.total_block_count == OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT &&
           superblock.inode_bitmap_relative_block ==
               OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK &&
           superblock.inode_table_start_relative_block ==
               OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK &&
           superblock.inode_table_block_count == OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT &&
           superblock.data_bitmap_relative_block ==
               OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK &&
           superblock.data_start_relative_block ==
               OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK &&
           superblock.inode_count == OS_KERNEL_FILE_SYSTEM_INODE_COUNT &&
           superblock.data_block_count == OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT &&
           superblock.root_inode_number == OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
}

}

uint32_t CalculateFileSystemCrc32(const uint8_t *bytes, const uint64_t length_bytes) noexcept {
    if (bytes == nullptr) {
        return OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32;
    }
    uint32_t crc = OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_INITIAL_VALUE;
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < length_bytes; ++byte_index) {
        crc ^= static_cast<uint32_t>(bytes[byte_index]);
        for (uint64_t bit_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
             bit_index < OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE; ++bit_index) {
            const bool low_bit_set = (crc & OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_LOW_BIT_MASK) !=
                                     OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32;
            crc >>= OS_KERNEL_FILE_SYSTEM_FORMAT_COUNTER_INCREMENT;
            if (low_bit_set) {
                crc ^= OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_REFLECTED_POLYNOMIAL;
            }
        }
    }
    return crc ^ OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_FINAL_XOR;
}

bool FileSystemBlockIsZero(const uint8_t *block, const uint64_t block_size_bytes) noexcept {
    if (block == nullptr || block_size_bytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < block_size_bytes; ++byte_index) {
        if (block[byte_index] != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_BYTE) {
            return false;
        }
    }
    return true;
}

FileSystemSuperblock CreateFileSystemSuperblock() noexcept {
    return FileSystemSuperblock{
        .version = OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION,
        .block_size_bytes = OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES,
        .total_block_count = OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT,
        .inode_bitmap_relative_block = OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK,
        .inode_table_start_relative_block = OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK,
        .inode_table_block_count = OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT,
        .data_bitmap_relative_block = OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK,
        .data_start_relative_block = OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK,
        .inode_count = OS_KERNEL_FILE_SYSTEM_INODE_COUNT,
        .data_block_count = OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT,
        .root_inode_number = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER,
        .transaction_state = FileSystemTransactionState::Clean,
        .transaction_generation = OS_KERNEL_FILE_SYSTEM_FORMAT_INITIAL_GENERATION,
    };
}

FileSystemFormatStatus EncodeFileSystemSuperblock(const FileSystemSuperblock &superblock,
                                                  uint8_t *block,
                                                  const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    if (!SuperblockLayoutIsValid(superblock)) {
        return FileSystemFormatStatus::InvalidLayout;
    }
    if (superblock.transaction_state != FileSystemTransactionState::Clean &&
        superblock.transaction_state != FileSystemTransactionState::Dirty) {
        return FileSystemFormatStatus::InvalidTransactionState;
    }

    ClearBytes(block, block_size_bytes);
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC_SIZE_BYTES; ++byte_index) {
        block[byte_index] = OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC[byte_index];
    }
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION_OFFSET_BYTES,
                        superblock.version);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_BLOCK_SIZE_OFFSET_BYTES,
                        superblock.block_size_bytes);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_TOTAL_BLOCK_COUNT_OFFSET_BYTES,
                        superblock.total_block_count);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_BITMAP_OFFSET_BYTES,
                        superblock.inode_bitmap_relative_block);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_START_OFFSET_BYTES,
                        superblock.inode_table_start_relative_block);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_COUNT_OFFSET_BYTES,
                        superblock.inode_table_block_count);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BITMAP_OFFSET_BYTES,
                        superblock.data_bitmap_relative_block);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_START_OFFSET_BYTES,
                        superblock.data_start_relative_block);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_COUNT_OFFSET_BYTES,
                        superblock.inode_count);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BLOCK_COUNT_OFFSET_BYTES,
                        superblock.data_block_count);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_ROOT_INODE_OFFSET_BYTES,
                        superblock.root_inode_number);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_STATE_OFFSET_BYTES,
                        static_cast<uint64_t>(superblock.transaction_state));
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_GENERATION_OFFSET_BYTES,
                        superblock.transaction_generation);
    const uint32_t checksum = CalculateFileSystemCrc32(
        block, OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    StoreLittleEndian32(block + OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
                        checksum);
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus DecodeFileSystemSuperblock(const uint8_t *block,
                                                  const uint64_t block_size_bytes,
                                                  FileSystemSuperblock &superblock) noexcept {
    if (block == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC_SIZE_BYTES; ++byte_index) {
        if (block[byte_index] != OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC[byte_index]) {
            return FileSystemFormatStatus::InvalidMagic;
        }
    }
    const uint32_t stored_checksum =
        LoadLittleEndian32(block + OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    const uint32_t calculated_checksum = CalculateFileSystemCrc32(
        block, OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    if (stored_checksum != calculated_checksum) {
        return FileSystemFormatStatus::InvalidChecksum;
    }

    FileSystemSuperblock decoded{
        .version = LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION_OFFSET_BYTES),
        .block_size_bytes =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_BLOCK_SIZE_OFFSET_BYTES),
        .total_block_count =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_TOTAL_BLOCK_COUNT_OFFSET_BYTES),
        .inode_bitmap_relative_block =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_BITMAP_OFFSET_BYTES),
        .inode_table_start_relative_block =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_START_OFFSET_BYTES),
        .inode_table_block_count =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_COUNT_OFFSET_BYTES),
        .data_bitmap_relative_block =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BITMAP_OFFSET_BYTES),
        .data_start_relative_block =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_START_OFFSET_BYTES),
        .inode_count =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_COUNT_OFFSET_BYTES),
        .data_block_count =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BLOCK_COUNT_OFFSET_BYTES),
        .root_inode_number =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_ROOT_INODE_OFFSET_BYTES),
        .transaction_state = static_cast<FileSystemTransactionState>(LoadLittleEndian64(
            block + OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_STATE_OFFSET_BYTES)),
        .transaction_generation = LoadLittleEndian64(
            block + OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_GENERATION_OFFSET_BYTES),
    };
    if (decoded.version != OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION) {
        return FileSystemFormatStatus::InvalidVersion;
    }
    if (!SuperblockLayoutIsValid(decoded)) {
        return FileSystemFormatStatus::InvalidLayout;
    }
    if (decoded.transaction_state != FileSystemTransactionState::Clean &&
        decoded.transaction_state != FileSystemTransactionState::Dirty) {
        return FileSystemFormatStatus::InvalidTransactionState;
    }
    superblock = decoded;
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus EncodeFileSystemInode(const FileSystemInode &inode, uint8_t *bytes,
                                             const uint64_t byte_count) noexcept {
    if (bytes == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    if (!NodeTypeIsValid(inode.type, true) ||
        inode.allocated_block_count > OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT ||
        inode.size_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES ||
        (inode.type == FileSystemNodeType::Unused &&
         (inode.size_bytes != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          inode.allocated_block_count != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64))) {
        return FileSystemFormatStatus::InvalidInode;
    }

    ClearBytes(bytes, byte_count);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TYPE_OFFSET_BYTES,
                        static_cast<uint64_t>(inode.type));
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_SIZE_OFFSET_BYTES,
                        inode.size_bytes);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_GENERATION_OFFSET_BYTES,
                        inode.generation);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_LINK_COUNT_OFFSET_BYTES,
                        inode.link_count);
    StoreLittleEndian64(bytes +
                            OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_ALLOCATED_BLOCK_COUNT_OFFSET_BYTES,
                        inode.allocated_block_count);
    for (uint64_t block_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         block_index < OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT; ++block_index) {
        StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_DIRECT_BLOCKS_OFFSET_BYTES +
                                block_index * OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES,
                            inode.direct_blocks[block_index]);
    }
    StoreLittleEndian32(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_RESERVED_OFFSET_BYTES,
                        OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32);
    const uint32_t checksum =
        CalculateFileSystemCrc32(bytes, OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    StoreLittleEndian32(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES, checksum);
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus DecodeFileSystemInode(const uint8_t *bytes, const uint64_t byte_count,
                                             FileSystemInode &inode) noexcept {
    if (bytes == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    const uint32_t stored_checksum =
        LoadLittleEndian32(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    const uint32_t calculated_checksum =
        CalculateFileSystemCrc32(bytes, OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    if (stored_checksum != calculated_checksum) {
        return FileSystemFormatStatus::InvalidChecksum;
    }

    FileSystemInode decoded{
        .type = static_cast<FileSystemNodeType>(
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TYPE_OFFSET_BYTES)),
        .size_bytes =
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_SIZE_OFFSET_BYTES),
        .generation =
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_GENERATION_OFFSET_BYTES),
        .link_count =
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_LINK_COUNT_OFFSET_BYTES),
        .allocated_block_count = LoadLittleEndian64(
            bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_ALLOCATED_BLOCK_COUNT_OFFSET_BYTES),
        .direct_blocks = {},
    };
    for (uint64_t block_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         block_index < OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT; ++block_index) {
        decoded.direct_blocks[block_index] = LoadLittleEndian64(
            bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_DIRECT_BLOCKS_OFFSET_BYTES +
            block_index * OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES);
    }
    if (!NodeTypeIsValid(decoded.type, true) ||
        decoded.allocated_block_count > OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT ||
        decoded.size_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES ||
        decoded.size_bytes >
            decoded.allocated_block_count * OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES ||
        (decoded.type == FileSystemNodeType::Unused &&
         (decoded.size_bytes != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          decoded.allocated_block_count != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64)) ||
        (decoded.type != FileSystemNodeType::Unused &&
         decoded.link_count < OS_KERNEL_FILE_SYSTEM_FORMAT_ROOT_LINK_COUNT)) {
        return FileSystemFormatStatus::InvalidInode;
    }
    inode = decoded;
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus EncodeFileSystemDirectoryEntry(const FileSystemDirectoryEntry &entry,
                                                      uint8_t *bytes,
                                                      const uint64_t byte_count) noexcept {
    if (bytes == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    if (!NodeTypeIsValid(entry.type, true) ||
        entry.name_length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES ||
        (entry.type == FileSystemNodeType::Unused &&
         (entry.inode_number != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          entry.name_length_bytes != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64)) ||
        (entry.type != FileSystemNodeType::Unused &&
         (entry.inode_number == OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          entry.name_length_bytes == OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64))) {
        return FileSystemFormatStatus::InvalidDirectoryEntry;
    }
    ClearBytes(bytes, byte_count);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_INODE_OFFSET_BYTES,
                        entry.inode_number);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_TYPE_OFFSET_BYTES,
                        static_cast<uint64_t>(entry.type));
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_LENGTH_OFFSET_BYTES,
                        entry.name_length_bytes);
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < entry.name_length_bytes; ++byte_index) {
        bytes[OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_OFFSET_BYTES + byte_index] =
            entry.name[byte_index];
    }
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus DecodeFileSystemDirectoryEntry(const uint8_t *bytes,
                                                      const uint64_t byte_count,
                                                      FileSystemDirectoryEntry &entry) noexcept {
    if (bytes == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    FileSystemDirectoryEntry decoded{
        .inode_number =
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_INODE_OFFSET_BYTES),
        .type = static_cast<FileSystemNodeType>(
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_TYPE_OFFSET_BYTES)),
        .name_length_bytes = LoadLittleEndian64(
            bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_LENGTH_OFFSET_BYTES),
        .name = {},
    };
    if (!NodeTypeIsValid(decoded.type, true) ||
        decoded.name_length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES ||
        (decoded.type == FileSystemNodeType::Unused &&
         (decoded.inode_number != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          decoded.name_length_bytes != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64)) ||
        (decoded.type != FileSystemNodeType::Unused &&
         (decoded.inode_number == OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          decoded.name_length_bytes == OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64))) {
        return FileSystemFormatStatus::InvalidDirectoryEntry;
    }
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byte_index < OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES; ++byte_index) {
        decoded.name[byte_index] =
            bytes[OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_OFFSET_BYTES + byte_index];
        if (byte_index >= decoded.name_length_bytes &&
            decoded.name[byte_index] != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_BYTE) {
            return FileSystemFormatStatus::InvalidDirectoryEntry;
        }
    }
    entry = decoded;
    return FileSystemFormatStatus::Succeeded;
}
}
