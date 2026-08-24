#include <os/kernel/fs/root_inode_metadata.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint8_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_MAGIC[] = {'O', 'S', 'I', 'E',
                                                                 'V', '0', '0', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_XATTR_MAGIC[] = {'O', 'S', 'X', 'A', 'V', '0', '0', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_QUOTA_MAGIC[] = {'O', 'S', 'Q', 'T', 'V', '0', '0', '1'};
constexpr uint64_t OS_KERNEL_ROOTFS_V5_METADATA_FORMAT_VERSION = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_METADATA_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_METADATA_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_METADATA_HEADER_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAGS_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_EXTENT_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_XATTR_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_DIRECTORY_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_PROJECT_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_ACL_GENERATION_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_QUOTA_GENERATION_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_RESERVED_START_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_INODE_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_INODE_GENERATION_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_BLOCK_GENERATION_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_COUNT_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_USED_SIZE_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_UUID_LOW_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_UUID_HIGH_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_RESERVED_START_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_ENTRIES_START_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_NAMESPACE_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_NAME_LENGTH_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_VALUE_SIZE_OFFSET_BYTES = 12ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_RECORD_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HASH_OFFSET_BYTES = 20ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_RECORD_ALIGNMENT_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_GENERATION_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_COUNT_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_UUID_LOW_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_UUID_HIGH_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RESERVED_START_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORDS_START_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_TYPE_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_IDENTIFIER_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_USED_BLOCKS_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_USED_INODES_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SOFT_BLOCKS_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_HARD_BLOCKS_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SOFT_INODES_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_HARD_INODES_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_GRACE_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_METADATA_FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_METADATA_FNV_PRIME = 1099511628211ULL;
constexpr os::abi::FileMode OS_KERNEL_ROOTFS_V5_ACL_PERMISSION_MASK = 0000007U;

void ClearBytes(uint8_t *const bytes, const uint64_t count) noexcept {
    for (uint64_t index = 0ULL; index < count; ++index) {
        bytes[index] = 0U;
    }
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t count) noexcept {
    for (uint64_t index = 0ULL; index < count; ++index) {
        destination[index] = source[index];
    }
}

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t count) noexcept {
    for (uint64_t index = 0ULL; index < count; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

void WriteU32(uint8_t *const bytes, const uint64_t offset, const uint32_t value) noexcept {
    for (uint64_t index = 0ULL; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8ULL)) & 0xFFU);
    }
}

void WriteU64(uint8_t *const bytes, const uint64_t offset, const uint64_t value) noexcept {
    for (uint64_t index = 0ULL; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8ULL)) & 0xFFULL);
    }
}

[[nodiscard]] uint32_t ReadU32(const uint8_t *const bytes, const uint64_t offset) noexcept {
    uint32_t value = 0U;
    for (uint64_t index = 0ULL; index < sizeof(value); ++index) {
        value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8ULL);
    }
    return value;
}

[[nodiscard]] uint64_t ReadU64(const uint8_t *const bytes, const uint64_t offset) noexcept {
    uint64_t value = 0ULL;
    for (uint64_t index = 0ULL; index < sizeof(value); ++index) {
        value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8ULL);
    }
    return value;
}

[[nodiscard]] bool NamespaceValid(const RootXattrNamespace name_space) noexcept {
    return name_space == RootXattrNamespace::User || name_space == RootXattrNamespace::System ||
           name_space == RootXattrNamespace::Security || name_space == RootXattrNamespace::Trusted;
}

