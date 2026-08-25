#include <os/kernel/fs/root_directory_index.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint8_t OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_MAGIC[] = {'O', 'S', 'D', 'R',
                                                                 'V', '0', '0', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAGIC[] = {'O', 'S', 'D', 'X',
                                                                 'V', '0', '0', '1'};
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_HEADER_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_GENERATION_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_GENERATION_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_COUNT_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_USED_SIZE_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_UUID_LOW_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_UUID_HIGH_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_RESERVED_START_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRIES_START_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_INODE_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_GENERATION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HASH_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_RECORD_LENGTH_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_NAME_LENGTH_OFFSET_BYTES = 28ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_TYPE_OFFSET_BYTES = 30ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_DEPTH_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_COUNT_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_UUID_LOW_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_UUID_HIGH_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_RESERVED_START_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRIES_START_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_HASH_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_CHILD_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_CHILD_GENERATION_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_COVERED_COUNT_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_RESERVED_TAIL_OFFSET_BYTES = 4064ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_RECORD_ALIGNMENT_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_FNV_PRIME = 1099511628211ULL;

void ClearBytes(uint8_t *const bytes, const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < byte_count; ++byte_index) {
        bytes[byte_index] = 0U;
    }
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < byte_count; ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < byte_count; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

void WriteU16(uint8_t *const bytes, const uint64_t offset, const uint16_t value) noexcept {
    bytes[offset] = static_cast<uint8_t>(value & 0xFFU);
    bytes[offset + 1ULL] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void WriteU32(uint8_t *const bytes, const uint64_t offset, const uint32_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        bytes[offset + byte_index] = static_cast<uint8_t>((value >> (byte_index * 8ULL)) & 0xFFU);
    }
}

void WriteU64(uint8_t *const bytes, const uint64_t offset, const uint64_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        bytes[offset + byte_index] = static_cast<uint8_t>((value >> (byte_index * 8ULL)) & 0xFFULL);
    }
}

[[nodiscard]] uint16_t ReadU16(const uint8_t *const bytes, const uint64_t offset) noexcept {
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1ULL]) << 8U);
}

