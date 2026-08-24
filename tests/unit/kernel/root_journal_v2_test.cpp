#include <os/kernel/fs/root_journal_v2.hpp>
#include <root_journal_v2_test_device.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_SUITE_NAME = "kernel/root_journal_v2/unit";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_STATE_MESSAGE =
    "journal v2 初始化、format、sequence、credit 与 abort 状态必须守恒";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_ORDERED_MESSAGE =
    "ordered data 必须先稳定，metadata 只在 commit 后 checkpoint 到 home";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_SLOT_MESSAGE =
    "四个 committed slot 必须有界，checkpoint 后才能安全复用";
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_JOURNAL_V2_UUID{
    .low = 0x1122334455667788ULL,
    .high = 0x8877665544332211ULL,
};
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_JOURNAL_V2_WRONG_UUID{
    .low = 0x1122334455667789ULL,
    .high = 0x8877665544332211ULL,
};
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_CREATION_TIME_NANOSECONDS = 123456789ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_COMMIT_TIME_NANOSECONDS = 987654321ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_METADATA_TARGET = 512ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_ORDERED_TARGET = 768ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_REVOKE_TARGET = 1024ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_SLOT_TARGET_BASE = 1280ULL;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_OLD_PATTERN = 0x17U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_METADATA_PATTERN = 0xA5U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_REPLACEMENT_PATTERN = 0xC3U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_ORDERED_PATTERN = 0x5AU;

void FillBlock(uint8_t *const block, const uint8_t pattern) noexcept {
    for (uint64_t byte_index = 0ULL;
         byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES; ++byte_index) {
        block[byte_index] = pattern;
    }
}