[[nodiscard]] bool XattrNameValid(const uint8_t *const name, const uint64_t length) noexcept {
    if (name == nullptr || length == 0ULL ||
        length > OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t index = 0ULL; index < length; ++index) {
        if (name[index] == 0U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint64_t XattrHash(const RootXattrEntry &entry) noexcept {
    uint64_t hash =
        OS_KERNEL_ROOTFS_V5_METADATA_FNV_OFFSET ^ static_cast<uint64_t>(entry.name_space);
    for (uint64_t index = 0ULL; index < entry.name_length_bytes; ++index) {
        hash ^= entry.name[index];
        hash *= OS_KERNEL_ROOTFS_V5_METADATA_FNV_PRIME;
    }
    for (uint64_t index = 0ULL; index < entry.value_size_bytes; ++index) {
        hash ^= entry.value[index];
        hash *= OS_KERNEL_ROOTFS_V5_METADATA_FNV_PRIME;
    }
    return hash;
}

[[nodiscard]] int CompareXattr(const RootXattrEntry &left, const RootXattrEntry &right) noexcept {
    if (left.name_space != right.name_space) {
        return static_cast<uint64_t>(left.name_space) < static_cast<uint64_t>(right.name_space) ? -1
                                                                                                : 1;
    }
    const uint64_t common = left.name_length_bytes < right.name_length_bytes
                                ? left.name_length_bytes
                                : right.name_length_bytes;
    for (uint64_t index = 0ULL; index < common; ++index) {
        if (left.name[index] != right.name[index]) {
            return left.name[index] < right.name[index] ? -1 : 1;
        }
    }
    return left.name_length_bytes < right.name_length_bytes   ? -1
           : left.name_length_bytes > right.name_length_bytes ? 1
                                                              : 0;
}

[[nodiscard]] bool XattrEntryValid(const RootXattrEntry &entry) noexcept {
    return NamespaceValid(entry.name_space) &&
           XattrNameValid(entry.name, entry.name_length_bytes) &&
           entry.value_size_bytes <= OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_VALUE_SIZE_BYTES &&
           RootV5BytesAreZero(entry.name + entry.name_length_bytes,
                              OS_KERNEL_ROOTFS_V5_XATTR_NAME_STORAGE_SIZE_BYTES -
                                  entry.name_length_bytes) &&
           RootV5BytesAreZero(entry.value + entry.value_size_bytes,
                              OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_VALUE_SIZE_BYTES -
                                  entry.value_size_bytes);
}

[[nodiscard]] uint64_t AlignedXattrRecordSize(const RootXattrEntry &entry) noexcept {
    const uint64_t size = OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES +
                          entry.name_length_bytes + entry.value_size_bytes;
    return (size + OS_KERNEL_ROOTFS_V5_XATTR_RECORD_ALIGNMENT_BYTES - 1ULL) /
           OS_KERNEL_ROOTFS_V5_XATTR_RECORD_ALIGNMENT_BYTES *
           OS_KERNEL_ROOTFS_V5_XATTR_RECORD_ALIGNMENT_BYTES;
}

[[nodiscard]] bool QuotaTypeValid(const RootQuotaType type) noexcept {
    return type == RootQuotaType::User || type == RootQuotaType::Group;
}

[[nodiscard]] bool QuotaRecordValid(const RootQuotaRecord &record) noexcept {
    return QuotaTypeValid(record.type) &&
           (record.hard_block_limit == 0ULL ||
            record.soft_block_limit <= record.hard_block_limit) &&
           (record.hard_inode_limit == 0ULL ||
            record.soft_inode_limit <= record.hard_inode_limit) &&
           (record.hard_block_limit == 0ULL ||
            record.used_block_count <= record.hard_block_limit) &&
           (record.hard_inode_limit == 0ULL || record.used_inode_count <= record.hard_inode_limit);
}

[[nodiscard]] bool InodeExtensionValid(const RootInodeExtension &extension) noexcept {
    if ((extension.flags & ~OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_SUPPORTED_FLAGS) != 0ULL) {
        return false;
    }
    const bool extents_valid =
        (extension.flags & OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS) != 0ULL
            ? extension.extent_root_relative_block != 0ULL
            : extension.extent_root_relative_block == 0ULL;
    const bool directory_valid =
        (extension.flags & OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_DIRECTORY_INDEX) != 0ULL
            ? extension.directory_index_root_relative_block != 0ULL
            : extension.directory_index_root_relative_block == 0ULL;
    const bool xattr_valid =
        (extension.flags & OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_XATTR) != 0ULL
            ? extension.xattr_relative_block != 0ULL
            : extension.xattr_relative_block == 0ULL;
    const bool acl_valid = (extension.flags & OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_ACL) != 0ULL
                               ? extension.acl_generation != 0ULL
                               : extension.acl_generation == 0ULL;
    const bool quota_valid =
        (extension.flags & OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_QUOTA) != 0ULL
            ? extension.quota_generation != 0ULL
            : extension.quota_generation == 0ULL;
    return extents_valid && directory_valid && xattr_valid && acl_valid && quota_valid;
}

}

RootInodeMetadataStatus EncodeRootInodeExtension(const RootInodeExtension &extension,
                                                 uint8_t *const bytes,
                                                 const uint64_t byte_count) noexcept {
    if (bytes == nullptr) {
        return RootInodeMetadataStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_SIZE_BYTES) {
        return RootInodeMetadataStatus::InvalidBufferSize;
    }
    if (!InodeExtensionValid(extension)) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    ClearBytes(bytes, byte_count);
    CopyBytes(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_MAGIC,
              OS_KERNEL_ROOTFS_V5_METADATA_MAGIC_SIZE_BYTES);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_METADATA_VERSION_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_METADATA_FORMAT_VERSION);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_METADATA_HEADER_SIZE_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_SIZE_BYTES);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAGS_OFFSET_BYTES, extension.flags);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_EXTENT_OFFSET_BYTES,
             extension.extent_root_relative_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_XATTR_OFFSET_BYTES,
             extension.xattr_relative_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_DIRECTORY_OFFSET_BYTES,
             extension.directory_index_root_relative_block);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_PROJECT_OFFSET_BYTES,
             extension.project_identifier);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_ACL_GENERATION_OFFSET_BYTES,
             extension.acl_generation);
    WriteU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_QUOTA_GENERATION_OFFSET_BYTES,
             extension.quota_generation);
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus DecodeRootInodeExtension(const uint8_t *const bytes,
                                                 const uint64_t byte_count,
                                                 RootInodeExtension &extension) noexcept {
    extension = RootInodeExtension{};
    if (bytes == nullptr) {
        return RootInodeMetadataStatus::NullBuffer;
    }
    if (byte_count != OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_SIZE_BYTES) {
        return RootInodeMetadataStatus::InvalidBufferSize;
    }
    if (!BytesEqual(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_MAGIC,
                    OS_KERNEL_ROOTFS_V5_METADATA_MAGIC_SIZE_BYTES)) {
        return RootInodeMetadataStatus::InvalidMagic;
    }
    if (ReadU64(bytes, OS_KERNEL_ROOTFS_V5_METADATA_VERSION_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_METADATA_FORMAT_VERSION ||
        ReadU64(bytes, OS_KERNEL_ROOTFS_V5_METADATA_HEADER_SIZE_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_SIZE_BYTES ||
        !RootV5BytesAreZero(bytes + OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_RESERVED_START_BYTES,
                            byte_count -
                                OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_RESERVED_START_BYTES)) {
        return RootInodeMetadataStatus::NonZeroReservedBytes;
    }
    extension = RootInodeExtension{
        .flags = ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAGS_OFFSET_BYTES),
        .extent_root_relative_block =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_EXTENT_OFFSET_BYTES),
        .xattr_relative_block =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_XATTR_OFFSET_BYTES),
        .directory_index_root_relative_block =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_DIRECTORY_OFFSET_BYTES),
        .project_identifier =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_PROJECT_OFFSET_BYTES),
        .acl_generation =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_ACL_GENERATION_OFFSET_BYTES),
        .quota_generation =
            ReadU64(bytes, OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_QUOTA_GENERATION_OFFSET_BYTES),
    };
    return InodeExtensionValid(extension) ? RootInodeMetadataStatus::Succeeded
                                          : RootInodeMetadataStatus::InvalidArgument;
}

