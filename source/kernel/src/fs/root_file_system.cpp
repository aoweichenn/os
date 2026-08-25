#include <os/kernel/fs/root_file_system.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_ROOTFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_WAIT_QUEUE_IDENTIFIER = 0x8000000000000102ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_MAXIMUM_TRAVERSAL_COUNT = OS_KERNEL_ROOTFS_INODE_COUNT;
constexpr uint64_t OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT =
    OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER;
constexpr uint64_t OS_KERNEL_ROOTFS_SINGLE_INDIRECT_LEVEL = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL = 2ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL = 3ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_LEVEL = 4ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL = 5ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_SINGLE_INDIRECT_CAPACITY =
    OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
constexpr uint64_t OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY =
    OS_KERNEL_ROOTFS_SINGLE_INDIRECT_CAPACITY * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
constexpr uint64_t OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY =
    OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
constexpr uint64_t OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY =
    OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY * OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK;
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

uint64_t RootFileSystem::ActiveBlockSizeBytes() const noexcept {
    return this->v5_active_ ? OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES
                            : OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
}

uint64_t RootFileSystem::ActiveRootInodeNumber() const noexcept {
    return this->v5_active_ ? OS_KERNEL_ROOTFS_V5_ROOT_INODE_NUMBER
                            : OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER;
}

uint64_t RootFileSystem::ReadOpenReferenceCount(const uint64_t inode_number) const noexcept {
    if (!this->v5_active_) {
        return inode_number == 0ULL || inode_number > OS_KERNEL_ROOTFS_INODE_COUNT
                   ? 0ULL
                   : this->open_counts_[inode_number - 1ULL];
    }
    for (uint64_t slot_index = 0ULL; slot_index < OS_KERNEL_ROOTFS_V5_OPEN_REFERENCE_CAPACITY;
         ++slot_index) {
        if (this->v5_open_references_[slot_index].inode_number == inode_number) {
            return this->v5_open_references_[slot_index].count;
        }
    }
    return 0ULL;
}

Status RootFileSystem::RetainOpenReference(const uint64_t inode_number) noexcept {
    if (!this->v5_active_) {
        if (inode_number == 0ULL || inode_number > OS_KERNEL_ROOTFS_INODE_COUNT ||
            this->open_counts_[inode_number - 1ULL] == UINT64_MAX) {
            return Status::CapacityExhausted;
        }
        ++this->open_counts_[inode_number - 1ULL];
        return Status::Succeeded;
    }
    V5OpenReference *free_slot = nullptr;
    for (uint64_t slot_index = 0ULL; slot_index < OS_KERNEL_ROOTFS_V5_OPEN_REFERENCE_CAPACITY;
         ++slot_index) {
        V5OpenReference &slot = this->v5_open_references_[slot_index];
        if (slot.inode_number == inode_number) {
            if (slot.count == UINT64_MAX) {
                return Status::CapacityExhausted;
            }
            ++slot.count;
            return Status::Succeeded;
        }
        if (slot.inode_number == 0ULL && free_slot == nullptr) {
            free_slot = &slot;
        }
    }
    if (free_slot == nullptr) {
        return Status::CapacityExhausted;
    }
    *free_slot = V5OpenReference{.inode_number = inode_number, .count = 1ULL};
    return Status::Succeeded;
}

Status RootFileSystem::ReleaseOpenReference(const uint64_t inode_number) noexcept {
    if (!this->v5_active_) {
        if (inode_number == 0ULL || inode_number > OS_KERNEL_ROOTFS_INODE_COUNT ||
            this->open_counts_[inode_number - 1ULL] == 0ULL) {
            return Status::InvalidHandle;
        }
        --this->open_counts_[inode_number - 1ULL];
        return Status::Succeeded;
    }
    for (uint64_t slot_index = 0ULL; slot_index < OS_KERNEL_ROOTFS_V5_OPEN_REFERENCE_CAPACITY;
         ++slot_index) {
        V5OpenReference &slot = this->v5_open_references_[slot_index];
        if (slot.inode_number != inode_number) {
            continue;
        }
        if (slot.count == 0ULL) {
            return Status::InvalidHandle;
        }
        --slot.count;
        if (slot.count == 0ULL) {
            slot = V5OpenReference{};
        }
        return Status::Succeeded;
    }
    return Status::InvalidHandle;
}

Status RootFileSystem::ReadV5Block(const uint64_t relative_block, uint8_t *const block) noexcept {
    if (this->device_ == nullptr || block == nullptr ||
        relative_block >= this->v5_superblock_.total_block_count) {
        return Status::InvalidArgument;
    }
    if (this->v5_journal_.IsActive() && this->v5_journal_.TryReadStagedMetadata(
                                               relative_block, block,
                                               OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES)) {
        return Status::Succeeded;
    }
    const uint64_t first_sector = OS_KERNEL_ROOTFS_V5_FILE_SYSTEM_START_LBA +
                                  relative_block * OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK;
    return this->device_->ReadBlock(first_sector, block, OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) ==
                   FileSystemBlockDeviceStatus::Succeeded
               ? Status::Succeeded
               : this->FailDeviceOperation();
}

Status RootFileSystem::WriteV5Block(const uint64_t relative_block,
                                    const uint8_t *const block) noexcept {
    if (this->device_ == nullptr || block == nullptr ||
        relative_block >= this->v5_superblock_.total_block_count) {
        return Status::InvalidArgument;
    }
    const uint64_t first_sector = OS_KERNEL_ROOTFS_V5_FILE_SYSTEM_START_LBA +
                                  relative_block * OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK;
    return this->device_->WriteBlock(first_sector, block, OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) ==
                   FileSystemBlockDeviceStatus::Succeeded
               ? Status::Succeeded
               : this->FailDeviceOperation();
}

Status RootFileSystem::LoadV5GroupDescriptors() noexcept {
    uint64_t loaded_relative_block = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    for (uint64_t group_index = 0ULL; group_index < this->v5_superblock_.group_count;
         ++group_index) {
        const uint64_t byte_offset =
            group_index * this->v5_superblock_.group_descriptor_size_bytes;
        const uint64_t relative_block =
            this->v5_superblock_.group_descriptor_table_start_block +
            byte_offset / OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
        const uint64_t block_offset = byte_offset % OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
        if ((relative_block != loaded_relative_block &&
             this->ReadV5Block(relative_block, this->read_block_scratch_) != Status::Succeeded) ||
            DecodeRootV5GroupDescriptor(
                this->v5_superblock_, this->read_block_scratch_ + block_offset,
                this->v5_superblock_.group_descriptor_size_bytes,
                this->v5_group_descriptors_[group_index]) != RootV5FormatStatus::Succeeded) {
            return Status::Corrupt;
        }
        loaded_relative_block = relative_block;
    }
    return Status::Succeeded;
}

