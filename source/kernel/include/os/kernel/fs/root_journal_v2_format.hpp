#pragma once

#include <os/kernel/fs/root_file_system_v5_format.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_FORMAT_VERSION = 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_HEADER_SIZE_BYTES = 192ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_RECORD_HEADER_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPERBLOCK_RELATIVE_BLOCK = 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT = 4ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT = 16ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_ORDERED_DATA_BLOCK_COUNT = 8ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT = 32ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_DESCRIPTOR_RELATIVE_BLOCK = 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKE_RELATIVE_BLOCK = 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK = 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RELATIVE_BLOCK =
    OS_KERNEL_ROOTFS_V5_JOURNAL_PAYLOAD_START_RELATIVE_BLOCK +
    OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_RELATIVE_BLOCK =
    OS_KERNEL_ROOTFS_V5_JOURNAL_COMMIT_RELATIVE_BLOCK + 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_BLOCK_COUNT =
    OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKPOINT_RELATIVE_BLOCK + 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT =
    1ULL + OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_COUNT * OS_KERNEL_ROOTFS_V5_JOURNAL_SLOT_BLOCK_COUNT;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_SIZE_BYTES = 32ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_TAGS_START_OFFSET_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKES_START_OFFSET_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES = 4092ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_INODE_NUMBER = 8ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_INODE_NUMBER = 15ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_HEADER_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_SIZE_BYTES = 8ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY = 503ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_CHECKSUM_OFFSET_BYTES = 4092ULL;

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_COMPAT_ORPHAN_FILE = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_COMPAT_FEATURES =
    OS_KERNEL_ROOTFS_V5_JOURNAL_COMPAT_ORPHAN_FILE;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_INCOMPAT_64_BIT_BLOCKS = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_INCOMPAT_REVOKE = 1ULL << 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_INCOMPAT_CRC32C = 1ULL << 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_INCOMPAT_FEATURES =
    OS_KERNEL_ROOTFS_V5_JOURNAL_INCOMPAT_64_BIT_BLOCKS |
    OS_KERNEL_ROOTFS_V5_JOURNAL_INCOMPAT_REVOKE | OS_KERNEL_ROOTFS_V5_JOURNAL_INCOMPAT_CRC32C;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_SUPPORTED_INCOMPAT_FEATURES =
    OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_INCOMPAT_FEATURES;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_FLAG_METADATA = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_JOURNAL_REQUIRED_TAG_FLAGS =
    OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_FLAG_METADATA;

static_assert(OS_KERNEL_ROOTFS_V5_JOURNAL_BLOCK_COUNT == 81ULL);
static_assert(OS_KERNEL_ROOTFS_V5_JOURNAL_TAGS_START_OFFSET_BYTES +
                  OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT *
                      OS_KERNEL_ROOTFS_V5_JOURNAL_TAG_SIZE_BYTES <=
              OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES);
static_assert(OS_KERNEL_ROOTFS_V5_JOURNAL_REVOKES_START_OFFSET_BYTES +
                  OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT * sizeof(uint64_t) <=
              OS_KERNEL_ROOTFS_V5_JOURNAL_CHECKSUM_OFFSET_BYTES);
static_assert(OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_HEADER_SIZE_BYTES +
                  OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY *
                      OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_SIZE_BYTES ==
              4088ULL);

enum class RootJournalV2FormatStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidMagic,
    InvalidVersion,
    InvalidHeaderSize,
    InvalidLayout,
    InvalidFeatures,
    UnsupportedRequiredFeature,
    InvalidChecksum,
    InvalidSequence,
    InvalidSlot,
    InvalidCount,
    InvalidTarget,
    DuplicateTarget,
    InvalidOrphan,
    CapacityExhausted,
    NonZeroReservedBytes,
};

struct RootJournalV2FormatProfile final {
    uint64_t file_system_total_block_count;
    uint64_t file_system_inode_count;
    uint64_t journal_start_relative_block;
    uint64_t creation_time_nanoseconds;
    RootV5Uuid file_system_uuid;
};

struct RootJournalV2Superblock final {
    uint64_t version;
    uint64_t header_size_bytes;
    uint64_t block_size_bytes;
    uint64_t sector_size_bytes;
    uint64_t file_system_total_block_count;
    uint64_t file_system_inode_count;
    uint64_t journal_start_relative_block;
    uint64_t journal_block_count;
    uint64_t slot_count;
    uint64_t slot_block_count;
    uint64_t maximum_metadata_block_count;
    uint64_t maximum_ordered_data_block_count;
    uint64_t maximum_revoke_count;
    uint64_t checksum_algorithm;
    uint64_t compatible_features;
    uint64_t incompatible_features;
    uint64_t next_sequence;
    uint64_t last_checkpoint_sequence;
    uint64_t creation_time_nanoseconds;
    uint64_t journal_generation;
    RootV5Uuid file_system_uuid;
};

