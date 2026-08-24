#include <os/kernel/fs/root_journal_v2_format.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint8_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAGIC[] = {'O', 'S', 'J', 'V',
                                                                    '2', 'S', 'B', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_MAGIC[] = {'O', 'S', 'J', 'V',
                                                                    '2', 'D', 'S', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_MAGIC[] = {'O', 'S', 'J', 'V',
                                                                '2', 'R', 'V', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_MAGIC[] = {'O', 'S', 'J', 'V',
                                                                '2', 'C', 'M', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_MAGIC[] = {'O', 'S', 'J', 'V',
                                                                    '2', 'C', 'P', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_MAGIC[] = {'O', 'S', 'O', 'R',
                                                             'V', '0', '0', '2'};
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_INITIAL_SEQUENCE = 1ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_INITIAL_GENERATION = 1ULL;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_HEADER_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_BLOCK_SIZE_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SECTOR_SIZE_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_TOTAL_BLOCKS_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_INODE_COUNT_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_START_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_BLOCK_COUNT_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SLOT_COUNT_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SLOT_BLOCKS_OFFSET_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_METADATA_OFFSET_BYTES = 88ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_ORDERED_OFFSET_BYTES = 96ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_REVOKE_OFFSET_BYTES = 104ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_CHECKSUM_ALGORITHM_OFFSET_BYTES = 112ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_COMPAT_OFFSET_BYTES = 120ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_INCOMPAT_OFFSET_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_NEXT_SEQUENCE_OFFSET_BYTES = 136ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_LAST_CHECKPOINT_OFFSET_BYTES = 144ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_CREATION_TIME_OFFSET_BYTES = 152ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_GENERATION_OFFSET_BYTES = 160ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_UUID_LOW_OFFSET_BYTES = 168ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_UUID_HIGH_OFFSET_BYTES = 176ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_RESERVED_START_BYTES = 184ULL;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_METADATA_COUNT_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_ORDERED_COUNT_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_REVOKE_COUNT_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_GENERATION_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_UUID_LOW_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_UUID_HIGH_OFFSET_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_RESERVED_START_BYTES = 88ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_TARGET_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_PAYLOAD_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_CHECKSUM_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_FLAGS_OFFSET_BYTES = 20ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_RESERVED_OFFSET_BYTES = 24ULL;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_COUNT_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_UUID_LOW_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_UUID_HIGH_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_RESERVED_START_BYTES = 64ULL;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_METADATA_COUNT_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_ORDERED_COUNT_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_REVOKE_COUNT_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_GENERATION_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_TIME_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_DESCRIPTOR_CHECKSUM_OFFSET_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_REVOKE_CHECKSUM_OFFSET_BYTES = 84ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_UUID_LOW_OFFSET_BYTES = 88ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_UUID_HIGH_OFFSET_BYTES = 96ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RESERVED_START_BYTES = 104ULL;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_GENERATION_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_COMMIT_CHECKSUM_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_UUID_LOW_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_UUID_HIGH_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_RESERVED_START_BYTES = 72ULL;

constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_GENERATION_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_COUNT_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_UUID_LOW_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_UUID_HIGH_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_RESERVED_START_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRIES_START_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_RESERVED_TAIL_OFFSET_BYTES = 4088ULL;

void ClearBytes(uint8_t *const destination, const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < byte_count; ++byte_index) {
        destination[byte_index] = 0U;
    }
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < byte_count; ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < byte_count; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

void WriteU32(uint8_t *const destination, const uint64_t offset_bytes,
              const uint32_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        destination[offset_bytes + byte_index] =
            static_cast<uint8_t>((value >> (byte_index * 8ULL)) & 0xFFU);
    }
}

void WriteU64(uint8_t *const destination, const uint64_t offset_bytes,
              const uint64_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        destination[offset_bytes + byte_index] =
            static_cast<uint8_t>((value >> (byte_index * 8ULL)) & 0xFFULL);
    }
}

[[nodiscard]] uint32_t ReadU32(const uint8_t *const source, const uint64_t offset_bytes) noexcept {
    uint32_t value = 0U;
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        value |= static_cast<uint32_t>(source[offset_bytes + byte_index]) << (byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] uint64_t ReadU64(const uint8_t *const source, const uint64_t offset_bytes) noexcept {
    uint64_t value = 0ULL;
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        value |= static_cast<uint64_t>(source[offset_bytes + byte_index]) << (byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] bool UuidEqual(const RootV5Uuid left, const RootV5Uuid right) noexcept {
    return left.low == right.low && left.high == right.high;
}

[[nodiscard]] bool UuidValid(const RootV5Uuid uuid) noexcept {
    return uuid.low != 0ULL || uuid.high != 0ULL;
}

[[nodiscard]] bool BlockHeaderValid(const uint8_t *const block, const uint8_t *const magic,
                                    const uint64_t header_size_bytes) noexcept {
    return BytesEqual(block, magic, OS_KERNEL_ROOTFS_V5_JOURNAL_MAGIC_SIZE_BYTES) &&
           ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_VERSION_OFFSET_BYTES) ==
               OS_KERNEL_ROOTFS_V5_JOURNAL_FORMAT_VERSION &&
           ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_HEADER_SIZE_OFFSET_BYTES) ==
               header_size_bytes;
}

[[nodiscard]] bool BlockChecksumValid(const uint8_t *const block) noexcept {
    return ReadU32(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES) ==
           CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES);
}

void WriteHeader(uint8_t *const block, const uint8_t *const magic,
                 const uint64_t header_size_bytes) noexcept {
    CopyBytes(block, magic, OS_KERNEL_ROOTFS_V5_JOURNAL_MAGIC_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_VERSION_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_JOURNAL_FORMAT_VERSION);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_HEADER_SIZE_OFFSET_BYTES, header_size_bytes);
}

