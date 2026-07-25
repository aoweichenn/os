#include "memory_block_device.hpp"
#include "os/kernel/file_system.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_SUITE_NAME =
    "kernel/file_system/lifecycle/integration";
constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_FORMATS_BLANK_DISK =
    "空白磁盘必须只在首次挂载时格式化并创建有效根目录";
constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_PERSISTS_FILE =
    "跨文件系统实例重挂载后必须完整读取多块文件";
constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_PATH_VALIDATION =
    "路径解析必须拒绝相对路径、空组件、点组件和过长名称";
constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_TRUNCATES_FILE =
    "截断必须释放数据块并保留可再次写入的文件节点";
constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_DETECTS_CORRUPTION =
    "非空且校验失败的超级块必须拒绝挂载而不能自动格式化";
constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_REJECTS_ORPHAN_INODE =
    "位图中不可从根目录到达的 inode 必须被一致性遍历拒绝";
constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_REJECTS_INVALID_NAME =
    "磁盘目录项中的控制字符名称必须被一致性遍历拒绝";
constexpr uint64_t OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES = 1300ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_LIFECYCLE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_LIFECYCLE_EXPECTED_DIRECTORY_BLOCK_COUNT = 2ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_LIFECYCLE_SUPERBLOCK_CORRUPTION_OFFSET = 64ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_LIFECYCLE_BITMAP_BYTE_OFFSET = 0ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_LIFECYCLE_ROOT_NAME_OFFSET_BYTES = 24ULL;
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_BYTE_MULTIPLIER = 37U;
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_BYTE_OFFSET = 11U;
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_CORRUPTION_MASK = 0x80U;
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_ORPHAN_INODE_MASK = 0x20U;
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_INVALID_NAME_MASK = 0x0CU;
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_SHARED_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('s'), static_cast<uint8_t>('h'),
    static_cast<uint8_t>('a'), static_cast<uint8_t>('r'), static_cast<uint8_t>('e'),
    static_cast<uint8_t>('d'),
};
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('s'), static_cast<uint8_t>('h'),
    static_cast<uint8_t>('a'), static_cast<uint8_t>('r'), static_cast<uint8_t>('e'),
    static_cast<uint8_t>('d'), static_cast<uint8_t>('/'), static_cast<uint8_t>('p'),
    static_cast<uint8_t>('a'), static_cast<uint8_t>('y'), static_cast<uint8_t>('l'),
    static_cast<uint8_t>('o'), static_cast<uint8_t>('a'), static_cast<uint8_t>('d'),
    static_cast<uint8_t>('.'), static_cast<uint8_t>('b'), static_cast<uint8_t>('i'),
    static_cast<uint8_t>('n'),
};
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_RELATIVE_PATH[] = {
    static_cast<uint8_t>('b'), static_cast<uint8_t>('a'), static_cast<uint8_t>('d'),
};
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_COMPONENT_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('a'), static_cast<uint8_t>('/'),
    static_cast<uint8_t>('/'), static_cast<uint8_t>('b'),
};
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_DOT_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('.'),
};
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_LONG_NAME_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('0'), static_cast<uint8_t>('1'),
    static_cast<uint8_t>('2'), static_cast<uint8_t>('3'), static_cast<uint8_t>('4'),
    static_cast<uint8_t>('5'), static_cast<uint8_t>('6'), static_cast<uint8_t>('7'),
    static_cast<uint8_t>('8'), static_cast<uint8_t>('9'), static_cast<uint8_t>('0'),
    static_cast<uint8_t>('1'), static_cast<uint8_t>('2'), static_cast<uint8_t>('3'),
    static_cast<uint8_t>('4'), static_cast<uint8_t>('5'), static_cast<uint8_t>('6'),
    static_cast<uint8_t>('7'), static_cast<uint8_t>('8'), static_cast<uint8_t>('9'),
    static_cast<uint8_t>('0'), static_cast<uint8_t>('1'), static_cast<uint8_t>('2'),
    static_cast<uint8_t>('3'), static_cast<uint8_t>('4'), static_cast<uint8_t>('5'),
    static_cast<uint8_t>('6'), static_cast<uint8_t>('7'), static_cast<uint8_t>('8'),
    static_cast<uint8_t>('9'), static_cast<uint8_t>('0'), static_cast<uint8_t>('1'),
    static_cast<uint8_t>('2'), static_cast<uint8_t>('3'), static_cast<uint8_t>('4'),
    static_cast<uint8_t>('5'), static_cast<uint8_t>('6'), static_cast<uint8_t>('7'),
    static_cast<uint8_t>('8'), static_cast<uint8_t>('9'), static_cast<uint8_t>('x'),
};

