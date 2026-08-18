#include "os/kernel/fs/root_journal.hpp"

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_HEADER_RELATIVE_BLOCK = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_START_RELATIVE_BLOCK = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK =
    OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_START_RELATIVE_BLOCK +
    OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_BLOCK_COUNT;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_COMMIT_RELATIVE_BLOCK =
    OS_KERNEL_ROOTFS_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK +
    OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_MAGIC = 0x4F534A4E4C763031ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_COMMIT_MAGIC = 0x4F53434D54763031ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_FORMAT_VERSION = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_HEADER_MAGIC_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_HEADER_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_HEADER_SEQUENCE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_HEADER_ENTRY_COUNT_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_HEADER_DESCRIPTOR_COUNT_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES =
    OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES - 4ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_TARGET_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_PAYLOAD_CHECKSUM_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_RESERVED_OFFSET_BYTES = 12ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_COMMIT_MAGIC_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_COMMIT_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_COMMIT_SEQUENCE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_COMMIT_ENTRY_COUNT_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_COMMIT_HEADER_CHECKSUM_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_COMMIT_CHECKSUM_OFFSET_BYTES =
    OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES - 4ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_UINT32_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_UINT64_SIZE_BYTES = 8ULL;
constexpr uint8_t OS_KERNEL_ROOTFS_JOURNAL_ZERO_BYTE = 0U;

static_assert(OS_KERNEL_ROOTFS_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK +
                  OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT ==
              OS_KERNEL_ROOTFS_JOURNAL_COMMIT_RELATIVE_BLOCK);
static_assert(OS_KERNEL_ROOTFS_JOURNAL_COMMIT_RELATIVE_BLOCK <
              OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT);

void CopyBlock(uint8_t *const destination, const uint8_t *const source) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES; ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool BlockIsZero(const uint8_t *const block) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES; ++byte_index) {
        if (block[byte_index] != OS_KERNEL_ROOTFS_JOURNAL_ZERO_BYTE) {
            return false;
        }
    }
    return true;
}

void StoreUint32(uint8_t *const bytes, const uint64_t offset_bytes, const uint32_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_JOURNAL_UINT32_SIZE_BYTES; ++byte_index) {
        bytes[offset_bytes + byte_index] =
            static_cast<uint8_t>(value >> static_cast<uint32_t>(byte_index * 8ULL));
    }
}

void StoreUint64(uint8_t *const bytes, const uint64_t offset_bytes, const uint64_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_JOURNAL_UINT64_SIZE_BYTES; ++byte_index) {
        bytes[offset_bytes + byte_index] =
            static_cast<uint8_t>(value >> static_cast<uint64_t>(byte_index * 8ULL));
    }
}

[[nodiscard]] uint32_t LoadUint32(const uint8_t *const bytes,
                                  const uint64_t offset_bytes) noexcept {
    uint32_t value = 0U;
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_JOURNAL_UINT32_SIZE_BYTES; ++byte_index) {
        value |= static_cast<uint32_t>(bytes[offset_bytes + byte_index])
                 << static_cast<uint32_t>(byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] uint64_t LoadUint64(const uint8_t *const bytes,
                                  const uint64_t offset_bytes) noexcept {
    uint64_t value = OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE;
    for (uint64_t byte_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         byte_index < OS_KERNEL_ROOTFS_JOURNAL_UINT64_SIZE_BYTES; ++byte_index) {
        value |= static_cast<uint64_t>(bytes[offset_bytes + byte_index])
                 << static_cast<uint64_t>(byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] bool TargetBlockIsValid(const uint64_t relative_block,
                                      const uint64_t total_block_count) noexcept {
    const bool inside_journal = relative_block >= OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK &&
                                relative_block < OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                                     OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT;
    return relative_block < total_block_count && !inside_journal;
}

}

RootJournalStatus RootJournal::Initialize(FileSystemBlockDevice &device,
                                          const uint64_t rootfs_start_lba,
                                          const uint64_t rootfs_total_block_count) noexcept {
    if (this->initialized_) {
        return RootJournalStatus::AlreadyInitialized;
    }
    if (rootfs_total_block_count <=
            OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK + OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT ||
        rootfs_total_block_count > OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT ||
        rootfs_start_lba > UINT64_MAX - rootfs_total_block_count) {
        return RootJournalStatus::InvalidArgument;
    }
    this->device_ = &device;
    this->rootfs_start_lba_ = rootfs_start_lba;
    this->rootfs_total_block_count_ = rootfs_total_block_count;
    this->statistics_ = RootJournalStatistics{};
    this->ResetTransaction();
    this->initialized_ = true;
    return RootJournalStatus::Succeeded;
}

RootJournalStatus RootJournal::ReadDeviceBlock(const uint64_t rootfs_relative_block,
                                               uint8_t *const block) noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return RootJournalStatus::NotInitialized;
    }
    if (block == nullptr || rootfs_relative_block >= this->rootfs_total_block_count_) {
        return RootJournalStatus::InvalidArgument;
    }
    return this->device_->ReadBlock(this->rootfs_start_lba_ + rootfs_relative_block, block,
                                    OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) ==
                   FileSystemBlockDeviceStatus::Succeeded
               ? RootJournalStatus::Succeeded
               : RootJournalStatus::DeviceReadFailed;
}