void WriteChecksum(uint8_t *const block) noexcept {
    WriteU32(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES,
             CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES));
}

[[nodiscard]] RootJournalV2FormatStatus
ValidateRecordIdentity(const RootJournalV2Superblock &superblock, const uint64_t sequence,
                       const uint64_t slot_index, const RootV5Uuid uuid) noexcept {
    if (sequence == 0ULL || sequence == UINT64_MAX) {
        return RootJournalV2FormatStatus::InvalidSequence;
    }
    if (slot_index >= superblock.slot_count) {
        return RootJournalV2FormatStatus::InvalidSlot;
    }
    return UuidEqual(uuid, superblock.file_system_uuid) ? RootJournalV2FormatStatus::Succeeded
                                                        : RootJournalV2FormatStatus::InvalidLayout;
}

[[nodiscard]] bool TagIsZero(const RootJournalV2Tag &tag) noexcept {
    return tag.target_relative_block == 0ULL && tag.payload_index == 0ULL &&
           tag.payload_checksum == 0U && tag.flags == 0U;
}

[[nodiscard]] RootJournalV2FormatStatus
ValidateDescriptor(const RootJournalV2Superblock &superblock,
                   const RootJournalV2Descriptor &descriptor) noexcept {
    RootJournalV2FormatStatus status = ValidateRecordIdentity(
        superblock, descriptor.sequence, descriptor.slot_index, descriptor.file_system_uuid);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    if (descriptor.metadata_block_count > superblock.maximum_metadata_block_count ||
        descriptor.ordered_data_block_count > superblock.maximum_ordered_data_block_count ||
        descriptor.revoke_count > superblock.maximum_revoke_count ||
        (descriptor.metadata_block_count == 0ULL && descriptor.revoke_count == 0ULL) ||
        descriptor.transaction_generation == 0ULL) {
        return RootJournalV2FormatStatus::InvalidCount;
    }
    for (uint64_t tag_index = 0ULL;
         tag_index < OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT; ++tag_index) {
        const RootJournalV2Tag &tag = descriptor.tags[tag_index];
        if (tag_index >= descriptor.metadata_block_count) {
            if (!TagIsZero(tag)) {
                return RootJournalV2FormatStatus::NonZeroReservedBytes;
            }
            continue;
        }
        if (!RootJournalV2TargetIsValid(superblock, tag.target_relative_block) ||
            tag.payload_index != tag_index ||
            tag.flags != OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_TAG_FLAGS) {
            return RootJournalV2FormatStatus::InvalidTarget;
        }
        for (uint64_t prior_index = 0ULL; prior_index < tag_index; ++prior_index) {
            if (descriptor.tags[prior_index].target_relative_block == tag.target_relative_block) {
                return RootJournalV2FormatStatus::DuplicateTarget;
            }
        }
    }
    return RootJournalV2FormatStatus::Succeeded;
}

[[nodiscard]] RootJournalV2FormatStatus
ValidateRevokeBlock(const RootJournalV2Superblock &superblock,
                    const RootJournalV2RevokeBlock &revoke_block) noexcept {
    RootJournalV2FormatStatus status = ValidateRecordIdentity(
        superblock, revoke_block.sequence, revoke_block.slot_index, revoke_block.file_system_uuid);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    if (revoke_block.revoke_count > superblock.maximum_revoke_count) {
        return RootJournalV2FormatStatus::InvalidCount;
    }
    for (uint64_t target_index = 0ULL;
         target_index < OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT; ++target_index) {
        const uint64_t target = revoke_block.targets[target_index];
        if (target_index >= revoke_block.revoke_count) {
            if (target != 0ULL) {
                return RootJournalV2FormatStatus::NonZeroReservedBytes;
            }
            continue;
        }
        if (!RootJournalV2TargetIsValid(superblock, target)) {
            return RootJournalV2FormatStatus::InvalidTarget;
        }
        for (uint64_t prior_index = 0ULL; prior_index < target_index; ++prior_index) {
            if (revoke_block.targets[prior_index] == target) {
                return RootJournalV2FormatStatus::DuplicateTarget;
            }
        }
    }
    return RootJournalV2FormatStatus::Succeeded;
}

