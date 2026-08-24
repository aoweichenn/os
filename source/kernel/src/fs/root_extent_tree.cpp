#include <os/kernel/fs/root_extent_tree.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint8_t OS_KERNEL_ROOTFS_V5_EXTENT_LEAF_MAGIC[] = {'O', 'S', 'E', 'X',
                                                             'L', '0', '0', '1'};
constexpr uint8_t OS_KERNEL_ROOTFS_V5_EXTENT_INDEX_MAGIC[] = {'O', 'S', 'E', 'X',
                                                              'I', '0', '0', '1'};
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_HEADER_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_TREE_GENERATION_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_INODE_NUMBER_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_INODE_GENERATION_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_DEPTH_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_COUNT_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_UUID_LOW_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_UUID_HIGH_OFFSET_BYTES = 72ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_HEADER_RESERVED_START_BYTES = 80ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_ENTRIES_START_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_LOGICAL_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_PHYSICAL_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_BLOCK_COUNT_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_STATE_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_RESERVED_TAIL_OFFSET_BYTES = 4064ULL;
constexpr uint64_t OS_KERNEL_ROOTFS_V5_EXTENT_INITIAL_GENERATION = 1ULL;

void ClearBytes(uint8_t *const bytes, const uint64_t byte_count) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < byte_count; ++byte_index) {
        bytes[byte_index] = 0U;
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

void WriteU32(uint8_t *const bytes, const uint64_t offset_bytes, const uint32_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        bytes[offset_bytes + byte_index] =
            static_cast<uint8_t>((value >> (byte_index * 8ULL)) & 0xFFU);
    }
}

void WriteU64(uint8_t *const bytes, const uint64_t offset_bytes, const uint64_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        bytes[offset_bytes + byte_index] =
            static_cast<uint8_t>((value >> (byte_index * 8ULL)) & 0xFFULL);
    }
}

[[nodiscard]] uint32_t ReadU32(const uint8_t *const bytes, const uint64_t offset_bytes) noexcept {
    uint32_t value = 0U;
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        value |= static_cast<uint32_t>(bytes[offset_bytes + byte_index]) << (byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] uint64_t ReadU64(const uint8_t *const bytes, const uint64_t offset_bytes) noexcept {
    uint64_t value = 0ULL;
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(value); ++byte_index) {
        value |= static_cast<uint64_t>(bytes[offset_bytes + byte_index]) << (byte_index * 8ULL);
    }
    return value;
}

[[nodiscard]] bool UuidEqual(const RootV5Uuid left, const RootV5Uuid right) noexcept {
    return left.low == right.low && left.high == right.high;
}

[[nodiscard]] bool StateValid(const RootExtentState state) noexcept {
    return state == RootExtentState::Initialized || state == RootExtentState::Unwritten;
}

[[nodiscard]] bool TryRangeEnd(const uint64_t start, const uint64_t count, uint64_t &end) noexcept {
    if (count == 0ULL || start > UINT64_MAX - count) {
        return false;
    }
    end = start + count;
    return true;
}

[[nodiscard]] bool RangesOverlap(const uint64_t left_start, const uint64_t left_count,
                                 const uint64_t right_start, const uint64_t right_count) noexcept {
    uint64_t left_end = 0ULL;
    uint64_t right_end = 0ULL;
    return TryRangeEnd(left_start, left_count, left_end) &&
           TryRangeEnd(right_start, right_count, right_end) && left_start < right_end &&
           right_start < left_end;
}

[[nodiscard]] bool ExtentsMergeable(const RootExtent &left, const RootExtent &right) noexcept {
    return left.state == right.state &&
           left.logical_start_block + left.block_count == right.logical_start_block &&
           left.physical_start_block + left.block_count == right.physical_start_block;
}

[[nodiscard]] bool NodeEntryZero(const RootExtentNodeEntry &entry) noexcept {
    return entry.logical_start_block == 0ULL && entry.physical_or_child_block == 0ULL &&
           entry.block_count_or_generation == 0ULL && entry.state_or_covered_block_count == 0ULL;
}

