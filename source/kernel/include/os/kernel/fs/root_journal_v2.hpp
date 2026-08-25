#pragma once

#include <os/kernel/device/block_device.hpp>
#include <os/kernel/fs/root_journal_v2_format.hpp>

#include <stdint.h>

namespace os::kernel::fs {

enum class RootJournalV2Status : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    NotFormatted,
    AlreadyActive,
    NotActive,
    InvalidArgument,
    CapacityExhausted,
    SequenceExhausted,
    DeviceReadFailed,
    DeviceWriteFailed,
    DeviceFlushFailed,
    Corrupt,
};

enum class RootJournalV2RecoveryResult : uint64_t {
    Clean,
    DiscardedIncomplete,
    Replayed,
};

struct RootJournalV2Statistics final {
    uint64_t format_count;
    uint64_t transaction_begin_count;
    uint64_t transaction_commit_count;
    uint64_t transaction_abort_count;
    uint64_t metadata_stage_count;
    uint64_t ordered_data_stage_count;
    uint64_t revoke_count;
    uint64_t checkpoint_transaction_count;
    uint64_t checkpoint_block_count;
    uint64_t replay_transaction_count;
    uint64_t replay_block_count;
    uint64_t revoked_replay_skip_count;
    uint64_t discarded_incomplete_count;
    uint64_t checksum_failure_count;
    uint64_t capacity_rejection_count;
    uint64_t flush_count;
};

class RootJournalV2 final {
  public:
    RootJournalV2() noexcept = default;
    RootJournalV2(const RootJournalV2 &) = delete;
    RootJournalV2 &operator=(const RootJournalV2 &) = delete;