[[nodiscard]] uint32_t ReadU32(const uint8_t *const bytes, const uint64_t offset) noexcept {
    uint32_t value = 0U;
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        value |= static_cast<uint32_t>(bytes[offset + byte_index]) << (byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] uint64_t ReadU64(const uint8_t *const bytes, const uint64_t offset) noexcept {
    uint64_t value = 0ULL;
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        value |= static_cast<uint64_t>(bytes[offset + byte_index]) << (byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] uint64_t AlignedRecordLength(const uint64_t name_length) noexcept {
    const uint64_t unaligned = OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES + name_length;
    return (unaligned + OS_KERNEL_ROOTFS_V5_DIRECTORY_RECORD_ALIGNMENT_BYTES - 1ULL) /
           OS_KERNEL_ROOTFS_V5_DIRECTORY_RECORD_ALIGNMENT_BYTES *
           OS_KERNEL_ROOTFS_V5_DIRECTORY_RECORD_ALIGNMENT_BYTES;
}

[[nodiscard]] bool NameValid(const uint8_t *const name, const uint64_t name_length) noexcept {
    if (name == nullptr || name_length == 0ULL ||
        name_length > OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t byte_index = 0ULL; byte_index < name_length; ++byte_index) {
        if (name[byte_index] == 0U || name[byte_index] == static_cast<uint8_t>('/')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool TypeValid(const RootV5NodeType type) noexcept {
    return type == RootV5NodeType::RegularFile || type == RootV5NodeType::Directory ||
           type == RootV5NodeType::SymbolicLink;
}

[[nodiscard]] int CompareNames(const RootDirectoryEntryV2 &left,
                               const RootDirectoryEntryV2 &right) noexcept {
    if (left.name_hash < right.name_hash) {
        return -1;
    }
    if (left.name_hash > right.name_hash) {
        return 1;
    }
    const uint64_t common = left.name_length_bytes < right.name_length_bytes
                                ? left.name_length_bytes
                                : right.name_length_bytes;
    for (uint64_t byte_index = 0ULL; byte_index < common; ++byte_index) {
        if (left.name[byte_index] < right.name[byte_index]) {
            return -1;
        }
        if (left.name[byte_index] > right.name[byte_index]) {
            return 1;
        }
    }
    return left.name_length_bytes < right.name_length_bytes   ? -1
           : left.name_length_bytes > right.name_length_bytes ? 1
                                                              : 0;
}

[[nodiscard]] bool EntryValid(const RootDirectoryEntryV2 &entry, const RootV5Uuid uuid) noexcept {
    return entry.inode_number != 0ULL && entry.inode_generation != 0ULL && TypeValid(entry.type) &&
           NameValid(entry.name, entry.name_length_bytes) &&
           entry.name_hash ==
               CalculateRootDirectoryNameHash(uuid, entry.name, entry.name_length_bytes) &&
           RootV5BytesAreZero(entry.name + entry.name_length_bytes,
                              OS_KERNEL_ROOTFS_V5_DIRECTORY_NAME_STORAGE_SIZE_BYTES -
                                  entry.name_length_bytes);
}

}

uint64_t CalculateRootDirectoryNameHash(const RootV5Uuid file_system_uuid,
                                        const uint8_t *const name,
                                        const uint64_t name_length_bytes) noexcept {
    if (!NameValid(name, name_length_bytes)) {
        return 0ULL;
    }
    uint64_t hash = OS_KERNEL_ROOTFS_V5_DIRECTORY_FNV_OFFSET_BASIS ^ file_system_uuid.low;
    hash ^= file_system_uuid.high;
    hash *= OS_KERNEL_ROOTFS_V5_DIRECTORY_FNV_PRIME;
    for (uint64_t byte_index = 0ULL; byte_index < name_length_bytes; ++byte_index) {
        hash ^= name[byte_index];
        hash *= OS_KERNEL_ROOTFS_V5_DIRECTORY_FNV_PRIME;
    }
    return hash == 0ULL ? 1ULL : hash;
}

RootDirectoryStatus EncodeRootDirectoryBlock(const RootDirectoryBlock &directory,
                                             uint8_t *const block,
                                             const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootDirectoryStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootDirectoryStatus::InvalidBufferSize;
    }
    if (directory.directory_inode_number == 0ULL || directory.directory_inode_generation == 0ULL ||
        directory.block_generation == 0ULL ||
        directory.entry_count > OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_ENTRY_COUNT ||
        (directory.file_system_uuid.low == 0ULL && directory.file_system_uuid.high == 0ULL)) {
        return RootDirectoryStatus::InvalidArgument;
    }
    ClearBytes(block, block_size_bytes);
    CopyBytes(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_MAGIC,
              OS_KERNEL_ROOTFS_V5_DIRECTORY_MAGIC_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_VERSION_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_DIRECTORY_FORMAT_VERSION);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_HEADER_SIZE_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_OFFSET_BYTES,
             directory.directory_inode_number);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_GENERATION_OFFSET_BYTES,
             directory.directory_inode_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_GENERATION_OFFSET_BYTES,
             directory.block_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_COUNT_OFFSET_BYTES, directory.entry_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_UUID_LOW_OFFSET_BYTES,
             directory.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_UUID_HIGH_OFFSET_BYTES,
             directory.file_system_uuid.high);
    uint64_t cursor = OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRIES_START_BYTES;
    for (uint64_t entry_index = 0ULL; entry_index < directory.entry_count; ++entry_index) {
        const RootDirectoryEntryV2 &entry = directory.entries[entry_index];
        if (!EntryValid(entry, directory.file_system_uuid)) {
            return RootDirectoryStatus::InvalidEntry;
        }
        const uint64_t record_length = AlignedRecordLength(entry.name_length_bytes);
        if (cursor > OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES - record_length) {
            return RootDirectoryStatus::CapacityExhausted;
        }
        WriteU64(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_INODE_OFFSET_BYTES,
                 entry.inode_number);
        WriteU64(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_GENERATION_OFFSET_BYTES,
                 entry.inode_generation);
        WriteU64(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HASH_OFFSET_BYTES,
                 entry.name_hash);
        WriteU32(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_RECORD_LENGTH_OFFSET_BYTES,
                 static_cast<uint32_t>(record_length));
        WriteU16(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_NAME_LENGTH_OFFSET_BYTES,
                 static_cast<uint16_t>(entry.name_length_bytes));
        WriteU16(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_TYPE_OFFSET_BYTES,
                 static_cast<uint16_t>(entry.type));
        CopyBytes(block + cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES,
                  entry.name, entry.name_length_bytes);
        cursor += record_length;
    }
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_USED_SIZE_OFFSET_BYTES, cursor);
    WriteU32(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES,
             CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES));
    return RootDirectoryStatus::Succeeded;
}

