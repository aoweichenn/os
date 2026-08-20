#include <os/kernel/fs/memfs.hpp>
#include <os/kernel/fs/procfs.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCFS_SUITE_NAME = "kernel/procfs/unit";
constexpr std::string_view OS_TEST_PROCFS_SNAPSHOT_CONTRACT =
    "procfs 必须把一次采样格式化为有界、可短读的只读文本";
constexpr std::string_view OS_TEST_PROCFS_DIRECTORY_CONTRACT =
    "procfs 根目录必须稳定枚举 ABI、时间、内存、进程、资源与挂载文件";
constexpr std::string_view OS_TEST_PROCFS_FAILURE_CONTRACT =
    "采样失败必须作为设备失败传播且不伪造旧快照";
constexpr std::string_view OS_TEST_PROCFS_LIFETIME_CONTRACT =
    "procfs 引用、统计、VFS 与堆资源必须在关闭后守恒";

constexpr uint64_t OS_TEST_PROCFS_ROOT_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_PROCFS_SUPERBLOCK_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_PROCFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PROCFS_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_PROCFS_HEAP_SIZE_BYTES = 256ULL * 1024ULL;
constexpr uint64_t OS_TEST_PROCFS_ROOT_NODE_LIMIT = 16ULL;
constexpr uint64_t OS_TEST_PROCFS_MAXIMUM_FILE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_PROCFS_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_PROCFS_EXPECTED_MANAGED_BYTES = 64ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_PROCFS_EXPECTED_FREE_BYTES = 48ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_PROCFS_EXPECTED_ALLOCATED_BYTES = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_PROCFS_MONOTONIC_NANOSECONDS = 123456789ULL;
constexpr uint64_t OS_TEST_PROCFS_ACTIVE_PROCESS_COUNT = 7ULL;
constexpr uint64_t OS_TEST_PROCFS_ACTIVE_THREAD_COUNT = 11ULL;
constexpr uint64_t OS_TEST_PROCFS_PROCESS_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_PROCFS_THREAD_CAPACITY = 128ULL;
constexpr uint64_t OS_TEST_PROCFS_CURRENT_PROCESS_IDENTIFIER = 5ULL;
constexpr uint64_t OS_TEST_PROCFS_HEAP_CONSUMED_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_PROCFS_ACTIVE_FILE_DESCRIPTION_COUNT = 9ULL;
constexpr uint64_t OS_TEST_PROCFS_ACTIVE_PIPE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PROCFS_SNAPSHOT_MOUNT_COUNT = 3ULL;
constexpr uint64_t OS_TEST_PROCFS_SNAPSHOT_VNODE_COUNT = 19ULL;
constexpr uint64_t OS_TEST_PROCFS_JOURNAL_COMMIT_COUNT = 17ULL;
constexpr uint64_t OS_TEST_PROCFS_EXPECTED_FAILURE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_PROCFS_MINIMUM_READ_COUNT = 1ULL;
constexpr uint8_t OS_TEST_PROCFS_MOUNT_PATH[] = {'/', 'p', 'r', 'o', 'c'};
constexpr uint8_t OS_TEST_PROCFS_MEMORY_PATH[] = {
    '/', 'p', 'r', 'o', 'c', '/', 'm', 'e', 'm', 'i', 'n', 'f', 'o',
};
constexpr char OS_TEST_PROCFS_MANAGED_LINE[] = "managed_bytes 67108864\n";
constexpr char OS_TEST_PROCFS_FREE_LINE[] = "free_bytes 50331648\n";
constexpr char OS_TEST_PROCFS_ALLOCATED_LINE[] = "allocated_bytes 16777216\n";
constexpr char OS_TEST_PROCFS_SWAP_TOTAL_LINE[] = "swap_total_bytes 67108864\n";
constexpr char OS_TEST_PROCFS_COMMIT_LIMIT_LINE[] = "commit_limit_bytes 83886080\n";

struct SnapshotSource final {
    os::kernel::fs::ProcfsSnapshot snapshot;
    uint64_t call_count;
    bool fail;
};

