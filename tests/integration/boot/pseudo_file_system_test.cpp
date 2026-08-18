#include "os/kernel/fs/devfs.hpp"
#include "os/kernel/fs/memfs.hpp"
#include "os/kernel/fs/procfs.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PSEUDO_FS_SUITE_NAME =
    "kernel/pseudo_file_system/integration";
constexpr std::string_view OS_TEST_PSEUDO_FS_MOUNT_CONTRACT =
    "memfs 根、devfs 与 procfs 必须在同一 VFS 命名空间独立跨越挂载点";
constexpr std::string_view OS_TEST_PSEUDO_FS_READ_ONLY_CONTRACT =
    "devfs/procfs 命名空间修改必须稳定返回只读错误";
constexpr std::string_view OS_TEST_PSEUDO_FS_RESOURCE_CONTRACT =
    "三后端资源统计、验证和引用释放必须共同守恒";

constexpr uint64_t OS_TEST_PSEUDO_FS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_HEAP_SIZE_BYTES = 256ULL * 1024ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_ROOT_NODE_LIMIT = 32ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_MAXIMUM_FILE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_DEVICE_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_ROOT_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_DEV_SUPERBLOCK_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_PROC_SUPERBLOCK_IDENTIFIER = 3ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_MONOTONIC_NANOSECONDS = 9ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_MANAGED_MEMORY_BYTES = 64ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_FREE_MEMORY_BYTES = 32ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_ALLOCATED_MEMORY_BYTES = 16ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_ACTIVE_PROCESS_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_ACTIVE_THREAD_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_PROCESS_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_THREAD_CAPACITY = 128ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_CURRENT_PROCESS_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_HEAP_CONSUMED_BYTES = 8ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_ACTIVE_FILE_DESCRIPTION_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_SNAPSHOT_MOUNT_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_SNAPSHOT_VNODE_COUNT = 10ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_JOURNAL_COMMIT_COUNT = 2ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_EXPECTED_MOUNT_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_MINIMUM_MOUNT_TRANSITION_COUNT = 2ULL;
constexpr uint64_t OS_TEST_PSEUDO_FS_MINIMUM_NON_PROCFS_VNODE_COUNT = 5ULL;
constexpr uint8_t OS_TEST_PSEUDO_FS_DEV_PATH[] = {'/', 'd', 'e', 'v'};
constexpr uint8_t OS_TEST_PSEUDO_FS_PROC_PATH[] = {'/', 'p', 'r', 'o', 'c'};
constexpr uint8_t OS_TEST_PSEUDO_FS_CONSOLE_PATH[] = {
    '/', 'd', 'e', 'v', '/', 'c', 'o', 'n', 's', 'o', 'l', 'e',
};
constexpr uint8_t OS_TEST_PSEUDO_FS_VERSION_PATH[] = {
    '/', 'p', 'r', 'o', 'c', '/', 'v', 'e', 'r', 's', 'i', 'o', 'n',
};
constexpr uint8_t OS_TEST_PSEUDO_FS_CONSOLE_NAME[] = {
    'c', 'o', 'n', 's', 'o', 'l', 'e',
};
constexpr uint8_t OS_TEST_PSEUDO_FS_FORBIDDEN_PATH[] = {
    '/', 'p', 'r', 'o', 'c', '/', 'n', 'e', 'w',
};

