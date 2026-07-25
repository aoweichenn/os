#include "os/kernel/file_system.hpp"

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

void ClearBytes(uint8_t *bytes, const uint64_t byteCount) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; byteIndex < byteCount;
         ++byteIndex) {
        bytes[byteIndex] = OS_KERNEL_FILE_SYSTEM_ZERO_BYTE;
    }
}

void CopyBytes(uint8_t *destination, const uint8_t *source,
               const uint64_t byteCount) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; byteIndex < byteCount;
         ++byteIndex) {
        destination[byteIndex] = source[byteIndex];
    }
}

[[nodiscard]] bool BytesAreEqual(const uint8_t *left, const uint8_t *right,
                                 const uint64_t byteCount) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE; byteIndex < byteCount;
         ++byteIndex) {
        if (left[byteIndex] != right[byteIndex]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] uint64_t RequiredBlockCount(const uint64_t sizeBytes) noexcept {
    if (sizeBytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    }
    return (sizeBytes + OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES -
            OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT) /
           OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
}

[[nodiscard]] uint64_t RelativeToLogicalBlockAddress(
    const uint64_t relativeBlock) noexcept {
    return OS_KERNEL_FILE_SYSTEM_START_LBA + relativeBlock;
}

[[nodiscard]] bool FileSystemFormatSucceeded(
    const FileSystemFormatStatus status) noexcept {
    return status == FileSystemFormatStatus::Succeeded;
}

[[nodiscard]] bool DirectoryNameIsValid(
    const FileSystemDirectoryEntry &entry) noexcept {
    if (entry.nameLengthBytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE ||
        entry.nameLengthBytes >
            OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         byteIndex < entry.nameLengthBytes; ++byteIndex) {
        const uint8_t nameByte = entry.name[byteIndex];
        if (nameByte <= OS_KERNEL_FILE_SYSTEM_MAXIMUM_CONTROL_CHARACTER ||
            nameByte == OS_KERNEL_FILE_SYSTEM_DELETE_CONTROL_CHARACTER ||
            nameByte == OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
            return false;
        }
    }
    const bool isDot =
        entry.nameLengthBytes == OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT &&
        entry.name[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] ==
            OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER;
    const bool isDotDot =
        entry.nameLengthBytes ==
            OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT +
                OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT &&
        entry.name[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] ==
            OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER &&
        entry.name[OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT] ==
            OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER;
    return !isDot && !isDotDot;
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

    uint8_t superblockBytes[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    if (device.ReadBlock(
            RelativeToLogicalBlockAddress(
                OS_KERNEL_FILE_SYSTEM_SUPERBLOCK_RELATIVE_BLOCK),
            superblockBytes, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
        FileSystemBlockDeviceStatus::Succeeded) {
        return this->FailDeviceOperation();
    }

    if (FileSystemBlockIsZero(superblockBytes,
                              OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES)) {
        const FileSystemStatus formatStatus = this->Format(device);
        if (formatStatus != FileSystemStatus::Succeeded) {
            return formatStatus;
        }
        formatted = true;
        this->statistics_.formattedDuringMount = true;
        return FileSystemStatus::Succeeded;
    }

    if (!FileSystemFormatSucceeded(DecodeFileSystemSuperblock(
            superblockBytes, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES,
            this->superblock_))) {
        return FileSystemStatus::Corrupt;
    }
    if (this->superblock_.transactionState ==
        FileSystemTransactionState::Dirty) {
        return FileSystemStatus::IncompleteTransaction;
    }

    this->cache_.Initialize(device);
    this->initialized_ = true;
    const FileSystemStatus consistencyStatus =
        this->CheckConsistencyUnlocked();
    if (consistencyStatus != FileSystemStatus::Succeeded) {
        this->initialized_ = false;
        return consistencyStatus;
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
    FileSystemStatus status =
        this->WriteBitmap(true, block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }

    ClearBytes(block, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES);
    for (uint64_t inodeTableBlockIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         inodeTableBlockIndex <
         OS_KERNEL_FILE_SYSTEM_INODE_TABLE_BLOCK_COUNT;
         ++inodeTableBlockIndex) {
        status = this->WriteRelativeBlock(
            OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK +
                inodeTableBlockIndex,
            block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }

    FileSystemInode rootInode{
        .type = FileSystemNodeType::Directory,
        .sizeBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .generation = this->superblock_.transactionGeneration,
        .linkCount = OS_KERNEL_FILE_SYSTEM_INITIAL_LINK_COUNT,
        .allocatedBlockCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .directBlocks = {},
    };
    status =
        this->WriteInode(OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER, rootInode);
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
    if (this->superblock_.transactionState !=
        FileSystemTransactionState::Clean) {
        return FileSystemStatus::IncompleteTransaction;
    }
    ++this->superblock_.transactionGeneration;
    this->superblock_.transactionState = FileSystemTransactionState::Dirty;
    return this->WriteSuperblockDirect();
}

FileSystemStatus FileSystem::CommitTransaction() noexcept {
    if (this->cache_.Sync() != BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    this->superblock_.transactionState = FileSystemTransactionState::Clean;
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
            this->superblock_, block,
            OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES))) {
        return FileSystemStatus::Corrupt;
    }
    if (this->device_->WriteBlock(
            RelativeToLogicalBlockAddress(
                OS_KERNEL_FILE_SYSTEM_SUPERBLOCK_RELATIVE_BLOCK),
            block, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
        FileSystemBlockDeviceStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    if (this->device_->Flush() !=
        FileSystemBlockDeviceStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ReadRelativeBlock(const uint64_t relativeBlock,
                                               uint8_t *block) noexcept {
    if (block == nullptr ||
        relativeBlock >= OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT) {
        return FileSystemStatus::InvalidArgument;
    }
    if (this->cache_.ReadBlock(RelativeToLogicalBlockAddress(relativeBlock),
                               block,
                               OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
        BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::WriteRelativeBlock(const uint64_t relativeBlock,
                                                const uint8_t *block) noexcept {
    if (block == nullptr ||
        relativeBlock >= OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT) {
        return FileSystemStatus::InvalidArgument;
    }
    if (this->cache_.WriteBlock(RelativeToLogicalBlockAddress(relativeBlock),
                                block,
                                OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
        BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ReadInode(const uint64_t inodeNumber,
                                       FileSystemInode &inode) noexcept {
    if (inodeNumber >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT) {
        return FileSystemStatus::Corrupt;
    }
    const uint64_t relativeBlock =
        OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK +
        inodeNumber / OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK;
    const uint64_t inodeOffset =
        (inodeNumber % OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK) *
        OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES;
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadRelativeBlock(relativeBlock, block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (!FileSystemFormatSucceeded(DecodeFileSystemInode(
            block + inodeOffset, OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES,
            inode))) {
        return FileSystemStatus::Corrupt;
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::WriteInode(const uint64_t inodeNumber,
                                        const FileSystemInode &inode) noexcept {
    if (inodeNumber >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT) {
        return FileSystemStatus::Corrupt;
    }
    const uint64_t relativeBlock =
        OS_KERNEL_FILE_SYSTEM_INODE_TABLE_START_RELATIVE_BLOCK +
        inodeNumber / OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK;
    const uint64_t inodeOffset =
        (inodeNumber % OS_KERNEL_FILE_SYSTEM_INODES_PER_BLOCK) *
        OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES;
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadRelativeBlock(relativeBlock, block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (!FileSystemFormatSucceeded(EncodeFileSystemInode(
            inode, block + inodeOffset,
            OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES))) {
        return FileSystemStatus::Corrupt;
    }
    return this->WriteRelativeBlock(relativeBlock, block);
}

FileSystemStatus FileSystem::ReadBitmap(const bool inodeBitmap,
                                        uint8_t *block) noexcept {
    return this->ReadRelativeBlock(
        inodeBitmap ? OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK
                    : OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK,
        block);
}

FileSystemStatus FileSystem::WriteBitmap(const bool inodeBitmap,
                                         const uint8_t *block) noexcept {
    return this->WriteRelativeBlock(
        inodeBitmap ? OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK
                    : OS_KERNEL_FILE_SYSTEM_DATA_BITMAP_RELATIVE_BLOCK,
        block);
}

bool FileSystem::BitmapBitIsSet(const uint8_t *bitmap,
                                const uint64_t bitIndex) const noexcept {
    const uint64_t byteIndex = bitIndex / OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
    const uint64_t bitOffset = bitIndex % OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
    const uint8_t mask = static_cast<uint8_t>(
        static_cast<uint64_t>(OS_KERNEL_FILE_SYSTEM_FIRST_BIT_MASK)
        << bitOffset);
    return (bitmap[byteIndex] & mask) != OS_KERNEL_FILE_SYSTEM_ZERO_BYTE;
}

void FileSystem::SetBitmapBit(uint8_t *bitmap, const uint64_t bitIndex,
                              const bool allocated) const noexcept {
    const uint64_t byteIndex = bitIndex / OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
    const uint64_t bitOffset = bitIndex % OS_KERNEL_FILE_SYSTEM_BITS_PER_BYTE;
    const uint8_t mask = static_cast<uint8_t>(
        static_cast<uint64_t>(OS_KERNEL_FILE_SYSTEM_FIRST_BIT_MASK)
        << bitOffset);
    if (allocated) {
        bitmap[byteIndex] = static_cast<uint8_t>(bitmap[byteIndex] | mask);
    } else {
        bitmap[byteIndex] =
            static_cast<uint8_t>(bitmap[byteIndex] &
                                 static_cast<uint8_t>(~mask));
    }
}

FileSystemStatus FileSystem::FindFreeBitmapBit(
    const uint8_t *bitmap, const uint64_t firstBit, const uint64_t bitCount,
    uint64_t &bitIndex) const noexcept {
    if (bitmap == nullptr || firstBit > bitCount) {
        return FileSystemStatus::InvalidArgument;
    }
    for (uint64_t candidateBit = firstBit; candidateBit < bitCount;
         ++candidateBit) {
        if (!this->BitmapBitIsSet(bitmap, candidateBit)) {
            bitIndex = candidateBit;
            return FileSystemStatus::Succeeded;
        }
    }
    return FileSystemStatus::NotFound;
}

FileSystemStatus FileSystem::AllocateInode(uint64_t &inodeNumber) noexcept {
    uint8_t bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(true, bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    status = this->FindFreeBitmapBit(
        bitmap, OS_KERNEL_FILE_SYSTEM_FIRST_ALLOCATABLE_INODE_NUMBER,
        OS_KERNEL_FILE_SYSTEM_INODE_COUNT, inodeNumber);
    if (status == FileSystemStatus::NotFound) {
        return FileSystemStatus::InodeCapacityExhausted;
    }
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    this->SetBitmapBit(bitmap, inodeNumber, true);
    return this->WriteBitmap(true, bitmap);
}

FileSystemStatus FileSystem::AllocateDataBlock(
    uint64_t &relativeBlock) noexcept {
    uint8_t bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(false, bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t dataBlockIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    status = this->FindFreeBitmapBit(
        bitmap, OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT, dataBlockIndex);
    if (status == FileSystemStatus::NotFound) {
        return FileSystemStatus::DataCapacityExhausted;
    }
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    this->SetBitmapBit(bitmap, dataBlockIndex, true);
    status = this->WriteBitmap(false, bitmap);
    if (status == FileSystemStatus::Succeeded) {
        relativeBlock =
            OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK + dataBlockIndex;
    }
    return status;
}

FileSystemStatus FileSystem::ReleaseInodeDataBlocks(
    FileSystemInode &inode) noexcept {
    uint8_t bitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(false, bitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    for (uint64_t blockIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         blockIndex < inode.allocatedBlockCount; ++blockIndex) {
        const uint64_t relativeBlock = inode.directBlocks[blockIndex];
        if (relativeBlock < OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK ||
            relativeBlock >= OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT) {
            return FileSystemStatus::Corrupt;
        }
        this->SetBitmapBit(
            bitmap,
            relativeBlock - OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK,
            false);
        inode.directBlocks[blockIndex] =
            OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    }
    inode.allocatedBlockCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    inode.sizeBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    return this->WriteBitmap(false, bitmap);
}

FileSystemStatus FileSystem::ParsePath(
    const uint8_t *path, const uint64_t pathLengthBytes,
    PathComponent *components, const uint64_t componentCapacity,
    uint64_t &componentCount) const noexcept {
    componentCount = OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT;
    if (path == nullptr || components == nullptr) {
        return FileSystemStatus::InvalidArgument;
    }
    if (pathLengthBytes < OS_KERNEL_FILE_SYSTEM_MINIMUM_PATH_LENGTH_BYTES ||
        path[OS_KERNEL_FILE_SYSTEM_FIRST_PATH_BYTE_INDEX] !=
            OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
        return FileSystemStatus::InvalidPath;
    }
    if (pathLengthBytes >
        OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_LENGTH_BYTES) {
        return FileSystemStatus::PathTooLong;
    }
    if (pathLengthBytes == OS_KERNEL_FILE_SYSTEM_MINIMUM_PATH_LENGTH_BYTES) {
        return FileSystemStatus::Succeeded;
    }
    if (path[pathLengthBytes - OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT] ==
        OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
        return FileSystemStatus::InvalidPath;
    }

    uint64_t byteIndex = OS_KERNEL_FILE_SYSTEM_FIRST_COMPONENT_BYTE_INDEX;
    while (byteIndex < pathLengthBytes) {
        if (componentCount >= componentCapacity) {
            return FileSystemStatus::PathTooLong;
        }
        PathComponent &component = components[componentCount];
        component = PathComponent{};
        while (byteIndex < pathLengthBytes &&
               path[byteIndex] != OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
            const uint8_t pathByte = path[byteIndex];
            if (pathByte <= OS_KERNEL_FILE_SYSTEM_MAXIMUM_CONTROL_CHARACTER ||
                pathByte ==
                    OS_KERNEL_FILE_SYSTEM_DELETE_CONTROL_CHARACTER) {
                return FileSystemStatus::InvalidPath;
            }
            if (component.lengthBytes >=
                OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES) {
                return FileSystemStatus::NameTooLong;
            }
            component.bytes[component.lengthBytes] = pathByte;
            ++component.lengthBytes;
            ++byteIndex;
        }
        if (component.lengthBytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
            return FileSystemStatus::InvalidPath;
        }
        const bool currentComponentIsDot =
            component.lengthBytes == OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT &&
            component.bytes[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] ==
                OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER;
        const bool currentComponentIsDotDot =
            component.lengthBytes ==
                OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT +
                    OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT &&
            component.bytes[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] ==
                OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER &&
            component.bytes[OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT] ==
                OS_KERNEL_FILE_SYSTEM_DOT_CHARACTER;
        if (currentComponentIsDot || currentComponentIsDotDot) {
            return FileSystemStatus::InvalidPath;
        }
        ++componentCount;
        if (byteIndex < pathLengthBytes) {
            ++byteIndex;
            if (byteIndex == pathLengthBytes ||
                path[byteIndex] == OS_KERNEL_FILE_SYSTEM_PATH_SEPARATOR) {
                return FileSystemStatus::InvalidPath;
            }
        }
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::FindDirectoryEntry(
    const FileSystemInode &directory, const PathComponent &name,
    DirectoryEntryLocation &location) noexcept {
    if (directory.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::NotDirectory;
    }
    if ((directory.sizeBytes %
         OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) !=
        OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::Corrupt;
    }
    const uint64_t entryCount =
        directory.sizeBytes /
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint64_t loadedBlockIndex = OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
    for (uint64_t entryIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         entryIndex < entryCount; ++entryIndex) {
        const uint64_t blockIndex =
            entryIndex /
            OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
        if (blockIndex != loadedBlockIndex) {
            if (blockIndex >= directory.allocatedBlockCount) {
                return FileSystemStatus::Corrupt;
            }
            FileSystemStatus status =
                this->ReadRelativeBlock(directory.directBlocks[blockIndex],
                                        block);
            if (status != FileSystemStatus::Succeeded) {
                return status;
            }
            loadedBlockIndex = blockIndex;
        }
        const uint64_t entryOffset =
            (entryIndex %
             OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
            OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
        FileSystemDirectoryEntry entry{};
        if (!FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
                block + entryOffset,
                OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES, entry))) {
            return FileSystemStatus::Corrupt;
        }
        if (entry.nameLengthBytes == name.lengthBytes &&
            BytesAreEqual(entry.name, name.bytes, name.lengthBytes)) {
            location.entryIndex = entryIndex;
            location.entry = entry;
            return FileSystemStatus::Succeeded;
        }
    }
    return FileSystemStatus::NotFound;
}

FileSystemStatus FileSystem::ResolvePath(
    const PathComponent *components, const uint64_t componentCount,
    uint64_t &inodeNumber, FileSystemInode &inode) noexcept {
    if (components == nullptr &&
        componentCount != OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT) {
        return FileSystemStatus::InvalidArgument;
    }
    inodeNumber = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
    FileSystemStatus status = this->ReadInode(inodeNumber, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    for (uint64_t componentIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         componentIndex < componentCount; ++componentIndex) {
        if (inode.type != FileSystemNodeType::Directory) {
            return FileSystemStatus::NotDirectory;
        }
        DirectoryEntryLocation location{};
        status = this->FindDirectoryEntry(
            inode, components[componentIndex], location);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        inodeNumber = location.entry.inodeNumber;
        status = this->ReadInode(inodeNumber, inode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        if (inode.type != location.entry.type) {
            return FileSystemStatus::Corrupt;
        }
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ResolveParent(
    const PathComponent *components, const uint64_t componentCount,
    uint64_t &parentInodeNumber, FileSystemInode &parentInode) noexcept {
    if (components == nullptr ||
        componentCount == OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT) {
        return FileSystemStatus::InvalidPath;
    }
    return this->ResolvePath(
        components,
        componentCount - OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT,
        parentInodeNumber, parentInode);
}

FileSystemStatus FileSystem::AppendDirectoryEntry(
    const uint64_t directoryInodeNumber, FileSystemInode &directory,
    const FileSystemDirectoryEntry &entry) noexcept {
    if (directory.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::NotDirectory;
    }
    const uint64_t entryIndex =
        directory.sizeBytes /
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    if (entryIndex >=
        OS_KERNEL_FILE_SYSTEM_MAXIMUM_DIRECTORY_ENTRY_COUNT) {
        return FileSystemStatus::DirectoryCapacityExhausted;
    }
    const uint64_t blockIndex =
        entryIndex / OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    if (blockIndex >= directory.allocatedBlockCount) {
        uint64_t relativeBlock = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
        FileSystemStatus status =
            this->AllocateDataBlock(relativeBlock);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        directory.directBlocks[blockIndex] = relativeBlock;
        ++directory.allocatedBlockCount;
        ClearBytes(block, OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES);
    } else {
        FileSystemStatus status =
            this->ReadRelativeBlock(directory.directBlocks[blockIndex],
                                    block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }

    const uint64_t entryOffset =
        (entryIndex %
         OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    if (!FileSystemFormatSucceeded(EncodeFileSystemDirectoryEntry(
            entry, block + entryOffset,
            OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES))) {
        return FileSystemStatus::Corrupt;
    }
    FileSystemStatus status =
        this->WriteRelativeBlock(directory.directBlocks[blockIndex], block);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    directory.sizeBytes +=
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    directory.generation = this->superblock_.transactionGeneration;
    return this->WriteInode(directoryInodeNumber, directory);
}

FileSystemStatus FileSystem::CreateNode(
    const PathComponent *components, const uint64_t componentCount,
    const FileSystemNodeType type, uint64_t &inodeNumber) noexcept {
    if (components == nullptr ||
        componentCount == OS_KERNEL_FILE_SYSTEM_ROOT_COMPONENT_COUNT ||
        (type != FileSystemNodeType::RegularFile &&
         type != FileSystemNodeType::Directory)) {
        return FileSystemStatus::InvalidArgument;
    }
    uint64_t parentInodeNumber = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemInode parentInode{};
    FileSystemStatus status =
        this->ResolveParent(components, componentCount, parentInodeNumber,
                            parentInode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (parentInode.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::NotDirectory;
    }
    DirectoryEntryLocation existing{};
    status = this->FindDirectoryEntry(
        parentInode,
        components[componentCount - OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT],
        existing);
    if (status == FileSystemStatus::Succeeded) {
        return FileSystemStatus::AlreadyExists;
    }
    if (status != FileSystemStatus::NotFound) {
        return status;
    }
    const uint64_t parentEntryCount =
        parentInode.sizeBytes /
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    if (parentEntryCount >=
        OS_KERNEL_FILE_SYSTEM_MAXIMUM_DIRECTORY_ENTRY_COUNT) {
        return FileSystemStatus::DirectoryCapacityExhausted;
    }

    // 在把超级块标记为脏之前完成容量预检，避免“空间不足”被误判成崩溃事务。
    uint8_t inodeBitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    status = this->ReadBitmap(true, inodeBitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t availableInode = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    status = this->FindFreeBitmapBit(
        inodeBitmap, OS_KERNEL_FILE_SYSTEM_FIRST_ALLOCATABLE_INODE_NUMBER,
        OS_KERNEL_FILE_SYSTEM_INODE_COUNT, availableInode);
    if (status == FileSystemStatus::NotFound) {
        return FileSystemStatus::InodeCapacityExhausted;
    }
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    const bool parentNeedsDataBlock =
        (parentEntryCount %
         OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) ==
        OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (parentNeedsDataBlock) {
        uint8_t dataBitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
        status = this->ReadBitmap(false, dataBitmap);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        uint64_t unusedDataBit = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
        status = this->FindFreeBitmapBit(
            dataBitmap, OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
            OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT, unusedDataBit);
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
    status = this->AllocateInode(inodeNumber);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inodeNumber != availableInode) {
        return FileSystemStatus::Corrupt;
    }
    const FileSystemInode inode{
        .type = type,
        .sizeBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .generation = this->superblock_.transactionGeneration,
        .linkCount = OS_KERNEL_FILE_SYSTEM_INITIAL_LINK_COUNT,
        .allocatedBlockCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .directBlocks = {},
    };
    status = this->WriteInode(inodeNumber, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }

    const PathComponent &name =
        components[componentCount -
                   OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT];
    FileSystemDirectoryEntry entry{
        .inodeNumber = inodeNumber,
        .type = type,
        .nameLengthBytes = name.lengthBytes,
        .name = {},
    };
    CopyBytes(entry.name, name.bytes, name.lengthBytes);
    status = this->AppendDirectoryEntry(parentInodeNumber, parentInode,
                                        entry);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    return this->CommitTransaction();
}

FileSystemStatus FileSystem::CreateDirectory(
    const uint8_t *path, const uint64_t pathLengthBytes) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    PathComponent components[OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT]{};
    uint64_t componentCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemStatus status = this->ParsePath(
        path, pathLengthBytes, components,
        OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT, componentCount);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t inodeNumber = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    return this->CreateNode(components, componentCount,
                            FileSystemNodeType::Directory, inodeNumber);
}

FileSystemStatus FileSystem::Open(
    const uint8_t *path, const uint64_t pathLengthBytes,
    const FileSystemOpenOptions &options, FileSystemHandle &handle) noexcept {
    SpinLockGuard guard{this->lock_};
    handle = FileSystemHandle{};
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if ((!options.readable && !options.writable) ||
        (options.truncate && !options.writable)) {
        return FileSystemStatus::InvalidArgument;
    }
    PathComponent components[OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT]{};
    uint64_t componentCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemStatus status = this->ParsePath(
        path, pathLengthBytes, components,
        OS_KERNEL_FILE_SYSTEM_MAXIMUM_PATH_COMPONENT_COUNT, componentCount);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t inodeNumber = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    FileSystemInode inode{};
    status =
        this->ResolvePath(components, componentCount, inodeNumber, inode);
    if (status == FileSystemStatus::NotFound && options.create) {
        status = this->CreateNode(components, componentCount,
                                  FileSystemNodeType::RegularFile,
                                  inodeNumber);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        status = this->ReadInode(inodeNumber, inode);
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
    if (options.truncate && inode.sizeBytes != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        status = this->BeginTransaction();
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        status = this->ReleaseInodeDataBlocks(inode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        inode.generation = this->superblock_.transactionGeneration;
        status = this->WriteInode(inodeNumber, inode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        status = this->CommitTransaction();
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }
    handle = FileSystemHandle{
        .inodeNumber = inodeNumber,
        .offsetBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE,
        .readable = options.readable,
        .writable = options.writable,
        .open = true,
    };
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::ReadFileBytes(
    const FileSystemInode &inode, const uint64_t offsetBytes,
    uint8_t *destination, const uint64_t capacityBytes,
    uint64_t &readBytes) noexcept {
    readBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (offsetBytes >= inode.sizeBytes ||
        capacityBytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::Succeeded;
    }
    const uint64_t availableBytes = inode.sizeBytes - offsetBytes;
    const uint64_t requestedBytes = Minimum(availableBytes, capacityBytes);
    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    while (readBytes < requestedBytes) {
        const uint64_t absoluteOffset = offsetBytes + readBytes;
        const uint64_t blockIndex =
            absoluteOffset / OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
        const uint64_t blockOffset =
            absoluteOffset % OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
        if (blockIndex >= inode.allocatedBlockCount) {
            return FileSystemStatus::Corrupt;
        }
        FileSystemStatus status =
            this->ReadRelativeBlock(inode.directBlocks[blockIndex], block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        const uint64_t chunkBytes = Minimum(
            requestedBytes - readBytes,
            OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES - blockOffset);
        CopyBytes(destination + readBytes, block + blockOffset, chunkBytes);
        readBytes += chunkBytes;
    }
    return FileSystemStatus::Succeeded;
}

FileSystemStatus FileSystem::Read(FileSystemHandle &handle,
                                  uint8_t *destination,
                                  const uint64_t capacityBytes,
                                  uint64_t &readBytes) noexcept {
    SpinLockGuard guard{this->lock_};
    readBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if (!handle.open) {
        return FileSystemStatus::InvalidHandle;
    }
    if (!handle.readable) {
        return FileSystemStatus::PermissionDenied;
    }
    if (destination == nullptr &&
        capacityBytes != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::InvalidArgument;
    }
    FileSystemInode inode{};
    FileSystemStatus status =
        this->ReadInode(handle.inodeNumber, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type != FileSystemNodeType::RegularFile) {
        return FileSystemStatus::IsDirectory;
    }
    status = this->ReadFileBytes(inode, handle.offsetBytes, destination,
                                 capacityBytes, readBytes);
    if (status == FileSystemStatus::Succeeded) {
        handle.offsetBytes += readBytes;
        this->statistics_.bytesRead += readBytes;
    }
    return status;
}

FileSystemStatus FileSystem::WriteFileBytes(
    const uint64_t inodeNumber, FileSystemInode &inode,
    const uint64_t offsetBytes, const uint8_t *source,
    const uint64_t lengthBytes, uint64_t &writtenBytes) noexcept {
    writtenBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    const uint64_t finalSize =
        offsetBytes + lengthBytes > inode.sizeBytes
            ? offsetBytes + lengthBytes
            : inode.sizeBytes;
    const uint64_t neededBlockCount = RequiredBlockCount(finalSize);
    while (inode.allocatedBlockCount < neededBlockCount) {
        uint64_t relativeBlock = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
        FileSystemStatus status =
            this->AllocateDataBlock(relativeBlock);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        inode.directBlocks[inode.allocatedBlockCount] = relativeBlock;
        ++inode.allocatedBlockCount;
        uint8_t zeroBlock[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
        status = this->WriteRelativeBlock(relativeBlock, zeroBlock);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }

    uint8_t block[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    while (writtenBytes < lengthBytes) {
        const uint64_t absoluteOffset = offsetBytes + writtenBytes;
        const uint64_t blockIndex =
            absoluteOffset / OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
        const uint64_t blockOffset =
            absoluteOffset % OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES;
        FileSystemStatus status =
            this->ReadRelativeBlock(inode.directBlocks[blockIndex], block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        const uint64_t chunkBytes = Minimum(
            lengthBytes - writtenBytes,
            OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES - blockOffset);
        CopyBytes(block + blockOffset, source + writtenBytes, chunkBytes);
        status =
            this->WriteRelativeBlock(inode.directBlocks[blockIndex], block);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        writtenBytes += chunkBytes;
    }
    inode.sizeBytes = finalSize;
    inode.generation = this->superblock_.transactionGeneration;
    return this->WriteInode(inodeNumber, inode);
}

FileSystemStatus FileSystem::Write(FileSystemHandle &handle,
                                   const uint8_t *source,
                                   const uint64_t lengthBytes,
                                   uint64_t &writtenBytes) noexcept {
    SpinLockGuard guard{this->lock_};
    writtenBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    if (!this->initialized_) {
        return FileSystemStatus::NotInitialized;
    }
    if (!handle.open) {
        return FileSystemStatus::InvalidHandle;
    }
    if (!handle.writable) {
        return FileSystemStatus::PermissionDenied;
    }
    if (source == nullptr &&
        lengthBytes != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::InvalidArgument;
    }
    if (lengthBytes == OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        return FileSystemStatus::Succeeded;
    }
    if (handle.offsetBytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES ||
        lengthBytes >
            OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES -
                handle.offsetBytes) {
        return FileSystemStatus::FileTooLarge;
    }
    FileSystemInode inode{};
    FileSystemStatus status =
        this->ReadInode(handle.inodeNumber, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type != FileSystemNodeType::RegularFile) {
        return FileSystemStatus::IsDirectory;
    }

    const uint64_t finalSize =
        handle.offsetBytes + lengthBytes > inode.sizeBytes
            ? handle.offsetBytes + lengthBytes
            : inode.sizeBytes;
    const uint64_t neededBlockCount = RequiredBlockCount(finalSize);
    const uint64_t additionalBlockCount =
        neededBlockCount - inode.allocatedBlockCount;
    if (additionalBlockCount != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
        uint8_t dataBitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
        status = this->ReadBitmap(false, dataBitmap);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        uint64_t freeBlockCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
        for (uint64_t dataBlockIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
             dataBlockIndex < OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT;
             ++dataBlockIndex) {
            if (!this->BitmapBitIsSet(dataBitmap, dataBlockIndex)) {
                ++freeBlockCount;
            }
        }
        if (freeBlockCount < additionalBlockCount) {
            return FileSystemStatus::DataCapacityExhausted;
        }
    }

    status = this->BeginTransaction();
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t transactionWrittenBytes = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    status = this->WriteFileBytes(
        handle.inodeNumber, inode, handle.offsetBytes, source, lengthBytes,
        transactionWrittenBytes);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    status = this->CommitTransaction();
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    handle.offsetBytes += transactionWrittenBytes;
    writtenBytes = transactionWrittenBytes;
    this->statistics_.bytesWritten += transactionWrittenBytes;
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

FileSystemStatus FileSystem::ValidateAllocatedInode(
    const uint64_t inodeNumber, const uint8_t *inodeBitmap,
    const uint8_t *dataBitmap, uint8_t *seenDataBitmap,
    uint64_t &fileCount, uint64_t &directoryCount) noexcept {
    FileSystemInode inode{};
    FileSystemStatus status = this->ReadInode(inodeNumber, inode);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (inode.type == FileSystemNodeType::Unused ||
        inode.allocatedBlockCount != RequiredBlockCount(inode.sizeBytes)) {
        return FileSystemStatus::Corrupt;
    }
    if (inode.type == FileSystemNodeType::Directory) {
        if ((inode.sizeBytes %
             OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) !=
            OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
            return FileSystemStatus::Corrupt;
        }
        ++directoryCount;
    } else if (inode.type == FileSystemNodeType::RegularFile) {
        ++fileCount;
    } else {
        return FileSystemStatus::Corrupt;
    }

    for (uint64_t blockIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         blockIndex < OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
         ++blockIndex) {
        const uint64_t relativeBlock = inode.directBlocks[blockIndex];
        if (blockIndex < inode.allocatedBlockCount) {
            if (relativeBlock <
                    OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK ||
                relativeBlock >= OS_KERNEL_FILE_SYSTEM_TOTAL_BLOCK_COUNT) {
                return FileSystemStatus::Corrupt;
            }
            const uint64_t dataBlockIndex =
                relativeBlock -
                OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK;
            if (!this->BitmapBitIsSet(dataBitmap, dataBlockIndex) ||
                this->BitmapBitIsSet(seenDataBitmap, dataBlockIndex)) {
                return FileSystemStatus::Corrupt;
            }
            this->SetBitmapBit(seenDataBitmap, dataBlockIndex, true);
        } else if (relativeBlock != OS_KERNEL_FILE_SYSTEM_ZERO_VALUE) {
            return FileSystemStatus::Corrupt;
        }
    }

    if (inode.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::Succeeded;
    }
    const uint64_t entryCount =
        inode.sizeBytes /
        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
    uint8_t currentBlock[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint8_t previousBlock[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint64_t currentLoadedBlock = OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
    uint64_t previousLoadedBlock = OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
    for (uint64_t entryIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         entryIndex < entryCount; ++entryIndex) {
        const uint64_t blockIndex =
            entryIndex /
            OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
        if (blockIndex != currentLoadedBlock) {
            status = this->ReadRelativeBlock(inode.directBlocks[blockIndex],
                                             currentBlock);
            if (status != FileSystemStatus::Succeeded) {
                return status;
            }
            currentLoadedBlock = blockIndex;
        }
        const uint64_t entryOffset =
            (entryIndex %
             OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
            OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
        FileSystemDirectoryEntry entry{};
        if (!FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
                currentBlock + entryOffset,
                OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES, entry)) ||
            !DirectoryNameIsValid(entry) ||
            entry.inodeNumber >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT ||
            !this->BitmapBitIsSet(inodeBitmap, entry.inodeNumber)) {
            return FileSystemStatus::Corrupt;
        }
        FileSystemInode targetInode{};
        status = this->ReadInode(entry.inodeNumber, targetInode);
        if (status != FileSystemStatus::Succeeded ||
            targetInode.type != entry.type) {
            return FileSystemStatus::Corrupt;
        }

        // 同一目录不允许出现重名项；用小规模二次扫描换取清晰、可验证的不变量。
        for (uint64_t previousIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
             previousIndex < entryIndex; ++previousIndex) {
            const uint64_t previousBlockIndex =
                previousIndex /
                OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
            if (previousBlockIndex != previousLoadedBlock) {
                status = this->ReadRelativeBlock(
                    inode.directBlocks[previousBlockIndex], previousBlock);
                if (status != FileSystemStatus::Succeeded) {
                    return status;
                }
                previousLoadedBlock = previousBlockIndex;
            }
            const uint64_t previousOffset =
                (previousIndex %
                 OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
                OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
            FileSystemDirectoryEntry previousEntry{};
            if (!FileSystemFormatSucceeded(DecodeFileSystemDirectoryEntry(
                    previousBlock + previousOffset,
                    OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES,
                    previousEntry))) {
                return FileSystemStatus::Corrupt;
            }
            if (previousEntry.nameLengthBytes == entry.nameLengthBytes &&
                BytesAreEqual(previousEntry.name, entry.name,
                              entry.nameLengthBytes)) {
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
    if (this->superblock_.transactionState !=
        FileSystemTransactionState::Clean) {
        return FileSystemStatus::IncompleteTransaction;
    }
    uint8_t inodeBitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint8_t dataBitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint8_t seenDataBitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    FileSystemStatus status = this->ReadBitmap(true, inodeBitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    status = this->ReadBitmap(false, dataBitmap);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    if (!this->BitmapBitIsSet(
            inodeBitmap, OS_KERNEL_FILE_SYSTEM_RESERVED_INODE_NUMBER) ||
        !this->BitmapBitIsSet(
            inodeBitmap, OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER)) {
        return FileSystemStatus::Corrupt;
    }
    for (uint64_t bitIndex = OS_KERNEL_FILE_SYSTEM_INODE_COUNT;
         bitIndex < OS_KERNEL_FILE_SYSTEM_UNUSED_BITMAP_BIT_COUNT;
         ++bitIndex) {
        if (this->BitmapBitIsSet(inodeBitmap, bitIndex)) {
            return FileSystemStatus::Corrupt;
        }
    }
    for (uint64_t bitIndex = OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT;
         bitIndex < OS_KERNEL_FILE_SYSTEM_UNUSED_BITMAP_BIT_COUNT;
         ++bitIndex) {
        if (this->BitmapBitIsSet(dataBitmap, bitIndex)) {
            return FileSystemStatus::Corrupt;
        }
    }

    uint64_t fileCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    uint64_t directoryCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    uint64_t allocatedInodeCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    for (uint64_t inodeNumber = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
         inodeNumber < OS_KERNEL_FILE_SYSTEM_INODE_COUNT; ++inodeNumber) {
        if (!this->BitmapBitIsSet(inodeBitmap, inodeNumber)) {
            continue;
        }
        ++allocatedInodeCount;
        status = this->ValidateAllocatedInode(
            inodeNumber, inodeBitmap, dataBitmap, seenDataBitmap, fileCount,
            directoryCount);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
    }
    FileSystemInode rootInode{};
    status = this->ReadInode(OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER,
                             rootInode);
    if (status != FileSystemStatus::Succeeded ||
        rootInode.type != FileSystemNodeType::Directory) {
        return FileSystemStatus::Corrupt;
    }

    // 从根目录执行有界广度优先遍历，证明每个已分配 inode 都确实可达。
    // 当前格式没有硬链接，因此第二次遇到同一 inode 同时表示重复引用或目录环。
    uint8_t reachableInodeBitmap[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    uint64_t pendingInodes[OS_KERNEL_FILE_SYSTEM_INODE_COUNT]{};
    uint64_t pendingReadIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    uint64_t pendingWriteIndex = OS_KERNEL_FILE_SYSTEM_COUNTER_INCREMENT;
    pendingInodes[OS_KERNEL_FILE_SYSTEM_ZERO_VALUE] =
        OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
    this->SetBitmapBit(reachableInodeBitmap,
                       OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER, true);
    while (pendingReadIndex < pendingWriteIndex) {
        const uint64_t currentInodeNumber =
            pendingInodes[pendingReadIndex];
        ++pendingReadIndex;
        FileSystemInode currentInode{};
        status = this->ReadInode(currentInodeNumber, currentInode);
        if (status != FileSystemStatus::Succeeded) {
            return status;
        }
        if (currentInode.linkCount !=
            OS_KERNEL_FILE_SYSTEM_INITIAL_LINK_COUNT) {
            return FileSystemStatus::Corrupt;
        }
        if (currentInode.type != FileSystemNodeType::Directory) {
            continue;
        }
        const uint64_t entryCount =
            currentInode.sizeBytes /
            OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
        uint8_t directoryBlock[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
        uint64_t loadedBlockIndex =
            OS_KERNEL_FILE_SYSTEM_DIRECT_BLOCK_COUNT;
        for (uint64_t entryIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
             entryIndex < entryCount; ++entryIndex) {
            const uint64_t blockIndex =
                entryIndex /
                OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK;
            if (blockIndex != loadedBlockIndex) {
                status = this->ReadRelativeBlock(
                    currentInode.directBlocks[blockIndex], directoryBlock);
                if (status != FileSystemStatus::Succeeded) {
                    return status;
                }
                loadedBlockIndex = blockIndex;
            }
            const uint64_t entryOffset =
                (entryIndex %
                 OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRIES_PER_BLOCK) *
                OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES;
            FileSystemDirectoryEntry entry{};
            if (!FileSystemFormatSucceeded(
                    DecodeFileSystemDirectoryEntry(
                        directoryBlock + entryOffset,
                        OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES,
                        entry)) ||
                this->BitmapBitIsSet(reachableInodeBitmap,
                                     entry.inodeNumber) ||
                pendingWriteIndex >= OS_KERNEL_FILE_SYSTEM_INODE_COUNT) {
                return FileSystemStatus::Corrupt;
            }
            this->SetBitmapBit(reachableInodeBitmap, entry.inodeNumber,
                               true);
            pendingInodes[pendingWriteIndex] = entry.inodeNumber;
            ++pendingWriteIndex;
        }
    }
    for (uint64_t inodeNumber = OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER;
         inodeNumber < OS_KERNEL_FILE_SYSTEM_INODE_COUNT; ++inodeNumber) {
        if (this->BitmapBitIsSet(inodeBitmap, inodeNumber) !=
            this->BitmapBitIsSet(reachableInodeBitmap, inodeNumber)) {
            return FileSystemStatus::Corrupt;
        }
    }

    uint64_t allocatedDataBlockCount = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
    for (uint64_t dataBlockIndex = OS_KERNEL_FILE_SYSTEM_ZERO_VALUE;
         dataBlockIndex < OS_KERNEL_FILE_SYSTEM_DATA_BLOCK_COUNT;
         ++dataBlockIndex) {
        const bool allocated =
            this->BitmapBitIsSet(dataBitmap, dataBlockIndex);
        if (allocated !=
            this->BitmapBitIsSet(seenDataBitmap, dataBlockIndex)) {
            return FileSystemStatus::Corrupt;
        }
        if (allocated) {
            ++allocatedDataBlockCount;
        }
    }
    this->statistics_.transactionGeneration =
        this->superblock_.transactionGeneration;
    this->statistics_.allocatedInodeCount = allocatedInodeCount;
    this->statistics_.allocatedDataBlockCount = allocatedDataBlockCount;
    this->statistics_.mountedFileCount = fileCount;
    this->statistics_.mountedDirectoryCount = directoryCount;
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
    statistics.transactionGeneration =
        this->superblock_.transactionGeneration;
    return statistics;
}

}