RootDirectoryStatus DecodeRootDirectoryBlock(const uint8_t *const block,
                                             const uint64_t block_size_bytes,
                                             RootDirectoryBlock &directory) noexcept {
    ClearBytes(reinterpret_cast<uint8_t *>(&directory), sizeof(directory));
    if (block == nullptr) {
        return RootDirectoryStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootDirectoryStatus::InvalidBufferSize;
    }
    if (!BytesEqual(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_MAGIC,
                    OS_KERNEL_ROOTFS_V5_DIRECTORY_MAGIC_SIZE_BYTES)) {
        return RootDirectoryStatus::InvalidMagic;
    }
    if (ReadU32(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES) !=
        CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES)) {
        return RootDirectoryStatus::InvalidChecksum;
    }
    if (ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_VERSION_OFFSET_BYTES) !=
        OS_KERNEL_ROOTFS_V5_DIRECTORY_FORMAT_VERSION) {
        return RootDirectoryStatus::InvalidVersion;
    }
    if (ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_HEADER_SIZE_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_HEADER_SIZE_BYTES ||
        !RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_DIRECTORY_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRIES_START_BYTES -
                                OS_KERNEL_ROOTFS_V5_DIRECTORY_RESERVED_START_BYTES)) {
        return RootDirectoryStatus::NonZeroReservedBytes;
    }
    directory.directory_inode_number =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_OFFSET_BYTES);
    directory.directory_inode_generation =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_GENERATION_OFFSET_BYTES);
    directory.block_generation =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_GENERATION_OFFSET_BYTES);
    directory.entry_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_COUNT_OFFSET_BYTES);
    const uint64_t used_size = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_USED_SIZE_OFFSET_BYTES);
    directory.file_system_uuid = RootV5Uuid{
        .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_UUID_LOW_OFFSET_BYTES),
        .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_UUID_HIGH_OFFSET_BYTES),
    };
    if (directory.directory_inode_number == 0ULL || directory.directory_inode_generation == 0ULL ||
        directory.block_generation == 0ULL ||
        directory.entry_count > OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_ENTRY_COUNT ||
        used_size < OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRIES_START_BYTES ||
        used_size > OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES) {
        return RootDirectoryStatus::InvalidArgument;
    }
    uint64_t cursor = OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRIES_START_BYTES;
    for (uint64_t entry_index = 0ULL; entry_index < directory.entry_count; ++entry_index) {
        if (cursor > used_size - OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES) {
            return RootDirectoryStatus::InvalidEntry;
        }
        const uint64_t record_length =
            ReadU32(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_RECORD_LENGTH_OFFSET_BYTES);
        const uint64_t name_length =
            ReadU16(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_NAME_LENGTH_OFFSET_BYTES);
        if (name_length == 0ULL ||
            name_length > OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_NAME_LENGTH_BYTES ||
            record_length != AlignedRecordLength(name_length) ||
            record_length > used_size - cursor) {
            return RootDirectoryStatus::InvalidEntry;
        }
        RootDirectoryEntryV2 &entry = directory.entries[entry_index];
        entry.inode_number =
            ReadU64(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_INODE_OFFSET_BYTES);
        entry.inode_generation =
            ReadU64(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_GENERATION_OFFSET_BYTES);
        entry.name_hash =
            ReadU64(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HASH_OFFSET_BYTES);
        entry.type = static_cast<RootV5NodeType>(
            ReadU16(block, cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_TYPE_OFFSET_BYTES));
        entry.name_length_bytes = name_length;
        CopyBytes(entry.name,
                  block + cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES,
                  name_length);
        if (!EntryValid(entry, directory.file_system_uuid) ||
            !RootV5BytesAreZero(
                block + cursor + OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES +
                    name_length,
                record_length - OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES -
                    name_length)) {
            return RootDirectoryStatus::InvalidEntry;
        }
        cursor += record_length;
    }
    if (cursor != used_size ||
        !RootV5BytesAreZero(block + used_size,
                            OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES - used_size)) {
        return RootDirectoryStatus::NonZeroReservedBytes;
    }
    return RootDirectoryStatus::Succeeded;
}

