#pragma once

#include "os/kernel/fs/block_cache.hpp"
#include "os/kernel/fs/root_file_system_format.hpp"
#include "os/kernel/fs/root_journal.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel::fs {

using RootTimestampSource = uint64_t (*)() noexcept;

struct RootFileSystemStatistics final {
    BlockCacheStatistics cache;
    RootJournalStatistics journal;
    uint64_t transaction_generation;
    uint64_t allocated_inode_count;
    uint64_t allocated_data_block_count;
    uint64_t allocated_metadata_block_count;
    uint64_t free_data_block_count;
    uint64_t open_reference_count;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t sparse_hole_read_bytes;
    uint64_t short_write_count;
    uint64_t create_count;
    uint64_t remove_count;
    uint64_t rename_count;
    uint64_t truncate_count;
    uint64_t link_count;
    uint64_t orphan_create_count;
    uint64_t orphan_reap_count;
};

class RootFileSystem final {
  public:
    RootFileSystem() noexcept = default;
    RootFileSystem(const RootFileSystem &) = delete;
    RootFileSystem &operator=(const RootFileSystem &) = delete;

    [[nodiscard]] Status Initialize(FileSystemBlockDevice &device, uint64_t superblock_identifier,
                                    bool read_only = false,
                                    RootTimestampSource timestamp_source = nullptr) noexcept;
    [[nodiscard]] Superblock &GetSuperblock() noexcept;
    [[nodiscard]] const Superblock &GetSuperblock() const noexcept;
    [[nodiscard]] RootFileSystemStatistics ReadStatistics() const noexcept;

  private:
    struct DirectoryEntryLocation final {
        uint64_t offset_bytes;
        RootDirectoryEntry entry;
    };

    struct RenameScratch final {
        RootInode source_parent_inode;
        RootInode destination_parent_inode;
        RootInode source_inode;
        RootInode destination_inode;
        DirectoryEntryLocation source_location;
        DirectoryEntryLocation destination_location;
        RootDirectoryEntry destination_entry;
        RootDirectoryEntry empty_entry;
    };

    [[nodiscard]] static Status LookupOperation(void *context, const Vnode &directory,
                                                const uint8_t *name, uint64_t name_length_bytes,
                                                Vnode &vnode) noexcept;
    [[nodiscard]] static Status CreateOperation(void *context, const Vnode &directory,
                                                const uint8_t *name, uint64_t name_length_bytes,
                                                NodeType type, Vnode &vnode) noexcept;
    [[nodiscard]] static Status OpenOperation(void *context, const Vnode &vnode) noexcept;
    [[nodiscard]] static Status CloseOperation(void *context, const Vnode &vnode) noexcept;
    [[nodiscard]] static Status RemoveOperation(void *context, const Vnode &directory,
                                                const uint8_t *name, uint64_t name_length_bytes,
                                                NodeType expected_type) noexcept;
    [[nodiscard]] static Status
    RenameOperation(void *context, const Vnode &source_directory, const uint8_t *source_name,
                    uint64_t source_name_length_bytes, const Vnode &destination_directory,
                    const uint8_t *destination_name, uint64_t destination_name_length_bytes,
                    bool replace) noexcept;
    [[nodiscard]] static Status LinkOperation(void *context, const Vnode &source,
                                              const Vnode &destination_directory,
                                              const uint8_t *destination_name,
                                              uint64_t destination_name_length_bytes) noexcept;
    [[nodiscard]] static Status
    CreateSymbolicLinkOperation(void *context, const Vnode &destination_directory,
                                const uint8_t *destination_name,
                                uint64_t destination_name_length_bytes, const uint8_t *target,
                                uint64_t target_length_bytes, Vnode &vnode) noexcept;
    [[nodiscard]] static Status ReadSymbolicLinkOperation(void *context, const Vnode &vnode,
                                                          uint8_t *destination,
                                                          uint64_t capacity_bytes,
                                                          uint64_t &target_length_bytes) noexcept;
    [[nodiscard]] static Status ParentOperation(void *context, const Vnode &vnode,
                                                Vnode &parent) noexcept;
    [[nodiscard]] static Status ReadOperation(void *context, const Vnode &vnode,
                                              uint64_t offset_bytes, uint8_t *destination,
                                              uint64_t capacity_bytes,
                                              uint64_t &read_bytes) noexcept;
    [[nodiscard]] static Status WriteOperation(void *context, const Vnode &vnode,
                                               uint64_t offset_bytes, const uint8_t *source,
                                               uint64_t length_bytes,
                                               uint64_t &written_bytes) noexcept;
    [[nodiscard]] static Status TruncateOperation(void *context, const Vnode &vnode,
                                                  uint64_t size_bytes) noexcept;
    [[nodiscard]] static Status ReadDirectoryOperation(void *context, const Vnode &directory,
                                                       uint64_t &cursor, DirectoryEntry &entry,
                                                       bool &end_of_directory) noexcept;
    [[nodiscard]] static Status GetNameOperation(void *context, const Vnode &vnode, uint8_t *name,
                                                 uint64_t name_capacity_bytes,
                                                 uint64_t &name_length_bytes) noexcept;
    [[nodiscard]] static Status StatOperation(void *context, const Vnode &vnode,
                                              BackendNodeInformation &information) noexcept;
    [[nodiscard]] static Status SyncOperation(void *context) noexcept;
    [[nodiscard]] static Status ValidateOperation(void *context) noexcept;
    [[nodiscard]] static Status ReadResourceUsageOperation(void *context,
                                                           ResourceUsage &usage) noexcept;

