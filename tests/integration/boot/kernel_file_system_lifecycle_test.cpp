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
constexpr std::string_view OS_TEST_FILE_SYSTEM_LIFECYCLE_READS_DIRECTORY =
    "目录句柄必须按稳定顺序读取类型、名称和目录结束";
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
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_EXPECTED_FILE_NAME[] = {
    static_cast<uint8_t>('p'), static_cast<uint8_t>('a'), static_cast<uint8_t>('y'),
    static_cast<uint8_t>('l'), static_cast<uint8_t>('o'), static_cast<uint8_t>('a'),
    static_cast<uint8_t>('d'), static_cast<uint8_t>('.'), static_cast<uint8_t>('b'),
    static_cast<uint8_t>('i'), static_cast<uint8_t>('n'),
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
    static_cast<uint8_t>('b'),
    static_cast<uint8_t>('a'),
    static_cast<uint8_t>('d'),
};
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_COMPONENT_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('a'), static_cast<uint8_t>('/'),
    static_cast<uint8_t>('/'), static_cast<uint8_t>('b'),
};
constexpr uint8_t OS_TEST_FILE_SYSTEM_LIFECYCLE_DOT_PATH[] = {
    static_cast<uint8_t>('/'),
    static_cast<uint8_t>('.'),
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

[[nodiscard]] uint8_t ExpectedPayloadByte(const uint64_t byte_index) noexcept {
    return static_cast<uint8_t>(
        byte_index * static_cast<uint64_t>(OS_TEST_FILE_SYSTEM_LIFECYCLE_BYTE_MULTIPLIER) +
        static_cast<uint64_t>(OS_TEST_FILE_SYSTEM_LIFECYCLE_BYTE_OFFSET));
}

[[nodiscard]] bool DirectoryEntryNameEquals(const os::kernel::FileSystemDirectoryEntry &entry,
                                            const uint8_t *const expected_name,
                                            const uint64_t expected_name_length_bytes) noexcept {
    if (expected_name == nullptr || entry.name_length_bytes != expected_name_length_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
         byte_index < expected_name_length_bytes; ++byte_index) {
        if (entry.name[byte_index] != expected_name[byte_index]) {
            return false;
        }
    }
    return true;
}
}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_SYSTEM_LIFECYCLE_SUITE_NAME};
    static os::test::MemoryBlockDevice device{};

    os::kernel::FileSystem first_file_system{};
    bool formatted = false;
    const bool first_mount_succeeded =
        first_file_system.MountOrFormat(device, formatted) ==
            os::kernel::FileSystemStatus::Succeeded &&
        formatted &&
        first_file_system.CheckConsistency() == os::kernel::FileSystemStatus::Succeeded;
    const os::kernel::FileSystemStatistics initial_statistics = first_file_system.Statistics();
    test_context.Expect(first_mount_succeeded &&
                            initial_statistics.mounted_directory_count ==
                                OS_TEST_FILE_SYSTEM_LIFECYCLE_COUNTER_INCREMENT &&
                            initial_statistics.mounted_file_count ==
                                OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE,
                        OS_TEST_FILE_SYSTEM_LIFECYCLE_FORMATS_BLANK_DISK);

    const bool paths_rejected =
        first_file_system.CreateDirectory(OS_TEST_FILE_SYSTEM_LIFECYCLE_RELATIVE_PATH,
                                          sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_RELATIVE_PATH)) ==
            os::kernel::FileSystemStatus::InvalidPath &&
        first_file_system.CreateDirectory(
            OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_COMPONENT_PATH,
            sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_COMPONENT_PATH)) ==
            os::kernel::FileSystemStatus::InvalidPath &&
        first_file_system.CreateDirectory(OS_TEST_FILE_SYSTEM_LIFECYCLE_DOT_PATH,
                                          sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_DOT_PATH)) ==
            os::kernel::FileSystemStatus::InvalidPath &&
        first_file_system.CreateDirectory(OS_TEST_FILE_SYSTEM_LIFECYCLE_LONG_NAME_PATH,
                                          sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_LONG_NAME_PATH)) ==
            os::kernel::FileSystemStatus::NameTooLong;
    test_context.Expect(paths_rejected, OS_TEST_FILE_SYSTEM_LIFECYCLE_PATH_VALIDATION);

    uint8_t payload[OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES]{};
    for (uint64_t byte_index = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
         byte_index < OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES; ++byte_index) {
        payload[byte_index] = ExpectedPayloadByte(byte_index);
    }
    os::kernel::FileSystemHandle write_handle{};
    const os::kernel::FileSystemOpenOptions create_options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = true,
    };
    uint64_t written_bytes = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
    const bool created_and_written =
        first_file_system.CreateDirectory(OS_TEST_FILE_SYSTEM_LIFECYCLE_SHARED_PATH,
                                          sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_SHARED_PATH)) ==
            os::kernel::FileSystemStatus::Succeeded &&
        first_file_system.Open(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH,
                               sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH), create_options,
                               write_handle) == os::kernel::FileSystemStatus::Succeeded &&
        first_file_system.Write(write_handle, payload,
                                OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES,
                                written_bytes) == os::kernel::FileSystemStatus::Succeeded &&
        written_bytes == OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES &&
        first_file_system.Close(write_handle) == os::kernel::FileSystemStatus::Succeeded &&
        first_file_system.Sync() == os::kernel::FileSystemStatus::Succeeded;

    os::kernel::FileSystem second_file_system{};
    formatted = true;
    uint8_t output[OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES]{};
    uint64_t read_bytes = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
    os::kernel::FileSystemHandle read_handle{};
    const os::kernel::FileSystemOpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
    };
    bool persisted =
        created_and_written &&
        second_file_system.MountOrFormat(device, formatted) ==
            os::kernel::FileSystemStatus::Succeeded &&
        !formatted &&
        second_file_system.Open(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH,
                                sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH), read_options,
                                read_handle) == os::kernel::FileSystemStatus::Succeeded &&
        second_file_system.Read(read_handle, output,
                                OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES,
                                read_bytes) == os::kernel::FileSystemStatus::Succeeded &&
        read_bytes == OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES;
    for (uint64_t byte_index = OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE;
         byte_index < OS_TEST_FILE_SYSTEM_LIFECYCLE_PAYLOAD_SIZE_BYTES; ++byte_index) {
        persisted = persisted && output[byte_index] == payload[byte_index];
    }
    test_context.Expect(persisted, OS_TEST_FILE_SYSTEM_LIFECYCLE_PERSISTS_FILE);

    os::kernel::FileSystemHandle directory_handle{};
    os::kernel::FileSystemDirectoryEntry directory_entry{};
    bool end_of_directory = true;
    const bool directory_read =
        second_file_system.OpenDirectory(OS_TEST_FILE_SYSTEM_LIFECYCLE_SHARED_PATH,
                                         sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_SHARED_PATH),
                                         directory_handle) ==
            os::kernel::FileSystemStatus::Succeeded &&
        second_file_system.ReadDirectory(directory_handle, directory_entry, end_of_directory) ==
            os::kernel::FileSystemStatus::Succeeded &&
        !end_of_directory && directory_entry.type == os::kernel::FileSystemNodeType::RegularFile &&
        DirectoryEntryNameEquals(directory_entry, OS_TEST_FILE_SYSTEM_LIFECYCLE_EXPECTED_FILE_NAME,
                                 sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_EXPECTED_FILE_NAME)) &&
        second_file_system.ReadDirectory(directory_handle, directory_entry, end_of_directory) ==
            os::kernel::FileSystemStatus::Succeeded &&
        end_of_directory &&
        second_file_system.Close(directory_handle) == os::kernel::FileSystemStatus::Succeeded;
    test_context.Expect(directory_read, OS_TEST_FILE_SYSTEM_LIFECYCLE_READS_DIRECTORY);

    os::kernel::FileSystemHandle truncate_handle{};
    const os::kernel::FileSystemOpenOptions truncate_options{
        .readable = true,
        .writable = true,
        .create = false,
        .truncate = true,
    };
    uint8_t empty_read_byte = 0U;
    read_bytes = OS_TEST_FILE_SYSTEM_LIFECYCLE_COUNTER_INCREMENT;
    const bool truncated =
        second_file_system.Close(read_handle) == os::kernel::FileSystemStatus::Succeeded &&
        second_file_system.Open(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH,
                                sizeof(OS_TEST_FILE_SYSTEM_LIFECYCLE_FILE_PATH), truncate_options,
                                truncate_handle) == os::kernel::FileSystemStatus::Succeeded &&
        second_file_system.Read(truncate_handle, &empty_read_byte,
                                OS_TEST_FILE_SYSTEM_LIFECYCLE_COUNTER_INCREMENT,
                                read_bytes) == os::kernel::FileSystemStatus::Succeeded &&
        read_bytes == OS_TEST_FILE_SYSTEM_LIFECYCLE_EMPTY_VALUE &&
        second_file_system.Close(truncate_handle) == os::kernel::FileSystemStatus::Succeeded &&
        second_file_system.CheckConsistency() == os::kernel::FileSystemStatus::Succeeded &&
        second_file_system.Statistics().allocated_data_block_count ==
            OS_TEST_FILE_SYSTEM_LIFECYCLE_EXPECTED_DIRECTORY_BLOCK_COUNT;
    test_context.Expect(truncated, OS_TEST_FILE_SYSTEM_LIFECYCLE_TRUNCATES_FILE);

    device.XorByte(os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA +
                       os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_BITMAP_BYTE_OFFSET,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_ORPHAN_INODE_MASK);
    os::kernel::FileSystem orphan_file_system{};
    formatted = true;
    test_context.Expect(orphan_file_system.MountOrFormat(device, formatted) ==
                                os::kernel::FileSystemStatus::Corrupt &&
                            !formatted,
                        OS_TEST_FILE_SYSTEM_LIFECYCLE_REJECTS_ORPHAN_INODE);
    device.XorByte(os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA +
                       os::kernel::OS_KERNEL_FILE_SYSTEM_INODE_BITMAP_RELATIVE_BLOCK,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_BITMAP_BYTE_OFFSET,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_ORPHAN_INODE_MASK);

    device.XorByte(os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA +
                       os::kernel::OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_ROOT_NAME_OFFSET_BYTES,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_INVALID_NAME_MASK);
    os::kernel::FileSystem invalid_name_file_system{};
    formatted = true;
    test_context.Expect(invalid_name_file_system.MountOrFormat(device, formatted) ==
                                os::kernel::FileSystemStatus::Corrupt &&
                            !formatted,
                        OS_TEST_FILE_SYSTEM_LIFECYCLE_REJECTS_INVALID_NAME);
    device.XorByte(os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA +
                       os::kernel::OS_KERNEL_FILE_SYSTEM_DATA_START_RELATIVE_BLOCK,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_ROOT_NAME_OFFSET_BYTES,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_INVALID_NAME_MASK);

    device.XorByte(os::kernel::OS_KERNEL_FILE_SYSTEM_START_LBA,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_SUPERBLOCK_CORRUPTION_OFFSET,
                   OS_TEST_FILE_SYSTEM_LIFECYCLE_CORRUPTION_MASK);
    os::kernel::FileSystem corrupt_file_system{};
    formatted = true;
    test_context.Expect(corrupt_file_system.MountOrFormat(device, formatted) ==
                                os::kernel::FileSystemStatus::Corrupt &&
                            !formatted,
                        OS_TEST_FILE_SYSTEM_LIFECYCLE_DETECTS_CORRUPTION);

    return test_context.ExitCode();
}