[[nodiscard]] RootExtentStatus ValidateDiskNode(const RootJournalV2Superblock &journal_superblock,
                                                const RootExtentNode &node,
                                                const bool leaf) noexcept {
    if (node.tree_generation == 0ULL || node.inode_number == 0ULL ||
        node.inode_number > journal_superblock.file_system_inode_count ||
        node.inode_generation == 0ULL || node.entry_count == 0ULL ||
        node.entry_count > OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_CAPACITY ||
        !UuidEqual(node.file_system_uuid, journal_superblock.file_system_uuid) ||
        (leaf && node.depth != 0ULL) ||
        (!leaf &&
         (node.depth == 0ULL || node.depth > OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_DEPTH))) {
        return RootExtentStatus::InvalidArgument;
    }
    uint64_t prior_end = 0ULL;
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_CAPACITY;
         ++entry_index) {
        const RootExtentNodeEntry &entry = node.entries[entry_index];
        if (entry_index >= node.entry_count) {
            if (!NodeEntryZero(entry)) {
                return RootExtentStatus::NonZeroReservedBytes;
            }
            continue;
        }
        uint64_t logical_end = 0ULL;
        if (leaf) {
            const RootExtentState state =
                static_cast<RootExtentState>(entry.state_or_covered_block_count);
            uint64_t physical_end = 0ULL;
            if (!StateValid(state) ||
                !TryRangeEnd(entry.logical_start_block, entry.block_count_or_generation,
                             logical_end) ||
                !TryRangeEnd(entry.physical_or_child_block, entry.block_count_or_generation,
                             physical_end) ||
                physical_end > journal_superblock.file_system_total_block_count ||
                !RootJournalV2TargetIsValid(journal_superblock, entry.physical_or_child_block) ||
                !RootJournalV2TargetIsValid(journal_superblock, physical_end - 1ULL)) {
                return RootExtentStatus::InvalidExtent;
            }
        } else {
            if (!TryRangeEnd(entry.logical_start_block, entry.state_or_covered_block_count,
                             logical_end) ||
                entry.block_count_or_generation == 0ULL ||
                !RootJournalV2TargetIsValid(journal_superblock, entry.physical_or_child_block)) {
                return RootExtentStatus::InvalidIndex;
            }
        }
        if (entry_index != 0ULL && entry.logical_start_block < prior_end) {
            return RootExtentStatus::Overlap;
        }
        for (uint64_t prior_index = 0ULL; prior_index < entry_index; ++prior_index) {
            const RootExtentNodeEntry &prior = node.entries[prior_index];
            if ((leaf &&
                 RangesOverlap(entry.physical_or_child_block, entry.block_count_or_generation,
                               prior.physical_or_child_block, prior.block_count_or_generation)) ||
                (!leaf && entry.physical_or_child_block == prior.physical_or_child_block)) {
                return RootExtentStatus::Overlap;
            }
        }
        prior_end = logical_end;
    }
    return RootExtentStatus::Succeeded;
}

[[nodiscard]] RootExtentStatus EncodeDiskNode(const RootJournalV2Superblock &journal_superblock,
                                              const RootExtentNode &node, const bool leaf,
                                              uint8_t *const block,
                                              const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return RootExtentStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootExtentStatus::InvalidBufferSize;
    }
    const RootExtentStatus status = ValidateDiskNode(journal_superblock, node, leaf);
    if (status != RootExtentStatus::Succeeded) {
        return status;
    }
    ClearBytes(block, block_size_bytes);
    CopyBytes(block,
              leaf ? OS_KERNEL_ROOTFS_V5_EXTENT_LEAF_MAGIC : OS_KERNEL_ROOTFS_V5_EXTENT_INDEX_MAGIC,
              OS_KERNEL_ROOTFS_V5_EXTENT_MAGIC_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_VERSION_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_EXTENT_FORMAT_VERSION);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_HEADER_SIZE_OFFSET_BYTES,
             OS_KERNEL_ROOTFS_V5_EXTENT_NODE_HEADER_SIZE_BYTES);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_TREE_GENERATION_OFFSET_BYTES, node.tree_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_INODE_NUMBER_OFFSET_BYTES, node.inode_number);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_INODE_GENERATION_OFFSET_BYTES,
             node.inode_generation);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_DEPTH_OFFSET_BYTES, node.depth);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_COUNT_OFFSET_BYTES, node.entry_count);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_UUID_LOW_OFFSET_BYTES, node.file_system_uuid.low);
    WriteU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_UUID_HIGH_OFFSET_BYTES, node.file_system_uuid.high);
    for (uint64_t entry_index = 0ULL; entry_index < node.entry_count; ++entry_index) {
        const uint64_t offset = OS_KERNEL_ROOTFS_V5_EXTENT_ENTRIES_START_BYTES +
                                entry_index * OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_SIZE_BYTES;
        const RootExtentNodeEntry &entry = node.entries[entry_index];
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_LOGICAL_OFFSET_BYTES,
                 entry.logical_start_block);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_PHYSICAL_OFFSET_BYTES,
                 entry.physical_or_child_block);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_BLOCK_COUNT_OFFSET_BYTES,
                 entry.block_count_or_generation);
        WriteU64(block, offset + OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_STATE_OFFSET_BYTES,
                 entry.state_or_covered_block_count);
    }
    WriteU32(block, OS_KERNEL_ROOTFS_V5_EXTENT_NODE_CHECKSUM_OFFSET_BYTES,
             CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_EXTENT_NODE_CHECKSUM_OFFSET_BYTES));
    return RootExtentStatus::Succeeded;
}