[[nodiscard]] RootJournalV2FormatStatus ValidateCommit(const RootJournalV2Superblock &superblock,
                                                       const RootJournalV2Commit &commit) noexcept {
    RootJournalV2FormatStatus status = ValidateRecordIdentity(
        superblock, commit.sequence, commit.slot_index, commit.file_system_uuid);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    return commit.metadata_block_count <= superblock.maximum_metadata_block_count &&
                   commit.ordered_data_block_count <= superblock.maximum_ordered_data_block_count &&
                   commit.revoke_count <= superblock.maximum_revoke_count &&
                   (commit.metadata_block_count != 0ULL || commit.revoke_count != 0ULL) &&
                   commit.transaction_generation != 0ULL
               ? RootJournalV2FormatStatus::Succeeded
               : RootJournalV2FormatStatus::InvalidCount;
}

[[nodiscard]] RootJournalV2FormatStatus
ValidateCheckpoint(const RootJournalV2Superblock &superblock,
                   const RootJournalV2Checkpoint &checkpoint) noexcept {
    const RootJournalV2FormatStatus status = ValidateRecordIdentity(
        superblock, checkpoint.sequence, checkpoint.slot_index, checkpoint.file_system_uuid);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    return checkpoint.checkpoint_generation != 0ULL ? RootJournalV2FormatStatus::Succeeded
                                                    : RootJournalV2FormatStatus::InvalidSequence;
}

[[nodiscard]] RootJournalV2FormatStatus
ValidateOrphanBlock(const RootJournalV2Superblock &superblock,
                    const RootJournalV2OrphanBlock &orphan_block) noexcept {
    if (orphan_block.generation == 0ULL ||
        orphan_block.entry_count > OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY ||
        !UuidEqual(orphan_block.file_system_uuid, superblock.file_system_uuid)) {
        return RootJournalV2FormatStatus::InvalidOrphan;
    }
    uint64_t observed_count = 0ULL;
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY;
         ++entry_index) {
        const uint64_t inode_number = orphan_block.inode_numbers[entry_index];
        if (inode_number == 0ULL) {
            continue;
        }
        if (inode_number < OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER ||
            inode_number > superblock.file_system_inode_count) {
            return RootJournalV2FormatStatus::InvalidOrphan;
        }
        for (uint64_t prior_index = 0ULL; prior_index < entry_index; ++prior_index) {
            if (orphan_block.inode_numbers[prior_index] == inode_number) {
                return RootJournalV2FormatStatus::DuplicateTarget;
            }
        }
        ++observed_count;
    }
    return observed_count == orphan_block.entry_count ? RootJournalV2FormatStatus::Succeeded
                                                      : RootJournalV2FormatStatus::InvalidCount;
}

}

RootJournalV2FormatStatus
PlanRootJournalV2Superblock(const RootJournalV2FormatProfile &profile,
                            RootJournalV2Superblock &superblock) noexcept {
    superblock = RootJournalV2Superblock{};
    if (profile.file_system_total_block_count == 0ULL ||
        profile.file_system_inode_count < OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER ||
        profile.journal_start_relative_block == 0ULL || !UuidValid(profile.file_system_uuid) ||
        profile.journal_start_relative_block >
            UINT64_MAX - OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT ||
        profile.journal_start_relative_block + OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT >
            profile.file_system_total_block_count) {
        return RootJournalV2FormatStatus::InvalidLayout;
    }
    superblock = RootJournalV2Superblock{
        .version = OS_KERNEL_ROOTFS_V5_JOURNAL_FORMAT_VERSION,
        .header_size_bytes = OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_HEADER_SIZE_BYTES,
        .block_size_bytes = OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES,
        .sector_size_bytes = OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES,
        .file_system_total_block_count = profile.file_system_total_block_count,
        .file_system_inode_count = profile.file_system_inode_count,
        .journal_start_relative_block = profile.journal_start_relative_block,
        .journal_block_count = OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT,
        .slot_count = OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT,
        .slot_block_count = OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_BLOCK_COUNT,
        .maximum_metadata_block_count = OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT,
        .maximum_ordered_data_block_count =
            OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_ORDERED_DATA_BLOCK_COUNT,
        .maximum_revoke_count = OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT,
        .checksum_algorithm = OS_KERNEL_ROOTFS_V5_CRC32C_ALGORITHM,
        .compatible_features = OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_COMPAT_FEATURES,
        .incompatible_features = OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_INCOMPAT_FEATURES,
        .next_sequence = OS_KERNEL_ROOTFS_V5_JOURNAL_INITIAL_SEQUENCE,
        .last_checkpoint_sequence = 0ULL,
        .creation_time_nanoseconds = profile.creation_time_nanoseconds,
        .journal_generation = OS_KERNEL_ROOTFS_V5_JOURNAL_INITIAL_GENERATION,
        .file_system_uuid = profile.file_system_uuid,
    };
    return RootJournalV2FormatStatus::Succeeded;
}

