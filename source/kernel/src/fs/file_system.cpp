#include "os/kernel/fs/file_system.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_SYSTEM_ZERO_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_RESERVED_INODE_NUMBER = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FIRST_ALLOCATABLE_INODE_NUMBER = 2ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_INITIAL_LINK_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT = 64ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_MINIMUM_PATH_LENGTH_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FIRST_PATH_BYTE_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_FIRST_COMPONENT_BYTE_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_MAXIMUM_CONTROL_CHARACTER = 0x1FULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_DELETE_CONTROL_CHARACTER = 0x7FULL;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_UNUSED_BITMAP_BIT_COUNT =
    OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES * OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
constexpr uint64_t OS_KERNEL_FILE_SYSTEM_MAXIMUM_DIRECTORY_ENTRY_COUNT =
    OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES /
    OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
constexpr uint8_t OS_KERNEL_FILE_SYSTEM_ZERO_BYTE = 0U;
constexpr uint8_t OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR = static_cast<uint8_t>('/');
constexpr uint8_t OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER = static_cast<uint8_t>('.');
constexpr uint8_t OS_KERNEL_FILE_SYSTEM_FIRST_BIT_MASK = 0x01U;

void ClearBytes(uint8_t *bytes, const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; byte_index < byte_count;
         ++byte_index) {
        bytes[byte_index] = OS_KERNEL_FILE_SYSTEM_ZERO_BYTE;
    }
}

void CopyBytes(uint8_t *destination, const uint8_t *source, const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; byte_index < byte_count;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool BytesAreEqual(const uint8_t *left, const uint8_t *right,
                                 const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; byte_index < byte_count;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] uint64_t RequiredBlockCount(const uint64_t size_bytes) noexcept {
    if (size_bytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    }
    return (size_bytes + OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES -
            OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT) /
           OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
}

[[nodiscard]] uint64_t RelativeToLogicalBlockAddress(const uint64_t relative_block) noexcept {
    return OS_KERNEL_FILE_SYSTEM_START_LBA + relative_block;
}

[[nodiscard]] bool FileSystemFormatSucceeded(const FileSystemFormatStatus status) noexcept {
    return status == FileSystemFormatStatus::Succeeded;
}

[[nodiscard]] bool DirectoryNameIsValid(const FileSystemDirectoryEntry &entry) noexcept {
    if (entry.name_length_bytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE ||
        entry.name_length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         byte_index < entry.name_length_bytes; ++byte_index) {
        const uint8_t name_byte = entry.name[byte_index];
        if (name_byte <= OS_KERNEL_FILE_SYSTEM_MAXIMUM_CONTROL_CHARACTER ||
            name_byte == OS_KERNEL_FILE_SYSTEM_DELETE_CONTROL_CHARACTER ||
            name_byte == OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
            return false;
        }
    }
    const bool is_dot =
        entry.name_length_bytes == OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT &&
        entry.name[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] == OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER;
    const bool is_dot_dot =
        entry.name_length_bytes ==
            OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT + OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT &&
        entry.name[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] == OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER &&
        entry.name[OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT] == OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER;
    return !is_dot && !is_dot_dot;
}

}

