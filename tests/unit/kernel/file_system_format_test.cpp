#include "os/kernel/fs/file_system_format.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_SYSTEM_FORMAT_SUITE_NAME = "kernel/file_system_format/unit";
constexpr std::string_view OS_TEST_FILE_SYSTEM_FORMAT_SUPERBLOCK_ROUND_TRIP =
    "超级块必须按显式小端布局和校验和无损往返";
constexpr std::string_view OS_TEST_FILE_SYSTEM_FORMAT_INODE_ROUND_TRIP =
    "inode 必须保留固定宽度字段和直接块地址";
constexpr std::string_view OS_TEST_FILE_SYSTEM_FORMAT_DIRECTORY_ROUND_TRIP =
    "目录项必须保留类型、inode 与有界名称";
constexpr std::string_view OS_TEST_FILE_SYSTEM_FORMAT_DETECTS_CORRUPTION =
    "任意受保护字节损坏都必须触发校验和错误";
constexpr uint64_t OS_TEST_FILE_SYSTEM_FORMAT_FIRST_DIRECT_BLOCK = 17ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_FORMAT_SECOND_DIRECT_BLOCK = 18ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_FORMAT_FILE_SIZE_BYTES = 700ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_FORMAT_GENERATION = 42ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_FORMAT_INODE_NUMBER = 7ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES = 4ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_FORMAT_CORRUPTION_OFFSET_BYTES = 32ULL;
constexpr uint8_t OS_TEST_FILE_SYSTEM_FORMAT_CORRUPTION_MASK = 0x40U;
constexpr uint8_t OS_TEST_FILE_SYSTEM_FORMAT_NAME[OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES] = {
    static_cast<uint8_t>('d'),
    static_cast<uint8_t>('a'),
    static_cast<uint8_t>('t'),
    static_cast<uint8_t>('a'),
};

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_SYSTEM_FORMAT_SUITE_NAME};

    uint8_t superblock_bytes[os::kernel::OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    os::kernel::FileSystemSuperblock superblock = os::kernel::CreateFileSystemSuperblock();
    superblock.transaction_generation = OS_TEST_FILE_SYSTEM_FORMAT_GENERATION;
    os::kernel::FileSystemSuperblock decoded_superblock{};
    const bool superblock_round_trip =
        os::kernel::EncodeFileSystemSuperblock(
            superblock, superblock_bytes, os::kernel::OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        os::kernel::DecodeFileSystemSuperblock(
            superblock_bytes, os::kernel::OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES,
            decoded_superblock) == os::kernel::FileSystemFormatStatus::Succeeded &&
        decoded_superblock.version == superblock.version &&
        decoded_superblock.data_start_relative_block == superblock.data_start_relative_block &&
        decoded_superblock.transaction_generation == OS_TEST_FILE_SYSTEM_FORMAT_GENERATION;
    test_context.Expect(superblock_round_trip, OS_TEST_FILE_SYSTEM_FORMAT_SUPERBLOCK_ROUND_TRIP);

    os::kernel::FileSystemInode inode{
        .type = os::kernel::FileSystemNodeType::RegularFile,
        .size_bytes = OS_TEST_FILE_SYSTEM_FORMAT_FILE_SIZE_BYTES,
        .generation = OS_TEST_FILE_SYSTEM_FORMAT_GENERATION,
        .link_count = 1ULL,
        .allocated_block_count = 2ULL,
        .direct_blocks = {OS_TEST_FILE_SYSTEM_FORMAT_FIRST_DIRECT_BLOCK,
                          OS_TEST_FILE_SYSTEM_FORMAT_SECOND_DIRECT_BLOCK},
    };
    uint8_t inode_bytes[os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES]{};
    os::kernel::FileSystemInode decoded_inode{};
    const bool inode_round_trip =
        os::kernel::EncodeFileSystemInode(inode, inode_bytes,
                                          os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        os::kernel::DecodeFileSystemInode(
            inode_bytes, os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES, decoded_inode) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        decoded_inode.size_bytes == OS_TEST_FILE_SYSTEM_FORMAT_FILE_SIZE_BYTES &&
        decoded_inode.direct_blocks[1ULL] == OS_TEST_FILE_SYSTEM_FORMAT_SECOND_DIRECT_BLOCK;
    test_context.Expect(inode_round_trip, OS_TEST_FILE_SYSTEM_FORMAT_INODE_ROUND_TRIP);

    os::kernel::FileSystemDirectoryEntry entry{
        .inode_number = OS_TEST_FILE_SYSTEM_FORMAT_INODE_NUMBER,
        .type = os::kernel::FileSystemNodeType::RegularFile,
        .name_length_bytes = OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES,
        .name = {},
    };
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES;
         ++byte_index) {
        entry.name[byte_index] = OS_TEST_FILE_SYSTEM_FORMAT_NAME[byte_index];
    }
    uint8_t entry_bytes[os::kernel::OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES]{};
    os::kernel::FileSystemDirectoryEntry decoded_entry{};
    bool directory_round_trip =
        os::kernel::EncodeFileSystemDirectoryEntry(
            entry, entry_bytes, os::kernel::OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        os::kernel::DecodeFileSystemDirectoryEntry(
            entry_bytes, os::kernel::OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES,
            decoded_entry) == os::kernel::FileSystemFormatStatus::Succeeded &&
        decoded_entry.inode_number == OS_TEST_FILE_SYSTEM_FORMAT_INODE_NUMBER &&
        decoded_entry.name_length_bytes == OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES;
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES;
         ++byte_index) {
        directory_round_trip =
            directory_round_trip &&
            decoded_entry.name[byte_index] == OS_TEST_FILE_SYSTEM_FORMAT_NAME[byte_index];
    }
    test_context.Expect(directory_round_trip, OS_TEST_FILE_SYSTEM_FORMAT_DIRECTORY_ROUND_TRIP);

    superblock_bytes[OS_TEST_FILE_SYSTEM_FORMAT_CORRUPTION_OFFSET_BYTES] =
        static_cast<uint8_t>(superblock_bytes[OS_TEST_FILE_SYSTEM_FORMAT_CORRUPTION_OFFSET_BYTES] ^
                             OS_TEST_FILE_SYSTEM_FORMAT_CORRUPTION_MASK);
    test_context.Expect(os::kernel::DecodeFileSystemSuperblock(
                            superblock_bytes, os::kernel::OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES,
                            decoded_superblock) ==
                            os::kernel::FileSystemFormatStatus::InvalidChecksum,
                        OS_TEST_FILE_SYSTEM_FORMAT_DETECTS_CORRUPTION);

    return test_context.ExitCode();
}