struct RootJournalV2Tag final {
    uint64_t target_relative_block;
    uint64_t payload_index;
    uint32_t payload_checksum;
    uint32_t flags;
};

struct RootJournalV2Descriptor final {
    uint64_t sequence;
    uint64_t slot_index;
    uint64_t metadata_block_count;
    uint64_t ordered_data_block_count;
    uint64_t revoke_count;
    uint64_t transaction_generation;
    RootV5Uuid file_system_uuid;
    RootJournalV2Tag tags[OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_METADATA_BLOCK_COUNT];
};

struct RootJournalV2RevokeBlock final {
    uint64_t sequence;
    uint64_t slot_index;
    uint64_t revoke_count;
    RootV5Uuid file_system_uuid;
    uint64_t targets[OS_KERNEL_ROOTFS_V5_JOURNAL_MAXIMUM_REVOKE_COUNT];
};

struct RootJournalV2Commit final {
    uint64_t sequence;
    uint64_t slot_index;
    uint64_t metadata_block_count;
    uint64_t ordered_data_block_count;
    uint64_t revoke_count;
    uint64_t transaction_generation;
    uint64_t commit_time_nanoseconds;
    uint32_t descriptor_checksum;
    uint32_t revoke_checksum;
    RootV5Uuid file_system_uuid;
};

struct RootJournalV2Checkpoint final {
    uint64_t sequence;
    uint64_t slot_index;
    uint64_t checkpoint_generation;
    uint32_t commit_checksum;
    RootV5Uuid file_system_uuid;
};

struct RootJournalV2OrphanBlock final {
    uint64_t generation;
    uint64_t entry_count;
    RootV5Uuid file_system_uuid;
    uint64_t inode_numbers[OS_KERNEL_ROOTFS_V5_ORPHAN_FILE_ENTRY_CAPACITY];
};

[[nodiscard]] RootJournalV2FormatStatus
PlanRootJournalV2Superblock(const RootJournalV2FormatProfile &profile,
                            RootJournalV2Superblock &superblock) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
ValidateRootJournalV2Superblock(const RootJournalV2Superblock &superblock) noexcept;
[[nodiscard]] bool RootJournalV2TargetIsValid(const RootJournalV2Superblock &superblock,
                                              uint64_t target_relative_block) noexcept;
[[nodiscard]] uint64_t RootJournalV2SlotStartRelativeBlock(uint64_t slot_index) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
EncodeRootJournalV2Superblock(const RootJournalV2Superblock &superblock, uint8_t *block,
                              uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
DecodeRootJournalV2Superblock(const uint8_t *block, uint64_t block_size_bytes,
                              RootJournalV2Superblock &superblock) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
EncodeRootJournalV2Descriptor(const RootJournalV2Superblock &superblock,
                              const RootJournalV2Descriptor &descriptor, uint8_t *block,
                              uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
DecodeRootJournalV2Descriptor(const RootJournalV2Superblock &superblock, const uint8_t *block,
                              uint64_t block_size_bytes,
                              RootJournalV2Descriptor &descriptor) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
EncodeRootJournalV2RevokeBlock(const RootJournalV2Superblock &superblock,
                               const RootJournalV2RevokeBlock &revoke_block, uint8_t *block,
                               uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
DecodeRootJournalV2RevokeBlock(const RootJournalV2Superblock &superblock, const uint8_t *block,
                               uint64_t block_size_bytes,
                               RootJournalV2RevokeBlock &revoke_block) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
EncodeRootJournalV2Commit(const RootJournalV2Superblock &superblock,
                          const RootJournalV2Commit &commit, uint8_t *block,
                          uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
DecodeRootJournalV2Commit(const RootJournalV2Superblock &superblock, const uint8_t *block,
                          uint64_t block_size_bytes, RootJournalV2Commit &commit) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
EncodeRootJournalV2Checkpoint(const RootJournalV2Superblock &superblock,
                              const RootJournalV2Checkpoint &checkpoint, uint8_t *block,
                              uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
DecodeRootJournalV2Checkpoint(const RootJournalV2Superblock &superblock, const uint8_t *block,
                              uint64_t block_size_bytes,
                              RootJournalV2Checkpoint &checkpoint) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
EncodeRootJournalV2OrphanBlock(const RootJournalV2Superblock &superblock,
                               const RootJournalV2OrphanBlock &orphan_block, uint8_t *block,
                               uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
DecodeRootJournalV2OrphanBlock(const RootJournalV2Superblock &superblock, const uint8_t *block,
                               uint64_t block_size_bytes,
                               RootJournalV2OrphanBlock &orphan_block) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
AddRootJournalV2Orphan(const RootJournalV2Superblock &superblock,
                       RootJournalV2OrphanBlock &orphan_block, uint64_t inode_number) noexcept;
[[nodiscard]] RootJournalV2FormatStatus
RemoveRootJournalV2Orphan(const RootJournalV2Superblock &superblock,
                          RootJournalV2OrphanBlock &orphan_block, uint64_t inode_number) noexcept;

}