RootJournalStatus RootJournal::WriteDeviceBlock(const uint64_t rootfs_relative_block,
                                                const uint8_t *const block) noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return RootJournalStatus::NotInitialized;
    }
    if (block == nullptr || rootfs_relative_block >= this->rootfs_total_block_count_) {
        return RootJournalStatus::InvalidArgument;
    }
    return this->device_->WriteBlock(this->rootfs_start_lba_ + rootfs_relative_block, block,
                                     OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) ==
                   FileSystemBlockDeviceStatus::Succeeded
               ? RootJournalStatus::Succeeded
               : RootJournalStatus::DeviceWriteFailed;
}

RootJournalStatus RootJournal::FlushDevice() noexcept {
    if (!this->initialized_ || this->device_ == nullptr) {
        return RootJournalStatus::NotInitialized;
    }
    if (this->device_->Flush() != FileSystemBlockDeviceStatus::Succeeded) {
        return RootJournalStatus::DeviceFlushFailed;
    }
    ++this->statistics_.flush_count;
    return RootJournalStatus::Succeeded;
}

RootJournalStatus RootJournal::Begin(const uint64_t sequence,
                                     const uint64_t reserved_credit_count) noexcept {
    if (!this->initialized_) {
        return RootJournalStatus::NotInitialized;
    }
    if (this->active_) {
        return RootJournalStatus::AlreadyActive;
    }
    if (sequence == OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE || sequence == UINT64_MAX) {
        return RootJournalStatus::SequenceExhausted;
    }
    if (reserved_credit_count == OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE ||
        reserved_credit_count > OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT) {
        ++this->statistics_.credit_rejection_count;
        return RootJournalStatus::CreditsExhausted;
    }
    this->ResetTransaction();
    this->sequence_ = sequence;
    this->reserved_credit_count_ = reserved_credit_count;
    this->active_ = true;
    ++this->statistics_.transaction_begin_count;
    return RootJournalStatus::Succeeded;
}

RootJournalStatus RootJournal::Stage(const uint64_t target_relative_block,
                                     const uint8_t *const block,
                                     const uint64_t block_size_bytes) noexcept {
    if (!this->initialized_) {
        return RootJournalStatus::NotInitialized;
    }
    if (!this->active_) {
        return RootJournalStatus::NotActive;
    }
    if (!TargetBlockIsValid(target_relative_block, this->rootfs_total_block_count_) ||
        block == nullptr || block_size_bytes != OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) {
        return RootJournalStatus::InvalidArgument;
    }
    for (uint64_t entry_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         entry_index < this->staged_block_count_; ++entry_index) {
        StagedBlock &entry = this->staged_blocks_[entry_index];
        if (entry.occupied && entry.target_relative_block == target_relative_block) {
            CopyBlock(entry.bytes, block);
            entry.checksum = CalculateRootCrc32(entry.bytes, sizeof(entry.bytes));
            return RootJournalStatus::Succeeded;
        }
    }
    if (this->staged_block_count_ >= this->reserved_credit_count_) {
        ++this->statistics_.credit_rejection_count;
        return RootJournalStatus::CreditsExhausted;
    }
    StagedBlock &entry = this->staged_blocks_[this->staged_block_count_];
    CopyBlock(entry.bytes, block);
    entry.target_relative_block = target_relative_block;
    entry.checksum = CalculateRootCrc32(entry.bytes, sizeof(entry.bytes));
    entry.occupied = true;
    ++this->staged_block_count_;
    ++this->statistics_.staged_block_count;
    return RootJournalStatus::Succeeded;
}