RootJournalV2FormatStatus
ValidateRootJournalV2Superblock(const RootJournalV2Superblock &superblock) noexcept {
    if (superblock.version != OS_KERNEL_ROOTFS_V5_JOURNAL_FORMAT_VERSION) {
        return RootJournalV2FormatStatus::InvalidVersion;
    }
    if (superblock.header_size_bytes != OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_HEADER_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidHeaderSize;
    }
    if ((superblock.compatible_features & OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_COMPAT_FEATURES) !=
        OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_COMPAT_FEATURES) {
        return RootJournalV2FormatStatus::InvalidFeatures;
    }
    if ((superblock.incompatible_features &
         ~OS_KERNEL_ROOTFS_V5_JOURNAL_SUPPORTED_INCOMPAT_FEATURES) != 0ULL) {
        return RootJournalV2FormatStatus::UnsupportedRequiredFeature;
    }
    if ((superblock.incompatible_features &
         OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_INCOMPAT_FEATURES) !=
        OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_INCOMPAT_FEATURES) {
        return RootJournalV2FormatStatus::InvalidFeatures;
    }
    RootJournalV2Superblock expected{};
    const RootJournalV2FormatStatus status = PlanRootJournalV2Superblock(
        RootJournalV2FormatProfile{
            .file_system_total_block_count = superblock.file_system_total_block_count,
            .file_system_inode_count = superblock.file_system_inode_count,
            .journal_start_relative_block = superblock.journal_start_relative_block,
            .creation_time_nanoseconds = superblock.creation_time_nanoseconds,
            .file_system_uuid = superblock.file_system_uuid,
        },
        expected);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    return superblock.block_size_bytes == expected.block_size_bytes &&
                   superblock.sector_size_bytes == expected.sector_size_bytes &&
                   superblock.journal_block_count == expected.journal_block_count &&
                   superblock.slot_count == expected.slot_count &&
                   superblock.slot_block_count == expected.slot_block_count &&
                   superblock.maximum_metadata_block_count ==
                       expected.maximum_metadata_block_count &&
                   superblock.maximum_ordered_data_block_count ==
                       expected.maximum_ordered_data_block_count &&
                   superblock.maximum_revoke_count == expected.maximum_revoke_count &&
                   superblock.checksum_algorithm == expected.checksum_algorithm &&
                   superblock.next_sequence != 0ULL && superblock.next_sequence != UINT64_MAX &&
                   superblock.last_checkpoint_sequence < superblock.next_sequence &&
                   superblock.journal_generation != 0ULL
               ? RootJournalV2FormatStatus::Succeeded
               : RootJournalV2FormatStatus::InvalidLayout;
}

bool RootJournalV2TargetIsValid(const RootJournalV2Superblock &superblock,
                                const uint64_t target_relative_block) noexcept {
    const bool inside_journal = target_relative_block >= superblock.journal_start_relative_block &&
                                target_relative_block < superblock.journal_start_relative_block +
                                                            superblock.journal_block_count;
    return target_relative_block < superblock.file_system_total_block_count && !inside_journal;
}

uint64_t RootJournalV2SlotStartRelativeBlock(const uint64_t slot_index) noexcept {
    return slot_index < OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT
               ? 1ULL + slot_index * OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_BLOCK_COUNT
               : OS_KERNEL_ROOTFS_V5_NO_BLOCK;
}

RootJournalV2FormatStatus EncodeRootJournalV2Superblock(const RootJournalV2Superblock &superblock,
                                                        uint8_t *const block,
                                                        const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    const RootJournalV2FormatStatus status = ValidateRootJournalV2Superblock(superblock);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(block, block_size_bytes);
    WriteHeader(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAGIC, superblock.header_size_bytes);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_BLOCK_SIZE_OFFSET_BYTES,
             superblock.block_size_bytes);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SECTOR_SIZE_OFFSET_BYTES,
             superblock.sector_size_bytes);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_TOTAL_BLOCKS_OFFSET_BYTES,
             superblock.file_system_total_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_INODE_COUNT_OFFSET_BYTES,
             superblock.file_system_inode_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_START_OFFSET_BYTES,
             superblock.journal_start_relative_block);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_BLOCK_COUNT_OFFSET_BYTES,
             superblock.journal_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SLOT_COUNT_OFFSET_BYTES,
             superblock.slot_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SLOT_BLOCKS_OFFSET_BYTES,
             superblock.slot_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_METADATA_OFFSET_BYTES,
             superblock.maximum_metadata_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_ORDERED_OFFSET_BYTES,
             superblock.maximum_ordered_data_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_REVOKE_OFFSET_BYTES,
             superblock.maximum_revoke_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_CHECKSUM_ALGORITHM_OFFSET_BYTES,
             superblock.checksum_algorithm);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_COMPAT_OFFSET_BYTES,
             superblock.compatible_features);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_INCOMPAT_OFFSET_BYTES,
             superblock.incompatible_features);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_NEXT_SEQUENCE_OFFSET_BYTES,
             superblock.next_sequence);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_LAST_CHECKPOINT_OFFSET_BYTES,
             superblock.last_checkpoint_sequence);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_CREATION_TIME_OFFSET_BYTES,
             superblock.creation_time_nanoseconds);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_GENERATION_OFFSET_BYTES,
             superblock.journal_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_UUID_LOW_OFFSET_BYTES,
             superblock.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_UUID_HIGH_OFFSET_BYTES,
             superblock.file_system_uuid.high);
    WriteChecksum(block);
    return RootJournalV2FormatStatus::Succeeded;
}

