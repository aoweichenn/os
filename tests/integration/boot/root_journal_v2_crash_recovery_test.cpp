#include <os/kernel/fs/root_journal_v2.hpp>
#include <root_journal_v2_test_device.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_CRASH_SUITE_NAME =
    "kernel/root_journal_v2_crash_recovery/integration";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_CRASH_MESSAGE =
    "每个 prepared/ordered/commit 断电点恢复后 metadata 新态必须蕴含 data 已稳定且 replay 幂等";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_CORRUPTION_MESSAGE =
    "有效 commit 引用的 payload checksum 损坏必须拒绝恢复";
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_JOURNAL_V2_CRASH_UUID{
    .low = 0x524F4F544A563243ULL,
    .high = 0x43524153484D4154ULL,
};
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_CRASH_POINT_COUNT = 128ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_CRASH_FAILURE_ORDINAL_SPAN = 48ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RECOVERY_CRASH_POINT_COUNT = 96ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RECOVERY_FAILURE_ORDINAL_SPAN = 64ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_CRASH_CREATION_TIME = 123456789ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_CRASH_COMMIT_TIME = 987654321ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_TARGET = 512ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_CRASH_ORDERED_TARGET = 768ULL;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_CRASH_OLD_PATTERN = 0x19U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_PATTERN = 0xA9U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_CRASH_ORDERED_PATTERN = 0x5CU;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_CRASH_CORRUPTION_MASK = 0x20U;

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
                              OS_TEST_ROOT_JOURNAL_V2_CRASH_UUID);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_JOURNAL_V2_CRASH_SUITE_NAME};
    uint8_t old_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t metadata_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t ordered_block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t observed[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    FillBlock(old_block, OS_TEST_ROOT_JOURNAL_V2_CRASH_OLD_PATTERN);
    FillBlock(metadata_block, OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_PATTERN);
    FillBlock(ordered_block, OS_TEST_ROOT_JOURNAL_V2_CRASH_ORDERED_PATTERN);
    bool crash_matrix_valid = true;

    for (uint64_t crash_point = 0ULL;
         crash_matrix_valid && crash_point < OS_TEST_ROOT_JOURNAL_V2_CRASH_POINT_COUNT;
         ++crash_point) {
        os::test::RootJournalV2TestDevice device{};
        os::kernel::fs::RootJournalV2 writer{};
        crash_matrix_valid =
            InitializeJournal(writer, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
            writer.Format(OS_TEST_ROOT_JOURNAL_V2_CRASH_CREATION_TIME) ==
                os::kernel::fs::RootJournalV2Status::Succeeded;
        device.WriteDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_TARGET,
                                           old_block);
        device.WriteDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_CRASH_ORDERED_TARGET, old_block);
        device.ResetOperationCounts();
        device.SetFailureOrdinal(crash_point % OS_TEST_ROOT_JOURNAL_V2_CRASH_FAILURE_ORDINAL_SPAN +
                                 1ULL);

        if (writer.Begin(1ULL, 1ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::Succeeded) {
            crash_matrix_valid =
                writer.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_TARGET, metadata_block,
                                     sizeof(metadata_block)) ==
                    os::kernel::fs::RootJournalV2Status::Succeeded &&
                writer.StageOrderedData(OS_TEST_ROOT_JOURNAL_V2_CRASH_ORDERED_TARGET, ordered_block,
                                        sizeof(ordered_block)) ==
                    os::kernel::fs::RootJournalV2Status::Succeeded;
            if (crash_matrix_valid) {
                static_cast<void>(writer.Commit(OS_TEST_ROOT_JOURNAL_V2_CRASH_COMMIT_TIME));
            }
        }
        device.Crash();

        os::kernel::fs::RootJournalV2 recovery{};
        os::kernel::fs::RootJournalV2RecoveryResult recovery_result{};
        crash_matrix_valid =
            crash_matrix_valid &&
            InitializeJournal(recovery, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
            recovery.Recover(recovery_result) == os::kernel::fs::RootJournalV2Status::Succeeded;
        device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_TARGET, observed);
        const bool metadata_old =
            BlockHasPattern(observed, OS_TEST_ROOT_JOURNAL_V2_CRASH_OLD_PATTERN);
        const bool metadata_new =
            BlockHasPattern(observed, OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_PATTERN);
        device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_CRASH_ORDERED_TARGET, observed);
        const bool ordered_old =
            BlockHasPattern(observed, OS_TEST_ROOT_JOURNAL_V2_CRASH_OLD_PATTERN);
        const bool ordered_new =
            BlockHasPattern(observed, OS_TEST_ROOT_JOURNAL_V2_CRASH_ORDERED_PATTERN);
        crash_matrix_valid = crash_matrix_valid && (metadata_old || metadata_new) &&
                             (ordered_old || ordered_new) && (!metadata_new || ordered_new);

        device.Crash();
        os::kernel::fs::RootJournalV2 idempotence{};
        crash_matrix_valid = crash_matrix_valid &&
                             InitializeJournal(idempotence, device) ==
                                 os::kernel::fs::RootJournalV2Status::Succeeded &&
                             idempotence.Recover(recovery_result) ==
                                 os::kernel::fs::RootJournalV2Status::Succeeded &&
                             recovery_result == os::kernel::fs::RootJournalV2RecoveryResult::Clean;
    }
    context.Expect(crash_matrix_valid, OS_TEST_ROOT_JOURNAL_V2_CRASH_MESSAGE);

    bool recovery_crash_valid = true;
    for (uint64_t crash_point = 0ULL;
         recovery_crash_valid && crash_point < OS_TEST_ROOT_JOURNAL_V2_RECOVERY_CRASH_POINT_COUNT;
         ++crash_point) {
        os::test::RootJournalV2TestDevice device{};
        os::kernel::fs::RootJournalV2 writer{};
        recovery_crash_valid =
            InitializeJournal(writer, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
            writer.Format(OS_TEST_ROOT_JOURNAL_V2_CRASH_CREATION_TIME) ==
                os::kernel::fs::RootJournalV2Status::Succeeded &&
            writer.Begin(1ULL, 0ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
            writer.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_TARGET, metadata_block,
                                 sizeof(metadata_block)) ==
                os::kernel::fs::RootJournalV2Status::Succeeded &&
            writer.Commit(OS_TEST_ROOT_JOURNAL_V2_CRASH_COMMIT_TIME) ==
                os::kernel::fs::RootJournalV2Status::Succeeded;
        device.Crash();
        device.SetFailureOrdinal(
            crash_point % OS_TEST_ROOT_JOURNAL_V2_RECOVERY_FAILURE_ORDINAL_SPAN + 1ULL);
        os::kernel::fs::RootJournalV2 interrupted_recovery{};
        os::kernel::fs::RootJournalV2RecoveryResult recovery_result{};
        recovery_crash_valid =
            recovery_crash_valid && InitializeJournal(interrupted_recovery, device) ==
                                        os::kernel::fs::RootJournalV2Status::Succeeded;
        if (recovery_crash_valid) {
            static_cast<void>(interrupted_recovery.Recover(recovery_result));
        }
        device.Crash();

        os::kernel::fs::RootJournalV2 final_recovery{};
        recovery_crash_valid = recovery_crash_valid &&
                               InitializeJournal(final_recovery, device) ==
                                   os::kernel::fs::RootJournalV2Status::Succeeded &&
                               final_recovery.Recover(recovery_result) ==
                                   os::kernel::fs::RootJournalV2Status::Succeeded;
        device.ReadDurableFileSystemBlock(OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_TARGET, observed);
        recovery_crash_valid =
            recovery_crash_valid &&
            BlockHasPattern(observed, OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_PATTERN);
        device.Crash();
        os::kernel::fs::RootJournalV2 idempotence{};
        recovery_crash_valid =
            recovery_crash_valid &&
            InitializeJournal(idempotence, device) ==
                os::kernel::fs::RootJournalV2Status::Succeeded &&
            idempotence.Recover(recovery_result) ==
                os::kernel::fs::RootJournalV2Status::Succeeded &&
            recovery_result == os::kernel::fs::RootJournalV2RecoveryResult::Clean;
    }
    context.Expect(recovery_crash_valid, OS_TEST_ROOT_JOURNAL_V2_CRASH_MESSAGE);

    os::test::RootJournalV2TestDevice corruption_device{};
    os::kernel::fs::RootJournalV2 corruption_writer{};
    bool corruption_rejected =
        InitializeJournal(corruption_writer, corruption_device) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        corruption_writer.Format(OS_TEST_ROOT_JOURNAL_V2_CRASH_CREATION_TIME) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        corruption_writer.Begin(1ULL, 0ULL, 0ULL) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        corruption_writer.StageMetadata(OS_TEST_ROOT_JOURNAL_V2_CRASH_METADATA_TARGET,
                                        metadata_block, sizeof(metadata_block)) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        corruption_writer.Commit(OS_TEST_ROOT_JOURNAL_V2_CRASH_COMMIT_TIME) ==
            os::kernel::fs::RootJournalV2Status::Succeeded;
    corruption_device.Crash();
    const uint64_t payload_relative_block =
        os::test::OS_TEST_ROOT_JOURNAL_V2_JOURNAL_START_RELATIVE_BLOCK +
        os::kernel::fs::RootJournalV2SlotStartRelativeBlock(0ULL) +
        os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK;
    const uint64_t payload_first_sector =
        os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_START_LBA +
        payload_relative_block * os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK;
    corruption_device.XorDurableSectorByte(payload_first_sector, 0ULL,
                                           OS_TEST_ROOT_JOURNAL_V2_CRASH_CORRUPTION_MASK);
    os::kernel::fs::RootJournalV2 corruption_reader{};
    os::kernel::fs::RootJournalV2RecoveryResult corruption_result{};
    corruption_rejected = corruption_rejected &&
                          InitializeJournal(corruption_reader, corruption_device) ==
                              os::kernel::fs::RootJournalV2Status::Succeeded &&
                          corruption_reader.Recover(corruption_result) ==
                              os::kernel::fs::RootJournalV2Status::Corrupt;
    context.Expect(corruption_rejected, OS_TEST_ROOT_JOURNAL_V2_CORRUPTION_MESSAGE);
    return context.ExitCode();
}
