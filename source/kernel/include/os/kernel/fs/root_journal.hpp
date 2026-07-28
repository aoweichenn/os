#pragma once

#include "os/kernel/fs/block_cache.hpp"
#include "os/kernel/fs/root_file_system_format.hpp"

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_BLOCK_COUNT = 4ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_ENTRY_SIZE_BYTES = 16ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_CHECKSUM_SIZE_BYTES = 4ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_ENTRIES_PER_DESCRIPTOR_BLOCK =
    (OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES - OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_CHECKSUM_SIZE_BYTES) /
    OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_ENTRY_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT =
    OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_BLOCK_COUNT *
    OS_KERNEL_ROOTFS_JOURNAL_ENTRIES_PER_DESCRIPTOR_BLOCK;

enum class RootJournalStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidArgument,
    AlreadyActive,
    NotActive,
    CreditsExhausted,
    SequenceExhausted,
    DeviceReadFailed,
    DeviceWriteFailed,
    DeviceFlushFailed,
    Corrupt,
};

enum class RootJournalRecoveryResult : uint64_t {
    Clean,
    DiscardedIncomplete,
    Replayed,
};

struct RootJournalStatistics final {
    uint64_t transaction_begin_count;
    uint64_t transaction_commit_count;
    uint64_t transaction_abort_count;
    uint64_t staged_block_count;
    uint64_t checkpoint_block_count;
    uint64_t replay_count;
    uint64_t discarded_incomplete_count;
    uint64_t checksum_failure_count;
    uint64_t credit_rejection_count;
    uint64_t flush_count;
};

class RootJournal final {
  public:
    RootJournal() noexcept = default;
    RootJournal(const RootJournal &) = delete;
    RootJournal &operator=(const RootJournal &) = delete;

    [[nodiscard]] RootJournalStatus Initialize(FileSystemBlockDevice &device,
                                               uint64_t rootfs_start_lba) noexcept;
    [[nodiscard]] RootJournalStatus Recover(RootJournalRecoveryResult &result) noexcept;
    [[nodiscard]] RootJournalStatus Begin(uint64_t sequence,
                                          uint64_t reserved_credit_count) noexcept;
    [[nodiscard]] RootJournalStatus Stage(uint64_t target_relative_block, const uint8_t *block,
                                          uint64_t block_size_bytes) noexcept;
    [[nodiscard]] bool TryReadStaged(uint64_t target_relative_block, uint8_t *block,
                                     uint64_t block_size_bytes) const noexcept;
    [[nodiscard]] RootJournalStatus Commit() noexcept;
    [[nodiscard]] RootJournalStatus Abort() noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] uint64_t StagedBlockCount() const noexcept;
    [[nodiscard]] RootJournalStatistics Statistics() const noexcept;

  private:
    struct StagedBlock final {
        uint8_t bytes[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES];
        uint64_t target_relative_block;
        uint32_t checksum;
        bool occupied;
    };

    [[nodiscard]] RootJournalStatus ReadDeviceBlock(uint64_t rootfs_relative_block,
                                                    uint8_t *block) noexcept;
    [[nodiscard]] RootJournalStatus WriteDeviceBlock(uint64_t rootfs_relative_block,
                                                     const uint8_t *block) noexcept;
    [[nodiscard]] RootJournalStatus FlushDevice() noexcept;
    [[nodiscard]] RootJournalStatus ClearPersistentState() noexcept;
    [[nodiscard]] RootJournalStatus
    LoadCommittedTransaction(uint64_t &sequence, uint64_t &entry_count, bool &committed) noexcept;
    [[nodiscard]] RootJournalStatus WritePreparedTransaction() noexcept;
    [[nodiscard]] RootJournalStatus WriteCommitRecord() noexcept;
    [[nodiscard]] RootJournalStatus CheckpointStagedBlocks(bool replay) noexcept;
    void ResetTransaction() noexcept;

    FileSystemBlockDevice *device_{nullptr};
    StagedBlock staged_blocks_[OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT]{};
    RootJournalStatistics statistics_{};
    uint64_t rootfs_start_lba_{};
    uint64_t sequence_{};
    uint64_t reserved_credit_count_{};
    uint64_t staged_block_count_{};
    bool initialized_{};
    bool active_{};
};

}
