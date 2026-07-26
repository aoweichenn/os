#include "os/kernel/fs/root_file_system.hpp"

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_ROOTFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_MAXIMUM_TRAVERSAL_COUNT = OS_KERNEL_ROOTFS_INODE_COUNT;
constexpr uint64_t OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT =
    OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER;
constexpr uint64_t OS_KERNEL_ROOTFS_SINGLE_INDIRECT_LEVEL = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL = 2ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL = 3ULL;
constexpr uint8_t OS_KERNEL_ROOTFS_ZERO_BYTE = 0U;
constexpr uint8_t OS_KERNEL_ROOTFS_PATH_SEPARATOR = static_cast<uint8_t>('/');
constexpr uint8_t OS_KERNEL_ROOTFS_DOT_CHARACTER = static_cast<uint8_t>('.');
constexpr uint8_t OS_KERNEL_ROOTFS_MAXIMUM_CONTROL_CHARACTER = 0x1FU;
constexpr uint8_t OS_KERNEL_ROOTFS_DELETE_CONTROL_CHARACTER = 0x7FU;

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

void ClearBytes(uint8_t *const destination, const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FIRST_INDEX; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = OS_KERNEL_ROOTFS_ZERO_BYTE;
    }
}

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left, const uint8_t *const right,
                                 const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FIRST_INDEX; byte_index < length_bytes;
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

[[nodiscard]] bool NameIsValid(const uint8_t *const name,
                               const uint64_t name_length_bytes) noexcept {
    if (name == nullptr || name_length_bytes == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        name_length_bytes > OS_KERNEL_ROOTFS_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_FIRST_INDEX; byte_index < name_length_bytes;
         ++byte_index) {
        const uint8_t value = name[byte_index];
        if (value <= OS_KERNEL_ROOTFS_MAXIMUM_CONTROL_CHARACTER ||
            value == OS_KERNEL_ROOTFS_DELETE_CONTROL_CHARACTER ||
            value == OS_KERNEL_ROOTFS_PATH_SEPARATOR) {
            return false;
        }
    }
    const bool dot = name_length_bytes == OS_KERNEL_ROOTFS_COUNTER_INCREMENT &&
                     name[OS_KERNEL_ROOTFS_FIRST_INDEX] == OS_KERNEL_ROOTFS_DOT_CHARACTER;
    const bool dot_dot = name_length_bytes == OS_KERNEL_ROOTFS_COUNTER_INCREMENT +
                                                  OS_KERNEL_ROOTFS_COUNTER_INCREMENT &&
                         name[OS_KERNEL_ROOTFS_FIRST_INDEX] == OS_KERNEL_ROOTFS_DOT_CHARACTER &&
                         name[OS_KERNEL_ROOTFS_COUNTER_INCREMENT] == OS_KERNEL_ROOTFS_DOT_CHARACTER;
    return !dot && !dot_dot;
}

[[nodiscard]] bool BitmapBitIsSet(const uint8_t *const bitmap, const uint64_t bit_index) noexcept {
    const uint64_t byte_index = bit_index / OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint64_t bit_offset = bit_index % OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    return (bitmap[byte_index] & static_cast<uint8_t>(1ULL << bit_offset)) !=
           OS_KERNEL_ROOTFS_ZERO_BYTE;
}

void SetBitmapBit(uint8_t *const bitmap, const uint64_t bit_index, const bool allocated) noexcept {
    const uint64_t byte_index = bit_index / OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint64_t bit_offset = bit_index % OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint8_t mask = static_cast<uint8_t>(1ULL << bit_offset);
    bitmap[byte_index] =
        allocated ? static_cast<uint8_t>(bitmap[byte_index] | mask)
                  : static_cast<uint8_t>(bitmap[byte_index] & static_cast<uint8_t>(~mask));
}