    [[nodiscard]] Status ReadRelativeBlock(uint64_t relative_block, uint8_t *block) noexcept;
    [[nodiscard]] Status WriteRelativeBlock(uint64_t relative_block, const uint8_t *block) noexcept;
    [[nodiscard]] Status WriteMetadataBlock(uint64_t relative_block, const uint8_t *block) noexcept;
    [[nodiscard]] Status StageSuperblock() noexcept;
    [[nodiscard]] Status BeginTransaction() noexcept;
    [[nodiscard]] Status CommitTransaction() noexcept;
    void AbortTransaction() noexcept;
    [[nodiscard]] Status FailDeviceOperation() noexcept;
    [[nodiscard]] Status ReadInode(uint64_t inode_number, RootInode &inode) noexcept;
    [[nodiscard]] Status WriteInode(uint64_t inode_number, const RootInode &inode) noexcept;
    [[nodiscard]] Status ReadPointerBlock(uint64_t relative_block,
                                          RootPointerBlock &pointer_block) noexcept;
    [[nodiscard]] Status WritePointerBlock(uint64_t relative_block,
                                           const RootPointerBlock &pointer_block) noexcept;
    [[nodiscard]] Status ReadBitmapBit(bool inode_bitmap, uint64_t bit_index,
                                       bool &allocated) noexcept;
    [[nodiscard]] Status WriteBitmapBit(bool inode_bitmap, uint64_t bit_index,
                                        bool allocated) noexcept;
    [[nodiscard]] Status FindFreeBitmapBit(bool inode_bitmap, uint64_t first_bit,
                                           uint64_t bit_count, uint64_t &bit_index) noexcept;
    [[nodiscard]] Status AllocateDataBlock(uint64_t &relative_block) noexcept;
    [[nodiscard]] Status ReleaseDataBlock(uint64_t relative_block) noexcept;
    [[nodiscard]] Status AllocateInodeNumber(uint64_t &inode_number) noexcept;
    [[nodiscard]] Status ReleaseInodeNumber(uint64_t inode_number) noexcept;
    [[nodiscard]] Status RequiredBlocksForLogicalBlock(const RootInode &inode,
                                                       uint64_t logical_block,
                                                       uint64_t &required_block_count) noexcept;
    [[nodiscard]] Status ResolveDataBlock(RootInode &inode, uint64_t logical_block, bool allocate,
                                          uint64_t &relative_block) noexcept;
    [[nodiscard]] Status ResolveIndirectDataBlock(uint64_t &root_block, uint64_t level,
                                                  uint64_t logical_block, bool allocate,
                                                  RootInode &inode,
                                                  uint64_t &relative_block) noexcept;
    [[nodiscard]] Status ReleaseLogicalBlock(RootInode &inode, uint64_t logical_block) noexcept;
    [[nodiscard]] Status ReleaseIndirectLogicalBlock(uint64_t &root_block, uint64_t level,
                                                     uint64_t logical_block,
                                                     RootInode &inode) noexcept;
    [[nodiscard]] Status ReleaseLogicalBlockRange(RootInode &inode, uint64_t first_logical_block,
                                                  uint64_t past_last_logical_block) noexcept;
    [[nodiscard]] Status ReleaseIndirectLogicalBlockRange(uint64_t &root_block, uint64_t level,
                                                          uint64_t first_logical_block,
                                                          uint64_t past_last_logical_block,
                                                          RootInode &inode) noexcept;
    [[nodiscard]] Status ReadFileBytes(RootInode &inode, uint64_t offset_bytes,
                                       uint8_t *destination, uint64_t capacity_bytes,
                                       uint64_t &read_bytes) noexcept;
    [[nodiscard]] Status WriteFileBytesInTransaction(uint64_t inode_number, RootInode &inode,
                                                     uint64_t offset_bytes, const uint8_t *source,
                                                     uint64_t length_bytes, bool metadata_content,
                                                     uint64_t &written_bytes) noexcept;
    [[nodiscard]] Status TruncateInTransaction(uint64_t inode_number, RootInode &inode,
                                               uint64_t size_bytes) noexcept;
    [[nodiscard]] Status ReadDirectoryEntryAt(RootInode &directory, uint64_t offset_bytes,
                                              RootDirectoryEntry &entry) noexcept;
    [[nodiscard]] Status WriteDirectoryEntryAt(uint64_t directory_inode_number,
                                               RootInode &directory, uint64_t offset_bytes,
                                               const RootDirectoryEntry &entry) noexcept;
    [[nodiscard]] Status FindDirectoryEntry(RootInode &directory, const uint8_t *name,
                                            uint64_t name_length_bytes,
                                            DirectoryEntryLocation &location) noexcept;
    [[nodiscard]] Status FindDirectorySlot(RootInode &directory, uint64_t &offset_bytes) noexcept;
    [[nodiscard]] Status TrimDirectoryTail(uint64_t directory_inode_number,
                                           RootInode &directory) noexcept;
    [[nodiscard]] Status DirectoryIsEmpty(RootInode &directory, bool &empty) noexcept;
    [[nodiscard]] Status RemoveInodeInTransaction(uint64_t inode_number, RootInode &inode) noexcept;
    [[nodiscard]] Status DropLinkInTransaction(uint64_t inode_number, RootInode &inode) noexcept;
    [[nodiscard]] Status ReapOrphans() noexcept;
    [[nodiscard]] Status LoadRecoveryStatistics() noexcept;
    [[nodiscard]] uint64_t ReadCurrentTimestamp() const noexcept;
    [[nodiscard]] Status ValidateVnode(const Vnode &vnode, RootInode &inode) noexcept;
    [[nodiscard]] Status ValidateUnlocked() noexcept;
    [[nodiscard]] Status MarkValidationDataBlock(uint64_t relative_block) noexcept;
    [[nodiscard]] Status ValidatePointerTree(uint64_t relative_block, uint64_t level,
                                             uint64_t logical_start, uint64_t logical_limit,
                                             uint64_t &metadata_block_count,
                                             uint64_t &data_block_count) noexcept;
    [[nodiscard]] Status ValidateInodeBlocks(const RootInode &inode, uint64_t &metadata_block_count,
                                             uint64_t &data_block_count) noexcept;
    [[nodiscard]] Status CompareValidationBitmaps(uint64_t &allocated_inode_count,
                                                  uint64_t &allocated_block_count) noexcept;
    [[nodiscard]] Vnode MakeVnode(uint64_t inode_number, const RootInode &inode) noexcept;
    [[nodiscard]] static NodeType ToVfsNodeType(RootNodeType type) noexcept;
    [[nodiscard]] static RootNodeType ToRootNodeType(NodeType type) noexcept;