RootDirectoryStatus EncodeRootDirectoryIndexNode(const RootDirectoryIndexNode &node,
                                                 uint8_t *const block,
                                                 const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootDirectoryStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootDirectoryStatus::InvalidBufferSize;
    }
    if (node.directory_inode_number == 0ULL || node.directory_inode_generation == 0ULL ||
        node.tree_generation == 0ULL || node.depth > 3ULL || node.entry_count == 0ULL ||
        node.entry_count > OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_CAPACITY ||
        (node.file_system_uuid.low == 0ULL && node.file_system_uuid.high == 0ULL)) {
        return RootDirectoryStatus::InvalidIndex;
    }
    for (uint64_t entry_index = 0ULL; entry_index < node.entry_count; ++entry_index) {
        const RootDirectoryIndexEntry &entry = node.entries[entry_index];
        if (entry.child_relative_block == 0ULL || entry.child_generation == 0ULL ||
            entry.covered_entry_count == 0ULL ||
            (entry_index != 0ULL &&
             entry.minimum_hash <= node.entries[entry_index - 1ULL].minimum_hash)) {
            return RootDirectoryStatus::InvalidIndex;
        }
    }
    ClearBytes(block, block_size_bytes);
    CopyBytes(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAGIC,
              OS_KERNEL_ROOTFS_V5_DIRECTORY_MAGIC_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_VERSION_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_DIRECTORY_FORMAT_VERSION);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_HEADER_SIZE_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_OFFSET_BYTES, node.directory_inode_number);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_GENERATION_OFFSET_BYTES,
             node.directory_inode_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_GENERATION_OFFSET_BYTES, node.tree_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_DEPTH_OFFSET_BYTES, node.depth);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_COUNT_OFFSET_BYTES, node.entry_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_UUID_LOW_OFFSET_BYTES,
             node.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_UUID_HIGH_OFFSET_BYTES,
             node.file_system_uuid.high);
    for (uint64_t entry_index = 0ULL; entry_index < node.entry_count; ++entry_index) {
        const uint64_t offset = OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRIES_START_BYTES +
                                entry_index * OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_SIZE_BYTES;
        const RootDirectoryIndexEntry &entry = node.entries[entry_index];
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_HASH_OFFSET_BYTES,
                 entry.minimum_hash);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_CHILD_OFFSET_BYTES,
                 entry.child_relative_block);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_CHILD_GENERATION_OFFSET_BYTES,
                 entry.child_generation);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_COVERED_COUNT_OFFSET_BYTES,
                 entry.covered_entry_count);
    }
    WriteU32(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES,
             CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES));
    return RootDirectoryStatus::Succeeded;
}

