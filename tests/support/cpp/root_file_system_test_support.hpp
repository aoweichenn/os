#pragma once

#include "os/kernel/fs/root_file_system_format.hpp"
#include "sparse_memory_block_device.hpp"

#include <stdint.h>

namespace os::test {

inline constexpr uint64_t OS_TEST_ROOTFS_INITIAL_TRANSACTION_GENERATION = 1ULL;
inline constexpr uint64_t OS_TEST_ROOTFS_INITIAL_ROOT_GENERATION = 1ULL;
inline constexpr uint64_t OS_TEST_ROOTFS_INITIAL_NEXT_INODE_GENERATION = 2ULL;
inline constexpr uint64_t OS_TEST_ROOTFS_ROOT_LINK_COUNT = 1ULL;
inline constexpr uint8_t OS_TEST_ROOTFS_ROOT_INODE_BITMAP_MASK = 0x01U;

// 测试格式化器只写入最小合法元数据，运行时内核仍然坚持“只挂载、不自动格式化”。
[[nodiscard]] inline bool FormatRootFileSystem(SparseMemoryBlockDevice &device) noexcept {
    using namespace os::kernel::fs;

    RootSuperblock superblock{
        .version = OS_KERNEL_ROOTFS_FORMAT_VERSION,
        .block_size_bytes = OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
        .total_block_count = OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT,
        .inode_bitmap_start_relative_block = OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK,
        .inode_bitmap_block_count = OS_KERNEL_ROOTFS_INODE_BITMAP_BLOCK_COUNT,
        .inode_table_start_relative_block = OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK,
        .inode_table_block_count = OS_KERNEL_ROOTFS_INODE_TABLE_BLOCK_COUNT,
        .data_bitmap_start_relative_block = OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK,
        .data_bitmap_block_count = OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT,
        .data_start_relative_block = OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK,
        .data_block_count = OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT,
        .inode_count = OS_KERNEL_ROOTFS_INODE_COUNT,
        .root_inode_number = OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER,
        .maximum_file_size_bytes = OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES,
        .transaction_state = RootTransactionState::Clean,
        .transaction_generation = OS_TEST_ROOTFS_INITIAL_TRANSACTION_GENERATION,
        .next_inode_generation = OS_TEST_ROOTFS_INITIAL_NEXT_INODE_GENERATION,
        .feature_flags = OS_KERNEL_ROOTFS_REQUIRED_FEATURES,
    };
    uint8_t superblock_bytes[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    if (EncodeRootSuperblock(superblock, superblock_bytes, sizeof(superblock_bytes)) !=
            RootFormatStatus::Succeeded ||
        device.WriteBlock(OS_KERNEL_ROOTFS_START_LBA + OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK,
                          superblock_bytes, sizeof(superblock_bytes)) !=
            os::kernel::FileSystemBlockDeviceStatus::Succeeded) {
        return false;
    }

    uint8_t inode_bitmap[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    inode_bitmap[0ULL] = OS_TEST_ROOTFS_ROOT_INODE_BITMAP_MASK;
    if (device.WriteBlock(OS_KERNEL_ROOTFS_START_LBA +
                              OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK,
                          inode_bitmap, sizeof(inode_bitmap)) !=
        os::kernel::FileSystemBlockDeviceStatus::Succeeded) {
        return false;
    }

    RootInode root_inode{
        .type = RootNodeType::Directory,
        .flags = 0ULL,
        .size_bytes = 0ULL,
        .generation = OS_TEST_ROOTFS_INITIAL_ROOT_GENERATION,
        .link_count = OS_TEST_ROOTFS_ROOT_LINK_COUNT,
        .allocated_data_block_count = 0ULL,
        .allocated_metadata_block_count = 0ULL,
        .parent_inode_number = OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER,
        .direct_blocks = {},
        .single_indirect_block = 0ULL,
        .double_indirect_block = 0ULL,
        .triple_indirect_block = 0ULL,
    };
    uint8_t inode_table_block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    if (EncodeRootInode(root_inode, inode_table_block, OS_KERNEL_ROOTFS_INODE_SIZE_BYTES) !=
            RootFormatStatus::Succeeded ||
        device.WriteBlock(OS_KERNEL_ROOTFS_START_LBA +
                              OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK,
                          inode_table_block, sizeof(inode_table_block)) !=
            os::kernel::FileSystemBlockDeviceStatus::Succeeded ||
        device.Flush() != os::kernel::FileSystemBlockDeviceStatus::Succeeded) {
        return false;
    }
    return true;
}

}
