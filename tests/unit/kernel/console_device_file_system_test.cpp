#include "os/kernel/fs/console_device_file_system.hpp"
#include "os/kernel/fs/memfs.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_CONSOLE_DEVICE_FS_SUITE_NAME =
    "kernel/console_device_file_system/unit";
constexpr std::string_view OS_TEST_CONSOLE_DEVICE_FS_VNODE_CONTRACT =
    "/dev/console 必须以只读命名空间中的字符设备 vnode 暴露";
constexpr std::string_view OS_TEST_CONSOLE_DEVICE_FS_OPEN_LIFETIME =
    "字符设备打开、保留和关闭必须保持 vnode 引用计数守恒";
constexpr std::string_view OS_TEST_CONSOLE_DEVICE_FS_DIRECTORY_CONTRACT =
    "/dev 目录枚举必须只返回 console 字符设备并稳定结束";
constexpr std::string_view OS_TEST_CONSOLE_DEVICE_FS_VALIDATION =
    "设备后端、VFS、memfs 与堆在释放全部引用后必须保持一致";

constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_ROOT_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_DEVICE_SUPERBLOCK_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_HEAP_SIZE_BYTES = 256ULL * 1024ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_ROOT_NODE_LIMIT = 16ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_MAXIMUM_FILE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_EXPECTED_OPEN_COUNT = 2ULL;
constexpr uint64_t OS_TEST_CONSOLE_DEVICE_FS_EXPECTED_DIRECTORY_READ_COUNT = 1ULL;
constexpr uint8_t OS_TEST_CONSOLE_DEVICE_FS_MOUNT_PATH[] = {'/', 'd', 'e', 'v'};
constexpr uint8_t OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_PATH[] = {
    '/', 'd', 'e', 'v', '/', 'c', 'o', 'n', 's', 'o', 'l', 'e',
};
constexpr uint8_t OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_NAME[] = {
    'c', 'o', 'n', 's', 'o', 'l', 'e',
};

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left, const uint8_t *const right,
                                 const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_TEST_CONSOLE_DEVICE_FS_EMPTY_VALUE;
         byte_index < length_bytes; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_CONSOLE_DEVICE_FS_SUITE_NAME};
    alignas(OS_TEST_CONSOLE_DEVICE_FS_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_bytes[OS_TEST_CONSOLE_DEVICE_FS_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs root_file_system{};
    os::kernel::fs::ConsoleDeviceFileSystem device_file_system{};
    os::kernel::fs::Mount mounts[OS_TEST_CONSOLE_DEVICE_FS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};

    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_bytes), sizeof(heap_bytes)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        root_file_system.Initialize(
            heap, OS_TEST_CONSOLE_DEVICE_FS_ROOT_SUPERBLOCK_IDENTIFIER,
            OS_TEST_CONSOLE_DEVICE_FS_ROOT_NODE_LIMIT,
            OS_TEST_CONSOLE_DEVICE_FS_MAXIMUM_FILE_SIZE_BYTES) ==
            os::kernel::fs::Status::Succeeded &&
        device_file_system.Initialize(
            OS_TEST_CONSOLE_DEVICE_FS_DEVICE_SUPERBLOCK_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_CONSOLE_DEVICE_FS_MOUNT_CAPACITY,
                       root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_CONSOLE_DEVICE_FS_MOUNT_PATH,
                            sizeof(OS_TEST_CONSOLE_DEVICE_FS_MOUNT_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.MountAt(context, OS_TEST_CONSOLE_DEVICE_FS_MOUNT_PATH,
                    sizeof(OS_TEST_CONSOLE_DEVICE_FS_MOUNT_PATH),
                    device_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded;

    os::kernel::fs::Path console_path{};
    os::kernel::fs::NodeInformation console_information{};
    const bool vnode_contract_valid =
        initialized &&
        vfs.Resolve(context, OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_PATH,
                    sizeof(OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_PATH), console_path) ==
            os::kernel::fs::Status::Succeeded &&
        console_path.vnode.type == os::kernel::fs::NodeType::CharacterDevice &&
        vfs.Stat(context, OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_PATH,
                 sizeof(OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_PATH), console_information) ==
            os::kernel::fs::Status::Succeeded &&
        console_information.type == os::kernel::fs::NodeType::CharacterDevice &&
        console_information.size_bytes == OS_TEST_CONSOLE_DEVICE_FS_EMPTY_VALUE;
    test_context.Expect(vnode_contract_valid, OS_TEST_CONSOLE_DEVICE_FS_VNODE_CONTRACT);

    const os::kernel::fs::OpenOptions read_write_options{
        .readable = true,
        .writable = true,
        .create = false,
        .truncate = false,
    };
    os::kernel::fs::OpenFile console_file{};
    os::kernel::fs::OpenFile retained_console_file{};
    const bool open_lifetime_valid =
        vnode_contract_valid &&
        vfs.Open(context, OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_PATH,
                 sizeof(OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_PATH), read_write_options,
                 console_file) == os::kernel::fs::Status::Succeeded &&
        console_file.path.vnode.type == os::kernel::fs::NodeType::CharacterDevice &&
        vfs.RetainOpenFile(console_file, retained_console_file) ==
            os::kernel::fs::Status::Succeeded &&
        device_file_system.ReadStatistics().active_open_count ==
            OS_TEST_CONSOLE_DEVICE_FS_EXPECTED_OPEN_COUNT &&
        vfs.Close(retained_console_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Close(console_file) == os::kernel::fs::Status::Succeeded &&
        device_file_system.ReadStatistics().active_open_count ==
            OS_TEST_CONSOLE_DEVICE_FS_EMPTY_VALUE;
    test_context.Expect(open_lifetime_valid, OS_TEST_CONSOLE_DEVICE_FS_OPEN_LIFETIME);

    os::kernel::fs::OpenFile directory{};
    os::kernel::fs::DirectoryEntry entry{};
    bool end_of_directory = false;
    bool second_end_of_directory = false;
    const bool directory_contract_valid =
        vfs.OpenDirectory(context, OS_TEST_CONSOLE_DEVICE_FS_MOUNT_PATH,
                          sizeof(OS_TEST_CONSOLE_DEVICE_FS_MOUNT_PATH), directory) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ReadDirectory(directory, entry, end_of_directory) ==
            os::kernel::fs::Status::Succeeded &&
        !end_of_directory && entry.type == os::kernel::fs::NodeType::CharacterDevice &&
        entry.name_length_bytes == sizeof(OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_NAME) &&
        BytesAreEqual(entry.name, OS_TEST_CONSOLE_DEVICE_FS_CONSOLE_NAME,
                      entry.name_length_bytes) &&
        vfs.ReadDirectory(directory, entry, second_end_of_directory) ==
            os::kernel::fs::Status::Succeeded &&
        second_end_of_directory &&
        vfs.Close(directory) == os::kernel::fs::Status::Succeeded &&
        device_file_system.ReadStatistics().directory_read_count ==
            OS_TEST_CONSOLE_DEVICE_FS_EXPECTED_DIRECTORY_READ_COUNT;
    test_context.Expect(directory_contract_valid,
                        OS_TEST_CONSOLE_DEVICE_FS_DIRECTORY_CONTRACT);

    const bool validation_valid =
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        device_file_system.Validate() == os::kernel::fs::Status::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        root_file_system.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_CONSOLE_DEVICE_FS_EMPTY_VALUE;
    test_context.Expect(validation_valid, OS_TEST_CONSOLE_DEVICE_FS_VALIDATION);
    return test_context.ExitCode();
}
