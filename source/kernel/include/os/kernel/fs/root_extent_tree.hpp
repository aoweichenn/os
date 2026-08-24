#pragma once

#include <os/kernel/fs/root_journal_v2_format.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_FORMAT_VERSION = 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_NODE_HEADER_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_SIZE_BYTES = 32ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_CAPACITY = 123ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_NODE_CHECKSUM_OFFSET_BYTES = 4092ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_TREE_FANOUT = 4ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT = 256ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_NODE_COUNT = 85ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_DEPTH = 3ULL;

static_assert(OS_KERNEL_ROOTFS_V5_EXTENT_NODE_HEADER_SIZE_BYTES +
                  OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_CAPACITY *
                      OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_SIZE_BYTES ==
              4064ULL);
static_assert(OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_NODE_COUNT == 64ULL + 16ULL + 4ULL + 1ULL);

enum class RootExtentState : uint64_t {
    Initialized = 1ULL,
    Unwritten = 2ULL,
};

enum class RootExtentStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidMagic,
    InvalidVersion,
    InvalidHeaderSize,
    InvalidChecksum,
    InvalidArgument,
    InvalidExtent,
    InvalidIndex,
    Overlap,
    CapacityExhausted,
    NotFound,
    NonZeroReservedBytes,
};

struct RootExtent final {
    uint64_t logical_start_block;
    uint64_t physical_start_block;
    uint64_t block_count;
    RootExtentState state;
};

struct RootExtentIndex final {
    uint64_t logical_start_block;
    uint64_t child_relative_block;
    uint64_t child_generation;
    uint64_t covered_block_count;
};

struct RootExtentNodeEntry final {
    uint64_t logical_start_block;
    uint64_t physical_or_child_block;
    uint64_t block_count_or_generation;
    uint64_t state_or_covered_block_count;
};

struct RootExtentNode final {
    uint64_t tree_generation;
    uint64_t inode_number;
    uint64_t inode_generation;
    uint64_t depth;
    uint64_t entry_count;
    RootV5Uuid file_system_uuid;
    RootExtentNodeEntry entries[OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_CAPACITY];
};

struct RootExtentTreeStatistics final {
    uint64_t insert_count;
    uint64_t remove_count;
    uint64_t convert_count;
    uint64_t lookup_count;
    uint64_t split_count;
    uint64_t merge_count;
    uint64_t depth_growth_count;
    uint64_t depth_shrink_count;
    uint64_t current_extent_count;
    uint64_t current_node_count;
    uint64_t current_depth;
};

class RootExtentTree final {
  public:
    RootExtentTree() noexcept = default;

    [[nodiscard]] RootExtentStatus Initialize(uint64_t file_system_total_block_count,
                                              uint64_t reserved_start_relative_block,
                                              uint64_t reserved_block_count, uint64_t inode_number,
                                              uint64_t inode_generation,
                                              RootV5Uuid file_system_uuid) noexcept;
    [[nodiscard]] RootExtentStatus Insert(const RootExtent &extent) noexcept;
    [[nodiscard]] RootExtentStatus Remove(uint64_t logical_start_block, uint64_t block_count,
                                          RootExtent *removed_extents,
                                          uint64_t removed_extent_capacity,
                                          uint64_t &removed_extent_count) noexcept;
    [[nodiscard]] RootExtentStatus Collect(uint64_t logical_start_block, uint64_t block_count,
                                           RootExtent *extents, uint64_t extent_capacity,
                                           uint64_t &extent_count) const noexcept;
    [[nodiscard]] RootExtentStatus Convert(uint64_t logical_start_block, uint64_t block_count,
                                           RootExtentState expected_state,
                                           RootExtentState new_state) noexcept;
    [[nodiscard]] RootExtentStatus Lookup(uint64_t logical_block, RootExtent &extent) noexcept;
    [[nodiscard]] RootExtentStatus FindNext(uint64_t logical_block,
                                            RootExtent &extent) const noexcept;
    [[nodiscard]] RootExtentStatus ExtentAt(uint64_t extent_index,
                                            RootExtent &extent) const noexcept;
    [[nodiscard]] RootExtentStatus Validate() const noexcept;
    [[nodiscard]] uint64_t ExtentCount() const noexcept;
    [[nodiscard]] RootExtentTreeStatistics Statistics() const noexcept;

  private:
    struct TreeNode final {
        RootExtentNodeEntry entries[OS_KERNEL_ROOTFS_V5_EXTENT_TREE_FANOUT];
        uint64_t depth;
        uint64_t entry_count;
        bool occupied;
    };

    [[nodiscard]] bool PhysicalRangeIsValid(uint64_t physical_start_block,
                                            uint64_t block_count) const noexcept;
    [[nodiscard]] RootExtentStatus RebuildTree() noexcept;
    [[nodiscard]] uint64_t TreeNodeCoveredBlockCount(const TreeNode &node) const noexcept;
    void NormalizeExtents() noexcept;

    RootExtent extents_[OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT]{};
    TreeNode nodes_[OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_NODE_COUNT]{};
    RootExtentTreeStatistics statistics_{};
    RootV5Uuid file_system_uuid_{};
    uint64_t file_system_total_block_count_{};
    uint64_t reserved_start_relative_block_{};
    uint64_t reserved_block_count_{};
    uint64_t inode_number_{};
    uint64_t inode_generation_{};
    uint64_t tree_generation_{};
    uint64_t extent_count_{};
    uint64_t node_count_{};
    uint64_t root_node_index_{OS_KERNEL_ROOTFS_V5_NO_BLOCK};
    uint64_t depth_{};
    bool initialized_{};
};

[[nodiscard]] RootExtentStatus
EncodeRootExtentLeafNode(const RootJournalV2Superblock &journal_superblock,
                         const RootExtentNode &node, uint8_t *block,
                         uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootExtentStatus
DecodeRootExtentLeafNode(const RootJournalV2Superblock &journal_superblock, const uint8_t *block,
                         uint64_t block_size_bytes, RootExtentNode &node) noexcept;
[[nodiscard]] RootExtentStatus
EncodeRootExtentIndexNode(const RootJournalV2Superblock &journal_superblock,
                          const RootExtentNode &node, uint8_t *block,
                          uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootExtentStatus
DecodeRootExtentIndexNode(const RootJournalV2Superblock &journal_superblock, const uint8_t *block,
                          uint64_t block_size_bytes, RootExtentNode &node) noexcept;

}