RootJournalV2FormatStatus
DecodeRootJournalV2Superblock(const uint8_t *const block, const uint64_t block_size_bytes,
                              RootJournalV2Superblock &superblock) noexcept {
    superblock = RootJournalV2Superblock{};
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    if (!BytesEqual(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAGIC,
                    OS_KERNEL_ROOTFS_V5_JOURNAL_MAGIC_SIZE_BYTES)) {
        return RootJournalV2FormatStatus::InvalidMagic;
    }
    if (!BlockChecksumValid(block)) {
        return RootJournalV2FormatStatus::InvalidChecksum;
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_RESERVED_START_BYTES)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    superblock = RootJournalV2Superblock{
        .version = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_VERSION_OFFSET_BYTES),
        .header_size_bytes = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_HEADER_SIZE_OFFSET_BYTES),
        .block_size_bytes =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_BLOCK_SIZE_OFFSET_BYTES),
        .sector_size_bytes =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SECTOR_SIZE_OFFSET_BYTES),
        .file_system_total_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_TOTAL_BLOCKS_OFFSET_BYTES),
        .file_system_inode_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_INODE_COUNT_OFFSET_BYTES),
        .journal_start_relative_block =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_START_OFFSET_BYTES),
        .journal_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_BLOCK_COUNT_OFFSET_BYTES),
        .slot_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SLOT_COUNT_OFFSET_BYTES),
        .slot_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_SLOT_BLOCKS_OFFSET_BYTES),
        .maximum_metadata_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_METADATA_OFFSET_BYTES),
        .maximum_ordered_data_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_ORDERED_OFFSET_BYTES),
        .maximum_revoke_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_MAX_REVOKE_OFFSET_BYTES),
        .checksum_algorithm =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_CHECKSUM_ALGORITHM_OFFSET_BYTES),
        .compatible_features =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_COMPAT_OFFSET_BYTES),
        .incompatible_features =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_INCOMPAT_OFFSET_BYTES),
        .next_sequence =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_NEXT_SEQUENCE_OFFSET_BYTES),
        .last_checkpoint_sequence =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_LAST_CHECKPOINT_OFFSET_BYTES),
        .creation_time_nanoseconds =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_CREATION_TIME_OFFSET_BYTES),
        .journal_generation =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_GENERATION_OFFSET_BYTES),
        .file_system_uuid =
            RootV5Uuid{
                .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_UUID_LOW_OFFSET_BYTES),
                .high =
                    ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_UUID_HIGH_OFFSET_BYTES),
            },
    };
    return ValidateRootJournalV2Superblock(superblock);
}

RootJournalV2FormatStatus EncodeRootJournalV2Descriptor(const RootJournalV2Superblock &superblock,
                                                        const RootJournalV2Descriptor &descriptor,
                                                        uint8_t *const block,
                                                        const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    const RootJournalV2FormatStatus status = ValidateDescriptor(superblock, descriptor);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(block, block_size_bytes);
    WriteHeader(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_MAGIC,
                OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES, descriptor.sequence);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES, descriptor.slot_index);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_METADATA_COUNT_OFFSET_BYTES,
             descriptor.metadata_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_ORDERED_COUNT_OFFSET_BYTES,
             descriptor.ordered_data_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_REVOKE_COUNT_OFFSET_BYTES,
             descriptor.revoke_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_GENERATION_OFFSET_BYTES,
             descriptor.transaction_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_UUID_LOW_OFFSET_BYTES,
             descriptor.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_UUID_HIGH_OFFSET_BYTES,
             descriptor.file_system_uuid.high);
    for (uint64_t tag_index = 0ULL; tag_index < descriptor.metadata_block_count; ++tag_index) {
        const uint64_t offset = OS_KERNEL_ROOTFS_V5_JOURNAL_TAGS_START_OFFSET_BYTES +
                                tag_index * OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_SIZE_BYTES;
        const RootJournalV2Tag &tag = descriptor.tags[tag_index];
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_TARGET_OFFSET_BYTES,
                 tag.target_relative_block);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_PAYLOAD_OFFSET_BYTES,
                 tag.payload_index);
        WriteU32(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_CHECKSUM_OFFSET_BYTES,
                 tag.payload_checksum);
        WriteU32(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_FLAGS_OFFSET_BYTES, tag.flags);
    }
    WriteChecksum(block);
    return RootJournalV2FormatStatus::Succeeded;
}