[[nodiscard]] bool CaptureSnapshot(
    void *const context,
    os::kernel::fs::ProcfsSnapshot &snapshot) noexcept {
    if (context == nullptr) {
        return false;
    }
    snapshot = *static_cast<const os::kernel::fs::ProcfsSnapshot *>(context);
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PSEUDO_FS_SUITE_NAME};
    alignas(OS_TEST_PSEUDO_FS_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_bytes[OS_TEST_PSEUDO_FS_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs memfs{};
    os::kernel::fs::Devfs devfs{};
    os::kernel::fs::DevfsDevice
        devices[OS_TEST_PSEUDO_FS_DEVICE_CAPACITY]{};
    os::kernel::fs::Procfs procfs{};
    os::kernel::fs::ProcfsSnapshot snapshot{
        .monotonic_nanoseconds = OS_TEST_PSEUDO_FS_MONOTONIC_NANOSECONDS,
        .managed_memory_bytes = OS_TEST_PSEUDO_FS_MANAGED_MEMORY_BYTES,
        .free_memory_bytes = OS_TEST_PSEUDO_FS_FREE_MEMORY_BYTES,
        .allocated_memory_bytes = OS_TEST_PSEUDO_FS_ALLOCATED_MEMORY_BYTES,
        .active_process_count = OS_TEST_PSEUDO_FS_ACTIVE_PROCESS_COUNT,
        .active_thread_count = OS_TEST_PSEUDO_FS_ACTIVE_THREAD_COUNT,
        .process_capacity = OS_TEST_PSEUDO_FS_PROCESS_CAPACITY,
        .thread_capacity = OS_TEST_PSEUDO_FS_THREAD_CAPACITY,
        .current_process_id =
            OS_TEST_PSEUDO_FS_CURRENT_PROCESS_IDENTIFIER,
        .heap_consumed_bytes = OS_TEST_PSEUDO_FS_HEAP_CONSUMED_BYTES,
        .active_file_description_count =
            OS_TEST_PSEUDO_FS_ACTIVE_FILE_DESCRIPTION_COUNT,
        .active_pipe_count = OS_TEST_PSEUDO_FS_EMPTY_VALUE,
        .mount_count = OS_TEST_PSEUDO_FS_SNAPSHOT_MOUNT_COUNT,
        .vnode_count = OS_TEST_PSEUDO_FS_SNAPSHOT_VNODE_COUNT,
        .journal_commit_count = OS_TEST_PSEUDO_FS_JOURNAL_COMMIT_COUNT,
    };
    os::kernel::fs::Mount mounts[OS_TEST_PSEUDO_FS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};
    uint64_t console_node_identifier = OS_TEST_PSEUDO_FS_EMPTY_VALUE;

    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_bytes),
                        sizeof(heap_bytes)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        memfs.Initialize(
            heap, OS_TEST_PSEUDO_FS_ROOT_SUPERBLOCK_IDENTIFIER,
            OS_TEST_PSEUDO_FS_ROOT_NODE_LIMIT,
            OS_TEST_PSEUDO_FS_MAXIMUM_FILE_SIZE_BYTES) ==
            os::kernel::fs::Status::Succeeded &&
        devfs.Initialize(OS_TEST_PSEUDO_FS_DEV_SUPERBLOCK_IDENTIFIER, devices,
                         OS_TEST_PSEUDO_FS_DEVICE_CAPACITY) ==
            os::kernel::fs::Status::Succeeded &&
        devfs.RegisterCharacterDevice(
            OS_TEST_PSEUDO_FS_CONSOLE_NAME,
            sizeof(OS_TEST_PSEUDO_FS_CONSOLE_NAME), console_node_identifier) ==
            os::kernel::fs::Status::Succeeded &&
        procfs.Initialize(OS_TEST_PSEUDO_FS_PROC_SUPERBLOCK_IDENTIFIER,
                          CaptureSnapshot, &snapshot) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_PSEUDO_FS_MOUNT_CAPACITY,
                       memfs.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_PSEUDO_FS_DEV_PATH,
                            sizeof(OS_TEST_PSEUDO_FS_DEV_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_PSEUDO_FS_PROC_PATH,
                            sizeof(OS_TEST_PSEUDO_FS_PROC_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.MountAt(context, OS_TEST_PSEUDO_FS_DEV_PATH,
                    sizeof(OS_TEST_PSEUDO_FS_DEV_PATH),
                    devfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        vfs.MountAt(context, OS_TEST_PSEUDO_FS_PROC_PATH,
                    sizeof(OS_TEST_PSEUDO_FS_PROC_PATH),
                    procfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded;

    os::kernel::fs::Path console_path{};
    os::kernel::fs::Path version_path{};
    const bool mount_contract_valid =
        initialized &&
        vfs.Resolve(context, OS_TEST_PSEUDO_FS_CONSOLE_PATH,
                    sizeof(OS_TEST_PSEUDO_FS_CONSOLE_PATH), console_path) ==
            os::kernel::fs::Status::Succeeded &&
        console_path.vnode.identifier == console_node_identifier &&
        console_path.vnode.type ==
            os::kernel::fs::NodeType::CharacterDevice &&
        vfs.Resolve(context, OS_TEST_PSEUDO_FS_VERSION_PATH,
                    sizeof(OS_TEST_PSEUDO_FS_VERSION_PATH), version_path) ==
            os::kernel::fs::Status::Succeeded &&
        version_path.vnode.type == os::kernel::fs::NodeType::RegularFile &&
        vfs.ReadStatistics().mount_count ==
            OS_TEST_PSEUDO_FS_EXPECTED_MOUNT_COUNT &&
        vfs.ReadStatistics().mount_transition_count >=
            OS_TEST_PSEUDO_FS_MINIMUM_MOUNT_TRANSITION_COUNT;
    test_context.Expect(mount_contract_valid,
                        OS_TEST_PSEUDO_FS_MOUNT_CONTRACT);

    const os::kernel::fs::OpenOptions create_options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile forbidden_file{};
    const bool read_only_contract_valid =
        vfs.Open(context, OS_TEST_PSEUDO_FS_FORBIDDEN_PATH,
                 sizeof(OS_TEST_PSEUDO_FS_FORBIDDEN_PATH), create_options,
                 forbidden_file) == os::kernel::fs::Status::ReadOnly &&
        !forbidden_file.open;
    test_context.Expect(read_only_contract_valid,
                        OS_TEST_PSEUDO_FS_READ_ONLY_CONTRACT);

    os::kernel::fs::ResourceUsage usage{};
    const bool resource_contract_valid =
        vfs.ReadResourceUsage(usage) == os::kernel::fs::Status::Succeeded &&
        usage.vnode_count >=
            os::kernel::fs::OS_KERNEL_PROCFS_FILE_COUNT +
                OS_TEST_PSEUDO_FS_MINIMUM_NON_PROCFS_VNODE_COUNT &&
        devfs.Validate() == os::kernel::fs::Status::Succeeded &&
        procfs.Validate() == os::kernel::fs::Status::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_PSEUDO_FS_EMPTY_VALUE;
    test_context.Expect(resource_contract_valid,
                        OS_TEST_PSEUDO_FS_RESOURCE_CONTRACT);
    return test_context.ExitCode();
}