[[nodiscard]] RootExtentStatus DecodeDiskNode(const RootJournalV2Superblock &journal_superblock,
                                              const uint8_t *const block,
                                              const uint64_t block_size_bytes, const bool leaf,
                                              RootExtentNode &node) noexcept {
    node = RootExtentNode{};
    if (block == nullptr) {
        return RootExtentStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES) {
        return RootExtentStatus::InvalidBufferSize;
    }
    const uint8_t *const expected_magic =
        leaf ? OS_KERNEL_ROOTFS_V5_EXTENT_LEAF_MAGIC : OS_KERNEL_ROOTFS_V5_EXTENT_INDEX_MAGIC;
    if (!BytesEqual(block, expected_magic, OS_KERNEL_ROOTFS_V5_EXTENT_MAGIC_SIZE_BYTES)) {
        return RootExtentStatus::InvalidMagic;
    }
    if (ReadU32(block, OS_KERNEL_ROOTFS_V5_EXTENT_NODE_CHECKSUM_OFFSET_BYTES) !=
        CalculateRootV5Crc32c(block, OS_KERNEL_ROOTFS_V5_EXTENT_NODE_CHECKSUM_OFFSET_BYTES)) {
        return RootExtentStatus::InvalidChecksum;
    }
    if (ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_VERSION_OFFSET_BYTES) !=
        OS_KERNEL_ROOTFS_V5_EXTENT_FORMAT_VERSION) {
        return RootExtentStatus::InvalidVersion;
    }
    if (ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_HEADER_SIZE_OFFSET_BYTES) !=
        OS_KERNEL_ROOTFS_V5_EXTENT_NODE_HEADER_SIZE_BYTES) {
        return RootExtentStatus::InvalidHeaderSize;
    }
    if (!RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_EXTENT_HEADER_RESERVED_START_BYTES,
                            OS_KERNEL_ROOTFS_V5_EXTENT_ENTRIES_START_BYTES -
                                OS_KERNEL_ROOTFS_V5_EXTENT_HEADER_RESERVED_START_BYTES) ||
        !RootV5BytesAreZero(block + OS_KERNEL_ROOTFS_V5_EXTENT_RESERVED_TAIL_OFFSET_BYTES,
                            OS_KERNEL_ROOTFS_V5_EXTENT_NODE_CHECKSUM_OFFSET_BYTES -
                                OS_KERNEL_ROOTFS_V5_EXTENT_RESERVED_TAIL_OFFSET_BYTES)) {
        return RootExtentStatus::NonZeroReservedBytes;
    }
    node.tree_generation = ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_TREE_GENERATION_OFFSET_BYTES);
    node.inode_number = ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_INODE_NUMBER_OFFSET_BYTES);
    node.inode_generation =
        ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_INODE_GENERATION_OFFSET_BYTES);
    node.depth = ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_DEPTH_OFFSET_BYTES);
    node.entry_count = ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_COUNT_OFFSET_BYTES);
    node.file_system_uuid = RootV5Uuid{
        .low = ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_UUID_LOW_OFFSET_BYTES),
        .high = ReadU64(block, OS_KERNEL_ROOTFS_V5_EXTENT_UUID_HIGH_OFFSET_BYTES),
    };
    for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_CAPACITY;
         ++entry_index) {
        const uint64_t offset = OS_KERNEL_ROOTFS_V5_EXTENT_ENTRIES_START_BYTES +
                                entry_index * OS_KERNEL_ROOTFS_V5_EXTENT_NODE_ENTRY_SIZE_BYTES;
        node.entries[entry_index] = RootExtentNodeEntry{
            .logical_start_block =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_LOGICAL_OFFSET_BYTES),
            .physical_or_child_block =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_PHYSICAL_OFFSET_BYTES),
            .block_count_or_generation =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_BLOCK_COUNT_OFFSET_BYTES),
            .state_or_covered_block_count =
                ReadU64(block, offset + OS_KERNEL_ROOTFS_V5_EXTENT_ENTRY_STATE_OFFSET_BYTES),
        };
    }
    return ValidateDiskNode(journal_superblock, node, leaf);
}

}