RootJournalV2FormatStatus
DecodeRootJournalV2Descriptor(const RootJournalV2Superblock &superblock, const uint8_t *const block,
                              const uint64_t block_size_bytes,
                              RootJournalV2Descriptor &descriptor) noexcept {
    descriptor = RootJournalV2Descriptor{};
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    if (!BlockHeaderValid(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_MAGIC,
                          OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES)) {
        return RootJournalV2FormatStatus::InvalidMagic;
    }
    if (!BlockChecksumValid(block)) {
        return RootJournalV2FormatStatus::InvalidChecksum;
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_JOURNAL_TAGS_START_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_RESERVED_START_BYTES)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    descriptor.sequence = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES);
    descriptor.slot_index = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES);
    descriptor.metadata_block_count =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_METADATA_COUNT_OFFSET_BYTES);
    descriptor.ordered_data_block_count =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_ORDERED_COUNT_OFFSET_BYTES);
    descriptor.revoke_count =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_REVOKE_COUNT_OFFSET_BYTES);
    descriptor.transaction_generation =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_GENERATION_OFFSET_BYTES);
    descriptor.file_system_uuid = RootV5Uuid{
        .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_UUID_LOW_OFFSET_BYTES),
        .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_UUID_HIGH_OFFSET_BYTES),
    };
    for (uint64_t tag_index = 0ULL;
         tag_index < OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT; ++tag_index) {
        const uint64_t offset = OS_KERNEL_ROOTFS_V5_JOURNAL_TAGS_START_OFFSET_BYTES +
                                tag_index * OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_SIZE_BYTES;
        if (ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_RESERVED_OFFSET_BYTES) !=
            0ULL) {
            return RootJournalV2FormatStatus::NonZeroReservedBytes;
        }
        descriptor.tags[tag_index] = RootJournalV2Tag{
            .target_relative_block =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_TARGET_OFFSET_BYTES),
            .payload_index =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_PAYLOAD_OFFSET_BYTES),
            .payload_checksum =
                ReadU32(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_CHECKSUM_OFFSET_BYTES),
            .flags = ReadU32(block, offset + OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_FLAGS_OFFSET_BYTES),
        };
    }
    const uint64_t used_end = OS_KERNEL_ROOTFS_V5_JOURNAL_TAGS_START_OFFSET_BYTES +
                              OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT *
                                  OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_SIZE_BYTES;
    if (!RootV5BytesAreZero(block + used_end,
                            OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES - used_end)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    return ValidateDescriptor(superblock, descriptor);
}

RootJournalV2FormatStatus
EncodeRootJournalV2RevokeBlock(const RootJournalV2Superblock &superblock,
                               const RootJournalV2RevokeBlock &revoke_block, uint8_t *const block,
                               const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    const RootJournalV2FormatStatus status = ValidateRevokeBlock(superblock, revoke_block);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(block, block_size_bytes);
    WriteHeader(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_MAGIC,
                OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES,
             revoke_block.sequence);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES, revoke_block.slot_index);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_COUNT_OFFSET_BYTES,
             revoke_block.revoke_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_UUID_LOW_OFFSET_BYTES,
             revoke_block.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_UUID_HIGH_OFFSET_BYTES,
             revoke_block.file_system_uuid.high);
    for (uint64_t target_index = 0ULL; target_index < revoke_block.revoke_count; ++target_index) {
        WriteU64(block,
                 OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKES_START_OFFSET_BYTES +
                     target_index * sizeof(uint64_t),
                 revoke_block.targets[target_index]);
    }
    WriteChecksum(block);
    return RootJournalV2FormatStatus::Succeeded;
}

RootJournalV2FormatStatus
DecodeRootJournalV2RevokeBlock(const RootJournalV2Superblock &superblock,
                               const uint8_t *const block, const uint64_t block_size_bytes,
                               RootJournalV2RevokeBlock &revoke_block) noexcept {
    revoke_block = RootJournalV2RevokeBlock{};
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    if (!BlockHeaderValid(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_MAGIC,
                          OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES)) {
        return RootJournalV2FormatStatus::InvalidMagic;
    }
    if (!BlockChecksumValid(block)) {
        return RootJournalV2FormatStatus::InvalidChecksum;
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKES_START_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_RESERVED_START_BYTES)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    revoke_block.sequence =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES);
    revoke_block.slot_index = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES);
    revoke_block.revoke_count =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_COUNT_OFFSET_BYTES);
    revoke_block.file_system_uuid = RootV5Uuid{
        .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_UUID_LOW_OFFSET_BYTES),
        .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_UUID_HIGH_OFFSET_BYTES),
    };
    for (uint64_t target_index = 0ULL;
         target_index < OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT; ++target_index) {
        revoke_block.targets[target_index] =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKES_START_OFFSET_BYTES +
                               target_index * sizeof(uint64_t));
    }
    const uint64_t used_end = OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKES_START_OFFSET_BYTES +
                              OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT * sizeof(uint64_t);
    if (!RootV5BytesAreZero(block + used_end,
                            OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES - used_end)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    return ValidateRevokeBlock(superblock, revoke_block);
}

