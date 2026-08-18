#include "os/kernel/fs/root_journal.hpp"
#include "root_journal_test_device.hpp"
#include "test_context.hpp"

#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_JOURNAL_SUITE_NAME = "kernel/root_journal/unit";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_EXPECTATION =
    "journal 单元状态、credit、校验和与清理结果必须符合契约";
constexpr uint64_t OS_TEST_ROOT_JOURNAL_SEQUENCE = 7ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_CREDIT_COUNT = 3ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_FIRST_TARGET =
    os::kernel::fs::OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_SECOND_TARGET = OS_TEST_ROOT_JOURNAL_FIRST_TARGET + 1ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_THIRD_TARGET = OS_TEST_ROOT_JOURNAL_FIRST_TARGET + 2ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_FOURTH_TARGET = OS_TEST_ROOT_JOURNAL_FIRST_TARGET + 3ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_COMMIT_WRITE_ORDINAL = 12ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_CHECKPOINT_WRITE_ORDINAL = 12ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_FIRST_JOURNAL_PAYLOAD_RELATIVE_BLOCK =
    os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK + 1ULL +
    os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_DESCRIPTOR_BLOCK_COUNT;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_FIRST_PATTERN = 0x31U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_SECOND_PATTERN = 0xA7U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_CORRUPTION_MASK = 0x01U;

void FillBlock(uint8_t *const block, const uint8_t pattern) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
         ++byte_index) {
        block[byte_index] = pattern;
    }
}

[[nodiscard]] bool BlocksEqual(const uint8_t *const left, const uint8_t *const right) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