uint64_t RootExtentTree::TreeNodeCoveredBlockCount(const TreeNode &node) const noexcept {
    if (node.entry_count == 0ULL) {
        return 0ULL;
    }
    const RootExtentNodeEntry &first = node.entries[0];
    const RootExtentNodeEntry &last = node.entries[node.entry_count - 1ULL];
    const uint64_t last_count =
        node.depth == 0ULL ? last.block_count_or_generation : last.state_or_covered_block_count;
    return last.logical_start_block + last_count - first.logical_start_block;
}

RootExtentStatus RootExtentTree::Initialize(const uint64_t file_system_total_block_count,
                                            const uint64_t reserved_start_relative_block,
                                            const uint64_t reserved_block_count,
                                            const uint64_t inode_number,
                                            const uint64_t inode_generation,
                                            const RootV5Uuid file_system_uuid) noexcept {
    if (this->initialized_ || file_system_total_block_count == 0ULL || inode_number == 0ULL ||
        inode_generation == 0ULL ||
        (file_system_uuid.low == 0ULL && file_system_uuid.high == 0ULL) ||
        reserved_start_relative_block > UINT64_MAX - reserved_block_count ||
        reserved_start_relative_block + reserved_block_count > file_system_total_block_count) {
        return RootExtentStatus::InvalidArgument;
    }
    this->file_system_total_block_count_ = file_system_total_block_count;
    this->reserved_start_relative_block_ = reserved_start_relative_block;
    this->reserved_block_count_ = reserved_block_count;
    this->inode_number_ = inode_number;
    this->inode_generation_ = inode_generation;
    this->file_system_uuid_ = file_system_uuid;
    this->tree_generation_ = OS_KERNEL_ROOTFS_V5_EXTENT_INITIAL_GENERATION;
    this->extent_count_ = 0ULL;
    this->node_count_ = 0ULL;
    this->depth_ = 0ULL;
    this->root_node_index_ = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    this->statistics_ = RootExtentTreeStatistics{};
    this->initialized_ = true;
    return RootExtentStatus::Succeeded;
}

bool RootExtentTree::PhysicalRangeIsValid(const uint64_t physical_start_block,
                                          const uint64_t block_count) const noexcept {
    uint64_t physical_end = 0ULL;
    if (!TryRangeEnd(physical_start_block, block_count, physical_end) ||
        physical_end > this->file_system_total_block_count_) {
        return false;
    }
    const uint64_t reserved_end =
        this->reserved_start_relative_block_ + this->reserved_block_count_;
    return physical_end <= this->reserved_start_relative_block_ ||
           physical_start_block >= reserved_end;
}

void RootExtentTree::NormalizeExtents() noexcept {
    if (this->extent_count_ < 2ULL) {
        return;
    }
    uint64_t output_index = 0ULL;
    for (uint64_t input_index = 1ULL; input_index < this->extent_count_; ++input_index) {
        RootExtent &output = this->extents_[output_index];
        const RootExtent &input = this->extents_[input_index];
        if (ExtentsMergeable(output, input)) {
            output.block_count += input.block_count;
        } else {
            ++output_index;
            this->extents_[output_index] = input;
        }
    }
    const uint64_t new_count = output_index + 1ULL;
    for (uint64_t clear_index = new_count; clear_index < this->extent_count_; ++clear_index) {
        this->extents_[clear_index] = RootExtent{};
    }
    this->extent_count_ = new_count;
}