bool RootJournal::TryReadStaged(const uint64_t target_relative_block, uint8_t *const block,
                                const uint64_t block_size_bytes) const noexcept {
    if (!this->initialized_ || !this->active_ || block == nullptr ||
        block_size_bytes != OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) {
        return false;
    }
    for (uint64_t entry_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         entry_index < this->staged_block_count_; ++entry_index) {
        const StagedBlock &entry = this->staged_blocks_[entry_index];
        if (entry.occupied && entry.target_relative_block == target_relative_block) {
            CopyBlock(block, entry.bytes);
            return true;
        }
    }
    return false;
}

RootJournalStatus RootJournal::WritePreparedTransaction() noexcept {
    uint8_t header[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    StoreUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_MAGIC_OFFSET_BYTES,
                OS_KERNEL_ROOTFS_JOURNAL_MAGIC);
    StoreUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_VERSION_OFFSET_BYTES,
                OS_KERNEL_ROOTFS_JOURNAL_FORMAT_VERSION);
    StoreUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_SEQUENCE_OFFSET_BYTES, this->sequence_);
    StoreUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_ENTRY_COUNT_OFFSET_BYTES,
                this->staged_block_count_);
    StoreUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_DESCRIPTOR_COUNT_OFFSET_BYTES,
                OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_BLOCK_COUNT);
    StoreUint32(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES,
                CalculateRootCrc32(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES));
    RootJournalStatus status =
        this->WriteDeviceBlock(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                   OS_KERNEL_ROOTFS_JOURNAL_HEADER_RELATIVE_BLOCK,
                               header);
    if (status != RootJournalStatus::Succeeded) {
        return status;
    }

    for (uint64_t descriptor_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         descriptor_index < OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_BLOCK_COUNT; ++descriptor_index) {
        uint8_t descriptor[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
        for (uint64_t slot_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
             slot_index < OS_KERNEL_ROOTFS_JOURNAL_ENTRIES_PER_DESCRIPTOR_BLOCK; ++slot_index) {
            const uint64_t entry_index =
                descriptor_index * OS_KERNEL_ROOTFS_JOURNAL_ENTRIES_PER_DESCRIPTOR_BLOCK +
                slot_index;
            if (entry_index >= this->staged_block_count_) {
                break;
            }
            const uint64_t entry_offset_bytes =
                slot_index * OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_ENTRY_SIZE_BYTES;
            const StagedBlock &entry = this->staged_blocks_[entry_index];
            StoreUint64(descriptor,
                        entry_offset_bytes +
                            OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_TARGET_OFFSET_BYTES,
                        entry.target_relative_block);
            StoreUint32(descriptor,
                        entry_offset_bytes +
                            OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_PAYLOAD_CHECKSUM_OFFSET_BYTES,
                        entry.checksum);
            StoreUint32(
                descriptor,
                entry_offset_bytes + OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_RESERVED_OFFSET_BYTES, 0U);
        }
        StoreUint32(
            descriptor, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES,
            CalculateRootCrc32(descriptor, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES));
        status = this->WriteDeviceBlock(
            OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_START_RELATIVE_BLOCK + descriptor_index,
            descriptor);
        if (status != RootJournalStatus::Succeeded) {
            return status;
        }
    }

    for (uint64_t entry_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         entry_index < this->staged_block_count_; ++entry_index) {
        status = this->WriteDeviceBlock(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                            OS_KERNEL_ROOTFS_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK +
                                            entry_index,
                                        this->staged_blocks_[entry_index].bytes);
        if (status != RootJournalStatus::Succeeded) {
            return status;
        }
    }
    return this->FlushDevice();
}