void RecordExpectation(os::test::TestContext &context, const bool condition) noexcept {
    context.Expect(condition, OS_TEST_ROOT_JOURNAL_EXPECTATION);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_JOURNAL_SUITE_NAME};
    os::test::RootJournalTestDevice device{};
    os::kernel::fs::RootJournal journal{};
    os::kernel::fs::RootJournalRecoveryResult recovery_result{};
    RecordExpectation(context,
                      journal.Initialize(device, os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                              os::kernel::fs::RootJournalStatus::Succeeded &&
                          journal.Recover(recovery_result) ==
                              os::kernel::fs::RootJournalStatus::Succeeded &&
                          recovery_result == os::kernel::fs::RootJournalRecoveryResult::Clean);

    RecordExpectation(
        context,
        journal.Begin(OS_TEST_ROOT_JOURNAL_SEQUENCE, 0ULL) ==
                os::kernel::fs::RootJournalStatus::CreditsExhausted &&
            journal.Begin(OS_TEST_ROOT_JOURNAL_SEQUENCE,
                          os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT + 1ULL) ==
                os::kernel::fs::RootJournalStatus::CreditsExhausted);

    uint8_t first_block[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    uint8_t second_block[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    uint8_t observed_block[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    FillBlock(first_block, OS_TEST_ROOT_JOURNAL_FIRST_PATTERN);
    FillBlock(second_block, OS_TEST_ROOT_JOURNAL_SECOND_PATTERN);
    RecordExpectation(
        context,
        journal.Begin(OS_TEST_ROOT_JOURNAL_SEQUENCE, OS_TEST_ROOT_JOURNAL_CREDIT_COUNT) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_FIRST_TARGET, first_block, sizeof(first_block)) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_FIRST_TARGET, second_block, sizeof(second_block)) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_SECOND_TARGET, first_block, sizeof(first_block)) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_THIRD_TARGET, first_block, sizeof(first_block)) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_FOURTH_TARGET, first_block, sizeof(first_block)) ==
                os::kernel::fs::RootJournalStatus::CreditsExhausted &&
            journal.StagedBlockCount() == OS_TEST_ROOT_JOURNAL_CREDIT_COUNT &&
            journal.TryReadStaged(OS_TEST_ROOT_JOURNAL_FIRST_TARGET, observed_block,
                                  sizeof(observed_block)) &&
            BlocksEqual(observed_block, second_block) &&
            journal.Abort() == os::kernel::fs::RootJournalStatus::Succeeded && !journal.IsActive());

    RecordExpectation(
        context,
        journal.Begin(OS_TEST_ROOT_JOURNAL_SEQUENCE + 1ULL, 2ULL) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_FIRST_TARGET, first_block, sizeof(first_block)) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_SECOND_TARGET, second_block, sizeof(second_block)) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Commit() == os::kernel::fs::RootJournalStatus::Succeeded &&
            device.ReadBlock(os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
                                 OS_TEST_ROOT_JOURNAL_FIRST_TARGET,
                             observed_block, sizeof(observed_block)) ==
                os::kernel::FileSystemBlockDeviceStatus::Succeeded &&
            BlocksEqual(observed_block, first_block));

    device.ResetOperationCounts();
    device.SetWriteFailureOrdinal(OS_TEST_ROOT_JOURNAL_COMMIT_WRITE_ORDINAL);
    RecordExpectation(
        context,
        journal.Begin(OS_TEST_ROOT_JOURNAL_SEQUENCE + 2ULL, 2ULL) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_FIRST_TARGET, second_block, sizeof(second_block)) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Stage(OS_TEST_ROOT_JOURNAL_SECOND_TARGET, first_block, sizeof(first_block)) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            journal.Commit() == os::kernel::fs::RootJournalStatus::DeviceWriteFailed);
    device.ClearFailures();
    os::kernel::fs::RootJournal recovery_journal{};
    RecordExpectation(
        context,
        recovery_journal.Initialize(device, os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            recovery_journal.Recover(recovery_result) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            recovery_result == os::kernel::fs::RootJournalRecoveryResult::DiscardedIncomplete);

    os::test::RootJournalTestDevice payload_corruption_device{};
    os::kernel::fs::RootJournal payload_corruption_writer{};
    RecordExpectation(
        context, payload_corruption_writer.Initialize(payload_corruption_device,
                                                      os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                         os::kernel::fs::RootJournalStatus::Succeeded &&
                     payload_corruption_writer.Begin(OS_TEST_ROOT_JOURNAL_SEQUENCE + 3ULL, 1ULL) ==
                         os::kernel::fs::RootJournalStatus::Succeeded &&
                     payload_corruption_writer.Stage(OS_TEST_ROOT_JOURNAL_FIRST_TARGET,
                                                     second_block, sizeof(second_block)) ==
                         os::kernel::fs::RootJournalStatus::Succeeded);
    payload_corruption_device.SetWriteFailureOrdinal(OS_TEST_ROOT_JOURNAL_CHECKPOINT_WRITE_ORDINAL);
    RecordExpectation(context, payload_corruption_writer.Commit() ==
                                   os::kernel::fs::RootJournalStatus::DeviceWriteFailed);
    payload_corruption_device.ClearFailures();
    payload_corruption_device.XorByte(os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
                                          OS_TEST_ROOT_JOURNAL_FIRST_JOURNAL_PAYLOAD_RELATIVE_BLOCK,
                                      0ULL, OS_TEST_ROOT_JOURNAL_CORRUPTION_MASK);
    os::kernel::fs::RootJournal payload_corruption_reader{};
    RecordExpectation(
        context, payload_corruption_reader.Initialize(payload_corruption_device,
                                                      os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                         os::kernel::fs::RootJournalStatus::Succeeded &&
                     payload_corruption_reader.Recover(recovery_result) ==
                         os::kernel::fs::RootJournalStatus::Corrupt);

    const uint64_t journal_header_lba =
        os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
        os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK;
    device.XorByte(journal_header_lba, 0ULL, OS_TEST_ROOT_JOURNAL_CORRUPTION_MASK);
    os::kernel::fs::RootJournal corrupt_journal{};
    RecordExpectation(
        context,
        corrupt_journal.Initialize(device, os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                os::kernel::fs::RootJournalStatus::Succeeded &&
            corrupt_journal.Recover(recovery_result) == os::kernel::fs::RootJournalStatus::Corrupt);

    return context.ExitCode();
}
