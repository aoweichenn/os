#include <os/kernel/fs/root_extent_tree.hpp>
#include <os/kernel/fs/root_journal_v2.hpp>
#include <root_journal_v2_test_device.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_EXTENT_JOURNAL_SUITE_NAME =
    "kernel/root_extent_journal_ordering/integration";
constexpr std::string_view OS_TEST_ROOT_EXTENT_JOURNAL_MESSAGE =
    "extent metadata 可恢复为新态时，对应 ordered data 必须已经稳定";
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_EXTENT_JOURNAL_UUID{
    .low = 0x4558544A4E4C4F52ULL,
    .high = 0x1020304050607080ULL,
};
constexpr uint64_t OS_TEST_ROOT_EXTENT_JOURNAL_CRASH_POINT_COUNT = 64ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_JOURNAL_FAILURE_ORDINAL_SPAN = 48ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_JOURNAL_METADATA_TARGET = 300ULL;
constexpr uint64_t OS_TEST_ROOT_EXTENT_JOURNAL_DATA_TARGET = 512ULL;
constexpr uint8_t OS_TEST_ROOT_EXTENT_JOURNAL_OLD_DATA_PATTERN = 0x11U;
constexpr uint8_t OS_TEST_ROOT_EXTENT_JOURNAL_NEW_DATA_PATTERN = 0xA7U;

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
                              OS_TEST_ROOT_EXTENT_JOURNAL_UUID);
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_EXTENT_JOURNAL_SUITE_NAME};
    bool ordered = true;
    uint8_t old_data[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t new_data[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t extent_node[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t observed[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    FillBlock(old_data, OS_TEST_ROOT_EXTENT_JOURNAL_OLD_DATA_PATTERN);
    FillBlock(new_data, OS_TEST_ROOT_EXTENT_JOURNAL_NEW_DATA_PATTERN);

    for (uint64_t crash_point = 0ULL;
         ordered && crash_point < OS_TEST_ROOT_EXTENT_JOURNAL_CRASH_POINT_COUNT; ++crash_point) {
        os::test::RootJournalV2TestDevice device{};
        os::kernel::fs::RootJournalV2 writer{};
        ordered =
            InitializeJournal(writer, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
            writer.Format(1ULL) == os::kernel::fs::RootJournalV2Status::Succeeded;
        device.WriteDurableFileSystemBlock(OS_TEST_ROOT_EXTENT_JOURNAL_DATA_TARGET, old_data);
        os::kernel::fs::RootExtentNode leaf{
            .tree_generation = 1ULL,
            .inode_number = 32ULL,
            .inode_generation = 1ULL,
            .depth = 0ULL,
            .entry_count = 1ULL,
            .file_system_uuid = OS_TEST_ROOT_EXTENT_JOURNAL_UUID,
            .entries = {},
        };
        leaf.entries[0] = os::kernel::fs::RootExtentNodeEntry{
            .logical_start_block = 0ULL,
            .physical_or_child_block = OS_TEST_ROOT_EXTENT_JOURNAL_DATA_TARGET,
            .block_count_or_generation = 1ULL,
            .state_or_covered_block_count =
                static_cast<uint64_t>(os::kernel::fs::RootExtentState::Initialized),
        };
        ordered = ordered && os::kernel::fs::EncodeRootExtentLeafNode(
                                 writer.Superblock(), leaf, extent_node, sizeof(extent_node)) ==
                                 os::kernel::fs::RootExtentStatus::Succeeded;
        device.ResetOperationCounts();
        device.SetFailureOrdinal(crash_point % OS_TEST_ROOT_EXTENT_JOURNAL_FAILURE_ORDINAL_SPAN +
                                 1ULL);
        if (ordered &&
            writer.Begin(1ULL, 1ULL, 0ULL) == os::kernel::fs::RootJournalV2Status::Succeeded) {
            ordered = writer.StageMetadata(OS_TEST_ROOT_EXTENT_JOURNAL_METADATA_TARGET, extent_node,
                                           sizeof(extent_node)) ==
                          os::kernel::fs::RootJournalV2Status::Succeeded &&
                      writer.StageOrderedData(OS_TEST_ROOT_EXTENT_JOURNAL_DATA_TARGET, new_data,
                                              sizeof(new_data)) ==
                          os::kernel::fs::RootJournalV2Status::Succeeded;
            if (ordered) {
                static_cast<void>(writer.Commit(crash_point + 1ULL));
            }
        }
        device.Crash();
        os::kernel::fs::RootJournalV2 recovery{};
        os::kernel::fs::RootJournalV2RecoveryResult recovery_result{};
        ordered =
            ordered &&
            InitializeJournal(recovery, device) == os::kernel::fs::RootJournalV2Status::Succeeded &&
            recovery.Recover(recovery_result) == os::kernel::fs::RootJournalV2Status::Succeeded;
        device.ReadDurableFileSystemBlock(OS_TEST_ROOT_EXTENT_JOURNAL_METADATA_TARGET, observed);
        if (ordered && !os::kernel::fs::RootV5BytesAreZero(observed, sizeof(observed))) {
            os::kernel::fs::RootExtentNode decoded{};
            ordered = os::kernel::fs::DecodeRootExtentLeafNode(recovery.Superblock(), observed,
                                                               sizeof(observed), decoded) ==
                      os::kernel::fs::RootExtentStatus::Succeeded;
            device.ReadDurableFileSystemBlock(OS_TEST_ROOT_EXTENT_JOURNAL_DATA_TARGET, observed);
            ordered =
                ordered && BlockHasPattern(observed, OS_TEST_ROOT_EXTENT_JOURNAL_NEW_DATA_PATTERN);
        }
    }
    context.Expect(ordered, OS_TEST_ROOT_EXTENT_JOURNAL_MESSAGE);
    return context.ExitCode();
}
