#include <os/kernel/fs/root_journal_v2.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DEVICE_SECTORS_PER_BLOCK =
    OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES / OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES;
constexpr uint8_t OS_KERNEL_ROOTFS_V5_JOURNAL_ZERO_BYTE = 0U;

void CopyBlock(uint8_t *const destination, const uint8_t *const source) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool BlockIsZero(const uint8_t *const block) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES;
         ++byte_index) {
        if (block[byte_index] != OS_KERNEL_ROOTFS_V5_JOURNAL_ZERO_BYTE) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint32_t ReadChecksum(const uint8_t *const block) noexcept {
    uint32_t checksum = 0U;
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(checksum); ++byte_index) {
        checksum |= static_cast<uint32_t>(
                        block[OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES + byte_index])
                    << (byte_index * 8ULL);
    }
    return checksum;
}

[[nodiscard]] RootJournalV2Status
ConvertFormatStatus(const RootJournalV2FormatStatus status) noexcept {
    if (status == RootJournalV2FormatStatus::Succeeded) {
        return RootJournalV2Status::Succeeded;
    }
    if (status == RootJournalV2FormatStatus::InvalidChecksum) {
        return RootJournalV2Status::Corrupt;
    }
    return RootJournalV2Status::Corrupt;
}

}