RootJournalV2FormatStatus EncodeRootJournalV2Commit(const RootJournalV2Superblock &superblock,
                                                    const RootJournalV2Commit &commit,
                                                    uint8_t *const block,
                                                    const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    const RootJournalV2FormatStatus status = ValidateCommit(superblock, commit);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(block, block_size_bytes);
    WriteHeader(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_MAGIC,
                OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES, commit.sequence);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES, commit.slot_index);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_METADATA_COUNT_OFFSET_BYTES,
             commit.metadata_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_ORDERED_COUNT_OFFSET_BYTES,
             commit.ordered_data_block_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_REVOKE_COUNT_OFFSET_BYTES,
             commit.revoke_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_GENERATION_OFFSET_BYTES,
             commit.transaction_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_TIME_OFFSET_BYTES,
             commit.commit_time_nanoseconds);
    WriteU32(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_DESCRIPTOR_CHECKSUM_OFFSET_BYTES,
             commit.descriptor_checksum);
    WriteU32(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_REVOKE_CHECKSUM_OFFSET_BYTES,
             commit.revoke_checksum);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_UUID_LOW_OFFSET_BYTES,
             commit.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_UUID_HIGH_OFFSET_BYTES,
             commit.file_system_uuid.high);
    WriteChecksum(block);
    return RootJournalV2FormatStatus::Succeeded;
}

RootJournalV2FormatStatus DecodeRootJournalV2Commit(const RootJournalV2Superblock &superblock,
                                                    const uint8_t *const block,
                                                    const uint64_t block_size_bytes,
                                                    RootJournalV2Commit &commit) noexcept {
    commit = RootJournalV2Commit{};
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    if (!BlockHeaderValid(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_MAGIC,
                          OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES)) {
        return RootJournalV2FormatStatus::InvalidMagic;
    }
    if (!BlockChecksumValid(block)) {
        return RootJournalV2FormatStatus::InvalidChecksum;
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RESERVED_START_BYTES)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    commit = RootJournalV2Commit{
        .sequence = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES),
        .slot_index = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES),
        .metadata_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_METADATA_COUNT_OFFSET_BYTES),
        .ordered_data_block_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_ORDERED_COUNT_OFFSET_BYTES),
        .revoke_count =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_REVOKE_COUNT_OFFSET_BYTES),
        .transaction_generation =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_GENERATION_OFFSET_BYTES),
        .commit_time_nanoseconds =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_TIME_OFFSET_BYTES),
        .descriptor_checksum =
            ReadU32(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_DESCRIPTOR_CHECKSUM_OFFSET_BYTES),
        .revoke_checksum =
            ReadU32(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_REVOKE_CHECKSUM_OFFSET_BYTES),
        .file_system_uuid =
            RootV5Uuid{
                .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_UUID_LOW_OFFSET_BYTES),
                .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_UUID_HIGH_OFFSET_BYTES),
            },
    };
    return ValidateCommit(superblock, commit);
}

RootJournalV2FormatStatus EncodeRootJournalV2Checkpoint(const RootJournalV2Superblock &superblock,
                                                        const RootJournalV2Checkpoint &checkpoint,
                                                        uint8_t *const block,
                                                        const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    const RootJournalV2FormatStatus status = ValidateCheckpoint(superblock, checkpoint);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(block, block_size_bytes);
    WriteHeader(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_MAGIC,
                OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES, checkpoint.sequence);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES, checkpoint.slot_index);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_GENERATION_OFFSET_BYTES,
             checkpoint.checkpoint_generation);
    WriteU32(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_COMMIT_CHECKSUM_OFFSET_BYTES,
             checkpoint.commit_checksum);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_UUID_LOW_OFFSET_BYTES,
             checkpoint.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_UUID_HIGH_OFFSET_BYTES,
             checkpoint.file_system_uuid.high);
    WriteChecksum(block);
    return RootJournalV2FormatStatus::Succeeded;
}

RootJournalV2FormatStatus
DecodeRootJournalV2Checkpoint(const RootJournalV2Superblock &superblock, const uint8_t *const block,
                              const uint64_t block_size_bytes,
                              RootJournalV2Checkpoint &checkpoint) noexcept {
    checkpoint = RootJournalV2Checkpoint{};
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    if (!BlockHeaderValid(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_MAGIC,
                          OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES)) {
        return RootJournalV2FormatStatus::InvalidMagic;
    }
    if (!BlockChecksumValid(block)) {
        return RootJournalV2FormatStatus::InvalidChecksum;
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_RESERVED_START_BYTES)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    checkpoint = RootJournalV2Checkpoint{
        .sequence = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SEQUENCE_OFFSET_BYTES),
        .slot_index = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_SLOT_OFFSET_BYTES),
        .checkpoint_generation =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_GENERATION_OFFSET_BYTES),
        .commit_checksum =
            ReadU32(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_COMMIT_CHECKSUM_OFFSET_BYTES),
        .file_system_uuid =
            RootV5Uuid{
                .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_UUID_LOW_OFFSET_BYTES),
                .high =
                    ReadU64(block, OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_UUID_HIGH_OFFSET_BYTES),
            },
    };
    return ValidateCheckpoint(superblock, checkpoint);
}