[[nodiscard]] uint8_t ExpectedPayloadByte(const uint64_t byteIndex) noexcept {
    return static_cast<uint8_t>(
        byteIndex *
            static_cast<uint64_t>(
                OS_TEST_FILE_SYSTEM_LIFECYCLE_BYTE_MULTIPLIER) +
        static_cast<uint64_t>(OS_TEST_FILE_SYSTEM_LIFECYCLE_BYTE_OFFSET));
}

}

int main() {
    os::test::TestContext testContext{
        OS_TEST_FILE_SYSTEM_LIFECYCLE_SUITE_NAME};
    static os::test::MemoryBlockDevice device{};

    os::kernel::FileSystem firstFileSystem{};
    bool formatted = false;
    const bool firstMountSucceeded =
        firstFileSystem.MountOrFormat(device, formatted) ==
            os::kernel::FileSystemStatus::Succeeded &&
        formatted &&
        firstFileSystem.CheckConsistency() ==
            os::kernel::FileSystemStatus::Succeeded;
    const os::kernel::FileSystemStatistics initialStatistics =
        firstFileSystem.Statistics();
    testContext.Expect(
        firstMountSucceeded &&
            initialStatistics.mountedDirectoryCount ==
                OS_TEST_FILE_SYSTEM_LIFECYCLE_COUNTER_INCREMENT &&
            initialStatistics.mountedFileCount ==
                OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_FORMATS_BLANK_DISK);

    const bool pathsRejected =
        firstFileSystem.CreateDirectory(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_RELATIVE_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_RELATIVE_PATH)) ==
            os::kernel::FileSystemStatus::InvalidPath &&
        firstFileSystem.CreateDirectory(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_COMPONENT_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_COMPONENT_PATH)) ==
            os::kernel::FileSystemStatus::InvalidPath &&
        firstFileSystem.CreateDirectory(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_DOT_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_DOT_PATH)) ==
            os::kernel::FileSystemStatus::InvalidPath &&
        firstFileSystem.CreateDirectory(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_LONG_NAME_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_LONG_NAME_PATH)) ==
            os::kernel::FileSystemStatus::NameTooLong;
    testContext.Expect(pathsRejected,
                       OS_TEST_FILE_SYSTEM_LIFECYCLE_PATH_VALIDATION);

    uint8_t payload[OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES]{};
    for (uint64_t byteIndex = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
         byteIndex < OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES;
         ++byteIndex) {
        payload[byteIndex] = ExpectedPayloadByte(byteIndex);
    }
    os::kernel::FileSystemHandle writeHandle{};
    const os::kernel::FileSystemOpenOptions createOptions{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = true,
    };
    uint64_t writtenBytes = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
    const bool createdAndWritten =
        firstFileSystem.CreateDirectory(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_SHARED_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_SHARED_PATH)) ==
            os::kernel::FileSystemStatus::Succeeded &&
        firstFileSystem.Open(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH), createOptions,
            writeHandle) == os::kernel::FileSystemStatus::Succeeded &&
        firstFileSystem.Write(
            writeHandle, payload,
            OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES,
            writtenBytes) == os::kernel::FileSystemStatus::Succeeded &&
        writtenBytes == OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES &&
        firstFileSystem.Close(writeHandle) ==
            os::kernel::FileSystemStatus::Succeeded &&
        firstFileSystem.Sync() == os::kernel::FileSystemStatus::Succeeded;

    os::kernel::FileSystem secondFileSystem{};
    formatted = true;
    uint8_t output[OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES]{};
    uint64_t readBytes = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
    os::kernel::FileSystemHandle readHandle{};
    const os::kernel::FileSystemOpenOptions readOptions{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
    };
    bool persisted =
        createdAndWritten &&
        secondFileSystem.MountOrFormat(device, formatted) ==
            os::kernel::FileSystemStatus::Succeeded &&
        !formatted &&
        secondFileSystem.Open(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH), readOptions,
            readHandle) == os::kernel::FileSystemStatus::Succeeded &&
        secondFileSystem.Read(
            readHandle, output,
            OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES,
            readBytes) == os::kernel::FileSystemStatus::Succeeded &&
        readBytes == OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES;
    for (uint64_t byteIndex = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
         byteIndex < OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES;
         ++byteIndex) {
        persisted = persisted && output[byteIndex] == payload[byteIndex];
    }
    testContext.Expect(persisted,
                       OS_TEST_FILE_SYSTEM_LIFECYCLE_PERSISTS_FILE);

    os::kernel::FileSystemHandle truncateHandle{};
    const os::kernel::FileSystemOpenOptions truncateOptions{
        .readable = true,
        .writable = true,
        .create = false,
        .truncate = true,
    };
    uint8_t emptyReadByte = 0U;
    readBytes = OS_TEST_FILE_SYSTEM_LIFECYCLE_COUNTER_INCREMENT;
    const bool truncated =
        secondFileSystem.Close(readHandle) ==
            os::kernel::FileSystemStatus::Succeeded &&
        secondFileSystem.Open(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH), truncateOptions,
            truncateHandle) == os::kernel::FileSystemStatus::Succeeded &&
        secondFileSystem.Read(
            truncateHandle, &emptyReadByte,
            OS_TEST_FILE_SYSTEM_LIFECYCLE_COUNTER_INCREMENT,
            readBytes) == os::kernel::FileSystemStatus::Succeeded &&
        readBytes == OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE &&
        secondFileSystem.Close(truncateHandle) ==
            os::kernel::FileSystemStatus::Succeeded &&
        secondFileSystem.CheckConsistency() ==
            os::kernel::FileSystemStatus::Succeeded &&
        secondFileSystem.Statistics().allocatedDataBlockCount ==
            OS_TEST_FILE_SYSTEM_LIFECYCLE_EXPECTED_DIRECTORY_BLOCK_COUNT;
    testContext.Expect(truncated,
                       OS_TEST_FILE_SYSTEM_LIFECYCLE_TRUNCATES_FILE);

    device.XorByte(
        os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA +
            os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_BITMAP_BYTE_OFFSET,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_ORPHAN_INODE_MASK);
    os::kernel::FileSystem orphanFileSystem{};
    formatted = true;
    testContext.Expect(
        orphanFileSystem.MountOrFormat(device, formatted) ==
                os::kernel::FileSystemStatus::Corrupt &&
            !formatted,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_REJECTS_ORPHAN_INODE);
    device.XorByte(
        os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA +
            os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_BITMAP_BYTE_OFFSET,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_ORPHAN_INODE_MASK);

    device.XorByte(
        os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA +
            os::kernel::OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_ROOT_NAME_OFFSET_BYTES,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_INVALID_NAME_MASK);
    os::kernel::FileSystem invalidNameFileSystem{};
    formatted = true;
    testContext.Expect(
        invalidNameFileSystem.MountOrFormat(device, formatted) ==
                os::kernel::FileSystemStatus::Corrupt &&
            !formatted,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_REJECTS_INVALID_NAME);
    device.XorByte(
        os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA +
            os::kernel::OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_ROOT_NAME_OFFSET_BYTES,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_INVALID_NAME_MASK);

    device.XorByte(
        os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_SUPERBLOCK_CORRUPTION_OFFSET,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_CORRUPTION_MASK);
    os::kernel::FileSystem corruptFileSystem{};
    formatted = true;
    testContext.Expect(
        corruptFileSystem.MountOrFormat(device, formatted) ==
                os::kernel::FileSystemStatus::Corrupt &&
            !formatted,
        OS_TEST_FILE_SYSTEM_LIFECYCLE_DETECTS_CORRUPTION);

    return testContext.ExitCode();
}
