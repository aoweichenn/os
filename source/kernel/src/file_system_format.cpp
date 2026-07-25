#include "os/kernel/file_system_format.hpp"

namespace os::kernel {

namespace {

constexpr uint8_t OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC[] = {'O', 'S', 'F', 'S',
                                                          'V', '0', '0', '1'};
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

void ClearBytes(uint8_t *bytes, const uint64_t byteCount) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < byteCount; ++byteIndex) {
        bytes[byteIndex] = OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_BYTE;
    }
}

void StoreLittleEndian64(uint8_t *bytes, const uint64_t value) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES; ++byteIndex) {
        bytes[byteIndex] =
            static_cast<uint8_t>(value >> (byteIndex * OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE));
    }
}

[[nodiscard]] uint64_t LoadLittleEndian64(const uint8_t *bytes) noexcept {
    uint64_t value = OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64;
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES; ++byteIndex) {
        value |= static_cast<uint64_t>(bytes[byteIndex])
                 << (byteIndex * OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE);
    }
    return value;
}

void StoreLittleEndian32(uint8_t *bytes, const uint32_t value) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < OS_KERNEL_FILE_SYSTEM_FORMAT_UINT32_SIZE_BYTES; ++byteIndex) {
        bytes[byteIndex] = static_cast<uint8_t>(
            value >> static_cast<uint32_t>(byteIndex * OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE));
    }
}

[[nodiscard]] uint32_t LoadLittleEndian32(const uint8_t *bytes) noexcept {
    uint32_t value = OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32;
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < OS_KERNEL_FILE_SYSTEM_FORMAT_UINT32_SIZE_BYTES; ++byteIndex) {
        value |= static_cast<uint32_t>(bytes[byteIndex])
                 << static_cast<uint32_t>(byteIndex *
                                          OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE);
    }
    return value;
}

[[nodiscard]] bool NodeTypeIsValid(const FileSystemNodeType type,
                                   const bool allowUnused) noexcept {
    return type == FileSystemNodeType::RegularFile || type == FileSystemNodeType::Directory ||
           (allowUnused && type == FileSystemNodeType::Unused);
}

[[nodiscard]] bool SuperblockLayoutIsValid(
    const FileSystemSuperblock &superblock) noexcept {
    return superblock.version == OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION &&
           superblock.blockSizeBytes == OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES &&
           superblock.totalBlockCount == OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT &&
           superblock.inodeBitmapRelativeBlock ==
               OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK &&
           superblock.inodeTableStartRelativeBlock ==
               OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK &&
           superblock.inodeTableBlockCount ==
               OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT &&
           superblock.dataBitmapRelativeBlock ==
               OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK &&
           superblock.dataStartRelativeBlock ==
               OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK &&
           superblock.inodeCount == OS_KERNEL_FILE_SYSTEM_INODE_COUNT &&
           superblock.dataBlockCount == OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT &&
           superblock.rootInodeNumber == OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
}

}

uint32_t CalculateFileSystemCrc32(const uint8_t *bytes, const uint64_t lengthBytes) noexcept {
    if (bytes == nullptr) {
        return OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32;
    }
    uint32_t crc = OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_INITIAL_VALUE;
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < lengthBytes; ++byteIndex) {
        crc ^= static_cast<uint32_t>(bytes[byteIndex]);
        for (uint64_t bitIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
             bitIndex < OS_KERNEL_FILE_SYSTEM_FORMAT_BITS_PER_BYTE; ++bitIndex) {
            const bool lowBitSet =
                (crc & OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_LOW_BIT_MASK) !=
                OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32;
            crc >>= OS_KERNEL_FILE_SYSTEM_FORMAT_COUNTER_INCREMENT;
            if (lowBitSet) {
                crc ^= OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_REFLECTED_POLYNOMIAL;
            }
        }
    }
    return crc ^ OS_KERNEL_FILE_SYSTEM_FORMAT_CRC32_FINAL_XOR;
}

bool FileSystemBlockIsZero(const uint8_t *block, const uint64_t blockSizeBytes) noexcept {
    if (block == nullptr || blockSizeBytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return false;
    }
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < blockSizeBytes; ++byteIndex) {
        if (block[byteIndex] != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_BYTE) {
            return false;
        }
    }
    return true;
}