[[nodiscard]] bool PointerBlockIsEmpty(const RootPointerBlock &pointer_block) noexcept {
    for (uint64_t pointer_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         pointer_index < OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK; ++pointer_index) {
        if (pointer_block.pointers[pointer_index] != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint64_t DivideRoundUp(const uint64_t value, const uint64_t divisor) noexcept {
    return value == OS_KERNEL_ROOTFS_EMPTY_VALUE
               ? OS_KERNEL_ROOTFS_EMPTY_VALUE
               : (value - OS_KERNEL_ROOTFS_COUNTER_INCREMENT) / divisor +
                     OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
}

}

const BackendOperations RootFileSystem::operations{
    .lookup = RootFileSystem::LookupOperation,
    .create = RootFileSystem::CreateOperation,
    .open = RootFileSystem::OpenOperation,
    .close = RootFileSystem::CloseOperation,
    .remove = RootFileSystem::RemoveOperation,
    .rename = RootFileSystem::RenameOperation,
    .parent = RootFileSystem::ParentOperation,
    .read = RootFileSystem::ReadOperation,
    .write = RootFileSystem::WriteOperation,
    .truncate = RootFileSystem::TruncateOperation,
    .read_directory = RootFileSystem::ReadDirectoryOperation,
    .get_name = RootFileSystem::GetNameOperation,
    .stat = RootFileSystem::StatOperation,
    .sync = RootFileSystem::SyncOperation,
    .validate = RootFileSystem::ValidateOperation,
    .read_resource_usage = RootFileSystem::ReadResourceUsageOperation,
};

Status RootFileSystem::Initialize(FileSystemBlockDevice &device,
                                  const uint64_t superblock_identifier,
                                  const bool read_only) noexcept {
    if (this->initialized_) {
        return Status::AlreadyInitialized;
    }
    if (superblock_identifier == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    this->device_ = &device;
    this->cache_.Initialize(device);
    this->lock_ = SpinLock{};
    this->failed_ = false;
    this->statistics_ = RootFileSystemStatistics{};
    this->next_data_allocation_hint_ = OS_KERNEL_ROOTFS_FIRST_INDEX;
    this->next_inode_allocation_hint_ = OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT;
    ClearBytes(reinterpret_cast<uint8_t *>(this->open_counts_), sizeof(this->open_counts_));
    ClearBytes(this->validation_inode_bitmap_, sizeof(this->validation_inode_bitmap_));
    ClearBytes(this->validation_data_bitmap_, sizeof(this->validation_data_bitmap_));

    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    Status status = this->ReadRelativeBlock(OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK, block);
    if (status != Status::Succeeded) {
        this->device_ = nullptr;
        return status;
    }
    const RootFormatStatus format_status =
        DecodeRootSuperblock(block, sizeof(block), this->disk_superblock_);
    if (format_status != RootFormatStatus::Succeeded) {
        this->cache_.Invalidate();
        this->device_ = nullptr;
        return Status::Corrupt;
    }
    if (this->disk_superblock_.transaction_state != RootTransactionState::Clean) {
        this->cache_.Invalidate();
        this->device_ = nullptr;
        return Status::IncompleteTransaction;
    }
    this->initialized_ = true;
    RootInode root_inode{};
    status = this->ReadInode(OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER, root_inode);
    if (status != Status::Succeeded || root_inode.type != RootNodeType::Directory ||
        root_inode.parent_inode_number != OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER) {
        this->initialized_ = false;
        this->cache_.Invalidate();
        this->device_ = nullptr;
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    this->vfs_superblock_ = Superblock{
        .backend_kind = BackendKind::Root,
        .identifier = superblock_identifier,
        .generation = this->disk_superblock_.transaction_generation,
        .root = {},
        .operations = &RootFileSystem::operations,
        .backend_context = this,
        .maximum_name_length_bytes = OS_KERNEL_ROOTFS_MAXIMUM_NAME_LENGTH_BYTES,
        .read_only = read_only,
        .initialized = true,
    };
    this->vfs_superblock_.root = this->MakeVnode(OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER, root_inode);
    status = this->ValidateUnlocked();
    if (status != Status::Succeeded) {
        this->initialized_ = false;
        this->vfs_superblock_ = Superblock{};
        this->cache_.Invalidate();
        this->device_ = nullptr;
        return status;
    }
    return Status::Succeeded;
}

Superblock &RootFileSystem::GetSuperblock() noexcept { return this->vfs_superblock_; }

const Superblock &RootFileSystem::GetSuperblock() const noexcept { return this->vfs_superblock_; }

RootFileSystemStatistics RootFileSystem::ReadStatistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    RootFileSystemStatistics statistics = this->statistics_;
    statistics.cache = this->cache_.Statistics();
    return statistics;
}

Status RootFileSystem::ReadRelativeBlock(const uint64_t relative_block,
                                         uint8_t *const block) noexcept {
    if (this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (block == nullptr || relative_block >= OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT) {
        return Status::InvalidArgument;
    }
    const BlockCacheStatus status = this->cache_.ReadBlock(
        OS_KERNEL_ROOTFS_START_LBA + relative_block, block, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    return status == BlockCacheStatus::Succeeded ? Status::Succeeded : this->FailDeviceOperation();
}

Status RootFileSystem::WriteRelativeBlock(const uint64_t relative_block,
                                          const uint8_t *const block) noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (block == nullptr || relative_block >= OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT) {
        return Status::InvalidArgument;
    }
    const BlockCacheStatus status = this->cache_.WriteBlock(
        OS_KERNEL_ROOTFS_START_LBA + relative_block, block, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    return status == BlockCacheStatus::Succeeded ? Status::Succeeded : this->FailDeviceOperation();
}

Status RootFileSystem::WriteSuperblockDirect() noexcept {
    if (this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    if (EncodeRootSuperblock(this->disk_superblock_, block, sizeof(block)) !=
        RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    const uint64_t superblock_logical_block_address =
        OS_KERNEL_ROOTFS_START_LBA + OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK;
    if (this->cache_.WriteBlock(superblock_logical_block_address, block, sizeof(block)) !=
            BlockCacheStatus::Succeeded ||
        this->cache_.Sync() != BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    return Status::Succeeded;
}

Status RootFileSystem::BeginTransaction() noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (this->failed_) {
        return Status::DeviceFailure;
    }
    if (this->vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    if (this->disk_superblock_.transaction_state != RootTransactionState::Clean ||
        this->disk_superblock_.transaction_generation == UINT64_MAX) {
        return Status::IncompleteTransaction;
    }
    if (this->cache_.Sync() != BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    this->disk_superblock_.transaction_state = RootTransactionState::Dirty;
    ++this->disk_superblock_.transaction_generation;
    return this->WriteSuperblockDirect();
}

Status RootFileSystem::CommitTransaction() noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (this->failed_) {
        return Status::DeviceFailure;
    }
    if (this->disk_superblock_.transaction_state != RootTransactionState::Dirty) {
        return Status::IncompleteTransaction;
    }
    if (this->cache_.Sync() != BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    this->disk_superblock_.transaction_state = RootTransactionState::Clean;
    const Status status = this->WriteSuperblockDirect();
    if (status == Status::Succeeded) {
        this->statistics_.transaction_generation = this->disk_superblock_.transaction_generation;
        this->vfs_superblock_.generation = this->disk_superblock_.transaction_generation;
    }
    return status;
}

Status RootFileSystem::FailDeviceOperation() noexcept {
    this->failed_ = true;
    this->cache_.Invalidate();
    return Status::DeviceFailure;
}

Status RootFileSystem::ReadInode(const uint64_t inode_number, RootInode &inode) noexcept {
    inode = RootInode{};
    if (!this->initialized_ || inode_number == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        inode_number > OS_KERNEL_ROOTFS_INODE_COUNT) {
        return !this->initialized_ ? Status::NotInitialized : Status::InvalidArgument;
    }
    const uint64_t inode_offset_bytes =
        (inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT) * OS_KERNEL_ROOTFS_INODE_SIZE_BYTES;
    const uint64_t relative_block = OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK +
                                    inode_offset_bytes / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    const uint64_t block_offset_bytes = inode_offset_bytes % OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    const Status read_status = this->ReadRelativeBlock(relative_block, block);
    if (read_status != Status::Succeeded) {
        return read_status;
    }
    return DecodeRootInode(block + block_offset_bytes, OS_KERNEL_ROOTFS_INODE_SIZE_BYTES, inode) ==
                   RootFormatStatus::Succeeded
               ? Status::Succeeded
               : Status::Corrupt;
}

Status RootFileSystem::WriteInode(const uint64_t inode_number, const RootInode &inode) noexcept {
    if (!this->initialized_ || inode_number == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        inode_number > OS_KERNEL_ROOTFS_INODE_COUNT) {
        return !this->initialized_ ? Status::NotInitialized : Status::InvalidArgument;
    }
    const uint64_t inode_offset_bytes =
        (inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT) * OS_KERNEL_ROOTFS_INODE_SIZE_BYTES;
    const uint64_t relative_block = OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK +
                                    inode_offset_bytes / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    const uint64_t block_offset_bytes = inode_offset_bytes % OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    Status status = this->ReadRelativeBlock(relative_block, block);
    if (status != Status::Succeeded) {
        return status;
    }
    if (EncodeRootInode(inode, block + block_offset_bytes, OS_KERNEL_ROOTFS_INODE_SIZE_BYTES) !=
        RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    status = this->WriteRelativeBlock(relative_block, block);
    return status;
}

Status RootFileSystem::ReadPointerBlock(const uint64_t relative_block,
                                        RootPointerBlock &pointer_block) noexcept {
    pointer_block = RootPointerBlock{};
    if (relative_block < OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK ||
        relative_block >= OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT) {
        return Status::Corrupt;
    }
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    const Status status = this->ReadRelativeBlock(relative_block, block);
    if (status != Status::Succeeded) {
        return status;
    }
    return DecodeRootPointerBlock(block, sizeof(block), pointer_block) ==
                   RootFormatStatus::Succeeded
               ? Status::Succeeded
               : Status::Corrupt;
}

Status RootFileSystem::WritePointerBlock(const uint64_t relative_block,
                                         const RootPointerBlock &pointer_block) noexcept {
    if (relative_block < OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK ||
        relative_block >= OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT) {
        return Status::Corrupt;
    }
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    if (EncodeRootPointerBlock(pointer_block, block, sizeof(block)) !=
        RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    return this->WriteRelativeBlock(relative_block, block);
}

Status RootFileSystem::ReadBitmapBit(const bool inode_bitmap, const uint64_t bit_index,
                                     bool &allocated) noexcept {
    allocated = false;
    const uint64_t bit_count =
        inode_bitmap ? OS_KERNEL_ROOTFS_INODE_COUNT : OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT;
    if (bit_index >= bit_count) {
        return Status::InvalidArgument;
    }
    const uint64_t bits_per_block =
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES * OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint64_t start_block = inode_bitmap ? OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK
                                              : OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK;
    const uint64_t relative_block = start_block + bit_index / bits_per_block;
    const uint64_t block_bit_index = bit_index % bits_per_block;
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    const Status status = this->ReadRelativeBlock(relative_block, block);
    if (status != Status::Succeeded) {
        return status;
    }
    allocated = BitmapBitIsSet(block, block_bit_index);
    return Status::Succeeded;
}

Status RootFileSystem::WriteBitmapBit(const bool inode_bitmap, const uint64_t bit_index,
                                      const bool allocated) noexcept {
    const uint64_t bit_count =
        inode_bitmap ? OS_KERNEL_ROOTFS_INODE_COUNT : OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT;
    if (bit_index >= bit_count) {
        return Status::InvalidArgument;
    }
    const uint64_t bits_per_block =
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES * OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint64_t start_block = inode_bitmap ? OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK
                                              : OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK;
    const uint64_t relative_block = start_block + bit_index / bits_per_block;
    const uint64_t block_bit_index = bit_index % bits_per_block;
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    Status status = this->ReadRelativeBlock(relative_block, block);
    if (status != Status::Succeeded) {
        return status;
    }
    SetBitmapBit(block, block_bit_index, allocated);
    status = this->WriteRelativeBlock(relative_block, block);
    return status;
}

Status RootFileSystem::FindFreeBitmapBit(const bool inode_bitmap, const uint64_t first_bit,
                                         const uint64_t bit_count, uint64_t &bit_index) noexcept {
    bit_index = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const uint64_t available_bit_count =
        inode_bitmap ? OS_KERNEL_ROOTFS_INODE_COUNT : OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT;
    if (first_bit > available_bit_count || bit_count > available_bit_count - first_bit) {
        return Status::InvalidArgument;
    }
    const uint64_t bits_per_block =
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES * OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint64_t start_block = inode_bitmap ? OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK
                                              : OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK;
    uint64_t current_bit = first_bit;
    const uint64_t end_bit = first_bit + bit_count;
    while (current_bit < end_bit) {
        const uint64_t bitmap_block_index = current_bit / bits_per_block;
        uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
        const Status status = this->ReadRelativeBlock(start_block + bitmap_block_index, block);
        if (status != Status::Succeeded) {
            return status;
        }
        const uint64_t block_end = Minimum(
            end_bit, (bitmap_block_index + OS_KERNEL_ROOTFS_COUNTER_INCREMENT) * bits_per_block);
        while (current_bit < block_end) {
            if (!BitmapBitIsSet(block, current_bit % bits_per_block)) {
                bit_index = current_bit;
                return Status::Succeeded;
            }
            ++current_bit;
        }
    }
    return Status::CapacityExhausted;
}

Status RootFileSystem::AllocateDataBlock(uint64_t &relative_block) noexcept {
    relative_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (this->statistics_.free_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::CapacityExhausted;
    }
    uint64_t search_start = this->next_data_allocation_hint_;
    if (search_start >= OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT) {
        search_start = OS_KERNEL_ROOTFS_FIRST_INDEX;
    }
    uint64_t bit_index = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    Status status = this->FindFreeBitmapBit(
        false, search_start, OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT - search_start, bit_index);
    if (status == Status::CapacityExhausted && search_start != OS_KERNEL_ROOTFS_FIRST_INDEX) {
        status =
            this->FindFreeBitmapBit(false, OS_KERNEL_ROOTFS_FIRST_INDEX, search_start, bit_index);
    }
    if (status != Status::Succeeded) {
        return status;
    }
    status = this->WriteBitmapBit(false, bit_index, true);
    if (status != Status::Succeeded) {
        return status;
    }
    relative_block = OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + bit_index;
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    status = this->WriteRelativeBlock(relative_block, block);
    if (status != Status::Succeeded) {
        return status;
    }
    if (this->statistics_.free_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    --this->statistics_.free_data_block_count;
    this->next_data_allocation_hint_ = bit_index + OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    if (this->next_data_allocation_hint_ >= OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT) {
        this->next_data_allocation_hint_ = OS_KERNEL_ROOTFS_FIRST_INDEX;
    }
    return Status::Succeeded;
}

Status RootFileSystem::ReleaseDataBlock(const uint64_t relative_block) noexcept {
    if (relative_block < OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK ||
        relative_block >= OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT) {
        return Status::Corrupt;
    }
    const uint64_t bit_index = relative_block - OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK;
    bool allocated = false;
    Status status = this->ReadBitmapBit(false, bit_index, allocated);
    if (status != Status::Succeeded || !allocated) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    status = this->WriteRelativeBlock(relative_block, block);
    if (status != Status::Succeeded) {
        return status;
    }
    status = this->WriteBitmapBit(false, bit_index, false);
    if (status != Status::Succeeded) {
        return status;
    }
    if (this->statistics_.free_data_block_count == OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT) {
        return Status::Corrupt;
    }
    ++this->statistics_.free_data_block_count;
    if (bit_index < this->next_data_allocation_hint_) {
        this->next_data_allocation_hint_ = bit_index;
    }
    return Status::Succeeded;
}

Status RootFileSystem::AllocateInodeNumber(uint64_t &inode_number) noexcept {
    inode_number = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    uint64_t search_start = this->next_inode_allocation_hint_;
    if (search_start < OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT ||
        search_start >= OS_KERNEL_ROOTFS_INODE_COUNT) {
        search_start = OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT;
    }
    uint64_t bit_index = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    Status find_status = this->FindFreeBitmapBit(
        true, search_start, OS_KERNEL_ROOTFS_INODE_COUNT - search_start, bit_index);
    if (find_status == Status::CapacityExhausted &&
        search_start > OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT) {
        find_status = this->FindFreeBitmapBit(
            true, OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT,
            search_start - OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT, bit_index);
    }
    if (find_status != Status::Succeeded) {
        return find_status;
    }
    const Status write_status = this->WriteBitmapBit(true, bit_index, true);
    if (write_status != Status::Succeeded) {
        return write_status;
    }
    inode_number = bit_index + OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    ++this->statistics_.allocated_inode_count;
    this->next_inode_allocation_hint_ = bit_index + OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    if (this->next_inode_allocation_hint_ >= OS_KERNEL_ROOTFS_INODE_COUNT) {
        this->next_inode_allocation_hint_ = OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT;
    }
    return Status::Succeeded;
}

Status RootFileSystem::ReleaseInodeNumber(const uint64_t inode_number) noexcept {
    if (inode_number <= OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER ||
        inode_number > OS_KERNEL_ROOTFS_INODE_COUNT ||
        this->statistics_.allocated_inode_count <= OS_KERNEL_ROOTFS_COUNTER_INCREMENT) {
        return Status::Corrupt;
    }
    const Status status =
        this->WriteBitmapBit(true, inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT, false);
    if (status == Status::Succeeded) {
        --this->statistics_.allocated_inode_count;
        const uint64_t bit_index = inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
        if (bit_index < this->next_inode_allocation_hint_) {
            this->next_inode_allocation_hint_ = bit_index;
        }
    }
    return status;
}

Status RootFileSystem::RequiredBlocksForLogicalBlock(const RootInode &inode,
                                                     const uint64_t logical_block,
                                                     uint64_t &required_block_count) noexcept {
    required_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const uint64_t maximum_logical_block_count =
        OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    if (logical_block >= maximum_logical_block_count) {
        return Status::FileTooLarge;
    }
    if (logical_block < OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT) {
        required_block_count = inode.direct_blocks[logical_block] == OS_KERNEL_ROOTFS_EMPTY_VALUE
                                   ? OS_KERNEL_ROOTFS_COUNTER_INCREMENT
                                   : OS_KERNEL_ROOTFS_EMPTY_VALUE;
        return Status::Succeeded;
    }

    uint64_t remaining_logical_block = logical_block - OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT;
    uint64_t level = OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    uint64_t root_block = inode.single_indirect_block;
    const uint64_t single_capacity = OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    const uint64_t double_capacity = single_capacity * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    if (remaining_logical_block >= single_capacity) {
        remaining_logical_block -= single_capacity;
        level = OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL;
        root_block = inode.double_indirect_block;
        if (remaining_logical_block >= double_capacity) {
            remaining_logical_block -= double_capacity;
            level = OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL;
            root_block = inode.triple_indirect_block;
        }
    }
    if (root_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        required_block_count = level + OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
        return Status::Succeeded;
    }
    uint64_t current_block = root_block;
    for (uint64_t current_level = level; current_level != OS_KERNEL_ROOTFS_EMPTY_VALUE;
         --current_level) {
        RootPointerBlock pointer_block{};
        const Status read_status = this->ReadPointerBlock(current_block, pointer_block);
        if (read_status != Status::Succeeded) {
            return read_status;
        }
        uint64_t child_span = OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
        for (uint64_t span_level = OS_KERNEL_ROOTFS_COUNTER_INCREMENT; span_level < current_level;
             ++span_level) {
            child_span *= OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
        }
        const uint64_t pointer_index = remaining_logical_block / child_span;
        remaining_logical_block %= child_span;
        if (pointer_index >= OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK) {
            return Status::FileTooLarge;
        }
        const uint64_t child_block = pointer_block.pointers[pointer_index];
        if (child_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            required_block_count = current_level;
            return Status::Succeeded;
        }
        current_block = child_block;
    }
    return Status::Succeeded;
}

Status RootFileSystem::ResolveDataBlock(RootInode &inode, const uint64_t logical_block,
                                        const bool allocate, uint64_t &relative_block) noexcept {
    relative_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const uint64_t maximum_logical_block_count =
        OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    if (logical_block >= maximum_logical_block_count) {
        return Status::FileTooLarge;
    }
    if (logical_block < OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT) {
        uint64_t &direct_block = inode.direct_blocks[logical_block];
        if (direct_block == OS_KERNEL_ROOTFS_EMPTY_VALUE && allocate) {
            Status status = this->AllocateDataBlock(direct_block);
            if (status != Status::Succeeded) {
                return status;
            }
            ++inode.allocated_data_block_count;
            ++this->statistics_.allocated_data_block_count;
        }
        relative_block = direct_block;
        return Status::Succeeded;
    }

    uint64_t remaining_logical_block = logical_block - OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT;
    const uint64_t single_capacity = OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    const uint64_t double_capacity = single_capacity * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    if (remaining_logical_block < single_capacity) {
        return this->ResolveIndirectDataBlock(
            inode.single_indirect_block, OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
            remaining_logical_block, allocate, inode, relative_block);
    }
    remaining_logical_block -= single_capacity;
    if (remaining_logical_block < double_capacity) {
        return this->ResolveIndirectDataBlock(
            inode.double_indirect_block, OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL,
            remaining_logical_block, allocate, inode, relative_block);
    }
    remaining_logical_block -= double_capacity;
    return this->ResolveIndirectDataBlock(inode.triple_indirect_block,
                                          OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL,
                                          remaining_logical_block, allocate, inode, relative_block);
}

Status RootFileSystem::ResolveIndirectDataBlock(uint64_t &root_block, const uint64_t level,
                                                const uint64_t logical_block, const bool allocate,
                                                RootInode &inode,
                                                uint64_t &relative_block) noexcept {
    relative_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (level == OS_KERNEL_ROOTFS_EMPTY_VALUE || level > OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL) {
        return Status::InvalidArgument;
    }
    if (root_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        if (!allocate) {
            return Status::Succeeded;
        }
        Status status = this->AllocateDataBlock(root_block);
        if (status != Status::Succeeded) {
            return status;
        }
        RootPointerBlock empty_pointer_block{};
        status = this->WritePointerBlock(root_block, empty_pointer_block);
        if (status != Status::Succeeded) {
            return status;
        }
        ++inode.allocated_metadata_block_count;
        ++this->statistics_.allocated_metadata_block_count;
    }

    RootPointerBlock pointer_block{};
    Status status = this->ReadPointerBlock(root_block, pointer_block);
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t child_span = OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    for (uint64_t span_level = OS_KERNEL_ROOTFS_COUNTER_INCREMENT; span_level < level;
         ++span_level) {
        child_span *= OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    }
    const uint64_t pointer_index = logical_block / child_span;
    const uint64_t child_logical_block = logical_block % child_span;
    if (pointer_index >= OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK) {
        return Status::FileTooLarge;
    }
    uint64_t child_block = pointer_block.pointers[pointer_index];
    if (level == OS_KERNEL_ROOTFS_COUNTER_INCREMENT) {
        if (child_block == OS_KERNEL_ROOTFS_EMPTY_VALUE && allocate) {
            status = this->AllocateDataBlock(child_block);
            if (status != Status::Succeeded) {
                return status;
            }
            pointer_block.pointers[pointer_index] = child_block;
            status = this->WritePointerBlock(root_block, pointer_block);
            if (status != Status::Succeeded) {
                return status;
            }
            ++inode.allocated_data_block_count;
            ++this->statistics_.allocated_data_block_count;
        }
        relative_block = child_block;
        return Status::Succeeded;
    }
    const uint64_t original_child_block = child_block;
    status = this->ResolveIndirectDataBlock(child_block, level - OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
                                            child_logical_block, allocate, inode, relative_block);
    if (status != Status::Succeeded) {
        return status;
    }
    if (child_block != original_child_block) {
        pointer_block.pointers[pointer_index] = child_block;
        status = this->WritePointerBlock(root_block, pointer_block);
    }
    return status;
}

Status RootFileSystem::ReleaseLogicalBlock(RootInode &inode,
                                           const uint64_t logical_block) noexcept {
    if (logical_block < OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT) {
        uint64_t &direct_block = inode.direct_blocks[logical_block];
        if (direct_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return Status::Succeeded;
        }
        const Status status = this->ReleaseDataBlock(direct_block);
        if (status != Status::Succeeded) {
            return status;
        }
        direct_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        if (inode.allocated_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
            this->statistics_.allocated_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return Status::Corrupt;
        }
        --inode.allocated_data_block_count;
        --this->statistics_.allocated_data_block_count;
        return Status::Succeeded;
    }
    uint64_t remaining_logical_block = logical_block - OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT;
    const uint64_t single_capacity = OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    const uint64_t double_capacity = single_capacity * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    if (remaining_logical_block < single_capacity) {
        return this->ReleaseIndirectLogicalBlock(inode.single_indirect_block,
                                                 OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
                                                 remaining_logical_block, inode);
    }
    remaining_logical_block -= single_capacity;
    if (remaining_logical_block < double_capacity) {
        return this->ReleaseIndirectLogicalBlock(inode.double_indirect_block,
                                                 OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL,
                                                 remaining_logical_block, inode);
    }
    remaining_logical_block -= double_capacity;
    return this->ReleaseIndirectLogicalBlock(inode.triple_indirect_block,
                                             OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL,
                                             remaining_logical_block, inode);
}

Status RootFileSystem::ReleaseIndirectLogicalBlock(uint64_t &root_block, const uint64_t level,
                                                   const uint64_t logical_block,
                                                   RootInode &inode) noexcept {
    if (root_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    RootPointerBlock pointer_block{};
    Status status = this->ReadPointerBlock(root_block, pointer_block);
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t child_span = OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    for (uint64_t span_level = OS_KERNEL_ROOTFS_COUNTER_INCREMENT; span_level < level;
         ++span_level) {
        child_span *= OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    }
    const uint64_t pointer_index = logical_block / child_span;
    const uint64_t child_logical_block = logical_block % child_span;
    if (pointer_index >= OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK) {
        return Status::FileTooLarge;
    }
    uint64_t child_block = pointer_block.pointers[pointer_index];
    if (level == OS_KERNEL_ROOTFS_COUNTER_INCREMENT) {
        if (child_block != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            status = this->ReleaseDataBlock(child_block);
            if (status != Status::Succeeded) {
                return status;
            }
            pointer_block.pointers[pointer_index] = OS_KERNEL_ROOTFS_EMPTY_VALUE;
            if (inode.allocated_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
                this->statistics_.allocated_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
                return Status::Corrupt;
            }
            --inode.allocated_data_block_count;
            --this->statistics_.allocated_data_block_count;
        }
    } else {
        const uint64_t original_child_block = child_block;
        status = this->ReleaseIndirectLogicalBlock(
            child_block, level - OS_KERNEL_ROOTFS_COUNTER_INCREMENT, child_logical_block, inode);
        if (status != Status::Succeeded) {
            return status;
        }
        if (child_block != original_child_block) {
            pointer_block.pointers[pointer_index] = child_block;
        }
    }
    if (PointerBlockIsEmpty(pointer_block)) {
        status = this->ReleaseDataBlock(root_block);
        if (status != Status::Succeeded) {
            return status;
        }
        root_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        if (inode.allocated_metadata_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
            this->statistics_.allocated_metadata_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return Status::Corrupt;
        }
        --inode.allocated_metadata_block_count;
        --this->statistics_.allocated_metadata_block_count;
        return Status::Succeeded;
    }
    return this->WritePointerBlock(root_block, pointer_block);
}

Status RootFileSystem::ReleaseLogicalBlockRange(RootInode &inode,
                                                const uint64_t first_logical_block,
                                                const uint64_t past_last_logical_block) noexcept {
    const uint64_t maximum_logical_block_count =
        OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    if (first_logical_block > past_last_logical_block ||
        past_last_logical_block > maximum_logical_block_count) {
        return Status::InvalidArgument;
    }
    if (first_logical_block == past_last_logical_block) {
        return Status::Succeeded;
    }

    const uint64_t direct_past_last =
        Minimum(past_last_logical_block, OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT);
    for (uint64_t logical_block = first_logical_block; logical_block < direct_past_last;
         ++logical_block) {
        const Status status = this->ReleaseLogicalBlock(inode, logical_block);
        if (status != Status::Succeeded) {
            return status;
        }
    }

    const uint64_t single_start = OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT;
    const uint64_t single_past_last = single_start + OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    if (past_last_logical_block > single_start && first_logical_block < single_past_last) {
        const uint64_t local_first = first_logical_block > single_start
                                         ? first_logical_block - single_start
                                         : OS_KERNEL_ROOTFS_EMPTY_VALUE;
        const uint64_t local_past_last =
            Minimum(past_last_logical_block, single_past_last) - single_start;
        const Status status = this->ReleaseIndirectLogicalBlockRange(
            inode.single_indirect_block, OS_KERNEL_ROOTFS_SINGLE_INDIRECT_LEVEL, local_first,
            local_past_last, inode);
        if (status != Status::Succeeded) {
            return status;
        }
    }

    const uint64_t double_capacity =
        OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    const uint64_t double_start = single_past_last;
    const uint64_t double_past_last = double_start + double_capacity;
    if (past_last_logical_block > double_start && first_logical_block < double_past_last) {
        const uint64_t local_first = first_logical_block > double_start
                                         ? first_logical_block - double_start
                                         : OS_KERNEL_ROOTFS_EMPTY_VALUE;
        const uint64_t local_past_last =
            Minimum(past_last_logical_block, double_past_last) - double_start;
        const Status status = this->ReleaseIndirectLogicalBlockRange(
            inode.double_indirect_block, OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL, local_first,
            local_past_last, inode);
        if (status != Status::Succeeded) {
            return status;
        }
    }

    const uint64_t triple_start = double_past_last;
    if (past_last_logical_block > triple_start) {
        const uint64_t local_first = first_logical_block > triple_start
                                         ? first_logical_block - triple_start
                                         : OS_KERNEL_ROOTFS_EMPTY_VALUE;
        const uint64_t local_past_last = past_last_logical_block - triple_start;
        return this->ReleaseIndirectLogicalBlockRange(inode.triple_indirect_block,
                                                      OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL,
                                                      local_first, local_past_last, inode);
    }
    return Status::Succeeded;
}

Status RootFileSystem::ReleaseIndirectLogicalBlockRange(uint64_t &root_block, const uint64_t level,
                                                        const uint64_t first_logical_block,
                                                        const uint64_t past_last_logical_block,
                                                        RootInode &inode) noexcept {
    if (root_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    if (level == OS_KERNEL_ROOTFS_EMPTY_VALUE || level > OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL ||
        first_logical_block > past_last_logical_block) {
        return Status::InvalidArgument;
    }
    uint64_t child_span = OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    for (uint64_t span_level = OS_KERNEL_ROOTFS_COUNTER_INCREMENT; span_level < level;
         ++span_level) {
        child_span *= OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    }
    const uint64_t tree_capacity = child_span * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    if (past_last_logical_block > tree_capacity) {
        return Status::InvalidArgument;
    }

    RootPointerBlock pointer_block{};
    Status status = this->ReadPointerBlock(root_block, pointer_block);
    if (status != Status::Succeeded) {
        return status;
    }
    bool changed = false;
    for (uint64_t pointer_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         pointer_index < OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK; ++pointer_index) {
        const uint64_t child_start = pointer_index * child_span;
        const uint64_t child_past_last = child_start + child_span;
        if (child_past_last <= first_logical_block || child_start >= past_last_logical_block ||
            pointer_block.pointers[pointer_index] == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            continue;
        }
        uint64_t &child_block = pointer_block.pointers[pointer_index];
        if (level == OS_KERNEL_ROOTFS_SINGLE_INDIRECT_LEVEL) {
            status = this->ReleaseDataBlock(child_block);
            if (status != Status::Succeeded) {
                return status;
            }
            child_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
            if (inode.allocated_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
                this->statistics_.allocated_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
                return Status::Corrupt;
            }
            --inode.allocated_data_block_count;
            --this->statistics_.allocated_data_block_count;
            changed = true;
            continue;
        }
        const uint64_t original_child_block = child_block;
        const uint64_t child_first = first_logical_block > child_start
                                         ? first_logical_block - child_start
                                         : OS_KERNEL_ROOTFS_EMPTY_VALUE;
        const uint64_t child_limit =
            Minimum(past_last_logical_block, child_past_last) - child_start;
        status = this->ReleaseIndirectLogicalBlockRange(child_block,
                                                        level - OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
                                                        child_first, child_limit, inode);
        if (status != Status::Succeeded) {
            return status;
        }
        changed = changed || child_block != original_child_block;
    }
    if (PointerBlockIsEmpty(pointer_block)) {
        status = this->ReleaseDataBlock(root_block);
        if (status != Status::Succeeded) {
            return status;
        }
        root_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        if (inode.allocated_metadata_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
            this->statistics_.allocated_metadata_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return Status::Corrupt;
        }
        --inode.allocated_metadata_block_count;
        --this->statistics_.allocated_metadata_block_count;
        return Status::Succeeded;
    }
    return changed ? this->WritePointerBlock(root_block, pointer_block) : Status::Succeeded;
}

Status RootFileSystem::ReadFileBytes(RootInode &inode, const uint64_t offset_bytes,
                                     uint8_t *const destination, const uint64_t capacity_bytes,
                                     uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (destination == nullptr && capacity_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    if (offset_bytes >= inode.size_bytes || capacity_bytes == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    read_bytes = Minimum(capacity_bytes, inode.size_bytes - offset_bytes);
    uint64_t copied_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    while (copied_bytes < read_bytes) {
        const uint64_t absolute_offset = offset_bytes + copied_bytes;
        const uint64_t logical_block = absolute_offset / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        const uint64_t block_offset_bytes = absolute_offset % OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        const uint64_t chunk_bytes = Minimum(
            read_bytes - copied_bytes, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES - block_offset_bytes);
        uint64_t relative_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        Status status = this->ResolveDataBlock(inode, logical_block, false, relative_block);
        if (status != Status::Succeeded) {
            read_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
            return status;
        }
        if (relative_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            ClearBytes(destination + copied_bytes, chunk_bytes);
            this->statistics_.sparse_hole_read_bytes += chunk_bytes;
        } else {
            uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
            status = this->ReadRelativeBlock(relative_block, block);
            if (status != Status::Succeeded) {
                read_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
                return status;
            }
            CopyBytes(destination + copied_bytes, block + block_offset_bytes, chunk_bytes);
        }
        copied_bytes += chunk_bytes;
    }
    this->statistics_.bytes_read += read_bytes;
    return Status::Succeeded;
}

Status RootFileSystem::WriteFileBytesInTransaction(const uint64_t inode_number, RootInode &inode,
                                                   const uint64_t offset_bytes,
                                                   const uint8_t *const source,
                                                   const uint64_t length_bytes,
                                                   uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if ((source == nullptr && length_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE) ||
        this->disk_superblock_.transaction_state != RootTransactionState::Dirty) {
        return Status::InvalidArgument;
    }
    if (length_bytes == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    if (offset_bytes > OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES ||
        length_bytes > OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES - offset_bytes) {
        return Status::FileTooLarge;
    }

    while (written_bytes < length_bytes) {
        const uint64_t absolute_offset = offset_bytes + written_bytes;
        const uint64_t logical_block = absolute_offset / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        const uint64_t block_offset_bytes = absolute_offset % OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        const uint64_t chunk_bytes = Minimum(
            length_bytes - written_bytes, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES - block_offset_bytes);
        uint64_t required_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        Status status =
            this->RequiredBlocksForLogicalBlock(inode, logical_block, required_block_count);
        if (status != Status::Succeeded) {
            return status;
        }
        if (required_block_count > this->statistics_.free_data_block_count) {
            if (written_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
                ++this->statistics_.short_write_count;
                break;
            }
            return Status::CapacityExhausted;
        }
        uint64_t relative_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        status = this->ResolveDataBlock(inode, logical_block, true, relative_block);
        if (status != Status::Succeeded || relative_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
        status = this->ReadRelativeBlock(relative_block, block);
        if (status != Status::Succeeded) {
            return status;
        }
        CopyBytes(block + block_offset_bytes, source + written_bytes, chunk_bytes);
        status = this->WriteRelativeBlock(relative_block, block);
        if (status != Status::Succeeded) {
            return status;
        }
        written_bytes += chunk_bytes;
        const uint64_t written_end = offset_bytes + written_bytes;
        if (written_end > inode.size_bytes) {
            inode.size_bytes = written_end;
        }
    }
    const Status inode_status = this->WriteInode(inode_number, inode);
    if (inode_status != Status::Succeeded) {
        return inode_status;
    }
    this->statistics_.bytes_written += written_bytes;
    return Status::Succeeded;
}

Status RootFileSystem::TruncateInTransaction(const uint64_t inode_number, RootInode &inode,
                                             const uint64_t size_bytes) noexcept {
    if (this->disk_superblock_.transaction_state != RootTransactionState::Dirty) {
        return Status::InvalidArgument;
    }
    if (inode.type == RootNodeType::Directory &&
        size_bytes % OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    if (size_bytes > OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES) {
        return Status::FileTooLarge;
    }
    if (size_bytes == inode.size_bytes) {
        return Status::Succeeded;
    }
    const uint64_t old_logical_block_count =
        DivideRoundUp(inode.size_bytes, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    const uint64_t new_logical_block_count =
        DivideRoundUp(size_bytes, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    if (size_bytes < inode.size_bytes &&
        size_bytes % OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        const uint64_t tail_logical_block = size_bytes / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        uint64_t tail_relative_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        Status status =
            this->ResolveDataBlock(inode, tail_logical_block, false, tail_relative_block);
        if (status != Status::Succeeded) {
            return status;
        }
        if (tail_relative_block != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
            status = this->ReadRelativeBlock(tail_relative_block, block);
            if (status != Status::Succeeded) {
                return status;
            }
            const uint64_t block_offset_bytes = size_bytes % OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
            ClearBytes(block + block_offset_bytes,
                       OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES - block_offset_bytes);
            status = this->WriteRelativeBlock(tail_relative_block, block);
            if (status != Status::Succeeded) {
                return status;
            }
        }
    }
    if (new_logical_block_count < old_logical_block_count) {
        const Status release_status =
            this->ReleaseLogicalBlockRange(inode, new_logical_block_count, old_logical_block_count);
        if (release_status != Status::Succeeded) {
            return release_status;
        }
    }
    inode.size_bytes = size_bytes;
    return this->WriteInode(inode_number, inode);
}

Status RootFileSystem::ReadDirectoryEntryAt(RootInode &directory, const uint64_t offset_bytes,
                                            RootDirectoryEntry &entry) noexcept {
    entry = RootDirectoryEntry{};
    if (directory.type != RootNodeType::Directory || offset_bytes > directory.size_bytes ||
        directory.size_bytes - offset_bytes < OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        return Status::InvalidArgument;
    }
    uint8_t encoded[OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES]{};
    uint64_t read_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const Status status =
        this->ReadFileBytes(directory, offset_bytes, encoded, sizeof(encoded), read_bytes);
    if (status != Status::Succeeded) {
        return status;
    }
    if (read_bytes != sizeof(encoded) ||
        DecodeRootDirectoryEntry(encoded, sizeof(encoded), entry) != RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    return Status::Succeeded;
}

Status RootFileSystem::WriteDirectoryEntryAt(const uint64_t directory_inode_number,
                                             RootInode &directory, const uint64_t offset_bytes,
                                             const RootDirectoryEntry &entry) noexcept {
    if (directory.type != RootNodeType::Directory || offset_bytes > directory.size_bytes ||
        offset_bytes % OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES !=
            OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    uint8_t encoded[OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES]{};
    if (EncodeRootDirectoryEntry(entry, encoded, sizeof(encoded)) != RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    uint64_t written_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const Status status = this->WriteFileBytesInTransaction(
        directory_inode_number, directory, offset_bytes, encoded, sizeof(encoded), written_bytes);
    return status == Status::Succeeded && written_bytes == sizeof(encoded) ? Status::Succeeded
           : status == Status::Succeeded ? Status::CapacityExhausted
                                         : status;
}

Status RootFileSystem::FindDirectoryEntry(RootInode &directory, const uint8_t *const name,
                                          const uint64_t name_length_bytes,
                                          DirectoryEntryLocation &location) noexcept {
    location = DirectoryEntryLocation{};
    if (directory.type != RootNodeType::Directory || !NameIsValid(name, name_length_bytes)) {
        return Status::InvalidArgument;
    }
    for (uint64_t offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE; offset_bytes < directory.size_bytes;
         offset_bytes += OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        RootDirectoryEntry entry{};
        const Status status = this->ReadDirectoryEntryAt(directory, offset_bytes, entry);
        if (status != Status::Succeeded) {
            return status;
        }
        if (entry.type != RootNodeType::Unused && entry.name_length_bytes == name_length_bytes &&
            BytesAreEqual(entry.name, name, name_length_bytes)) {
            location = DirectoryEntryLocation{
                .offset_bytes = offset_bytes,
                .entry = entry,
            };
            return Status::Succeeded;
        }
    }
    return Status::NotFound;
}

Status RootFileSystem::FindDirectorySlot(RootInode &directory, uint64_t &offset_bytes) noexcept {
    offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (directory.type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }
    for (uint64_t candidate_offset = OS_KERNEL_ROOTFS_EMPTY_VALUE;
         candidate_offset < directory.size_bytes;
         candidate_offset += OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        RootDirectoryEntry entry{};
        const Status status = this->ReadDirectoryEntryAt(directory, candidate_offset, entry);
        if (status != Status::Succeeded) {
            return status;
        }
        if (entry.type == RootNodeType::Unused) {
            offset_bytes = candidate_offset;
            return Status::Succeeded;
        }
    }
    if (directory.size_bytes >
        OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES - OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        return Status::FileTooLarge;
    }
    offset_bytes = directory.size_bytes;
    return Status::Succeeded;
}

Status RootFileSystem::TrimDirectoryTail(const uint64_t directory_inode_number,
                                         RootInode &directory) noexcept {
    if (directory.type != RootNodeType::Directory ||
        directory.size_bytes % OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES !=
            OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    uint64_t new_size_bytes = directory.size_bytes;
    while (new_size_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        const uint64_t candidate_offset =
            new_size_bytes - OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES;
        RootDirectoryEntry entry{};
        const Status status = this->ReadDirectoryEntryAt(directory, candidate_offset, entry);
        if (status != Status::Succeeded) {
            return status;
        }
        if (entry.type != RootNodeType::Unused) {
            break;
        }
        new_size_bytes = candidate_offset;
    }
    return new_size_bytes == directory.size_bytes
               ? Status::Succeeded
               : this->TruncateInTransaction(directory_inode_number, directory, new_size_bytes);
}

Status RootFileSystem::DirectoryIsEmpty(RootInode &directory, bool &empty) noexcept {
    empty = false;
    if (directory.type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }
    for (uint64_t offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE; offset_bytes < directory.size_bytes;
         offset_bytes += OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        RootDirectoryEntry entry{};
        const Status status = this->ReadDirectoryEntryAt(directory, offset_bytes, entry);
        if (status != Status::Succeeded) {
            return status;
        }
        if (entry.type != RootNodeType::Unused) {
            return Status::Succeeded;
        }
    }
    empty = true;
    return Status::Succeeded;
}

Status RootFileSystem::RemoveInodeInTransaction(const uint64_t inode_number,
                                                RootInode &inode) noexcept {
    if (inode_number <= OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER ||
        inode_number > OS_KERNEL_ROOTFS_INODE_COUNT ||
        this->open_counts_[inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT] !=
            OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Busy;
    }
    Status status = this->TruncateInTransaction(inode_number, inode, OS_KERNEL_ROOTFS_EMPTY_VALUE);
    if (status != Status::Succeeded) {
        return status;
    }
    RootInode unused_inode{};
    unused_inode.type = RootNodeType::Unused;
    status = this->WriteInode(inode_number, unused_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    return this->ReleaseInodeNumber(inode_number);
}

Status RootFileSystem::ValidateVnode(const Vnode &vnode, RootInode &inode) noexcept {
    inode = RootInode{};
    if (!this->initialized_ || vnode.superblock != &this->vfs_superblock_ ||
        vnode.identifier == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        vnode.identifier > OS_KERNEL_ROOTFS_INODE_COUNT ||
        vnode.generation == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        (vnode.type != NodeType::RegularFile && vnode.type != NodeType::Directory)) {
        return Status::InvalidHandle;
    }
    bool allocated = false;
    Status status =
        this->ReadBitmapBit(true, vnode.identifier - OS_KERNEL_ROOTFS_COUNTER_INCREMENT, allocated);
    if (status != Status::Succeeded || !allocated) {
        return status == Status::Succeeded ? Status::InvalidHandle : status;
    }
    status = this->ReadInode(vnode.identifier, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    return inode.generation == vnode.generation &&
                   RootFileSystem::ToVfsNodeType(inode.type) == vnode.type
               ? Status::Succeeded
               : Status::InvalidHandle;
}

Vnode RootFileSystem::MakeVnode(const uint64_t inode_number, const RootInode &inode) noexcept {
    return Vnode{
        .superblock = &this->vfs_superblock_,
        .identifier = inode_number,
        .generation = inode.generation,
        .type = RootFileSystem::ToVfsNodeType(inode.type),
    };
}

NodeType RootFileSystem::ToVfsNodeType(const RootNodeType type) noexcept {
    if (type == RootNodeType::RegularFile) {
        return NodeType::RegularFile;
    }
    if (type == RootNodeType::Directory) {
        return NodeType::Directory;
    }
    return NodeType::None;
}

RootNodeType RootFileSystem::ToRootNodeType(const NodeType type) noexcept {
    if (type == NodeType::RegularFile) {
        return RootNodeType::RegularFile;
    }
    if (type == NodeType::Directory) {
        return RootNodeType::Directory;
    }
    return RootNodeType::Unused;
}

Status RootFileSystem::LookupOperation(void *const context, const Vnode &directory,
                                       const uint8_t *const name, const uint64_t name_length_bytes,
                                       Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || !NameIsValid(name, name_length_bytes)) {
        return name_length_bytes > OS_KERNEL_ROOTFS_MAXIMUM_NAME_LENGTH_BYTES
                   ? Status::NameTooLong
                   : Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    RootInode directory_inode{};
    Status status = file_system.ValidateVnode(directory, directory_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (directory_inode.type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }
    DirectoryEntryLocation location{};
    status = file_system.FindDirectoryEntry(directory_inode, name, name_length_bytes, location);
    if (status != Status::Succeeded) {
        return status;
    }
    RootInode child_inode{};
    status = file_system.ReadInode(location.entry.inode_number, child_inode);
    if (status != Status::Succeeded || child_inode.generation != location.entry.inode_generation ||
        child_inode.type != location.entry.type ||
        child_inode.parent_inode_number != directory.identifier) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    vnode = file_system.MakeVnode(location.entry.inode_number, child_inode);
    return vnode.type == NodeType::None ? Status::Corrupt : Status::Succeeded;
}

Status RootFileSystem::CreateOperation(void *const context, const Vnode &directory,
                                       const uint8_t *const name, const uint64_t name_length_bytes,
                                       const NodeType type, Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || !NameIsValid(name, name_length_bytes) ||
        (type != NodeType::RegularFile && type != NodeType::Directory)) {
        return name_length_bytes > OS_KERNEL_ROOTFS_MAXIMUM_NAME_LENGTH_BYTES
                   ? Status::NameTooLong
                   : Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode directory_inode{};
    Status status = file_system.ValidateVnode(directory, directory_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (directory_inode.type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }
    DirectoryEntryLocation existing{};
    status = file_system.FindDirectoryEntry(directory_inode, name, name_length_bytes, existing);
    if (status == Status::Succeeded) {
        return Status::AlreadyExists;
    }
    if (status != Status::NotFound) {
        return status;
    }
    uint64_t free_inode_bit = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.FindFreeBitmapBit(
        true, OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
        OS_KERNEL_ROOTFS_INODE_COUNT - OS_KERNEL_ROOTFS_COUNTER_INCREMENT, free_inode_bit);
    if (status != Status::Succeeded) {
        return status;
    }
    static_cast<void>(free_inode_bit);
    uint64_t slot_offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.FindDirectorySlot(directory_inode, slot_offset_bytes);
    if (status != Status::Succeeded) {
        return status;
    }
    const uint64_t first_logical_block = slot_offset_bytes / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    const uint64_t last_logical_block =
        (slot_offset_bytes + OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES -
         OS_KERNEL_ROOTFS_COUNTER_INCREMENT) /
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    uint64_t required_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    for (uint64_t logical_block = first_logical_block; logical_block <= last_logical_block;
         ++logical_block) {
        uint64_t block_requirement = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        status = file_system.RequiredBlocksForLogicalBlock(directory_inode, logical_block,
                                                           block_requirement);
        if (status != Status::Succeeded || required_block_count > UINT64_MAX - block_requirement) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        required_block_count += block_requirement;
    }
    if (required_block_count > file_system.statistics_.free_data_block_count) {
        return Status::CapacityExhausted;
    }
    if (file_system.disk_superblock_.next_inode_generation == UINT64_MAX) {
        return Status::CapacityExhausted;
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t inode_number = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.AllocateInodeNumber(inode_number);
    if (status != Status::Succeeded) {
        return status;
    }
    RootInode inode{
        .type = RootFileSystem::ToRootNodeType(type),
        .flags = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .size_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .generation = file_system.disk_superblock_.next_inode_generation,
        .link_count = OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
        .allocated_data_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .allocated_metadata_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .parent_inode_number = directory.identifier,
        .direct_blocks = {},
        .single_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .double_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .triple_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
    };
    ++file_system.disk_superblock_.next_inode_generation;
    status = file_system.WriteInode(inode_number, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    RootDirectoryEntry entry{
        .inode_number = inode_number,
        .inode_generation = inode.generation,
        .type = inode.type,
        .name_length_bytes = name_length_bytes,
        .name = {},
    };
    CopyBytes(entry.name, name, name_length_bytes);
    status = file_system.WriteDirectoryEntryAt(directory.identifier, directory_inode,
                                               slot_offset_bytes, entry);
    if (status != Status::Succeeded) {
        return status;
    }
    status = file_system.CommitTransaction();
    if (status == Status::Succeeded) {
        ++file_system.statistics_.create_count;
        vnode = file_system.MakeVnode(inode_number, inode);
    }
    return status;
}

Status RootFileSystem::OpenOperation(void *const context, const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    RootInode inode{};
    const Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t &open_count =
        file_system.open_counts_[vnode.identifier - OS_KERNEL_ROOTFS_COUNTER_INCREMENT];
    if (open_count == UINT64_MAX || file_system.statistics_.open_reference_count == UINT64_MAX) {
        return Status::CapacityExhausted;
    }
    ++open_count;
    ++file_system.statistics_.open_reference_count;
    return Status::Succeeded;
}

Status RootFileSystem::CloseOperation(void *const context, const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    RootInode inode{};
    const Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t &open_count =
        file_system.open_counts_[vnode.identifier - OS_KERNEL_ROOTFS_COUNTER_INCREMENT];
    if (open_count == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        file_system.statistics_.open_reference_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::InvalidHandle;
    }
    --open_count;
    --file_system.statistics_.open_reference_count;
    return Status::Succeeded;
}

Status RootFileSystem::RemoveOperation(void *const context, const Vnode &directory,
                                       const uint8_t *const name, const uint64_t name_length_bytes,
                                       const NodeType expected_type) noexcept {
    if (context == nullptr || !NameIsValid(name, name_length_bytes) ||
        (expected_type != NodeType::RegularFile && expected_type != NodeType::Directory)) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode directory_inode{};
    Status status = file_system.ValidateVnode(directory, directory_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (directory_inode.type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }
    DirectoryEntryLocation location{};
    status = file_system.FindDirectoryEntry(directory_inode, name, name_length_bytes, location);
    if (status != Status::Succeeded) {
        return status;
    }
    const NodeType actual_type = RootFileSystem::ToVfsNodeType(location.entry.type);
    if (actual_type != expected_type) {
        return expected_type == NodeType::Directory ? Status::NotDirectory : Status::IsDirectory;
    }
    RootInode child_inode{};
    status = file_system.ReadInode(location.entry.inode_number, child_inode);
    if (status != Status::Succeeded || child_inode.generation != location.entry.inode_generation ||
        child_inode.type != location.entry.type ||
        child_inode.parent_inode_number != directory.identifier) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    if (file_system
            .open_counts_[location.entry.inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT] !=
        OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Busy;
    }
    if (child_inode.type == RootNodeType::Directory) {
        bool empty = false;
        status = file_system.DirectoryIsEmpty(child_inode, empty);
        if (status != Status::Succeeded) {
            return status;
        }
        if (!empty) {
            return Status::DirectoryNotEmpty;
        }
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    RootDirectoryEntry empty_entry{};
    empty_entry.type = RootNodeType::Unused;
    status = file_system.WriteDirectoryEntryAt(directory.identifier, directory_inode,
                                               location.offset_bytes, empty_entry);
    if (status != Status::Succeeded) {
        return status;
    }
    status = file_system.RemoveInodeInTransaction(location.entry.inode_number, child_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    status = file_system.TrimDirectoryTail(directory.identifier, directory_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    status = file_system.CommitTransaction();
    if (status == Status::Succeeded) {
        ++file_system.statistics_.remove_count;
    }
    return status;
}

Status RootFileSystem::RenameOperation(void *const context, const Vnode &source_directory,
                                       const uint8_t *const source_name,
                                       const uint64_t source_name_length_bytes,
                                       const Vnode &destination_directory,
                                       const uint8_t *const destination_name,
                                       const uint64_t destination_name_length_bytes,
                                       const bool replace) noexcept {
    if (context == nullptr || !NameIsValid(source_name, source_name_length_bytes) ||
        !NameIsValid(destination_name, destination_name_length_bytes)) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode source_parent_inode{};
    Status status = file_system.ValidateVnode(source_directory, source_parent_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    const bool same_parent = source_directory.identifier == destination_directory.identifier &&
                             source_directory.generation == destination_directory.generation;
    RootInode destination_parent_storage{};
    RootInode *destination_parent_inode = &source_parent_inode;
    if (!same_parent) {
        status = file_system.ValidateVnode(destination_directory, destination_parent_storage);
        if (status != Status::Succeeded) {
            return status;
        }
        destination_parent_inode = &destination_parent_storage;
    }
    if (source_parent_inode.type != RootNodeType::Directory ||
        destination_parent_inode->type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }

    DirectoryEntryLocation source_location{};
    status = file_system.FindDirectoryEntry(source_parent_inode, source_name,
                                            source_name_length_bytes, source_location);
    if (status != Status::Succeeded) {
        return status;
    }
    DirectoryEntryLocation destination_location{};
    const Status destination_lookup_status =
        file_system.FindDirectoryEntry(*destination_parent_inode, destination_name,
                                       destination_name_length_bytes, destination_location);
    if (destination_lookup_status != Status::Succeeded &&
        destination_lookup_status != Status::NotFound) {
        return destination_lookup_status;
    }
    if (destination_lookup_status == Status::Succeeded &&
        destination_location.entry.inode_number == source_location.entry.inode_number &&
        destination_location.entry.inode_generation == source_location.entry.inode_generation) {
        return Status::Succeeded;
    }
    RootInode source_inode{};
    status = file_system.ReadInode(source_location.entry.inode_number, source_inode);
    if (status != Status::Succeeded ||
        source_inode.generation != source_location.entry.inode_generation ||
        source_inode.type != source_location.entry.type ||
        source_inode.parent_inode_number != source_directory.identifier) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    if (source_inode.type == RootNodeType::Directory) {
        uint64_t ancestor_inode_number = destination_directory.identifier;
        for (uint64_t traversal_count = OS_KERNEL_ROOTFS_FIRST_INDEX;
             traversal_count < OS_KERNEL_ROOTFS_MAXIMUM_TRAVERSAL_COUNT; ++traversal_count) {
            if (ancestor_inode_number == source_location.entry.inode_number) {
                return Status::LoopDetected;
            }
            if (ancestor_inode_number == OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER) {
                break;
            }
            RootInode ancestor{};
            status = file_system.ReadInode(ancestor_inode_number, ancestor);
            if (status != Status::Succeeded || ancestor.type != RootNodeType::Directory) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            ancestor_inode_number = ancestor.parent_inode_number;
        }
        if (ancestor_inode_number != OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER) {
            return Status::LoopDetected;
        }
    }

    RootInode destination_inode{};
    const bool destination_exists = destination_lookup_status == Status::Succeeded;
    if (destination_exists) {
        if (!replace) {
            return Status::AlreadyExists;
        }
        status = file_system.ReadInode(destination_location.entry.inode_number, destination_inode);
        if (status != Status::Succeeded ||
            destination_inode.generation != destination_location.entry.inode_generation ||
            destination_inode.type != destination_location.entry.type ||
            destination_inode.parent_inode_number != destination_directory.identifier) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        if (destination_inode.type != source_inode.type) {
            return source_inode.type == RootNodeType::Directory ? Status::NotDirectory
                                                                : Status::IsDirectory;
        }
        if (file_system.open_counts_[destination_location.entry.inode_number -
                                     OS_KERNEL_ROOTFS_COUNTER_INCREMENT] !=
            OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return Status::Busy;
        }
        if (destination_inode.type == RootNodeType::Directory) {
            bool empty = false;
            status = file_system.DirectoryIsEmpty(destination_inode, empty);
            if (status != Status::Succeeded) {
                return status;
            }
            if (!empty) {
                return Status::DirectoryNotEmpty;
            }
        }
    } else {
        status = file_system.FindDirectorySlot(*destination_parent_inode,
                                               destination_location.offset_bytes);
        if (status != Status::Succeeded) {
            return status;
        }
        const uint64_t first_logical_block =
            destination_location.offset_bytes / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        const uint64_t last_logical_block =
            (destination_location.offset_bytes + OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES -
             OS_KERNEL_ROOTFS_COUNTER_INCREMENT) /
            OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        uint64_t required_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        for (uint64_t logical_block = first_logical_block; logical_block <= last_logical_block;
             ++logical_block) {
            uint64_t block_requirement = OS_KERNEL_ROOTFS_EMPTY_VALUE;
            status = file_system.RequiredBlocksForLogicalBlock(*destination_parent_inode,
                                                               logical_block, block_requirement);
            if (status != Status::Succeeded ||
                required_block_count > UINT64_MAX - block_requirement) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            required_block_count += block_requirement;
        }
        if (required_block_count > file_system.statistics_.free_data_block_count) {
            return Status::CapacityExhausted;
        }
    }

    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    if (destination_exists) {
        status = file_system.RemoveInodeInTransaction(destination_location.entry.inode_number,
                                                      destination_inode);
        if (status != Status::Succeeded) {
            return status;
        }
    }
    RootDirectoryEntry destination_entry{
        .inode_number = source_location.entry.inode_number,
        .inode_generation = source_location.entry.inode_generation,
        .type = source_location.entry.type,
        .name_length_bytes = destination_name_length_bytes,
        .name = {},
    };
    CopyBytes(destination_entry.name, destination_name, destination_name_length_bytes);
    status = file_system.WriteDirectoryEntryAt(
        destination_directory.identifier, *destination_parent_inode,
        destination_location.offset_bytes, destination_entry);
    if (status != Status::Succeeded) {
        return status;
    }
    RootDirectoryEntry empty_entry{};
    empty_entry.type = RootNodeType::Unused;
    status = file_system.WriteDirectoryEntryAt(source_directory.identifier, source_parent_inode,
                                               source_location.offset_bytes, empty_entry);
    if (status != Status::Succeeded) {
        return status;
    }
    if (!same_parent) {
        source_inode.parent_inode_number = destination_directory.identifier;
        status = file_system.WriteInode(source_location.entry.inode_number, source_inode);
        if (status != Status::Succeeded) {
            return status;
        }
    }
    status = file_system.TrimDirectoryTail(source_directory.identifier, source_parent_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (!same_parent) {
        status = file_system.TrimDirectoryTail(destination_directory.identifier,
                                               *destination_parent_inode);
        if (status != Status::Succeeded) {
            return status;
        }
    }
    status = file_system.CommitTransaction();
    if (status == Status::Succeeded) {
        ++file_system.statistics_.rename_count;
    }
    return status;
}

Status RootFileSystem::ParentOperation(void *const context, const Vnode &vnode,
                                       Vnode &parent) noexcept {
    parent = Vnode{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    RootInode inode{};
    Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    RootInode parent_inode{};
    status = file_system.ReadInode(inode.parent_inode_number, parent_inode);
    if (status != Status::Succeeded || parent_inode.type != RootNodeType::Directory) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    parent = file_system.MakeVnode(inode.parent_inode_number, parent_inode);
    return Status::Succeeded;
}

Status RootFileSystem::ReadOperation(void *const context, const Vnode &vnode,
                                     const uint64_t offset_bytes, uint8_t *const destination,
                                     const uint64_t capacity_bytes, uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (context == nullptr ||
        (destination == nullptr && capacity_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE)) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    RootInode inode{};
    const Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (inode.type == RootNodeType::Directory) {
        return Status::IsDirectory;
    }
    if (inode.type != RootNodeType::RegularFile) {
        return Status::Corrupt;
    }
    return file_system.ReadFileBytes(inode, offset_bytes, destination, capacity_bytes, read_bytes);
}

Status RootFileSystem::WriteOperation(void *const context, const Vnode &vnode,
                                      const uint64_t offset_bytes, const uint8_t *const source,
                                      const uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (context == nullptr || (source == nullptr && length_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE)) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode inode{};
    Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (inode.type == RootNodeType::Directory) {
        return Status::IsDirectory;
    }
    if (inode.type != RootNodeType::RegularFile) {
        return Status::Corrupt;
    }
    if (length_bytes == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    if (offset_bytes > OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES ||
        length_bytes > OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES - offset_bytes) {
        return Status::FileTooLarge;
    }
    uint64_t first_required_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.RequiredBlocksForLogicalBlock(
        inode, offset_bytes / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES, first_required_block_count);
    if (status != Status::Succeeded) {
        return status;
    }
    if (first_required_block_count > file_system.statistics_.free_data_block_count) {
        return Status::CapacityExhausted;
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    status = file_system.WriteFileBytesInTransaction(vnode.identifier, inode, offset_bytes, source,
                                                     length_bytes, written_bytes);
    if (status != Status::Succeeded) {
        return status;
    }
    return file_system.CommitTransaction();
}

Status RootFileSystem::TruncateOperation(void *const context, const Vnode &vnode,
                                         const uint64_t size_bytes) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode inode{};
    Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (inode.type == RootNodeType::Directory) {
        return Status::IsDirectory;
    }
    if (inode.type != RootNodeType::RegularFile) {
        return Status::Corrupt;
    }
    if (size_bytes > OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES) {
        return Status::FileTooLarge;
    }
    if (size_bytes == inode.size_bytes) {
        return Status::Succeeded;
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    status = file_system.TruncateInTransaction(vnode.identifier, inode, size_bytes);
    if (status != Status::Succeeded) {
        return status;
    }
    status = file_system.CommitTransaction();
    if (status == Status::Succeeded) {
        ++file_system.statistics_.truncate_count;
    }
    return status;
}

Status RootFileSystem::ReadDirectoryOperation(void *const context, const Vnode &directory,
                                              uint64_t &cursor, DirectoryEntry &entry,
                                              bool &end_of_directory) noexcept {
    entry = DirectoryEntry{};
    end_of_directory = false;
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    RootInode directory_inode{};
    Status status = file_system.ValidateVnode(directory, directory_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (directory_inode.type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }
    if (cursor % OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES != OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        cursor > directory_inode.size_bytes) {
        return Status::InvalidHandle;
    }
    while (cursor < directory_inode.size_bytes) {
        RootDirectoryEntry disk_entry{};
        status = file_system.ReadDirectoryEntryAt(directory_inode, cursor, disk_entry);
        if (status != Status::Succeeded) {
            return status;
        }
        cursor += OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES;
        if (disk_entry.type == RootNodeType::Unused) {
            continue;
        }
        entry = DirectoryEntry{
            .node_identifier = disk_entry.inode_number,
            .type = RootFileSystem::ToVfsNodeType(disk_entry.type),
            .name_length_bytes = disk_entry.name_length_bytes,
            .name = {},
        };
        if (entry.type == NodeType::None) {
            return Status::Corrupt;
        }
        CopyBytes(entry.name, disk_entry.name, disk_entry.name_length_bytes);
        return Status::Succeeded;
    }
    end_of_directory = true;
    return Status::Succeeded;
}

Status RootFileSystem::GetNameOperation(void *const context, const Vnode &vnode,
                                        uint8_t *const name, const uint64_t name_capacity_bytes,
                                        uint64_t &name_length_bytes) noexcept {
    name_length_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (context == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    RootInode inode{};
    Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (vnode.identifier == OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER) {
        return Status::Succeeded;
    }
    RootInode parent_inode{};
    status = file_system.ReadInode(inode.parent_inode_number, parent_inode);
    if (status != Status::Succeeded || parent_inode.type != RootNodeType::Directory) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    for (uint64_t offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
         offset_bytes < parent_inode.size_bytes;
         offset_bytes += OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        RootDirectoryEntry entry{};
        status = file_system.ReadDirectoryEntryAt(parent_inode, offset_bytes, entry);
        if (status != Status::Succeeded) {
            return status;
        }
        if (entry.inode_number == vnode.identifier && entry.inode_generation == vnode.generation &&
            entry.type == inode.type) {
            if (entry.name_length_bytes > name_capacity_bytes) {
                return Status::NameTooLong;
            }
            CopyBytes(name, entry.name, entry.name_length_bytes);
            name_length_bytes = entry.name_length_bytes;
            return Status::Succeeded;
        }
    }
    return Status::Corrupt;
}

Status RootFileSystem::StatOperation(void *const context, const Vnode &vnode,
                                     BackendNodeInformation &information) noexcept {
    information = BackendNodeInformation{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    RootInode inode{};
    const Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    const uint64_t allocated_block_count =
        inode.allocated_data_block_count + inode.allocated_metadata_block_count;
    if (allocated_block_count > UINT64_MAX / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) {
        return Status::Corrupt;
    }
    information = BackendNodeInformation{
        .size_bytes = inode.size_bytes,
        .allocated_size_bytes = allocated_block_count * OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
        .link_count = inode.link_count,
    };
    return Status::Succeeded;
}

Status RootFileSystem::SyncOperation(void *const context) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    if (!file_system.initialized_ || file_system.device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (file_system.failed_) {
        return Status::DeviceFailure;
    }
    return file_system.cache_.Sync() == BlockCacheStatus::Succeeded
               ? Status::Succeeded
               : file_system.FailDeviceOperation();
}

Status RootFileSystem::ValidateOperation(void *const context) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    return file_system.ValidateUnlocked();
}

Status RootFileSystem::ReadResourceUsageOperation(void *const context,
                                                  ResourceUsage &usage) noexcept {
    usage = ResourceUsage{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const RootFileSystem &file_system = *static_cast<const RootFileSystem *>(context);
    SpinLockGuard guard{file_system.lock_};
    if (!file_system.initialized_) {
        return Status::NotInitialized;
    }
    usage.vnode_count = file_system.statistics_.allocated_inode_count;
    return Status::Succeeded;
}

Status RootFileSystem::MarkValidationDataBlock(const uint64_t relative_block) noexcept {
    if (relative_block < OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK ||
        relative_block >= OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT) {
        return Status::Corrupt;
    }
    const uint64_t bit_index = relative_block - OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK;
    if (BitmapBitIsSet(this->validation_data_bitmap_, bit_index)) {
        return Status::Corrupt;
    }
    // 这里只建立“可达块集合”并检查重复引用；盘面分配位图在最终逐块比较时
    // 一次性核对，避免对每个数据块重复复制整张 512 字节位图块。
    SetBitmapBit(this->validation_data_bitmap_, bit_index, true);
    return Status::Succeeded;
}

Status RootFileSystem::ValidatePointerTree(const uint64_t relative_block, const uint64_t level,
                                           const uint64_t logical_start,
                                           const uint64_t logical_limit,
                                           uint64_t &metadata_block_count,
                                           uint64_t &data_block_count) noexcept {
    if (relative_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    if (level == OS_KERNEL_ROOTFS_EMPTY_VALUE || level > OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL) {
        return Status::Corrupt;
    }
    Status status = this->MarkValidationDataBlock(relative_block);
    if (status != Status::Succeeded) {
        return status;
    }
    ++metadata_block_count;
    RootPointerBlock pointer_block{};
    status = this->ReadPointerBlock(relative_block, pointer_block);
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t child_span = OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    for (uint64_t span_level = OS_KERNEL_ROOTFS_COUNTER_INCREMENT; span_level < level;
         ++span_level) {
        child_span *= OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    }
    for (uint64_t pointer_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         pointer_index < OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK; ++pointer_index) {
        const uint64_t child_block = pointer_block.pointers[pointer_index];
        if (child_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            continue;
        }
        const uint64_t child_logical_start = logical_start + pointer_index * child_span;
        if (child_logical_start >= logical_limit) {
            return Status::Corrupt;
        }
        if (level == OS_KERNEL_ROOTFS_COUNTER_INCREMENT) {
            status = this->MarkValidationDataBlock(child_block);
            if (status != Status::Succeeded) {
                return status;
            }
            ++data_block_count;
        } else {
            status = this->ValidatePointerTree(
                child_block, level - OS_KERNEL_ROOTFS_COUNTER_INCREMENT, child_logical_start,
                logical_limit, metadata_block_count, data_block_count);
            if (status != Status::Succeeded) {
                return status;
            }
        }
    }
    return Status::Succeeded;
}

Status RootFileSystem::ValidateInodeBlocks(const RootInode &inode, uint64_t &metadata_block_count,
                                           uint64_t &data_block_count) noexcept {
    metadata_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    data_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const uint64_t logical_limit =
        DivideRoundUp(inode.size_bytes, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    for (uint64_t block_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         block_index < OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT; ++block_index) {
        const uint64_t relative_block = inode.direct_blocks[block_index];
        if (relative_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            continue;
        }
        if (block_index >= logical_limit) {
            return Status::Corrupt;
        }
        const Status status = this->MarkValidationDataBlock(relative_block);
        if (status != Status::Succeeded) {
            return status;
        }
        ++data_block_count;
    }
    uint64_t logical_start = OS_KERNEL_ROOTFS_DIRECT_BLOCK_COUNT;
    Status status = this->ValidatePointerTree(
        inode.single_indirect_block, OS_KERNEL_ROOTFS_COUNTER_INCREMENT, logical_start,
        logical_limit, metadata_block_count, data_block_count);
    if (status != Status::Succeeded) {
        return status;
    }
    logical_start += OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    status = this->ValidatePointerTree(inode.double_indirect_block,
                                       OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL, logical_start,
                                       logical_limit, metadata_block_count, data_block_count);
    if (status != Status::Succeeded) {
        return status;
    }
    logical_start +=
        OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
    status = this->ValidatePointerTree(inode.triple_indirect_block,
                                       OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL, logical_start,
                                       logical_limit, metadata_block_count, data_block_count);
    if (status != Status::Succeeded) {
        return status;
    }
    return metadata_block_count == inode.allocated_metadata_block_count &&
                   data_block_count == inode.allocated_data_block_count
               ? Status::Succeeded
               : Status::Corrupt;
}

Status RootFileSystem::CompareValidationBitmaps(uint64_t &allocated_inode_count,
                                                uint64_t &allocated_block_count) noexcept {
    allocated_inode_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    allocated_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    for (uint64_t bitmap_block_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         bitmap_block_index < OS_KERNEL_ROOTFS_INODE_BITMAP_BLOCK_COUNT; ++bitmap_block_index) {
        uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
        const Status status = this->ReadRelativeBlock(
            OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK + bitmap_block_index, block);
        if (status != Status::Succeeded) {
            return status;
        }
        const uint64_t byte_start = bitmap_block_index * OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        for (uint64_t byte_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
             byte_index < OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES; ++byte_index) {
            const uint8_t actual = block[byte_index];
            const uint8_t expected = this->validation_inode_bitmap_[byte_start + byte_index];
            if (actual != expected) {
                return Status::Corrupt;
            }
            for (uint64_t bit_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
                 bit_index < OS_KERNEL_ROOTFS_BITS_PER_BYTE; ++bit_index) {
                if ((actual & static_cast<uint8_t>(1ULL << bit_index)) !=
                    OS_KERNEL_ROOTFS_ZERO_BYTE) {
                    ++allocated_inode_count;
                }
            }
        }
    }
    for (uint64_t bitmap_block_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         bitmap_block_index < OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT; ++bitmap_block_index) {
        uint8_t block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
        const Status status = this->ReadRelativeBlock(
            OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK + bitmap_block_index, block);
        if (status != Status::Succeeded) {
            return status;
        }
        const uint64_t byte_start = bitmap_block_index * OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        for (uint64_t byte_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
             byte_index < OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES; ++byte_index) {
            const uint8_t actual = block[byte_index];
            const uint8_t expected = this->validation_data_bitmap_[byte_start + byte_index];
            if (actual != expected) {
                return Status::Corrupt;
            }
            const uint64_t absolute_byte_index = byte_start + byte_index;
            for (uint64_t bit_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
                 bit_index < OS_KERNEL_ROOTFS_BITS_PER_BYTE; ++bit_index) {
                const uint64_t data_bit_index =
                    absolute_byte_index * OS_KERNEL_ROOTFS_BITS_PER_BYTE + bit_index;
                if (data_bit_index < OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT &&
                    (actual & static_cast<uint8_t>(1ULL << bit_index)) !=
                        OS_KERNEL_ROOTFS_ZERO_BYTE) {
                    ++allocated_block_count;
                }
            }
        }
    }
    return Status::Succeeded;
}

Status RootFileSystem::ValidateUnlocked() noexcept {
    if (!this->initialized_ || this->device_ == nullptr || this->failed_ ||
        !this->vfs_superblock_.initialized || this->vfs_superblock_.backend_context != this ||
        this->vfs_superblock_.operations != &RootFileSystem::operations ||
        this->disk_superblock_.transaction_state != RootTransactionState::Clean) {
        return this->failed_ ? Status::DeviceFailure : Status::Corrupt;
    }
    ClearBytes(this->validation_inode_bitmap_, sizeof(this->validation_inode_bitmap_));
    ClearBytes(this->validation_data_bitmap_, sizeof(this->validation_data_bitmap_));
    SetBitmapBit(this->validation_inode_bitmap_,
                 OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER - OS_KERNEL_ROOTFS_COUNTER_INCREMENT, true);
    this->validation_queue_[OS_KERNEL_ROOTFS_FIRST_INDEX] = OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER;
    uint64_t queue_read_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
    uint64_t queue_write_index = OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    uint64_t maximum_inode_generation = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    uint64_t validated_data_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    uint64_t validated_metadata_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const uint64_t bytes_read_before = this->statistics_.bytes_read;
    const uint64_t hole_bytes_before = this->statistics_.sparse_hole_read_bytes;

    while (queue_read_index < queue_write_index) {
        const uint64_t inode_number = this->validation_queue_[queue_read_index];
        ++queue_read_index;
        bool allocated = false;
        Status status =
            this->ReadBitmapBit(true, inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT, allocated);
        if (status != Status::Succeeded || !allocated) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        RootInode inode{};
        status = this->ReadInode(inode_number, inode);
        if (status != Status::Succeeded || inode.type == RootNodeType::Unused ||
            inode.link_count != OS_KERNEL_ROOTFS_COUNTER_INCREMENT) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        if (inode.generation > maximum_inode_generation) {
            maximum_inode_generation = inode.generation;
        }
        uint64_t metadata_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        uint64_t data_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        status = this->ValidateInodeBlocks(inode, metadata_block_count, data_block_count);
        if (status != Status::Succeeded ||
            validated_metadata_block_count > UINT64_MAX - metadata_block_count ||
            validated_data_block_count > UINT64_MAX - data_block_count) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        validated_metadata_block_count += metadata_block_count;
        validated_data_block_count += data_block_count;

        if (inode_number == OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER &&
            (inode.type != RootNodeType::Directory ||
             inode.parent_inode_number != OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER)) {
            return Status::Corrupt;
        }
        if (inode.type != RootNodeType::Directory) {
            continue;
        }
        if (inode.size_bytes % OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES !=
            OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return Status::Corrupt;
        }
        for (uint64_t offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE; offset_bytes < inode.size_bytes;
             offset_bytes += OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
            RootDirectoryEntry entry{};
            status = this->ReadDirectoryEntryAt(inode, offset_bytes, entry);
            if (status != Status::Succeeded) {
                return status;
            }
            if (entry.type == RootNodeType::Unused) {
                continue;
            }
            if (entry.inode_number == OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER ||
                entry.inode_number > OS_KERNEL_ROOTFS_INODE_COUNT) {
                return Status::Corrupt;
            }
            for (uint64_t previous_offset = OS_KERNEL_ROOTFS_EMPTY_VALUE;
                 previous_offset < offset_bytes;
                 previous_offset += OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
                RootDirectoryEntry previous_entry{};
                status = this->ReadDirectoryEntryAt(inode, previous_offset, previous_entry);
                if (status != Status::Succeeded) {
                    return status;
                }
                if (previous_entry.type != RootNodeType::Unused &&
                    previous_entry.name_length_bytes == entry.name_length_bytes &&
                    BytesAreEqual(previous_entry.name, entry.name, entry.name_length_bytes)) {
                    return Status::Corrupt;
                }
            }
            const uint64_t child_bit_index =
                entry.inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
            if (BitmapBitIsSet(this->validation_inode_bitmap_, child_bit_index) ||
                queue_write_index >= OS_KERNEL_ROOTFS_INODE_COUNT) {
                return Status::Corrupt;
            }
            RootInode child_inode{};
            status = this->ReadInode(entry.inode_number, child_inode);
            if (status != Status::Succeeded || child_inode.generation != entry.inode_generation ||
                child_inode.type != entry.type || child_inode.parent_inode_number != inode_number) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            SetBitmapBit(this->validation_inode_bitmap_, child_bit_index, true);
            this->validation_queue_[queue_write_index] = entry.inode_number;
            ++queue_write_index;
        }
    }

    uint64_t bitmap_inode_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    uint64_t bitmap_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const Status bitmap_status =
        this->CompareValidationBitmaps(bitmap_inode_count, bitmap_block_count);
    if (bitmap_status != Status::Succeeded || bitmap_inode_count != queue_write_index ||
        bitmap_block_count != validated_data_block_count + validated_metadata_block_count ||
        this->disk_superblock_.next_inode_generation <= maximum_inode_generation) {
        return bitmap_status == Status::Succeeded ? Status::Corrupt : bitmap_status;
    }
    uint64_t open_reference_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    for (uint64_t inode_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         inode_index < OS_KERNEL_ROOTFS_INODE_COUNT; ++inode_index) {
        if (this->open_counts_[inode_index] != OS_KERNEL_ROOTFS_EMPTY_VALUE &&
            !BitmapBitIsSet(this->validation_inode_bitmap_, inode_index)) {
            return Status::Corrupt;
        }
        if (open_reference_count > UINT64_MAX - this->open_counts_[inode_index]) {
            return Status::Corrupt;
        }
        open_reference_count += this->open_counts_[inode_index];
    }
    if (open_reference_count != this->statistics_.open_reference_count) {
        return Status::Corrupt;
    }
    this->statistics_.transaction_generation = this->disk_superblock_.transaction_generation;
    this->statistics_.allocated_inode_count = bitmap_inode_count;
    this->statistics_.allocated_data_block_count = validated_data_block_count;
    this->statistics_.allocated_metadata_block_count = validated_metadata_block_count;
    this->statistics_.free_data_block_count =
        OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT - bitmap_block_count;
    this->statistics_.bytes_read = bytes_read_before;
    this->statistics_.sparse_hole_read_bytes = hole_bytes_before;
    RootInode root_inode{};
    const Status root_status = this->ReadInode(OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER, root_inode);
    if (root_status != Status::Succeeded || !this->vfs_superblock_.initialized ||
        this->vfs_superblock_.root.identifier != OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER ||
        this->vfs_superblock_.root.generation != root_inode.generation ||
        this->vfs_superblock_.root.type != NodeType::Directory) {
        return root_status == Status::Succeeded ? Status::Corrupt : root_status;
    }
    return Status::Succeeded;
}

}