    static const BackendOperations operations;

    FileSystemBlockDevice *device_{nullptr};
    BlockCache cache_{};
    RootJournal journal_{};
    RootSuperblock disk_superblock_{};
    RootSuperblock transaction_superblock_snapshot_{};
    Superblock vfs_superblock_{};
    uint64_t open_counts_[OS_KERNEL_ROOTFS_INODE_COUNT]{};
    uint8_t validation_inode_bitmap_[OS_KERNEL_ROOTFS_INODE_BITMAP_BLOCK_COUNT *
                                     OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    uint64_t validation_link_counts_[OS_KERNEL_ROOTFS_INODE_COUNT]{};
    uint64_t validation_queue_[OS_KERNEL_ROOTFS_INODE_COUNT]{};
    RootFileSystemStatistics statistics_{};
    RootFileSystemStatistics transaction_statistics_snapshot_{};
    uint64_t next_data_allocation_hint_{};
    uint64_t next_inode_allocation_hint_{};
    uint64_t transaction_data_allocation_hint_snapshot_{};
    uint64_t transaction_inode_allocation_hint_snapshot_{};
    uint64_t last_validated_transaction_generation_{};
    RootTimestampSource timestamp_source_{nullptr};
    RenameScratch rename_scratch_{};
    mutable SpinLock lock_{};
    bool initialized_{};
    bool failed_{};
    bool transaction_snapshot_valid_{};
};

}