RootExtentStatus RootExtentTree::RebuildTree() noexcept {
    const uint64_t old_node_count = this->node_count_;
    const uint64_t old_depth = this->depth_;
    for (uint64_t node_index = 0ULL;
         node_index < OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_NODE_COUNT; ++node_index) {
        this->nodes_[node_index] = TreeNode{};
    }
    this->node_count_ = 0ULL;
    this->root_node_index_ = OS_KERNEL_ROOTFS_V5_NO_BLOCK;
    this->depth_ = 0ULL;
    if (this->extent_count_ == 0ULL) {
        if (old_node_count != 0ULL) {
            this->statistics_.merge_count += old_node_count;
        }
        if (old_depth != 0ULL) {
            ++this->statistics_.depth_shrink_count;
        }
        return RootExtentStatus::Succeeded;
    }
    uint64_t level_start = 0ULL;
    uint64_t level_count = 0ULL;
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_;) {
        if (this->node_count_ >= OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_NODE_COUNT) {
            return RootExtentStatus::CapacityExhausted;
        }
        TreeNode &leaf = this->nodes_[this->node_count_];
        leaf.depth = 0ULL;
        leaf.occupied = true;
        for (uint64_t entry_index = 0ULL; entry_index < OS_KERNEL_ROOTFS_V5_EXTENT_TREE_FANOUT &&
                                          extent_index < this->extent_count_;
             ++entry_index, ++extent_index) {
            const RootExtent &extent = this->extents_[extent_index];
            leaf.entries[entry_index] = RootExtentNodeEntry{
                .logical_start_block = extent.logical_start_block,
                .physical_or_child_block = extent.physical_start_block,
                .block_count_or_generation = extent.block_count,
                .state_or_covered_block_count = static_cast<uint64_t>(extent.state),
            };
            ++leaf.entry_count;
        }
        ++this->node_count_;
        ++level_count;
    }
    while (level_count > 1ULL) {
        const uint64_t next_level_start = this->node_count_;
        uint64_t next_level_count = 0ULL;
        for (uint64_t child_offset = 0ULL; child_offset < level_count;) {
            if (this->node_count_ >= OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_NODE_COUNT) {
                return RootExtentStatus::CapacityExhausted;
            }
            TreeNode &parent = this->nodes_[this->node_count_];
            parent.depth = this->nodes_[level_start + child_offset].depth + 1ULL;
            parent.occupied = true;
            if (parent.depth > OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_DEPTH) {
                return RootExtentStatus::CapacityExhausted;
            }
            for (uint64_t entry_index = 0ULL;
                 entry_index < OS_KERNEL_ROOTFS_V5_EXTENT_TREE_FANOUT && child_offset < level_count;
                 ++entry_index, ++child_offset) {
                const uint64_t child_index = level_start + child_offset;
                const TreeNode &child = this->nodes_[child_index];
                parent.entries[entry_index] = RootExtentNodeEntry{
                    .logical_start_block = child.entries[0].logical_start_block,
                    .physical_or_child_block = child_index,
                    .block_count_or_generation = this->tree_generation_,
                    .state_or_covered_block_count = this->TreeNodeCoveredBlockCount(child),
                };
                ++parent.entry_count;
            }
            ++this->node_count_;
            ++next_level_count;
        }
        level_start = next_level_start;
        level_count = next_level_count;
    }
    this->root_node_index_ = level_start;
    this->depth_ = this->nodes_[this->root_node_index_].depth;
    if (this->node_count_ > old_node_count) {
        this->statistics_.split_count += this->node_count_ - old_node_count;
    } else if (this->node_count_ < old_node_count) {
        this->statistics_.merge_count += old_node_count - this->node_count_;
    }
    if (this->depth_ > old_depth) {
        ++this->statistics_.depth_growth_count;
    } else if (this->depth_ < old_depth) {
        ++this->statistics_.depth_shrink_count;
    }
    return RootExtentStatus::Succeeded;
}

RootExtentStatus RootExtentTree::Insert(const RootExtent &extent) noexcept {
    uint64_t logical_end = 0ULL;
    if (!this->initialized_ || !StateValid(extent.state) ||
        !TryRangeEnd(extent.logical_start_block, extent.block_count, logical_end) ||
        !this->PhysicalRangeIsValid(extent.physical_start_block, extent.block_count) ||
        this->extent_count_ >= OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT ||
        this->tree_generation_ == UINT64_MAX) {
        return RootExtentStatus::InvalidExtent;
    }
    uint64_t insert_index = 0ULL;
    while (insert_index < this->extent_count_ &&
           this->extents_[insert_index].logical_start_block < extent.logical_start_block) {
        ++insert_index;
    }
    if ((insert_index != 0ULL && this->extents_[insert_index - 1ULL].logical_start_block +
                                         this->extents_[insert_index - 1ULL].block_count >
                                     extent.logical_start_block) ||
        (insert_index < this->extent_count_ &&
         logical_end > this->extents_[insert_index].logical_start_block)) {
        return RootExtentStatus::Overlap;
    }
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_; ++extent_index) {
        const RootExtent &existing = this->extents_[extent_index];
        if (RangesOverlap(extent.physical_start_block, extent.block_count,
                          existing.physical_start_block, existing.block_count)) {
            return RootExtentStatus::Overlap;
        }
    }
    for (uint64_t move_index = this->extent_count_; move_index > insert_index; --move_index) {
        this->extents_[move_index] = this->extents_[move_index - 1ULL];
    }
    this->extents_[insert_index] = extent;
    ++this->extent_count_;
    ++this->tree_generation_;
    this->NormalizeExtents();
    const RootExtentStatus status = this->RebuildTree();
    if (status == RootExtentStatus::Succeeded) {
        ++this->statistics_.insert_count;
        this->statistics_.current_extent_count = this->extent_count_;
        this->statistics_.current_node_count = this->node_count_;
        this->statistics_.current_depth = this->depth_;
    }
    return status;
}

