#include <os/kernel/fs/root_journal_v2.hpp>
#include <root_journal_v2_test_device.hpp>
#include <test_context.hpp>

#include <array>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_RANDOM_SUITE_NAME =
    "kernel/root_journal_v2/randomized";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_RANDOM_MESSAGE =
    "十万步 metadata/ordered/revoke/abort/commit/checkpoint 必须与独立 home oracle 一致";
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_SEED = 0x4A563252414E4431ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_LEFT_SHIFT = 13ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_RIGHT_SHIFT = 7ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_FINAL_LEFT_SHIFT = 17ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_OPERATION_MODULUS = 1000ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_THRESHOLD = 40ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_THRESHOLD = 50ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_REVOKE_THRESHOLD = 60ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_ABORT_THRESHOLD = 70ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_COMMIT_THRESHOLD = 71ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT = 16ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT = 8ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_CREDIT_COUNT = 8ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_CREDIT_COUNT = 4ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_REVOKE_CREDIT_COUNT = 8ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_OBSERVATION_INTERVAL = 64ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_BASE = 512ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_BASE = 768ULL;
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_JOURNAL_V2_RANDOM_UUID{
    .low = 0x1020304050607080ULL,
    .high = 0x8070605040302010ULL,
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << OS_TEST_ROOT_JOURNAL_V2_RANDOM_LEFT_SHIFT;
    state ^= state >> OS_TEST_ROOT_JOURNAL_V2_RANDOM_RIGHT_SHIFT;
    state ^= state << OS_TEST_ROOT_JOURNAL_V2_RANDOM_FINAL_LEFT_SHIFT;
    return state;
}

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