RootJournalV2FormatStatus
EncodeRootJournalV2OrphanBlock(const RootJournalV2Superblock &superblock,
                               const RootJournalV2OrphanBlock &orphan_block, uint8_t *const block,
                               const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    const RootJournalV2FormatStatus status = ValidateOrphanBlock(superblock, orphan_block);
    if (status != RootJournalV2FormatStatus::Succeeded) {
        return status;
    }
    ClearBytes(block, block_size_bytes);
    WriteHeader(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_MAGIC,
                OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_GENERATION_OFFSET_BYTES,
             orphan_block.generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_COUNT_OFFSET_BYTES,
             orphan_block.entry_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_UUID_LOW_OFFSET_BYTES,
             orphan_block.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_UUID_HIGH_OFFSET_BYTES,
             orphan_block.file_system_uuid.high);
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY;
         ++entry_index) {
        WriteU64(block,
                 OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRIES_START_BYTES +
                     entry_index * OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_SIZE_BYTES,
                 orphan_block.inode_numbers[entry_index]);
    }
    WriteChecksum(block);
    return RootJournalV2FormatStatus::Succeeded;
}

RootJournalV2FormatStatus
DecodeRootJournalV2OrphanBlock(const RootJournalV2Superblock &superblock,
                               const uint8_t *const block, const uint64_t block_size_bytes,
                               RootJournalV2OrphanBlock &orphan_block) noexcept {
    orphan_block = RootJournalV2OrphanBlock{};
    if (block == nullptr) {
        return RootJournalV2FormatStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootJournalV2FormatStatus::InvalidBufferSize;
    }
    if (!BlockHeaderValid(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_MAGIC,
                          OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_HEADER_SIZE_BYTES)) {
        return RootJournalV2FormatStatus::InvalidMagic;
    }
    if (!BlockChecksumValid(block)) {
        return RootJournalV2FormatStatus::InvalidChecksum;
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRIES_START_BYTES -
                                OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_RESERVED_START_BYTES)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    orphan_block.generation =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_GENERATION_OFFSET_BYTES);
    orphan_block.entry_count =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_COUNT_OFFSET_BYTES);
    orphan_block.file_system_uuid = RootV5Uuid{
        .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_UUID_LOW_OFFSET_BYTES),
        .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_UUID_HIGH_OFFSET_BYTES),
    };
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY;
         ++entry_index) {
        orphan_block.inode_numbers[entry_index] =
            ReadU64(block, OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRIES_START_BYTES +
                               entry_index * OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_SIZE_BYTES);
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_RESERVED_TAIL_OFFSET_BYTES,
                            OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_CHECKSUM_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_RESERVED_TAIL_OFFSET_BYTES)) {
        return RootJournalV2FormatStatus::NonZeroReservedBytes;
    }
    return ValidateOrphanBlock(superblock, orphan_block);
}

RootJournalV2FormatStatus AddRootJournalV2Orphan(const RootJournalV2Superblock &superblock,
                                                 RootJournalV2OrphanBlock &orphan_block,
                                                 const uint64_t inode_number) noexcept {
    RootJournalV2FormatStatus status = ValidateOrphanBlock(superblock, orphan_block);
    if (status != RootJournalV2FormatStatus::Succeeded ||
        inode_number < OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER ||
        inode_number > superblock.file_system_inode_count) {
        return status == RootJournalV2FormatStatus::Succeeded
                   ? RootJournalV2FormatStatus::InvalidOrphan
                   : status;
    }
    uint64_t free_index = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY;
         ++entry_index) {
        if (orphan_block.inode_numbers[entry_index] == inode_number) {
            return RootJournalV2FormatStatus::Succeeded;
        }
        if (orphan_block.inode_numbers[entry_index] == 0ULL &&
            free_index == OS_KERNEL_ROOTFS_V5_NO_BLOCK) {
            free_index = entry_index;
        }
    }
    if (free_index == OS_KERNEL_ROOTFS_V5_NO_BLOCK || orphan_block.generation == UINT64_MAX) {
        return RootJournalV2FormatStatus::CapacityExhausted;
    }
    orphan_block.inode_numbers[free_index] = inode_number;
    ++orphan_block.entry_count;
    ++orphan_block.generation;
    return RootJournalV2FormatStatus::Succeeded;
}

RootJournalV2FormatStatus RemoveRootJournalV2Orphan(const RootJournalV2Superblock &superblock,
                                                    RootJournalV2OrphanBlock &orphan_block,
                                                    const uint64_t inode_number) noexcept {
    RootJournalV2FormatStatus status = ValidateOrphanBlock(superblock, orphan_block);
    if (status != RootJournalV2FormatStatus::Succeeded ||
        inode_number < OS_KERNEL_ROOTFS_V5_FIRST_USER_INODE_NUMBER ||
        inode_number > superblock.file_system_inode_count) {
        return status == RootJournalV2FormatStatus::Succeeded
                   ? RootJournalV2FormatStatus::InvalidOrphan
                   : status;
    }
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY;
         ++entry_index) {
        if (orphan_block.inode_numbers[entry_index] != inode_number) {
            continue;
        }
        if (orphan_block.generation == UINT64_MAX || orphan_block.entry_count == 0ULL) {
            return RootJournalV2FormatStatus::InvalidOrphan;
        }
        orphan_block.inode_numbers[entry_index] = 0ULL;
        --orphan_block.entry_count;
        ++orphan_block.generation;
        return RootJournalV2FormatStatus::Succeeded;
    }
    return RootJournalV2FormatStatus::Succeeded;
}

}