RootExtentStatus RootExtentTree::Collect(const uint64_t logical_start_block,
                                         const uint64_t block_count, RootExtent *const extents,
                                         const uint64_t extent_capacity,
                                         uint64_t &extent_count) const noexcept {
    extent_count = 0ULL;
    uint64_t logical_end = 0ULL;
    if (!this->initialized_ || !TryRangeEnd(logical_start_block, block_count, logical_end) ||
        (extents == nullptr && extent_capacity != 0ULL)) {
        return RootExtentStatus::InvalidArgument;
    }
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_; ++extent_index) {
        const RootExtent &extent = this->extents_[extent_index];
        const uint64_t extent_end = extent.logical_start_block + extent.block_count;
        const uint64_t intersection_start = extent.logical_start_block > logical_start_block
                                                ? extent.logical_start_block
                                                : logical_start_block;
        const uint64_t intersection_end = extent_end < logical_end ? extent_end : logical_end;
        if (intersection_start >= intersection_end) {
            continue;
        }
        if (extent_count >= extent_capacity) {
            extent_count = 0ULL;
            return RootExtentStatus::CapacityExhausted;
        }
        extents[extent_count] = RootExtent{
            .logical_start_block = intersection_start,
            .physical_start_block =
                extent.physical_start_block + intersection_start - extent.logical_start_block,
            .block_count = intersection_end - intersection_start,
            .state = extent.state,
        };
        ++extent_count;
    }
    return RootExtentStatus::Succeeded;
}

RootExtentStatus RootExtentTree::Remove(const uint64_t logical_start_block,
                                        const uint64_t block_count,
                                        RootExtent *const removed_extents,
                                        const uint64_t removed_extent_capacity,
                                        uint64_t &removed_extent_count) noexcept {
    RootExtentStatus status = this->Collect(logical_start_block, block_count, removed_extents,
                                            removed_extent_capacity, removed_extent_count);
    if (status != RootExtentStatus::Succeeded || removed_extent_count == 0ULL) {
        return status == RootExtentStatus::Succeeded ? RootExtentStatus::NotFound : status;
    }
    uint64_t logical_end = 0ULL;
    static_cast<void>(TryRangeEnd(logical_start_block, block_count, logical_end));
    RootExtent rebuilt[OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT]{};
    uint64_t rebuilt_count = 0ULL;
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_; ++extent_index) {
        const RootExtent &extent = this->extents_[extent_index];
        const uint64_t extent_end = extent.logical_start_block + extent.block_count;
        if (extent_end <= logical_start_block || extent.logical_start_block >= logical_end) {
            if (rebuilt_count >= OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT) {
                return RootExtentStatus::CapacityExhausted;
            }
            rebuilt[rebuilt_count++] = extent;
            continue;
        }
        if (extent.logical_start_block < logical_start_block) {
            if (rebuilt_count >= OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT) {
                return RootExtentStatus::CapacityExhausted;
            }
            rebuilt[rebuilt_count++] = RootExtent{
                .logical_start_block = extent.logical_start_block,
                .physical_start_block = extent.physical_start_block,
                .block_count = logical_start_block - extent.logical_start_block,
                .state = extent.state,
            };
        }
        if (extent_end > logical_end) {
            if (rebuilt_count >= OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT) {
                return RootExtentStatus::CapacityExhausted;
            }
            rebuilt[rebuilt_count++] = RootExtent{
                .logical_start_block = logical_end,
                .physical_start_block =
                    extent.physical_start_block + logical_end - extent.logical_start_block,
                .block_count = extent_end - logical_end,
                .state = extent.state,
            };
        }
    }
    for (uint64_t extent_index = 0ULL;
         extent_index < OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT; ++extent_index) {
        this->extents_[extent_index] =
            extent_index < rebuilt_count ? rebuilt[extent_index] : RootExtent{};
    }
    this->extent_count_ = rebuilt_count;
    ++this->tree_generation_;
    this->NormalizeExtents();
    status = this->RebuildTree();
    if (status == RootExtentStatus::Succeeded) {
        ++this->statistics_.remove_count;
        this->statistics_.current_extent_count = this->extent_count_;
        this->statistics_.current_node_count = this->node_count_;
        this->statistics_.current_depth = this->depth_;
    }
    return status;
}

