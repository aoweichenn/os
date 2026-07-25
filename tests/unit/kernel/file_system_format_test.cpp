#include "os/kernel/file_system_format.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_SYSTEM_FORMAT_SUITE_NAME =
    "kernel/file_system_format/unit";
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
    static_cast<uint8_t>('d'), static_cast<uint8_t>('a'), static_cast<uint8_t>('t'),
    static_cast<uint8_t>('a'),
};

}

int main() {
    os::test::TestContext testContext{OS_TEST_FILE_SYSTEM_FORMAT_SUITE_NAME};

    uint8_t superblockBytes[os::kernel::OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES]{};
    os::kernel::FileSystemSuperblock superblock =
        os::kernel::CreateFileSystemSuperblock();
    superblock.transactionGeneration = OS_TEST_FILE_SYSTEM_FORMAT_GENERATION;
    os::kernel::FileSystemSuperblock decodedSuperblock{};
    const bool superblockRoundTrip =
        os::kernel::EncodeFileSystemSuperblock(
            superblock, superblockBytes,
            os::kernel::OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        os::kernel::DecodeFileSystemSuperblock(
            superblockBytes, os::kernel::OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES,
            decodedSuperblock) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        decodedSuperblock.version == superblock.version &&
        decodedSuperblock.dataStartRelativeBlock ==
            superblock.dataStartRelativeBlock &&
        decodedSuperblock.transactionGeneration ==
            OS_TEST_FILE_SYSTEM_FORMAT_GENERATION;
    testContext.Expect(superblockRoundTrip,
                       OS_TEST_FILE_SYSTEM_FORMAT_SUPERBLOCK_ROUND_TRIP);

    os::kernel::FileSystemInode inode{
        .type = os::kernel::FileSystemNodeType::RegularFile,
        .sizeBytes = OS_TEST_FILE_SYSTEM_FORMAT_FILE_SIZE_BYTES,
        .generation = OS_TEST_FILE_SYSTEM_FORMAT_GENERATION,
        .linkCount = 1ULL,
        .allocatedBlockCount = 2ULL,
        .directBlocks = {OS_TEST_FILE_SYSTEM_FORMAT_FIRST_DIRECT_BLOCK,
                         OS_TEST_FILE_SYSTEM_FORMAT_SECOND_DIRECT_BLOCK},
    };
    uint8_t inodeBytes[os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES]{};
    os::kernel::FileSystemInode decodedInode{};
    const bool inodeRoundTrip =
        os::kernel::EncodeFileSystemInode(
            inode, inodeBytes,
            os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        os::kernel::DecodeFileSystemInode(
            inodeBytes, os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_SIZE_BYTES,
            decodedInode) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        decodedInode.sizeBytes == OS_TEST_FILE_SYSTEM_FORMAT_FILE_SIZE_BYTES &&
        decodedInode.directBlocks[1ULL] ==
            OS_TEST_FILE_SYSTEM_FORMAT_SECOND_DIRECT_BLOCK;
    testContext.Expect(inodeRoundTrip,
                       OS_TEST_FILE_SYSTEM_FORMAT_INODE_ROUND_TRIP);

    os::kernel::FileSystemDirectoryEntry entry{
        .inodeNumber = OS_TEST_FILE_SYSTEM_FORMAT_INODE_NUMBER,
        .type = os::kernel::FileSystemNodeType::RegularFile,
        .nameLengthBytes = OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES,
        .name = {},
    };
    for (uint64_t byteIndex = 0ULL;
         byteIndex < OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES; ++byteIndex) {
        entry.name[byteIndex] = OS_TEST_FILE_SYSTEM_FORMAT_NAME[byteIndex];
    }
    uint8_t entryBytes[os::kernel::OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES]{};
    os::kernel::FileSystemDirectoryEntry decodedEntry{};
    bool directoryRoundTrip =
        os::kernel::EncodeFileSystemDirectoryEntry(
            entry, entryBytes,
            os::kernel::OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES) ==
            os::kernel::FileSystemFormatStatus::Succeeded &&
        os::kernel::DecodeFileSystemDirectoryEntry(
            entryBytes,
            os::kernel::OS_KERNEL_FILE_SYSTEM_DIRECTORY_ENTRY_SIZE_BYTES,
            decodedEntry) == os::kernel::FileSystemFormatStatus::Succeeded &&
        decodedEntry.inodeNumber == OS_TEST_FILE_SYSTEM_FORMAT_INODE_NUMBER &&
        decodedEntry.nameLengthBytes ==
            OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES;
    for (uint64_t byteIndex = 0ULL;
         byteIndex < OS_TEST_FILE_SYSTEM_FORMAT_NAME_LENGTH_BYTES; ++byteIndex) {
        directoryRoundTrip =
            directoryRoundTrip &&
            decodedEntry.name[byteIndex] ==
                OS_TEST_FILE_SYSTEM_FORMAT_NAME[byteIndex];
    }
    testContext.Expect(directoryRoundTrip,
                       OS_TEST_FILE_SYSTEM_FORMAT_DIRECTORY_ROUND_TRIP);

    superblockBytes[OS_TEST_FILE_SYSTEM_FORMAT_CORRUPTION_OFFSET_BYTES] =
        static_cast<uint8_t>(
            superblockBytes[OS_TEST_FILE_SYSTEM_FORMAT_CORRUPTION_OFFSET_BYTES] ^
            OS_TEST_FILE_SYSTEM_FORMAT_CORRUPTION_MASK);
    testContext.Expect(
        os::kernel::DecodeFileSystemSuperblock(
            superblockBytes, os::kernel::OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES,
            decodedSuperblock) ==
            os::kernel::FileSystemFormatStatus::InvalidChecksum,
        OS_TEST_FILE_SYSTEM_FORMAT_DETECTS_CORRUPTION);

    return testContext.ExitCode();
}
