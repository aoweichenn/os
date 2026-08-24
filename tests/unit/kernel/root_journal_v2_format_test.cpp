#include <os/kernel/fs/root_journal_v2_format.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_FORMAT_SUITE_NAME =
    "kernel/root_journal_v2_format/unit";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_FORMAT_GEOMETRY_MESSAGE =
    "journal v2 必须形成固定 4 KiB、多槽与 64 位目标几何";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_FORMAT_RECORD_MESSAGE =
    "descriptor/revoke/commit/checkpoint 必须按小端 CRC32C 盘面无损往返";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_FORMAT_ORPHAN_MESSAGE =
    "orphan block 必须保持唯一 inode、幂等增删、计数和 checksum";
constexpr std::string_view OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MESSAGE =
    "journal v2 每类记录 checksum、feature 与重复目标损坏必须 fail closed";
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_JOURNAL_V2_FORMAT_UUID{
    .low = 0x0123456789ABCDEFULL,
    .high = 0xFEDCBA9876543210ULL,
};
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_TOTAL_BLOCK_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_INODE_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_START_BLOCK = 16ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_CREATION_TIME_NANOSECONDS = 123456789ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_TARGET = 512ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_SECOND_TARGET = 513ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_REVOKED_TARGET = 514ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_SEQUENCE = 7ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_TRANSACTION_GENERATION = 3ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_COMMIT_TIME_NANOSECONDS = 987654321ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_ORPHAN = 32ULL;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_SECOND_ORPHAN = 64ULL;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_PATTERN = 0x31U;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_SECOND_PATTERN = 0xA7U;
constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_OFFSET_BYTES = 64ULL;
constexpr uint8_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MASK = 0x40U;
constexpr uint32_t OS_TEST_ROOT_JOURNAL_V2_FORMAT_EXPECTED_SUPERBLOCK_CHECKSUM = 0xB797102EU;