void ResetTransactionModel(
    std::array<bool, OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT> &metadata_staged,
    std::array<bool, OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT> &ordered_staged,
    std::array<bool, OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT> &revoked) noexcept {
    metadata_staged.fill(false);
    ordered_staged.fill(false);
    revoked.fill(false);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_JOURNAL_V2_RANDOM_SUITE_NAME};
    os::test::RootJournalV2TestDevice device{};
    os::kernel::fs::RootJournalV2 journal{};
    bool consistent =
        journal.Initialize(device, os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_START_LBA,
                           os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_BLOCK_COUNT,
                           os::test::OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_INODE_COUNT,
                           os::test::OS_TEST_ROOT_JOURNAL_V2_JOURNAL_START_RELATIVE_BLOCK,
                           OS_TEST_ROOT_JOURNAL_V2_RANDOM_UUID) ==
            os::kernel::fs::RootJournalV2Status::Succeeded &&
        journal.Format(1ULL) == os::kernel::fs::RootJournalV2Status::Succeeded;
    std::array<uint8_t, OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT> metadata_home{};
    std::array<uint8_t, OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT> ordered_home{};
    std::array<uint8_t, OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT> metadata_pending{};
    std::array<uint8_t, OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT> ordered_pending{};
    std::array<bool, OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT> metadata_staged{};
    std::array<bool, OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT> ordered_staged{};
    std::array<bool, OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT> revoked{};
    uint64_t metadata_staged_count = 0ULL;
    uint64_t ordered_staged_count = 0ULL;
    uint64_t revoke_count = 0ULL;
    uint64_t random_state = OS_TEST_ROOT_JOURNAL_V2_RANDOM_SEED;
    uint8_t block[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};

    for (uint64_t step = 0ULL; consistent && step < OS_TEST_ROOT_JOURNAL_V2_RANDOM_STEP_COUNT;
         ++step) {
        if (!journal.IsActive()) {
            consistent = journal.Begin(OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_CREDIT_COUNT,
                                       OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_CREDIT_COUNT,
                                       OS_TEST_ROOT_JOURNAL_V2_RANDOM_REVOKE_CREDIT_COUNT) ==
                         os::kernel::fs::RootJournalV2Status::Succeeded;
            ResetTransactionModel(metadata_staged, ordered_staged, revoked);
            metadata_staged_count = 0ULL;
            ordered_staged_count = 0ULL;
            revoke_count = 0ULL;
        }
        const uint64_t operation =
            NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_V2_RANDOM_OPERATION_MODULUS;
        if (operation < OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_THRESHOLD) {
            const uint64_t target_index =
                NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT;
            const uint8_t pattern = static_cast<uint8_t>(NextRandom(random_state));
            FillBlock(block, pattern);
            const os::kernel::fs::RootJournalV2Status status = journal.StageMetadata(
                OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_BASE + target_index, block,
                sizeof(block));
            if (status == os::kernel::fs::RootJournalV2Status::Succeeded) {
                if (!metadata_staged[target_index]) {
                    ++metadata_staged_count;
                }
                if (revoked[target_index]) {
                    revoked[target_index] = false;
                    --revoke_count;
                }
                metadata_staged[target_index] = true;
                metadata_pending[target_index] = pattern;
            } else {
                consistent =
                    status == os::kernel::fs::RootJournalV2Status::CapacityExhausted &&
                    metadata_staged_count == OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_CREDIT_COUNT;
            }
        } else if (operation < OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_THRESHOLD) {
            const uint64_t target_index =
                NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT;
            const uint8_t pattern = static_cast<uint8_t>(NextRandom(random_state));
            FillBlock(block, pattern);
            const os::kernel::fs::RootJournalV2Status status = journal.StageOrderedData(
                OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_BASE + target_index, block,
                sizeof(block));
            if (status == os::kernel::fs::RootJournalV2Status::Succeeded) {
                if (!ordered_staged[target_index]) {
                    ++ordered_staged_count;
                }
                ordered_staged[target_index] = true;
                ordered_pending[target_index] = pattern;
            } else {
                consistent =
                    status == os::kernel::fs::RootJournalV2Status::CapacityExhausted &&
                    ordered_staged_count == OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_CREDIT_COUNT;
            }
        } else if (operation < OS_TEST_ROOT_JOURNAL_V2_RANDOM_REVOKE_THRESHOLD) {
            const uint64_t target_index =
                NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT;
            const os::kernel::fs::RootJournalV2Status status =
                journal.Revoke(OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_BASE + target_index);
            if (status == os::kernel::fs::RootJournalV2Status::Succeeded) {
                if (metadata_staged[target_index]) {
                    metadata_staged[target_index] = false;
                    --metadata_staged_count;
                }
                if (!revoked[target_index]) {
                    revoked[target_index] = true;
                    ++revoke_count;
                }
            } else {
                consistent = status == os::kernel::fs::RootJournalV2Status::CapacityExhausted &&
                             revoke_count == OS_TEST_ROOT_JOURNAL_V2_RANDOM_REVOKE_CREDIT_COUNT;
            }
        } else if (operation < OS_TEST_ROOT_JOURNAL_V2_RANDOM_ABORT_THRESHOLD) {
            consistent = journal.Abort() == os::kernel::fs::RootJournalV2Status::Succeeded;
            ResetTransactionModel(metadata_staged, ordered_staged, revoked);
        } else if (operation < OS_TEST_ROOT_JOURNAL_V2_RANDOM_COMMIT_THRESHOLD) {
            if (metadata_staged_count == 0ULL && revoke_count == 0ULL) {
                consistent = journal.Abort() == os::kernel::fs::RootJournalV2Status::Succeeded;
            } else {
                consistent =
                    journal.Commit(step + 1ULL) == os::kernel::fs::RootJournalV2Status::Succeeded &&
                    journal.CheckpointOldest() == os::kernel::fs::RootJournalV2Status::Succeeded;
                if (consistent) {
                    for (uint64_t target_index = 0ULL;
                         target_index < OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT;
                         ++target_index) {
                        if (metadata_staged[target_index]) {
                            metadata_home[target_index] = metadata_pending[target_index];
                        }
                    }
                    for (uint64_t target_index = 0ULL;
                         target_index < OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT;
                         ++target_index) {
                        if (ordered_staged[target_index]) {
                            ordered_home[target_index] = ordered_pending[target_index];
                        }
                    }
                }
            }
            ResetTransactionModel(metadata_staged, ordered_staged, revoked);
        }

        if (step % OS_TEST_ROOT_JOURNAL_V2_RANDOM_OBSERVATION_INTERVAL == 0ULL) {
            const bool observe_metadata = (NextRandom(random_state) & 1ULL) == 0ULL;
            if (observe_metadata) {
                const uint64_t target_index =
                    NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT;
                device.ReadDurableFileSystemBlock(
                    OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_BASE + target_index, block);
                consistent = consistent && BlockHasPattern(block, metadata_home[target_index]);
            } else {
                const uint64_t target_index =
                    NextRandom(random_state) % OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT;
                device.ReadDurableFileSystemBlock(
                    OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_BASE + target_index, block);
                consistent = consistent && BlockHasPattern(block, ordered_home[target_index]);
            }
        }
    }
    if (consistent && journal.IsActive()) {
        consistent = journal.Abort() == os::kernel::fs::RootJournalV2Status::Succeeded;
    }
    for (uint64_t target_index = 0ULL;
         consistent && target_index < OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_COUNT;
         ++target_index) {
        device.ReadDurableFileSystemBlock(
            OS_TEST_ROOT_JOURNAL_V2_RANDOM_METADATA_TARGET_BASE + target_index, block);
        consistent = BlockHasPattern(block, metadata_home[target_index]);
    }
    for (uint64_t target_index = 0ULL;
         consistent && target_index < OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_COUNT;
         ++target_index) {
        device.ReadDurableFileSystemBlock(
            OS_TEST_ROOT_JOURNAL_V2_RANDOM_ORDERED_TARGET_BASE + target_index, block);
        consistent = BlockHasPattern(block, ordered_home[target_index]);
    }
    const os::kernel::fs::RootJournalV2Statistics statistics = journal.Statistics();
    consistent = consistent && statistics.transaction_begin_count != 0ULL &&
                 statistics.transaction_commit_count != 0ULL &&
                 statistics.checkpoint_transaction_count == statistics.transaction_commit_count;
    context.ExpectRandom(consistent, OS_TEST_ROOT_JOURNAL_V2_RANDOM_MESSAGE,
                         OS_TEST_ROOT_JOURNAL_V2_RANDOM_SEED,
                         OS_TEST_ROOT_JOURNAL_V2_RANDOM_STEP_COUNT);
    return context.ExitCode();
}
