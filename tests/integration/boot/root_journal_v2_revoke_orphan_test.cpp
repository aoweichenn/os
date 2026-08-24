#include <os/kernel/fs/root_journal_v2.hpp>
#include <root_journal_v2_test_device.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_SUITE_NAME =
    "kernel/root_journal_v2_revoke_orphan/integration";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_REVOKE_MESSAGE =
    "后续 committed revoke 必须阻止较早 metadata replay 覆盖已释放目标";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_ORPHAN_MESSAGE =
    "orphan file 与 inode metadata 必须同事务重放且二次恢复幂等";
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_UUID{
    .low = 0xCAFEBABE10203040ULL,
    .high = 0x0123456789ABCDEFULL,
};
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_CREATION_TIME = 123456789ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_COMMIT_TIME = 987654321ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_REVOKED_HOME_TARGET = 512ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_ORPHAN_HOME_TARGET = 640ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_INODE_HOME_TARGET = 641ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_ORPHAN_INODE_NUMBER = 32ULL;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_OLD_PATTERN = 0x11U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_STALE_PATTERN = 0x77U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_INODE_PATTERN = 0xA5U;

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
                              OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_UUID);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_SUITE_NAME};
    os::test::RootJournalV2TestDevice device{};
    os::kernel::fs::RootJournalV2 writer{};
    uint8_t old_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t stale_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t inode_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t orphan_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t observed[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    FillBlock(old_block, OS_TEST_ROOT_JOURNAL_V2_OLD_PATTERN);
    FillBlock(stale_block, OS_TEST_ROOT_JOURNAL_V2_STALE_PATTERN);
    FillBlock(inode_block, OS_TEST_ROOT_JOURNAL_V2_INODE_PATTERN);
    device.WriteDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_REVOKED_HOME_TARGET, old_block);

    bool prepared =
        InitializeJournal(writer, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        writer.Format(OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_CREATION_TIME) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        writer.Begin(1ULL, 0ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        writer.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_REVOKED_HOME_TARGET, stale_block,
                             sizeof(stale_block)) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        writer.Commit(OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_COMMIT_TIME) ==
            os::kernel::fs::RootJournalV2Status::Succeeded;

    os::kernel::fs::RootJournalV2OrphanBlock orphan_block{
        .generation = 1ULL,
        .entry_count = 0ULL,
        .file_system_uuid = OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_UUID,
        .inode_numbers = {},
    };
    prepared =
        prepared &&
        os::kernel::fs::AddRootJournalV2Orphan(writer.Superblock(), orphan_block,
                                               OS_TEST_ROOT_JOURNAL_V2_ORPHAN_INODE_NUMBER) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::EncodeRootJournalV2OrphanBlock(writer.Superblock(), orphan_block,
                                                       orphan_bytes, sizeof(orphan_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        writer.Begin(1ULL, 0ULL, 1ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        writer.Revoke(OS_TEST_ROOT_JOURNAL_V2_REVOKED_HOME_TARGET) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        writer.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_ORPHAN_HOME_TARGET, orphan_bytes,
                             sizeof(orphan_bytes)) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        writer.Commit(OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_COMMIT_TIME + 1ULL) ==
            os::kernel::fs::RootJournalV2Status::Succeeded;
    device.Crash();

    os::kernel::fs::RootJournalV2 recovery{};
    os::kernel::fs::RootJournalV2RecoveryResult recovery_result{};
    const bool recovered =
        prepared &&
        InitializeJournal(recovery, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        recovery.Recover(recovery_result) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        recovery_result == os::kernel::fs::RootJournalV2RecoveryResult::Replayed;
    device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_REVOKED_HOME_TARGET, observed);
    context.Expect(recovered && BlockHasPattern(observed, OS_TEST_ROOT_JOURNAL_V2_OLD_PATTERN) &&
                       recovery.Statistics().revoked_replay_skip_count == 1ULL,
                   OS_TEST_ROOT_JOURNAL_V2_REVOKE_MESSAGE);

    device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_ORPHAN_HOME_TARGET, observed);
    os::kernel::fs::RootJournalV2OrphanBlock decoded_orphan{};
    bool orphan_valid =
        os::kernel::fs::DecodeRootJournalV2OrphanBlock(recovery.Superblock(), observed,
                                                       sizeof(observed), decoded_orphan) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        decoded_orphan.entry_count == 1ULL &&
        decoded_orphan.inode_numbers[0] == OS_TEST_ROOT_JOURNAL_V2_ORPHAN_INODE_NUMBER;

    orphan_valid =
        orphan_valid &&
        os::kernel::fs::RemoveRootJournalV2Orphan(recovery.Superblock(), decoded_orphan,
                                                  OS_TEST_ROOT_JOURNAL_V2_ORPHAN_INODE_NUMBER) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::EncodeRootJournalV2OrphanBlock(recovery.Superblock(), decoded_orphan,
                                                       orphan_bytes, sizeof(orphan_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        recovery.Begin(2ULL, 0ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        recovery.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_ORPHAN_HOME_TARGET, orphan_bytes,
                               sizeof(orphan_bytes)) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        recovery.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_INODE_HOME_TARGET, inode_block,
                               sizeof(inode_block)) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        recovery.Commit(OS_TEST_ROOT_JOURNAL_V2_INTEGRATION_COMMIT_TIME + 2ULL) ==
            os::kernel::fs::RootJournalV2Status::Succeeded;
    device.Crash();

    os::kernel::fs::RootJournalV2 second_recovery{};
    orphan_valid = orphan_valid &&
                   InitializeJournal(second_recovery, device) ==
                       os::kernel::fs::RootJournalV2Status::Succeeded &&
                   second_recovery.Recover(recovery_result) ==
                       os::kernel::fs::RootJournalV2Status::Succeeded &&
                   recovery_result == os::kernel::fs::RootJournalV2RecoveryResult::Replayed;
    device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_ORPHAN_HOME_TARGET, observed);
    orphan_valid = orphan_valid &&
                   os::kernel::fs::DecodeRootJournalV2OrphanBlock(
                       second_recovery.Superblock(), observed, sizeof(observed), decoded_orphan) ==
                       os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
                   decoded_orphan.entry_count == 0ULL;
    device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_INODE_HOME_TARGET, observed);
    orphan_valid = orphan_valid && BlockHasPattern(observed, OS_TEST_ROOT_JOURNAL_V2_INODE_PATTERN);

    device.Crash();
    os::kernel::fs::RootJournalV2 idempotence{};
    orphan_valid =
        orphan_valid &&
        InitializeJournal(idempotence, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        idempotence.Recover(recovery_result) == os::kernel::fs::RootJournalV2Status::Succeeded &&
        recovery_result == os::kernel::fs::RootJournalV2RecoveryResult::Clean;
    context.Expect(orphan_valid, OS_TEST_ROOT_JOURNAL_V2_ORPHAN_MESSAGE);
    return context.ExitCode();
}