void FillBlock(uint8_t *const block, const uint8_t pattern) noexcept {
    for (uint64_t byte_index = 0ULL;
         byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES; ++byte_index) {
        block[byte_index] = pattern;
    }
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_JOURNAL_V2_FORMAT_SUITE_NAME};
    os::kernel::fs::RootJournalV2Superblock superblock{};
    uint8_t superblock_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootJournalV2Superblock decoded_superblock{};
    const bool geometry_valid =
        os::kernel::fs::PlanRootJournalV2Superblock(
            os::kernel::fs::RootJournalV2FormatProfile{
                .file_system_total_block_count = OS_TEST_ROOT_JOURNAL_V2_FORMAT_TOTAL_BLOCK_COUNT,
                .file_system_inode_count = OS_TEST_ROOT_JOURNAL_V2_FORMAT_INODE_COUNT,
                .journal_start_relative_block = OS_TEST_ROOT_JOURNAL_V2_FORMAT_START_BLOCK,
                .creation_time_nanoseconds =
                    OS_TEST_ROOT_JOURNAL_V2_FORMAT_CREATION_TIME_NANOSECONDS,
                .file_system_uuid = OS_TEST_ROOT_JOURNAL_V2_FORMAT_UUID,
            },
            superblock) == os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        superblock.block_size_bytes == os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES &&
        superblock.journal_block_count == os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT &&
        superblock.slot_count == os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT &&
        os::kernel::fs::RootJournalV2SlotStartRelativeBlock(3ULL) == 61ULL &&
        !os::kernel::fs::RootJournalV2TargetIsValid(superblock,
                                                    OS_TEST_ROOT_JOURNAL_V2_FORMAT_START_BLOCK) &&
        os::kernel::fs::RootJournalV2TargetIsValid(superblock,
                                                   OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_TARGET) &&
        os::kernel::fs::EncodeRootJournalV2Superblock(superblock, superblock_bytes,
                                                      sizeof(superblock_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootJournalV2Superblock(superblock_bytes, sizeof(superblock_bytes),
                                                      decoded_superblock) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        decoded_superblock.file_system_inode_count == OS_TEST_ROOT_JOURNAL_V2_FORMAT_INODE_COUNT &&
        (static_cast<uint32_t>(
             superblock_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES]) |
         static_cast<uint32_t>(
             superblock_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES +
                              1ULL])
             << 8U |
         static_cast<uint32_t>(
             superblock_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES +
                              2ULL])
             << 16U |
         static_cast<uint32_t>(
             superblock_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES +
                              3ULL])
             << 24U) == OS_TEST_ROOT_JOURNAL_V2_FORMAT_EXPECTED_SUPERBLOCK_CHECKSUM;
    context.Expect(geometry_valid, OS_TEST_ROOT_JOURNAL_V2_FORMAT_GEOMETRY_MESSAGE);

    uint8_t first_payload[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t second_payload[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    FillBlock(first_payload, OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_PATTERN);
    FillBlock(second_payload, OS_TEST_ROOT_JOURNAL_V2_FORMAT_SECOND_PATTERN);
    os::kernel::fs::RootJournalV2Descriptor descriptor{
        .sequence = OS_TEST_ROOT_JOURNAL_V2_FORMAT_SEQUENCE,
        .slot_index = 0ULL,
        .metadata_block_count = 2ULL,
        .ordered_data_block_count = 1ULL,
        .revoke_count = 1ULL,
        .transaction_generation = OS_TEST_ROOT_JOURNAL_V2_FORMAT_TRANSACTION_GENERATION,
        .file_system_uuid = OS_TEST_ROOT_JOURNAL_V2_FORMAT_UUID,
        .tags = {},
    };
    descriptor.tags[0] = os::kernel::fs::RootJournalV2Tag{
        .target_relative_block = OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_TARGET,
        .payload_index = 0ULL,
        .payload_checksum =
            os::kernel::fs::CalculateRootV5Crc32c(first_payload, sizeof(first_payload)),
        .flags =
            static_cast<uint32_t>(os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_TAG_FLAGS),
    };
    descriptor.tags[1] = os::kernel::fs::RootJournalV2Tag{
        .target_relative_block = OS_TEST_ROOT_JOURNAL_V2_FORMAT_SECOND_TARGET,
        .payload_index = 1ULL,
        .payload_checksum =
            os::kernel::fs::CalculateRootV5Crc32c(second_payload, sizeof(second_payload)),
        .flags =
            static_cast<uint32_t>(os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_TAG_FLAGS),
    };
    os::kernel::fs::RootJournalV2RevokeBlock revoke_block{
        .sequence = descriptor.sequence,
        .slot_index = descriptor.slot_index,
        .revoke_count = descriptor.revoke_count,
        .file_system_uuid = OS_TEST_ROOT_JOURNAL_V2_FORMAT_UUID,
        .targets = {},
    };
    revoke_block.targets[0] = OS_TEST_ROOT_JOURNAL_V2_FORMAT_REVOKED_TARGET;
    uint8_t descriptor_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    uint8_t revoke_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootJournalV2Descriptor decoded_descriptor{};
    os::kernel::fs::RootJournalV2RevokeBlock decoded_revoke{};
    const bool descriptor_valid =
        os::kernel::fs::EncodeRootJournalV2Descriptor(superblock, descriptor, descriptor_bytes,
                                                      sizeof(descriptor_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootJournalV2Descriptor(
            superblock, descriptor_bytes, sizeof(descriptor_bytes), decoded_descriptor) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::EncodeRootJournalV2RevokeBlock(superblock, revoke_block, revoke_bytes,
                                                       sizeof(revoke_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootJournalV2RevokeBlock(superblock, revoke_bytes,
                                                       sizeof(revoke_bytes), decoded_revoke) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        decoded_descriptor.tags[1].target_relative_block ==
            OS_TEST_ROOT_JOURNAL_V2_FORMAT_SECOND_TARGET &&
        decoded_revoke.targets[0] == OS_TEST_ROOT_JOURNAL_V2_FORMAT_REVOKED_TARGET;

    const uint64_t checksum_offset =
        os::kernel::fs::OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES;
    const uint32_t descriptor_checksum =
        static_cast<uint32_t>(descriptor_bytes[checksum_offset]) |
        static_cast<uint32_t>(descriptor_bytes[checksum_offset + 1ULL]) << 8U |
        static_cast<uint32_t>(descriptor_bytes[checksum_offset + 2ULL]) << 16U |
        static_cast<uint32_t>(descriptor_bytes[checksum_offset + 3ULL]) << 24U;
    const uint32_t revoke_checksum =
        static_cast<uint32_t>(revoke_bytes[checksum_offset]) |
        static_cast<uint32_t>(revoke_bytes[checksum_offset + 1ULL]) << 8U |
        static_cast<uint32_t>(revoke_bytes[checksum_offset + 2ULL]) << 16U |
        static_cast<uint32_t>(revoke_bytes[checksum_offset + 3ULL]) << 24U;
    os::kernel::fs::RootJournalV2Commit commit{
        .sequence = descriptor.sequence,
        .slot_index = descriptor.slot_index,
        .metadata_block_count = descriptor.metadata_block_count,
        .ordered_data_block_count = descriptor.ordered_data_block_count,
        .revoke_count = descriptor.revoke_count,
        .transaction_generation = descriptor.transaction_generation,
        .commit_time_nanoseconds = OS_TEST_ROOT_JOURNAL_V2_FORMAT_COMMIT_TIME_NANOSECONDS,
        .descriptor_checksum = descriptor_checksum,
        .revoke_checksum = revoke_checksum,
        .file_system_uuid = OS_TEST_ROOT_JOURNAL_V2_FORMAT_UUID,
    };
    uint8_t commit_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootJournalV2Commit decoded_commit{};
    os::kernel::fs::RootJournalV2Checkpoint checkpoint{
        .sequence = commit.sequence,
        .slot_index = commit.slot_index,
        .checkpoint_generation = 4ULL,
        .commit_checksum = 0x10203040U,
        .file_system_uuid = OS_TEST_ROOT_JOURNAL_V2_FORMAT_UUID,
    };
    uint8_t checkpoint_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootJournalV2Checkpoint decoded_checkpoint{};
    const bool records_valid =
        descriptor_valid &&
        os::kernel::fs::EncodeRootJournalV2Commit(superblock, commit, commit_bytes,
                                                  sizeof(commit_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootJournalV2Commit(superblock, commit_bytes, sizeof(commit_bytes),
                                                  decoded_commit) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::EncodeRootJournalV2Checkpoint(superblock, checkpoint, checkpoint_bytes,
                                                      sizeof(checkpoint_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootJournalV2Checkpoint(
            superblock, checkpoint_bytes, sizeof(checkpoint_bytes), decoded_checkpoint) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        decoded_commit.commit_time_nanoseconds ==
            OS_TEST_ROOT_JOURNAL_V2_FORMAT_COMMIT_TIME_NANOSECONDS &&
        decoded_checkpoint.commit_checksum == checkpoint.commit_checksum;
    context.Expect(records_valid, OS_TEST_ROOT_JOURNAL_V2_FORMAT_RECORD_MESSAGE);

    os::kernel::fs::RootJournalV2OrphanBlock orphan_block{
        .generation = 1ULL,
        .entry_count = 0ULL,
        .file_system_uuid = OS_TEST_ROOT_JOURNAL_V2_FORMAT_UUID,
        .inode_numbers = {},
    };
    uint8_t orphan_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootJournalV2OrphanBlock decoded_orphan{};
    const bool orphan_valid =
        os::kernel::fs::AddRootJournalV2Orphan(superblock, orphan_block,
                                               OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_ORPHAN) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::AddRootJournalV2Orphan(superblock, orphan_block,
                                               OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_ORPHAN) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::AddRootJournalV2Orphan(superblock, orphan_block,
                                               OS_TEST_ROOT_JOURNAL_V2_FORMAT_SECOND_ORPHAN) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        orphan_block.entry_count == 2ULL &&
        os::kernel::fs::RemoveRootJournalV2Orphan(superblock, orphan_block,
                                                  OS_TEST_ROOT_JOURNAL_V2_FORMAT_FIRST_ORPHAN) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::EncodeRootJournalV2OrphanBlock(superblock, orphan_block, orphan_bytes,
                                                       sizeof(orphan_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootJournalV2OrphanBlock(superblock, orphan_bytes,
                                                       sizeof(orphan_bytes), decoded_orphan) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        decoded_orphan.entry_count == 1ULL &&
        decoded_orphan.inode_numbers[1] == OS_TEST_ROOT_JOURNAL_V2_FORMAT_SECOND_ORPHAN;
    os::kernel::fs::RootJournalV2OrphanBlock full_orphan{
        .generation = 1ULL,
        .entry_count = os::kernel::fs::OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY,
        .file_system_uuid = OS_TEST_ROOT_JOURNAL_V2_FORMAT_UUID,
        .inode_numbers = {},
    };
    for (uint64_t entry_index = 0ULL;
         entry_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY;
         ++entry_index) {
        full_orphan.inode_numbers[entry_index] =
            os::kernel::fs::OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER + entry_index;
    }
    const bool orphan_boundary_valid =
        os::kernel::fs::EncodeRootJournalV2OrphanBlock(superblock, full_orphan, orphan_bytes,
                                                       sizeof(orphan_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::Succeeded &&
        os::kernel::fs::AddRootJournalV2Orphan(superblock, full_orphan, 1024ULL) ==
            os::kernel::fs::RootJournalV2FormatStatus::CapacityExhausted;
    context.Expect(orphan_valid && orphan_boundary_valid,
                   OS_TEST_ROOT_JOURNAL_V2_FORMAT_ORPHAN_MESSAGE);

    superblock_bytes[OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MASK;
    descriptor_bytes[OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MASK;
    revoke_bytes[OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MASK;
    commit_bytes[OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MASK;
    checkpoint_bytes[OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MASK;
    orphan_bytes[OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MASK;
    os::kernel::fs::RootJournalV2Superblock unknown_required = superblock;
    unknown_required.incompatible_features |= 1ULL << 63ULL;
    os::kernel::fs::RootJournalV2Descriptor duplicate_descriptor = descriptor;
    duplicate_descriptor.tags[1].target_relative_block =
        duplicate_descriptor.tags[0].target_relative_block;
    const bool corruption_rejected =
        os::kernel::fs::DecodeRootJournalV2Superblock(superblock_bytes, sizeof(superblock_bytes),
                                                      decoded_superblock) ==
            os::kernel::fs::RootJournalV2FormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootJournalV2Descriptor(
            superblock, descriptor_bytes, sizeof(descriptor_bytes), decoded_descriptor) ==
            os::kernel::fs::RootJournalV2FormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootJournalV2RevokeBlock(superblock, revoke_bytes,
                                                       sizeof(revoke_bytes), decoded_revoke) ==
            os::kernel::fs::RootJournalV2FormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootJournalV2Commit(superblock, commit_bytes, sizeof(commit_bytes),
                                                  decoded_commit) ==
            os::kernel::fs::RootJournalV2FormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootJournalV2Checkpoint(
            superblock, checkpoint_bytes, sizeof(checkpoint_bytes), decoded_checkpoint) ==
            os::kernel::fs::RootJournalV2FormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootJournalV2OrphanBlock(superblock, orphan_bytes,
                                                       sizeof(orphan_bytes), decoded_orphan) ==
            os::kernel::fs::RootJournalV2FormatStatus::InvalidChecksum &&
        os::kernel::fs::ValidateRootJournalV2Superblock(unknown_required) ==
            os::kernel::fs::RootJournalV2FormatStatus::UnsupportedRequiredFeature &&
        os::kernel::fs::EncodeRootJournalV2Descriptor(superblock, duplicate_descriptor,
                                                      descriptor_bytes, sizeof(descriptor_bytes)) ==
            os::kernel::fs::RootJournalV2FormatStatus::DuplicateTarget;
    context.Expect(corruption_rejected, OS_TEST_ROOT_JOURNAL_V2_FORMAT_CORRUPTION_MESSAGE);
    return context.ExitCode();
}