Status RootFileSystem::StageV5GroupDescriptor(const uint64_t group_index) noexcept {
    if (!this->v5_active_ || group_index >= this->v5_superblock_.group_count) {
        return Status::InvalidArgument;
    }
    const uint64_t byte_offset =
        group_index * this->v5_superblock_.group_descriptor_size_bytes;
    const uint64_t relative_block = this->v5_superblock_.group_descriptor_table_start_block +
                                    byte_offset / OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
    const uint64_t block_offset = byte_offset % OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
    Status status = this->ReadRelativeBlock(relative_block, this->write_block_scratch_);
    if (status != Status::Succeeded) {
        return status;
    }
    if (EncodeRootV5GroupDescriptor(
            this->v5_superblock_, this->v5_group_descriptors_[group_index],
            this->write_block_scratch_ + block_offset,
            this->v5_superblock_.group_descriptor_size_bytes) != RootV5FormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    return this->WriteMetadataBlock(relative_block, this->write_block_scratch_);
}

const BackendOperations RootFileSystem::operations{
    .lookup = RootFileSystem::LookupOperation,
    .create = RootFileSystem::CreateOperation,
    .open = RootFileSystem::OpenOperation,
    .close = RootFileSystem::CloseOperation,
    .remove = RootFileSystem::RemoveOperation,
    .rename = RootFileSystem::RenameOperation,
    .link = RootFileSystem::LinkOperation,
    .create_symbolic_link = RootFileSystem::CreateSymbolicLinkOperation,
    .read_symbolic_link = RootFileSystem::ReadSymbolicLinkOperation,
    .parent = RootFileSystem::ParentOperation,
    .read = RootFileSystem::ReadOperation,
    .write = RootFileSystem::WriteOperation,
    .truncate = RootFileSystem::TruncateOperation,
    .read_directory = RootFileSystem::ReadDirectoryOperation,
    .get_name = RootFileSystem::GetNameOperation,
    .stat = RootFileSystem::StatOperation,
    .change_mode = RootFileSystem::ChangeModeOperation,
    .change_owner = RootFileSystem::ChangeOwnerOperation,
    .sync = RootFileSystem::SyncOperation,
    .validate = RootFileSystem::ValidateOperation,
    .read_resource_usage = RootFileSystem::ReadResourceUsageOperation,
};

Status RootFileSystem::Initialize(FileSystemBlockDevice &device,
                                  const uint64_t superblock_identifier, const bool read_only,
                                  const RootTimestampSource timestamp_source) noexcept {
    if (this->initialized_) {
        return Status::AlreadyInitialized;
    }
    if (superblock_identifier == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    this->device_ = &device;
    this->timestamp_source_ = timestamp_source;
    bool v5_read_succeeded = true;
    for (uint64_t sector_index = 0ULL; sector_index < OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK;
         ++sector_index) {
        if (device.ReadBlock(
                OS_KERNEL_ROOTFS_V5_FILE_SYSTEM_START_LBA + sector_index,
                this->initialization_block_scratch_ +
                    sector_index * OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES,
                OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES) != FileSystemBlockDeviceStatus::Succeeded) {
            v5_read_succeeded = false;
            break;
        }
    }
    if (v5_read_succeeded &&
        DecodeRootV5Superblock(this->initialization_block_scratch_,
                               OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES, this->v5_superblock_) ==
            RootV5FormatStatus::Succeeded) {
        this->v5_active_ = true;
        if (this->ReadV5Block(this->v5_superblock_.group_descriptor_table_start_block,
                              this->read_block_scratch_) != Status::Succeeded ||
            DecodeRootV5GroupDescriptor(
                this->v5_superblock_, this->read_block_scratch_,
                this->v5_superblock_.group_descriptor_size_bytes,
                this->v5_group_descriptors_[0]) != RootV5FormatStatus::Succeeded) {
            this->device_ = nullptr;
            this->v5_active_ = false;
            return Status::Corrupt;
        }
        const uint64_t journal_start = this->v5_group_descriptors_[0].data_start_block;
        if (this->v5_journal_.Initialize(
                device, this->v5_superblock_.file_system_start_lba,
                this->v5_superblock_.total_block_count, this->v5_superblock_.inode_count,
                journal_start, this->v5_superblock_.uuid) != RootJournalV2Status::Succeeded ||
            this->v5_journal_.Open() != RootJournalV2Status::Succeeded) {
            this->device_ = nullptr;
            this->v5_active_ = false;
            return Status::Corrupt;
        }
        RootJournalV2RecoveryResult recovery_result = RootJournalV2RecoveryResult::Clean;
        const RootJournalV2Status recovery_status = this->v5_journal_.Recover(recovery_result);
        if (recovery_status != RootJournalV2Status::Succeeded) {
            this->device_ = nullptr;
            this->v5_active_ = false;
            return recovery_status == RootJournalV2Status::DeviceReadFailed ||
                           recovery_status == RootJournalV2Status::DeviceWriteFailed ||
                           recovery_status == RootJournalV2Status::DeviceFlushFailed
                       ? Status::DeviceFailure
                       : Status::Corrupt;
        }
        if (this->ReadV5Block(0ULL, this->initialization_block_scratch_) != Status::Succeeded ||
            DecodeRootV5Superblock(this->initialization_block_scratch_,
                                   OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
                                   this->v5_superblock_) != RootV5FormatStatus::Succeeded ||
            this->LoadV5GroupDescriptors() != Status::Succeeded) {
            this->device_ = nullptr;
            this->v5_active_ = false;
            return Status::Corrupt;
        }
        this->disk_superblock_ = RootSuperblock{
            .version = OS_KERNEL_ROOTFS_V5_FORMAT_VERSION,
            .block_size_bytes = OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
            .total_block_count = this->v5_superblock_.total_block_count,
            .journal_start_relative_block = journal_start,
            .journal_block_count = OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT,
            .inode_bitmap_start_relative_block = 0ULL,
            .inode_bitmap_block_count = this->v5_superblock_.group_count,
            .inode_table_start_relative_block = 0ULL,
            .inode_table_block_count = 0ULL,
            .data_bitmap_start_relative_block = 0ULL,
            .data_bitmap_block_count = this->v5_superblock_.group_count,
            .data_start_relative_block = 0ULL,
            .data_block_count = this->v5_superblock_.total_block_count,
            .inode_count = this->v5_superblock_.inode_count,
            .root_inode_number = this->v5_superblock_.root_inode_number,
            .maximum_file_size_bytes =
                this->v5_superblock_.total_block_count * this->v5_superblock_.block_size_bytes,
            .transaction_state = RootTransactionState::Clean,
            .transaction_generation = this->v5_journal_.Superblock().journal_generation,
            .next_inode_generation = this->v5_superblock_.format_generation + 1ULL,
            .feature_flags = OS_KERNEL_ROOTFS_REQUIRED_FEATURES,
            .allocated_inode_count =
                this->v5_superblock_.inode_count - this->v5_superblock_.free_inode_count,
            .allocated_data_block_count =
                this->v5_superblock_.total_block_count - this->v5_superblock_.free_block_count,
            .allocated_metadata_block_count = 0ULL,
        };
        if (this->lock_.Initialize(WaitQueueId{
                .value = OS_KERNEL_ROOTFS_WAIT_QUEUE_IDENTIFIER,
            }) != RuntimeMutexStatus::Succeeded) {
            this->device_ = nullptr;
            this->v5_active_ = false;
            return Status::Corrupt;
        }
        this->failed_ = false;
        this->statistics_ = RootFileSystemStatistics{};
        this->transaction_snapshot_valid_ = false;
        this->next_data_allocation_hint_ = journal_start + OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT;
        this->next_inode_allocation_hint_ = OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER - 1ULL;
        this->last_validated_transaction_generation_ = 0ULL;
        ClearBytes(reinterpret_cast<uint8_t *>(this->open_counts_), sizeof(this->open_counts_));
        ClearBytes(reinterpret_cast<uint8_t *>(this->v5_open_references_),
                   sizeof(this->v5_open_references_));
        this->initialized_ = true;
        this->statistics_.transaction_generation =
            this->v5_journal_.Superblock().journal_generation;
        this->statistics_.allocated_inode_count = this->disk_superblock_.allocated_inode_count;
        this->statistics_.allocated_data_block_count =
            this->disk_superblock_.allocated_data_block_count;
        this->statistics_.free_data_block_count = this->v5_superblock_.free_block_count;
        RootInode root_inode{};
        Status status = this->ReadInode(this->ActiveRootInodeNumber(), root_inode);
        if (status != Status::Succeeded || root_inode.type != RootNodeType::Directory ||
            root_inode.parent_inode_number != this->ActiveRootInodeNumber()) {
            this->initialized_ = false;
            this->device_ = nullptr;
            this->v5_active_ = false;
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        this->vfs_superblock_ = Superblock{
            .backend_kind = BackendKind::Root,
            .identifier = superblock_identifier,
            .generation = this->statistics_.transaction_generation,
            .root = {},
            .operations = &RootFileSystem::operations,
            .backend_context = this,
            .maximum_name_length_bytes = OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_NAME_LENGTH_BYTES,
            .cache_regular_file_data = true,
            .read_only = read_only,
            .initialized = true,
        };
        this->vfs_superblock_.root = this->MakeVnode(this->ActiveRootInodeNumber(), root_inode);
        if (!read_only) {
            status = this->ReapV5Orphans();
            if (status != Status::Succeeded) {
                this->initialized_ = false;
                this->vfs_superblock_ = Superblock{};
                this->device_ = nullptr;
                this->v5_active_ = false;
                return status;
            }
        }
        status = this->ValidateUnlocked();
        if (status != Status::Succeeded) {
            this->initialized_ = false;
            this->vfs_superblock_ = Superblock{};
            this->device_ = nullptr;
            this->v5_active_ = false;
            return status;
        }
        return Status::Succeeded;
    }
    this->v5_active_ = false;
    if (device.ReadBlock(OS_KERNEL_ROOTFS_START_LBA + OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK,
                         this->initialization_block_scratch_, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) !=
            FileSystemBlockDeviceStatus::Succeeded ||
        DecodeRootSuperblock(this->initialization_block_scratch_, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
                             this->disk_superblock_) != RootFormatStatus::Succeeded) {
        this->device_ = nullptr;
        return Status::Corrupt;
    }
    if (this->journal_.Initialize(device, OS_KERNEL_ROOTFS_START_LBA,
                                  this->disk_superblock_.total_block_count) !=
        RootJournalStatus::Succeeded) {
        this->device_ = nullptr;
        return Status::Corrupt;
    }
    RootJournalRecoveryResult recovery_result = RootJournalRecoveryResult::Clean;
    const RootJournalStatus recovery_status = this->journal_.Recover(recovery_result);
    if (recovery_status != RootJournalStatus::Succeeded) {
        this->device_ = nullptr;
        return recovery_status == RootJournalStatus::DeviceReadFailed ||
                       recovery_status == RootJournalStatus::DeviceWriteFailed ||
                       recovery_status == RootJournalStatus::DeviceFlushFailed
                   ? Status::DeviceFailure
                   : Status::Corrupt;
    }
    this->cache_.Initialize(device);
    if (this->lock_.Initialize(WaitQueueId{
            .value = OS_KERNEL_ROOTFS_WAIT_QUEUE_IDENTIFIER,
        }) != RuntimeMutexStatus::Succeeded) {
        this->cache_.Invalidate();
        this->device_ = nullptr;
        return Status::Corrupt;
    }
    this->failed_ = false;
    this->statistics_ = RootFileSystemStatistics{};
    this->transaction_snapshot_valid_ = false;
    this->next_data_allocation_hint_ = OS_KERNEL_ROOTFS_FIRST_INDEX;
    this->next_inode_allocation_hint_ = OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT;
    this->last_validated_transaction_generation_ = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    ClearBytes(reinterpret_cast<uint8_t *>(this->open_counts_), sizeof(this->open_counts_));
    ClearBytes(this->validation_inode_bitmap_, sizeof(this->validation_inode_bitmap_));
    ClearBytes(reinterpret_cast<uint8_t *>(this->validation_link_counts_),
               sizeof(this->validation_link_counts_));

    Status status = this->ReadRelativeBlock(OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK,
                                            this->initialization_block_scratch_);
    if (status != Status::Succeeded) {
        this->device_ = nullptr;
        return status;
    }
    const RootFormatStatus format_status =
        DecodeRootSuperblock(this->initialization_block_scratch_, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
                             this->disk_superblock_);
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
    status = this->LoadRecoveryStatistics();
    if (status == Status::Succeeded) {
        status = this->ReapOrphans();
    }
    if (status != Status::Succeeded) {
        this->initialized_ = false;
        this->cache_.Invalidate();
        this->device_ = nullptr;
        return status;
    }
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
        .cache_regular_file_data = true,
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
    RuntimeMutexGuard guard{this->lock_};
    RootFileSystemStatistics statistics = this->statistics_;
    if (this->v5_active_) {
        const RootJournalV2Statistics journal_statistics = this->v5_journal_.Statistics();
        statistics.journal = RootJournalStatistics{
            .transaction_begin_count = journal_statistics.transaction_begin_count,
            .transaction_commit_count = journal_statistics.transaction_commit_count,
            .transaction_abort_count = journal_statistics.transaction_abort_count,
            .staged_block_count = journal_statistics.metadata_stage_count,
            .checkpoint_block_count = journal_statistics.checkpoint_block_count,
            .replay_count = journal_statistics.replay_transaction_count,
            .discarded_incomplete_count = journal_statistics.discarded_incomplete_count,
            .checksum_failure_count = journal_statistics.checksum_failure_count,
            .credit_rejection_count = journal_statistics.capacity_rejection_count,
            .flush_count = journal_statistics.flush_count,
        };
    } else {
        statistics.cache = this->cache_.Statistics();
        statistics.journal = this->journal_.Statistics();
    }
    return statistics;
}

Status RootFileSystem::ReadRelativeBlock(const uint64_t relative_block,
                                         uint8_t *const block) noexcept {
    if (this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (block == nullptr || relative_block >= this->disk_superblock_.total_block_count) {
        return Status::InvalidArgument;
    }
    if (this->v5_active_) {
        return this->ReadV5Block(relative_block, block);
    }
    if (this->journal_.TryReadStaged(relative_block, block, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES)) {
        return Status::Succeeded;
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
    if (block == nullptr || relative_block >= this->disk_superblock_.total_block_count) {
        return Status::InvalidArgument;
    }
    if (this->v5_active_) {
        return this->WriteV5Block(relative_block, block);
    }
    const BlockCacheStatus status = this->cache_.WriteBlock(
        OS_KERNEL_ROOTFS_START_LBA + relative_block, block, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    return status == BlockCacheStatus::Succeeded ? Status::Succeeded : this->FailDeviceOperation();
}

Status RootFileSystem::WriteMetadataBlock(const uint64_t relative_block,
                                          const uint8_t *const block) noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (this->v5_active_) {
        const RootJournalV2Status status = this->v5_journal_.StageMetadata(
            relative_block, block, OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES);
        if (status == RootJournalV2Status::Succeeded) {
            return Status::Succeeded;
        }
        return status == RootJournalV2Status::CapacityExhausted ? Status::CapacityExhausted
               : status == RootJournalV2Status::InvalidArgument ? Status::InvalidArgument
                                                                : Status::IncompleteTransaction;
    }
    const RootJournalStatus status =
        this->journal_.Stage(relative_block, block, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    if (status == RootJournalStatus::Succeeded) {
        return Status::Succeeded;
    }
    if (status == RootJournalStatus::CreditsExhausted) {
        return Status::CapacityExhausted;
    }
    return status == RootJournalStatus::InvalidArgument ? Status::InvalidArgument
                                                        : Status::IncompleteTransaction;
}

Status RootFileSystem::StageSuperblock() noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (this->v5_active_) {
        if (EncodeRootV5Superblock(this->v5_superblock_, this->superblock_block_scratch_,
                                   OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) !=
            RootV5FormatStatus::Succeeded) {
            return Status::Corrupt;
        }
        return this->WriteMetadataBlock(0ULL, this->superblock_block_scratch_);
    }
    if (EncodeRootSuperblock(this->disk_superblock_, this->superblock_block_scratch_,
                             OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) !=
        RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    return this->WriteMetadataBlock(OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK,
                                    this->superblock_block_scratch_);
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
        this->disk_superblock_.transaction_generation == UINT64_MAX ||
        (this->v5_active_ ? this->v5_journal_.IsActive() : this->journal_.IsActive())) {
        return Status::IncompleteTransaction;
    }
    const uint64_t next_sequence =
        this->disk_superblock_.transaction_generation + OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    this->transaction_superblock_snapshot_ = this->disk_superblock_;
    this->transaction_statistics_snapshot_ = this->statistics_;
    this->transaction_data_allocation_hint_snapshot_ = this->next_data_allocation_hint_;
    this->transaction_inode_allocation_hint_snapshot_ = this->next_inode_allocation_hint_;
    if (this->v5_active_) {
        this->v5_transaction_superblock_snapshot_ = this->v5_superblock_;
        for (uint64_t group_index = 0ULL; group_index < this->v5_superblock_.group_count;
             ++group_index) {
            this->v5_transaction_group_descriptor_snapshots_[group_index] =
                this->v5_group_descriptors_[group_index];
        }
        const RootJournalV2Status begin_status = this->v5_journal_.Begin(
            OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT,
            OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_ORDERED_DATA_BLOCK_COUNT,
            OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT);
        if (begin_status != RootJournalV2Status::Succeeded) {
            return begin_status == RootJournalV2Status::CapacityExhausted
                       ? Status::CapacityExhausted
                       : Status::IncompleteTransaction;
        }
        this->transaction_snapshot_valid_ = true;
        ++this->disk_superblock_.transaction_generation;
        ++this->v5_superblock_.format_generation;
        return Status::Succeeded;
    }
    const RootJournalStatus begin_status =
        this->journal_.Begin(next_sequence, OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT);
    if (begin_status != RootJournalStatus::Succeeded) {
        return begin_status == RootJournalStatus::CreditsExhausted ? Status::CapacityExhausted
                                                                   : Status::IncompleteTransaction;
    }
    this->transaction_snapshot_valid_ = true;
    ++this->disk_superblock_.transaction_generation;
    return Status::Succeeded;
}

Status RootFileSystem::CommitTransaction() noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (this->failed_) {
        return Status::DeviceFailure;
    }
    if (this->disk_superblock_.transaction_state != RootTransactionState::Clean ||
        !(this->v5_active_ ? this->v5_journal_.IsActive() : this->journal_.IsActive())) {
        return Status::IncompleteTransaction;
    }
    this->disk_superblock_.allocated_inode_count = this->statistics_.allocated_inode_count;
    this->disk_superblock_.allocated_data_block_count =
        this->statistics_.allocated_data_block_count;
    this->disk_superblock_.allocated_metadata_block_count =
        this->statistics_.allocated_metadata_block_count;
    Status status = this->StageSuperblock();
    if (status != Status::Succeeded) {
        this->AbortTransaction();
        return status;
    }
    if (this->v5_active_) {
        const RootJournalV2Status commit_status =
            this->v5_journal_.CommitAndCheckpoint(this->ReadCurrentTimestamp());
        if (commit_status != RootJournalV2Status::Succeeded) {
            return this->FailDeviceOperation();
        }
        this->statistics_.transaction_generation =
            this->v5_journal_.Superblock().journal_generation;
        this->disk_superblock_.transaction_generation =
            this->statistics_.transaction_generation;
        this->transaction_snapshot_valid_ = false;
        return Status::Succeeded;
    }
    // ordered mode 要求普通文件数据先越过设备缓存边界，之后才允许 commit 持久化。
    if (this->cache_.Sync() != BlockCacheStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    if (this->journal_.Commit() != RootJournalStatus::Succeeded) {
        return this->FailDeviceOperation();
    }
    this->cache_.Invalidate();
    this->statistics_.transaction_generation = this->disk_superblock_.transaction_generation;
    this->transaction_snapshot_valid_ = false;
    return Status::Succeeded;
}

void RootFileSystem::AbortTransaction() noexcept {
    if (this->v5_active_ && this->v5_journal_.IsActive()) {
        static_cast<void>(this->v5_journal_.Abort());
    } else if (this->journal_.IsActive()) {
        static_cast<void>(this->journal_.Abort());
    }
    if (this->transaction_snapshot_valid_) {
        this->disk_superblock_ = this->transaction_superblock_snapshot_;
        this->statistics_ = this->transaction_statistics_snapshot_;
        this->next_data_allocation_hint_ = this->transaction_data_allocation_hint_snapshot_;
        this->next_inode_allocation_hint_ = this->transaction_inode_allocation_hint_snapshot_;
        if (this->v5_active_) {
            this->v5_superblock_ = this->v5_transaction_superblock_snapshot_;
            for (uint64_t group_index = 0ULL; group_index < this->v5_superblock_.group_count;
                 ++group_index) {
                this->v5_group_descriptors_[group_index] =
                    this->v5_transaction_group_descriptor_snapshots_[group_index];
            }
        }
        this->transaction_snapshot_valid_ = false;
    }
}

Status RootFileSystem::FailDeviceOperation() noexcept {
    this->failed_ = true;
    this->cache_.Invalidate();
    return Status::DeviceFailure;
}

Status RootFileSystem::ReadInode(const uint64_t inode_number, RootInode &inode) noexcept {
    inode = RootInode{};
    if (!this->initialized_ || inode_number == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        inode_number > this->disk_superblock_.inode_count) {
        return !this->initialized_ ? Status::NotInitialized : Status::InvalidArgument;
    }
    if (this->v5_active_) {
        const uint64_t group_index =
            (inode_number - 1ULL) / this->v5_superblock_.inodes_per_group;
        const uint64_t group_offset =
            (inode_number - 1ULL) % this->v5_superblock_.inodes_per_group;
        const uint64_t byte_offset = group_offset * this->v5_superblock_.inode_size_bytes;
        const uint64_t relative_block =
            this->v5_group_descriptors_[group_index].inode_table_start_block +
            byte_offset / OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
        const uint64_t block_offset = byte_offset % OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
        const uint8_t *inode_block = this->read_inode_block_scratch_;
        Status status = Status::Succeeded;
        if (this->v5_inode_cache_valid_ &&
            this->v5_inode_cache_relative_block_ == relative_block &&
            !this->v5_journal_.IsActive()) {
            inode_block = this->v5_inode_cache_block_;
        } else {
            status = this->ReadRelativeBlock(relative_block, this->read_inode_block_scratch_);
            if (status == Status::Succeeded && !this->v5_journal_.IsActive()) {
                CopyBytes(this->v5_inode_cache_block_, this->read_inode_block_scratch_,
                          OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES);
                this->v5_inode_cache_relative_block_ = relative_block;
                this->v5_inode_cache_valid_ = true;
                inode_block = this->v5_inode_cache_block_;
            }
        }
        RootV5Inode disk_inode{};
        if (status != Status::Succeeded) {
            return status;
        }
        if (DecodeRootV5Inode(this->v5_superblock_, inode_block + block_offset,
                              this->v5_superblock_.inode_size_bytes,
                              disk_inode) != RootV5FormatStatus::Succeeded ||
            disk_inode.inode_number != inode_number) {
            return Status::Corrupt;
        }
        RootNodeType type = RootNodeType::Unused;
        if (disk_inode.type == RootV5NodeType::RegularFile) {
            type = RootNodeType::RegularFile;
        } else if (disk_inode.type == RootV5NodeType::Directory) {
            type = RootNodeType::Directory;
        } else if (disk_inode.type == RootV5NodeType::SymbolicLink) {
            type = RootNodeType::SymbolicLink;
        }
        inode = RootInode{
            .type = type,
            .flags = disk_inode.flags,
            .size_bytes = disk_inode.size_bytes,
            .generation = disk_inode.generation,
            .link_count = disk_inode.link_count,
            .allocated_data_block_count = disk_inode.allocated_block_count == 0ULL
                                              ? 0ULL
                                              : disk_inode.allocated_block_count - 1ULL,
            .allocated_metadata_block_count =
                disk_inode.allocated_block_count == 0ULL ? 0ULL : 1ULL,
            .parent_inode_number = disk_inode.parent_inode_number,
            .direct_blocks = {},
            .single_indirect_block = 0ULL,
            .double_indirect_block = 0ULL,
            .triple_indirect_block = 0ULL,
            .quadruple_indirect_block = 0ULL,
            .quintuple_indirect_block = 0ULL,
            .access_time_nanoseconds = disk_inode.access_time_nanoseconds,
            .modification_time_nanoseconds = disk_inode.modification_time_nanoseconds,
            .change_time_nanoseconds = disk_inode.change_time_nanoseconds,
            .birth_time_nanoseconds = disk_inode.birth_time_nanoseconds,
            .owner_user_identifier = disk_inode.owner_user_identifier,
            .owner_group_identifier = disk_inode.owner_group_identifier,
            .mode = disk_inode.mode,
        };
        if (disk_inode.allocated_block_count != 0ULL) {
            RootInodeExtension extension{};
            if (DecodeRootInodeExtension(disk_inode.mapping_root, sizeof(disk_inode.mapping_root),
                                         extension) != RootInodeMetadataStatus::Succeeded ||
                extension.flags != OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS) {
                return Status::Corrupt;
            }
            inode.direct_blocks[0] = extension.extent_root_relative_block;
        }
        if (inode.type == RootNodeType::Directory && inode.direct_blocks[0] != 0ULL) {
            RootExtentNode &extent_node = this->v5_extent_scratch_;
            ClearBytes(reinterpret_cast<uint8_t *>(&extent_node), sizeof(extent_node));
            status = this->ReadV5ExtentNode(inode, extent_node);
            if (status != Status::Succeeded || extent_node.entry_count != 1ULL ||
                extent_node.entries[0].logical_start_block != 0ULL ||
                extent_node.entries[0].block_count_or_generation != 1ULL) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            status = this->ReadRelativeBlock(extent_node.entries[0].physical_or_child_block,
                                             this->read_block_scratch_);
            RootDirectoryBlock &directory = this->v5_directory_scratch_;
            ClearBytes(reinterpret_cast<uint8_t *>(&directory), sizeof(directory));
            if (status != Status::Succeeded ||
                DecodeRootDirectoryBlock(this->read_block_scratch_,
                                         OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
                                         directory) != RootDirectoryStatus::Succeeded ||
                directory.directory_inode_number != inode_number ||
                directory.directory_inode_generation != inode.generation) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            inode.size_bytes =
                directory.entry_count * OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES;
        }
        return type == RootNodeType::Unused ? Status::Corrupt : Status::Succeeded;
    }
    const uint64_t inode_offset_bytes =
        (inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT) * OS_KERNEL_ROOTFS_INODE_SIZE_BYTES;
    const uint64_t relative_block = this->disk_superblock_.inode_table_start_relative_block +
                                    inode_offset_bytes / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    const uint64_t block_offset_bytes = inode_offset_bytes % OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    const Status read_status =
        this->ReadRelativeBlock(relative_block, this->read_inode_block_scratch_);
    if (read_status != Status::Succeeded) {
        return read_status;
    }
    return DecodeRootInode(this->read_inode_block_scratch_ + block_offset_bytes,
                           OS_KERNEL_ROOTFS_INODE_SIZE_BYTES, inode) == RootFormatStatus::Succeeded
               ? Status::Succeeded
               : Status::Corrupt;
}

Status RootFileSystem::WriteInode(const uint64_t inode_number, const RootInode &inode) noexcept {
    if (!this->initialized_ || inode_number == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        inode_number > this->disk_superblock_.inode_count) {
        return !this->initialized_ ? Status::NotInitialized : Status::InvalidArgument;
    }
    if (this->v5_active_) {
        this->v5_inode_cache_valid_ = false;
        this->v5_inode_cache_relative_block_ = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
        const uint64_t group_index =
            (inode_number - 1ULL) / this->v5_superblock_.inodes_per_group;
        const uint64_t group_offset =
            (inode_number - 1ULL) % this->v5_superblock_.inodes_per_group;
        const uint64_t byte_offset = group_offset * this->v5_superblock_.inode_size_bytes;
        const uint64_t relative_block =
            this->v5_group_descriptors_[group_index].inode_table_start_block +
            byte_offset / OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
        const uint64_t block_offset = byte_offset % OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
        Status status = this->ReadRelativeBlock(relative_block, this->write_inode_block_scratch_);
        if (status != Status::Succeeded) {
            return status;
        }
        if (inode.type == RootNodeType::Unused) {
            ClearBytes(this->write_inode_block_scratch_ + block_offset,
                       this->v5_superblock_.inode_size_bytes);
            return this->WriteMetadataBlock(relative_block, this->write_inode_block_scratch_);
        }
        RootV5NodeType type = RootV5NodeType::Unused;
        if (inode.type == RootNodeType::RegularFile) {
            type = RootV5NodeType::RegularFile;
        } else if (inode.type == RootNodeType::Directory) {
            type = RootV5NodeType::Directory;
        } else if (inode.type == RootNodeType::SymbolicLink) {
            type = RootV5NodeType::SymbolicLink;
        }
        if (type == RootV5NodeType::Unused) {
            return Status::Corrupt;
        }
        RootV5Inode disk_inode{
            .inode_number = inode_number,
            .generation = inode.generation,
            .type = type,
            .flags = inode.flags,
            .size_bytes = inode.type == RootNodeType::Directory && inode.direct_blocks[0] != 0ULL
                              ? OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES
                              : inode.size_bytes,
            .allocated_block_count =
                inode.allocated_data_block_count + inode.allocated_metadata_block_count,
            .link_count = inode.link_count,
            .parent_inode_number = inode.parent_inode_number,
            .access_time_nanoseconds = inode.access_time_nanoseconds,
            .modification_time_nanoseconds = inode.modification_time_nanoseconds,
            .change_time_nanoseconds = inode.change_time_nanoseconds,
            .birth_time_nanoseconds = inode.birth_time_nanoseconds,
            .owner_user_identifier = inode.owner_user_identifier,
            .owner_group_identifier = inode.owner_group_identifier,
            .mode = inode.mode,
            .project_identifier = 0U,
            .mapping_root = {},
        };
        if (inode.direct_blocks[0] != 0ULL) {
            const RootInodeExtension extension{
                .flags = OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS,
                .extent_root_relative_block = inode.direct_blocks[0],
                .xattr_relative_block = 0ULL,
                .directory_index_root_relative_block = 0ULL,
                .project_identifier = 0ULL,
                .acl_generation = 0ULL,
                .quota_generation = 0ULL,
            };
            if (EncodeRootInodeExtension(extension, disk_inode.mapping_root,
                                         sizeof(disk_inode.mapping_root)) !=
                RootInodeMetadataStatus::Succeeded) {
                return Status::Corrupt;
            }
        }
        if (EncodeRootV5Inode(this->v5_superblock_, disk_inode,
                              this->write_inode_block_scratch_ + block_offset,
                              this->v5_superblock_.inode_size_bytes) !=
            RootV5FormatStatus::Succeeded) {
            return Status::Corrupt;
        }
        return this->WriteMetadataBlock(relative_block, this->write_inode_block_scratch_);
    }
    const uint64_t inode_offset_bytes =
        (inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT) * OS_KERNEL_ROOTFS_INODE_SIZE_BYTES;
    const uint64_t relative_block = this->disk_superblock_.inode_table_start_relative_block +
                                    inode_offset_bytes / OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    const uint64_t block_offset_bytes = inode_offset_bytes % OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
    Status status = this->ReadRelativeBlock(relative_block, this->write_inode_block_scratch_);
    if (status != Status::Succeeded) {
        return status;
    }
    if (EncodeRootInode(inode, this->write_inode_block_scratch_ + block_offset_bytes,
                        OS_KERNEL_ROOTFS_INODE_SIZE_BYTES) != RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    status = this->WriteMetadataBlock(relative_block, this->write_inode_block_scratch_);
    return status;
}

Status RootFileSystem::ReadV5ExtentNode(const RootInode &inode,
                                        RootExtentNode &node) noexcept {
    ClearBytes(reinterpret_cast<uint8_t *>(&node), sizeof(node));
    if (!this->v5_active_ || inode.direct_blocks[0] == 0ULL) {
        return Status::InvalidArgument;
    }
    const Status status =
        this->ReadRelativeBlock(inode.direct_blocks[0], this->read_pointer_block_scratch_);
    if (status != Status::Succeeded) {
        return status;
    }
    return DecodeRootExtentLeafNode(this->v5_journal_.Superblock(),
                                    this->read_pointer_block_scratch_,
                                    OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
                                    node) == RootExtentStatus::Succeeded
               ? Status::Succeeded
               : Status::Corrupt;
}

Status RootFileSystem::WriteV5ExtentNode(const RootInode &inode,
                                         const RootExtentNode &node) noexcept {
    if (!this->v5_active_ || inode.direct_blocks[0] == 0ULL) {
        return Status::InvalidArgument;
    }
    if (EncodeRootExtentLeafNode(this->v5_journal_.Superblock(), node,
                                 this->write_pointer_block_scratch_,
                                 OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) !=
        RootExtentStatus::Succeeded) {
        return Status::Corrupt;
    }
    return this->WriteMetadataBlock(inode.direct_blocks[0],
                                    this->write_pointer_block_scratch_);
}

Status RootFileSystem::ReadPointerBlock(const uint64_t relative_block,
                                        RootPointerBlock &pointer_block) noexcept {
    pointer_block = RootPointerBlock{};
    if (relative_block < this->disk_superblock_.data_start_relative_block ||
        relative_block >= this->disk_superblock_.total_block_count) {
        return Status::Corrupt;
    }
    const Status status =
        this->ReadRelativeBlock(relative_block, this->read_pointer_block_scratch_);
    if (status != Status::Succeeded) {
        return status;
    }
    return DecodeRootPointerBlock(this->read_pointer_block_scratch_,
                                  OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
                                  pointer_block) == RootFormatStatus::Succeeded
               ? Status::Succeeded
               : Status::Corrupt;
}

Status RootFileSystem::WritePointerBlock(const uint64_t relative_block,
                                         const RootPointerBlock &pointer_block) noexcept {
    if (relative_block < this->disk_superblock_.data_start_relative_block ||
        relative_block >= this->disk_superblock_.total_block_count) {
        return Status::Corrupt;
    }
    if (EncodeRootPointerBlock(pointer_block, this->write_pointer_block_scratch_,
                               OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) !=
        RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    return this->WriteMetadataBlock(relative_block, this->write_pointer_block_scratch_);
}

Status RootFileSystem::ReadBitmapBit(const bool inode_bitmap, const uint64_t bit_index,
                                     bool &allocated) noexcept {
    allocated = false;
    const uint64_t bit_count =
        inode_bitmap ? this->disk_superblock_.inode_count : this->disk_superblock_.data_block_count;
    if (bit_index >= bit_count) {
        return Status::InvalidArgument;
    }
    if (this->v5_active_) {
        const uint64_t group_index =
            inode_bitmap ? bit_index / this->v5_superblock_.inodes_per_group
                         : bit_index / this->v5_superblock_.blocks_per_group;
        if (group_index >= this->v5_superblock_.group_count) {
            return Status::InvalidArgument;
        }
        const RootV5GroupDescriptor &descriptor = this->v5_group_descriptors_[group_index];
        const uint64_t local_bit =
            inode_bitmap ? bit_index % this->v5_superblock_.inodes_per_group
                         : bit_index - descriptor.first_block;
        if (local_bit >= (inode_bitmap ? descriptor.inode_count : descriptor.block_count)) {
            return Status::InvalidArgument;
        }
        const uint64_t bitmap_block =
            inode_bitmap ? descriptor.inode_bitmap_block : descriptor.block_bitmap_block;
        const Status status =
            this->ReadRelativeBlock(bitmap_block, this->read_bitmap_block_scratch_);
        if (status != Status::Succeeded) {
            return status;
        }
        allocated = BitmapBitIsSet(this->read_bitmap_block_scratch_, local_bit);
        return Status::Succeeded;
    }
    const uint64_t bits_per_block =
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES * OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint64_t start_block = inode_bitmap
                                     ? this->disk_superblock_.inode_bitmap_start_relative_block
                                     : this->disk_superblock_.data_bitmap_start_relative_block;
    const uint64_t relative_block = start_block + bit_index / bits_per_block;
    const uint64_t block_bit_index = bit_index % bits_per_block;
    const Status status = this->ReadRelativeBlock(relative_block, this->read_bitmap_block_scratch_);
    if (status != Status::Succeeded) {
        return status;
    }
    allocated = BitmapBitIsSet(this->read_bitmap_block_scratch_, block_bit_index);
    return Status::Succeeded;
}

Status RootFileSystem::WriteBitmapBit(const bool inode_bitmap, const uint64_t bit_index,
                                      const bool allocated) noexcept {
    const uint64_t bit_count =
        inode_bitmap ? this->disk_superblock_.inode_count : this->disk_superblock_.data_block_count;
    if (bit_index >= bit_count) {
        return Status::InvalidArgument;
    }
    if (this->v5_active_) {
        const uint64_t group_index =
            inode_bitmap ? bit_index / this->v5_superblock_.inodes_per_group
                         : bit_index / this->v5_superblock_.blocks_per_group;
        if (group_index >= this->v5_superblock_.group_count) {
            return Status::InvalidArgument;
        }
        RootV5GroupDescriptor &descriptor = this->v5_group_descriptors_[group_index];
        const uint64_t local_bit =
            inode_bitmap ? bit_index % this->v5_superblock_.inodes_per_group
                         : bit_index - descriptor.first_block;
        if (local_bit >= (inode_bitmap ? descriptor.inode_count : descriptor.block_count)) {
            return Status::InvalidArgument;
        }
        const uint64_t bitmap_block =
            inode_bitmap ? descriptor.inode_bitmap_block : descriptor.block_bitmap_block;
        Status status =
            this->ReadRelativeBlock(bitmap_block, this->write_bitmap_block_scratch_);
        if (status != Status::Succeeded) {
            return status;
        }
        const bool was_allocated =
            BitmapBitIsSet(this->write_bitmap_block_scratch_, local_bit);
        if (was_allocated == allocated) {
            return Status::Succeeded;
        }
        SetBitmapBit(this->write_bitmap_block_scratch_, local_bit, allocated);
        if (inode_bitmap) {
            if (allocated) {
                if (descriptor.free_inode_count == 0ULL ||
                    this->v5_superblock_.free_inode_count == 0ULL) {
                    return Status::Corrupt;
                }
                --descriptor.free_inode_count;
                --this->v5_superblock_.free_inode_count;
            } else {
                ++descriptor.free_inode_count;
                ++this->v5_superblock_.free_inode_count;
            }
            descriptor.inode_bitmap_checksum = CalculateRootV5Crc32c(
                this->write_bitmap_block_scratch_, OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES);
        } else {
            if (allocated) {
                if (descriptor.free_block_count == 0ULL ||
                    this->v5_superblock_.free_block_count == 0ULL) {
                    return Status::Corrupt;
                }
                --descriptor.free_block_count;
                --this->v5_superblock_.free_block_count;
            } else {
                ++descriptor.free_block_count;
                ++this->v5_superblock_.free_block_count;
            }
            descriptor.block_bitmap_checksum = CalculateRootV5Crc32c(
                this->write_bitmap_block_scratch_, OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES);
        }
        ++descriptor.metadata_generation;
        status = this->WriteMetadataBlock(bitmap_block, this->write_bitmap_block_scratch_);
        return status == Status::Succeeded ? this->StageV5GroupDescriptor(group_index) : status;
    }
    const uint64_t bits_per_block =
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES * OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint64_t start_block = inode_bitmap
                                     ? this->disk_superblock_.inode_bitmap_start_relative_block
                                     : this->disk_superblock_.data_bitmap_start_relative_block;
    const uint64_t relative_block = start_block + bit_index / bits_per_block;
    const uint64_t block_bit_index = bit_index % bits_per_block;
    Status status = this->ReadRelativeBlock(relative_block, this->write_bitmap_block_scratch_);
    if (status != Status::Succeeded) {
        return status;
    }
    SetBitmapBit(this->write_bitmap_block_scratch_, block_bit_index, allocated);
    status = this->WriteMetadataBlock(relative_block, this->write_bitmap_block_scratch_);
    return status;
}

Status RootFileSystem::FindFreeBitmapBit(const bool inode_bitmap, const uint64_t first_bit,
                                         const uint64_t bit_count, uint64_t &bit_index) noexcept {
    bit_index = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const uint64_t available_bit_count =
        inode_bitmap ? this->disk_superblock_.inode_count : this->disk_superblock_.data_block_count;
    if (first_bit > available_bit_count || bit_count > available_bit_count - first_bit) {
        return Status::InvalidArgument;
    }
    if (this->v5_active_) {
        const uint64_t end_bit = first_bit + bit_count;
        for (uint64_t current_bit = first_bit; current_bit < end_bit; ++current_bit) {
            bool allocated = false;
            const Status status = this->ReadBitmapBit(inode_bitmap, current_bit, allocated);
            if (status != Status::Succeeded) {
                return status;
            }
            if (!allocated) {
                bit_index = current_bit;
                return Status::Succeeded;
            }
        }
        return Status::CapacityExhausted;
    }
    const uint64_t bits_per_block =
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES * OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    const uint64_t start_block = inode_bitmap
                                     ? this->disk_superblock_.inode_bitmap_start_relative_block
                                     : this->disk_superblock_.data_bitmap_start_relative_block;
    uint64_t current_bit = first_bit;
    const uint64_t end_bit = first_bit + bit_count;
    while (current_bit < end_bit) {
        const uint64_t bitmap_block_index = current_bit / bits_per_block;
        const Status status = this->ReadRelativeBlock(start_block + bitmap_block_index,
                                                      this->find_bitmap_block_scratch_);
        if (status != Status::Succeeded) {
            return status;
        }
        const uint64_t block_end = Minimum(
            end_bit, (bitmap_block_index + OS_KERNEL_ROOTFS_COUNTER_INCREMENT) * bits_per_block);
        while (current_bit < block_end) {
            if (!BitmapBitIsSet(this->find_bitmap_block_scratch_, current_bit % bits_per_block)) {
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
    if (this->v5_active_) {
        uint64_t start_group =
            this->next_data_allocation_hint_ / this->v5_superblock_.blocks_per_group;
        if (start_group >= this->v5_superblock_.group_count) {
            start_group = 0ULL;
        }
        for (uint64_t group_offset = 0ULL; group_offset < this->v5_superblock_.group_count;
             ++group_offset) {
            const uint64_t group_index =
                (start_group + group_offset) % this->v5_superblock_.group_count;
            const RootV5GroupDescriptor &descriptor =
                this->v5_group_descriptors_[group_index];
            if (descriptor.free_block_count == 0ULL) {
                continue;
            }
            Status status = this->ReadRelativeBlock(descriptor.block_bitmap_block,
                                                    this->find_bitmap_block_scratch_);
            if (status != Status::Succeeded) {
                return status;
            }
            for (uint64_t candidate = descriptor.data_start_block;
                 candidate < descriptor.first_block + descriptor.block_count; ++candidate) {
                const uint64_t local_bit = candidate - descriptor.first_block;
                if (BitmapBitIsSet(this->find_bitmap_block_scratch_, local_bit)) {
                    continue;
                }
                status = this->WriteBitmapBit(false, candidate, true);
                if (status != Status::Succeeded) {
                    return status;
                }
                relative_block = candidate;
                ClearBytes(this->allocate_data_block_scratch_,
                           OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES);
                status = this->WriteV5Block(relative_block,
                                            this->allocate_data_block_scratch_);
                if (status != Status::Succeeded) {
                    return status;
                }
                --this->statistics_.free_data_block_count;
                this->next_data_allocation_hint_ = candidate + 1ULL;
                return Status::Succeeded;
            }
        }
        return Status::CapacityExhausted;
    }
    uint64_t search_start = this->next_data_allocation_hint_;
    if (search_start >= this->disk_superblock_.data_block_count) {
        search_start = OS_KERNEL_ROOTFS_FIRST_INDEX;
    }
    uint64_t bit_index = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    Status status = this->FindFreeBitmapBit(
        false, search_start, this->disk_superblock_.data_block_count - search_start, bit_index);
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
    relative_block = this->disk_superblock_.data_start_relative_block + bit_index;
    ClearBytes(this->allocate_data_block_scratch_, OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES);
    status = this->WriteRelativeBlock(relative_block, this->allocate_data_block_scratch_);
    if (status != Status::Succeeded) {
        return status;
    }
    if (this->statistics_.free_data_block_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    --this->statistics_.free_data_block_count;
    this->next_data_allocation_hint_ = bit_index + OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    if (this->next_data_allocation_hint_ >= this->disk_superblock_.data_block_count) {
        this->next_data_allocation_hint_ = OS_KERNEL_ROOTFS_FIRST_INDEX;
    }
    return Status::Succeeded;
}

Status RootFileSystem::ReleaseDataBlock(const uint64_t relative_block) noexcept {
    if (this->v5_active_) {
        if (relative_block >= this->v5_superblock_.total_block_count) {
            return Status::Corrupt;
        }
        const uint64_t group_index =
            relative_block / this->v5_superblock_.blocks_per_group;
        if (group_index >= this->v5_superblock_.group_count ||
            relative_block < this->v5_group_descriptors_[group_index].data_start_block) {
            return Status::Corrupt;
        }
        bool allocated = false;
        Status status = this->ReadBitmapBit(false, relative_block, allocated);
        if (status != Status::Succeeded || !allocated) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        status = this->WriteBitmapBit(false, relative_block, false);
        if (status == Status::Succeeded) {
            ++this->statistics_.free_data_block_count;
            if (relative_block < this->next_data_allocation_hint_) {
                this->next_data_allocation_hint_ = relative_block;
            }
        }
        return status;
    }
    if (relative_block < this->disk_superblock_.data_start_relative_block ||
        relative_block >= this->disk_superblock_.total_block_count) {
        return Status::Corrupt;
    }
    const uint64_t bit_index = relative_block - this->disk_superblock_.data_start_relative_block;
    bool allocated = false;
    Status status = this->ReadBitmapBit(false, bit_index, allocated);
    if (status != Status::Succeeded || !allocated) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    status = this->WriteBitmapBit(false, bit_index, false);
    if (status != Status::Succeeded) {
        return status;
    }
    if (this->statistics_.free_data_block_count == this->disk_superblock_.data_block_count) {
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
    const uint64_t first_allocatable_bit =
        this->v5_active_ ? OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER - 1ULL
                         : OS_KERNEL_ROOTFS_FIRST_ALLOCATABLE_INODE_BITMAP_BIT;
    if (search_start < first_allocatable_bit ||
        search_start >= this->disk_superblock_.inode_count) {
        search_start = first_allocatable_bit;
    }
    uint64_t bit_index = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    Status find_status = this->FindFreeBitmapBit(
        true, search_start, this->disk_superblock_.inode_count - search_start, bit_index);
    if (find_status == Status::CapacityExhausted &&
        search_start > first_allocatable_bit) {
        find_status = this->FindFreeBitmapBit(
            true, first_allocatable_bit, search_start - first_allocatable_bit, bit_index);
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
    if (this->next_inode_allocation_hint_ >= this->disk_superblock_.inode_count) {
        this->next_inode_allocation_hint_ = first_allocatable_bit;
    }
    return Status::Succeeded;
}

Status RootFileSystem::ReleaseInodeNumber(const uint64_t inode_number) noexcept {
    if (inode_number <= this->ActiveRootInodeNumber() ||
        inode_number > this->disk_superblock_.inode_count ||
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
        this->disk_superblock_.maximum_file_size_bytes / this->ActiveBlockSizeBytes();
    if (logical_block >= maximum_logical_block_count) {
        return Status::FileTooLarge;
    }
    if (this->v5_active_) {
        if (inode.direct_blocks[0] == 0ULL) {
            required_block_count = 2ULL;
            return Status::Succeeded;
        }
        RootExtentNode &node = this->v5_extent_scratch_;
        ClearBytes(reinterpret_cast<uint8_t *>(&node), sizeof(node));
        const Status status = this->ReadV5ExtentNode(inode, node);
        if (status != Status::Succeeded) {
            return status;
        }
        for (uint64_t entry_index = 0ULL; entry_index < node.entry_count; ++entry_index) {
            const RootExtentNodeEntry &entry = node.entries[entry_index];
            if (logical_block >= entry.logical_start_block &&
                logical_block - entry.logical_start_block < entry.block_count_or_generation) {
                return Status::Succeeded;
            }
        }
        required_block_count = 1ULL;
        return Status::Succeeded;
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
    if (remaining_logical_block >= OS_KERNEL_ROOTFS_SINGLE_INDIRECT_CAPACITY) {
        remaining_logical_block -= OS_KERNEL_ROOTFS_SINGLE_INDIRECT_CAPACITY;
        level = OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL;
        root_block = inode.double_indirect_block;
        if (remaining_logical_block >= OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY) {
            remaining_logical_block -= OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY;
            level = OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL;
            root_block = inode.triple_indirect_block;
            if (remaining_logical_block >= OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY) {
                remaining_logical_block -= OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY;
                level = OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_LEVEL;
                root_block = inode.quadruple_indirect_block;
                if (remaining_logical_block >= OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY) {
                    remaining_logical_block -= OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY;
                    level = OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL;
                    root_block = inode.quintuple_indirect_block;
                }
            }
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
        this->disk_superblock_.maximum_file_size_bytes / this->ActiveBlockSizeBytes();
    if (logical_block >= maximum_logical_block_count) {
        return Status::FileTooLarge;
    }
    if (this->v5_active_) {
        RootExtentNode &node = this->v5_extent_scratch_;
        ClearBytes(reinterpret_cast<uint8_t *>(&node), sizeof(node));
        if (inode.direct_blocks[0] == 0ULL) {
            if (!allocate) {
                return Status::Succeeded;
            }
            if (this->active_mapping_inode_number_ == 0ULL) {
                return Status::Corrupt;
            }
            Status status = this->AllocateDataBlock(inode.direct_blocks[0]);
            if (status != Status::Succeeded) {
                return status;
            }
            ++inode.allocated_metadata_block_count;
            ++this->statistics_.allocated_metadata_block_count;
            node.tree_generation = 1ULL;
            node.inode_number = this->active_mapping_inode_number_;
            node.inode_generation = inode.generation;
            node.file_system_uuid = this->v5_superblock_.uuid;
        } else {
            const Status status = this->ReadV5ExtentNode(inode, node);
            if (status != Status::Succeeded) {
                return status;
            }
        }
        for (uint64_t entry_index = 0ULL; entry_index < node.entry_count; ++entry_index) {
            const RootExtentNodeEntry &entry = node.entries[entry_index];
            if (logical_block >= entry.logical_start_block &&
                logical_block - entry.logical_start_block < entry.block_count_or_generation) {
                relative_block = entry.physical_or_child_block +
                                 logical_block - entry.logical_start_block;
                return Status::Succeeded;
            }
        }
        if (!allocate) {
            return Status::Succeeded;
        }
        uint64_t data_block = 0ULL;
        Status status = this->AllocateDataBlock(data_block);
        if (status != Status::Succeeded) {
            return status;
        }
        uint64_t insert_index = 0ULL;
        while (insert_index < node.entry_count &&
               node.entries[insert_index].logical_start_block < logical_block) {
            ++insert_index;
        }
        if (insert_index > 0ULL) {
            RootExtentNodeEntry &previous = node.entries[insert_index - 1ULL];
            if (previous.logical_start_block + previous.block_count_or_generation ==
                    logical_block &&
                previous.physical_or_child_block + previous.block_count_or_generation ==
                    data_block &&
                previous.state_or_covered_block_count ==
                    static_cast<uint64_t>(RootExtentState::Initialized)) {
                ++previous.block_count_or_generation;
                status = this->WriteV5ExtentNode(inode, node);
                if (status == Status::Succeeded) {
                    relative_block = data_block;
                    ++inode.allocated_data_block_count;
                    ++this->statistics_.allocated_data_block_count;
                }
                return status;
            }
        }
        if (node.entry_count >= OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_CAPACITY) {
            return Status::FileTooLarge;
        }
        for (uint64_t move_index = node.entry_count; move_index > insert_index; --move_index) {
            node.entries[move_index] = node.entries[move_index - 1ULL];
        }
        node.entries[insert_index] = RootExtentNodeEntry{
            .logical_start_block = logical_block,
            .physical_or_child_block = data_block,
            .block_count_or_generation = 1ULL,
            .state_or_covered_block_count =
                static_cast<uint64_t>(RootExtentState::Initialized),
        };
        ++node.entry_count;
        status = this->WriteV5ExtentNode(inode, node);
        if (status == Status::Succeeded) {
            relative_block = data_block;
            ++inode.allocated_data_block_count;
            ++this->statistics_.allocated_data_block_count;
        }
        return status;
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
    if (remaining_logical_block < OS_KERNEL_ROOTFS_SINGLE_INDIRECT_CAPACITY) {
        return this->ResolveIndirectDataBlock(
            inode.single_indirect_block, OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
            remaining_logical_block, allocate, inode, relative_block);
    }
    remaining_logical_block -= OS_KERNEL_ROOTFS_SINGLE_INDIRECT_CAPACITY;
    if (remaining_logical_block < OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY) {
        return this->ResolveIndirectDataBlock(
            inode.double_indirect_block, OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL,
            remaining_logical_block, allocate, inode, relative_block);
    }
    remaining_logical_block -= OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY;
    if (remaining_logical_block < OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY) {
        return this->ResolveIndirectDataBlock(
            inode.triple_indirect_block, OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL,
            remaining_logical_block, allocate, inode, relative_block);
    }
    remaining_logical_block -= OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY;
    if (remaining_logical_block < OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY) {
        return this->ResolveIndirectDataBlock(
            inode.quadruple_indirect_block, OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_LEVEL,
            remaining_logical_block, allocate, inode, relative_block);
    }
    remaining_logical_block -= OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY;
    return this->ResolveIndirectDataBlock(inode.quintuple_indirect_block,
                                          OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL,
                                          remaining_logical_block, allocate, inode, relative_block);
}

Status RootFileSystem::ResolveIndirectDataBlock(uint64_t &root_block, const uint64_t level,
                                                const uint64_t logical_block, const bool allocate,
                                                RootInode &inode,
                                                uint64_t &relative_block) noexcept {
    relative_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (level == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        level > OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL) {
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
    if (this->v5_active_) {
        return this->ReleaseLogicalBlockRange(inode, logical_block, logical_block + 1ULL);
    }
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
    if (remaining_logical_block < OS_KERNEL_ROOTFS_SINGLE_INDIRECT_CAPACITY) {
        return this->ReleaseIndirectLogicalBlock(inode.single_indirect_block,
                                                 OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
                                                 remaining_logical_block, inode);
    }
    remaining_logical_block -= OS_KERNEL_ROOTFS_SINGLE_INDIRECT_CAPACITY;
    if (remaining_logical_block < OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY) {
        return this->ReleaseIndirectLogicalBlock(inode.double_indirect_block,
                                                 OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_LEVEL,
                                                 remaining_logical_block, inode);
    }
    remaining_logical_block -= OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY;
    if (remaining_logical_block < OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY) {
        return this->ReleaseIndirectLogicalBlock(inode.triple_indirect_block,
                                                 OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL,
                                                 remaining_logical_block, inode);
    }
    remaining_logical_block -= OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY;
    if (remaining_logical_block < OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY) {
        return this->ReleaseIndirectLogicalBlock(inode.quadruple_indirect_block,
                                                 OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_LEVEL,
                                                 remaining_logical_block, inode);
    }
    remaining_logical_block -= OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY;
    return this->ReleaseIndirectLogicalBlock(inode.quintuple_indirect_block,
                                             OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL,
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
        this->disk_superblock_.maximum_file_size_bytes / this->ActiveBlockSizeBytes();
    if (first_logical_block > past_last_logical_block ||
        past_last_logical_block > maximum_logical_block_count) {
        return Status::InvalidArgument;
    }
    if (first_logical_block == past_last_logical_block) {
        return Status::Succeeded;
    }
    if (this->v5_active_) {
        if (inode.direct_blocks[0] == 0ULL) {
            return Status::Succeeded;
        }
        RootExtentNode &node = this->v5_extent_scratch_;
        ClearBytes(reinterpret_cast<uint8_t *>(&node), sizeof(node));
        Status status = this->ReadV5ExtentNode(inode, node);
        if (status != Status::Succeeded) {
            return status;
        }
        RootExtentNode &updated = this->v5_extent_secondary_scratch_;
        CopyBytes(reinterpret_cast<uint8_t *>(&updated),
                  reinterpret_cast<const uint8_t *>(&node), sizeof(node));
        updated.entry_count = 0ULL;
        for (uint64_t entry_index = 0ULL; entry_index < node.entry_count; ++entry_index) {
            const RootExtentNodeEntry &entry = node.entries[entry_index];
            const uint64_t entry_end =
                entry.logical_start_block + entry.block_count_or_generation;
            const uint64_t release_start = entry.logical_start_block < first_logical_block
                                               ? first_logical_block
                                               : entry.logical_start_block;
            const uint64_t release_end = entry_end < past_last_logical_block
                                             ? entry_end
                                             : past_last_logical_block;
            if (release_start >= release_end) {
                updated.entries[updated.entry_count++] = entry;
                continue;
            }
            if (entry.logical_start_block < release_start) {
                updated.entries[updated.entry_count++] = RootExtentNodeEntry{
                    .logical_start_block = entry.logical_start_block,
                    .physical_or_child_block = entry.physical_or_child_block,
                    .block_count_or_generation = release_start - entry.logical_start_block,
                    .state_or_covered_block_count = entry.state_or_covered_block_count,
                };
            }
            for (uint64_t logical_block = release_start; logical_block < release_end;
                 ++logical_block) {
                status = this->ReleaseDataBlock(
                    entry.physical_or_child_block + logical_block - entry.logical_start_block);
                if (status != Status::Succeeded || inode.allocated_data_block_count == 0ULL ||
                    this->statistics_.allocated_data_block_count == 0ULL) {
                    return status == Status::Succeeded ? Status::Corrupt : status;
                }
                --inode.allocated_data_block_count;
                --this->statistics_.allocated_data_block_count;
            }
            if (release_end < entry_end) {
                updated.entries[updated.entry_count++] = RootExtentNodeEntry{
                    .logical_start_block = release_end,
                    .physical_or_child_block =
                        entry.physical_or_child_block + release_end - entry.logical_start_block,
                    .block_count_or_generation = entry_end - release_end,
                    .state_or_covered_block_count = entry.state_or_covered_block_count,
                };
            }
        }
        if (updated.entry_count != 0ULL) {
            return this->WriteV5ExtentNode(inode, updated);
        }
        const uint64_t extent_root = inode.direct_blocks[0];
        status = this->ReleaseDataBlock(extent_root);
        if (status != Status::Succeeded || inode.allocated_metadata_block_count == 0ULL ||
            this->statistics_.allocated_metadata_block_count == 0ULL) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        inode.direct_blocks[0] = 0ULL;
        --inode.allocated_metadata_block_count;
        --this->statistics_.allocated_metadata_block_count;
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

    const uint64_t double_start = single_past_last;
    const uint64_t double_past_last = double_start + OS_KERNEL_ROOTFS_DOUBLE_INDIRECT_CAPACITY;
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
    const uint64_t triple_past_last = triple_start + OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY;
    if (past_last_logical_block > triple_start && first_logical_block < triple_past_last) {
        const uint64_t local_first = first_logical_block > triple_start
                                         ? first_logical_block - triple_start
                                         : OS_KERNEL_ROOTFS_EMPTY_VALUE;
        const uint64_t local_past_last =
            Minimum(past_last_logical_block, triple_past_last) - triple_start;
        const Status status = this->ReleaseIndirectLogicalBlockRange(
            inode.triple_indirect_block, OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_LEVEL, local_first,
            local_past_last, inode);
        if (status != Status::Succeeded) {
            return status;
        }
    }

    const uint64_t quadruple_start = triple_past_last;
    const uint64_t quadruple_past_last =
        quadruple_start + OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY;
    if (past_last_logical_block > quadruple_start && first_logical_block < quadruple_past_last) {
        const uint64_t local_first = first_logical_block > quadruple_start
                                         ? first_logical_block - quadruple_start
                                         : OS_KERNEL_ROOTFS_EMPTY_VALUE;
        const uint64_t local_past_last =
            Minimum(past_last_logical_block, quadruple_past_last) - quadruple_start;
        const Status status = this->ReleaseIndirectLogicalBlockRange(
            inode.quadruple_indirect_block, OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_LEVEL, local_first,
            local_past_last, inode);
        if (status != Status::Succeeded) {
            return status;
        }
    }

    const uint64_t quintuple_start = quadruple_past_last;
    if (past_last_logical_block > quintuple_start) {
        const uint64_t local_first = first_logical_block > quintuple_start
                                         ? first_logical_block - quintuple_start
                                         : OS_KERNEL_ROOTFS_EMPTY_VALUE;
        const uint64_t local_past_last = past_last_logical_block - quintuple_start;
        return this->ReleaseIndirectLogicalBlockRange(inode.quintuple_indirect_block,
                                                      OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL,
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
    if (level == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        level > OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL ||
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
    const uint64_t block_size_bytes = this->ActiveBlockSizeBytes();
    uint64_t copied_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    while (copied_bytes < read_bytes) {
        const uint64_t absolute_offset = offset_bytes + copied_bytes;
        const uint64_t logical_block = absolute_offset / block_size_bytes;
        const uint64_t block_offset_bytes = absolute_offset % block_size_bytes;
        const uint64_t chunk_bytes = Minimum(
            read_bytes - copied_bytes, block_size_bytes - block_offset_bytes);
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
            status = this->ReadRelativeBlock(relative_block, this->read_block_scratch_);
            if (status != Status::Succeeded) {
                read_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
                return status;
            }
            CopyBytes(destination + copied_bytes, this->read_block_scratch_ + block_offset_bytes,
                      chunk_bytes);
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
                                                   const bool metadata_content,
                                                   uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if ((source == nullptr && length_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE) ||
        !(this->v5_active_ ? this->v5_journal_.IsActive() : this->journal_.IsActive())) {
        return Status::InvalidArgument;
    }
    if (length_bytes == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    if (offset_bytes > this->disk_superblock_.maximum_file_size_bytes ||
        length_bytes > this->disk_superblock_.maximum_file_size_bytes - offset_bytes) {
        return Status::FileTooLarge;
    }

    this->active_mapping_inode_number_ = inode_number;
    const uint64_t block_size_bytes = this->ActiveBlockSizeBytes();
    while (written_bytes < length_bytes) {
        const uint64_t absolute_offset = offset_bytes + written_bytes;
        const uint64_t logical_block = absolute_offset / block_size_bytes;
        const uint64_t block_offset_bytes = absolute_offset % block_size_bytes;
        const uint64_t chunk_bytes = Minimum(
            length_bytes - written_bytes, block_size_bytes - block_offset_bytes);
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
        status = this->ReadRelativeBlock(relative_block, this->write_block_scratch_);
        if (status != Status::Succeeded) {
            return status;
        }
        CopyBytes(this->write_block_scratch_ + block_offset_bytes, source + written_bytes,
                  chunk_bytes);
        if (metadata_content) {
            status = this->WriteMetadataBlock(relative_block, this->write_block_scratch_);
        } else if (this->v5_active_) {
            const RootJournalV2Status journal_status = this->v5_journal_.StageOrderedData(
                relative_block, this->write_block_scratch_,
                OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES);
            status = journal_status == RootJournalV2Status::Succeeded
                         ? Status::Succeeded
                     : journal_status == RootJournalV2Status::CapacityExhausted
                         ? Status::CapacityExhausted
                         : Status::IncompleteTransaction;
        } else {
            status = this->WriteRelativeBlock(relative_block, this->write_block_scratch_);
        }
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
    this->active_mapping_inode_number_ = 0ULL;
    return Status::Succeeded;
}

Status RootFileSystem::TruncateInTransaction(const uint64_t inode_number, RootInode &inode,
                                             const uint64_t size_bytes) noexcept {
    if (!(this->v5_active_ ? this->v5_journal_.IsActive() : this->journal_.IsActive())) {
        return Status::InvalidArgument;
    }
    if (inode.type == RootNodeType::Directory &&
        size_bytes % OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    if (size_bytes > this->disk_superblock_.maximum_file_size_bytes) {
        return Status::FileTooLarge;
    }
    if (size_bytes == inode.size_bytes) {
        return Status::Succeeded;
    }
    this->active_mapping_inode_number_ = inode_number;
    const uint64_t block_size_bytes = this->ActiveBlockSizeBytes();
    const uint64_t old_logical_block_count = DivideRoundUp(inode.size_bytes, block_size_bytes);
    const uint64_t new_logical_block_count = DivideRoundUp(size_bytes, block_size_bytes);
    if (size_bytes < inode.size_bytes &&
        size_bytes % block_size_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        const uint64_t tail_logical_block = size_bytes / block_size_bytes;
        uint64_t tail_relative_block = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        Status status =
            this->ResolveDataBlock(inode, tail_logical_block, false, tail_relative_block);
        if (status != Status::Succeeded) {
            return status;
        }
        if (tail_relative_block != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            status = this->ReadRelativeBlock(tail_relative_block, this->truncate_block_scratch_);
            if (status != Status::Succeeded) {
                return status;
            }
            const uint64_t block_offset_bytes = size_bytes % block_size_bytes;
            ClearBytes(this->truncate_block_scratch_ + block_offset_bytes,
                       block_size_bytes - block_offset_bytes);
            if (inode.type == RootNodeType::Directory) {
                status = this->WriteMetadataBlock(tail_relative_block,
                                                  this->truncate_block_scratch_);
            } else if (this->v5_active_) {
                status = this->v5_journal_.StageOrderedData(
                             tail_relative_block, this->truncate_block_scratch_,
                             OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) ==
                                 RootJournalV2Status::Succeeded
                             ? Status::Succeeded
                             : Status::IncompleteTransaction;
            } else {
                status =
                    this->WriteRelativeBlock(tail_relative_block, this->truncate_block_scratch_);
            }
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
    const Status status = this->WriteInode(inode_number, inode);
    this->active_mapping_inode_number_ = 0ULL;
    return status;
}

Status RootFileSystem::ReadDirectoryEntryAt(RootInode &directory, const uint64_t offset_bytes,
                                            RootDirectoryEntry &entry) noexcept {
    entry = RootDirectoryEntry{};
    if (directory.type != RootNodeType::Directory || offset_bytes > directory.size_bytes ||
        directory.size_bytes - offset_bytes < OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
        return Status::InvalidArgument;
    }
    if (this->v5_active_) {
        const Status status =
            this->ReadV5DirectoryBlock(directory, this->v5_directory_scratch_);
        const uint64_t entry_index = offset_bytes / OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES;
        if (status != Status::Succeeded ||
            entry_index >= this->v5_directory_scratch_.entry_count) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        const RootDirectoryEntryV2 &disk_entry =
            this->v5_directory_scratch_.entries[entry_index];
        entry = RootDirectoryEntry{
            .inode_number = disk_entry.inode_number,
            .inode_generation = disk_entry.inode_generation,
            .type = disk_entry.type == RootV5NodeType::RegularFile
                        ? RootNodeType::RegularFile
                    : disk_entry.type == RootV5NodeType::Directory
                        ? RootNodeType::Directory
                    : disk_entry.type == RootV5NodeType::SymbolicLink
                        ? RootNodeType::SymbolicLink
                        : RootNodeType::Unused,
            .name_length_bytes = disk_entry.name_length_bytes,
            .name = {},
        };
        CopyBytes(entry.name, disk_entry.name, disk_entry.name_length_bytes);
        return entry.type == RootNodeType::Unused ? Status::Corrupt : Status::Succeeded;
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
    if (this->v5_active_) {
        RootDirectoryBlock &block = this->v5_directory_scratch_;
        ClearBytes(reinterpret_cast<uint8_t *>(&block), sizeof(block));
        Status status = Status::Succeeded;
        if (directory.direct_blocks[0] == 0ULL) {
            block.directory_inode_number = directory_inode_number;
            block.directory_inode_generation = directory.generation;
            block.block_generation = 1ULL;
            block.file_system_uuid = this->v5_superblock_.uuid;
        } else {
            status = this->ReadV5DirectoryBlock(directory, block);
            if (status != Status::Succeeded) {
                return status;
            }
            if (block.block_generation == UINT64_MAX) {
                return Status::CapacityExhausted;
            }
            ++block.block_generation;
        }
        const uint64_t entry_index = offset_bytes / OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES;
        if (entry.type == RootNodeType::Unused) {
            if (entry_index >= block.entry_count) {
                return Status::Corrupt;
            }
            for (uint64_t move_index = entry_index + 1ULL; move_index < block.entry_count;
                 ++move_index) {
                block.entries[move_index - 1ULL] = block.entries[move_index];
            }
            --block.entry_count;
            block.entries[block.entry_count] = RootDirectoryEntryV2{};
        } else {
            if (entry_index > block.entry_count ||
                block.entry_count >= OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_ENTRY_COUNT) {
                return Status::CapacityExhausted;
            }
            const RootV5NodeType disk_type =
                entry.type == RootNodeType::RegularFile ? RootV5NodeType::RegularFile
                : entry.type == RootNodeType::Directory ? RootV5NodeType::Directory
                : entry.type == RootNodeType::SymbolicLink ? RootV5NodeType::SymbolicLink
                                                          : RootV5NodeType::Unused;
            if (disk_type == RootV5NodeType::Unused) {
                return Status::Corrupt;
            }
            RootDirectoryEntryV2 disk_entry{
                .inode_number = entry.inode_number,
                .inode_generation = entry.inode_generation,
                .name_hash = CalculateRootDirectoryNameHash(this->v5_superblock_.uuid, entry.name,
                                                            entry.name_length_bytes),
                .type = disk_type,
                .name_length_bytes = entry.name_length_bytes,
                .name = {},
            };
            CopyBytes(disk_entry.name, entry.name, entry.name_length_bytes);
            if (entry_index == block.entry_count) {
                ++block.entry_count;
            }
            block.entries[entry_index] = disk_entry;
            for (uint64_t sort_index = 1ULL; sort_index < block.entry_count; ++sort_index) {
                RootDirectoryEntryV2 candidate = block.entries[sort_index];
                uint64_t insert_index = sort_index;
                while (insert_index > 0ULL) {
                    const RootDirectoryEntryV2 &previous = block.entries[insert_index - 1ULL];
                    bool previous_after = previous.name_hash > candidate.name_hash;
                    if (previous.name_hash == candidate.name_hash) {
                        const uint64_t common = Minimum(previous.name_length_bytes,
                                                        candidate.name_length_bytes);
                        uint64_t byte_index = 0ULL;
                        while (byte_index < common &&
                               previous.name[byte_index] == candidate.name[byte_index]) {
                            ++byte_index;
                        }
                        previous_after =
                            byte_index < common
                                ? previous.name[byte_index] > candidate.name[byte_index]
                                : previous.name_length_bytes > candidate.name_length_bytes;
                    }
                    if (!previous_after) {
                        break;
                    }
                    block.entries[insert_index] = previous;
                    --insert_index;
                }
                block.entries[insert_index] = candidate;
            }
        }
        directory.size_bytes =
            block.entry_count * OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES;
        return this->WriteV5DirectoryBlock(directory_inode_number, directory, block);
    }
    uint8_t encoded[OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES]{};
    if (EncodeRootDirectoryEntry(entry, encoded, sizeof(encoded)) != RootFormatStatus::Succeeded) {
        return Status::Corrupt;
    }
    uint64_t written_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const Status status =
        this->WriteFileBytesInTransaction(directory_inode_number, directory, offset_bytes, encoded,
                                          sizeof(encoded), true, written_bytes);
    return status == Status::Succeeded && written_bytes == sizeof(encoded) ? Status::Succeeded
           : status == Status::Succeeded ? Status::CapacityExhausted
                                         : status;
}

Status RootFileSystem::ReadV5DirectoryBlock(RootInode &directory,
                                             RootDirectoryBlock &block) noexcept {
    ClearBytes(reinterpret_cast<uint8_t *>(&block), sizeof(block));
    if (!this->v5_active_ || directory.type != RootNodeType::Directory ||
        directory.direct_blocks[0] == 0ULL) {
        return Status::InvalidArgument;
    }
    uint64_t relative_block = 0ULL;
    const Status status = this->ResolveDataBlock(directory, 0ULL, false, relative_block);
    if (status != Status::Succeeded || relative_block == 0ULL) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    const Status read_status = this->ReadRelativeBlock(relative_block, this->read_block_scratch_);
    if (read_status != Status::Succeeded) {
        return read_status;
    }
    return DecodeRootDirectoryBlock(this->read_block_scratch_,
                                    OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
                                    block) == RootDirectoryStatus::Succeeded &&
                   block.directory_inode_generation == directory.generation
               ? Status::Succeeded
               : Status::Corrupt;
}

Status RootFileSystem::WriteV5DirectoryBlock(const uint64_t inode_number, RootInode &directory,
                                              const RootDirectoryBlock &block) noexcept {
    this->active_mapping_inode_number_ = inode_number;
    uint64_t relative_block = 0ULL;
    Status status = this->ResolveDataBlock(directory, 0ULL, true, relative_block);
    if (status != Status::Succeeded || relative_block == 0ULL) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    if (EncodeRootDirectoryBlock(block, this->write_block_scratch_,
                                 OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) !=
        RootDirectoryStatus::Succeeded) {
        return Status::CapacityExhausted;
    }
    status = this->WriteMetadataBlock(relative_block, this->write_block_scratch_);
    if (status == Status::Succeeded) {
        status = this->WriteInode(inode_number, directory);
    }
    this->active_mapping_inode_number_ = 0ULL;
    return status;
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
    if (directory.size_bytes > this->disk_superblock_.maximum_file_size_bytes -
                                   OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
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
    if (inode_number <= this->ActiveRootInodeNumber() ||
        inode_number > this->disk_superblock_.inode_count ||
        this->ReadOpenReferenceCount(inode_number) != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Busy;
    }
    const uint64_t old_logical_block_count =
        this->v5_active_ && inode.type == RootNodeType::Directory &&
                inode.direct_blocks[0] != 0ULL
            ? 1ULL
            : DivideRoundUp(inode.size_bytes, this->ActiveBlockSizeBytes());
    Status status = this->ReleaseLogicalBlockRange(inode, OS_KERNEL_ROOTFS_EMPTY_VALUE,
                                                   old_logical_block_count);
    if (status != Status::Succeeded) {
        return status;
    }
    if (inode.allocated_data_block_count != OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        inode.allocated_metadata_block_count != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    RootInode unused_inode{};
    unused_inode.type = RootNodeType::Unused;
    status = this->WriteInode(inode_number, unused_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    return this->ReleaseInodeNumber(inode_number);
}

Status RootFileSystem::DropLinkInTransaction(const uint64_t inode_number,
                                             RootInode &inode) noexcept {
    if (inode_number <= this->ActiveRootInodeNumber() ||
        inode_number > this->disk_superblock_.inode_count ||
        inode.link_count == OS_KERNEL_ROOTFS_EMPTY_VALUE || inode.type == RootNodeType::Directory) {
        return Status::Corrupt;
    }
    if (inode.link_count > OS_KERNEL_ROOTFS_COUNTER_INCREMENT) {
        --inode.link_count;
        inode.change_time_nanoseconds = this->ReadCurrentTimestamp();
        return this->WriteInode(inode_number, inode);
    }
    const uint64_t open_count = this->ReadOpenReferenceCount(inode_number);
    if (open_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return this->RemoveInodeInTransaction(inode_number, inode);
    }
    inode.link_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    inode.flags |= OS_KERNEL_ROOTFS_INODE_FLAG_ORPHAN;
    inode.change_time_nanoseconds = this->ReadCurrentTimestamp();
    const Status status = this->WriteInode(inode_number, inode);
    if (status == Status::Succeeded) {
        ++this->statistics_.orphan_create_count;
    }
    return status;
}

Status RootFileSystem::ReapOrphans() noexcept {
    const uint64_t bits_per_bitmap_block =
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES * OS_KERNEL_ROOTFS_BITS_PER_BYTE;
    for (uint64_t bitmap_block_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         bitmap_block_index < this->disk_superblock_.inode_bitmap_block_count;
         ++bitmap_block_index) {
        Status status = this->ReadRelativeBlock(
            this->disk_superblock_.inode_bitmap_start_relative_block + bitmap_block_index,
            this->orphan_bitmap_block_scratch_);
        if (status != Status::Succeeded) {
            return status;
        }
        for (uint64_t byte_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
             byte_index < OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES; ++byte_index) {
            const uint8_t allocated_bits = this->orphan_bitmap_block_scratch_[byte_index];
            if (allocated_bits == OS_KERNEL_ROOTFS_ZERO_BYTE) {
                continue;
            }
            for (uint64_t bit_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
                 bit_index < OS_KERNEL_ROOTFS_BITS_PER_BYTE; ++bit_index) {
                if ((allocated_bits & static_cast<uint8_t>(1ULL << bit_index)) ==
                    OS_KERNEL_ROOTFS_ZERO_BYTE) {
                    continue;
                }
                const uint64_t inode_index = bitmap_block_index * bits_per_bitmap_block +
                                             byte_index * OS_KERNEL_ROOTFS_BITS_PER_BYTE +
                                             bit_index;
                const uint64_t inode_number = inode_index + OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
                if (inode_number > this->disk_superblock_.inode_count) {
                    return Status::Corrupt;
                }
                if (inode_number == this->ActiveRootInodeNumber()) {
                    continue;
                }
                RootInode inode{};
                status = this->ReadInode(inode_number, inode);
                if (status != Status::Succeeded) {
                    return status;
                }
                if ((inode.flags & OS_KERNEL_ROOTFS_INODE_FLAG_ORPHAN) ==
                    OS_KERNEL_ROOTFS_EMPTY_VALUE) {
                    continue;
                }
                if (inode.link_count != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
                    return Status::Corrupt;
                }
                status = this->BeginTransaction();
                if (status != Status::Succeeded) {
                    return status;
                }
                status = this->RemoveInodeInTransaction(inode_number, inode);
                if (status != Status::Succeeded) {
                    this->AbortTransaction();
                    return status;
                }
                status = this->CommitTransaction();
                if (status != Status::Succeeded) {
                    return status;
                }
                ++this->statistics_.orphan_reap_count;
            }
        }
    }
    return Status::Succeeded;
}

Status RootFileSystem::ReapV5Orphans() noexcept {
    if (!this->v5_active_) {
        return Status::InvalidArgument;
    }
    for (uint64_t group_index = 0ULL; group_index < this->v5_superblock_.group_count;
         ++group_index) {
        const RootV5GroupDescriptor &descriptor = this->v5_group_descriptors_[group_index];
        RootV5GroupDescriptor initial_descriptor{};
        if (BuildInitialRootV5GroupDescriptor(this->v5_superblock_, group_index,
                                              initial_descriptor) !=
            RootV5FormatStatus::Succeeded) {
            return Status::Corrupt;
        }
        if (descriptor.free_inode_count == initial_descriptor.free_inode_count) {
            continue;
        }
        Status status = this->ReadRelativeBlock(descriptor.inode_bitmap_block,
                                                this->orphan_bitmap_block_scratch_);
        if (status != Status::Succeeded) {
            return status;
        }
        for (uint64_t local_inode_index = 0ULL; local_inode_index < descriptor.inode_count;
             ++local_inode_index) {
            const uint64_t inode_number = descriptor.inode_start_number + local_inode_index;
            if (inode_number < this->v5_superblock_.first_user_inode_number ||
                !BitmapBitIsSet(this->orphan_bitmap_block_scratch_, local_inode_index)) {
                continue;
            }
            RootInode inode{};
            status = this->ReadInode(inode_number, inode);
            if (status != Status::Succeeded) {
                return status;
            }
            if ((inode.flags & OS_KERNEL_ROOTFS_V5_INODE_FLAG_ORPHAN) == 0ULL) {
                continue;
            }
            if (inode.link_count != 0ULL) {
                return Status::Corrupt;
            }
            status = this->BeginTransaction();
            if (status == Status::Succeeded) {
                status = this->RemoveInodeInTransaction(inode_number, inode);
            }
            if (status == Status::Succeeded) {
                status = this->CommitTransaction();
            }
            if (status != Status::Succeeded) {
                this->AbortTransaction();
                return status;
            }
            ++this->statistics_.orphan_reap_count;
        }
    }
    return Status::Succeeded;
}

Status RootFileSystem::LoadRecoveryStatistics() noexcept {
    this->statistics_.allocated_inode_count = this->disk_superblock_.allocated_inode_count;
    this->statistics_.allocated_data_block_count =
        this->disk_superblock_.allocated_data_block_count;
    this->statistics_.allocated_metadata_block_count =
        this->disk_superblock_.allocated_metadata_block_count;
    this->statistics_.free_data_block_count = this->disk_superblock_.data_block_count -
                                              this->disk_superblock_.allocated_data_block_count -
                                              this->disk_superblock_.allocated_metadata_block_count;
    return Status::Succeeded;
}

uint64_t RootFileSystem::ReadCurrentTimestamp() const noexcept {
    const uint64_t timestamp = this->timestamp_source_ == nullptr ? OS_KERNEL_ROOTFS_EMPTY_VALUE
                                                                  : this->timestamp_source_();
    return timestamp == OS_KERNEL_ROOTFS_EMPTY_VALUE ? this->disk_superblock_.transaction_generation
                                                     : timestamp;
}

Status RootFileSystem::ValidateVnode(const Vnode &vnode, RootInode &inode) noexcept {
    inode = RootInode{};
    if (!this->initialized_ || vnode.superblock != &this->vfs_superblock_ ||
        vnode.identifier == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        vnode.identifier > this->disk_superblock_.inode_count ||
        vnode.generation == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        (vnode.type != NodeType::RegularFile && vnode.type != NodeType::Directory &&
         vnode.type != NodeType::SymbolicLink)) {
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
    if (type == RootNodeType::SymbolicLink) {
        return NodeType::SymbolicLink;
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
    if (type == NodeType::SymbolicLink) {
        return RootNodeType::SymbolicLink;
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
    RuntimeMutexGuard guard{file_system.lock_};
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
        (child_inode.type == RootNodeType::Directory &&
         child_inode.parent_inode_number != directory.identifier)) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    vnode = file_system.MakeVnode(location.entry.inode_number, child_inode);
    return vnode.type == NodeType::None ? Status::Corrupt : Status::Succeeded;
}

Status RootFileSystem::CreateOperation(void *const context, const Vnode &directory,
                                       const uint8_t *const name, const uint64_t name_length_bytes,
                                       const NodeType type,
                                       const NodeCreationAttributes &attributes,
                                       Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || !NameIsValid(name, name_length_bytes) ||
        (type != NodeType::RegularFile && type != NodeType::Directory) ||
        !security::ModeTypeMatches(attributes.mode, type == NodeType::Directory
                                                        ? os::abi::OS_ABI_FILE_MODE_DIRECTORY
                                                        : os::abi::OS_ABI_FILE_MODE_REGULAR)) {
        return name_length_bytes > OS_KERNEL_ROOTFS_MAXIMUM_NAME_LENGTH_BYTES
                   ? Status::NameTooLong
                   : Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
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
    status = file_system.FindFreeBitmapBit(true, OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
                                           file_system.disk_superblock_.inode_count -
                                               OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
                                           free_inode_bit);
    if (status != Status::Succeeded) {
        return status;
    }
    static_cast<void>(free_inode_bit);
    uint64_t slot_offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.FindDirectorySlot(directory_inode, slot_offset_bytes);
    if (status != Status::Succeeded) {
        return status;
    }
    const uint64_t first_logical_block =
        slot_offset_bytes / file_system.ActiveBlockSizeBytes();
    const uint64_t last_logical_block =
        (slot_offset_bytes + OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES -
         OS_KERNEL_ROOTFS_COUNTER_INCREMENT) /
        file_system.ActiveBlockSizeBytes();
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
        file_system.AbortTransaction();
        return status;
    }
    const uint64_t timestamp = file_system.ReadCurrentTimestamp();
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
        .quadruple_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .quintuple_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .access_time_nanoseconds = timestamp,
        .modification_time_nanoseconds = timestamp,
        .change_time_nanoseconds = timestamp,
        .birth_time_nanoseconds = timestamp,
        .owner_user_identifier = attributes.owner_user_identifier,
        .owner_group_identifier = attributes.owner_group_identifier,
        .mode = attributes.mode,
    };
    ++file_system.disk_superblock_.next_inode_generation;
    if (file_system.v5_active_ && type == NodeType::Directory) {
        const uint64_t group_index =
            (inode_number - 1ULL) / file_system.v5_superblock_.inodes_per_group;
        ++file_system.v5_group_descriptors_[group_index].used_directory_count;
        ++file_system.v5_group_descriptors_[group_index].metadata_generation;
        ++file_system.v5_superblock_.allocated_directory_count;
        status = file_system.StageV5GroupDescriptor(group_index);
        if (status != Status::Succeeded) {
            file_system.AbortTransaction();
            return status;
        }
    }
    status = file_system.WriteInode(inode_number, inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
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
    directory_inode.modification_time_nanoseconds = timestamp;
    directory_inode.change_time_nanoseconds = timestamp;
    status = file_system.WriteDirectoryEntryAt(directory.identifier, directory_inode,
                                               slot_offset_bytes, entry);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
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
    RuntimeMutexGuard guard{file_system.lock_};
    RootInode inode{};
    const Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (file_system.statistics_.open_reference_count == UINT64_MAX) {
        return Status::CapacityExhausted;
    }
    if (file_system.RetainOpenReference(vnode.identifier) != Status::Succeeded) {
        return Status::CapacityExhausted;
    }
    ++file_system.statistics_.open_reference_count;
    return Status::Succeeded;
}

Status RootFileSystem::CloseOperation(void *const context, const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
    if (vnode.type == NodeType::Directory) {
        // 目录持有 open reference 时不能被 rmdir 或复用，也不会进入 orphan。
        // close 只归还既有引用，不能为了重复验证 inode 把设备瞬态引入退出路径。
        if (!file_system.initialized_ || vnode.superblock != &file_system.vfs_superblock_ ||
            vnode.identifier == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
            vnode.identifier > file_system.disk_superblock_.inode_count ||
            vnode.generation == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return Status::InvalidHandle;
        }
        if (file_system.ReadOpenReferenceCount(vnode.identifier) ==
                OS_KERNEL_ROOTFS_EMPTY_VALUE ||
            file_system.statistics_.open_reference_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return Status::InvalidHandle;
        }
        if (file_system.ReleaseOpenReference(vnode.identifier) != Status::Succeeded) {
            return Status::InvalidHandle;
        }
        --file_system.statistics_.open_reference_count;
        return Status::Succeeded;
    }
    RootInode inode{};
    const Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (file_system.ReadOpenReferenceCount(vnode.identifier) == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        file_system.statistics_.open_reference_count == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::InvalidHandle;
    }
    const uint64_t open_count = file_system.ReadOpenReferenceCount(vnode.identifier);
    if (open_count == OS_KERNEL_ROOTFS_COUNTER_INCREMENT &&
        inode.link_count == OS_KERNEL_ROOTFS_EMPTY_VALUE &&
        (inode.flags & OS_KERNEL_ROOTFS_INODE_FLAG_ORPHAN) != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        Status reap_status = file_system.BeginTransaction();
        if (reap_status != Status::Succeeded) {
            return reap_status;
        }
        if (file_system.ReleaseOpenReference(vnode.identifier) != Status::Succeeded) {
            file_system.AbortTransaction();
            return Status::InvalidHandle;
        }
        --file_system.statistics_.open_reference_count;
        reap_status = file_system.RemoveInodeInTransaction(vnode.identifier, inode);
        if (reap_status != Status::Succeeded) {
            static_cast<void>(file_system.RetainOpenReference(vnode.identifier));
            ++file_system.statistics_.open_reference_count;
            file_system.AbortTransaction();
            return reap_status;
        }
        reap_status = file_system.CommitTransaction();
        if (reap_status == Status::Succeeded) {
            ++file_system.statistics_.orphan_reap_count;
        }
        return reap_status;
    }
    if (file_system.ReleaseOpenReference(vnode.identifier) != Status::Succeeded) {
        return Status::InvalidHandle;
    }
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
    RuntimeMutexGuard guard{file_system.lock_};
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
    if (actual_type != expected_type &&
        !(expected_type == NodeType::RegularFile && actual_type == NodeType::SymbolicLink)) {
        return expected_type == NodeType::Directory ? Status::NotDirectory : Status::IsDirectory;
    }
    RootInode child_inode{};
    status = file_system.ReadInode(location.entry.inode_number, child_inode);
    if (status != Status::Succeeded || child_inode.generation != location.entry.inode_generation ||
        child_inode.type != location.entry.type ||
        (child_inode.type == RootNodeType::Directory &&
         child_inode.parent_inode_number != directory.identifier)) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    if (child_inode.type == RootNodeType::Directory &&
            file_system.ReadOpenReferenceCount(location.entry.inode_number) !=
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
        file_system.AbortTransaction();
        return status;
    }
    status = child_inode.type == RootNodeType::Directory
                 ? file_system.RemoveInodeInTransaction(location.entry.inode_number, child_inode)
                 : file_system.DropLinkInTransaction(location.entry.inode_number, child_inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    if (file_system.v5_active_ && child_inode.type == RootNodeType::Directory) {
        const uint64_t group_index =
            (location.entry.inode_number - 1ULL) /
            file_system.v5_superblock_.inodes_per_group;
        if (file_system.v5_group_descriptors_[group_index].used_directory_count == 0ULL ||
            file_system.v5_superblock_.allocated_directory_count == 0ULL) {
            file_system.AbortTransaction();
            return Status::Corrupt;
        }
        --file_system.v5_group_descriptors_[group_index].used_directory_count;
        ++file_system.v5_group_descriptors_[group_index].metadata_generation;
        --file_system.v5_superblock_.allocated_directory_count;
        status = file_system.StageV5GroupDescriptor(group_index);
        if (status != Status::Succeeded) {
            file_system.AbortTransaction();
            return status;
        }
    }
    status = file_system.TrimDirectoryTail(directory.identifier, directory_inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    const uint64_t timestamp = file_system.ReadCurrentTimestamp();
    directory_inode.modification_time_nanoseconds = timestamp;
    directory_inode.change_time_nanoseconds = timestamp;
    status = file_system.WriteInode(directory.identifier, directory_inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
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
    RuntimeMutexGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    ClearBytes(reinterpret_cast<uint8_t *>(&file_system.rename_scratch_),
               sizeof(file_system.rename_scratch_));
    RootInode &source_parent_inode = file_system.rename_scratch_.source_parent_inode;
    RootInode &destination_parent_storage = file_system.rename_scratch_.destination_parent_inode;
    RootInode &source_inode = file_system.rename_scratch_.source_inode;
    RootInode &destination_inode = file_system.rename_scratch_.destination_inode;
    DirectoryEntryLocation &source_location = file_system.rename_scratch_.source_location;
    DirectoryEntryLocation &destination_location = file_system.rename_scratch_.destination_location;
    Status status = file_system.ValidateVnode(source_directory, source_parent_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    const bool same_parent = source_directory.identifier == destination_directory.identifier &&
                             source_directory.generation == destination_directory.generation;
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

    status = file_system.FindDirectoryEntry(source_parent_inode, source_name,
                                            source_name_length_bytes, source_location);
    if (status != Status::Succeeded) {
        return status;
    }
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
    status = file_system.ReadInode(source_location.entry.inode_number, source_inode);
    if (status != Status::Succeeded ||
        source_inode.generation != source_location.entry.inode_generation ||
        source_inode.type != source_location.entry.type ||
        (source_inode.type == RootNodeType::Directory &&
         source_inode.parent_inode_number != source_directory.identifier)) {
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    if (source_inode.type == RootNodeType::Directory) {
        uint64_t ancestor_inode_number = destination_directory.identifier;
        for (uint64_t traversal_count = OS_KERNEL_ROOTFS_FIRST_INDEX;
             traversal_count < OS_KERNEL_ROOTFS_MAXIMUM_TRAVERSAL_COUNT; ++traversal_count) {
            if (ancestor_inode_number == source_location.entry.inode_number) {
                return Status::LoopDetected;
            }
            if (ancestor_inode_number == file_system.ActiveRootInodeNumber()) {
                break;
            }
            ClearBytes(reinterpret_cast<uint8_t *>(&destination_inode), sizeof(destination_inode));
            status = file_system.ReadInode(ancestor_inode_number, destination_inode);
            if (status != Status::Succeeded || destination_inode.type != RootNodeType::Directory) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            ancestor_inode_number = destination_inode.parent_inode_number;
        }
        if (ancestor_inode_number != file_system.ActiveRootInodeNumber()) {
            return Status::LoopDetected;
        }
    }

    ClearBytes(reinterpret_cast<uint8_t *>(&destination_inode), sizeof(destination_inode));
    const bool destination_exists = destination_lookup_status == Status::Succeeded;
    if (destination_exists) {
        if (!replace) {
            return Status::AlreadyExists;
        }
        status = file_system.ReadInode(destination_location.entry.inode_number, destination_inode);
        if (status != Status::Succeeded ||
            destination_inode.generation != destination_location.entry.inode_generation ||
            destination_inode.type != destination_location.entry.type ||
            (destination_inode.type == RootNodeType::Directory &&
             destination_inode.parent_inode_number != destination_directory.identifier)) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        if (destination_inode.type != source_inode.type) {
            return source_inode.type == RootNodeType::Directory ? Status::NotDirectory
                                                                : Status::IsDirectory;
        }
        if (destination_inode.type == RootNodeType::Directory &&
            file_system.ReadOpenReferenceCount(destination_location.entry.inode_number) !=
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
            destination_location.offset_bytes / file_system.ActiveBlockSizeBytes();
        const uint64_t last_logical_block =
            (destination_location.offset_bytes + OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES -
             OS_KERNEL_ROOTFS_COUNTER_INCREMENT) /
            file_system.ActiveBlockSizeBytes();
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
        status = destination_inode.type == RootNodeType::Directory
                     ? file_system.RemoveInodeInTransaction(destination_location.entry.inode_number,
                                                            destination_inode)
                     : file_system.DropLinkInTransaction(destination_location.entry.inode_number,
                                                         destination_inode);
        if (status != Status::Succeeded) {
            file_system.AbortTransaction();
            return status;
        }
        if (file_system.v5_active_ && destination_inode.type == RootNodeType::Directory) {
            const uint64_t group_index =
                (destination_location.entry.inode_number - 1ULL) /
                file_system.v5_superblock_.inodes_per_group;
            if (file_system.v5_group_descriptors_[group_index].used_directory_count == 0ULL ||
                file_system.v5_superblock_.allocated_directory_count == 0ULL) {
                file_system.AbortTransaction();
                return Status::Corrupt;
            }
            --file_system.v5_group_descriptors_[group_index].used_directory_count;
            ++file_system.v5_group_descriptors_[group_index].metadata_generation;
            --file_system.v5_superblock_.allocated_directory_count;
            status = file_system.StageV5GroupDescriptor(group_index);
            if (status != Status::Succeeded) {
                file_system.AbortTransaction();
                return status;
            }
        }
    }
    RootDirectoryEntry &destination_entry = file_system.rename_scratch_.destination_entry;
    destination_entry.inode_number = source_location.entry.inode_number;
    destination_entry.inode_generation = source_location.entry.inode_generation;
    destination_entry.type = source_location.entry.type;
    destination_entry.name_length_bytes = destination_name_length_bytes;
    CopyBytes(destination_entry.name, destination_name, destination_name_length_bytes);
    status = file_system.WriteDirectoryEntryAt(
        destination_directory.identifier, *destination_parent_inode,
        destination_location.offset_bytes, destination_entry);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    if (file_system.v5_active_ && same_parent) {
        status = file_system.FindDirectoryEntry(source_parent_inode, source_name,
                                                source_name_length_bytes, source_location);
        if (status != Status::Succeeded) {
            file_system.AbortTransaction();
            return status;
        }
    }
    RootDirectoryEntry &empty_entry = file_system.rename_scratch_.empty_entry;
    empty_entry.type = RootNodeType::Unused;
    status = file_system.WriteDirectoryEntryAt(source_directory.identifier, source_parent_inode,
                                               source_location.offset_bytes, empty_entry);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    const uint64_t timestamp = file_system.ReadCurrentTimestamp();
    source_inode.change_time_nanoseconds = timestamp;
    source_parent_inode.modification_time_nanoseconds = timestamp;
    source_parent_inode.change_time_nanoseconds = timestamp;
    destination_parent_inode->modification_time_nanoseconds = timestamp;
    destination_parent_inode->change_time_nanoseconds = timestamp;
    if (!same_parent && source_inode.type == RootNodeType::Directory) {
        source_inode.parent_inode_number = destination_directory.identifier;
    }
    status = file_system.WriteInode(source_location.entry.inode_number, source_inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    status = file_system.TrimDirectoryTail(source_directory.identifier, source_parent_inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    if (!same_parent) {
        status = file_system.TrimDirectoryTail(destination_directory.identifier,
                                               *destination_parent_inode);
        if (status != Status::Succeeded) {
            file_system.AbortTransaction();
            return status;
        }
    }
    status = file_system.WriteInode(source_directory.identifier, source_parent_inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    if (!same_parent) {
        status =
            file_system.WriteInode(destination_directory.identifier, *destination_parent_inode);
        if (status != Status::Succeeded) {
            file_system.AbortTransaction();
            return status;
        }
    }
    status = file_system.CommitTransaction();
    if (status == Status::Succeeded) {
        ++file_system.statistics_.rename_count;
    }
    return status;
}

Status RootFileSystem::LinkOperation(void *const context, const Vnode &source,
                                     const Vnode &destination_directory,
                                     const uint8_t *const destination_name,
                                     const uint64_t destination_name_length_bytes) noexcept {
    if (context == nullptr || !NameIsValid(destination_name, destination_name_length_bytes)) {
        return destination_name_length_bytes > OS_KERNEL_ROOTFS_MAXIMUM_NAME_LENGTH_BYTES
                   ? Status::NameTooLong
                   : Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode source_inode{};
    Status status = file_system.ValidateVnode(source, source_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (source_inode.type == RootNodeType::Directory) {
        return Status::PermissionDenied;
    }
    if (source_inode.link_count == UINT64_MAX ||
        (source_inode.flags & OS_KERNEL_ROOTFS_INODE_FLAG_ORPHAN) != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::CapacityExhausted;
    }
    RootInode destination_parent_inode{};
    status = file_system.ValidateVnode(destination_directory, destination_parent_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (destination_parent_inode.type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }
    DirectoryEntryLocation existing{};
    status = file_system.FindDirectoryEntry(destination_parent_inode, destination_name,
                                            destination_name_length_bytes, existing);
    if (status == Status::Succeeded) {
        return Status::AlreadyExists;
    }
    if (status != Status::NotFound) {
        return status;
    }
    uint64_t slot_offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.FindDirectorySlot(destination_parent_inode, slot_offset_bytes);
    if (status != Status::Succeeded) {
        return status;
    }
    const uint64_t first_logical_block =
        slot_offset_bytes / file_system.ActiveBlockSizeBytes();
    const uint64_t last_logical_block =
        (slot_offset_bytes + OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES -
         OS_KERNEL_ROOTFS_COUNTER_INCREMENT) /
        file_system.ActiveBlockSizeBytes();
    uint64_t required_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    for (uint64_t logical_block = first_logical_block; logical_block <= last_logical_block;
         ++logical_block) {
        uint64_t block_requirement = OS_KERNEL_ROOTFS_EMPTY_VALUE;
        status = file_system.RequiredBlocksForLogicalBlock(destination_parent_inode, logical_block,
                                                           block_requirement);
        if (status != Status::Succeeded || required_block_count > UINT64_MAX - block_requirement) {
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
        required_block_count += block_requirement;
    }
    if (required_block_count > file_system.statistics_.free_data_block_count) {
        return Status::CapacityExhausted;
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    const uint64_t timestamp = file_system.ReadCurrentTimestamp();
    source_inode.link_count += OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
    source_inode.change_time_nanoseconds = timestamp;
    destination_parent_inode.modification_time_nanoseconds = timestamp;
    destination_parent_inode.change_time_nanoseconds = timestamp;
    RootDirectoryEntry entry{
        .inode_number = source.identifier,
        .inode_generation = source.generation,
        .type = source_inode.type,
        .name_length_bytes = destination_name_length_bytes,
        .name = {},
    };
    CopyBytes(entry.name, destination_name, destination_name_length_bytes);
    status = file_system.WriteDirectoryEntryAt(destination_directory.identifier,
                                               destination_parent_inode, slot_offset_bytes, entry);
    if (status == Status::Succeeded) {
        status = file_system.WriteInode(source.identifier, source_inode);
    }
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    status = file_system.CommitTransaction();
    if (status == Status::Succeeded) {
        ++file_system.statistics_.link_count;
    }
    return status;
}

Status RootFileSystem::CreateSymbolicLinkOperation(
    void *const context, const Vnode &destination_directory, const uint8_t *const destination_name,
    const uint64_t destination_name_length_bytes, const uint8_t *const target,
    const uint64_t target_length_bytes, const NodeCreationAttributes &attributes,
    Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || !NameIsValid(destination_name, destination_name_length_bytes) ||
        target == nullptr || target_length_bytes == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        target_length_bytes > OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES ||
        !security::ModeTypeMatches(attributes.mode, os::abi::OS_ABI_FILE_MODE_SYMBOLIC_LINK)) {
        return target_length_bytes > OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES
                   ? Status::PathTooLong
                   : Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode directory_inode{};
    Status status = file_system.ValidateVnode(destination_directory, directory_inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (directory_inode.type != RootNodeType::Directory) {
        return Status::NotDirectory;
    }
    DirectoryEntryLocation existing{};
    status = file_system.FindDirectoryEntry(directory_inode, destination_name,
                                            destination_name_length_bytes, existing);
    if (status == Status::Succeeded) {
        return Status::AlreadyExists;
    }
    if (status != Status::NotFound) {
        return status;
    }
    uint64_t free_inode_bit = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.FindFreeBitmapBit(true, OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
                                           file_system.disk_superblock_.inode_count -
                                               OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
                                           free_inode_bit);
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t slot_offset_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.FindDirectorySlot(directory_inode, slot_offset_bytes);
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t required_block_count =
        DivideRoundUp(target_length_bytes, file_system.ActiveBlockSizeBytes());
    const uint64_t first_logical_block =
        slot_offset_bytes / file_system.ActiveBlockSizeBytes();
    const uint64_t last_logical_block =
        (slot_offset_bytes + OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES -
         OS_KERNEL_ROOTFS_COUNTER_INCREMENT) /
        file_system.ActiveBlockSizeBytes();
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
    if (required_block_count > file_system.statistics_.free_data_block_count ||
        file_system.disk_superblock_.next_inode_generation == UINT64_MAX) {
        return Status::CapacityExhausted;
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    uint64_t inode_number = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.AllocateInodeNumber(inode_number);
    if (status != Status::Succeeded ||
        inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT != free_inode_bit) {
        file_system.AbortTransaction();
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    const uint64_t timestamp = file_system.ReadCurrentTimestamp();
    RootInode inode{
        .type = RootNodeType::SymbolicLink,
        .flags = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .size_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .generation = file_system.disk_superblock_.next_inode_generation,
        .link_count = OS_KERNEL_ROOTFS_COUNTER_INCREMENT,
        .allocated_data_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .allocated_metadata_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .parent_inode_number = destination_directory.identifier,
        .direct_blocks = {},
        .single_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .double_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .triple_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .quadruple_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .quintuple_indirect_block = OS_KERNEL_ROOTFS_EMPTY_VALUE,
        .access_time_nanoseconds = timestamp,
        .modification_time_nanoseconds = timestamp,
        .change_time_nanoseconds = timestamp,
        .birth_time_nanoseconds = timestamp,
        .owner_user_identifier = attributes.owner_user_identifier,
        .owner_group_identifier = attributes.owner_group_identifier,
        .mode = attributes.mode,
    };
    ++file_system.disk_superblock_.next_inode_generation;
    uint64_t written_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status =
        file_system.WriteFileBytesInTransaction(inode_number, inode, OS_KERNEL_ROOTFS_EMPTY_VALUE,
                                                target, target_length_bytes, false, written_bytes);
    if (status != Status::Succeeded || written_bytes != target_length_bytes) {
        file_system.AbortTransaction();
        return status == Status::Succeeded ? Status::Corrupt : status;
    }
    RootDirectoryEntry entry{
        .inode_number = inode_number,
        .inode_generation = inode.generation,
        .type = inode.type,
        .name_length_bytes = destination_name_length_bytes,
        .name = {},
    };
    CopyBytes(entry.name, destination_name, destination_name_length_bytes);
    directory_inode.modification_time_nanoseconds = timestamp;
    directory_inode.change_time_nanoseconds = timestamp;
    status = file_system.WriteDirectoryEntryAt(destination_directory.identifier, directory_inode,
                                               slot_offset_bytes, entry);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    status = file_system.CommitTransaction();
    if (status == Status::Succeeded) {
        ++file_system.statistics_.create_count;
        vnode = file_system.MakeVnode(inode_number, inode);
    }
    return status;
}

Status RootFileSystem::ReadSymbolicLinkOperation(void *const context, const Vnode &vnode,
                                                 uint8_t *const destination,
                                                 const uint64_t capacity_bytes,
                                                 uint64_t &target_length_bytes) noexcept {
    target_length_bytes = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    if (context == nullptr ||
        (destination == nullptr && capacity_bytes != OS_KERNEL_ROOTFS_EMPTY_VALUE)) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
    RootInode inode{};
    Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (inode.type != RootNodeType::SymbolicLink) {
        return Status::InvalidArgument;
    }
    if (inode.size_bytes > capacity_bytes) {
        return Status::PathTooLong;
    }
    status = file_system.ReadFileBytes(inode, OS_KERNEL_ROOTFS_EMPTY_VALUE, destination,
                                       inode.size_bytes, target_length_bytes);
    return status == Status::Succeeded && target_length_bytes == inode.size_bytes
               ? Status::Succeeded
               : (status == Status::Succeeded ? Status::Corrupt : status);
}

Status RootFileSystem::ParentOperation(void *const context, const Vnode &vnode,
                                       Vnode &parent) noexcept {
    parent = Vnode{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
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
    RuntimeMutexGuard guard{file_system.lock_};
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
    RuntimeMutexGuard guard{file_system.lock_};
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
    if (offset_bytes > file_system.disk_superblock_.maximum_file_size_bytes ||
        length_bytes > file_system.disk_superblock_.maximum_file_size_bytes - offset_bytes) {
        return Status::FileTooLarge;
    }
    uint64_t first_required_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    status = file_system.RequiredBlocksForLogicalBlock(
        inode, offset_bytes / file_system.ActiveBlockSizeBytes(), first_required_block_count);
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
    const uint64_t timestamp = file_system.ReadCurrentTimestamp();
    inode.modification_time_nanoseconds = timestamp;
    inode.change_time_nanoseconds = timestamp;
    status = file_system.WriteFileBytesInTransaction(vnode.identifier, inode, offset_bytes, source,
                                                     length_bytes, false, written_bytes);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
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
    RuntimeMutexGuard guard{file_system.lock_};
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
    if (size_bytes > file_system.disk_superblock_.maximum_file_size_bytes) {
        return Status::FileTooLarge;
    }
    if (size_bytes == inode.size_bytes) {
        return Status::Succeeded;
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    const uint64_t timestamp = file_system.ReadCurrentTimestamp();
    inode.modification_time_nanoseconds = timestamp;
    inode.change_time_nanoseconds = timestamp;
    status = file_system.TruncateInTransaction(vnode.identifier, inode, size_bytes);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
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
    RuntimeMutexGuard guard{file_system.lock_};
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
    RuntimeMutexGuard guard{file_system.lock_};
    RootInode inode{};
    Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    if (vnode.identifier == file_system.ActiveRootInodeNumber()) {
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
    RuntimeMutexGuard guard{file_system.lock_};
    RootInode inode{};
    const Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    const uint64_t allocated_block_count =
        inode.allocated_data_block_count + inode.allocated_metadata_block_count;
    if (allocated_block_count > UINT64_MAX / file_system.ActiveBlockSizeBytes()) {
        return Status::Corrupt;
    }
    information = BackendNodeInformation{
        .size_bytes = inode.size_bytes,
        .allocated_size_bytes = allocated_block_count * file_system.ActiveBlockSizeBytes(),
        .link_count = inode.link_count,
        .access_time_nanoseconds = inode.access_time_nanoseconds,
        .modification_time_nanoseconds = inode.modification_time_nanoseconds,
        .change_time_nanoseconds = inode.change_time_nanoseconds,
        .birth_time_nanoseconds = inode.birth_time_nanoseconds,
        .owner_user_identifier = inode.owner_user_identifier,
        .owner_group_identifier = inode.owner_group_identifier,
        .mode = inode.mode,
    };
    return Status::Succeeded;
}

Status RootFileSystem::ChangeModeOperation(void *const context, const Vnode &vnode,
                                           const os::abi::FileMode mode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode inode{};
    Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded ||
        !security::ModeTypeMatches(mode, inode.mode & os::abi::OS_ABI_FILE_MODE_TYPE_MASK)) {
        return status != Status::Succeeded ? status : Status::InvalidArgument;
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    inode.mode = mode;
    inode.change_time_nanoseconds = file_system.ReadCurrentTimestamp();
    status = file_system.WriteInode(vnode.identifier, inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    return file_system.CommitTransaction();
}

Status
RootFileSystem::ChangeOwnerOperation(void *const context, const Vnode &vnode,
                                     const os::abi::UserIdentifier user_identifier,
                                     const os::abi::GroupIdentifier group_identifier) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
    if (file_system.vfs_superblock_.read_only) {
        return Status::ReadOnly;
    }
    RootInode inode{};
    Status status = file_system.ValidateVnode(vnode, inode);
    if (status != Status::Succeeded) {
        return status;
    }
    status = file_system.BeginTransaction();
    if (status != Status::Succeeded) {
        return status;
    }
    inode.owner_user_identifier = user_identifier;
    inode.owner_group_identifier = group_identifier;
    inode.mode &= ~(os::abi::OS_ABI_FILE_MODE_SET_USER_IDENTIFIER |
                    os::abi::OS_ABI_FILE_MODE_SET_GROUP_IDENTIFIER);
    inode.change_time_nanoseconds = file_system.ReadCurrentTimestamp();
    status = file_system.WriteInode(vnode.identifier, inode);
    if (status != Status::Succeeded) {
        file_system.AbortTransaction();
        return status;
    }
    return file_system.CommitTransaction();
}

Status RootFileSystem::SyncOperation(void *const context) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    RootFileSystem &file_system = *static_cast<RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
    if (!file_system.initialized_ || file_system.device_ == nullptr) {
        return Status::NotInitialized;
    }
    if (file_system.failed_) {
        return Status::DeviceFailure;
    }
    if (file_system.v5_active_) {
        if (EncodeRootV5Superblock(file_system.v5_superblock_,
                                   file_system.superblock_block_scratch_,
                                   OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) !=
            RootV5FormatStatus::Succeeded) {
            return Status::Corrupt;
        }
        for (uint64_t group_index = 0ULL;
             group_index < file_system.v5_superblock_.group_count; ++group_index) {
            const RootV5GroupDescriptor &descriptor =
                file_system.v5_group_descriptors_[group_index];
            if ((descriptor.flags & OS_KERNEL_ROOTFS_V5_GROUP_FLAG_HAS_SUPERBLOCK_COPY) == 0ULL) {
                continue;
            }
            Status status = file_system.WriteV5Block(descriptor.superblock_copy_block,
                                                     file_system.superblock_block_scratch_);
            for (uint64_t table_block = 0ULL;
                 status == Status::Succeeded &&
                 table_block < file_system.v5_superblock_.group_descriptor_table_block_count;
                 ++table_block) {
                status = file_system.ReadV5Block(
                    file_system.v5_superblock_.group_descriptor_table_start_block + table_block,
                    file_system.read_block_scratch_);
                if (status == Status::Succeeded) {
                    status = file_system.WriteV5Block(
                        descriptor.group_descriptor_copy_start_block + table_block,
                        file_system.read_block_scratch_);
                }
            }
            if (status != Status::Succeeded) {
                return status;
            }
        }
        return file_system.device_->Flush() == FileSystemBlockDeviceStatus::Succeeded
                   ? Status::Succeeded
                   : file_system.FailDeviceOperation();
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
    RuntimeMutexGuard guard{file_system.lock_};
    return file_system.ValidateUnlocked();
}

Status RootFileSystem::ReadResourceUsageOperation(void *const context,
                                                  ResourceUsage &usage) noexcept {
    usage = ResourceUsage{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const RootFileSystem &file_system = *static_cast<const RootFileSystem *>(context);
    RuntimeMutexGuard guard{file_system.lock_};
    if (!file_system.initialized_) {
        return Status::NotInitialized;
    }
    usage.vnode_count = file_system.statistics_.allocated_inode_count;
    return Status::Succeeded;
}

Status RootFileSystem::MarkValidationDataBlock(const uint64_t relative_block) noexcept {
    if (relative_block < this->disk_superblock_.data_start_relative_block ||
        relative_block >= this->disk_superblock_.total_block_count) {
        return Status::Corrupt;
    }
    bool allocated = false;
    const Status status = this->ReadBitmapBit(
        false, relative_block - this->disk_superblock_.data_start_relative_block, allocated);
    return status == Status::Succeeded && allocated
               ? Status::Succeeded
               : (status == Status::Succeeded ? Status::Corrupt : status);
}

Status RootFileSystem::ValidatePointerTree(const uint64_t relative_block, const uint64_t level,
                                           const uint64_t logical_start,
                                           const uint64_t logical_limit,
                                           uint64_t &metadata_block_count,
                                           uint64_t &data_block_count) noexcept {
    if (relative_block == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    if (level == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
        level > OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL) {
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
    logical_start += OS_KERNEL_ROOTFS_TRIPLE_INDIRECT_CAPACITY;
    status = this->ValidatePointerTree(inode.quadruple_indirect_block,
                                       OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_LEVEL, logical_start,
                                       logical_limit, metadata_block_count, data_block_count);
    if (status != Status::Succeeded) {
        return status;
    }
    logical_start += OS_KERNEL_ROOTFS_QUADRUPLE_INDIRECT_CAPACITY;
    status = this->ValidatePointerTree(inode.quintuple_indirect_block,
                                       OS_KERNEL_ROOTFS_QUINTUPLE_INDIRECT_LEVEL, logical_start,
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
         bitmap_block_index < this->disk_superblock_.inode_bitmap_block_count;
         ++bitmap_block_index) {
        const Status status = this->ReadRelativeBlock(
            this->disk_superblock_.inode_bitmap_start_relative_block + bitmap_block_index,
            this->validation_bitmap_block_scratch_);
        if (status != Status::Succeeded) {
            return status;
        }
        const uint64_t byte_start = bitmap_block_index * OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
        for (uint64_t byte_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
             byte_index < OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES; ++byte_index) {
            const uint8_t actual = this->validation_bitmap_block_scratch_[byte_index];
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
    allocated_block_count = this->disk_superblock_.allocated_data_block_count +
                            this->disk_superblock_.allocated_metadata_block_count;
    return Status::Succeeded;
}

Status RootFileSystem::ValidateUnlocked() noexcept {
    if (this->v5_active_) {
        if (!this->initialized_ || this->device_ == nullptr || this->failed_ ||
            !this->vfs_superblock_.initialized || this->vfs_superblock_.backend_context != this ||
            this->vfs_superblock_.operations != &RootFileSystem::operations ||
            this->v5_journal_.IsActive() ||
            ValidateRootV5Superblock(this->v5_superblock_) != RootV5FormatStatus::Succeeded) {
            return this->failed_ ? Status::DeviceFailure : Status::Corrupt;
        }
        if (this->last_validated_transaction_generation_ ==
            this->disk_superblock_.transaction_generation) {
            return Status::Succeeded;
        }
        uint64_t free_block_count = 0ULL;
        uint64_t free_inode_count = 0ULL;
        uint64_t directory_count = 0ULL;
        for (uint64_t group_index = 0ULL; group_index < this->v5_superblock_.group_count;
             ++group_index) {
            const RootV5GroupDescriptor &descriptor =
                this->v5_group_descriptors_[group_index];
            if (ValidateRootV5GroupDescriptor(this->v5_superblock_, descriptor) !=
                RootV5FormatStatus::Succeeded) {
                return Status::Corrupt;
            }
            RootV5GroupDescriptor initial_descriptor{};
            if (BuildInitialRootV5GroupDescriptor(this->v5_superblock_, group_index,
                                                  initial_descriptor) !=
                RootV5FormatStatus::Succeeded) {
                return Status::Corrupt;
            }
            const bool group_has_runtime_allocations =
                group_index == 0ULL ||
                descriptor.free_block_count != initial_descriptor.free_block_count ||
                descriptor.free_inode_count != initial_descriptor.free_inode_count ||
                descriptor.used_directory_count != initial_descriptor.used_directory_count;
            if (!group_has_runtime_allocations) {
                free_block_count += descriptor.free_block_count;
                free_inode_count += descriptor.free_inode_count;
                directory_count += descriptor.used_directory_count;
                continue;
            }
            Status status = this->ReadRelativeBlock(descriptor.block_bitmap_block,
                                                    this->validation_bitmap_block_scratch_);
            if (status != Status::Succeeded ||
                CalculateRootV5Crc32c(this->validation_bitmap_block_scratch_,
                                      OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) !=
                    descriptor.block_bitmap_checksum) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            uint64_t group_free_blocks = 0ULL;
            for (uint64_t bit_index = 0ULL; bit_index < descriptor.block_count; ++bit_index) {
                group_free_blocks +=
                    BitmapBitIsSet(this->validation_bitmap_block_scratch_, bit_index) ? 0ULL : 1ULL;
            }
            status = this->ReadRelativeBlock(descriptor.inode_bitmap_block,
                                             this->validation_bitmap_block_scratch_);
            if (status != Status::Succeeded ||
                CalculateRootV5Crc32c(this->validation_bitmap_block_scratch_,
                                      OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) !=
                    descriptor.inode_bitmap_checksum) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            uint64_t group_free_inodes = 0ULL;
            for (uint64_t bit_index = 0ULL; bit_index < descriptor.inode_count; ++bit_index) {
                group_free_inodes +=
                    BitmapBitIsSet(this->validation_bitmap_block_scratch_, bit_index) ? 0ULL : 1ULL;
            }
            if (group_free_blocks != descriptor.free_block_count ||
                group_free_inodes != descriptor.free_inode_count) {
                return Status::Corrupt;
            }
            free_block_count += group_free_blocks;
            free_inode_count += group_free_inodes;
            directory_count += descriptor.used_directory_count;
        }
        if (free_block_count != this->v5_superblock_.free_block_count ||
            free_inode_count != this->v5_superblock_.free_inode_count ||
            directory_count != this->v5_superblock_.allocated_directory_count) {
            return Status::Corrupt;
        }
        ClearBytes(this->validation_inode_bitmap_, sizeof(this->validation_inode_bitmap_));
        const uint64_t root_inode_number = this->ActiveRootInodeNumber();
        SetBitmapBit(this->validation_inode_bitmap_, root_inode_number - 1ULL, true);
        this->validation_queue_[0] = root_inode_number;
        uint64_t queue_read_index = 0ULL;
        uint64_t queue_write_index = 1ULL;
        uint64_t reachable_directory_count = 0ULL;
        uint64_t inode_data_block_count = 0ULL;
        uint64_t inode_metadata_block_count = 0ULL;
        const uint64_t bytes_read_before = this->statistics_.bytes_read;
        const uint64_t hole_bytes_before = this->statistics_.sparse_hole_read_bytes;
        uint64_t cached_inode_bitmap_group = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
        uint64_t cached_block_bitmap_group = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
        const auto read_validation_bit =
            [this, &cached_inode_bitmap_group,
             &cached_block_bitmap_group](const bool inode_bitmap, const uint64_t global_index,
                                         bool &allocated) noexcept -> Status {
            const uint64_t group_index =
                inode_bitmap ? global_index / this->v5_superblock_.inodes_per_group
                             : global_index / this->v5_superblock_.blocks_per_group;
            if (group_index >= this->v5_superblock_.group_count) {
                return Status::Corrupt;
            }
            uint64_t &cached_group =
                inode_bitmap ? cached_inode_bitmap_group : cached_block_bitmap_group;
            uint8_t *const bitmap = inode_bitmap ? this->orphan_bitmap_block_scratch_
                                                 : this->validation_bitmap_block_scratch_;
            const RootV5GroupDescriptor &descriptor =
                this->v5_group_descriptors_[group_index];
            if (cached_group != group_index) {
                const Status status = this->ReadRelativeBlock(
                    inode_bitmap ? descriptor.inode_bitmap_block : descriptor.block_bitmap_block,
                    bitmap);
                if (status != Status::Succeeded) {
                    return status;
                }
                cached_group = group_index;
            }
            const uint64_t local_index =
                inode_bitmap ? global_index % this->v5_superblock_.inodes_per_group
                             : global_index - descriptor.first_block;
            allocated = BitmapBitIsSet(bitmap, local_index);
            return Status::Succeeded;
        };
        while (queue_read_index < queue_write_index) {
            const uint64_t inode_number = this->validation_queue_[queue_read_index++];
            bool inode_allocated = false;
            Status status =
                read_validation_bit(true, inode_number - 1ULL, inode_allocated);
            RootInode inode{};
            if (status != Status::Succeeded || !inode_allocated ||
                this->ReadInode(inode_number, inode) != Status::Succeeded ||
                inode.type == RootNodeType::Unused || inode.flags != 0ULL) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            uint64_t extent_data_blocks = 0ULL;
            if (inode.direct_blocks[0] != 0ULL) {
                RootExtentNode &extent_node = this->v5_extent_scratch_;
                ClearBytes(reinterpret_cast<uint8_t *>(&extent_node), sizeof(extent_node));
                status = this->ReadV5ExtentNode(inode, extent_node);
                if (status != Status::Succeeded || extent_node.inode_number != inode_number ||
                    extent_node.inode_generation != inode.generation || extent_node.depth != 0ULL) {
                    return status == Status::Succeeded ? Status::Corrupt : status;
                }
                for (uint64_t entry_index = 0ULL; entry_index < extent_node.entry_count;
                     ++entry_index) {
                    const RootExtentNodeEntry &extent = extent_node.entries[entry_index];
                    extent_data_blocks += extent.block_count_or_generation;
                    for (uint64_t block_offset = 0ULL;
                         block_offset < extent.block_count_or_generation; ++block_offset) {
                        bool block_allocated = false;
                        status = read_validation_bit(
                            false, extent.physical_or_child_block + block_offset,
                            block_allocated);
                        if (status != Status::Succeeded || !block_allocated) {
                            return status == Status::Succeeded ? Status::Corrupt : status;
                        }
                    }
                }
                bool extent_root_allocated = false;
                status = read_validation_bit(false, inode.direct_blocks[0],
                                             extent_root_allocated);
                if (status != Status::Succeeded || !extent_root_allocated ||
                    extent_data_blocks != inode.allocated_data_block_count ||
                    inode.allocated_metadata_block_count != 1ULL) {
                    return status == Status::Succeeded ? Status::Corrupt : status;
                }
                ++inode_metadata_block_count;
            } else if (inode.size_bytes != 0ULL || inode.allocated_data_block_count != 0ULL ||
                       inode.allocated_metadata_block_count != 0ULL) {
                return Status::Corrupt;
            }
            inode_data_block_count += extent_data_blocks;
            if (inode_number == root_inode_number &&
                (inode.type != RootNodeType::Directory ||
                 inode.parent_inode_number != root_inode_number)) {
                return Status::Corrupt;
            }
            if (inode.type != RootNodeType::Directory) {
                continue;
            }
            ++reachable_directory_count;
            for (uint64_t offset_bytes = 0ULL; offset_bytes < inode.size_bytes;
                 offset_bytes += OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES) {
                RootDirectoryEntry entry{};
                status = this->ReadDirectoryEntryAt(inode, offset_bytes, entry);
                if (status != Status::Succeeded || entry.inode_number == root_inode_number ||
                    entry.inode_number > this->v5_superblock_.inode_count) {
                    return status == Status::Succeeded ? Status::Corrupt : status;
                }
                RootInode child{};
                status = this->ReadInode(entry.inode_number, child);
                if (status != Status::Succeeded || child.generation != entry.inode_generation ||
                    child.type != entry.type ||
                    (child.type == RootNodeType::Directory &&
                     child.parent_inode_number != inode_number)) {
                    return status == Status::Succeeded ? Status::Corrupt : status;
                }
                const uint64_t child_bit = entry.inode_number - 1ULL;
                if (BitmapBitIsSet(this->validation_inode_bitmap_, child_bit)) {
                    if (child.type == RootNodeType::Directory) {
                        return Status::Corrupt;
                    }
                    continue;
                }
                if (queue_write_index >= OS_KERNEL_ROOTFS_INODE_COUNT) {
                    return Status::Corrupt;
                }
                SetBitmapBit(this->validation_inode_bitmap_, child_bit, true);
                this->validation_queue_[queue_write_index++] = entry.inode_number;
            }
        }
        const uint64_t expected_allocated_inodes =
            this->v5_superblock_.inode_count - this->v5_superblock_.free_inode_count;
        if (queue_write_index + this->v5_superblock_.reserved_inode_count - 1ULL !=
                expected_allocated_inodes ||
            reachable_directory_count != this->v5_superblock_.allocated_directory_count) {
            return Status::Corrupt;
        }
        uint64_t open_reference_count = 0ULL;
        for (uint64_t slot_index = 0ULL;
             slot_index < OS_KERNEL_ROOTFS_V5_OPEN_REFERENCE_CAPACITY; ++slot_index) {
            if (open_reference_count >
                UINT64_MAX - this->v5_open_references_[slot_index].count) {
                return Status::Corrupt;
            }
            open_reference_count += this->v5_open_references_[slot_index].count;
        }
        if (open_reference_count != this->statistics_.open_reference_count) {
            return Status::Corrupt;
        }
        RootInode root_inode{};
        const Status root_status = this->ReadInode(root_inode_number, root_inode);
        if (root_status != Status::Succeeded ||
            this->vfs_superblock_.root.identifier != root_inode_number ||
            this->vfs_superblock_.root.generation != root_inode.generation ||
            this->vfs_superblock_.root.type != NodeType::Directory) {
            return root_status == Status::Succeeded ? Status::Corrupt : root_status;
        }
        this->statistics_.allocated_inode_count = expected_allocated_inodes;
        this->statistics_.allocated_data_block_count = inode_data_block_count;
        this->statistics_.allocated_metadata_block_count = inode_metadata_block_count;
        this->statistics_.free_data_block_count = this->v5_superblock_.free_block_count;
        this->statistics_.bytes_read = bytes_read_before;
        this->statistics_.sparse_hole_read_bytes = hole_bytes_before;
        this->last_validated_transaction_generation_ =
            this->disk_superblock_.transaction_generation;
        return Status::Succeeded;
    }
    if (!this->initialized_ || this->device_ == nullptr || this->failed_ ||
        !this->vfs_superblock_.initialized || this->vfs_superblock_.backend_context != this ||
        this->vfs_superblock_.operations != &RootFileSystem::operations ||
        this->disk_superblock_.transaction_state != RootTransactionState::Clean ||
        this->journal_.IsActive()) {
        return this->failed_ ? Status::DeviceFailure : Status::Corrupt;
    }
    if (this->last_validated_transaction_generation_ ==
        this->disk_superblock_.transaction_generation) {
        return Status::Succeeded;
    }
    ClearBytes(this->validation_inode_bitmap_, sizeof(this->validation_inode_bitmap_));
    ClearBytes(reinterpret_cast<uint8_t *>(this->validation_link_counts_),
               sizeof(this->validation_link_counts_));
    SetBitmapBit(this->validation_inode_bitmap_,
                 OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER - OS_KERNEL_ROOTFS_COUNTER_INCREMENT, true);
    this->validation_link_counts_[OS_KERNEL_ROOTFS_FIRST_INDEX] =
        OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
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
            inode.flags != OS_KERNEL_ROOTFS_EMPTY_VALUE) {
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
                entry.inode_number > this->disk_superblock_.inode_count) {
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
            RootInode child_inode{};
            status = this->ReadInode(entry.inode_number, child_inode);
            if (status != Status::Succeeded || child_inode.generation != entry.inode_generation ||
                child_inode.type != entry.type ||
                (child_inode.type == RootNodeType::Directory &&
                 child_inode.parent_inode_number != inode_number)) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            const uint64_t child_bit_index =
                entry.inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
            if (this->validation_link_counts_[child_bit_index] == UINT64_MAX) {
                return Status::Corrupt;
            }
            ++this->validation_link_counts_[child_bit_index];
            if (BitmapBitIsSet(this->validation_inode_bitmap_, child_bit_index)) {
                if (child_inode.type == RootNodeType::Directory) {
                    return Status::Corrupt;
                }
                continue;
            }
            if (queue_write_index >= this->disk_superblock_.inode_count) {
                return Status::Corrupt;
            }
            SetBitmapBit(this->validation_inode_bitmap_, child_bit_index, true);
            this->validation_queue_[queue_write_index] = entry.inode_number;
            ++queue_write_index;
        }
    }

    uint64_t validated_inode_count = queue_write_index;
    for (uint64_t inode_number =
             OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER + OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
         inode_number <= this->disk_superblock_.inode_count; ++inode_number) {
        const uint64_t inode_index = inode_number - OS_KERNEL_ROOTFS_COUNTER_INCREMENT;
        bool allocated = false;
        Status status = this->ReadBitmapBit(true, inode_index, allocated);
        if (status != Status::Succeeded) {
            return status;
        }
        if (!allocated) {
            continue;
        }
        RootInode inode{};
        status = this->ReadInode(inode_number, inode);
        if (BitmapBitIsSet(this->validation_inode_bitmap_, inode_index)) {
            if (status != Status::Succeeded || inode.flags != OS_KERNEL_ROOTFS_EMPTY_VALUE ||
                inode.link_count != this->validation_link_counts_[inode_index]) {
                return status == Status::Succeeded ? Status::Corrupt : status;
            }
            continue;
        }
        if (status != Status::Succeeded ||
            (inode.flags & OS_KERNEL_ROOTFS_INODE_FLAG_ORPHAN) == OS_KERNEL_ROOTFS_EMPTY_VALUE ||
            inode.link_count != OS_KERNEL_ROOTFS_EMPTY_VALUE ||
            this->open_counts_[inode_index] == OS_KERNEL_ROOTFS_EMPTY_VALUE) {
            return status == Status::Succeeded ? Status::Corrupt : status;
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
        if (inode.generation > maximum_inode_generation) {
            maximum_inode_generation = inode.generation;
        }
        SetBitmapBit(this->validation_inode_bitmap_, inode_index, true);
        ++validated_inode_count;
    }

    uint64_t bitmap_inode_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    uint64_t bitmap_block_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    const Status bitmap_status =
        this->CompareValidationBitmaps(bitmap_inode_count, bitmap_block_count);
    if (bitmap_status != Status::Succeeded || bitmap_inode_count != validated_inode_count ||
        bitmap_inode_count != this->disk_superblock_.allocated_inode_count ||
        validated_data_block_count != this->disk_superblock_.allocated_data_block_count ||
        validated_metadata_block_count != this->disk_superblock_.allocated_metadata_block_count ||
        bitmap_block_count != validated_data_block_count + validated_metadata_block_count ||
        this->disk_superblock_.next_inode_generation <= maximum_inode_generation) {
        return bitmap_status == Status::Succeeded ? Status::Corrupt : bitmap_status;
    }
    uint64_t open_reference_count = OS_KERNEL_ROOTFS_EMPTY_VALUE;
    for (uint64_t inode_index = OS_KERNEL_ROOTFS_FIRST_INDEX;
         inode_index < this->disk_superblock_.inode_count; ++inode_index) {
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
        this->disk_superblock_.data_block_count - bitmap_block_count;
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
    this->last_validated_transaction_generation_ = this->disk_superblock_.transaction_generation;
    return Status::Succeeded;
}

}