RootDirectoryStatus DecodeRootDirectoryIndexNode(const uint8_t *const block,
                                                 const uint64_t block_size_bytes,
                                                 RootDirectoryIndexNode &node) noexcept {
    node = RootDirectoryIndexNode{};
    if (block == nullptr) {
        return RootDirectoryStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootDirectoryStatus::InvalidBufferSize;
    }
    if (!BytesEqual(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAGIC,
                    OS_KERNEL_ROOTFS_V5_DIRECTORY_MAGIC_SIZE_BYTES)) {
        return RootDirectoryStatus::InvalidMagic;
    }
    if (ReadU32(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES) !=
        CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES)) {
        return RootDirectoryStatus::InvalidChecksum;
    }
    node.directory_inode_number = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_OFFSET_BYTES);
    node.directory_inode_generation =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INODE_GENERATION_OFFSET_BYTES);
    node.tree_generation = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_GENERATION_OFFSET_BYTES);
    node.depth = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_DEPTH_OFFSET_BYTES);
    node.entry_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_COUNT_OFFSET_BYTES);
    node.file_system_uuid = RootV5Uuid{
        .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_UUID_LOW_OFFSET_BYTES),
        .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_UUID_HIGH_OFFSET_BYTES),
    };
    if (ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_VERSION_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_DIRECTORY_FORMAT_VERSION ||
        ReadU64(block, OS_KERNEL_ROOTFS_V5_DIRECTORY_HEADER_SIZE_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_HEADER_SIZE_BYTES ||
        !RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRIES_START_BYTES -
                                OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_RESERVED_START_BYTES)) {
        return RootDirectoryStatus::NonZeroReservedBytes;
    }
    for (uint64_t entry_index = 0ULL;
         entry_index < OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_CAPACITY; ++entry_index) {
        const uint64_t offset = OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRIES_START_BYTES +
                                entry_index * OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_SIZE_BYTES;
        node.entries[entry_index] = RootDirectoryIndexEntry{
            .minimum_hash =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_HASH_OFFSET_BYTES),
            .child_relative_block =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_CHILD_OFFSET_BYTES),
            .child_generation = ReadU64(
                block, offset + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_CHILD_GENERATION_OFFSET_BYTES),
            .covered_entry_count = ReadU64(
                block, offset + OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_COVERED_COUNT_OFFSET_BYTES),
        };
    }
    if (node.directory_inode_number == 0ULL || node.directory_inode_generation == 0ULL ||
        node.tree_generation == 0ULL || node.depth > 3ULL || node.entry_count == 0ULL ||
        node.entry_count > OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_CAPACITY ||
        (node.file_system_uuid.low == 0ULL && node.file_system_uuid.high == 0ULL) ||
        !RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_DIRECTORY_RESERVED_TAIL_OFFSET_BYTES,
                            OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_DIRECTORY_RESERVED_TAIL_OFFSET_BYTES)) {
        return RootDirectoryStatus::InvalidIndex;
    }
    for (uint64_t entry_index = 0ULL;
         entry_index < OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_CAPACITY; ++entry_index) {
        const RootDirectoryIndexEntry &entry = node.entries[entry_index];
        if (entry_index >= node.entry_count) {
            if (entry.minimum_hash != 0ULL || entry.child_relative_block != 0ULL ||
                entry.child_generation != 0ULL || entry.covered_entry_count != 0ULL) {
                return RootDirectoryStatus::NonZeroReservedBytes;
            }
            continue;
        }
        if (entry.child_relative_block == 0ULL || entry.child_generation == 0ULL ||
            entry.covered_entry_count == 0ULL ||
            (entry_index != 0ULL &&
             entry.minimum_hash <= node.entries[entry_index - 1ULL].minimum_hash)) {
            return RootDirectoryStatus::InvalidIndex;
        }
    }
    return RootDirectoryStatus::Succeeded;
}