FileSystemSuperblock CreateFileSystemSuperblock() noexcept {
    return FileSystemSuperblock{
        .version = OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION,
        .blockSizeBytes = OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES,
        .totalBlockCount = OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT,
        .inodeBitmapRelativeBlock = OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK,
        .inodeTableStartRelativeBlock = OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK,
        .inodeTableBlockCount = OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT,
        .dataBitmapRelativeBlock = OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK,
        .dataStartRelativeBlock = OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK,
        .inodeCount = OS_KERNEL_FILE_SYSTEM_INODE_COUNT,
        .dataBlockCount = OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT,
        .rootInodeNumber = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER,
        .transactionState = FileSystemTransactionState::Clean,
        .transactionGeneration = OS_KERNEL_FILE_SYSTEM_FORMAT_INITIAL_GENERATION,
    };
}

FileSystemFormatStatus EncodeFileSystemSuperblock(const FileSystemSuperblock &superblock,
                                                  uint8_t *block,
                                                  const uint64_t blockSizeBytes) noexcept {
    if (block == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (blockSizeBytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    if (!SuperblockLayoutIsValid(superblock)) {
        return FileSystemFormatStatus::InvalidLayout;
    }
    if (superblock.transactionState != FileSystemTransactionState::Clean &&
        superblock.transactionState != FileSystemTransactionState::Dirty) {
        return FileSystemFormatStatus::InvalidTransactionState;
    }

    ClearBytes(block, blockSizeBytes);
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC_SIZE_BYTES; ++byteIndex) {
        block[byteIndex] = OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC[byteIndex];
    }
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION_OFFSET_BYTES,
                        superblock.version);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_BLOCK_SIZE_OFFSET_BYTES,
                        superblock.blockSizeBytes);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_TOTAL_BLOCK_COUNT_OFFSET_BYTES,
                        superblock.totalBlockCount);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_BITMAP_OFFSET_BYTES,
                        superblock.inodeBitmapRelativeBlock);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_START_OFFSET_BYTES,
                        superblock.inodeTableStartRelativeBlock);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_COUNT_OFFSET_BYTES,
                        superblock.inodeTableBlockCount);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BITMAP_OFFSET_BYTES,
                        superblock.dataBitmapRelativeBlock);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_START_OFFSET_BYTES,
                        superblock.dataStartRelativeBlock);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_COUNT_OFFSET_BYTES,
                        superblock.inodeCount);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BLOCK_COUNT_OFFSET_BYTES,
                        superblock.dataBlockCount);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_ROOT_INODE_OFFSET_BYTES,
                        superblock.rootInodeNumber);
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_STATE_OFFSET_BYTES,
                        static_cast<uint64_t>(superblock.transactionState));
    StoreLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_GENERATION_OFFSET_BYTES,
                        superblock.transactionGeneration);
    const uint32_t checksum =
        CalculateFileSystemCrc32(block,
                                 OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    StoreLittleEndian32(block + OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES,
                        checksum);
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus DecodeFileSystemSuperblock(const uint8_t *block,
                                                  const uint64_t blockSizeBytes,
                                                  FileSystemSuperblock &superblock) noexcept {
    if (block == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (blockSizeBytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC_SIZE_BYTES; ++byteIndex) {
        if (block[byteIndex] != OS_KERNEL_FILE_SYSTEM_FORMAT_MAGIC[byteIndex]) {
            return FileSystemFormatStatus::InvalidMagic;
        }
    }
    const uint32_t storedChecksum =
        LoadLittleEndian32(block + OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    const uint32_t calculatedChecksum =
        CalculateFileSystemCrc32(block,
                                 OS_KERNEL_FILE_SYSTEM_FORMAT_SUPERBLOCK_CHECKSUM_OFFSET_BYTES);
    if (storedChecksum != calculatedChecksum) {
        return FileSystemFormatStatus::InvalidChecksum;
    }

    FileSystemSuperblock decoded{
        .version =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION_OFFSET_BYTES),
        .blockSizeBytes =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_BLOCK_SIZE_OFFSET_BYTES),
        .totalBlockCount =
            LoadLittleEndian64(block +
                               OS_KERNEL_FILE_SYSTEM_FORMAT_TOTAL_BLOCK_COUNT_OFFSET_BYTES),
        .inodeBitmapRelativeBlock =
            LoadLittleEndian64(block +
                               OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_BITMAP_OFFSET_BYTES),
        .inodeTableStartRelativeBlock =
            LoadLittleEndian64(block +
                               OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_START_OFFSET_BYTES),
        .inodeTableBlockCount =
            LoadLittleEndian64(block +
                               OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TABLE_COUNT_OFFSET_BYTES),
        .dataBitmapRelativeBlock =
            LoadLittleEndian64(block +
                               OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BITMAP_OFFSET_BYTES),
        .dataStartRelativeBlock =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_START_OFFSET_BYTES),
        .inodeCount =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_COUNT_OFFSET_BYTES),
        .dataBlockCount =
            LoadLittleEndian64(block +
                               OS_KERNEL_FILE_SYSTEM_FORMAT_DATA_BLOCK_COUNT_OFFSET_BYTES),
        .rootInodeNumber =
            LoadLittleEndian64(block + OS_KERNEL_FILE_SYSTEM_FORMAT_ROOT_INODE_OFFSET_BYTES),
        .transactionState = static_cast<FileSystemTransactionState>(
            LoadLittleEndian64(block +
                               OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_STATE_OFFSET_BYTES)),
        .transactionGeneration =
            LoadLittleEndian64(block +
                               OS_KERNEL_FILE_SYSTEM_FORMAT_TRANSACTION_GENERATION_OFFSET_BYTES),
    };
    if (decoded.version != OS_KERNEL_FILE_SYSTEM_FORMAT_VERSION) {
        return FileSystemFormatStatus::InvalidVersion;
    }
    if (!SuperblockLayoutIsValid(decoded)) {
        return FileSystemFormatStatus::InvalidLayout;
    }
    if (decoded.transactionState != FileSystemTransactionState::Clean &&
        decoded.transactionState != FileSystemTransactionState::Dirty) {
        return FileSystemFormatStatus::InvalidTransactionState;
    }
    superblock = decoded;
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus EncodeFileSystemInode(const FileSystemInode &inode, uint8_t *bytes,
                                             const uint64_t byteCount) noexcept {
    if (bytes == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (byteCount != OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    if (!NodeTypeIsValid(inode.type, true) ||
        inode.allocatedBlockCount > OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT ||
        inode.sizeBytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES ||
        (inode.type == FileSystemNodeType::Unused &&
         (inode.sizeBytes != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          inode.allocatedBlockCount != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64))) {
        return FileSystemFormatStatus::InvalidInode;
    }

    ClearBytes(bytes, byteCount);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TYPE_OFFSET_BYTES,
                        static_cast<uint64_t>(inode.type));
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_SIZE_OFFSET_BYTES,
                        inode.sizeBytes);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_GENERATION_OFFSET_BYTES,
                        inode.generation);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_LINK_COUNT_OFFSET_BYTES,
                        inode.linkCount);
    StoreLittleEndian64(
        bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_ALLOCATED_BLOCK_COUNT_OFFSET_BYTES,
        inode.allocatedBlockCount);
    for (uint64_t blockIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         blockIndex < OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT; ++blockIndex) {
        StoreLittleEndian64(
            bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_DIRECT_BLOCKS_OFFSET_BYTES +
                blockIndex * OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES,
            inode.directBlocks[blockIndex]);
    }
    StoreLittleEndian32(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_RESERVED_OFFSET_BYTES,
                        OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT32);
    const uint32_t checksum =
        CalculateFileSystemCrc32(bytes, OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    StoreLittleEndian32(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES,
                        checksum);
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus DecodeFileSystemInode(const uint8_t *bytes, const uint64_t byteCount,
                                             FileSystemInode &inode) noexcept {
    if (bytes == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (byteCount != OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    const uint32_t storedChecksum =
        LoadLittleEndian32(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    const uint32_t calculatedChecksum =
        CalculateFileSystemCrc32(bytes, OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_CHECKSUM_OFFSET_BYTES);
    if (storedChecksum != calculatedChecksum) {
        return FileSystemFormatStatus::InvalidChecksum;
    }

    FileSystemInode decoded{
        .type = static_cast<FileSystemNodeType>(
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_TYPE_OFFSET_BYTES)),
        .sizeBytes =
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_SIZE_OFFSET_BYTES),
        .generation =
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_GENERATION_OFFSET_BYTES),
        .linkCount =
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_LINK_COUNT_OFFSET_BYTES),
        .allocatedBlockCount = LoadLittleEndian64(
            bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_ALLOCATED_BLOCK_COUNT_OFFSET_BYTES),
        .directBlocks = {},
    };
    for (uint64_t blockIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         blockIndex < OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT; ++blockIndex) {
        decoded.directBlocks[blockIndex] = LoadLittleEndian64(
            bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_INODE_DIRECT_BLOCKS_OFFSET_BYTES +
            blockIndex * OS_KERNEL_FILE_SYSTEM_FORMAT_UINT64_SIZE_BYTES);
    }
    if (!NodeTypeIsValid(decoded.type, true) ||
        decoded.allocatedBlockCount > OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT ||
        decoded.sizeBytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES ||
        decoded.sizeBytes >
            decoded.allocatedBlockCount * OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES ||
        (decoded.type == FileSystemNodeType::Unused &&
         (decoded.sizeBytes != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          decoded.allocatedBlockCount != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64)) ||
        (decoded.type != FileSystemNodeType::Unused &&
         decoded.linkCount < OS_KERNEL_FILE_SYSTEM_FORMAT_ROOT_LINK_COUNT)) {
        return FileSystemFormatStatus::InvalidInode;
    }
    inode = decoded;
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus
EncodeFileSystemDirectoryEntry(const FileSystemDirectoryEntry &entry, uint8_t *bytes,
                               const uint64_t byteCount) noexcept {
    if (bytes == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (byteCount != OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    if (!NodeTypeIsValid(entry.type, true) ||
        entry.nameLengthBytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES ||
        (entry.type == FileSystemNodeType::Unused &&
         (entry.inodeNumber != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          entry.nameLengthBytes != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64)) ||
        (entry.type != FileSystemNodeType::Unused &&
         (entry.inodeNumber == OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          entry.nameLengthBytes == OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64))) {
        return FileSystemFormatStatus::InvalidDirectoryEntry;
    }
    ClearBytes(bytes, byteCount);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_INODE_OFFSET_BYTES,
                        entry.inodeNumber);
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_TYPE_OFFSET_BYTES,
                        static_cast<uint64_t>(entry.type));
    StoreLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_LENGTH_OFFSET_BYTES,
                        entry.nameLengthBytes);
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < entry.nameLengthBytes; ++byteIndex) {
        bytes[OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_OFFSET_BYTES + byteIndex] =
            entry.name[byteIndex];
    }
    return FileSystemFormatStatus::Succeeded;
}