[[nodiscard]] bool BlockHasPattern(const uint8_t *const block, const uint8_t pattern) noexcept {
    for (uint64_t byte_index = 0ULL;
         byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES; ++byte_index) {
        if (block[byte_index] != pattern) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] os::kernel::fs::RootJournalV2Status
InitializeJournal(os::kernel::fs::RootJournalV2 &journal,
                  os::test::RootJournalV2TestDevice &device) noexcept {
    return journal.Initialize(device, os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_START_LBA,
                              os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_BLOCK_COUNT,
                              os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_INODE_COUNT,
                              os::test::OS_TEST_ROOT_JOURNAL_V2_JOURNAL_START_RELATIVE_BLOCK,
                              OS_TEST_ROOT_JOURNAL_V2_UUID);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_JOURNAL_V2_SUITE_NAME};
    os::test::RootJournalV2TestDevice device{};
    os::kernel::fs::RootJournalV2 journal{};
    os::kernel::fs::RootJournalV2RecoveryResult recovery_result{};
    const bool state_valid =
        InitializeJournal(journal, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.Open() == os::kernel::fs::RootJournalV2Status::NotFormatted &&
        journal.Format(OS_TEST_ROOT_JOURNAL_V2_CREATION_TIME_NANOSECONDS) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.Recover(recovery_result) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        recovery_result == os::kernel::fs::RootJournalV2RecoveryResult::Clean &&
        journal.Begin(0ULL, 1ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::CapacityExhausted &&
        journal.Begin(1ULL, 1ULL, 1ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.ActiveSequence() == 1ULL &&
        journal.Abort() == os::kernel::fs::RootJournalV2Status::Succeeded && !journal.IsActive() &&
        journal.Superblock().next_sequence == 2ULL;
    context.Expect(state_valid, OS_TEST_ROOT_JOURNAL_V2_STATE_MESSAGE);
    os::kernel::fs::RootJournalV2 wrong_uuid_journal{};
    const bool wrong_uuid_rejected =
        wrong_uuid_journal.Initialize(
            device, os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_START_LBA,
            os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_BLOCK_COUNT,
            os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_INODE_COUNT,
            os::test::OS_TEST_ROOT_JOURNAL_V2_JOURNAL_START_RELATIVE_BLOCK,
            OS_TEST_ROOT_JOURNAL_V2_WRONG_UUID) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        wrong_uuid_journal.Open() == os::kernel::fs::RootJournalV2Status::Corrupt;
    context.Expect(wrong_uuid_rejected, OS_TEST_ROOT_JOURNAL_V2_STATE_MESSAGE);

    uint8_t old_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t metadata_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t replacement_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t ordered_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t observed_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    FillBlock(old_block, OS_TEST_ROOT_JOURNAL_V2_OLD_PATTERN);
    FillBlock(metadata_block, OS_TEST_ROOT_JOURNAL_V2_METADATA_PATTERN);
    FillBlock(replacement_block, OS_TEST_ROOT_JOURNAL_V2_REPLACEMENT_PATTERN);
    FillBlock(ordered_block, OS_TEST_ROOT_JOURNAL_V2_ORDERED_PATTERN);
    device.WriteDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_METADATA_TARGET, old_block);
    device.WriteDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_ORDERED_TARGET, old_block);
    const bool transaction_staged =
        journal.Begin(2ULL, 1ULL, 1ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_METADATA_TARGET, metadata_block,
                              sizeof(metadata_block)) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_METADATA_TARGET, replacement_block,
                              sizeof(replacement_block)) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.StageOrderedData(OS_TEST_ROOT_JOURNAL_V2_ORDERED_TARGET, ordered_block,
                                 sizeof(ordered_block)) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.Revoke(OS_TEST_ROOT_JOURNAL_V2_REVOKE_TARGET) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.TryReadStagedMetadata(OS_TEST_ROOT_JOURNAL_V2_METADATA_TARGET, observed_block,
                                      sizeof(observed_block)) &&
        BlockHasPattern(observed_block, OS_TEST_ROOT_JOURNAL_V2_REPLACEMENT_PATTERN) &&
        journal.Commit(OS_TEST_ROOT_JOURNAL_V2_COMMIT_TIME_NANOSECONDS) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.PendingCommittedTransactionCount() == 1ULL;
    device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_ORDERED_TARGET, observed_block);
    const bool ordered_stable =
        transaction_staged &&
        BlockHasPattern(observed_block, OS_TEST_ROOT_JOURNAL_V2_ORDERED_PATTERN);
    device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_METADATA_TARGET, observed_block);
    const bool metadata_not_checkpointed =
        BlockHasPattern(observed_block, OS_TEST_ROOT_JOURNAL_V2_OLD_PATTERN);
    const bool checkpoint_valid =
        journal.CheckpointOldest() == os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.PendingCommittedTransactionCount() == 0ULL;
    device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_METADATA_TARGET, observed_block);
    const bool ordered_valid =
        ordered_stable && metadata_not_checkpointed && checkpoint_valid &&
        BlockHasPattern(observed_block, OS_TEST_ROOT_JOURNAL_V2_REPLACEMENT_PATTERN);
    context.Expect(ordered_valid, OS_TEST_ROOT_JOURNAL_V2_ORDERED_MESSAGE);

    bool slots_valid = true;
    for (uint64_t slot_index = 0ULL;
         slots_valid && slot_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT;
         ++slot_index) {
        FillBlock(metadata_block,
                  static_cast<uint8_t>(OS_TEST_ROOT_JOURNAL_V2_METADATA_PATTERN + slot_index));
        slots_valid =
            journal.Begin(1ULL, 0ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
            journal.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_SLOT_TARGET_BASE + slot_index,
                                  metadata_block, sizeof(metadata_block)) ==
                os::kernel::fs::RootJournalV2Status::Succeeded &&
            journal.Commit(OS_TEST_ROOT_JOURNAL_V2_COMMIT_TIME_NANOSECONDS + slot_index) ==
                os::kernel::fs::RootJournalV2Status::Succeeded;
    }
    slots_valid =
        slots_valid &&
        journal.PendingCommittedTransactionCount() ==
            os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT &&
        journal.Begin(1ULL, 0ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::CapacityExhausted &&
        journal.CheckpointOldest() == os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.Begin(1ULL, 0ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.Abort() == os::kernel::fs::RootJournalV2Status::Succeeded;
    const os::kernel::fs::RootJournalV2Statistics statistics = journal.Statistics();
    slots_valid = slots_valid && statistics.transaction_commit_count == 5ULL &&
                  statistics.checkpoint_transaction_count == 2ULL &&
                  statistics.capacity_rejection_count >= 2ULL;
    context.Expect(slots_valid, OS_TEST_ROOT_JOURNAL_V2_SLOT_MESSAGE);
    return context.ExitCode();
}