[[nodiscard]] bool CaptureSnapshot(void *const context,
                                   os::kernel::fs::ProcfsSnapshot &snapshot) noexcept {
    if (context == nullptr) {
        return false;
    }
    SnapshotSource &source = *static_cast<SnapshotSource *>(context);
    ++source.call_count;
    if (source.fail) {
        snapshot = os::kernel::fs::ProcfsSnapshot{};
        return false;
    }
    snapshot = source.snapshot;
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCFS_SUITE_NAME};
    alignas(OS_TEST_PROCFS_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_bytes[OS_TEST_PROCFS_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs root_file_system{};
    os::kernel::fs::Procfs procfs{};
    os::kernel::fs::Mount mounts[OS_TEST_PROCFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};
    SnapshotSource source{
        .snapshot =
            {
                .monotonic_nanoseconds = OS_TEST_PROCFS_MONOTONIC_NANOSECONDS,
                .managed_memory_bytes = OS_TEST_PROCFS_EXPECTED_MANAGED_BYTES,
                .free_memory_bytes = OS_TEST_PROCFS_EXPECTED_FREE_BYTES,
                .allocated_memory_bytes = OS_TEST_PROCFS_EXPECTED_ALLOCATED_BYTES,
                .resident_limit_bytes = OS_TEST_PROCFS_EXPECTED_FREE_BYTES,
                .swap_total_bytes = OS_TEST_PROCFS_EXPECTED_MANAGED_BYTES,
                .swap_free_bytes = OS_TEST_PROCFS_EXPECTED_FREE_BYTES,
                .committed_memory_bytes = OS_TEST_PROCFS_EXPECTED_ALLOCATED_BYTES,
                .commit_limit_bytes = 80ULL * 1024ULL * 1024ULL,
                .oom_kill_count = OS_TEST_PROCFS_EXPECTED_FAILURE_COUNT,
                .active_process_count = OS_TEST_PROCFS_ACTIVE_PROCESS_COUNT,
                .active_thread_count = OS_TEST_PROCFS_ACTIVE_THREAD_COUNT,
                .process_capacity = OS_TEST_PROCFS_PROCESS_CAPACITY,
                .thread_capacity = OS_TEST_PROCFS_THREAD_CAPACITY,
                .current_process_id = OS_TEST_PROCFS_CURRENT_PROCESS_IDENTIFIER,
                .heap_consumed_bytes = OS_TEST_PROCFS_HEAP_CONSUMED_BYTES,
                .active_file_description_count = OS_TEST_PROCFS_ACTIVE_FILE_DESCRIPTION_COUNT,
                .active_pipe_count = OS_TEST_PROCFS_ACTIVE_PIPE_COUNT,
                .mount_count = OS_TEST_PROCFS_SNAPSHOT_MOUNT_COUNT,
                .vnode_count = OS_TEST_PROCFS_SNAPSHOT_VNODE_COUNT,
                .journal_commit_count = OS_TEST_PROCFS_JOURNAL_COMMIT_COUNT,
            },
        .call_count = OS_TEST_PROCFS_EMPTY_VALUE,
        .fail = false,
    };

    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_bytes), sizeof(heap_bytes)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        root_file_system.Initialize(
            heap, OS_TEST_PROCFS_ROOT_SUPERBLOCK_IDENTIFIER, OS_TEST_PROCFS_ROOT_NODE_LIMIT,
            OS_TEST_PROCFS_MAXIMUM_FILE_SIZE_BYTES) == os::kernel::fs::Status::Succeeded &&
        procfs.Initialize(OS_TEST_PROCFS_SUPERBLOCK_IDENTIFIER, CaptureSnapshot, &source) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_PROCFS_MOUNT_CAPACITY, root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_PROCFS_MOUNT_PATH,
                            sizeof(OS_TEST_PROCFS_MOUNT_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.MountAt(context, OS_TEST_PROCFS_MOUNT_PATH, sizeof(OS_TEST_PROCFS_MOUNT_PATH),
                    procfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded;

    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile memory_file{};
    uint8_t memory_bytes[os::kernel::fs::OS_KERNEL_PROCFS_MAXIMUM_SNAPSHOT_SIZE_BYTES]{};
    uint64_t memory_read_bytes = OS_TEST_PROCFS_EMPTY_VALUE;
    os::kernel::fs::NodeInformation memory_information{};
    const bool snapshot_contract_valid =
        initialized &&
        vfs.Stat(context, OS_TEST_PROCFS_MEMORY_PATH, sizeof(OS_TEST_PROCFS_MEMORY_PATH),
                 memory_information) == os::kernel::fs::Status::Succeeded &&
        memory_information.owner_user_identifier == os::abi::OS_ABI_ROOT_USER_IDENTIFIER &&
        memory_information.owner_group_identifier == os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER &&
        memory_information.mode == (os::abi::OS_ABI_FILE_MODE_REGULAR | 0000444U) &&
        vfs.Open(context, OS_TEST_PROCFS_MEMORY_PATH, sizeof(OS_TEST_PROCFS_MEMORY_PATH),
                 read_options, memory_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Read(memory_file, memory_bytes, sizeof(memory_bytes), memory_read_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        std::string_view{reinterpret_cast<const char *>(memory_bytes), memory_read_bytes}.find(
            OS_TEST_PROCFS_MANAGED_LINE) != std::string_view::npos &&
        std::string_view{reinterpret_cast<const char *>(memory_bytes), memory_read_bytes}.find(
            OS_TEST_PROCFS_FREE_LINE) != std::string_view::npos &&
        std::string_view{reinterpret_cast<const char *>(memory_bytes), memory_read_bytes}.find(
            OS_TEST_PROCFS_ALLOCATED_LINE) != std::string_view::npos &&
        std::string_view{reinterpret_cast<const char *>(memory_bytes), memory_read_bytes}.find(
            OS_TEST_PROCFS_SWAP_TOTAL_LINE) != std::string_view::npos &&
        std::string_view{reinterpret_cast<const char *>(memory_bytes), memory_read_bytes}.find(
            OS_TEST_PROCFS_COMMIT_LIMIT_LINE) != std::string_view::npos;
    test_context.Expect(snapshot_contract_valid, OS_TEST_PROCFS_SNAPSHOT_CONTRACT);

    os::kernel::fs::OpenFile directory{};
    uint64_t directory_entry_count = OS_TEST_PROCFS_EMPTY_VALUE;
    bool directory_valid =
        vfs.OpenDirectory(context, OS_TEST_PROCFS_MOUNT_PATH, sizeof(OS_TEST_PROCFS_MOUNT_PATH),
                          directory) == os::kernel::fs::Status::Succeeded;
    while (directory_valid) {
        os::kernel::fs::DirectoryEntry entry{};
        bool end_of_directory = false;
        if (vfs.ReadDirectory(directory, entry, end_of_directory) !=
            os::kernel::fs::Status::Succeeded) {
            directory_valid = false;
        } else if (end_of_directory) {
            break;
        } else if (entry.type != os::kernel::fs::NodeType::RegularFile) {
            directory_valid = false;
        } else {
            ++directory_entry_count;
        }
    }
    directory_valid = directory_valid &&
                      directory_entry_count == os::kernel::fs::OS_KERNEL_PROCFS_FILE_COUNT &&
                      vfs.Close(directory) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(directory_valid, OS_TEST_PROCFS_DIRECTORY_CONTRACT);

    source.fail = true;
    uint64_t failure_read_bytes = OS_TEST_PROCFS_EMPTY_VALUE;
    const bool failure_contract_valid =
        vfs.ReadAt(memory_file, OS_TEST_PROCFS_EMPTY_VALUE, memory_bytes, sizeof(memory_bytes),
                   failure_read_bytes) == os::kernel::fs::Status::DeviceFailure &&
        failure_read_bytes == OS_TEST_PROCFS_EMPTY_VALUE &&
        procfs.ReadStatistics().snapshot_failure_count == OS_TEST_PROCFS_EXPECTED_FAILURE_COUNT;
    source.fail = false;
    test_context.Expect(failure_contract_valid, OS_TEST_PROCFS_FAILURE_CONTRACT);

    const bool lifetime_contract_valid =
        vfs.Close(memory_file) == os::kernel::fs::Status::Succeeded &&
        procfs.ReadStatistics().active_open_count == OS_TEST_PROCFS_EMPTY_VALUE &&
        procfs.ReadStatistics().snapshot_read_count >= OS_TEST_PROCFS_MINIMUM_READ_COUNT &&
        procfs.Validate() == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        root_file_system.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_PROCFS_EMPTY_VALUE;
    test_context.Expect(lifetime_contract_valid, OS_TEST_PROCFS_LIFETIME_CONTRACT);
    return test_context.ExitCode();
}