RootExtentStatus RootExtentTree::Convert(const uint64_t logical_start_block,
                                         const uint64_t block_count,
                                         const RootExtentState expected_state,
                                         const RootExtentState new_state) noexcept {
    uint64_t logical_end = 0ULL;
    if (!this->initialized_ || !StateValid(expected_state) || !StateValid(new_state) ||
        expected_state == new_state ||
        !TryRangeEnd(logical_start_block, block_count, logical_end) ||
        this->tree_generation_ == UINT64_MAX) {
        return RootExtentStatus::InvalidArgument;
    }
    uint64_t covered_count = 0ULL;
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_; ++extent_index) {
        const RootExtent &extent = this->extents_[extent_index];
        const uint64_t extent_end = extent.logical_start_block + extent.block_count;
        const uint64_t intersection_start = extent.logical_start_block > logical_start_block
                                                ? extent.logical_start_block
                                                : logical_start_block;
        const uint64_t intersection_end = extent_end < logical_end ? extent_end : logical_end;
        if (intersection_start < intersection_end) {
            if (extent.state != expected_state) {
                return RootExtentStatus::InvalidExtent;
            }
            covered_count += intersection_end - intersection_start;
        }
    }
    if (covered_count != block_count) {
        return RootExtentStatus::NotFound;
    }
    RootExtent rebuilt[OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT]{};
    uint64_t rebuilt_count = 0ULL;
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_; ++extent_index) {
        const RootExtent &extent = this->extents_[extent_index];
        const uint64_t extent_end = extent.logical_start_block + extent.block_count;
        if (extent_end <= logical_start_block || extent.logical_start_block >= logical_end) {
            if (rebuilt_count >= OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT) {
                return RootExtentStatus::CapacityExhausted;
            }
            rebuilt[rebuilt_count++] = extent;
            continue;
        }
        const uint64_t intersection_start = extent.logical_start_block > logical_start_block
                                                ? extent.logical_start_block
                                                : logical_start_block;
        const uint64_t intersection_end = extent_end < logical_end ? extent_end : logical_end;
        const uint64_t required_entries =
            (extent.logical_start_block < intersection_start ? 1ULL : 0ULL) + 1ULL +
            (extent_end > intersection_end ? 1ULL : 0ULL);
        if (rebuilt_count >
            OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT - required_entries) {
            return RootExtentStatus::CapacityExhausted;
        }
        if (extent.logical_start_block < intersection_start) {
            rebuilt[rebuilt_count++] = RootExtent{
                .logical_start_block = extent.logical_start_block,
                .physical_start_block = extent.physical_start_block,
                .block_count = intersection_start - extent.logical_start_block,
                .state = extent.state,
            };
        }
        rebuilt[rebuilt_count++] = RootExtent{
            .logical_start_block = intersection_start,
            .physical_start_block =
                extent.physical_start_block + intersection_start - extent.logical_start_block,
            .block_count = intersection_end - intersection_start,
            .state = new_state,
        };
        if (extent_end > intersection_end) {
            rebuilt[rebuilt_count++] = RootExtent{
                .logical_start_block = intersection_end,
                .physical_start_block =
                    extent.physical_start_block + intersection_end - extent.logical_start_block,
                .block_count = extent_end - intersection_end,
                .state = extent.state,
            };
        }
    }
    for (uint64_t extent_index = 0ULL;
         extent_index < OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT; ++extent_index) {
        this->extents_[extent_index] =
            extent_index < rebuilt_count ? rebuilt[extent_index] : RootExtent{};
    }
    this->extent_count_ = rebuilt_count;
    ++this->tree_generation_;
    this->NormalizeExtents();
    const RootExtentStatus status = this->RebuildTree();
    if (status == RootExtentStatus::Succeeded) {
        ++this->statistics_.convert_count;
        this->statistics_.current_extent_count = this->extent_count_;
        this->statistics_.current_node_count = this->node_count_;
        this->statistics_.current_depth = this->depth_;
    }
    return status;
}

RootExtentStatus RootExtentTree::Lookup(const uint64_t logical_block, RootExtent &extent) noexcept {
    ++this->statistics_.lookup_count;
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_; ++extent_index) {
        const RootExtent &candidate = this->extents_[extent_index];
        if (logical_block >= candidate.logical_start_block &&
            logical_block < candidate.logical_start_block + candidate.block_count) {
            extent = candidate;
            return RootExtentStatus::Succeeded;
        }
    }
    extent = RootExtent{};
    return RootExtentStatus::NotFound;
}