RootInodeMetadataStatus RootXattrSet::Initialize(const uint64_t inode_number,
                                                 const uint64_t inode_generation,
                                                 const RootV5Uuid file_system_uuid) noexcept {
    if (this->initialized_ || inode_number == 0ULL || inode_generation == 0ULL ||
        (file_system_uuid.low == 0ULL && file_system_uuid.high == 0ULL)) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    this->block_ = RootXattrBlock{
        .inode_number = inode_number,
        .inode_generation = inode_generation,
        .block_generation = 1ULL,
        .entry_count = 0ULL,
        .file_system_uuid = file_system_uuid,
        .entries = {},
    };
    this->initialized_ = true;
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus RootXattrSet::Set(const RootXattrNamespace name_space,
                                          const uint8_t *const name,
                                          const uint64_t name_length_bytes,
                                          const uint8_t *const value,
                                          const uint64_t value_size_bytes) noexcept {
    if (!this->initialized_ || !NamespaceValid(name_space) ||
        !XattrNameValid(name, name_length_bytes) ||
        (value == nullptr && value_size_bytes != 0ULL) ||
        value_size_bytes > OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_VALUE_SIZE_BYTES ||
        this->block_.block_generation == UINT64_MAX) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    RootXattrEntry candidate{
        .name_space = name_space,
        .name_length_bytes = name_length_bytes,
        .value_size_bytes = value_size_bytes,
        .name = {},
        .value = {},
    };
    CopyBytes(candidate.name, name, name_length_bytes);
    if (value_size_bytes != 0ULL) {
        CopyBytes(candidate.value, value, value_size_bytes);
    }
    uint64_t index = 0ULL;
    while (index < this->block_.entry_count &&
           CompareXattr(this->block_.entries[index], candidate) < 0) {
        ++index;
    }
    if (index < this->block_.entry_count &&
        CompareXattr(this->block_.entries[index], candidate) == 0) {
        this->block_.entries[index] = candidate;
        ++this->block_.block_generation;
        return RootInodeMetadataStatus::Succeeded;
    }
    if (this->block_.entry_count >= OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_ENTRY_COUNT) {
        return RootInodeMetadataStatus::CapacityExhausted;
    }
    for (uint64_t move = this->block_.entry_count; move > index; --move) {
        this->block_.entries[move] = this->block_.entries[move - 1ULL];
    }
    this->block_.entries[index] = candidate;
    ++this->block_.entry_count;
    ++this->block_.block_generation;
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus RootXattrSet::Get(const RootXattrNamespace name_space,
                                          const uint8_t *const name,
                                          const uint64_t name_length_bytes,
                                          RootXattrEntry &entry) const noexcept {
    entry = RootXattrEntry{};
    if (!this->initialized_ || !XattrNameValid(name, name_length_bytes)) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    for (uint64_t index = 0ULL; index < this->block_.entry_count; ++index) {
        const RootXattrEntry &candidate = this->block_.entries[index];
        if (candidate.name_space == name_space &&
            candidate.name_length_bytes == name_length_bytes &&
            BytesEqual(candidate.name, name, name_length_bytes)) {
            entry = candidate;
            return RootInodeMetadataStatus::Succeeded;
        }
    }
    return RootInodeMetadataStatus::NotFound;
}

RootInodeMetadataStatus RootXattrSet::Remove(const RootXattrNamespace name_space,
                                             const uint8_t *const name,
                                             const uint64_t name_length_bytes) noexcept {
    RootXattrEntry ignored{};
    if (this->Get(name_space, name, name_length_bytes, ignored) !=
        RootInodeMetadataStatus::Succeeded) {
        return RootInodeMetadataStatus::NotFound;
    }
    for (uint64_t index = 0ULL; index < this->block_.entry_count; ++index) {
        RootXattrEntry &candidate = this->block_.entries[index];
        if (candidate.name_space != name_space ||
            candidate.name_length_bytes != name_length_bytes ||
            !BytesEqual(candidate.name, name, name_length_bytes)) {
            continue;
        }
        for (uint64_t move = index + 1ULL; move < this->block_.entry_count; ++move) {
            this->block_.entries[move - 1ULL] = this->block_.entries[move];
        }
        --this->block_.entry_count;
        this->block_.entries[this->block_.entry_count] = RootXattrEntry{};
        ++this->block_.block_generation;
        return RootInodeMetadataStatus::Succeeded;
    }
    return RootInodeMetadataStatus::NotFound;
}

RootInodeMetadataStatus RootXattrSet::Export(RootXattrBlock &block) const noexcept {
    if (!this->initialized_) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    block = this->block_;
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus RootXattrSet::Validate() const noexcept {
    if (!this->initialized_ ||
        this->block_.entry_count > OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_ENTRY_COUNT) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    for (uint64_t index = 0ULL; index < this->block_.entry_count; ++index) {
        if (!XattrEntryValid(this->block_.entries[index]) ||
            (index != 0ULL &&
             CompareXattr(this->block_.entries[index - 1ULL], this->block_.entries[index]) >= 0)) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
    }
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus EncodeRootXattrBlock(const RootXattrBlock &xattr_block,
                                             uint8_t *const block,
                                             const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootInodeMetadataStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES ||
        xattr_block.inode_number == 0ULL || xattr_block.inode_generation == 0ULL ||
        xattr_block.block_generation == 0ULL ||
        xattr_block.entry_count > OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_ENTRY_COUNT ||
        (xattr_block.file_system_uuid.low == 0ULL && xattr_block.file_system_uuid.high == 0ULL)) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    ClearBytes(block, block_size_bytes);
    CopyBytes(block, OS_KERNEL_ROOTFS_V5_XATTR_MAGIC,
              OS_KERNEL_ROOTFS_V5_METADATA_MAGIC_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_METADATA_VERSION_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_METADATA_FORMAT_VERSION);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_METADATA_HEADER_SIZE_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_XATTR_BLOCK_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_XATTR_INODE_OFFSET_BYTES, xattr_block.inode_number);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_XATTR_INODE_GENERATION_OFFSET_BYTES,
             xattr_block.inode_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_XATTR_BLOCK_GENERATION_OFFSET_BYTES,
             xattr_block.block_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_COUNT_OFFSET_BYTES, xattr_block.entry_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_XATTR_UUID_LOW_OFFSET_BYTES,
             xattr_block.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_XATTR_UUID_HIGH_OFFSET_BYTES,
             xattr_block.file_system_uuid.high);
    uint64_t cursor = OS_KERNEL_ROOTFS_V5_XATTR_ENTRIES_START_BYTES;
    for (uint64_t index = 0ULL; index < xattr_block.entry_count; ++index) {
        const RootXattrEntry &entry = xattr_block.entries[index];
        if (!XattrEntryValid(entry) ||
            (index != 0ULL && CompareXattr(xattr_block.entries[index - 1ULL], entry) >= 0)) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
        const uint64_t record_size = AlignedXattrRecordSize(entry);
        if (cursor > OS_KERNEL_ROOTFS_V5_XATTR_CHECKSUM_OFFSET_BYTES - record_size) {
            return RootInodeMetadataStatus::CapacityExhausted;
        }
        WriteU64(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_NAMESPACE_OFFSET_BYTES,
                 static_cast<uint64_t>(entry.name_space));
        WriteU32(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_NAME_LENGTH_OFFSET_BYTES,
                 static_cast<uint32_t>(entry.name_length_bytes));
        WriteU32(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_VALUE_SIZE_OFFSET_BYTES,
                 static_cast<uint32_t>(entry.value_size_bytes));
        WriteU32(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_RECORD_SIZE_OFFSET_BYTES,
                 static_cast<uint32_t>(record_size));
        WriteU32(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HASH_OFFSET_BYTES,
                 static_cast<uint32_t>(XattrHash(entry)));
        CopyBytes(block + cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES, entry.name,
                  entry.name_length_bytes);
        CopyBytes(block + cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES +
                      entry.name_length_bytes,
                  entry.value, entry.value_size_bytes);
        cursor += record_size;
    }
    WriteU64(block, OS_KERNEL_ROOTFS_V5_XATTR_USED_SIZE_OFFSET_BYTES, cursor);
    WriteU32(block, OS_KERNEL_ROOTFS_V5_XATTR_CHECKSUM_OFFSET_BYTES,
             CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_XATTR_CHECKSUM_OFFSET_BYTES));
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus DecodeRootXattrBlock(const uint8_t *const block,
                                             const uint64_t block_size_bytes,
                                             RootXattrBlock &xattr_block) noexcept {
    xattr_block = RootXattrBlock{};
    if (block == nullptr) {
        return RootInodeMetadataStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootInodeMetadataStatus::InvalidBufferSize;
    }
    if (!BytesEqual(block, OS_KERNEL_ROOTFS_V5_XATTR_MAGIC,
                    OS_KERNEL_ROOTFS_V5_METADATA_MAGIC_SIZE_BYTES)) {
        return RootInodeMetadataStatus::InvalidMagic;
    }
    if (ReadU32(block, OS_KERNEL_ROOTFS_V5_XATTR_CHECKSUM_OFFSET_BYTES) !=
        CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_XATTR_CHECKSUM_OFFSET_BYTES)) {
        return RootInodeMetadataStatus::InvalidChecksum;
    }
    xattr_block.inode_number = ReadU64(block, OS_KERNEL_ROOTFS_V5_XATTR_INODE_OFFSET_BYTES);
    xattr_block.inode_generation =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_XATTR_INODE_GENERATION_OFFSET_BYTES);
    xattr_block.block_generation =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_XATTR_BLOCK_GENERATION_OFFSET_BYTES);
    xattr_block.entry_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_COUNT_OFFSET_BYTES);
    const uint64_t used_size = ReadU64(block, OS_KERNEL_ROOTFS_V5_XATTR_USED_SIZE_OFFSET_BYTES);
    xattr_block.file_system_uuid = RootV5Uuid{
        .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_XATTR_UUID_LOW_OFFSET_BYTES),
        .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_XATTR_UUID_HIGH_OFFSET_BYTES),
    };
    if (ReadU64(block, OS_KERNEL_ROOTFS_V5_METADATA_VERSION_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_METADATA_FORMAT_VERSION ||
        ReadU64(block, OS_KERNEL_ROOTFS_V5_METADATA_HEADER_SIZE_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_XATTR_BLOCK_HEADER_SIZE_BYTES ||
        xattr_block.inode_number == 0ULL || xattr_block.inode_generation == 0ULL ||
        xattr_block.block_generation == 0ULL ||
        xattr_block.entry_count > OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_ENTRY_COUNT ||
        (xattr_block.file_system_uuid.low == 0ULL && xattr_block.file_system_uuid.high == 0ULL) ||
        used_size < OS_KERNEL_ROOTFS_V5_XATTR_ENTRIES_START_BYTES ||
        used_size > OS_KERNEL_ROOTFS_V5_XATTR_CHECKSUM_OFFSET_BYTES ||
        !RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_XATTR_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_XATTR_ENTRIES_START_BYTES -
                                OS_KERNEL_ROOTFS_V5_XATTR_RESERVED_START_BYTES)) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    uint64_t cursor = OS_KERNEL_ROOTFS_V5_XATTR_ENTRIES_START_BYTES;
    for (uint64_t index = 0ULL; index < xattr_block.entry_count; ++index) {
        if (cursor > used_size - OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
        RootXattrEntry &entry = xattr_block.entries[index];
        entry.name_space = static_cast<RootXattrNamespace>(
            ReadU64(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_NAMESPACE_OFFSET_BYTES));
        entry.name_length_bytes =
            ReadU32(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_NAME_LENGTH_OFFSET_BYTES);
        entry.value_size_bytes =
            ReadU32(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_VALUE_SIZE_OFFSET_BYTES);
        const uint64_t record_size =
            ReadU32(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_RECORD_SIZE_OFFSET_BYTES);
        if (entry.name_length_bytes == 0ULL ||
            entry.name_length_bytes > OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_NAME_LENGTH_BYTES ||
            entry.value_size_bytes > OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_VALUE_SIZE_BYTES ||
            record_size != AlignedXattrRecordSize(entry) || record_size > used_size - cursor) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
        CopyBytes(entry.name, block + cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES,
                  entry.name_length_bytes);
        CopyBytes(entry.value,
                  block + cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES +
                      entry.name_length_bytes,
                  entry.value_size_bytes);
        if (!XattrEntryValid(entry) ||
            ReadU32(block, cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HASH_OFFSET_BYTES) !=
                static_cast<uint32_t>(XattrHash(entry)) ||
            !RootV5BytesAreZero(block + cursor + OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES +
                                    entry.name_length_bytes + entry.value_size_bytes,
                                record_size - OS_KERNEL_ROOTFS_V5_XATTR_ENTRY_HEADER_SIZE_BYTES -
                                    entry.name_length_bytes - entry.value_size_bytes) ||
            (index != 0ULL && CompareXattr(xattr_block.entries[index - 1ULL], entry) >= 0)) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
        cursor += record_size;
    }
    if (cursor != used_size ||
        !RootV5BytesAreZero(block + used_size,
                            OS_KERNEL_ROOTFS_V5_XATTR_CHECKSUM_OFFSET_BYTES - used_size)) {
        return RootInodeMetadataStatus::NonZeroReservedBytes;
    }
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus ValidateRootAcl(const RootAcl &acl) noexcept {
    if (acl.generation == 0ULL || acl.entry_count < 3ULL ||
        acl.entry_count > OS_KERNEL_ROOTFS_V5_ACL_MAXIMUM_ENTRY_COUNT) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    uint64_t user_object_count = 0ULL;
    uint64_t group_object_count = 0ULL;
    uint64_t other_count = 0ULL;
    uint64_t mask_count = 0ULL;
    bool named = false;
    for (uint64_t index = 0ULL; index < acl.entry_count; ++index) {
        const RootAclEntry &entry = acl.entries[index];
        if ((entry.permissions & ~OS_KERNEL_ROOTFS_V5_ACL_PERMISSION_MASK) != 0U) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
        user_object_count += entry.type == RootAclEntryType::UserObject ? 1ULL : 0ULL;
        group_object_count += entry.type == RootAclEntryType::GroupObject ? 1ULL : 0ULL;
        other_count += entry.type == RootAclEntryType::Other ? 1ULL : 0ULL;
        mask_count += entry.type == RootAclEntryType::Mask ? 1ULL : 0ULL;
        named = named || entry.type == RootAclEntryType::NamedUser ||
                entry.type == RootAclEntryType::NamedGroup;
        for (uint64_t prior = 0ULL; prior < index; ++prior) {
            if (acl.entries[prior].type == entry.type &&
                acl.entries[prior].identifier == entry.identifier) {
                return RootInodeMetadataStatus::DuplicateEntry;
            }
        }
    }
    return user_object_count == 1ULL && group_object_count == 1ULL && other_count == 1ULL &&
                   mask_count <= 1ULL && (!named || mask_count == 1ULL)
               ? RootInodeMetadataStatus::Succeeded
               : RootInodeMetadataStatus::InvalidEntry;
}

RootInodeMetadataStatus EvaluateRootAcl(const RootAcl &acl,
                                        const os::abi::UserIdentifier owner_user_identifier,
                                        const os::abi::GroupIdentifier owner_group_identifier,
                                        const os::abi::UserIdentifier user_identifier,
                                        const os::abi::GroupIdentifier *const group_identifiers,
                                        const uint64_t group_identifier_count,
                                        const os::abi::FileMode requested_permissions) noexcept {
    if (ValidateRootAcl(acl) != RootInodeMetadataStatus::Succeeded ||
        (group_identifiers == nullptr && group_identifier_count != 0ULL) ||
        (requested_permissions & ~OS_KERNEL_ROOTFS_V5_ACL_PERMISSION_MASK) != 0U) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    if (user_identifier == os::abi::OS_ABI_ROOT_USER_IDENTIFIER) {
        return RootInodeMetadataStatus::Succeeded;
    }
    os::abi::FileMode mask = OS_KERNEL_ROOTFS_V5_ACL_PERMISSION_MASK;
    os::abi::FileMode selected = 0U;
    if (user_identifier == owner_user_identifier) {
        for (uint64_t index = 0ULL; index < acl.entry_count; ++index) {
            if (acl.entries[index].type == RootAclEntryType::UserObject) {
                selected = acl.entries[index].permissions;
            }
        }
    } else {
        bool named_user_matched = false;
        for (uint64_t index = 0ULL; index < acl.entry_count; ++index) {
            const RootAclEntry &entry = acl.entries[index];
            if (entry.type == RootAclEntryType::Mask) {
                mask = entry.permissions;
            }
            if (entry.type == RootAclEntryType::NamedUser && entry.identifier == user_identifier) {
                selected = entry.permissions;
                named_user_matched = true;
            }
        }
        if (!named_user_matched) {
            bool group_matched = false;
            for (uint64_t group_index = 0ULL; group_index < group_identifier_count; ++group_index) {
                const uint64_t group = group_identifiers[group_index];
                for (uint64_t index = 0ULL; index < acl.entry_count; ++index) {
                    const RootAclEntry &entry = acl.entries[index];
                    if ((entry.type == RootAclEntryType::GroupObject &&
                         group == owner_group_identifier) ||
                        (entry.type == RootAclEntryType::NamedGroup && entry.identifier == group)) {
                        selected = static_cast<os::abi::FileMode>(selected | entry.permissions);
                        group_matched = true;
                    }
                }
            }
            if (!group_matched) {
                for (uint64_t index = 0ULL; index < acl.entry_count; ++index) {
                    if (acl.entries[index].type == RootAclEntryType::Other) {
                        selected = acl.entries[index].permissions;
                    }
                }
            } else {
                selected = static_cast<os::abi::FileMode>(selected & mask);
            }
        } else {
            selected = static_cast<os::abi::FileMode>(selected & mask);
        }
    }
    return (selected & requested_permissions) == requested_permissions
               ? RootInodeMetadataStatus::Succeeded
               : RootInodeMetadataStatus::PermissionDenied;
}

RootInodeMetadataStatus RootQuotaManager::Initialize(const RootV5Uuid file_system_uuid) noexcept {
    if (this->initialized_ || (file_system_uuid.low == 0ULL && file_system_uuid.high == 0ULL)) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    this->block_ = RootQuotaBlock{
        .generation = 1ULL,
        .record_count = 0ULL,
        .file_system_uuid = file_system_uuid,
        .records = {},
    };
    this->initialized_ = true;
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus
RootQuotaManager::SetLimits(const RootQuotaType type, const uint64_t identifier,
                            const uint64_t soft_block_limit, const uint64_t hard_block_limit,
                            const uint64_t soft_inode_limit, const uint64_t hard_inode_limit,
                            const uint64_t grace_deadline_nanoseconds) noexcept {
    if (!this->initialized_ || !QuotaTypeValid(type) ||
        (hard_block_limit != 0ULL && soft_block_limit > hard_block_limit) ||
        (hard_inode_limit != 0ULL && soft_inode_limit > hard_inode_limit) ||
        this->block_.generation == UINT64_MAX) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    uint64_t index = 0ULL;
    while (index < this->block_.record_count &&
           (this->block_.records[index].type != type ||
            this->block_.records[index].identifier != identifier)) {
        ++index;
    }
    if (index == this->block_.record_count) {
        if (this->block_.record_count >= OS_KERNEL_ROOTFS_V5_QUOTA_MAXIMUM_RECORD_COUNT) {
            return RootInodeMetadataStatus::CapacityExhausted;
        }
        ++this->block_.record_count;
    }
    RootQuotaRecord candidate = this->block_.records[index];
    candidate.type = type;
    candidate.identifier = identifier;
    candidate.soft_block_limit = soft_block_limit;
    candidate.hard_block_limit = hard_block_limit;
    candidate.soft_inode_limit = soft_inode_limit;
    candidate.hard_inode_limit = hard_inode_limit;
    candidate.grace_deadline_nanoseconds = grace_deadline_nanoseconds;
    if (!QuotaRecordValid(candidate)) {
        if (index + 1ULL == this->block_.record_count && this->block_.records[index].type != type) {
            --this->block_.record_count;
        }
        return RootInodeMetadataStatus::QuotaExceeded;
    }
    this->block_.records[index] = candidate;
    ++this->block_.generation;
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus RootQuotaManager::Charge(const RootQuotaType type,
                                                 const uint64_t identifier,
                                                 const uint64_t block_count,
                                                 const uint64_t inode_count,
                                                 const uint64_t now_nanoseconds) noexcept {
    if (!this->initialized_ || block_count > UINT64_MAX - inode_count) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    for (uint64_t index = 0ULL; index < this->block_.record_count; ++index) {
        RootQuotaRecord &record = this->block_.records[index];
        if (record.type != type || record.identifier != identifier ||
            block_count > UINT64_MAX - record.used_block_count ||
            inode_count > UINT64_MAX - record.used_inode_count) {
            continue;
        }
        const uint64_t new_blocks = record.used_block_count + block_count;
        const uint64_t new_inodes = record.used_inode_count + inode_count;
        const bool hard_exceeded =
            (record.hard_block_limit != 0ULL && new_blocks > record.hard_block_limit) ||
            (record.hard_inode_limit != 0ULL && new_inodes > record.hard_inode_limit);
        const bool soft_expired =
            record.grace_deadline_nanoseconds != 0ULL &&
            now_nanoseconds > record.grace_deadline_nanoseconds &&
            ((record.soft_block_limit != 0ULL && new_blocks > record.soft_block_limit) ||
             (record.soft_inode_limit != 0ULL && new_inodes > record.soft_inode_limit));
        if (hard_exceeded || soft_expired) {
            return RootInodeMetadataStatus::QuotaExceeded;
        }
        record.used_block_count = new_blocks;
        record.used_inode_count = new_inodes;
        ++this->block_.generation;
        return RootInodeMetadataStatus::Succeeded;
    }
    return RootInodeMetadataStatus::NotFound;
}

RootInodeMetadataStatus RootQuotaManager::Release(const RootQuotaType type,
                                                  const uint64_t identifier,
                                                  const uint64_t block_count,
                                                  const uint64_t inode_count) noexcept {
    for (uint64_t index = 0ULL; index < this->block_.record_count; ++index) {
        RootQuotaRecord &record = this->block_.records[index];
        if (record.type == type && record.identifier == identifier) {
            if (block_count > record.used_block_count || inode_count > record.used_inode_count) {
                return RootInodeMetadataStatus::InvalidArgument;
            }
            record.used_block_count -= block_count;
            record.used_inode_count -= inode_count;
            ++this->block_.generation;
            return RootInodeMetadataStatus::Succeeded;
        }
    }
    return RootInodeMetadataStatus::NotFound;
}

RootInodeMetadataStatus RootQuotaManager::Export(RootQuotaBlock &block) const noexcept {
    if (!this->initialized_) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    block = this->block_;
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus RootQuotaManager::Validate() const noexcept {
    if (!this->initialized_ || this->block_.generation == 0ULL ||
        this->block_.record_count > OS_KERNEL_ROOTFS_V5_QUOTA_MAXIMUM_RECORD_COUNT) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    for (uint64_t index = 0ULL; index < this->block_.record_count; ++index) {
        if (!QuotaRecordValid(this->block_.records[index])) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
        for (uint64_t prior = 0ULL; prior < index; ++prior) {
            if (this->block_.records[prior].type == this->block_.records[index].type &&
                this->block_.records[prior].identifier == this->block_.records[index].identifier) {
                return RootInodeMetadataStatus::DuplicateEntry;
            }
        }
    }
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus EncodeRootQuotaBlock(const RootQuotaBlock &quota_block,
                                             uint8_t *const block,
                                             const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootInodeMetadataStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES ||
        quota_block.generation == 0ULL ||
        quota_block.record_count > OS_KERNEL_ROOTFS_V5_QUOTA_MAXIMUM_RECORD_COUNT ||
        (quota_block.file_system_uuid.low == 0ULL && quota_block.file_system_uuid.high == 0ULL)) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    ClearBytes(block, block_size_bytes);
    CopyBytes(block, OS_KERNEL_ROOTFS_V5_QUOTA_MAGIC,
              OS_KERNEL_ROOTFS_V5_METADATA_MAGIC_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_METADATA_VERSION_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_METADATA_FORMAT_VERSION);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_METADATA_HEADER_SIZE_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_QUOTA_BLOCK_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_QUOTA_GENERATION_OFFSET_BYTES, quota_block.generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_COUNT_OFFSET_BYTES, quota_block.record_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_QUOTA_UUID_LOW_OFFSET_BYTES,
             quota_block.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_QUOTA_UUID_HIGH_OFFSET_BYTES,
             quota_block.file_system_uuid.high);
    for (uint64_t index = 0ULL; index < quota_block.record_count; ++index) {
        const RootQuotaRecord &record = quota_block.records[index];
        if (!QuotaRecordValid(record)) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
        for (uint64_t prior = 0ULL; prior < index; ++prior) {
            if (quota_block.records[prior].type == record.type &&
                quota_block.records[prior].identifier == record.identifier) {
                return RootInodeMetadataStatus::DuplicateEntry;
            }
        }
        const uint64_t offset = OS_KERNEL_ROOTFS_V5_QUOTA_RECORDS_START_BYTES +
                                index * OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SIZE_BYTES;
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_TYPE_OFFSET_BYTES,
                 static_cast<uint64_t>(record.type));
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_IDENTIFIER_OFFSET_BYTES,
                 record.identifier);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_USED_BLOCKS_OFFSET_BYTES,
                 record.used_block_count);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_USED_INODES_OFFSET_BYTES,
                 record.used_inode_count);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SOFT_BLOCKS_OFFSET_BYTES,
                 record.soft_block_limit);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_HARD_BLOCKS_OFFSET_BYTES,
                 record.hard_block_limit);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SOFT_INODES_OFFSET_BYTES,
                 record.soft_inode_limit);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_HARD_INODES_OFFSET_BYTES,
                 record.hard_inode_limit);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_GRACE_OFFSET_BYTES,
                 record.grace_deadline_nanoseconds);
    }
    WriteU32(block, OS_KERNEL_ROOTFS_V5_QUOTA_CHECKSUM_OFFSET_BYTES,
             CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_QUOTA_CHECKSUM_OFFSET_BYTES));
    return RootInodeMetadataStatus::Succeeded;
}