RootDirectoryStatus RootDirectoryIndex::Initialize(const uint64_t directory_inode_number,
                                                   const uint64_t directory_inode_generation,
                                                   const RootV5Uuid file_system_uuid) noexcept {
    if (this->initialized_ || directory_inode_number == 0ULL ||
        directory_inode_generation == 0ULL ||
        (file_system_uuid.low == 0ULL && file_system_uuid.high == 0ULL)) {
        return RootDirectoryStatus::InvalidArgument;
    }
    this->directory_inode_number_ = directory_inode_number;
    this->directory_inode_generation_ = directory_inode_generation;
    this->file_system_uuid_ = file_system_uuid;
    this->statistics_ = RootDirectoryIndexStatistics{};
    this->tree_generation_ = 1ULL;
    this->initialized_ = true;
    return RootDirectoryStatus::Succeeded;
}

bool RootDirectoryIndex::NameEqual(const RootDirectoryEntryV2 &entry, const uint8_t *const name,
                                   const uint64_t name_length_bytes) const noexcept {
    return entry.name_length_bytes == name_length_bytes &&
           BytesEqual(entry.name, name, name_length_bytes);
}

RootDirectoryStatus RootDirectoryIndex::Rebuild() noexcept {
    const uint64_t old_nodes = this->node_count_;
    const uint64_t old_depth = this->depth_;
    for (uint64_t node_index = 0ULL;
         node_index < OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAXIMUM_NODE_COUNT; ++node_index) {
        this->nodes_[node_index] = IndexNode{};
    }
    this->node_count_ = 0ULL;
    this->depth_ = 0ULL;
    if (this->entry_count_ == 0ULL) {
        return RootDirectoryStatus::Succeeded;
    }
    uint64_t level_start = 0ULL;
    uint64_t level_count = 0ULL;
    for (uint64_t entry_index = 0ULL; entry_index < this->entry_count_;) {
        IndexNode &leaf = this->nodes_[this->node_count_];
        leaf.depth = 0ULL;
        leaf.occupied = true;
        for (uint64_t slot = 0ULL;
             slot < OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_FANOUT && entry_index < this->entry_count_;
             ++slot, ++entry_index) {
            leaf.entries[slot] = RootDirectoryIndexEntry{
                .minimum_hash = this->entries_[entry_index].name_hash,
                .child_relative_block = entry_index,
                .child_generation = this->tree_generation_,
                .covered_entry_count = 1ULL,
            };
            ++leaf.entry_count;
        }
        ++this->node_count_;
        ++level_count;
    }
    while (level_count > 1ULL) {
        const uint64_t next_start = this->node_count_;
        uint64_t next_count = 0ULL;
        for (uint64_t child_offset = 0ULL; child_offset < level_count;) {
            IndexNode &parent = this->nodes_[this->node_count_];
            parent.depth = this->nodes_[level_start + child_offset].depth + 1ULL;
            parent.occupied = true;
            for (uint64_t slot = 0ULL;
                 slot < OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_FANOUT && child_offset < level_count;
                 ++slot, ++child_offset) {
                const uint64_t child_index = level_start + child_offset;
                const IndexNode &child = this->nodes_[child_index];
                uint64_t covered = 0ULL;
                for (uint64_t child_slot = 0ULL; child_slot < child.entry_count; ++child_slot) {
                    covered += child.entries[child_slot].covered_entry_count;
                }
                parent.entries[slot] = RootDirectoryIndexEntry{
                    .minimum_hash = child.entries[0].minimum_hash,
                    .child_relative_block = child_index,
                    .child_generation = this->tree_generation_,
                    .covered_entry_count = covered,
                };
                ++parent.entry_count;
            }
            ++this->node_count_;
            ++next_count;
        }
        level_start = next_start;
        level_count = next_count;
    }
    this->depth_ = this->nodes_[level_start].depth;
    if (this->node_count_ > old_nodes) {
        this->statistics_.split_count += this->node_count_ - old_nodes;
    } else if (this->node_count_ < old_nodes) {
        this->statistics_.merge_count += old_nodes - this->node_count_;
    }
    if (this->depth_ > old_depth) {
        ++this->statistics_.depth_growth_count;
    } else if (this->depth_ < old_depth) {
        ++this->statistics_.depth_shrink_count;
    }
    return RootDirectoryStatus::Succeeded;
}