RootJournalV2Status RootJournalV2::Initialize(BlockDevice &device,
                                              const uint64_t file_system_start_lba,
                                              const uint64_t file_system_total_block_count,
                                              const uint64_t file_system_inode_count,
                                              const uint64_t journal_start_relative_block,
                                              const RootV5Uuid file_system_uuid) noexcept {
    if (this->initialized_) {
        return RootJournalV2Status::AlreadyInitialized;
    }
    if (file_system_total_block_count == 0ULL ||
        file_system_inode_count < OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER ||
        file_system_total_block_count >
            UINT64_MAX / OS_KERNEL_ROOTFS_V5_JOURNAL_DEVICE_SECTORS_PER_BLOCK ||
        file_system_start_lba >
            UINT64_MAX - file_system_total_block_count *
                             OS_KERNEL_ROOTFS_V5_JOURNAL_DEVICE_SECTORS_PER_BLOCK) {
        return RootJournalV2Status::InvalidArgument;
    }
    RootJournalV2Superblock planned{};
    if (PlanRootJournalV2Superblock(
            RootJournalV2FormatProfile{
                .file_system_total_block_count = file_system_total_block_count,
                .file_system_inode_count = file_system_inode_count,
                .journal_start_relative_block = journal_start_relative_block,
                .creation_time_nanoseconds = 0ULL,
                .file_system_uuid = file_system_uuid,
            },
            planned) != RootJournalV2FormatStatus::Succeeded) {
        return RootJournalV2Status::InvalidArgument;
    }
    this->device_ = &device;
    this->file_system_start_lba_ = file_system_start_lba;
    this->configured_total_block_count_ = file_system_total_block_count;
    this->configured_inode_count_ = file_system_inode_count;
    this->configured_journal_start_relative_block_ = journal_start_relative_block;
    this->configured_file_system_uuid_ = file_system_uuid;
    this->statistics_ = RootJournalV2Statistics{};
    this->superblock_ = RootJournalV2Superblock{};
    this->ResetActiveTransaction();
    this->initialized_ = true;
    this->formatted_ = false;
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::ReadFileSystemBlock(const uint64_t relative_block,
                                                       uint8_t *const block) noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return RootJournalV2Status::NotInitialized;
    }
    if (block == nullptr || relative_block >= this->configured_total_block_count_) {
        return RootJournalV2Status::InvalidArgument;
    }
    const uint64_t first_sector =
        this->file_system_start_lba_ +
        relative_block * OS_KERNEL_ROOTFS_V5_JOURNAL_DEVICE_SECTORS_PER_BLOCK;
    for (uint64_t sector_index = 0ULL;
         sector_index < OS_KERNEL_ROOTFS_V5_JOURNAL_DEVICE_SECTORS_PER_BLOCK; ++sector_index) {
        if (this->device_->ReadBlock(first_sector + sector_index,
                                     block + sector_index * OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES,
                                     OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES) !=
            BlockDeviceStatus::Succeeded) {
            return RootJournalV2Status::DeviceReadFailed;
        }
    }
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::WriteFileSystemBlock(const uint64_t relative_block,
                                                        const uint8_t *const block) noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return RootJournalV2Status::NotInitialized;
    }
    if (block == nullptr || relative_block >= this->configured_total_block_count_) {
        return RootJournalV2Status::InvalidArgument;
    }
    const uint64_t first_sector =
        this->file_system_start_lba_ +
        relative_block * OS_KERNEL_ROOTFS_V5_JOURNAL_DEVICE_SECTORS_PER_BLOCK;
    for (uint64_t sector_index = 0ULL;
         sector_index < OS_KERNEL_ROOTFS_V5_JOURNAL_DEVICE_SECTORS_PER_BLOCK; ++sector_index) {
        if (this->device_->WriteBlock(first_sector + sector_index,
                                      block + sector_index * OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES,
                                      OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES) !=
            BlockDeviceStatus::Succeeded) {
            return RootJournalV2Status::DeviceWriteFailed;
        }
    }
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::FlushDevice() noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return RootJournalV2Status::NotInitialized;
    }
    if (this->device_->Flush() != BlockDeviceStatus::Succeeded) {
        return RootJournalV2Status::DeviceFlushFailed;
    }
    ++this->statistics_.flush_count;
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::WriteSuperblock() noexcept {
    uint8_t block[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    const RootJournalV2FormatStatus status =
        EncodeRootJournalV2Superblock(this->superblock_, block, sizeof(block));
    return status == RootJournalV2FormatStatus::Succeeded
               ? this->WriteFileSystemBlock(this->superblock_.journal_start_relative_block, block)
               : ConvertFormatStatus(status);
}

RootJournalV2Status RootJournalV2::Format(const uint64_t creation_time_nanoseconds) noexcept {
    if (!this->initialized_) {
        return RootJournalV2Status::NotInitialized;
    }
    if (this->active_) {
        return RootJournalV2Status::AlreadyActive;
    }
    RootJournalV2Superblock planned{};
    const RootJournalV2FormatStatus plan_status = PlanRootJournalV2Superblock(
        RootJournalV2FormatProfile{
            .file_system_total_block_count = this->configured_total_block_count_,
            .file_system_inode_count = this->configured_inode_count_,
            .journal_start_relative_block = this->configured_journal_start_relative_block_,
            .creation_time_nanoseconds = creation_time_nanoseconds,
            .file_system_uuid = this->configured_file_system_uuid_,
        },
        planned);
    if (plan_status != RootJournalV2FormatStatus::Succeeded) {
        return RootJournalV2Status::InvalidArgument;
    }
    uint8_t zero_block[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    for (uint64_t journal_block = 1ULL; journal_block < planned.journal_block_count;
         ++journal_block) {
        const RootJournalV2Status status = this->WriteFileSystemBlock(
            planned.journal_start_relative_block + journal_block, zero_block);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
    }
    this->superblock_ = planned;
    RootJournalV2Status status = this->WriteSuperblock();
    if (status == RootJournalV2Status::Succeeded) {
        status = this->FlushDevice();
    }
    if (status == RootJournalV2Status::Succeeded) {
        this->formatted_ = true;
        ++this->statistics_.format_count;
    }
    return status;
}

RootJournalV2Status RootJournalV2::Open() noexcept {
    if (!this->initialized_) {
        return RootJournalV2Status::NotInitialized;
    }
    if (this->active_) {
        return RootJournalV2Status::AlreadyActive;
    }
    uint8_t block[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    RootJournalV2Status status =
        this->ReadFileSystemBlock(this->configured_journal_start_relative_block_, block);
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    RootJournalV2Superblock decoded{};
    const RootJournalV2FormatStatus decode_status =
        DecodeRootJournalV2Superblock(block, sizeof(block), decoded);
    if (decode_status == RootJournalV2FormatStatus::InvalidMagic && BlockIsZero(block)) {
        return RootJournalV2Status::NotFormatted;
    }
    if (decode_status != RootJournalV2FormatStatus::Succeeded) {
        if (decode_status == RootJournalV2FormatStatus::InvalidChecksum) {
            ++this->statistics_.checksum_failure_count;
        }
        return RootJournalV2Status::Corrupt;
    }
    if (decoded.file_system_total_block_count != this->configured_total_block_count_ ||
        decoded.file_system_inode_count != this->configured_inode_count_ ||
        decoded.journal_start_relative_block != this->configured_journal_start_relative_block_ ||
        decoded.file_system_uuid.low != this->configured_file_system_uuid_.low ||
        decoded.file_system_uuid.high != this->configured_file_system_uuid_.high) {
        return RootJournalV2Status::Corrupt;
    }
    this->superblock_ = decoded;
    this->formatted_ = true;
    this->ResetActiveTransaction();
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::ReadSlotState(const uint64_t slot_index, SlotState &state,
                                                 const bool validate_payloads) noexcept {
    state = SlotState{.sequence = 0ULL,
                      .slot_index = slot_index,
                      .committed = false,
                      .checkpointed = false,
                      .incomplete = false};
    if (!this->formatted_ || slot_index >= this->superblock_.slot_count) {
        return RootJournalV2Status::InvalidArgument;
    }
    const uint64_t slot_start = this->superblock_.journal_start_relative_block +
                                RootJournalV2SlotStartRelativeBlock(slot_index);
    uint8_t descriptor_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    RootJournalV2Status status = this->ReadFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_RELATIVE_BLOCK, descriptor_bytes);
    if (status != RootJournalV2Status::Succeeded || BlockIsZero(descriptor_bytes)) {
        return status;
    }
    uint8_t commit_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    status = this->ReadFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RELATIVE_BLOCK, commit_bytes);
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    RootJournalV2Commit commit{};
    const RootJournalV2FormatStatus commit_status =
        DecodeRootJournalV2Commit(this->superblock_, commit_bytes, sizeof(commit_bytes), commit);
    if (commit_status != RootJournalV2FormatStatus::Succeeded) {
        state.incomplete = true;
        return RootJournalV2Status::Succeeded;
    }
    RootJournalV2Descriptor descriptor{};
    const RootJournalV2FormatStatus descriptor_status = DecodeRootJournalV2Descriptor(
        this->superblock_, descriptor_bytes, sizeof(descriptor_bytes), descriptor);
    uint8_t revoke_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    status = this->ReadFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_RELATIVE_BLOCK, revoke_bytes);
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    RootJournalV2RevokeBlock revoke_block{};
    const RootJournalV2FormatStatus revoke_status = DecodeRootJournalV2RevokeBlock(
        this->superblock_, revoke_bytes, sizeof(revoke_bytes), revoke_block);
    if (descriptor_status != RootJournalV2FormatStatus::Succeeded ||
        revoke_status != RootJournalV2FormatStatus::Succeeded ||
        descriptor.sequence != commit.sequence || descriptor.slot_index != commit.slot_index ||
        descriptor.metadata_block_count != commit.metadata_block_count ||
        descriptor.ordered_data_block_count != commit.ordered_data_block_count ||
        descriptor.revoke_count != commit.revoke_count ||
        descriptor.transaction_generation != commit.transaction_generation ||
        revoke_block.sequence != commit.sequence || revoke_block.slot_index != commit.slot_index ||
        revoke_block.revoke_count != commit.revoke_count ||
        ReadChecksum(descriptor_bytes) != commit.descriptor_checksum ||
        ReadChecksum(revoke_bytes) != commit.revoke_checksum) {
        ++this->statistics_.checksum_failure_count;
        return RootJournalV2Status::Corrupt;
    }
    if (validate_payloads) {
        uint8_t payload[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
        for (uint64_t entry_index = 0ULL; entry_index < descriptor.metadata_block_count;
             ++entry_index) {
            status = this->ReadFileSystemBlock(
                slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK + entry_index,
                payload);
            if (status != RootJournalV2Status::Succeeded) {
                return status;
            }
            if (CalculateRootV5Crc32c(payload, sizeof(payload)) !=
                descriptor.tags[entry_index].payload_checksum) {
                ++this->statistics_.checksum_failure_count;
                return RootJournalV2Status::Corrupt;
            }
        }
    }
    uint8_t checkpoint_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    status = this->ReadFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_RELATIVE_BLOCK, checkpoint_bytes);
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    if (!BlockIsZero(checkpoint_bytes)) {
        RootJournalV2Checkpoint checkpoint{};
        if (DecodeRootJournalV2Checkpoint(this->superblock_, checkpoint_bytes,
                                          sizeof(checkpoint_bytes),
                                          checkpoint) != RootJournalV2FormatStatus::Succeeded ||
            checkpoint.sequence != commit.sequence || checkpoint.slot_index != commit.slot_index ||
            checkpoint.commit_checksum != ReadChecksum(commit_bytes)) {
            ++this->statistics_.checksum_failure_count;
            return RootJournalV2Status::Corrupt;
        }
        state.checkpointed = true;
    }
    state.sequence = commit.sequence;
    state.committed = true;
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::ClearSlot(const uint64_t slot_index) noexcept {
    if (!this->formatted_ || slot_index >= this->superblock_.slot_count) {
        return RootJournalV2Status::InvalidArgument;
    }
    const uint64_t slot_start = this->superblock_.journal_start_relative_block +
                                RootJournalV2SlotStartRelativeBlock(slot_index);
    uint8_t zero_block[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    const uint64_t clear_offsets[] = {
        OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_RELATIVE_BLOCK,
        OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RELATIVE_BLOCK,
        OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_RELATIVE_BLOCK,
    };
    for (const uint64_t clear_offset : clear_offsets) {
        const RootJournalV2Status status =
            this->WriteFileSystemBlock(slot_start + clear_offset, zero_block);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
    }
    return this->FlushDevice();
}

RootJournalV2Status RootJournalV2::FindFreeSlot(uint64_t &slot_index) noexcept {
    slot_index = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    for (uint64_t candidate = 0ULL; candidate < this->superblock_.slot_count; ++candidate) {
        SlotState state{};
        RootJournalV2Status status = this->ReadSlotState(candidate, state, false);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
        if (state.incomplete || state.checkpointed) {
            status = this->ClearSlot(candidate);
            if (status != RootJournalV2Status::Succeeded) {
                return status;
            }
            slot_index = candidate;
            return RootJournalV2Status::Succeeded;
        }
        if (!state.committed) {
            slot_index = candidate;
            return RootJournalV2Status::Succeeded;
        }
    }
    ++this->statistics_.capacity_rejection_count;
    return RootJournalV2Status::CapacityExhausted;
}

RootJournalV2Status RootJournalV2::Begin(const uint64_t reserved_metadata_block_count,
                                         const uint64_t reserved_ordered_data_block_count,
                                         const uint64_t reserved_revoke_count) noexcept {
    if (!this->initialized_) {
        return RootJournalV2Status::NotInitialized;
    }
    if (!this->formatted_) {
        return RootJournalV2Status::NotFormatted;
    }
    if (this->active_) {
        return RootJournalV2Status::AlreadyActive;
    }
    if (reserved_metadata_block_count > this->superblock_.maximum_metadata_block_count ||
        reserved_ordered_data_block_count > this->superblock_.maximum_ordered_data_block_count ||
        reserved_revoke_count > this->superblock_.maximum_revoke_count ||
        (reserved_metadata_block_count == 0ULL && reserved_revoke_count == 0ULL)) {
        ++this->statistics_.capacity_rejection_count;
        return RootJournalV2Status::CapacityExhausted;
    }
    if (this->superblock_.next_sequence == UINT64_MAX ||
        this->superblock_.journal_generation == UINT64_MAX) {
        return RootJournalV2Status::SequenceExhausted;
    }
    uint64_t slot_index = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    RootJournalV2Status status = this->FindFreeSlot(slot_index);
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    const uint64_t sequence = this->superblock_.next_sequence;
    ++this->superblock_.next_sequence;
    ++this->superblock_.journal_generation;
    status = this->WriteSuperblock();
    if (status == RootJournalV2Status::Succeeded) {
        status = this->FlushDevice();
    }
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    this->ResetActiveTransaction();
    this->active_sequence_ = sequence;
    this->active_slot_index_ = slot_index;
    this->reserved_metadata_block_count_ = reserved_metadata_block_count;
    this->reserved_ordered_data_block_count_ = reserved_ordered_data_block_count;
    this->reserved_revoke_count_ = reserved_revoke_count;
    this->active_ = true;
    ++this->statistics_.transaction_begin_count;
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::StageMetadata(const uint64_t target_relative_block,
                                                 const uint8_t *const block,
                                                 const uint64_t block_size_bytes) noexcept {
    if (!this->active_) {
        return this->initialized_ ? RootJournalV2Status::NotActive
                                  : RootJournalV2Status::NotInitialized;
    }
    if (block == nullptr || block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES ||
        !RootJournalV2TargetIsValid(this->superblock_, target_relative_block)) {
        return RootJournalV2Status::InvalidArgument;
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->ordered_data_block_count_;
         ++entry_index) {
        if (this->ordered_data_blocks_[entry_index].target_relative_block ==
            target_relative_block) {
            return RootJournalV2Status::InvalidArgument;
        }
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->metadata_block_count_; ++entry_index) {
        StagedBlock &entry = this->metadata_blocks_[entry_index];
        if (entry.target_relative_block == target_relative_block) {
            CopyBlock(entry.bytes, block);
            entry.checksum = CalculateRootV5Crc32c(block, block_size_bytes);
            return RootJournalV2Status::Succeeded;
        }
    }
    if (this->metadata_block_count_ >= this->reserved_metadata_block_count_) {
        ++this->statistics_.capacity_rejection_count;
        return RootJournalV2Status::CapacityExhausted;
    }
    for (uint64_t revoke_index = 0ULL; revoke_index < this->revoke_count_; ++revoke_index) {
        if (this->revoke_targets_[revoke_index] != target_relative_block) {
            continue;
        }
        for (uint64_t move_index = revoke_index + 1ULL; move_index < this->revoke_count_;
             ++move_index) {
            this->revoke_targets_[move_index - 1ULL] = this->revoke_targets_[move_index];
        }
        --this->revoke_count_;
        this->revoke_targets_[this->revoke_count_] = 0ULL;
        break;
    }
    StagedBlock &entry = this->metadata_blocks_[this->metadata_block_count_];
    CopyBlock(entry.bytes, block);
    entry.target_relative_block = target_relative_block;
    entry.checksum = CalculateRootV5Crc32c(block, block_size_bytes);
    entry.occupied = true;
    ++this->metadata_block_count_;
    ++this->statistics_.metadata_stage_count;
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::StageOrderedData(const uint64_t target_relative_block,
                                                    const uint8_t *const block,
                                                    const uint64_t block_size_bytes) noexcept {
    if (!this->active_) {
        return this->initialized_ ? RootJournalV2Status::NotActive
                                  : RootJournalV2Status::NotInitialized;
    }
    if (block == nullptr || block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES ||
        !RootJournalV2TargetIsValid(this->superblock_, target_relative_block)) {
        return RootJournalV2Status::InvalidArgument;
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->metadata_block_count_; ++entry_index) {
        if (this->metadata_blocks_[entry_index].target_relative_block == target_relative_block) {
            return RootJournalV2Status::InvalidArgument;
        }
    }
    for (uint64_t revoke_index = 0ULL; revoke_index < this->revoke_count_; ++revoke_index) {
        if (this->revoke_targets_[revoke_index] == target_relative_block) {
            return RootJournalV2Status::InvalidArgument;
        }
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->ordered_data_block_count_;
         ++entry_index) {
        StagedBlock &entry = this->ordered_data_blocks_[entry_index];
        if (entry.target_relative_block == target_relative_block) {
            CopyBlock(entry.bytes, block);
            entry.checksum = CalculateRootV5Crc32c(block, block_size_bytes);
            return RootJournalV2Status::Succeeded;
        }
    }
    if (this->ordered_data_block_count_ >= this->reserved_ordered_data_block_count_) {
        ++this->statistics_.capacity_rejection_count;
        return RootJournalV2Status::CapacityExhausted;
    }
    StagedBlock &entry = this->ordered_data_blocks_[this->ordered_data_block_count_];
    CopyBlock(entry.bytes, block);
    entry.target_relative_block = target_relative_block;
    entry.checksum = CalculateRootV5Crc32c(block, block_size_bytes);
    entry.occupied = true;
    ++this->ordered_data_block_count_;
    ++this->statistics_.ordered_data_stage_count;
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::Revoke(const uint64_t target_relative_block) noexcept {
    if (!this->active_) {
        return this->initialized_ ? RootJournalV2Status::NotActive
                                  : RootJournalV2Status::NotInitialized;
    }
    if (!RootJournalV2TargetIsValid(this->superblock_, target_relative_block)) {
        return RootJournalV2Status::InvalidArgument;
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->ordered_data_block_count_;
         ++entry_index) {
        if (this->ordered_data_blocks_[entry_index].target_relative_block ==
            target_relative_block) {
            return RootJournalV2Status::InvalidArgument;
        }
    }
    for (uint64_t revoke_index = 0ULL; revoke_index < this->revoke_count_; ++revoke_index) {
        if (this->revoke_targets_[revoke_index] == target_relative_block) {
            return RootJournalV2Status::Succeeded;
        }
    }
    if (this->revoke_count_ >= this->reserved_revoke_count_) {
        ++this->statistics_.capacity_rejection_count;
        return RootJournalV2Status::CapacityExhausted;
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->metadata_block_count_; ++entry_index) {
        if (this->metadata_blocks_[entry_index].target_relative_block != target_relative_block) {
            continue;
        }
        for (uint64_t move_index = entry_index + 1ULL; move_index < this->metadata_block_count_;
             ++move_index) {
            this->metadata_blocks_[move_index - 1ULL] = this->metadata_blocks_[move_index];
        }
        --this->metadata_block_count_;
        this->metadata_blocks_[this->metadata_block_count_] = StagedBlock{};
        break;
    }
    this->revoke_targets_[this->revoke_count_] = target_relative_block;
    ++this->revoke_count_;
    ++this->statistics_.revoke_count;
    return RootJournalV2Status::Succeeded;
}

bool RootJournalV2::TryReadStagedMetadata(const uint64_t target_relative_block,
                                          uint8_t *const block,
                                          const uint64_t block_size_bytes) const noexcept {
    if (!this->active_ || block == nullptr ||
        block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return false;
    }
    for (uint64_t entry_index = 0ULL; entry_index < this->metadata_block_count_; ++entry_index) {
        const StagedBlock &entry = this->metadata_blocks_[entry_index];
        if (entry.target_relative_block == target_relative_block) {
            CopyBlock(block, entry.bytes);
            return true;
        }
    }
    return false;
}

RootJournalV2Status RootJournalV2::WritePreparedTransaction(uint32_t &descriptor_checksum,
                                                            uint32_t &revoke_checksum) noexcept {
    descriptor_checksum = 0U;
    revoke_checksum = 0U;
    RootJournalV2Descriptor descriptor{
        .sequence = this->active_sequence_,
        .slot_index = this->active_slot_index_,
        .metadata_block_count = this->metadata_block_count_,
        .ordered_data_block_count = this->ordered_data_block_count_,
        .revoke_count = this->revoke_count_,
        .transaction_generation = this->superblock_.journal_generation,
        .file_system_uuid = this->superblock_.file_system_uuid,
        .tags = {},
    };
    for (uint64_t entry_index = 0ULL; entry_index < this->metadata_block_count_; ++entry_index) {
        descriptor.tags[entry_index] = RootJournalV2Tag{
            .target_relative_block = this->metadata_blocks_[entry_index].target_relative_block,
            .payload_index = entry_index,
            .payload_checksum = this->metadata_blocks_[entry_index].checksum,
            .flags = static_cast<uint32_t>(OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_TAG_FLAGS),
        };
    }
    RootJournalV2RevokeBlock revoke_block{
        .sequence = this->active_sequence_,
        .slot_index = this->active_slot_index_,
        .revoke_count = this->revoke_count_,
        .file_system_uuid = this->superblock_.file_system_uuid,
        .targets = {},
    };
    for (uint64_t revoke_index = 0ULL; revoke_index < this->revoke_count_; ++revoke_index) {
        revoke_block.targets[revoke_index] = this->revoke_targets_[revoke_index];
    }
    uint8_t descriptor_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t revoke_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    if (EncodeRootJournalV2Descriptor(this->superblock_, descriptor, descriptor_bytes,
                                      sizeof(descriptor_bytes)) !=
            RootJournalV2FormatStatus::Succeeded ||
        EncodeRootJournalV2RevokeBlock(this->superblock_, revoke_block, revoke_bytes,
                                       sizeof(revoke_bytes)) !=
            RootJournalV2FormatStatus::Succeeded) {
        return RootJournalV2Status::Corrupt;
    }
    const uint64_t slot_start = this->superblock_.journal_start_relative_block +
                                RootJournalV2SlotStartRelativeBlock(this->active_slot_index_);
    RootJournalV2Status status = this->WriteFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_RELATIVE_BLOCK, descriptor_bytes);
    if (status == RootJournalV2Status::Succeeded) {
        status = this->WriteFileSystemBlock(
            slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_RELATIVE_BLOCK, revoke_bytes);
    }
    for (uint64_t entry_index = 0ULL;
         status == RootJournalV2Status::Succeeded && entry_index < this->metadata_block_count_;
         ++entry_index) {
        status = this->WriteFileSystemBlock(
            slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK + entry_index,
            this->metadata_blocks_[entry_index].bytes);
    }
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    descriptor_checksum = ReadChecksum(descriptor_bytes);
    revoke_checksum = ReadChecksum(revoke_bytes);
    return this->FlushDevice();
}

RootJournalV2Status RootJournalV2::WriteOrderedData() noexcept {
    for (uint64_t entry_index = 0ULL; entry_index < this->ordered_data_block_count_;
         ++entry_index) {
        const RootJournalV2Status status = this->WriteFileSystemBlock(
            this->ordered_data_blocks_[entry_index].target_relative_block,
            this->ordered_data_blocks_[entry_index].bytes);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
    }
    return this->ordered_data_block_count_ == 0ULL ? RootJournalV2Status::Succeeded
                                                   : this->FlushDevice();
}

RootJournalV2Status RootJournalV2::WriteCommitRecord(const uint64_t commit_time_nanoseconds,
                                                     const uint32_t descriptor_checksum,
                                                     const uint32_t revoke_checksum) noexcept {
    RootJournalV2Commit commit{
        .sequence = this->active_sequence_,
        .slot_index = this->active_slot_index_,
        .metadata_block_count = this->metadata_block_count_,
        .ordered_data_block_count = this->ordered_data_block_count_,
        .revoke_count = this->revoke_count_,
        .transaction_generation = this->superblock_.journal_generation,
        .commit_time_nanoseconds = commit_time_nanoseconds,
        .descriptor_checksum = descriptor_checksum,
        .revoke_checksum = revoke_checksum,
        .file_system_uuid = this->superblock_.file_system_uuid,
    };
    uint8_t commit_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    if (EncodeRootJournalV2Commit(this->superblock_, commit, commit_bytes, sizeof(commit_bytes)) !=
        RootJournalV2FormatStatus::Succeeded) {
        return RootJournalV2Status::Corrupt;
    }
    const uint64_t slot_start = this->superblock_.journal_start_relative_block +
                                RootJournalV2SlotStartRelativeBlock(this->active_slot_index_);
    const RootJournalV2Status status = this->WriteFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RELATIVE_BLOCK, commit_bytes);
    return status == RootJournalV2Status::Succeeded ? this->FlushDevice() : status;
}

RootJournalV2Status RootJournalV2::Commit(const uint64_t commit_time_nanoseconds) noexcept {
    if (!this->active_) {
        return this->initialized_ ? RootJournalV2Status::NotActive
                                  : RootJournalV2Status::NotInitialized;
    }
    if (this->metadata_block_count_ == 0ULL && this->revoke_count_ == 0ULL) {
        return RootJournalV2Status::InvalidArgument;
    }
    uint32_t descriptor_checksum = 0U;
    uint32_t revoke_checksum = 0U;
    RootJournalV2Status status =
        this->WritePreparedTransaction(descriptor_checksum, revoke_checksum);
    if (status == RootJournalV2Status::Succeeded) {
        status = this->WriteOrderedData();
    }
    if (status == RootJournalV2Status::Succeeded) {
        status =
            this->WriteCommitRecord(commit_time_nanoseconds, descriptor_checksum, revoke_checksum);
    }
    if (status == RootJournalV2Status::Succeeded) {
        ++this->statistics_.transaction_commit_count;
        this->ResetActiveTransaction();
    }
    return status;
}

RootJournalV2Status RootJournalV2::LoadCommittedSlot(const uint64_t slot_index,
                                                     RootJournalV2Descriptor &descriptor,
                                                     RootJournalV2RevokeBlock &revoke_block,
                                                     RootJournalV2Commit &commit) noexcept {
    const uint64_t slot_start = this->superblock_.journal_start_relative_block +
                                RootJournalV2SlotStartRelativeBlock(slot_index);
    uint8_t descriptor_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t revoke_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t commit_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    RootJournalV2Status status = this->ReadFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_RELATIVE_BLOCK, descriptor_bytes);
    if (status == RootJournalV2Status::Succeeded) {
        status = this->ReadFileSystemBlock(
            slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_RELATIVE_BLOCK, revoke_bytes);
    }
    if (status == RootJournalV2Status::Succeeded) {
        status = this->ReadFileSystemBlock(
            slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RELATIVE_BLOCK, commit_bytes);
    }
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    if (DecodeRootJournalV2Descriptor(this->superblock_, descriptor_bytes, sizeof(descriptor_bytes),
                                      descriptor) != RootJournalV2FormatStatus::Succeeded ||
        DecodeRootJournalV2RevokeBlock(this->superblock_, revoke_bytes, sizeof(revoke_bytes),
                                       revoke_block) != RootJournalV2FormatStatus::Succeeded ||
        DecodeRootJournalV2Commit(this->superblock_, commit_bytes, sizeof(commit_bytes), commit) !=
            RootJournalV2FormatStatus::Succeeded ||
        descriptor.sequence != commit.sequence || descriptor.slot_index != commit.slot_index ||
        revoke_block.sequence != commit.sequence || revoke_block.slot_index != commit.slot_index) {
        return RootJournalV2Status::Corrupt;
    }
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::TargetRevokedAfter(const uint64_t target_relative_block,
                                                      const uint64_t sequence,
                                                      const SlotState *const states,
                                                      const uint64_t state_count,
                                                      bool &revoked) noexcept {
    revoked = false;
    if (states == nullptr && state_count != 0ULL) {
        return RootJournalV2Status::InvalidArgument;
    }
    for (uint64_t state_index = 0ULL; state_index < state_count; ++state_index) {
        const SlotState &state = states[state_index];
        if (!state.committed || state.checkpointed || state.sequence <= sequence) {
            continue;
        }
        RootJournalV2Descriptor descriptor{};
        RootJournalV2RevokeBlock revoke_block{};
        RootJournalV2Commit commit{};
        const RootJournalV2Status status =
            this->LoadCommittedSlot(state.slot_index, descriptor, revoke_block, commit);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
        for (uint64_t revoke_index = 0ULL; revoke_index < revoke_block.revoke_count;
             ++revoke_index) {
            if (revoke_block.targets[revoke_index] == target_relative_block) {
                revoked = true;
                return RootJournalV2Status::Succeeded;
            }
        }
    }
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::WriteCheckpointRecord(const uint64_t slot_index,
                                                         const uint64_t sequence,
                                                         const uint32_t commit_checksum) noexcept {
    RootJournalV2Checkpoint checkpoint{
        .sequence = sequence,
        .slot_index = slot_index,
        .checkpoint_generation = this->superblock_.journal_generation,
        .commit_checksum = commit_checksum,
        .file_system_uuid = this->superblock_.file_system_uuid,
    };
    uint8_t checkpoint_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    if (EncodeRootJournalV2Checkpoint(this->superblock_, checkpoint, checkpoint_bytes,
                                      sizeof(checkpoint_bytes)) !=
        RootJournalV2FormatStatus::Succeeded) {
        return RootJournalV2Status::Corrupt;
    }
    const uint64_t slot_start = this->superblock_.journal_start_relative_block +
                                RootJournalV2SlotStartRelativeBlock(slot_index);
    const RootJournalV2Status status = this->WriteFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_RELATIVE_BLOCK, checkpoint_bytes);
    return status == RootJournalV2Status::Succeeded ? this->FlushDevice() : status;
}

RootJournalV2Status RootJournalV2::CheckpointSlot(const uint64_t slot_index, const bool replay,
                                                  const SlotState *const states,
                                                  const uint64_t state_count) noexcept {
    RootJournalV2Descriptor descriptor{};
    RootJournalV2RevokeBlock revoke_block{};
    RootJournalV2Commit commit{};
    RootJournalV2Status status =
        this->LoadCommittedSlot(slot_index, descriptor, revoke_block, commit);
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    const uint64_t slot_start = this->superblock_.journal_start_relative_block +
                                RootJournalV2SlotStartRelativeBlock(slot_index);
    uint8_t payload[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    for (uint64_t entry_index = 0ULL; entry_index < descriptor.metadata_block_count;
         ++entry_index) {
        bool revoked = false;
        status = this->TargetRevokedAfter(descriptor.tags[entry_index].target_relative_block,
                                          descriptor.sequence, states, state_count, revoked);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
        if (revoked) {
            ++this->statistics_.revoked_replay_skip_count;
            continue;
        }
        status = this->ReadFileSystemBlock(
            slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK + entry_index,
            payload);
        if (status != RootJournalV2Status::Succeeded ||
            CalculateRootV5Crc32c(payload, sizeof(payload)) !=
                descriptor.tags[entry_index].payload_checksum) {
            if (status == RootJournalV2Status::Succeeded) {
                ++this->statistics_.checksum_failure_count;
                status = RootJournalV2Status::Corrupt;
            }
            return status;
        }
        status =
            this->WriteFileSystemBlock(descriptor.tags[entry_index].target_relative_block, payload);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
        if (replay) {
            ++this->statistics_.replay_block_count;
        } else {
            ++this->statistics_.checkpoint_block_count;
        }
    }
    status = this->FlushDevice();
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    uint8_t commit_bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    status = this->ReadFileSystemBlock(
        slot_start + OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RELATIVE_BLOCK, commit_bytes);
    if (status == RootJournalV2Status::Succeeded) {
        status = this->WriteCheckpointRecord(slot_index, descriptor.sequence,
                                             ReadChecksum(commit_bytes));
    }
    if (status != RootJournalV2Status::Succeeded) {
        return status;
    }
    if (descriptor.sequence > this->superblock_.last_checkpoint_sequence) {
        if (this->superblock_.journal_generation == UINT64_MAX) {
            return RootJournalV2Status::SequenceExhausted;
        }
        this->superblock_.last_checkpoint_sequence = descriptor.sequence;
        ++this->superblock_.journal_generation;
        status = this->WriteSuperblock();
        if (status == RootJournalV2Status::Succeeded) {
            status = this->FlushDevice();
        }
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
    }
    status = this->ClearSlot(slot_index);
    if (status == RootJournalV2Status::Succeeded) {
        if (replay) {
            ++this->statistics_.replay_transaction_count;
        } else {
            ++this->statistics_.checkpoint_transaction_count;
        }
    }
    return status;
}

RootJournalV2Status RootJournalV2::CheckpointOldest() noexcept {
    if (!this->initialized_) {
        return RootJournalV2Status::NotInitialized;
    }
    if (!this->formatted_) {
        return RootJournalV2Status::NotFormatted;
    }
    if (this->active_) {
        return RootJournalV2Status::AlreadyActive;
    }
    SlotState states[OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT]{};
    uint64_t state_count = 0ULL;
    uint64_t oldest_index = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    uint64_t oldest_sequence = UINT64_MAX;
    for (uint64_t slot_index = 0ULL; slot_index < this->superblock_.slot_count; ++slot_index) {
        SlotState state{};
        RootJournalV2Status status = this->ReadSlotState(slot_index, state, true);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
        if (state.incomplete || state.checkpointed) {
            status = this->ClearSlot(slot_index);
            if (status != RootJournalV2Status::Succeeded) {
                return status;
            }
            continue;
        }
        if (!state.committed) {
            continue;
        }
        states[state_count] = state;
        ++state_count;
        if (state.sequence < oldest_sequence) {
            oldest_sequence = state.sequence;
            oldest_index = state_count - 1ULL;
        }
    }
    return oldest_index == OS_KERNEL_ROOTFS_V5_NO_BLOCK
               ? RootJournalV2Status::Succeeded
               : this->CheckpointSlot(states[oldest_index].slot_index, false, states, state_count);
}

RootJournalV2Status RootJournalV2::Recover(RootJournalV2RecoveryResult &result) noexcept {
    result = RootJournalV2RecoveryResult::Clean;
    if (!this->initialized_) {
        return RootJournalV2Status::NotInitialized;
    }
    if (this->active_) {
        return RootJournalV2Status::AlreadyActive;
    }
    if (!this->formatted_) {
        const RootJournalV2Status open_status = this->Open();
        if (open_status != RootJournalV2Status::Succeeded) {
            return open_status;
        }
    }
    SlotState states[OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT]{};
    uint64_t state_count = 0ULL;
    bool discarded = false;
    for (uint64_t slot_index = 0ULL; slot_index < this->superblock_.slot_count; ++slot_index) {
        SlotState state{};
        RootJournalV2Status status = this->ReadSlotState(slot_index, state, true);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
        if (state.incomplete) {
            status = this->ClearSlot(slot_index);
            if (status != RootJournalV2Status::Succeeded) {
                return status;
            }
            ++this->statistics_.discarded_incomplete_count;
            discarded = true;
            continue;
        }
        if (state.checkpointed) {
            status = this->ClearSlot(slot_index);
            if (status != RootJournalV2Status::Succeeded) {
                return status;
            }
            continue;
        }
        if (!state.committed) {
            continue;
        }
        for (uint64_t prior_index = 0ULL; prior_index < state_count; ++prior_index) {
            if (states[prior_index].sequence == state.sequence) {
                return RootJournalV2Status::Corrupt;
            }
        }
        states[state_count] = state;
        ++state_count;
    }
    for (uint64_t left = 0ULL; left < state_count; ++left) {
        for (uint64_t right = left + 1ULL; right < state_count; ++right) {
            if (states[right].sequence < states[left].sequence) {
                const SlotState temporary = states[left];
                states[left] = states[right];
                states[right] = temporary;
            }
        }
    }
    for (uint64_t state_index = 0ULL; state_index < state_count; ++state_index) {
        const RootJournalV2Status status =
            this->CheckpointSlot(states[state_index].slot_index, true, states, state_count);
        if (status != RootJournalV2Status::Succeeded) {
            return status;
        }
    }
    if (state_count != 0ULL) {
        result = RootJournalV2RecoveryResult::Replayed;
    } else if (discarded) {
        result = RootJournalV2RecoveryResult::DiscardedIncomplete;
    }
    this->ResetActiveTransaction();
    return RootJournalV2Status::Succeeded;
}

RootJournalV2Status RootJournalV2::Abort() noexcept {
    if (!this->initialized_) {
        return RootJournalV2Status::NotInitialized;
    }
    if (!this->active_) {
        return RootJournalV2Status::NotActive;
    }
    ++this->statistics_.transaction_abort_count;
    this->ResetActiveTransaction();
    return RootJournalV2Status::Succeeded;
}

void RootJournalV2::ResetActiveTransaction() noexcept {
    for (uint64_t entry_index = 0ULL;
         entry_index < OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT; ++entry_index) {
        this->metadata_blocks_[entry_index] = StagedBlock{};
    }
    for (uint64_t entry_index = 0ULL;
         entry_index < OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_ORDERED_DATA_BLOCK_COUNT;
         ++entry_index) {
        this->ordered_data_blocks_[entry_index] = StagedBlock{};
    }
    for (uint64_t revoke_index = 0ULL;
         revoke_index < OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT; ++revoke_index) {
        this->revoke_targets_[revoke_index] = 0ULL;
    }
    this->active_sequence_ = OS_KERNEL_ROOTFS_V5_JOURNAL_EMPTY_VALUE;
    this->active_slot_index_ = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    this->reserved_metadata_block_count_ = OS_KERNEL_ROOTFS_V5_JOURNAL_EMPTY_VALUE;
    this->reserved_ordered_data_block_count_ = OS_KERNEL_ROOTFS_V5_JOURNAL_EMPTY_VALUE;
    this->reserved_revoke_count_ = OS_KERNEL_ROOTFS_V5_JOURNAL_EMPTY_VALUE;
    this->metadata_block_count_ = OS_KERNEL_ROOTFS_V5_JOURNAL_EMPTY_VALUE;
    this->ordered_data_block_count_ = OS_KERNEL_ROOTFS_V5_JOURNAL_EMPTY_VALUE;
    this->revoke_count_ = OS_KERNEL_ROOTFS_V5_JOURNAL_EMPTY_VALUE;
    this->active_ = false;
}

uint64_t RootJournalV2::PendingCommittedTransactionCount() noexcept {
    if (!this->formatted_ || this->active_) {
        return 0ULL;
    }
    uint64_t count = 0ULL;
    for (uint64_t slot_index = 0ULL; slot_index < this->superblock_.slot_count; ++slot_index) {
        SlotState state{};
        if (this->ReadSlotState(slot_index, state, false) != RootJournalV2Status::Succeeded) {
            return 0ULL;
        }
        if (state.committed && !state.checkpointed) {
            ++count;
        }
    }
    return count;
}

bool RootJournalV2::IsActive() const noexcept { return this->active_; }

uint64_t RootJournalV2::ActiveSequence() const noexcept { return this->active_sequence_; }

const RootJournalV2Superblock &RootJournalV2::Superblock() const noexcept {
    return this->superblock_;
}

RootJournalV2Statistics RootJournalV2::Statistics() const noexcept { return this->statistics_; }

}