FileSystemFormatStatus
DecodeFileSystemDirectoryEntry(const uint8_t *bytes, const uint64_t byteCount,
                               FileSystemDirectoryEntry &entry) noexcept {
    if (bytes == nullptr) {
        return FileSystemFormatStatus::NullBuffer;
    }
    if (byteCount != OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) {
        return FileSystemFormatStatus::InvalidBufferSize;
    }
    FileSystemDirectoryEntry decoded{
        .inodeNumber =
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_INODE_OFFSET_BYTES),
        .type = static_cast<FileSystemNodeType>(
            LoadLittleEndian64(bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_TYPE_OFFSET_BYTES)),
        .nameLengthBytes = LoadLittleEndian64(
            bytes + OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_LENGTH_OFFSET_BYTES),
        .name = {},
    };
    if (!NodeTypeIsValid(decoded.type, true) ||
        decoded.nameLengthBytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES ||
        (decoded.type == FileSystemNodeType::Unused &&
         (decoded.inodeNumber != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          decoded.nameLengthBytes != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64)) ||
        (decoded.type != FileSystemNodeType::Unused &&
         (decoded.inodeNumber == OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64 ||
          decoded.nameLengthBytes == OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_UINT64))) {
        return FileSystemFormatStatus::InvalidDirectoryEntry;
    }
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FORMAT_FIRST_BYTE_INDEX;
         byteIndex < OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES; ++byteIndex) {
        decoded.name[byteIndex] =
            bytes[OS_KERNEL_FILE_SYSTEM_FORMAT_DIRECTORY_NAME_OFFSET_BYTES + byteIndex];
        if (byteIndex >= decoded.nameLengthBytes &&
            decoded.name[byteIndex] != OS_KERNEL_FILE_SYSTEM_FORMAT_ZERO_BYTE) {
            return FileSystemFormatStatus::InvalidDirectoryEntry;
        }
    }
    entry = decoded;
    return FileSystemFormatStatus::Succeeded;
}

}