RootDirectoryStatus RootDirectoryIndex::Insert(const uint8_t *const name,
                                               const uint64_t name_length_bytes,
                                               const uint64_t inode_number,
                                               const uint64_t inode_generation,
                                               const RootV5NodeType type) noexcept {
    if (!this->initialized_ || !NameValid(name, name_length_bytes) || inode_number == 0ULL ||
        inode_generation == 0ULL || !TypeValid(type) ||
        this->entry_count_ >= OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_ENTRY_COUNT ||
        this->tree_generation_ == UINT64_MAX) {
        return RootDirectoryStatus::InvalidArgument;
    }
    RootDirectoryEntryV2 candidate{
        .inode_number = inode_number,
        .inode_generation = inode_generation,
        .name_hash =
            CalculateRootDirectoryNameHash(this->file_system_uuid_, name, name_length_bytes),
        .type = type,
        .name_length_bytes = name_length_bytes,
        .name = {},
    };
    CopyBytes(candidate.name, name, name_length_bytes);
    uint64_t insert_index = 0ULL;
    while (insert_index < this->entry_count_ &&
           CompareNames(this->entries_[insert_index], candidate) < 0) {
        ++insert_index;
    }
    if (insert_index < this->entry_count_ &&
        CompareNames(this->entries_[insert_index], candidate) == 0) {
        return RootDirectoryStatus::DuplicateName;
    }
    for (uint64_t move_index = this->entry_count_; move_index > insert_index; --move_index) {
        this->entries_[move_index] = this->entries_[move_index - 1ULL];
    }
    this->entries_[insert_index] = candidate;
    ++this->entry_count_;
    ++this->tree_generation_;
    const RootDirectoryStatus status = this->Rebuild();
    if (status == RootDirectoryStatus::Succeeded) {
        ++this->statistics_.insert_count;
        this->statistics_.current_entry_count = this->entry_count_;
        this->statistics_.current_node_count = this->node_count_;
        this->statistics_.current_depth = this->depth_;
    }
    return status;
}

RootDirectoryStatus RootDirectoryIndex::Lookup(const uint8_t *const name,
                                               const uint64_t name_length_bytes,
                                               RootDirectoryEntryV2 &entry) noexcept {
    entry = RootDirectoryEntryV2{};
    if (!this->initialized_ || !NameValid(name, name_length_bytes)) {
        return RootDirectoryStatus::InvalidArgument;
    }
    const uint64_t hash =
        CalculateRootDirectoryNameHash(this->file_system_uuid_, name, name_length_bytes);
    ++this->statistics_.lookup_count;
    this->statistics_.last_lookup_node_count = 0ULL;
    if (this->node_count_ == 0ULL) {
        return RootDirectoryStatus::NotFound;
    }
    uint64_t node_index = this->node_count_ - 1ULL;
    while (this->nodes_[node_index].depth != 0ULL) {
        const IndexNode &node = this->nodes_[node_index];
        ++this->statistics_.last_lookup_node_count;
        uint64_t selected_slot = 0ULL;
        for (uint64_t slot = 1ULL; slot < node.entry_count; ++slot) {
            if (node.entries[slot].minimum_hash > hash) {
                break;
            }
            selected_slot = slot;
        }
        node_index = node.entries[selected_slot].child_relative_block;
        if (node_index >= this->node_count_) {
            return RootDirectoryStatus::InvalidIndex;
        }
    }
    while (node_index < this->node_count_ && this->nodes_[node_index].depth == 0ULL) {
        const IndexNode &leaf = this->nodes_[node_index];
        ++this->statistics_.last_lookup_node_count;
        bool leaf_may_continue_collision = false;
        for (uint64_t slot = 0ULL; slot < leaf.entry_count; ++slot) {
            const uint64_t entry_index = leaf.entries[slot].child_relative_block;
            if (entry_index >= this->entry_count_) {
                return RootDirectoryStatus::InvalidIndex;
            }
            const RootDirectoryEntryV2 &candidate = this->entries_[entry_index];
            if (candidate.name_hash == hash) {
                leaf_may_continue_collision = true;
                if (this->NameEqual(candidate, name, name_length_bytes)) {
                    entry = candidate;
                    if (this->statistics_.last_lookup_node_count >
                        this->statistics_.maximum_lookup_node_count) {
                        this->statistics_.maximum_lookup_node_count =
                            this->statistics_.last_lookup_node_count;
                    }
                    return RootDirectoryStatus::Succeeded;
                }
                ++this->statistics_.hash_collision_count;
            } else if (candidate.name_hash > hash) {
                leaf_may_continue_collision = false;
                break;
            }
        }
        if (!leaf_may_continue_collision ||
            leaf.entries[leaf.entry_count - 1ULL].minimum_hash != hash) {
            break;
        }
        ++node_index;
    }
    if (this->statistics_.last_lookup_node_count > this->statistics_.maximum_lookup_node_count) {
        this->statistics_.maximum_lookup_node_count = this->statistics_.last_lookup_node_count;
    }
    return RootDirectoryStatus::NotFound;
}