RootInodeMetadataStatus DecodeRootQuotaBlock(const uint8_t *const block,
                                             const uint64_t block_size_bytes,
                                             RootQuotaBlock &quota_block) noexcept {
    quota_block = RootQuotaBlock{};
    if (block == nullptr) {
        return RootInodeMetadataStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootInodeMetadataStatus::InvalidBufferSize;
    }
    if (!BytesEqual(block, OS_KERNEL_ROOTFS_V5_QUOTA_MAGIC,
                    OS_KERNEL_ROOTFS_V5_METADATA_MAGIC_SIZE_BYTES)) {
        return RootInodeMetadataStatus::InvalidMagic;
    }
    if (ReadU32(block, OS_KERNEL_ROOTFS_V5_QUOTA_CHECKSUM_OFFSET_BYTES) !=
        CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_QUOTA_CHECKSUM_OFFSET_BYTES)) {
        return RootInodeMetadataStatus::InvalidChecksum;
    }
    quota_block.generation = ReadU64(block, OS_KERNEL_ROOTFS_V5_QUOTA_GENERATION_OFFSET_BYTES);
    quota_block.record_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_COUNT_OFFSET_BYTES);
    quota_block.file_system_uuid = RootV5Uuid{
        .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_QUOTA_UUID_LOW_OFFSET_BYTES),
        .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_QUOTA_UUID_HIGH_OFFSET_BYTES),
    };
    if (ReadU64(block, OS_KERNEL_ROOTFS_V5_METADATA_VERSION_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_METADATA_FORMAT_VERSION ||
        ReadU64(block, OS_KERNEL_ROOTFS_V5_METADATA_HEADER_SIZE_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_V5_QUOTA_BLOCK_HEADER_SIZE_BYTES ||
        quota_block.generation == 0ULL ||
        quota_block.record_count > OS_KERNEL_ROOTFS_V5_QUOTA_MAXIMUM_RECORD_COUNT ||
        (quota_block.file_system_uuid.low == 0ULL && quota_block.file_system_uuid.high == 0ULL) ||
        !RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_QUOTA_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_QUOTA_RECORDS_START_BYTES -
                                OS_KERNEL_ROOTFS_V5_QUOTA_RESERVED_START_BYTES)) {
        return RootInodeMetadataStatus::InvalidArgument;
    }
    for (uint64_t index = 0ULL; index < quota_block.record_count; ++index) {
        const uint64_t offset = OS_KERNEL_ROOTFS_V5_QUOTA_RECORDS_START_BYTES +
                                index * OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SIZE_BYTES;
        quota_block.records[index] = RootQuotaRecord{
            .type = static_cast<RootQuotaType>(
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_TYPE_OFFSET_BYTES)),
            .identifier =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_IDENTIFIER_OFFSET_BYTES),
            .used_block_count =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_USED_BLOCKS_OFFSET_BYTES),
            .used_inode_count =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_USED_INODES_OFFSET_BYTES),
            .soft_block_limit =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SOFT_BLOCKS_OFFSET_BYTES),
            .hard_block_limit =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_HARD_BLOCKS_OFFSET_BYTES),
            .soft_inode_limit =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SOFT_INODES_OFFSET_BYTES),
            .hard_inode_limit =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_HARD_INODES_OFFSET_BYTES),
            .grace_deadline_nanoseconds =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_GRACE_OFFSET_BYTES),
        };
        if (!QuotaRecordValid(quota_block.records[index])) {
            return RootInodeMetadataStatus::InvalidEntry;
        }
        for (uint64_t prior = 0ULL; prior < index; ++prior) {
            if (quota_block.records[prior].type == quota_block.records[index].type &&
                quota_block.records[prior].identifier == quota_block.records[index].identifier) {
                return RootInodeMetadataStatus::DuplicateEntry;
            }
        }
    }
    const uint64_t used_end =
        OS_KERNEL_ROOTFS_V5_QUOTA_RECORDS_START_BYTES +
        quota_block.record_count * OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SIZE_BYTES;
    if (!RootV5BytesAreZero(block + used_end,
                            OS_KERNEL_ROOTFS_V5_QUOTA_CHECKSUM_OFFSET_BYTES - used_end)) {
        return RootInodeMetadataStatus::NonZeroReservedBytes;
    }
    return RootInodeMetadataStatus::Succeeded;
}

}