    [[nodiscard]] RootJournalV2Status
    Initialize(BlockDevice &device, uint64_t file_system_start_lba,
               uint64_t file_system_total_block_count, uint64_t file_system_inode_count,
               uint64_t journal_start_relative_block, RootV5Uuid file_system_uuid) noexcept;
    [[nodiscard]] RootJournalV2Status Format(uint64_t creation_time_nanoseconds) noexcept;
    [[nodiscard]] RootJournalV2Status Open() noexcept;
    [[nodiscard]] RootJournalV2Status Recover(RootJournalV2RecoveryResult &result) noexcept;
    [[nodiscard]] RootJournalV2Status Begin(uint64_t reserved_metadata_block_count,
                                            uint64_t reserved_ordered_data_block_count,
                                            uint64_t reserved_revoke_count) noexcept;
    [[nodiscard]] RootJournalV2Status StageMetadata(uint64_t target_relative_block,
                                                    const uint8_t *block,
                                                    uint64_t block_size_bytes) noexcept;
    [[nodiscard]] RootJournalV2Status StageOrderedData(uint64_t target_relative_block,
                                                       const uint8_t *block,
                                                       uint64_t block_size_bytes) noexcept;
    [[nodiscard]] RootJournalV2Status Revoke(uint64_t target_relative_block) noexcept;
    [[nodiscard]] bool TryReadStagedMetadata(uint64_t target_relative_block, uint8_t *block,
                                             uint64_t block_size_bytes) const noexcept;
    [[nodiscard]] RootJournalV2Status Commit(uint64_t commit_time_nanoseconds) noexcept;
    [[nodiscard]] RootJournalV2Status CommitAndCheckpoint(
        uint64_t commit_time_nanoseconds) noexcept;
    [[nodiscard]] RootJournalV2Status CheckpointOldest() noexcept;
    [[nodiscard]] RootJournalV2Status Abort() noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] uint64_t ActiveSequence() const noexcept;
    [[nodiscard]] uint64_t PendingCommittedTransactionCount() noexcept;
    [[nodiscard]] const RootJournalV2Superblock &Superblock() const noexcept;
    [[nodiscard]] RootJournalV2Statistics Statistics() const noexcept;

  private:
    static constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SCRATCH_BLOCK_COUNT = 5ULL;
    struct StagedBlock final {
        uint8_t bytes[OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES];
        uint64_t target_relative_block;
        uint32_t checksum;
        bool occupied;
    };

    struct SlotState final {
        uint64_t sequence;
        uint64_t slot_index;
        bool committed;
        bool checkpointed;
        bool incomplete;
    };

    [[nodiscard]] RootJournalV2Status ReadFileSystemBlock(uint64_t relative_block,
                                                          uint8_t *block) noexcept;
    [[nodiscard]] RootJournalV2Status WriteFileSystemBlock(uint64_t relative_block,
                                                           const uint8_t *block) noexcept;
    [[nodiscard]] RootJournalV2Status FlushDevice() noexcept;
    [[nodiscard]] RootJournalV2Status WriteSuperblock() noexcept;
    [[nodiscard]] RootJournalV2Status FindFreeSlot(uint64_t &slot_index) noexcept;
    [[nodiscard]] RootJournalV2Status ReadSlotState(uint64_t slot_index, SlotState &state,
                                                    bool validate_payloads) noexcept;
    [[nodiscard]] RootJournalV2Status LoadCommittedSlot(uint64_t slot_index,
                                                        RootJournalV2Descriptor &descriptor,
                                                        RootJournalV2RevokeBlock &revoke_block,
                                                        RootJournalV2Commit &commit) noexcept;
    [[nodiscard]] RootJournalV2Status WritePreparedTransaction(uint32_t &descriptor_checksum,
                                                               uint32_t &revoke_checksum) noexcept;
    [[nodiscard]] RootJournalV2Status WriteOrderedData() noexcept;
    [[nodiscard]] RootJournalV2Status WriteCommitRecord(uint64_t commit_time_nanoseconds,
                                                        uint32_t descriptor_checksum,
                                                        uint32_t revoke_checksum) noexcept;
    [[nodiscard]] RootJournalV2Status WriteCheckpointRecord(uint64_t slot_index, uint64_t sequence,
                                                            uint32_t commit_checksum) noexcept;
    [[nodiscard]] RootJournalV2Status ClearSlot(uint64_t slot_index) noexcept;
    [[nodiscard]] RootJournalV2Status CheckpointActive(uint32_t commit_checksum) noexcept;
    [[nodiscard]] RootJournalV2Status CheckpointSlot(uint64_t slot_index, bool replay,
                                                     const SlotState *states,
                                                     uint64_t state_count) noexcept;
    [[nodiscard]] RootJournalV2Status TargetRevokedAfter(uint64_t target_relative_block,
                                                         uint64_t sequence, const SlotState *states,
                                                         uint64_t state_count,
                                                         bool &revoked) noexcept;
    void ClearStagedBlock(StagedBlock &block) noexcept;
    void ResetActiveTransaction() noexcept;

    BlockDevice *device_{nullptr};
    uint8_t block_scratch_[OS_KERNEL_ROOTFS_V5_JOURNAL_SCRATCH_BLOCK_COUNT]
                          [OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    StagedBlock metadata_blocks_[OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT]{};
    StagedBlock
        ordered_data_blocks_[OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_ORDERED_DATA_BLOCK_COUNT]{};
    uint64_t revoke_targets_[OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT]{};
    RootJournalV2Superblock superblock_{};
    RootJournalV2Statistics statistics_{};
    uint64_t file_system_start_lba_{};
    uint64_t configured_total_block_count_{};
    uint64_t configured_inode_count_{};
    uint64_t configured_journal_start_relative_block_{};
    RootV5Uuid configured_file_system_uuid_{};
    uint64_t active_sequence_{};
    uint64_t active_slot_index_{OS_KERNEL_ROOTFS_V5_NO_BLOCK};
    uint64_t reserved_metadata_block_count_{};
    uint64_t reserved_ordered_data_block_count_{};
    uint64_t reserved_revoke_count_{};
    uint64_t metadata_block_count_{};
    uint64_t ordered_data_block_count_{};
    uint64_t revoke_count_{};
    bool initialized_{};
    bool formatted_{};
    bool active_{};
};

}
