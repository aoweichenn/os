#include "os/kernel/fs/root_file_system_format.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOTFS_FORMAT_SUITE_NAME = "kernel/root_file_system_format/unit";
constexpr std::string_view OS_TEST_ROOTFS_FORMAT_SUPERBLOCK_ROUND_TRIP =
    "rootfs v4 超级块必须按版本化小端 64 位布局无损往返";
constexpr std::string_view OS_TEST_ROOTFS_FORMAT_INODE_ROUND_TRIP =
    "rootfs v4 inode 必须保留五级索引根、时间戳与空间统计";
constexpr std::string_view OS_TEST_ROOTFS_FORMAT_POINTER_ROUND_TRIP =
    "rootfs v4 间接块必须覆盖 63 个 64 位指针并校验内容";
constexpr std::string_view OS_TEST_ROOTFS_FORMAT_DIRECTORY_ROUND_TRIP =
    "rootfs v4 目录项必须保留 inode 代际和有界名称";
constexpr std::string_view OS_TEST_ROOTFS_FORMAT_CORRUPTION_REJECTED =
    "rootfs v4 的全部元数据类型都必须拒绝校验和损坏";
constexpr std::string_view OS_TEST_ROOTFS_FORMAT_UNIX_METADATA_REQUIRED =
    "rootfs v4 必须拒绝缺少 UNIX_METADATA 特性或类型不匹配的 mode";

constexpr uint64_t OS_TEST_ROOTFS_FORMAT_TRANSACTION_GENERATION = 71ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_NEXT_INODE_GENERATION = 93ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_INODE_GENERATION = 29ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_INODE_PARENT = 3ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_INODE_SIZE_BYTES = 8193ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_DIRECT_BLOCK =
    os::kernel::fs::OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_SINGLE_INDIRECT_BLOCK =
    os::kernel::fs::OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_DOUBLE_INDIRECT_BLOCK =
    os::kernel::fs::OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + 3ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_TRIPLE_INDIRECT_BLOCK =
    os::kernel::fs::OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + 4ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_QUADRUPLE_INDIRECT_BLOCK =
    os::kernel::fs::OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + 5ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_QUINTUPLE_INDIRECT_BLOCK =
    os::kernel::fs::OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + 6ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_POINTER_VALUE =
    os::kernel::fs::OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK + 7ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_ACCESS_TIME_NANOSECONDS = 101ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_MODIFICATION_TIME_NANOSECONDS = 102ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_CHANGE_TIME_NANOSECONDS = 103ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_BIRTH_TIME_NANOSECONDS = 104ULL;
constexpr os::abi::UserIdentifier OS_TEST_ROOTFS_FORMAT_OWNER_USER_IDENTIFIER = 1000U;
constexpr os::abi::GroupIdentifier OS_TEST_ROOTFS_FORMAT_OWNER_GROUP_IDENTIFIER = 100U;
constexpr os::abi::FileMode OS_TEST_ROOTFS_FORMAT_MODE =
    os::abi::OS_ABI_FILE_MODE_REGULAR | 0000640U;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_DIRECTORY_INODE = 17ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_BYTES = 5ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_CORRUPTION_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_TEST_ROOTFS_FORMAT_ALLOCATED_METADATA_BLOCK_COUNT = 3ULL;
constexpr uint8_t OS_TEST_ROOTFS_FORMAT_CORRUPTION_MASK = 0x40U;
constexpr uint8_t
    OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME[OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_BYTES] = {
        static_cast<uint8_t>('a'), static_cast<uint8_t>('l'), static_cast<uint8_t>('p'),
        static_cast<uint8_t>('h'), static_cast<uint8_t>('a'),
};