RootExtentStatus RootExtentTree::FindNext(const uint64_t logical_block,
                                          RootExtent &extent) const noexcept {
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_; ++extent_index) {
        const RootExtent &candidate = this->extents_[extent_index];
        if (logical_block < candidate.logical_start_block + candidate.block_count) {
            extent = candidate;
            return RootExtentStatus::Succeeded;
        }
    }
    extent = RootExtent{};
    return RootExtentStatus::NotFound;
}

RootExtentStatus RootExtentTree::ExtentAt(const uint64_t extent_index,
                                          RootExtent &extent) const noexcept {
    if (extent_index >= this->extent_count_) {
        extent = RootExtent{};
        return RootExtentStatus::NotFound;
    }
    extent = this->extents_[extent_index];
    return RootExtentStatus::Succeeded;
}

RootExtentStatus RootExtentTree::Validate() const noexcept {
    if (!this->initialized_ ||
        this->extent_count_ > OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_EXTENT_COUNT ||
        this->node_count_ > OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_NODE_COUNT ||
        this->depth_ > OS_KERNEL_ROOTFS_V5_EXTENT_TREE_MAXIMUM_DEPTH) {
        return RootExtentStatus::InvalidArgument;
    }
    for (uint64_t extent_index = 0ULL; extent_index < this->extent_count_; ++extent_index) {
        const RootExtent &extent = this->extents_[extent_index];
        if (!StateValid(extent.state) ||
            !this->PhysicalRangeIsValid(extent.physical_start_block, extent.block_count)) {
            return RootExtentStatus::InvalidExtent;
        }
        if (extent_index != 0ULL) {
            const RootExtent &prior = this->extents_[extent_index - 1ULL];
            if (prior.logical_start_block + prior.block_count > extent.logical_start_block ||
                ExtentsMergeable(prior, extent)) {
                return RootExtentStatus::Overlap;
            }
        }
        for (uint64_t prior_index = 0ULL; prior_index < extent_index; ++prior_index) {
            const RootExtent &prior = this->extents_[prior_index];
            if (RangesOverlap(extent.physical_start_block, extent.block_count,
                              prior.physical_start_block, prior.block_count)) {
                return RootExtentStatus::Overlap;
            }
        }
    }
    if ((this->extent_count_ == 0ULL) != (this->node_count_ == 0ULL) ||
        (this->node_count_ == 0ULL && this->root_node_index_ != OS_KERNEL_ROOTFS_V5_NO_BLOCK) ||
        (this->node_count_ != 0ULL &&
         (this->root_node_index_ >= this->node_count_ ||
          this->nodes_[this->root_node_index_].depth != this->depth_))) {
        return RootExtentStatus::InvalidIndex;
    }
    return RootExtentStatus::Succeeded;
}

uint64_t RootExtentTree::ExtentCount() const noexcept { return this->extent_count_; }

RootExtentTreeStatistics RootExtentTree::Statistics() const noexcept { return this->statistics_; }

RootExtentStatus EncodeRootExtentLeafNode(const RootJournalV2Superblock &journal_superblock,
                                          const RootExtentNode &node, uint8_t *const block,
                                          const uint64_t block_size_bytes) noexcept {
    return EncodeDiskNode(journal_superblock, node, true, block, block_size_bytes);
}

RootExtentStatus DecodeRootExtentLeafNode(const RootJournalV2Superblock &journal_superblock,
                                          const uint8_t *const block,
                                          const uint64_t block_size_bytes,
                                          RootExtentNode &node) noexcept {
    return DecodeDiskNode(journal_superblock, block, block_size_bytes, true, node);
}

RootExtentStatus EncodeRootExtentIndexNode(const RootJournalV2Superblock &journal_superblock,
                                           const RootExtentNode &node, uint8_t *const block,
                                           const uint64_t block_size_bytes) noexcept {
    return EncodeDiskNode(journal_superblock, node, false, block, block_size_bytes);
}

RootExtentStatus DecodeRootExtentIndexNode(const RootJournalV2Superblock &journal_superblock,
                                           const uint8_t *const block,
                                           const uint64_t block_size_bytes,
                                           RootExtentNode &node) noexcept {
    return DecodeDiskNode(journal_superblock, block, block_size_bytes, false, node);
}

}
