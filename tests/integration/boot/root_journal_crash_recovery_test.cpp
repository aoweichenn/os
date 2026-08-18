#include "os/kernel/fs/root_journal.hpp"
#include "root_journal_test_device.hpp"
#include "test_context.hpp"

#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_JOURNAL_CRASH_SUITE_NAME =
    "kernel/root_journal_crash_recovery/integration";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_CRASH_EXPECTATION =
    "每个断电点恢复后必须是完整旧状态或完整新状态且二次 replay 幂等";
constexpr uint64_t OS_TEST_ROOT_JOURNAL_CRASH_POINT_COUNT = 1000ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_CRASH_MAXIMUM_ENTRY_COUNT =
    os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_MAXIMUM_CREDIT_COUNT;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_CRASH_WRITE_FAILURE_MODULUS = 512ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_CRASH_FLUSH_FAILURE_MODULUS = 8ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_CRASH_FIRST_SEQUENCE = 100ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_CRASH_FIRST_TARGET =
    os::kernel::fs::OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK + 64ULL;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_CRASH_OLD_PATTERN_BASE = 0x10U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_CRASH_NEW_PATTERN_BASE = 0x80U;

void FillBlock(uint8_t *const block, const uint8_t pattern) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
         ++byte_index) {
        block[byte_index] = pattern;
    }
}

[[nodiscard]] bool BlockHasPattern(const uint8_t *const block, const uint8_t pattern) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
         ++byte_index) {
        if (block[byte_index] != pattern) {
            return false;
        }
    }
    return true;
}

void RecordExpectation(os::test::TestContext &context, const bool condition) noexcept {
    context.Expect(condition, OS_TEST_ROOT_JOURNAL_CRASH_EXPECTATION);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_JOURNAL_CRASH_SUITE_NAME};

    for (uint64_t crash_point = 0ULL; crash_point < OS_TEST_ROOT_JOURNAL_CRASH_POINT_COUNT;
         ++crash_point) {
        os::test::RootJournalTestDevice device{};
        const uint64_t entry_count =
            crash_point % OS_TEST_ROOT_JOURNAL_CRASH_MAXIMUM_ENTRY_COUNT + 1ULL;
        uint8_t block[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
        for (uint64_t entry_index = 0ULL; entry_index < entry_count; ++entry_index) {
            FillBlock(block, static_cast<uint8_t>(OS_TEST_ROOT_JOURNAL_CRASH_OLD_PATTERN_BASE +
                                                  entry_index));
            RecordExpectation(context,
                              device.WriteBlock(os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
                                                    OS_TEST_ROOT_JOURNAL_CRASH_FIRST_TARGET +
                                                    entry_index,
                                                block, sizeof(block)) ==
                                  os::kernel::FileSystemBlockDeviceStatus::Succeeded);
        }
        device.ResetOperationCounts();
        if ((crash_point & 1ULL) == 0ULL) {
            device.SetWriteFailureOrdinal(
                crash_point % OS_TEST_ROOT_JOURNAL_CRASH_WRITE_FAILURE_MODULUS + 1ULL);
        } else {
            device.SetFlushFailureOrdinal(
                crash_point % OS_TEST_ROOT_JOURNAL_CRASH_FLUSH_FAILURE_MODULUS + 1ULL);
        }

        os::kernel::fs::RootJournal journal{};
        RecordExpectation(context,
                          journal.Initialize(device, os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                                  os::kernel::fs::RootJournalStatus::Succeeded &&
                              journal.Begin(OS_TEST_ROOT_JOURNAL_CRASH_FIRST_SEQUENCE + crash_point,
                                            entry_count) ==
                                  os::kernel::fs::RootJournalStatus::Succeeded);
        for (uint64_t entry_index = 0ULL; entry_index < entry_count; ++entry_index) {
            FillBlock(block, static_cast<uint8_t>(OS_TEST_ROOT_JOURNAL_CRASH_NEW_PATTERN_BASE +
                                                  entry_index));
            RecordExpectation(context,
                              journal.Stage(OS_TEST_ROOT_JOURNAL_CRASH_FIRST_TARGET + entry_index,
                                            block, sizeof(block)) ==
                                  os::kernel::fs::RootJournalStatus::Succeeded);
        }
        static_cast<void>(journal.Commit());
        device.ClearFailures();

        os::kernel::fs::RootJournal recovery_journal{};
        os::kernel::fs::RootJournalRecoveryResult recovery_result{};
        RecordExpectation(context, recovery_journal.Initialize(
                                       device, os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                                           os::kernel::fs::RootJournalStatus::Succeeded &&
                                       recovery_journal.Recover(recovery_result) ==
                                           os::kernel::fs::RootJournalStatus::Succeeded);

        bool all_old = true;
        bool all_new = true;
        for (uint64_t entry_index = 0ULL; entry_index < entry_count; ++entry_index) {
            RecordExpectation(
                context, device.ReadBlock(os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
                                              OS_TEST_ROOT_JOURNAL_CRASH_FIRST_TARGET + entry_index,
                                          block, sizeof(block)) ==
                             os::kernel::FileSystemBlockDeviceStatus::Succeeded);
            all_old = all_old &&
                      BlockHasPattern(
                          block, static_cast<uint8_t>(OS_TEST_ROOT_JOURNAL_CRASH_OLD_PATTERN_BASE +
                                                      entry_index));
            all_new = all_new &&
                      BlockHasPattern(
                          block, static_cast<uint8_t>(OS_TEST_ROOT_JOURNAL_CRASH_NEW_PATTERN_BASE +
                                                      entry_index));
        }
        RecordExpectation(context, all_old || all_new);

        os::kernel::fs::RootJournal idempotence_journal{};
        os::kernel::fs::RootJournalRecoveryResult idempotence_result{};
        RecordExpectation(
            context,
            idempotence_journal.Initialize(device, os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                    os::kernel::fs::RootJournalStatus::Succeeded &&
                idempotence_journal.Recover(idempotence_result) ==
                    os::kernel::fs::RootJournalStatus::Succeeded &&
                idempotence_result == os::kernel::fs::RootJournalRecoveryResult::Clean);
    }

    return context.ExitCode();
}
