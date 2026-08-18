#include "os/kernel/fs/devfs.hpp"
#include "os/kernel/fs/memfs.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DEVFS_SUITE_NAME = "kernel/devfs/unit";
constexpr std::string_view OS_TEST_DEVFS_REGISTRATION_CONTRACT =
    "devfs 注册表必须拒绝重名和容量溢出且保持已提交设备不变";
constexpr std::string_view OS_TEST_DEVFS_VNODE_CONTRACT =
    "/dev/console 必须以只读注册表中的字符设备 vnode 暴露";
constexpr std::string_view OS_TEST_DEVFS_DIRECTORY_CONTRACT =
    "/dev 目录必须按注册顺序枚举全部设备并稳定结束";
constexpr std::string_view OS_TEST_DEVFS_LIFETIME_CONTRACT =
    "设备 vnode 打开、保留、关闭和 VFS 资源统计必须守恒";

constexpr uint64_t OS_TEST_DEVFS_ROOT_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_DEVFS_SUPERBLOCK_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_DEVFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_DEVFS_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_DEVFS_HEAP_SIZE_BYTES = 256ULL * 1024ULL;
constexpr uint64_t OS_TEST_DEVFS_ROOT_NODE_LIMIT = 16ULL;
constexpr uint64_t OS_TEST_DEVFS_MAXIMUM_FILE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_DEVFS_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_DEVFS_DEVICE_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_DEVFS_EXPECTED_ACTIVE_OPEN_COUNT = 2ULL;
constexpr uint64_t OS_TEST_DEVFS_EXPECTED_DIRECTORY_READ_COUNT = 2ULL;
constexpr uint64_t OS_TEST_DEVFS_EXPECTED_REJECTION_COUNT = 2ULL;
constexpr uint64_t OS_TEST_DEVFS_EXPECTED_MINIMUM_VNODE_COUNT = 5ULL;
constexpr uint8_t OS_TEST_DEVFS_MOUNT_PATH[] = {'/', 'd', 'e', 'v'};
constexpr uint8_t OS_TEST_DEVFS_CONSOLE_PATH[] = {
    '/', 'd', 'e', 'v', '/', 'c', 'o', 'n', 's', 'o', 'l', 'e',
};
constexpr uint8_t OS_TEST_DEVFS_CONSOLE_NAME[] = {
    'c', 'o', 'n', 's', 'o', 'l', 'e',
};
constexpr uint8_t OS_TEST_DEVFS_TTY_NAME[] = {'t', 't', 'y', '0'};
constexpr uint8_t OS_TEST_DEVFS_OVERFLOW_NAME[] = {'n', 'u', 'l', 'l'};

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left,
                                 const uint8_t *const right,
                                 const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_TEST_DEVFS_EMPTY_VALUE;
         byte_index < length_bytes; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_DEVFS_SUITE_NAME};
    alignas(OS_TEST_DEVFS_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_bytes[OS_TEST_DEVFS_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs root_file_system{};
    os::kernel::fs::Devfs devfs{};
    os::kernel::fs::DevfsDevice
        device_storage[OS_TEST_DEVFS_DEVICE_CAPACITY]{};
    os::kernel::fs::Mount mounts[OS_TEST_DEVFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};

    uint64_t console_node_identifier = OS_TEST_DEVFS_EMPTY_VALUE;
    uint64_t tty_node_identifier = OS_TEST_DEVFS_EMPTY_VALUE;
    uint64_t rejected_node_identifier = OS_TEST_DEVFS_EMPTY_VALUE;
    const bool registration_valid =
        devfs.Initialize(OS_TEST_DEVFS_SUPERBLOCK_IDENTIFIER, device_storage,
                         OS_TEST_DEVFS_DEVICE_CAPACITY) ==
            os::kernel::fs::Status::Succeeded &&
        devfs.RegisterCharacterDevice(
            OS_TEST_DEVFS_CONSOLE_NAME, sizeof(OS_TEST_DEVFS_CONSOLE_NAME),
            console_node_identifier) == os::kernel::fs::Status::Succeeded &&
        devfs.RegisterCharacterDevice(OS_TEST_DEVFS_TTY_NAME,
                                      sizeof(OS_TEST_DEVFS_TTY_NAME),
                                      tty_node_identifier) ==
            os::kernel::fs::Status::Succeeded &&
        console_node_identifier != tty_node_identifier &&
        devfs.RegisterCharacterDevice(
            OS_TEST_DEVFS_CONSOLE_NAME, sizeof(OS_TEST_DEVFS_CONSOLE_NAME),
            rejected_node_identifier) == os::kernel::fs::Status::AlreadyExists &&
        rejected_node_identifier == OS_TEST_DEVFS_EMPTY_VALUE &&
        devfs.RegisterCharacterDevice(
            OS_TEST_DEVFS_OVERFLOW_NAME, sizeof(OS_TEST_DEVFS_OVERFLOW_NAME),
            rejected_node_identifier) ==
            os::kernel::fs::Status::CapacityExhausted &&
        devfs.ReadStatistics().registered_device_count ==
            OS_TEST_DEVFS_DEVICE_CAPACITY &&
        devfs.ReadStatistics().rejected_registration_count ==
            OS_TEST_DEVFS_EXPECTED_REJECTION_COUNT &&
        devfs.Validate() == os::kernel::fs::Status::Succeeded;
    test_context.Expect(registration_valid,
                        OS_TEST_DEVFS_REGISTRATION_CONTRACT);

    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_bytes),
                        sizeof(heap_bytes)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        root_file_system.Initialize(
            heap, OS_TEST_DEVFS_ROOT_SUPERBLOCK_IDENTIFIER,
            OS_TEST_DEVFS_ROOT_NODE_LIMIT,
            OS_TEST_DEVFS_MAXIMUM_FILE_SIZE_BYTES) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_DEVFS_MOUNT_CAPACITY,
                       root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_DEVFS_MOUNT_PATH,
                            sizeof(OS_TEST_DEVFS_MOUNT_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.MountAt(context, OS_TEST_DEVFS_MOUNT_PATH,
                    sizeof(OS_TEST_DEVFS_MOUNT_PATH),
                    devfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded;

    os::kernel::fs::Path console_path{};
    os::kernel::fs::NodeInformation console_information{};
    const bool vnode_contract_valid =
        initialized &&
        vfs.Resolve(context, OS_TEST_DEVFS_CONSOLE_PATH,
                    sizeof(OS_TEST_DEVFS_CONSOLE_PATH), console_path) ==
            os::kernel::fs::Status::Succeeded &&
        console_path.vnode.identifier == console_node_identifier &&
        console_path.vnode.type == os::kernel::fs::NodeType::CharacterDevice &&
        vfs.Stat(context, OS_TEST_DEVFS_CONSOLE_PATH,
                 sizeof(OS_TEST_DEVFS_CONSOLE_PATH), console_information) ==
            os::kernel::fs::Status::Succeeded &&
        console_information.type ==
            os::kernel::fs::NodeType::CharacterDevice &&
        console_information.size_bytes == OS_TEST_DEVFS_EMPTY_VALUE;
    test_context.Expect(vnode_contract_valid, OS_TEST_DEVFS_VNODE_CONTRACT);

    os::kernel::fs::OpenFile directory{};
    os::kernel::fs::DirectoryEntry first_entry{};
    os::kernel::fs::DirectoryEntry second_entry{};
    os::kernel::fs::DirectoryEntry end_entry{};
    bool first_end = true;
    bool second_end = true;
    bool final_end = false;
    const bool directory_contract_valid =
        vfs.OpenDirectory(context, OS_TEST_DEVFS_MOUNT_PATH,
                          sizeof(OS_TEST_DEVFS_MOUNT_PATH), directory) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ReadDirectory(directory, first_entry, first_end) ==
            os::kernel::fs::Status::Succeeded &&
        !first_end &&
        first_entry.node_identifier == console_node_identifier &&
        BytesAreEqual(first_entry.name, OS_TEST_DEVFS_CONSOLE_NAME,
                      sizeof(OS_TEST_DEVFS_CONSOLE_NAME)) &&
        vfs.ReadDirectory(directory, second_entry, second_end) ==
            os::kernel::fs::Status::Succeeded &&
        !second_end && second_entry.node_identifier == tty_node_identifier &&
        BytesAreEqual(second_entry.name, OS_TEST_DEVFS_TTY_NAME,
                      sizeof(OS_TEST_DEVFS_TTY_NAME)) &&
        vfs.ReadDirectory(directory, end_entry, final_end) ==
            os::kernel::fs::Status::Succeeded &&
        final_end &&
        vfs.Close(directory) == os::kernel::fs::Status::Succeeded &&
        devfs.ReadStatistics().directory_read_count ==
            OS_TEST_DEVFS_EXPECTED_DIRECTORY_READ_COUNT;
    test_context.Expect(directory_contract_valid,
                        OS_TEST_DEVFS_DIRECTORY_CONTRACT);

    const os::kernel::fs::OpenOptions read_write_options{
        .readable = true,
        .writable = true,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile console_file{};
    os::kernel::fs::OpenFile retained_console_file{};
    os::kernel::fs::ResourceUsage usage{};
    const bool lifetime_contract_valid =
        vnode_contract_valid &&
        vfs.Open(context, OS_TEST_DEVFS_CONSOLE_PATH,
                 sizeof(OS_TEST_DEVFS_CONSOLE_PATH), read_write_options,
                 console_file) == os::kernel::fs::Status::Succeeded &&
        vfs.RetainOpenFile(console_file, retained_console_file) ==
            os::kernel::fs::Status::Succeeded &&
        devfs.ReadStatistics().active_open_count ==
            OS_TEST_DEVFS_EXPECTED_ACTIVE_OPEN_COUNT &&
        vfs.Close(retained_console_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Close(console_file) == os::kernel::fs::Status::Succeeded &&
        devfs.ReadStatistics().active_open_count == OS_TEST_DEVFS_EMPTY_VALUE &&
        vfs.ReadResourceUsage(usage) == os::kernel::fs::Status::Succeeded &&
        usage.vnode_count >= OS_TEST_DEVFS_EXPECTED_MINIMUM_VNODE_COUNT &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        root_file_system.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_DEVFS_EMPTY_VALUE;
    test_context.Expect(lifetime_contract_valid,
                        OS_TEST_DEVFS_LIFETIME_CONTRACT);
    return test_context.ExitCode();
}
