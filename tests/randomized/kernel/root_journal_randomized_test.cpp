#include "os/kernel/fs/root_journal.hpp"
#include "root_journal_test_device.hpp"
#include "test_context.hpp"

#include <array>
#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_JOURNAL_RANDOM_SUITE_NAME =
    "kernel/root_journal/randomized";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_RANDOM_EXPECTATION =
    "十万步模型中的 durable block 必须只在 commit 后变化";
constexpr uint64_t OS_TEST_ROOT_JOURNAL_RANDOM_SEED = 0x4A4F55524E414C31ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_RANDOM_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_RANDOM_BLOCK_COUNT = 32ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_RANDOM_FIRST_TARGET =
    os::kernel::fs::OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK + 256ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_RANDOM_STAGE_THRESHOLD = 70ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_RANDOM_ABORT_THRESHOLD = 85ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_RANDOM_PERCENT_MODULUS = 100ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_RANDOM_CREDIT_COUNT = 16ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << 13ULL;
    state ^= state >> 7ULL;
    state ^= state << 17ULL;
    return state;
}

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
    context.Expect(condition, OS_TEST_ROOT_JOURNAL_RANDOM_EXPECTATION);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_JOURNAL_RANDOM_SUITE_NAME};
    os::test::RootJournalTestDevice device{};
    os::kernel::fs::RootJournal journal{};
    RecordExpectation(context,
                      journal.Initialize(device, os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA) ==
                          os::kernel::fs::RootJournalStatus::Succeeded);

    std::array<uint8_t, OS_TEST_ROOT_JOURNAL_RANDOM_BLOCK_COUNT> durable_patterns{};
    std::array<uint8_t, OS_TEST_ROOT_JOURNAL_RANDOM_BLOCK_COUNT> staged_patterns{};
    std::array<bool, OS_TEST_ROOT_JOURNAL_RANDOM_BLOCK_COUNT> staged{};
    uint64_t random_state = OS_TEST_ROOT_JOURNAL_RANDOM_SEED;
    uint64_t sequence = 1ULL;
    uint8_t block[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};

    for (uint64_t step = 0ULL; step < OS_TEST_ROOT_JOURNAL_RANDOM_STEP_COUNT; ++step) {
        if (!journal.IsActive()) {
            ++sequence;
            RecordExpectation(context,
                              journal.Begin(sequence, OS_TEST_ROOT_JOURNAL_RANDOM_CREDIT_COUNT) ==
                                  os::kernel::fs::RootJournalStatus::Succeeded);
            staged.fill(false);
        }

        const uint64_t operation =
            NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_RANDOM_PERCENT_MODULUS;
        if (operation < OS_TEST_ROOT_JOURNAL_RANDOM_STAGE_THRESHOLD) {
            const uint64_t block_index =
                NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_RANDOM_BLOCK_COUNT;
            const uint8_t pattern = static_cast<uint8_t>(NextRandom(random_state));
            FillBlock(block, pattern);
            const os::kernel::fs::RootJournalStatus status = journal.Stage(
                OS_TEST_ROOT_JOURNAL_RANDOM_FIRST_TARGET + block_index, block, sizeof(block));
            if (status == os::kernel::fs::RootJournalStatus::Succeeded) {
                staged_patterns[block_index] = pattern;
                staged[block_index] = true;
            } else {
                RecordExpectation(context,
                                  status == os::kernel::fs::RootJournalStatus::CreditsExhausted);
            }
        } else if (operation < OS_TEST_ROOT_JOURNAL_RANDOM_ABORT_THRESHOLD) {
            RecordExpectation(context,
                              journal.Abort() == os::kernel::fs::RootJournalStatus::Succeeded);
            staged.fill(false);
        } else if (journal.StagedBlockCount() == 0ULL) {
            RecordExpectation(context,
                              journal.Abort() == os::kernel::fs::RootJournalStatus::Succeeded);
            staged.fill(false);
        } else {
            RecordExpectation(context,
                              journal.Commit() == os::kernel::fs::RootJournalStatus::Succeeded);
            for (uint64_t block_index = 0ULL; block_index < OS_TEST_ROOT_JOURNAL_RANDOM_BLOCK_COUNT;
                 ++block_index) {
                if (staged[block_index]) {
                    durable_patterns[block_index] = staged_patterns[block_index];
                }
            }
            staged.fill(false);
        }

        const uint64_t observed_index =
            NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_RANDOM_BLOCK_COUNT;
        RecordExpectation(context, device.ReadBlock(os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
                                                        OS_TEST_ROOT_JOURNAL_RANDOM_FIRST_TARGET +
                                                        observed_index,
                                                    block, sizeof(block)) ==
                                           os::kernel::FileSystemBlockDeviceStatus::Succeeded &&
                                       BlockHasPattern(block, durable_patterns[observed_index]));
    }

    if (journal.IsActive()) {
        RecordExpectation(context, journal.Abort() == os::kernel::fs::RootJournalStatus::Succeeded);
    }
    return context.ExitCode();
}
