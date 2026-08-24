#include <os/kernel/fs/memfs.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <test_context.hpp>

#include <atomic>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_VFS_AT_PATH_SUITE_NAME = "kernel/vfs_at_path";
constexpr std::string_view OS_TEST_VFS_AT_PATH_RESOLUTION_MESSAGE =
    "目录句柄相对解析必须支持 create/stat 和绝对路径忽略 dirfd";
constexpr std::string_view OS_TEST_VFS_AT_PATH_MUTATION_MESSAGE =
    "renameat 必须跨双 parent 提交并保持旧目录句柄身份";
constexpr std::string_view OS_TEST_VFS_AT_PATH_RENAME_MESSAGE =
    "renameat 与目录自身 rename 必须成功提交";
constexpr std::string_view OS_TEST_VFS_AT_PATH_HANDLE_MESSAGE =
    "目录改名后旧句柄必须继续支持相对 create";
constexpr std::string_view OS_TEST_VFS_AT_PATH_RENAMED_HANDLE_MESSAGE =
    "目录改名后旧句柄必须继续支持相对 create";
constexpr std::string_view OS_TEST_VFS_AT_PATH_SYMBOLIC_MESSAGE =
    "renameat 后源目录和目标目录的相对 lookup 必须一致";
constexpr std::string_view OS_TEST_VFS_AT_PATH_CONCURRENCY_MESSAGE =
    "同一目录句柄上的并发 create 必须由 namespace writer 串行提交";
constexpr std::string_view OS_TEST_VFS_AT_PATH_CONTENDED_CREATE_MESSAGE =
    "并发 create 同一路径时二次解析必须复用胜出者结果且不得锁重入";
constexpr std::string_view OS_TEST_VFS_AT_PATH_LIFECYCLE_MESSAGE =
    "目录句柄 retain/release、打开引用和最终 namespace 必须完整守恒";