FileSystemStatus FileSystem::MountOrFormat(FileSystemBlockDevice &device,
                                           bool &formatted) noexcept {
    SpinLockGuard guard{this->lock_};
    formatted = false;
    if (this->initialized_) {
        return FileSystemStatus::AlreadyInitialized;
    }

    this->device_ = &device;
    this->failed_ = false;
    this->statistics_ = FileSystemStatistics{};

    uint8_t superblock_bytes[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    if (device.ReadBlock(
            RelativeToLogicalBlockAddress(OS_KERNEL_FILE_SYSTEM_SUPERBLOCK_RELATIVE_BLOCK),
            superblock_bytes,
            OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) != FileSystemBlockDeviceStatus::Succeeded) {
        return this->FailDeviceOperation();
    }

    if (FileSystemBlockIsZero(superblock_bytes, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES)) {
        const FileSystemStatus format_status = this->Format(device);
        if (format_status != FileSystemStatus::Succeeded) {
            return format_status;
        }
        formatted = true;
        this->statistics_.formatted_during_mount = true;
        return FileSystemStatus::Succeeded;
    }

    if (!FileSystemFormatSucceeded(DecodeFileSystemSuperblock(
            superblock_bytes, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES, this->superblock_))) {
        return FileSystemStatus::Corrupt;
    }
    if (this->superblock_.transaction_state == FileSystemTransactionState::Dirty) {
        return FileSystemStatus::IncompleteTransaction;
    }

    this->cache_.Initialize(device);
    this->initialized_ = true;
    const FileSystemStatus consistency_status = this->CheckConsistencyUnlocked();
    if (consistency_status != FileSystemStatus::Succeeded) {
        this->initialized_ = false;
        return consistency_status;
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::Format(FileSystemBlockDevice &device) noexcept {
    this->device_ = &device;
    this->cache_.Initialize(device);
    this->superblock_ = CreateFileSystemSuperblock();

    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    SetBitmapBit(block, OS_KERNEL_FILE_SYSTEM_RESERVED_INODE_NUMBER, true);
    SetBitmapBit(block, OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER, true);
    FileSystemStatus status = this->WriteBitmap(true, block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }

    ClearBytes(block, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES);
    for (uint64_t inode_table_block_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         inode_table_block_index < OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT;
         ++inode_table_block_index) {
        status = this->WriteRelativeBlock(OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK +
                                              inode_table_block_index,
                                          block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }

    FileSystemInode root_inode{
        .type = FileSystemNodeType::Directory,
        .size_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .generation = this->superblock_.transaction_generation,
        .link_count = OS_KERNEL_FILE_SYSTEM_INITIAL_LINK_COUNT,
        .allocated_block_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .direct_blocks = {},
    };
    status = this->WriteInode(OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER, root_inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }

    ClearBytes(block, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES);
    status = this->WriteBitmap(false, block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (this->cache_.Sync() != BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    status = this->WriteSuperblockDirect();
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }

    this->initialized_ = true;
    return this->CheckConsistencyUnlocked();
}

FileSystemStatus FileSystem::BeginTransaction() noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (this->failed_) {
        return FileSystemStatus::DeviceFailure;
    }
    if (this->superblock_.transaction_state != FileSystemTransactionState::Clean) {
        return FileSystemStatus::IncompleteTransaction;
    }
    ++this->superblock_.transaction_generation;
    this->superblock_.transaction_state = FileSystemTransactionState::Dirty;
    return this->WriteSuperblockDirect();
}

FileSystemStatus FileSystem::CommitTransaction() noexcept {
    if (this->cache_.Sync() != BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    this->superblock_.transaction_state = FileSystemTransactionState::Clean;
    return this->WriteSuperblockDirect();
}

FileSystemStatus FileSystem::FailDeviceOperation() noexcept {
    this->failed_ = true;
    return FileSystemStatus::DeviceFailure;
}

FileSystemStatus FileSystem::WriteSuperblockDirect() noexcept {
    if (this->device_ == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    if (!FileSystemFormatSucceeded(EncodeFileSystemSuperblock(
            this->superblock_, block, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES))) {
        return FileSystemStatus::Corrupt;
    }
    if (this->device_->WriteBlock(
            RelativeToLogicalBlockAddress(OS_KERNEL_FILE_SYSTEM_SUPERBLOCK_RELATIVE_BLOCK), block,
            OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) != FileSystemBlockDeviceStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    if (this->device_->Flush() != FileSystemBlockDeviceStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ReadRelativeBlock(const uint64_t relative_block,
                                               uint8_t *block) noexcept {
    if (block == nullptr || relative_block >= OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT) {
        return FileSystemStatus::InvalidArgument;
    }
    if (this->cache_.ReadBlock(RelativeToLogicalBlockAddress(relative_block), block,
                               OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
        BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::WriteRelativeBlock(const uint64_t relative_block,
                                                const uint8_t *block) noexcept {
    if (block == nullptr || relative_block >= OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT) {
        return FileSystemStatus::InvalidArgument;
    }
    if (this->cache_.WriteBlock(RelativeToLogicalBlockAddress(relative_block), block,
                                OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
        BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ReadInode(const uint64_t inode_number,
                                       FileSystemInode &inode) noexcept {
    if (inode_number >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT) {
        return FileSystemStatus::Corrupt;
    }
    const uint64_t relative_block = OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK +
                                    inode_number / OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK;
    const uint64_t inode_offset = (inode_number % OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK) *
                                  OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES;
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadRelativeBlock(relative_block, block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (!FileSystemFormatSucceeded(DecodeFileSystemInode(
            block + inode_offset, OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES, inode))) {
        return FileSystemStatus::Corrupt;
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::WriteInode(const uint64_t inode_number,
                                        const FileSystemInode &inode) noexcept {
    if (inode_number >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT) {
        return FileSystemStatus::Corrupt;
    }
    const uint64_t relative_block = OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK +
                                    inode_number / OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK;
    const uint64_t inode_offset = (inode_number % OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK) *
                                  OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES;
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadRelativeBlock(relative_block, block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (!FileSystemFormatSucceeded(EncodeFileSystemInode(inode, block + inode_offset,
                                                         OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES))) {
        return FileSystemStatus::Corrupt;
    }
    return this->WriteRelativeBlock(relative_block, block);
}

FileSystemStatus FileSystem::ReadBitmap(const bool inode_bitmap, uint8_t *block) noexcept {
    return this->ReadRelativeBlock(inode_bitmap ? OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK
                                                : OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK,
                                   block);
}

FileSystemStatus FileSystem::WriteBitmap(const bool inode_bitmap, const uint8_t *block) noexcept {
    return this->WriteRelativeBlock(inode_bitmap ? OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK
                                                 : OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK,
                                    block);
}

bool FileSystem::BitmapBitIsSet(const uint8_t *bitmap, const uint64_t bit_index) const noexcept {
    const uint64_t byte_index = bit_index / OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
    const uint64_t bit_offset = bit_index % OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
    const uint8_t mask = static_cast<uint8_t>(
        static_cast<uint64_t>(OS_KERNEL_FILE_SYSTEM_FIRST_BIT_MASK) << bit_offset);
    return (bitmap[byte_index] & mask) != OS_KERNEL_FILE_SYSTEM_ZERO_BYTE;
}

void FileSystem::SetBitmapBit(uint8_t *bitmap, const uint64_t bit_index,
                              const bool allocated) const noexcept {
    const uint64_t byte_index = bit_index / OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
    const uint64_t bit_offset = bit_index % OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
    const uint8_t mask = static_cast<uint8_t>(
        static_cast<uint64_t>(OS_KERNEL_FILE_SYSTEM_FIRST_BIT_MASK) << bit_offset);
    if (allocated) {
        bitmap[byte_index] = static_cast<uint8_t>(bitmap[byte_index] | mask);
    } else {
        bitmap[byte_index] = static_cast<uint8_t>(bitmap[byte_index] & static_cast<uint8_t>(~mask));
    }
}

FileSystemStatus FileSystem::FindFreeBitmapBit(const uint8_t *bitmap, const uint64_t first_bit,
                                               const uint64_t bit_count,
                                               uint64_t &bit_index) const noexcept {
    if (bitmap == nullptr || first_bit > bit_count) {
        return FileSystemStatus::InvalidArgument;
    }
    for (uint64_t candidate_bit = first_bit; candidate_bit < bit_count; ++candidate_bit) {
        if (!this->BitmapBitIsSet(bitmap, candidate_bit)) {
            bit_index = candidate_bit;
            return FileSystemStatus::Succeeded;
        }
    }
    return FileSystemStatus::NotFound;
}

FileSystemStatus FileSystem::AllocateInode(uint64_t &inode_number) noexcept {
    uint8_t bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(true, bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    status = this->FindFreeBitmapBit(bitmap, OS_KERNEL_FILE_SYSTEM_FIRST_ALLOCATABLE_INODE_NUMBER,
                                     OS_KERNEL_FILE_SYSTEM_INODE_COUNT, inode_number);
    if (status == FileSystemStatus::NotFound) {
        return FileSystemStatus::InodeCapacityExhausted;
    }
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    this->SetBitmapBit(bitmap, inode_number, true);
    return this->WriteBitmap(true, bitmap);
}

FileSystemStatus FileSystem::AllocateDataBlock(uint64_t &relative_block) noexcept {
    uint8_t bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(false, bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t data_block_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    status = this->FindFreeBitmapBit(bitmap, OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
                                     OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT, data_block_index);
    if (status == FileSystemStatus::NotFound) {
        return FileSystemStatus::DataCapacityExhausted;
    }
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    this->SetBitmapBit(bitmap, data_block_index, true);
    status = this->WriteBitmap(false, bitmap);
    if (status == FileSystemStatus::Succeeded) {
        relative_block = OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK + data_block_index;
    }
    return status;
}

FileSystemStatus FileSystem::ReleaseInodeDataBlocks(FileSystemInode &inode) noexcept {
    uint8_t bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(false, bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    for (uint64_t block_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         block_index < inode.allocated_block_count; ++block_index) {
        const uint64_t relative_block = inode.direct_blocks[block_index];
        if (relative_block < OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK ||
            relative_block >= OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT) {
            return FileSystemStatus::Corrupt;
        }
        this->SetBitmapBit(bitmap, relative_block - OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK,
                           false);
        inode.direct_blocks[block_index] = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    }
    inode.allocated_block_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    inode.size_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    return this->WriteBitmap(false, bitmap);
}

FileSystemStatus FileSystem::ParsePath(const uint8_t *path, const uint64_t path_length_bytes,
                                       PathComponent *components, const uint64_t component_capacity,
                                       uint64_t &component_count) const noexcept {
    component_count = OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT;
    if (path == nullptr || components == nullptr) {
        return FileSystemStatus::InvalidArgument;
    }
    if (path_length_bytes < OS_KERNEL_FILE_SYSTEM_MINIMUM_PATH_LENGTH_BYTES ||
        path[OS_KERNEL_FILE_SYSTEM_FIRST_PATH_BYTE_INDEX] != OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
        return FileSystemStatus::InvalidPath;
    }
    if (path_length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_LENGTH_BYTES) {
        return FileSystemStatus::PathTooLong;
    }
    if (path_length_bytes == OS_KERNEL_FILE_SYSTEM_MINIMUM_PATH_LENGTH_BYTES) {
        return FileSystemStatus::Succeeded;
    }
    if (path[path_length_bytes - OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT] ==
        OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
        return FileSystemStatus::InvalidPath;
    }

    uint64_t byte_index = OS_KERNEL_FILE_SYSTEM_FIRST_COMPONENT_BYTE_INDEX;
    while (byte_index < path_length_bytes) {
        if (component_count >= component_capacity) {
            return FileSystemStatus::PathTooLong;
        }
        PathComponent &component = components[component_count];
        component = PathComponent{};
        while (byte_index < path_length_bytes &&
               path[byte_index] != OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
            const uint8_t path_byte = path[byte_index];
            if (path_byte <= OS_KERNEL_FILE_SYSTEM_MAXIMUM_CONTROL_CHARACTER ||
                path_byte == OS_KERNEL_FILE_SYSTEM_DELETE_CONTROL_CHARACTER) {
                return FileSystemStatus::InvalidPath;
            }
            if (component.length_bytes >= OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES) {
                return FileSystemStatus::NameTooLong;
            }
            component.bytes[component.length_bytes] = path_byte;
            ++component.length_bytes;
            ++byte_index;
        }
        if (component.length_bytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
            return FileSystemStatus::InvalidPath;
        }
        const bool current_component_is_dot =
            component.length_bytes == OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT &&
            component.bytes[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] ==
                OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER;
        const bool current_component_is_dot_dot =
            component.length_bytes ==
                OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT + OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT &&
            component.bytes[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] ==
                OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER &&
            component.bytes[OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT] ==
                OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER;
        if (current_component_is_dot || current_component_is_dot_dot) {
            return FileSystemStatus::InvalidPath;
        }
        ++component_count;
        if (byte_index < path_length_bytes) {
            ++byte_index;
            if (byte_index == path_length_bytes ||
                path[byte_index] == OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
                return FileSystemStatus::InvalidPath;
            }
        }
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::FindDirectoryEntry(const FileSystemInode &directory,
                                                const PathComponent &name,
                                                DirectoryEntryLocation &location) noexcept {
    if (directory.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::NotDirectory;
    }
    if ((directory.size_bytes % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) !=
        OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::Corrupt;
    }
    const uint64_t entry_count =
        directory.size_bytes / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint64_t loaded_block_index = OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
    for (uint64_t entry_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; entry_index < entry_count;
         ++entry_index) {
        const uint64_t block_index =
            entry_index / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
        if (block_index != loaded_block_index) {
            if (block_index >= directory.allocated_block_count) {
                return FileSystemStatus::Corrupt;
            }
            FileSystemStatus status =
                this->ReadRelativeBlock(directory.direct_blocks[block_index], block);
            if (status != FileSystemStatus::Succeeded) {
                return status;
            }
            loaded_block_index = block_index;
        }
        const uint64_t entry_offset =
            (entry_index % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
            OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
        FileSystemDirectoryEntry entry{};
        if (!FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
                block + entry_offset, OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES, entry))) {
            return FileSystemStatus::Corrupt;
        }
        if (entry.name_length_bytes == name.length_bytes &&
            BytesAreEqual(entry.name, name.bytes, name.length_bytes)) {
            location.entry_index = entry_index;
            location.entry = entry;
            return FileSystemStatus::Succeeded;
        }
    }
    return FileSystemStatus::NotFound;
}

FileSystemStatus FileSystem::ResolvePath(const PathComponent *components,
                                         const uint64_t component_count, uint64_t &inode_number,
                                         FileSystemInode &inode) noexcept {
    if (components == nullptr && component_count != OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT) {
        return FileSystemStatus::InvalidArgument;
    }
    inode_number = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
    FileSystemStatus status = this->ReadInode(inode_number, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    for (uint64_t component_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         component_index < component_count; ++component_index) {
        if (inode.type != FileSystemNodeType::Directory) {
            return FileSystemStatus::NotDirectory;
        }
        DirectoryEntryLocation location{};
        status = this->FindDirectoryEntry(inode, components[component_index], location);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        inode_number = location.entry.inode_number;
        status = this->ReadInode(inode_number, inode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        if (inode.type != location.entry.type) {
            return FileSystemStatus::Corrupt;
        }
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ResolveParent(const PathComponent *components,
                                           const uint64_t component_count,
                                           uint64_t &parent_inode_number,
                                           FileSystemInode &parent_inode) noexcept {
    if (components == nullptr || component_count == OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT) {
        return FileSystemStatus::InvalidPath;
    }
    return this->ResolvePath(components, component_count - OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT,
                             parent_inode_number, parent_inode);
}

FileSystemStatus FileSystem::AppendDirectoryEntry(const uint64_t directory_inode_number,
                                                  FileSystemInode &directory,
                                                  const FileSystemDirectoryEntry &entry) noexcept {
    if (directory.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::NotDirectory;
    }
    const uint64_t entry_index =
        directory.size_bytes / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    if (entry_index >= OS_KERNEL_FILE_SYSTEM_MAXIMUM_DIRECTORY_ENTRY_COUNT) {
        return FileSystemStatus::DirectoryCapacityExhausted;
    }
    const uint64_t block_index = entry_index / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    if (block_index >= directory.allocated_block_count) {
        uint64_t relative_block = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
        FileSystemStatus status = this->AllocateDataBlock(relative_block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        directory.direct_blocks[block_index] = relative_block;
        ++directory.allocated_block_count;
        ClearBytes(block, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES);
    } else {
        FileSystemStatus status =
            this->ReadRelativeBlock(directory.direct_blocks[block_index], block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }

    const uint64_t entry_offset =
        (entry_index % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    if (!FileSystemFormatSucceeded(EncodeFileSystemDirectoryEntry(
            entry, block + entry_offset, OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES))) {
        return FileSystemStatus::Corrupt;
    }
    FileSystemStatus status = this->WriteRelativeBlock(directory.direct_blocks[block_index], block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    directory.size_bytes += OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    directory.generation = this->superblock_.transaction_generation;
    return this->WriteInode(directory_inode_number, directory);
}

FileSystemStatus FileSystem::ReadDirectoryEntryAt(const FileSystemInode &directory,
                                                  const uint64_t entry_index,
                                                  FileSystemDirectoryEntry &entry) noexcept {
    entry = FileSystemDirectoryEntry{};
    if (directory.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::NotDirectory;
    }
    if ((directory.size_bytes % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) !=
        OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::Corrupt;
    }
    const uint64_t entry_count =
        directory.size_bytes / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    if (entry_index >= entry_count) {
        return FileSystemStatus::NotFound;
    }
    const uint64_t block_index = entry_index / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
    if (block_index >= directory.allocated_block_count) {
        return FileSystemStatus::Corrupt;
    }
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadRelativeBlock(directory.direct_blocks[block_index], block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    const uint64_t entry_offset =
        (entry_index % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    return FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
               block + entry_offset, OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES, entry))
               ? FileSystemStatus::Succeeded
               : FileSystemStatus::Corrupt;
}

FileSystemStatus FileSystem::FindParentNode(const uint64_t child_inode_number,
                                            uint64_t &parent_inode_number,
                                            FileSystemInode &parent_inode,
                                            PathComponent &child_name) noexcept {
    parent_inode_number = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    parent_inode = FileSystemInode{};
    child_name = PathComponent{};
    if (child_inode_number == OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER) {
        parent_inode_number = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
        return this->ReadInode(parent_inode_number, parent_inode);
    }
    if (child_inode_number >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT) {
        return FileSystemStatus::InvalidArgument;
    }

    uint8_t inode_bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(true, inode_bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (!this->BitmapBitIsSet(inode_bitmap, child_inode_number)) {
        return FileSystemStatus::NotFound;
    }

    for (uint64_t candidate_inode_number = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
         candidate_inode_number < OS_KERNEL_FILE_SYSTEM_INODE_COUNT; ++candidate_inode_number) {
        if (!this->BitmapBitIsSet(inode_bitmap, candidate_inode_number)) {
            continue;
        }
        FileSystemInode candidate_inode{};
        status = this->ReadInode(candidate_inode_number, candidate_inode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        if (candidate_inode.type != FileSystemNodeType::Directory) {
            continue;
        }
        if ((candidate_inode.size_bytes % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) !=
            OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
            return FileSystemStatus::Corrupt;
        }
        const uint64_t entry_count =
            candidate_inode.size_bytes / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
        for (uint64_t entry_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; entry_index < entry_count;
             ++entry_index) {
            FileSystemDirectoryEntry entry{};
            status = this->ReadDirectoryEntryAt(candidate_inode, entry_index, entry);
            if (status != FileSystemStatus::Succeeded) {
                return status;
            }
            if (entry.inode_number != child_inode_number) {
                continue;
            }
            parent_inode_number = candidate_inode_number;
            parent_inode = candidate_inode;
            child_name.length_bytes = entry.name_length_bytes;
            CopyBytes(child_name.bytes, entry.name, entry.name_length_bytes);
            return FileSystemStatus::Succeeded;
        }
    }
    return FileSystemStatus::NotFound;
}

FileSystemStatus FileSystem::CreateChildNode(const uint64_t parent_inode_number,
                                             FileSystemInode &parent_inode,
                                             const PathComponent &name,
                                             const FileSystemNodeType type,
                                             uint64_t &inode_number) noexcept {
    inode_number = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (parent_inode_number >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT ||
        name.length_bytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE ||
        name.length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES ||
        (type != FileSystemNodeType::RegularFile && type != FileSystemNodeType::Directory)) {
        return FileSystemStatus::InvalidArgument;
    }
    if (parent_inode.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::NotDirectory;
    }
    DirectoryEntryLocation existing{};
    FileSystemStatus status = this->FindDirectoryEntry(parent_inode, name, existing);
    if (status == FileSystemStatus::Succeeded) {
        return FileSystemStatus::AlreadyExists;
    }
    if (status != FileSystemStatus::NotFound) {
        return status;
    }
    const uint64_t parent_entry_count =
        parent_inode.size_bytes / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    if (parent_entry_count >= OS_KERNEL_FILE_SYSTEM_MAXIMUM_DIRECTORY_ENTRY_COUNT) {
        return FileSystemStatus::DirectoryCapacityExhausted;
    }

    // 在把超级块标记为脏之前完成容量预检，避免“空间不足”被误判成崩溃事务。
    uint8_t inode_bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    status = this->ReadBitmap(true, inode_bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t available_inode = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    status =
        this->FindFreeBitmapBit(inode_bitmap, OS_KERNEL_FILE_SYSTEM_FIRST_ALLOCATABLE_INODE_NUMBER,
                                OS_KERNEL_FILE_SYSTEM_INODE_COUNT, available_inode);
    if (status == FileSystemStatus::NotFound) {
        return FileSystemStatus::InodeCapacityExhausted;
    }
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    const bool parent_needs_data_block =
        (parent_entry_count % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) ==
        OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (parent_needs_data_block) {
        uint8_t data_bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
        status = this->ReadBitmap(false, data_bitmap);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        uint64_t unused_data_bit = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
        status = this->FindFreeBitmapBit(data_bitmap, OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
                                         OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT, unused_data_bit);
        if (status == FileSystemStatus::NotFound) {
            return FileSystemStatus::DataCapacityExhausted;
        }
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }

    status = this->BeginTransaction();
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    status = this->AllocateInode(inode_number);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode_number != available_inode) {
        return FileSystemStatus::Corrupt;
    }
    const FileSystemInode inode{
        .type = type,
        .size_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .generation = this->superblock_.transaction_generation,
        .link_count = OS_KERNEL_FILE_SYSTEM_INITIAL_LINK_COUNT,
        .allocated_block_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .direct_blocks = {},
    };
    status = this->WriteInode(inode_number, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }

    FileSystemDirectoryEntry entry{
        .inode_number = inode_number,
        .type = type,
        .name_length_bytes = name.length_bytes,
        .name = {},
    };
    CopyBytes(entry.name, name.bytes, name.length_bytes);
    status = this->AppendDirectoryEntry(parent_inode_number, parent_inode, entry);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    return this->CommitTransaction();
}

FileSystemStatus FileSystem::CreateNode(const PathComponent *components,
                                        const uint64_t component_count,
                                        const FileSystemNodeType type,
                                        uint64_t &inode_number) noexcept {
    if (components == nullptr || component_count == OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT ||
        (type != FileSystemNodeType::RegularFile && type != FileSystemNodeType::Directory)) {
        return FileSystemStatus::InvalidArgument;
    }
    uint64_t parent_inode_number = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemInode parent_inode{};
    FileSystemStatus status =
        this->ResolveParent(components, component_count, parent_inode_number, parent_inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    const PathComponent &name =
        components[component_count - OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT];
    return this->CreateChildNode(parent_inode_number, parent_inode, name, type, inode_number);
}

FileSystemStatus FileSystem::CreateDirectory(const uint8_t *path,
                                             const uint64_t path_length_bytes) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    PathComponent components[OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT]{};
    uint64_t component_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemStatus status =
        this->ParsePath(path, path_length_bytes, components,
                        OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT, component_count);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t inode_number = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    return this->CreateNode(components, component_count, FileSystemNodeType::Directory,
                            inode_number);
}

FileSystemStatus FileSystem::Open(const uint8_t *path, const uint64_t path_length_bytes,
                                  const FileSystemOpenOptions &options,
                                  FileSystemHandle &handle) noexcept {
    SpinLockGuard guard{this->lock_};
    handle = FileSystemHandle{};
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if ((!options.readable && !options.writable) || (options.truncate && !options.writable)) {
        return FileSystemStatus::InvalidArgument;
    }
    PathComponent components[OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT]{};
    uint64_t component_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemStatus status =
        this->ParsePath(path, path_length_bytes, components,
                        OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT, component_count);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t inode_number = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemInode inode{};
    status = this->ResolvePath(components, component_count, inode_number, inode);
    if (status == FileSystemStatus::NotFound && options.create) {
        status = this->CreateNode(components, component_count, FileSystemNodeType::RegularFile,
                                  inode_number);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        status = this->ReadInode(inode_number, inode);
    }
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type == FileSystemNodeType::Directory) {
        return FileSystemStatus::IsDirectory;
    }
    if (inode.type != FileSystemNodeType::RegularFile) {
        return FileSystemStatus::Corrupt;
    }
    if (options.truncate && inode.size_bytes != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        status = this->BeginTransaction();
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        status = this->ReleaseInodeDataBlocks(inode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        inode.generation = this->superblock_.transaction_generation;
        status = this->WriteInode(inode_number, inode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        status = this->CommitTransaction();
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }
    handle = FileSystemHandle{
        .inode_number = inode_number,
        .offset_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .node_type = FileSystemNodeType::RegularFile,
        .readable = options.readable,
        .writable = options.writable,
        .open = true,
    };
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::OpenDirectory(const uint8_t *const path,
                                           const uint64_t path_length_bytes,
                                           FileSystemHandle &handle) noexcept {
    SpinLockGuard guard{this->lock_};
    handle = FileSystemHandle{};
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    PathComponent components[OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT]{};
    uint64_t component_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemStatus status =
        this->ParsePath(path, path_length_bytes, components,
                        OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT, component_count);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t inode_number = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemInode inode{};
    status = this->ResolvePath(components, component_count, inode_number, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::NotDirectory;
    }
    handle = FileSystemHandle{
        .inode_number = inode_number,
        .offset_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .node_type = FileSystemNodeType::Directory,
        .readable = true,
        .writable = false,
        .open = true,
    };
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ReadDirectory(FileSystemHandle &handle,
                                           FileSystemDirectoryEntry &entry,
                                           bool &end_of_directory) noexcept {
    SpinLockGuard guard{this->lock_};
    entry = FileSystemDirectoryEntry{};
    end_of_directory = false;
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if (!handle.open || handle.node_type != FileSystemNodeType::Directory) {
        return FileSystemStatus::InvalidHandle;
    }
    if (!handle.readable) {
        return FileSystemStatus::PermissionDenied;
    }
    FileSystemInode inode{};
    FileSystemStatus status = this->ReadInode(handle.inode_number, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type != FileSystemNodeType::Directory ||
        (handle.offset_bytes % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) !=
            OS_KERNEL_FILE_SYSTEM_ZERO_VALUE ||
        handle.offset_bytes > inode.size_bytes) {
        return FileSystemStatus::Corrupt;
    }
    if (handle.offset_bytes == inode.size_bytes) {
        end_of_directory = true;
        return FileSystemStatus::Succeeded;
    }
    const uint64_t entry_index =
        handle.offset_bytes / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    const uint64_t block_index = entry_index / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
    if (block_index >= inode.allocated_block_count) {
        return FileSystemStatus::Corrupt;
    }
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    status = this->ReadRelativeBlock(inode.direct_blocks[block_index], block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    const uint64_t entry_offset =
        (entry_index % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    if (!FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
            block + entry_offset, OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES, entry))) {
        return FileSystemStatus::Corrupt;
    }
    handle.offset_bytes += OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ReadFileBytes(const FileSystemInode &inode,
                                           const uint64_t offset_bytes, uint8_t *destination,
                                           const uint64_t capacity_bytes,
                                           uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (offset_bytes >= inode.size_bytes || capacity_bytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::Succeeded;
    }
    const uint64_t available_bytes = inode.size_bytes - offset_bytes;
    const uint64_t requested_bytes = Minimum(available_bytes, capacity_bytes);
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    while (read_bytes < requested_bytes) {
        const uint64_t absolute_offset = offset_bytes + read_bytes;
        const uint64_t block_index = absolute_offset / OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
        const uint64_t block_offset = absolute_offset % OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
        if (block_index >= inode.allocated_block_count) {
            return FileSystemStatus::Corrupt;
        }
        FileSystemStatus status = this->ReadRelativeBlock(inode.direct_blocks[block_index], block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        const uint64_t chunk_bytes = Minimum(requested_bytes - read_bytes,
                                             OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES - block_offset);
        CopyBytes(destination + read_bytes, block + block_offset, chunk_bytes);
        read_bytes += chunk_bytes;
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::Read(FileSystemHandle &handle, uint8_t *destination,
                                  const uint64_t capacity_bytes, uint64_t &read_bytes) noexcept {
    SpinLockGuard guard{this->lock_};
    read_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if (!handle.open || handle.node_type != FileSystemNodeType::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    if (!handle.readable) {
        return FileSystemStatus::PermissionDenied;
    }
    if (destination == nullptr && capacity_bytes != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::InvalidArgument;
    }
    FileSystemInode inode{};
    FileSystemStatus status = this->ReadInode(handle.inode_number, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type != FileSystemNodeType::RegularFile) {
        return FileSystemStatus::IsDirectory;
    }
    status =
        this->ReadFileBytes(inode, handle.offset_bytes, destination, capacity_bytes, read_bytes);
    if (status == FileSystemStatus::Succeeded) {
        handle.offset_bytes += read_bytes;
        this->statistics_.bytes_read += read_bytes;
    }
    return status;
}

FileSystemStatus FileSystem::WriteFileBytes(const uint64_t inode_number, FileSystemInode &inode,
                                            const uint64_t offset_bytes, const uint8_t *source,
                                            const uint64_t length_bytes,
                                            uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    const uint64_t final_size = offset_bytes + length_bytes > inode.size_bytes
                                    ? offset_bytes + length_bytes
                                    : inode.size_bytes;
    const uint64_t needed_block_count = RequiredBlockCount(final_size);
    while (inode.allocated_block_count < needed_block_count) {
        uint64_t relative_block = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
        FileSystemStatus status = this->AllocateDataBlock(relative_block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        inode.direct_blocks[inode.allocated_block_count] = relative_block;
        ++inode.allocated_block_count;
        uint8_t zero_block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
        status = this->WriteRelativeBlock(relative_block, zero_block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }

    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    while (written_bytes < length_bytes) {
        const uint64_t absolute_offset = offset_bytes + written_bytes;
        const uint64_t block_index = absolute_offset / OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
        const uint64_t block_offset = absolute_offset % OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
        FileSystemStatus status = this->ReadRelativeBlock(inode.direct_blocks[block_index], block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        const uint64_t chunk_bytes = Minimum(length_bytes - written_bytes,
                                             OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES - block_offset);
        CopyBytes(block + block_offset, source + written_bytes, chunk_bytes);
        status = this->WriteRelativeBlock(inode.direct_blocks[block_index], block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        written_bytes += chunk_bytes;
    }
    inode.size_bytes = final_size;
    inode.generation = this->superblock_.transaction_generation;
    return this->WriteInode(inode_number, inode);
}

FileSystemStatus FileSystem::Write(FileSystemHandle &handle, const uint8_t *source,
                                   const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    SpinLockGuard guard{this->lock_};
    written_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if (!handle.open || handle.node_type != FileSystemNodeType::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    if (!handle.writable) {
        return FileSystemStatus::PermissionDenied;
    }
    if (source == nullptr && length_bytes != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::InvalidArgument;
    }
    if (length_bytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::Succeeded;
    }
    if (handle.offset_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES ||
        length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES - handle.offset_bytes) {
        return FileSystemStatus::FileTooLarge;
    }
    FileSystemInode inode{};
    FileSystemStatus status = this->ReadInode(handle.inode_number, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type != FileSystemNodeType::RegularFile) {
        return FileSystemStatus::IsDirectory;
    }

    const uint64_t final_size = handle.offset_bytes + length_bytes > inode.size_bytes
                                    ? handle.offset_bytes + length_bytes
                                    : inode.size_bytes;
    const uint64_t needed_block_count = RequiredBlockCount(final_size);
    const uint64_t additional_block_count = needed_block_count - inode.allocated_block_count;
    if (additional_block_count != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        uint8_t data_bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
        status = this->ReadBitmap(false, data_bitmap);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        uint64_t free_block_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
        for (uint64_t data_block_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
             data_block_index < OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT; ++data_block_index) {
            if (!this->BitmapBitIsSet(data_bitmap, data_block_index)) {
                ++free_block_count;
            }
        }
        if (free_block_count < additional_block_count) {
            return FileSystemStatus::DataCapacityExhausted;
        }
    }

    status = this->BeginTransaction();
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t transaction_written_bytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    status = this->WriteFileBytes(handle.inode_number, inode, handle.offset_bytes, source,
                                  length_bytes, transaction_written_bytes);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    status = this->CommitTransaction();
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    handle.offset_bytes += transaction_written_bytes;
    written_bytes = transaction_written_bytes;
    this->statistics_.bytes_written += transaction_written_bytes;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::Close(FileSystemHandle &handle) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!handle.open) {
        return FileSystemStatus::InvalidHandle;
    }
    handle = FileSystemHandle{};
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::Sync() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if (this->cache_.Sync() != BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ValidateAllocatedInode(const uint64_t inode_number,
                                                    const uint8_t *inode_bitmap,
                                                    const uint8_t *data_bitmap,
                                                    uint8_t *seen_data_bitmap, uint64_t &file_count,
                                                    uint64_t &directory_count) noexcept {
    FileSystemInode inode{};
    FileSystemStatus status = this->ReadInode(inode_number, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type == FileSystemNodeType::Unused ||
        inode.allocated_block_count != RequiredBlockCount(inode.size_bytes)) {
        return FileSystemStatus::Corrupt;
    }
    if (inode.type == FileSystemNodeType::Directory) {
        if ((inode.size_bytes % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) !=
            OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
            return FileSystemStatus::Corrupt;
        }
        ++directory_count;
    } else if (inode.type == FileSystemNodeType::RegularFile) {
        ++file_count;
    } else {
        return FileSystemStatus::Corrupt;
    }

    for (uint64_t block_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         block_index < OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT; ++block_index) {
        const uint64_t relative_block = inode.direct_blocks[block_index];
        if (block_index < inode.allocated_block_count) {
            if (relative_block < OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK ||
                relative_block >= OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT) {
                return FileSystemStatus::Corrupt;
            }
            const uint64_t data_block_index =
                relative_block - OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK;
            if (!this->BitmapBitIsSet(data_bitmap, data_block_index) ||
                this->BitmapBitIsSet(seen_data_bitmap, data_block_index)) {
                return FileSystemStatus::Corrupt;
            }
            this->SetBitmapBit(seen_data_bitmap, data_block_index, true);
        } else if (relative_block != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
            return FileSystemStatus::Corrupt;
        }
    }

    if (inode.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::Succeeded;
    }
    const uint64_t entry_count =
        inode.size_bytes / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    uint8_t current_block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint8_t previous_block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint64_t current_loaded_block = OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
    uint64_t previous_loaded_block = OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
    for (uint64_t entry_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; entry_index < entry_count;
         ++entry_index) {
        const uint64_t block_index =
            entry_index / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
        if (block_index != current_loaded_block) {
            status = this->ReadRelativeBlock(inode.direct_blocks[block_index], current_block);
            if (status != FileSystemStatus::Succeeded) {
                return status;
            }
            current_loaded_block = block_index;
        }
        const uint64_t entry_offset =
            (entry_index % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
            OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
        FileSystemDirectoryEntry entry{};
        if (!FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
                current_block + entry_offset, OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES,
                entry)) ||
            !DirectoryNameIsValid(entry) ||
            entry.inode_number >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT ||
            !this->BitmapBitIsSet(inode_bitmap, entry.inode_number)) {
            return FileSystemStatus::Corrupt;
        }
        FileSystemInode target_inode{};
        status = this->ReadInode(entry.inode_number, target_inode);
        if (status != FileSystemStatus::Succeeded || target_inode.type != entry.type) {
            return FileSystemStatus::Corrupt;
        }

        // 同一目录不允许出现重名项；用小规模二次扫描换取清晰、可验证的不变量。
        for (uint64_t previous_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
             previous_index < entry_index; ++previous_index) {
            const uint64_t previous_block_index =
                previous_index / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
            if (previous_block_index != previous_loaded_block) {
                status = this->ReadRelativeBlock(inode.direct_blocks[previous_block_index],
                                                 previous_block);
                if (status != FileSystemStatus::Succeeded) {
                    return status;
                }
                previous_loaded_block = previous_block_index;
            }
            const uint64_t previous_offset =
                (previous_index % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
                OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
            FileSystemDirectoryEntry previous_entry{};
            if (!FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
                    previous_block + previous_offset,
                    OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES, previous_entry))) {
                return FileSystemStatus::Corrupt;
            }
            if (previous_entry.name_length_bytes == entry.name_length_bytes &&
                BytesAreEqual(previous_entry.name, entry.name, entry.name_length_bytes)) {
                return FileSystemStatus::Corrupt;
            }
        }
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::CheckConsistencyUnlocked() noexcept {
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if (this->superblock_.transaction_state != FileSystemTransactionState::Clean) {
        return FileSystemStatus::IncompleteTransaction;
    }
    uint8_t inode_bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint8_t data_bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint8_t seen_data_bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(true, inode_bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    status = this->ReadBitmap(false, data_bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (!this->BitmapBitIsSet(inode_bitmap, OS_KERNEL_FILE_SYSTEM_RESERVED_INODE_NUMBER) ||
        !this->BitmapBitIsSet(inode_bitmap, OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER)) {
        return FileSystemStatus::Corrupt;
    }
    for (uint64_t bit_index = OS_KERNEL_FILE_SYSTEM_INODE_COUNT;
         bit_index < OS_KERNEL_FILE_SYSTEM_UNUSED_BITMAP_BIT_COUNT; ++bit_index) {
        if (this->BitmapBitIsSet(inode_bitmap, bit_index)) {
            return FileSystemStatus::Corrupt;
        }
    }
    for (uint64_t bit_index = OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT;
         bit_index < OS_KERNEL_FILE_SYSTEM_UNUSED_BITMAP_BIT_COUNT; ++bit_index) {
        if (this->BitmapBitIsSet(data_bitmap, bit_index)) {
            return FileSystemStatus::Corrupt;
        }
    }

    uint64_t file_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    uint64_t directory_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    uint64_t allocated_inode_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    for (uint64_t inode_number = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
         inode_number < OS_KERNEL_FILE_SYSTEM_INODE_COUNT; ++inode_number) {
        if (!this->BitmapBitIsSet(inode_bitmap, inode_number)) {
            continue;
        }
        ++allocated_inode_count;
        status = this->ValidateAllocatedInode(inode_number, inode_bitmap, data_bitmap,
                                              seen_data_bitmap, file_count, directory_count);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }
    FileSystemInode root_inode{};
    status = this->ReadInode(OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER, root_inode);
    if (status != FileSystemStatus::Succeeded || root_inode.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::Corrupt;
    }

    // 从根目录执行有界广度优先遍历，证明每个已分配 inode 都确实可达。
    // 当前格式没有硬链接，因此第二次遇到同一 inode 同时表示重复引用或目录环。
    uint8_t reachable_inode_bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint64_t pending_inodes[OS_KERNEL_FILE_SYSTEM_INODE_COUNT]{};
    uint64_t pending_read_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    uint64_t pending_write_index = OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT;
    pending_inodes[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
    this->SetBitmapBit(reachable_inode_bitmap, OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER, true);
    while (pending_read_index < pending_write_index) {
        const uint64_t current_inode_number = pending_inodes[pending_read_index];
        ++pending_read_index;
        FileSystemInode current_inode{};
        status = this->ReadInode(current_inode_number, current_inode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        if (current_inode.link_count != OS_KERNEL_FILE_SYSTEM_INITIAL_LINK_COUNT) {
            return FileSystemStatus::Corrupt;
        }
        if (current_inode.type != FileSystemNodeType::Directory) {
            continue;
        }
        const uint64_t entry_count =
            current_inode.size_bytes / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
        uint8_t directory_block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
        uint64_t loaded_block_index = OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
        for (uint64_t entry_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; entry_index < entry_count;
             ++entry_index) {
            const uint64_t block_index =
                entry_index / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
            if (block_index != loaded_block_index) {
                status = this->ReadRelativeBlock(current_inode.direct_blocks[block_index],
                                                 directory_block);
                if (status != FileSystemStatus::Succeeded) {
                    return status;
                }
                loaded_block_index = block_index;
            }
            const uint64_t entry_offset =
                (entry_index % OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
                OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
            FileSystemDirectoryEntry entry{};
            if (!FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
                    directory_block + entry_offset,
                    OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES, entry)) ||
                this->BitmapBitIsSet(reachable_inode_bitmap, entry.inode_number) ||
                pending_write_index >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT) {
                return FileSystemStatus::Corrupt;
            }
            this->SetBitmapBit(reachable_inode_bitmap, entry.inode_number, true);
            pending_inodes[pending_write_index] = entry.inode_number;
            ++pending_write_index;
        }
    }
    for (uint64_t inode_number = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
         inode_number < OS_KERNEL_FILE_SYSTEM_INODE_COUNT; ++inode_number) {
        if (this->BitmapBitIsSet(inode_bitmap, inode_number) !=
            this->BitmapBitIsSet(reachable_inode_bitmap, inode_number)) {
            return FileSystemStatus::Corrupt;
        }
    }

    uint64_t allocated_data_block_count = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    for (uint64_t data_block_index = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         data_block_index < OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT; ++data_block_index) {
        const bool allocated = this->BitmapBitIsSet(data_bitmap, data_block_index);
        if (allocated != this->BitmapBitIsSet(seen_data_bitmap, data_block_index)) {
            return FileSystemStatus::Corrupt;
        }
        if (allocated) {
            ++allocated_data_block_count;
        }
    }
    this->statistics_.transaction_generation = this->superblock_.transaction_generation;
    this->statistics_.allocated_inode_count = allocated_inode_count;
    this->statistics_.allocated_data_block_count = allocated_data_block_count;
    this->statistics_.mounted_file_count = file_count;
    this->statistics_.mounted_directory_count = directory_count;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::CheckConsistency() noexcept {
    SpinLockGuard guard{this->lock_};
    return this->CheckConsistencyUnlocked();
}

FileSystemStatistics FileSystem::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    FileSystemStatistics statistics = this->statistics_;
    statistics.cache = this->cache_.Statistics();
    statistics.transaction_generation = this->superblock_.transaction_generation;
    return statistics;
}

}
