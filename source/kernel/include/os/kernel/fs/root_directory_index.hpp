#pragma once

#include <os/kernel/fs/root_file_system_v5_format.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_FORMAT_VERSION = 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_HEADER_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_ENTRY_HEADER_SIZE_BYTES = 32ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_NAME_LENGTH_BYTES = 255ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_NAME_STORAGE_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_CHECKSUM_OFFSET_BYTES = 4092ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_SIZE_BYTES = 32ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_CAPACITY = 123ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_FANOUT = 8ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_ENTRY_COUNT = 512ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAXIMUM_NODE_COUNT = 73ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAXIMUM_DEPTH = 2ULL;

static_assert(OS_KERNEL_ROOTFS_V5_DIRECTORY_BLOCK_HEADER_SIZE_BYTES +
                  OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_CAPACITY *
                      OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_SIZE_BYTES ==
              4064ULL);

enum class RootDirectoryStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidMagic,
    InvalidVersion,
    InvalidChecksum,
    InvalidArgument,
    InvalidEntry,
    InvalidIndex,
    DuplicateName,
    NotFound,
    CapacityExhausted,
    NonZeroReservedBytes,
};

struct RootDirectoryEntryV2 final {
    uint64_t inode_number;
    uint64_t inode_generation;
    uint64_t name_hash;
    RootV5NodeType type;
    uint64_t name_length_bytes;
    uint8_t name[OS_KERNEL_ROOTFS_V5_DIRECTORY_NAME_STORAGE_SIZE_BYTES];
};

struct RootDirectoryBlock final {
    uint64_t directory_inode_number;
    uint64_t directory_inode_generation;
    uint64_t block_generation;
    uint64_t entry_count;
    RootV5Uuid file_system_uuid;
    RootDirectoryEntryV2 entries[OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_ENTRY_COUNT];
};

struct RootDirectoryIndexEntry final {
    uint64_t minimum_hash;
    uint64_t child_relative_block;
    uint64_t child_generation;
    uint64_t covered_entry_count;
};

struct RootDirectoryIndexNode final {
    uint64_t directory_inode_number;
    uint64_t directory_inode_generation;
    uint64_t tree_generation;
    uint64_t depth;
    uint64_t entry_count;
    RootV5Uuid file_system_uuid;
    RootDirectoryIndexEntry entries[OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_ENTRY_CAPACITY];
};

struct RootDirectoryIndexStatistics final {
    uint64_t insert_count;
    uint64_t remove_count;
    uint64_t lookup_count;
    uint64_t hash_collision_count;
    uint64_t split_count;
    uint64_t merge_count;
    uint64_t depth_growth_count;
    uint64_t depth_shrink_count;
    uint64_t last_lookup_node_count;
    uint64_t maximum_lookup_node_count;
    uint64_t current_entry_count;
    uint64_t current_node_count;
    uint64_t current_depth;
};

class RootDirectoryIndex final {
  public:
    RootDirectoryIndex() noexcept = default;

    [[nodiscard]] RootDirectoryStatus Initialize(uint64_t directory_inode_number,
                                                 uint64_t directory_inode_generation,
                                                 RootV5Uuid file_system_uuid) noexcept;
    [[nodiscard]] RootDirectoryStatus Insert(const uint8_t *name, uint64_t name_length_bytes,
                                             uint64_t inode_number, uint64_t inode_generation,
                                             RootV5NodeType type) noexcept;
    [[nodiscard]] RootDirectoryStatus Remove(const uint8_t *name,
                                             uint64_t name_length_bytes) noexcept;
    [[nodiscard]] RootDirectoryStatus Lookup(const uint8_t *name, uint64_t name_length_bytes,
                                             RootDirectoryEntryV2 &entry) noexcept;
    [[nodiscard]] RootDirectoryStatus EntryAt(uint64_t entry_index,
                                              RootDirectoryEntryV2 &entry) const noexcept;
    [[nodiscard]] RootDirectoryStatus Validate() const noexcept;
    [[nodiscard]] uint64_t EntryCount() const noexcept;
    [[nodiscard]] RootDirectoryIndexStatistics Statistics() const noexcept;

  private:
    struct IndexNode final {
        RootDirectoryIndexEntry entries[OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_FANOUT];
        uint64_t depth;
        uint64_t entry_count;
        bool occupied;
    };

    [[nodiscard]] RootDirectoryStatus Rebuild() noexcept;
    [[nodiscard]] bool NameEqual(const RootDirectoryEntryV2 &entry, const uint8_t *name,
                                 uint64_t name_length_bytes) const noexcept;

    RootDirectoryEntryV2 entries_[OS_KERNEL_ROOTFS_V5_DIRECTORY_MAXIMUM_ENTRY_COUNT]{};
    IndexNode nodes_[OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAXIMUM_NODE_COUNT]{};
    RootDirectoryIndexStatistics statistics_{};
    RootV5Uuid file_system_uuid_{};
    uint64_t directory_inode_number_{};
    uint64_t directory_inode_generation_{};
    uint64_t tree_generation_{1ULL};
    uint64_t entry_count_{};
    uint64_t node_count_{};
    uint64_t depth_{};
    bool initialized_{};
};

[[nodiscard]] uint64_t CalculateRootDirectoryNameHash(RootV5Uuid file_system_uuid,
                                                      const uint8_t *name,
                                                      uint64_t name_length_bytes) noexcept;
[[nodiscard]] RootDirectoryStatus EncodeRootDirectoryBlock(const RootDirectoryBlock &directory,
                                                           uint8_t *block,
                                                           uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootDirectoryStatus DecodeRootDirectoryBlock(const uint8_t *block,
                                                           uint64_t block_size_bytes,
                                                           RootDirectoryBlock &directory) noexcept;
[[nodiscard]] RootDirectoryStatus EncodeRootDirectoryIndexNode(const RootDirectoryIndexNode &node,
                                                               uint8_t *block,
                                                               uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootDirectoryStatus
DecodeRootDirectoryIndexNode(const uint8_t *block, uint64_t block_size_bytes,
                             RootDirectoryIndexNode &node) noexcept;

}