constexpr uint64_t OS_TEST_VFS_AT_PATH_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_AT_PATH_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_AT_PATH_NODE_LIMIT = 64ULL;
constexpr uint64_t OS_TEST_VFS_AT_PATH_MAXIMUM_FILE_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_AT_PATH_MOUNT_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_AT_PATH_SUPERBLOCK_IDENTIFIER = 71ULL;
constexpr uint8_t OS_TEST_VFS_AT_PATH_LEFT[] = {'/', 'l', 'e', 'f', 't'};
constexpr uint8_t OS_TEST_VFS_AT_PATH_RIGHT[] = {'/', 'r', 'i', 'g', 'h', 't'};
constexpr uint8_t OS_TEST_VFS_AT_PATH_RENAMED[] = {'/', 'r', 'e', 'n', 'a', 'm', 'e', 'd'};
constexpr uint8_t OS_TEST_VFS_AT_PATH_FILE[] = {'f', 'i', 'l', 'e'};
constexpr uint8_t OS_TEST_VFS_AT_PATH_MOVED[] = {'m', 'o', 'v', 'e', 'd'};
constexpr uint8_t OS_TEST_VFS_AT_PATH_AFTER[] = {'a', 'f', 't', 'e', 'r'};
constexpr uint8_t OS_TEST_VFS_AT_PATH_CONTENDED[] = {
    'c', 'o', 'n', 't', 'e', 'n', 'd', 'e', 'd',
};
constexpr uint64_t OS_TEST_VFS_AT_PATH_WORKER_COUNT = 4ULL;
constexpr uint64_t OS_TEST_VFS_AT_PATH_WORKER_NAME_LENGTH_BYTES = 8ULL;
constexpr uint8_t OS_TEST_VFS_AT_PATH_WORKER_NAMES[OS_TEST_VFS_AT_PATH_WORKER_COUNT]
                                                  [OS_TEST_VFS_AT_PATH_WORKER_NAME_LENGTH_BYTES] = {
                                                      {'w', 'o', 'r', 'k', 'e', 'r', '_', 'a'},
                                                      {'w', 'o', 'r', 'k', 'e', 'r', '_', 'b'},
                                                      {'w', 'o', 'r', 'k', 'e', 'r', '_', 'c'},
                                                      {'w', 'o', 'r', 'k', 'e', 'r', '_', 'd'},
};

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_AT_PATH_SUITE_NAME};
    alignas(OS_TEST_VFS_AT_PATH_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_bytes[OS_TEST_VFS_AT_PATH_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs memfs{};
    os::kernel::fs::Mount mounts[OS_TEST_VFS_AT_PATH_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};
    bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_bytes), sizeof(heap_bytes)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        memfs.Initialize(
            heap, OS_TEST_VFS_AT_PATH_SUPERBLOCK_IDENTIFIER, OS_TEST_VFS_AT_PATH_NODE_LIMIT,
            OS_TEST_VFS_AT_PATH_MAXIMUM_FILE_SIZE_BYTES) == os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_VFS_AT_PATH_MOUNT_CAPACITY, memfs.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_VFS_AT_PATH_LEFT, sizeof(OS_TEST_VFS_AT_PATH_LEFT)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_VFS_AT_PATH_RIGHT,
                            sizeof(OS_TEST_VFS_AT_PATH_RIGHT)) == os::kernel::fs::Status::Succeeded;
    os::kernel::fs::OpenFile left_file{};
    os::kernel::fs::OpenFile right_file{};
    os::kernel::fs::DirectoryHandle left_handle{};
    os::kernel::fs::DirectoryHandle right_handle{};
    initialized =
        initialized &&
        vfs.OpenDirectory(context, OS_TEST_VFS_AT_PATH_LEFT, sizeof(OS_TEST_VFS_AT_PATH_LEFT),
                          left_file) == os::kernel::fs::Status::Succeeded &&
        vfs.OpenDirectory(context, OS_TEST_VFS_AT_PATH_RIGHT, sizeof(OS_TEST_VFS_AT_PATH_RIGHT),
                          right_file) == os::kernel::fs::Status::Succeeded &&
        vfs.RetainDirectoryHandle(left_file, left_handle) == os::kernel::fs::Status::Succeeded &&
        vfs.RetainDirectoryHandle(right_file, right_handle) == os::kernel::fs::Status::Succeeded;

    const os::kernel::fs::OpenOptions create_options{
        .readable = true,
        .writable = true,
        .create = true,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile created_file{};
    os::kernel::fs::NodeInformation information{};
    os::kernel::fs::DirectoryHandle invalid_handle{};
    bool resolution_consistent = initialized &&
                                 vfs.OpenAt(context, &left_handle, OS_TEST_VFS_AT_PATH_FILE,
                                            sizeof(OS_TEST_VFS_AT_PATH_FILE), create_options,
                                            created_file) == os::kernel::fs::Status::Succeeded &&
                                 vfs.Close(created_file) == os::kernel::fs::Status::Succeeded &&
                                 vfs.StatAt(context, &left_handle, OS_TEST_VFS_AT_PATH_FILE,
                                            sizeof(OS_TEST_VFS_AT_PATH_FILE), true,
                                            information) == os::kernel::fs::Status::Succeeded &&
                                 information.type == os::kernel::fs::NodeType::RegularFile &&
                                 vfs.StatAt(context, &invalid_handle, OS_TEST_VFS_AT_PATH_FILE,
                                            sizeof(OS_TEST_VFS_AT_PATH_FILE), true,
                                            information) == os::kernel::fs::Status::InvalidHandle &&
                                 vfs.StatAt(context, &invalid_handle, OS_TEST_VFS_AT_PATH_RIGHT,
                                            sizeof(OS_TEST_VFS_AT_PATH_RIGHT), true,
                                            information) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(resolution_consistent, OS_TEST_VFS_AT_PATH_RESOLUTION_MESSAGE);

    const bool rename_consistent =
        resolution_consistent &&
        vfs.RenameAt(context, &left_handle, OS_TEST_VFS_AT_PATH_FILE,
                     sizeof(OS_TEST_VFS_AT_PATH_FILE), &right_handle, OS_TEST_VFS_AT_PATH_MOVED,
                     sizeof(OS_TEST_VFS_AT_PATH_MOVED),
                     false) == os::kernel::fs::Status::Succeeded &&
        vfs.Rename(context, OS_TEST_VFS_AT_PATH_LEFT, sizeof(OS_TEST_VFS_AT_PATH_LEFT),
                   OS_TEST_VFS_AT_PATH_RENAMED, sizeof(OS_TEST_VFS_AT_PATH_RENAMED),
                   false) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(rename_consistent, OS_TEST_VFS_AT_PATH_RENAME_MESSAGE);
    const bool renamed_handle_consistent =
        rename_consistent &&
        vfs.OpenAt(context, &left_handle, OS_TEST_VFS_AT_PATH_AFTER,
                   sizeof(OS_TEST_VFS_AT_PATH_AFTER), create_options,
                   created_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Close(created_file) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(renamed_handle_consistent, OS_TEST_VFS_AT_PATH_RENAMED_HANDLE_MESSAGE);
    const bool handle_consistent = renamed_handle_consistent;
    test_context.Expect(handle_consistent, OS_TEST_VFS_AT_PATH_HANDLE_MESSAGE);
    const bool mutation_consistent =
        handle_consistent &&
        vfs.RemoveFileAt(context, &right_handle, OS_TEST_VFS_AT_PATH_MOVED,
                         sizeof(OS_TEST_VFS_AT_PATH_MOVED)) == os::kernel::fs::Status::Succeeded &&
        vfs.StatAt(context, &right_handle, OS_TEST_VFS_AT_PATH_MOVED,
                   sizeof(OS_TEST_VFS_AT_PATH_MOVED), true,
                   information) == os::kernel::fs::Status::NotFound;
    test_context.Expect(mutation_consistent, OS_TEST_VFS_AT_PATH_SYMBOLIC_MESSAGE);
    test_context.Expect(mutation_consistent, OS_TEST_VFS_AT_PATH_MUTATION_MESSAGE);

    bool contended_results[OS_TEST_VFS_AT_PATH_WORKER_COUNT]{};
    std::thread contended_workers[OS_TEST_VFS_AT_PATH_WORKER_COUNT];
    std::atomic<uint64_t> ready_worker_count{0ULL};
    std::atomic<bool> start_contended_workers{false};
    for (uint64_t worker_index = 0ULL; worker_index < OS_TEST_VFS_AT_PATH_WORKER_COUNT;
         ++worker_index) {
        contended_workers[worker_index] =
            std::thread([&vfs, &context, &left_handle, &create_options, &contended_results,
                         &ready_worker_count, &start_contended_workers, worker_index]() {
                ready_worker_count.fetch_add(1ULL, std::memory_order_release);
                while (!start_contended_workers.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                os::kernel::fs::OpenFile worker_file{};
                contended_results[worker_index] =
                    vfs.OpenAt(context, &left_handle, OS_TEST_VFS_AT_PATH_CONTENDED,
                               sizeof(OS_TEST_VFS_AT_PATH_CONTENDED), create_options,
                               worker_file) == os::kernel::fs::Status::Succeeded &&
                    vfs.Close(worker_file) == os::kernel::fs::Status::Succeeded;
            });
    }
    while (ready_worker_count.load(std::memory_order_acquire) != OS_TEST_VFS_AT_PATH_WORKER_COUNT) {
        std::this_thread::yield();
    }
    start_contended_workers.store(true, std::memory_order_release);
    bool contended_create_consistent = mutation_consistent;
    for (uint64_t worker_index = 0ULL; worker_index < OS_TEST_VFS_AT_PATH_WORKER_COUNT;
         ++worker_index) {
        contended_workers[worker_index].join();
        contended_create_consistent =
            contended_create_consistent && contended_results[worker_index];
    }
    contended_create_consistent =
        contended_create_consistent &&
        vfs.RemoveFileAt(context, &left_handle, OS_TEST_VFS_AT_PATH_CONTENDED,
                         sizeof(OS_TEST_VFS_AT_PATH_CONTENDED)) ==
            os::kernel::fs::Status::Succeeded;
    test_context.Expect(contended_create_consistent, OS_TEST_VFS_AT_PATH_CONTENDED_CREATE_MESSAGE);

    bool worker_results[OS_TEST_VFS_AT_PATH_WORKER_COUNT]{};
    std::thread workers[OS_TEST_VFS_AT_PATH_WORKER_COUNT];
    for (uint64_t worker_index = 0ULL; worker_index < OS_TEST_VFS_AT_PATH_WORKER_COUNT;
         ++worker_index) {
        workers[worker_index] = std::thread([&vfs, &context, &left_handle, &create_options,
                                             &worker_results, worker_index]() {
            os::kernel::fs::OpenFile worker_file{};
            worker_results[worker_index] =
                vfs.OpenAt(context, &left_handle, OS_TEST_VFS_AT_PATH_WORKER_NAMES[worker_index],
                           OS_TEST_VFS_AT_PATH_WORKER_NAME_LENGTH_BYTES, create_options,
                           worker_file) == os::kernel::fs::Status::Succeeded &&
                vfs.Close(worker_file) == os::kernel::fs::Status::Succeeded;
        });
    }
    bool concurrency_consistent = contended_create_consistent;
    for (uint64_t worker_index = 0ULL; worker_index < OS_TEST_VFS_AT_PATH_WORKER_COUNT;
         ++worker_index) {
        workers[worker_index].join();
        concurrency_consistent = concurrency_consistent && worker_results[worker_index];
    }
    test_context.Expect(concurrency_consistent, OS_TEST_VFS_AT_PATH_CONCURRENCY_MESSAGE);

    bool worker_cleanup_succeeded = concurrency_consistent;
    for (uint64_t worker_index = 0ULL; worker_index < OS_TEST_VFS_AT_PATH_WORKER_COUNT;
         ++worker_index) {
        worker_cleanup_succeeded =
            vfs.RemoveFileAt(context, &left_handle, OS_TEST_VFS_AT_PATH_WORKER_NAMES[worker_index],
                             OS_TEST_VFS_AT_PATH_WORKER_NAME_LENGTH_BYTES) ==
                os::kernel::fs::Status::Succeeded &&
            worker_cleanup_succeeded;
    }
    const bool released =
        worker_cleanup_succeeded &&
        vfs.RemoveFileAt(context, &left_handle, OS_TEST_VFS_AT_PATH_AFTER,
                         sizeof(OS_TEST_VFS_AT_PATH_AFTER)) == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseDirectoryHandle(left_handle) == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseDirectoryHandle(right_handle) == os::kernel::fs::Status::Succeeded &&
        vfs.Close(left_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Close(right_file) == os::kernel::fs::Status::Succeeded &&
        vfs.RemoveDirectory(context, OS_TEST_VFS_AT_PATH_RENAMED,
                            sizeof(OS_TEST_VFS_AT_PATH_RENAMED)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.RemoveDirectory(context, OS_TEST_VFS_AT_PATH_RIGHT,
                            sizeof(OS_TEST_VFS_AT_PATH_RIGHT)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ReadStatistics().directory_handle_retain_count == 2ULL &&
        vfs.ReadStatistics().directory_handle_release_count == 2ULL &&
        vfs.ReadStatistics().active_directory_handle_count == 0ULL &&
        vfs.ReadStatistics().peak_directory_handle_count == 2ULL &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    test_context.Expect(released, OS_TEST_VFS_AT_PATH_LIFECYCLE_MESSAGE);
    return released ? test_context.ExitCode() : 1;
}