RootJournalStatus RootJournal::WriteCommitRecord() noexcept {
    uint8_t commit[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    StoreUint64(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_MAGIC_OFFSET_BYTES,
                OS_KERNEL_ROOTFS_JOURNAL_COMMIT_MAGIC);
    StoreUint64(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_VERSION_OFFSET_BYTES,
                OS_KERNEL_ROOTFS_JOURNAL_FORMAT_VERSION);
    StoreUint64(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_SEQUENCE_OFFSET_BYTES, this->sequence_);
    StoreUint64(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_ENTRY_COUNT_OFFSET_BYTES,
                this->staged_block_count_);
    uint8_t header[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    RootJournalStatus status =
        this->ReadDeviceBlock(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                  OS_KERNEL_ROOTFS_JOURNAL_HEADER_RELATIVE_BLOCK,
                              header);
    if (status != RootJournalStatus::Succeeded) {
        return status;
    }
    StoreUint32(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_HEADER_CHECKSUM_OFFSET_BYTES,
                LoadUint32(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES));
    StoreUint32(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_CHECKSUM_OFFSET_BYTES,
                CalculateRootCrc32(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_CHECKSUM_OFFSET_BYTES));
    status = this->WriteDeviceBlock(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                        OS_KERNEL_ROOTFS_JOURNAL_COMMIT_RELATIVE_BLOCK,
                                    commit);
    return status == RootJournalStatus::Succeeded ? this->FlushDevice() : status;
}

RootJournalStatus RootJournal::CheckpointStagedBlocks(const bool replay) noexcept {
    for (uint64_t entry_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         entry_index < this->staged_block_count_; ++entry_index) {
        const RootJournalStatus status =
            this->WriteDeviceBlock(this->staged_blocks_[entry_index].target_relative_block,
                                   this->staged_blocks_[entry_index].bytes);
        if (status != RootJournalStatus::Succeeded) {
            return status;
        }
        ++this->statistics_.checkpoint_block_count;
    }
    const RootJournalStatus flush_status = this->FlushDevice();
    if (flush_status == RootJournalStatus::Succeeded && replay) {
        ++this->statistics_.replay_count;
    }
    return flush_status;
}

RootJournalStatus RootJournal::ClearPersistentState() noexcept {
    uint8_t zero_block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    RootJournalStatus status =
        this->WriteDeviceBlock(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                   OS_KERNEL_ROOTFS_JOURNAL_HEADER_RELATIVE_BLOCK,
                               zero_block);
    if (status != RootJournalStatus::Succeeded) {
        return status;
    }
    status = this->WriteDeviceBlock(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                        OS_KERNEL_ROOTFS_JOURNAL_COMMIT_RELATIVE_BLOCK,
                                    zero_block);
    return status == RootJournalStatus::Succeeded ? this->FlushDevice() : status;
}

RootJournalStatus RootJournal::Commit() noexcept {
    if (!this->initialized_) {
        return RootJournalStatus::NotInitialized;
    }
    if (!this->active_) {
        return RootJournalStatus::NotActive;
    }
    if (this->staged_block_count_ == OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE) {
        return RootJournalStatus::InvalidArgument;
    }
    RootJournalStatus status = this->WritePreparedTransaction();
    if (status == RootJournalStatus::Succeeded) {
        status = this->WriteCommitRecord();
    }
    if (status == RootJournalStatus::Succeeded) {
        status = this->CheckpointStagedBlocks(false);
    }
    if (status == RootJournalStatus::Succeeded) {
        status = this->ClearPersistentState();
    }
    if (status == RootJournalStatus::Succeeded) {
        ++this->statistics_.transaction_commit_count;
        this->ResetTransaction();
    }
    return status;
}

RootJournalStatus RootJournal::Abort() noexcept {
    if (!this->initialized_) {
        return RootJournalStatus::NotInitialized;
    }
    if (!this->active_) {
        return RootJournalStatus::NotActive;
    }
    ++this->statistics_.transaction_abort_count;
    this->ResetTransaction();
    return RootJournalStatus::Succeeded;
}

RootJournalStatus RootJournal::LoadCommittedTransaction(uint64_t &sequence, uint64_t &entry_count,
                                                        bool &committed) noexcept {
    sequence = OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE;
    entry_count = OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE;
    committed = false;
    uint8_t header[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    RootJournalStatus status =
        this->ReadDeviceBlock(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                  OS_KERNEL_ROOTFS_JOURNAL_HEADER_RELATIVE_BLOCK,
                              header);
    if (status != RootJournalStatus::Succeeded) {
        return status;
    }
    if (BlockIsZero(header)) {
        return RootJournalStatus::Succeeded;
    }
    const uint32_t stored_header_checksum =
        LoadUint32(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES);
    if (LoadUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_MAGIC_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_JOURNAL_MAGIC ||
        LoadUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_VERSION_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_JOURNAL_FORMAT_VERSION ||
        stored_header_checksum !=
            CalculateRootCrc32(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES)) {
        ++this->statistics_.checksum_failure_count;
        return RootJournalStatus::Corrupt;
    }
    sequence = LoadUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_SEQUENCE_OFFSET_BYTES);
    entry_count = LoadUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_ENTRY_COUNT_OFFSET_BYTES);
    if (sequence == OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE ||
        entry_count == OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE ||
        entry_count > OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT ||
        LoadUint64(header, OS_KERNEL_ROOTFS_JOURNAL_HEADER_DESCRIPTOR_COUNT_OFFSET_BYTES) !=
            OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_BLOCK_COUNT) {
        return RootJournalStatus::Corrupt;
    }

    uint8_t commit[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    status = this->ReadDeviceBlock(OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                                       OS_KERNEL_ROOTFS_JOURNAL_COMMIT_RELATIVE_BLOCK,
                                   commit);
    if (status != RootJournalStatus::Succeeded) {
        return status;
    }
    if (BlockIsZero(commit)) {
        return RootJournalStatus::Succeeded;
    }
    const bool commit_valid =
        LoadUint64(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_MAGIC_OFFSET_BYTES) ==
            OS_KERNEL_ROOTFS_JOURNAL_COMMIT_MAGIC &&
        LoadUint64(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_VERSION_OFFSET_BYTES) ==
            OS_KERNEL_ROOTFS_JOURNAL_FORMAT_VERSION &&
        LoadUint64(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_SEQUENCE_OFFSET_BYTES) == sequence &&
        LoadUint64(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_ENTRY_COUNT_OFFSET_BYTES) ==
            entry_count &&
        LoadUint32(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_HEADER_CHECKSUM_OFFSET_BYTES) ==
            stored_header_checksum &&
        LoadUint32(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_CHECKSUM_OFFSET_BYTES) ==
            CalculateRootCrc32(commit, OS_KERNEL_ROOTFS_JOURNAL_COMMIT_CHECKSUM_OFFSET_BYTES);
    if (!commit_valid) {
        return RootJournalStatus::Succeeded;
    }

    this->staged_block_count_ = entry_count;
    for (uint64_t descriptor_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         descriptor_index < OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_BLOCK_COUNT; ++descriptor_index) {
        uint8_t descriptor[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
        status = this->ReadDeviceBlock(
            OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_START_RELATIVE_BLOCK + descriptor_index,
            descriptor);
        if (status != RootJournalStatus::Succeeded) {
            return status;
        }
        if (LoadUint32(descriptor, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES) !=
            CalculateRootCrc32(descriptor, OS_KERNEL_ROOTFS_JOURNAL_HEADER_CHECKSUM_OFFSET_BYTES)) {
            ++this->statistics_.checksum_failure_count;
            return RootJournalStatus::Corrupt;
        }
        for (uint64_t slot_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
             slot_index < OS_KERNEL_ROOTFS_JOURNAL_ENTRIES_PER_DESCRIPTOR_BLOCK; ++slot_index) {
            const uint64_t entry_index =
                descriptor_index * OS_KERNEL_ROOTFS_JOURNAL_ENTRIES_PER_DESCRIPTOR_BLOCK +
                slot_index;
            if (entry_index >= entry_count) {
                break;
            }
            const uint64_t entry_offset_bytes =
                slot_index * OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_ENTRY_SIZE_BYTES;
            StagedBlock &entry = this->staged_blocks_[entry_index];
            entry.target_relative_block =
                LoadUint64(descriptor, entry_offset_bytes +
                                           OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_TARGET_OFFSET_BYTES);
            entry.checksum = LoadUint32(
                descriptor, entry_offset_bytes +
                                OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_PAYLOAD_CHECKSUM_OFFSET_BYTES);
            const uint32_t reserved = LoadUint32(
                descriptor,
                entry_offset_bytes + OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_RESERVED_OFFSET_BYTES);
            if (!TargetBlockIsValid(entry.target_relative_block, this->rootfs_total_block_count_) ||
                reserved != 0U) {
                return RootJournalStatus::Corrupt;
            }
            for (uint64_t prior_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
                 prior_index < entry_index; ++prior_index) {
                if (this->staged_blocks_[prior_index].target_relative_block ==
                    entry.target_relative_block) {
                    return RootJournalStatus::Corrupt;
                }
            }
            status = this->ReadDeviceBlock(
                OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK +
                    OS_KERNEL_ROOTFS_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK + entry_index,
                entry.bytes);
            if (status != RootJournalStatus::Succeeded) {
                return status;
            }
            if (CalculateRootCrc32(entry.bytes, sizeof(entry.bytes)) != entry.checksum) {
                ++this->statistics_.checksum_failure_count;
                return RootJournalStatus::Corrupt;
            }
            entry.occupied = true;
        }
    }
    this->sequence_ = sequence;
    this->reserved_credit_count_ = entry_count;
    committed = true;
    return RootJournalStatus::Succeeded;
}

RootJournalStatus RootJournal::Recover(RootJournalRecoveryResult &result) noexcept {
    result = RootJournalRecoveryResult::Clean;
    if (!this->initialized_) {
        return RootJournalStatus::NotInitialized;
    }
    if (this->active_) {
        return RootJournalStatus::AlreadyActive;
    }
    uint64_t sequence = OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE;
    uint64_t entry_count = OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE;
    bool committed = false;
    RootJournalStatus status = this->LoadCommittedTransaction(sequence, entry_count, committed);
    if (status != RootJournalStatus::Succeeded) {
        this->ResetTransaction();
        return status;
    }
    if (sequence == OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE) {
        this->ResetTransaction();
        return RootJournalStatus::Succeeded;
    }
    if (!committed) {
        status = this->ClearPersistentState();
        if (status == RootJournalStatus::Succeeded) {
            result = RootJournalRecoveryResult::DiscardedIncomplete;
            ++this->statistics_.discarded_incomplete_count;
        }
        this->ResetTransaction();
        return status;
    }
    status = this->CheckpointStagedBlocks(true);
    if (status == RootJournalStatus::Succeeded) {
        status = this->ClearPersistentState();
    }
    if (status == RootJournalStatus::Succeeded) {
        result = RootJournalRecoveryResult::Replayed;
    }
    this->ResetTransaction();
    return status;
}

void RootJournal::ResetTransaction() noexcept {
    for (uint64_t entry_index = OS_KERNEL_ROOTFS_JOURNAL_FIRST_INDEX;
         entry_index < OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT; ++entry_index) {
        this->staged_blocks_[entry_index] = StagedBlock{};
    }
    this->sequence_ = OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE;
    this->reserved_credit_count_ = OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE;
    this->staged_block_count_ = OS_KERNEL_ROOTFS_JOURNAL_EMPTY_VALUE;
    this->active_ = false;
}

bool RootJournal::IsActive() const noexcept { return this->active_; }

uint64_t RootJournal::StagedBlockCount() const noexcept { return this->staged_block_count_; }

RootJournalStatistics RootJournal::Statistics() const noexcept { return this->statistics_; }

}