[[nodiscard]] os::kernel::fs::RootSuperblock MakeSuperblock() noexcept {
    return os::kernel::fs::RootSuperblock{
        .version = os::kernel::fs::OS_KERNEL_ROOTFS_FORMAT_VERSION,
        .block_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
        .total_block_count = os::kernel::fs::OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT,
        .journal_start_relative_block =
            os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK,
        .journal_block_count = os::kernel::fs::OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT,
        .inode_bitmap_start_relative_block =
            os::kernel::fs::OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK,
        .inode_bitmap_block_count = os::kernel::fs::OS_KERNEL_ROOTFS_INODE_BITMAP_BLOCK_COUNT,
        .inode_table_start_relative_block =
            os::kernel::fs::OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK,
        .inode_table_block_count = os::kernel::fs::OS_KERNEL_ROOTFS_INODE_TABLE_BLOCK_COUNT,
        .data_bitmap_start_relative_block =
            os::kernel::fs::OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK,
        .data_bitmap_block_count = os::kernel::fs::OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT,
        .data_start_relative_block = os::kernel::fs::OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK,
        .data_block_count = os::kernel::fs::OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT,
        .inode_count = os::kernel::fs::OS_KERNEL_ROOTFS_INODE_COUNT,
        .root_inode_number = os::kernel::fs::OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER,
        .maximum_file_size_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES,
        .transaction_state = os::kernel::fs::RootTransactionState::Clean,
        .transaction_generation = OS_TEST_ROOTFS_FORMAT_TRANSACTION_GENERATION,
        .next_inode_generation = OS_TEST_ROOTFS_FORMAT_NEXT_INODE_GENERATION,
        .feature_flags = os::kernel::fs::OS_KERNEL_ROOTFS_REQUIRED_FEATURES,
        .allocated_inode_count = 1ULL,
        .allocated_data_block_count = 0ULL,
        .allocated_metadata_block_count = 0ULL,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_ROOTFS_FORMAT_SUITE_NAME};

    const os::kernel::fs::RootSuperblock superblock = MakeSuperblock();
    uint8_t superblock_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootSuperblock decoded_superblock{};
    const bool superblock_valid =
        os::kernel::fs::EncodeRootSuperblock(superblock, superblock_bytes,
                                             sizeof(superblock_bytes)) ==
            os::kernel::fs::RootFormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootSuperblock(superblock_bytes, sizeof(superblock_bytes),
                                             decoded_superblock) ==
            os::kernel::fs::RootFormatStatus::Succeeded &&
        decoded_superblock.transaction_generation == OS_TEST_ROOTFS_FORMAT_TRANSACTION_GENERATION &&
        decoded_superblock.next_inode_generation == OS_TEST_ROOTFS_FORMAT_NEXT_INODE_GENERATION &&
        decoded_superblock.feature_flags == os::kernel::fs::OS_KERNEL_ROOTFS_REQUIRED_FEATURES;
    test_context.Expect(superblock_valid, OS_TEST_ROOTFS_FORMAT_SUPERBLOCK_ROUND_TRIP);

    os::kernel::fs::RootInode inode{
        .type = os::kernel::fs::RootNodeType::RegularFile,
        .flags = 0ULL,
        .size_bytes = OS_TEST_ROOTFS_FORMAT_INODE_SIZE_BYTES,
        .generation = OS_TEST_ROOTFS_FORMAT_INODE_GENERATION,
        .link_count = 1ULL,
        .allocated_data_block_count = 1ULL,
        .allocated_metadata_block_count = OS_TEST_ROOTFS_FORMAT_ALLOCATED_METADATA_BLOCK_COUNT,
        .parent_inode_number = OS_TEST_ROOTFS_FORMAT_INODE_PARENT,
        .direct_blocks = {OS_TEST_ROOTFS_FORMAT_DIRECT_BLOCK},
        .single_indirect_block = OS_TEST_ROOTFS_FORMAT_SINGLE_INDIRECT_BLOCK,
        .double_indirect_block = OS_TEST_ROOTFS_FORMAT_DOUBLE_INDIRECT_BLOCK,
        .triple_indirect_block = OS_TEST_ROOTFS_FORMAT_TRIPLE_INDIRECT_BLOCK,
        .quadruple_indirect_block = OS_TEST_ROOTFS_FORMAT_QUADRUPLE_INDIRECT_BLOCK,
        .quintuple_indirect_block = OS_TEST_ROOTFS_FORMAT_QUINTUPLE_INDIRECT_BLOCK,
        .access_time_nanoseconds = OS_TEST_ROOTFS_FORMAT_ACCESS_TIME_NANOSECONDS,
        .modification_time_nanoseconds = OS_TEST_ROOTFS_FORMAT_MODIFICATION_TIME_NANOSECONDS,
        .change_time_nanoseconds = OS_TEST_ROOTFS_FORMAT_CHANGE_TIME_NANOSECONDS,
        .birth_time_nanoseconds = OS_TEST_ROOTFS_FORMAT_BIRTH_TIME_NANOSECONDS,
        .owner_user_identifier = OS_TEST_ROOTFS_FORMAT_OWNER_USER_IDENTIFIER,
        .owner_group_identifier = OS_TEST_ROOTFS_FORMAT_OWNER_GROUP_IDENTIFIER,
        .mode = OS_TEST_ROOTFS_FORMAT_MODE,
    };
    uint8_t inode_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_INODE_SIZE_BYTES]{};
    os::kernel::fs::RootInode decoded_inode{};
    const bool inode_valid =
        os::kernel::fs::EncodeRootInode(inode, inode_bytes, sizeof(inode_bytes)) ==
            os::kernel::fs::RootFormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootInode(inode_bytes, sizeof(inode_bytes), decoded_inode) ==
            os::kernel::fs::RootFormatStatus::Succeeded &&
        decoded_inode.size_bytes == OS_TEST_ROOTFS_FORMAT_INODE_SIZE_BYTES &&
        decoded_inode.direct_blocks[0ULL] == OS_TEST_ROOTFS_FORMAT_DIRECT_BLOCK &&
        decoded_inode.triple_indirect_block == OS_TEST_ROOTFS_FORMAT_TRIPLE_INDIRECT_BLOCK &&
        decoded_inode.quadruple_indirect_block == OS_TEST_ROOTFS_FORMAT_QUADRUPLE_INDIRECT_BLOCK &&
        decoded_inode.quintuple_indirect_block == OS_TEST_ROOTFS_FORMAT_QUINTUPLE_INDIRECT_BLOCK &&
        decoded_inode.access_time_nanoseconds == OS_TEST_ROOTFS_FORMAT_ACCESS_TIME_NANOSECONDS &&
        decoded_inode.modification_time_nanoseconds ==
            OS_TEST_ROOTFS_FORMAT_MODIFICATION_TIME_NANOSECONDS &&
        decoded_inode.change_time_nanoseconds == OS_TEST_ROOTFS_FORMAT_CHANGE_TIME_NANOSECONDS &&
        decoded_inode.birth_time_nanoseconds == OS_TEST_ROOTFS_FORMAT_BIRTH_TIME_NANOSECONDS &&
        decoded_inode.owner_user_identifier == OS_TEST_ROOTFS_FORMAT_OWNER_USER_IDENTIFIER &&
        decoded_inode.owner_group_identifier == OS_TEST_ROOTFS_FORMAT_OWNER_GROUP_IDENTIFIER &&
        decoded_inode.mode == OS_TEST_ROOTFS_FORMAT_MODE;
    test_context.Expect(inode_valid, OS_TEST_ROOTFS_FORMAT_INODE_ROUND_TRIP);

    os::kernel::fs::RootPointerBlock pointer_block{};
    pointer_block.pointers[os::kernel::fs::OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK - 1ULL] =
        OS_TEST_ROOTFS_FORMAT_POINTER_VALUE;
    uint8_t pointer_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootPointerBlock decoded_pointer_block{};
    const bool pointer_valid =
        os::kernel::fs::EncodeRootPointerBlock(pointer_block, pointer_bytes,
                                               sizeof(pointer_bytes)) ==
            os::kernel::fs::RootFormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootPointerBlock(pointer_bytes, sizeof(pointer_bytes),
                                               decoded_pointer_block) ==
            os::kernel::fs::RootFormatStatus::Succeeded &&
        decoded_pointer_block
                .pointers[os::kernel::fs::OS_KERNEL_ROOTFS_POINTERS_PER_INDIRECT_BLOCK - 1ULL] ==
            OS_TEST_ROOTFS_FORMAT_POINTER_VALUE;
    test_context.Expect(pointer_valid, OS_TEST_ROOTFS_FORMAT_POINTER_ROUND_TRIP);

    os::kernel::fs::RootDirectoryEntry directory_entry{
        .inode_number = OS_TEST_ROOTFS_FORMAT_DIRECTORY_INODE,
        .inode_generation = OS_TEST_ROOTFS_FORMAT_INODE_GENERATION,
        .type = os::kernel::fs::RootNodeType::Directory,
        .name_length_bytes = OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_BYTES,
        .name = {},
    };
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_BYTES;
         ++byte_index) {
        directory_entry.name[byte_index] = OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME[byte_index];
    }
    uint8_t directory_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES]{};
    os::kernel::fs::RootDirectoryEntry decoded_directory_entry{};
    bool directory_valid =
        os::kernel::fs::EncodeRootDirectoryEntry(directory_entry, directory_bytes,
                                                 sizeof(directory_bytes)) ==
            os::kernel::fs::RootFormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootDirectoryEntry(directory_bytes, sizeof(directory_bytes),
                                                 decoded_directory_entry) ==
            os::kernel::fs::RootFormatStatus::Succeeded &&
        decoded_directory_entry.inode_generation == OS_TEST_ROOTFS_FORMAT_INODE_GENERATION &&
        decoded_directory_entry.name_length_bytes ==
            OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_BYTES;
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME_LENGTH_BYTES;
         ++byte_index) {
        directory_valid = directory_valid && decoded_directory_entry.name[byte_index] ==
                                                 OS_TEST_ROOTFS_FORMAT_DIRECTORY_NAME[byte_index];
    }
    test_context.Expect(directory_valid, OS_TEST_ROOTFS_FORMAT_DIRECTORY_ROUND_TRIP);

    os::kernel::fs::RootSuperblock legacy_superblock = MakeSuperblock();
    legacy_superblock.feature_flags &= ~os::kernel::fs::OS_KERNEL_ROOTFS_FEATURE_UNIX_METADATA;
    os::kernel::fs::RootInode mismatched_mode_inode = inode;
    mismatched_mode_inode.mode = os::abi::OS_ABI_FILE_MODE_DIRECTORY | 0000640U;
    test_context.Expect(os::kernel::fs::EncodeRootSuperblock(legacy_superblock, superblock_bytes,
                                                             sizeof(superblock_bytes)) ==
                                os::kernel::fs::RootFormatStatus::InvalidLayout &&
                            os::kernel::fs::EncodeRootInode(mismatched_mode_inode, inode_bytes,
                                                            sizeof(inode_bytes)) ==
                                os::kernel::fs::RootFormatStatus::InvalidInode,
                        OS_TEST_ROOTFS_FORMAT_UNIX_METADATA_REQUIRED);

    static_cast<void>(os::kernel::fs::EncodeRootSuperblock(superblock, superblock_bytes,
                                                           sizeof(superblock_bytes)));
    static_cast<void>(os::kernel::fs::EncodeRootInode(inode, inode_bytes, sizeof(inode_bytes)));

    superblock_bytes[OS_TEST_ROOTFS_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOTFS_FORMAT_CORRUPTION_MASK;
    inode_bytes[OS_TEST_ROOTFS_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOTFS_FORMAT_CORRUPTION_MASK;
    pointer_bytes[OS_TEST_ROOTFS_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOTFS_FORMAT_CORRUPTION_MASK;
    directory_bytes[OS_TEST_ROOTFS_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOTFS_FORMAT_CORRUPTION_MASK;
    const bool corruption_rejected =
        os::kernel::fs::DecodeRootSuperblock(superblock_bytes, sizeof(superblock_bytes),
                                             decoded_superblock) ==
            os::kernel::fs::RootFormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootInode(inode_bytes, sizeof(inode_bytes), decoded_inode) ==
            os::kernel::fs::RootFormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootPointerBlock(pointer_bytes, sizeof(pointer_bytes),
                                               decoded_pointer_block) ==
            os::kernel::fs::RootFormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootDirectoryEntry(directory_bytes, sizeof(directory_bytes),
                                                 decoded_directory_entry) ==
            os::kernel::fs::RootFormatStatus::InvalidChecksum;
    test_context.Expect(corruption_rejected, OS_TEST_ROOTFS_FORMAT_CORRUPTION_REJECTED);

    return test_context.ExitCode();
}