RootDirectoryStatus RootDirectoryIndex::Remove(const uint8_t *const name,
                                               const uint64_t name_length_bytes) noexcept {
    RootDirectoryEntryV2 ignored{};
    if (this->Lookup(name, name_length_bytes, ignored) != RootDirectoryStatus::Succeeded) {
        return RootDirectoryStatus::NotFound;
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->entry_count_; ++entry_index) {
        if (!this->NameEqual(this->entries_[entry_index], name, name_length_bytes)) {
            continue;
        }
        for (uint64_t move_index = entry_index + 1ULL; move_index < this->entry_count_;
             ++move_index) {
            this->entries_[move_index - 1ULL] = this->entries_[move_index];
        }
        --this->entry_count_;
        this->entries_[this->entry_count_] = RootDirectoryEntryV2{};
        ++this->tree_generation_;
        const RootDirectoryStatus status = this->Rebuild();
        if (status == RootDirectoryStatus::Succeeded) {
            ++this->statistics_.remove_count;
            this->statistics_.current_entry_count = this->entry_count_;
            this->statistics_.current_node_count = this->node_count_;
            this->statistics_.current_depth = this->depth_;
        }
        return status;
    }
    return RootDirectoryStatus::NotFound;
}

RootDirectoryStatus RootDirectoryIndex::EntryAt(const uint64_t entry_index,
                                                RootDirectoryEntryV2 &entry) const noexcept {
    if (entry_index >= this->entry_count_) {
        return RootDirectoryStatus::NotFound;
    }
    entry = this->entries_[entry_index];
    return RootDirectoryStatus::Succeeded;
}

RootDirectoryStatus RootDirectoryIndex::Validate() const noexcept {
    if (!this->initialized_ ||
        this->entry_count_ > OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_ENTRY_COUNT ||
        this->node_count_ > OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAXIMUM_NODE_COUNT ||
        this->depth_ > OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAXIMUM_DEPTH) {
        return RootDirectoryStatus::InvalidArgument;
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->entry_count_; ++entry_index) {
        if (!EntryValid(this->entries_[entry_index], this->file_system_uuid_) ||
            (entry_index != 0ULL &&
             CompareNames(this->entries_[entry_index - 1ULL], this->entries_[entry_index]) >= 0)) {
            return RootDirectoryStatus::InvalidEntry;
        }
    }
    if ((this->entry_count_ == 0ULL) != (this->node_count_ == 0ULL)) {
        return RootDirectoryStatus::InvalidIndex;
    }
    return RootDirectoryStatus::Succeeded;
}

uint64_t RootDirectoryIndex::EntryCount() const noexcept { return this->entry_count_; }

RootDirectoryIndexStatistics RootDirectoryIndex::Statistics() const noexcept {
    return this->statistics_;
}

}
