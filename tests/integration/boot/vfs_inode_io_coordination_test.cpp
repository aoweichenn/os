#include <os/kernel/fs/memfs.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <test_context.hpp>

#include <atomic>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_VFS_INODE_IO_SUITE_NAME = "kernel/vfs_inode_io/integration";
constexpr std::string_view OS_TEST_VFS_INODE_IO_PARALLEL_MESSAGE =
    "不同 inode 的 buffered write 必须越过旧全局 append 串行点并同时进入缓存层";
constexpr std::string_view OS_TEST_VFS_INODE_IO_TRUNCATE_MESSAGE =
    "同 inode write 必须等待 truncate 准备、后端提交和缓存收缩全部结束";
constexpr std::string_view OS_TEST_VFS_INODE_IO_TRUNCATE_FAILURE_MESSAGE =
    "truncate 准备失败不得提交后端长度或调用缓存收缩";
constexpr std::string_view OS_TEST_VFS_INODE_IO_RESOURCE_MESSAGE =
    "协调槽、打开引用、memfs 节点和 heap 生命周期必须最终守恒";
constexpr uint64_t OS_TEST_VFS_INODE_IO_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_INODE_IO_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_INODE_IO_NODE_LIMIT = 32ULL;
constexpr uint64_t OS_TEST_VFS_INODE_IO_MAXIMUM_FILE_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_INODE_IO_SUPERBLOCK_IDENTIFIER = 41ULL;
constexpr uint64_t OS_TEST_VFS_INODE_IO_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_VFS_INODE_IO_SPIN_LIMIT = 10000000ULL;
constexpr uint8_t OS_TEST_VFS_INODE_IO_FIRST_PATH[] = {'/', 'f', 'i', 'r', 's', 't'};
constexpr uint8_t OS_TEST_VFS_INODE_IO_SECOND_PATH[] = {'/', 's', 'e', 'c', 'o', 'n', 'd'};
constexpr uint8_t OS_TEST_VFS_INODE_IO_FIRST_BYTE = static_cast<uint8_t>('A');
constexpr uint8_t OS_TEST_VFS_INODE_IO_SECOND_BYTE = static_cast<uint8_t>('B');
constexpr uint8_t OS_TEST_VFS_INODE_IO_AFTER_TRUNCATE_BYTE = static_cast<uint8_t>('T');

enum class CacheTestMode : uint64_t {
    Normal,
    ParallelFiles,
    TruncateSameFile,
};

struct CacheTestContext final {
    os::kernel::fs::Vfs *vfs;
    std::atomic<uint64_t> mode;
    std::atomic<uint64_t> first_node_identifier;
    std::atomic<uint64_t> second_node_identifier;
    std::atomic<uint64_t> parallel_entry_count;
    std::atomic<bool> release_parallel_writes;
    std::atomic<uint64_t> same_file_write_attempt_count;
    std::atomic<uint64_t> same_file_write_entry_count;
    std::atomic<uint64_t> truncate_prepare_entry_count;
    std::atomic<uint64_t> truncate_cache_entry_count;
    std::atomic<bool> release_truncate_prepare;
    std::atomic<bool> fail_truncate_prepare;
};

[[nodiscard]] bool WaitUntilAtLeast(const std::atomic<uint64_t> &value,
                                    const uint64_t expected) noexcept {
    for (uint64_t spin_count = 0ULL; spin_count < OS_TEST_VFS_INODE_IO_SPIN_LIMIT; ++spin_count) {
        if (value.load(std::memory_order_acquire) >= expected) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

[[nodiscard]] bool WaitUntilActiveInodeIoReferences(const os::kernel::fs::Vfs &vfs,
                                                    const uint64_t expected) noexcept {
    for (uint64_t spin_count = 0ULL; spin_count < OS_TEST_VFS_INODE_IO_SPIN_LIMIT; ++spin_count) {
        if (vfs.ReadInodeIoStatistics().active_reference_count >= expected) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

[[nodiscard]] os::kernel::fs::Status
ReadThroughCache(void *const context, const os::kernel::fs::OpenFile &open_file,
                 const uint64_t offset_bytes, uint8_t *const destination,
                 const uint64_t capacity_bytes, uint64_t &read_bytes,
                 os::kernel::fs::RegularFileReadCacheObservation &observation) noexcept {
    observation = os::kernel::fs::RegularFileReadCacheObservation{};
    if (context == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    CacheTestContext &cache_context = *static_cast<CacheTestContext *>(context);
    return cache_context.vfs->ReadUncachedAt(open_file, offset_bytes, destination, capacity_bytes,
                                             read_bytes);
}

[[nodiscard]] os::kernel::fs::Status
WriteThroughCache(void *const context, const os::kernel::fs::OpenFile &open_file,
                  const uint64_t offset_bytes, const uint8_t *const source,
                  const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    if (context == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    CacheTestContext &cache_context = *static_cast<CacheTestContext *>(context);
    const CacheTestMode mode =
        static_cast<CacheTestMode>(cache_context.mode.load(std::memory_order_acquire));
    if (mode == CacheTestMode::ParallelFiles &&
        (open_file.path.vnode.identifier ==
             cache_context.first_node_identifier.load(std::memory_order_acquire) ||
         open_file.path.vnode.identifier ==
             cache_context.second_node_identifier.load(std::memory_order_acquire))) {
        cache_context.parallel_entry_count.fetch_add(1ULL, std::memory_order_release);
        while (!cache_context.release_parallel_writes.load(std::memory_order_acquire)) {
        }
    } else if (mode == CacheTestMode::TruncateSameFile &&
               open_file.path.vnode.identifier ==
                   cache_context.first_node_identifier.load(std::memory_order_acquire)) {
        cache_context.same_file_write_entry_count.fetch_add(1ULL, std::memory_order_release);
    }
    return cache_context.vfs->WriteUncachedAt(open_file, offset_bytes, source, length_bytes,
                                              written_bytes);
}

[[nodiscard]] os::kernel::fs::Status ResolveCachedSize(void *const context,
                                                       const os::kernel::fs::Vnode &vnode,
                                                       const uint64_t backend_size_bytes,
                                                       uint64_t &size_bytes) noexcept {
    if (context == nullptr || vnode.type != os::kernel::fs::NodeType::RegularFile) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    size_bytes = backend_size_bytes;
    return os::kernel::fs::Status::Succeeded;
}

[[nodiscard]] os::kernel::fs::Status TruncateCache(void *const context,
                                                   const os::kernel::fs::Vnode &vnode,
                                                   const uint64_t size_bytes) noexcept {
    static_cast<void>(size_bytes);
    if (context == nullptr || vnode.type != os::kernel::fs::NodeType::RegularFile) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    CacheTestContext &cache_context = *static_cast<CacheTestContext *>(context);
    cache_context.truncate_cache_entry_count.fetch_add(1ULL, std::memory_order_release);
    return os::kernel::fs::Status::Succeeded;
}

[[nodiscard]] os::kernel::fs::Status PrepareTruncate(void *const context,
                                                     const os::kernel::fs::Vnode &vnode,
                                                     const uint64_t size_bytes) noexcept {
    static_cast<void>(size_bytes);
    if (context == nullptr || vnode.type != os::kernel::fs::NodeType::RegularFile) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    CacheTestContext &cache_context = *static_cast<CacheTestContext *>(context);
    cache_context.truncate_prepare_entry_count.fetch_add(1ULL, std::memory_order_release);
    if (cache_context.fail_truncate_prepare.load(std::memory_order_acquire)) {
        return os::kernel::fs::Status::DeviceFailure;
    }
    while (!cache_context.release_truncate_prepare.load(std::memory_order_acquire)) {
    }
    return os::kernel::fs::Status::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_INODE_IO_SUITE_NAME};
    alignas(OS_TEST_VFS_INODE_IO_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_storage[OS_TEST_VFS_INODE_IO_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs memfs{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::Mount mounts[OS_TEST_VFS_INODE_IO_MOUNT_CAPACITY]{};
    os::kernel::fs::FsContext file_system_context{};
    CacheTestContext cache_context{
        .vfs = &vfs,
        .mode = static_cast<uint64_t>(CacheTestMode::Normal),
        .first_node_identifier = 0ULL,
        .second_node_identifier = 0ULL,
        .parallel_entry_count = 0ULL,
        .release_parallel_writes = false,
        .same_file_write_attempt_count = 0ULL,
        .same_file_write_entry_count = 0ULL,
        .truncate_prepare_entry_count = 0ULL,
        .truncate_cache_entry_count = 0ULL,
        .release_truncate_prepare = false,
        .fail_truncate_prepare = false,
    };
    const bool initialized = heap.Initialize(reinterpret_cast<uint64_t>(heap_storage),
                                             OS_TEST_VFS_INODE_IO_HEAP_SIZE_BYTES) ==
                                 os::kernel::KernelHeapStatus::Succeeded &&
                             memfs.Initialize(heap, OS_TEST_VFS_INODE_IO_SUPERBLOCK_IDENTIFIER,
                                              OS_TEST_VFS_INODE_IO_NODE_LIMIT,
                                              OS_TEST_VFS_INODE_IO_MAXIMUM_FILE_SIZE_BYTES) ==
                                 os::kernel::fs::Status::Succeeded;
    memfs.GetSuperblock().cache_regular_file_data = true;
    const bool vfs_ready =
        initialized &&
        vfs.Initialize(mounts, OS_TEST_VFS_INODE_IO_MOUNT_CAPACITY, memfs.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureRegularFileDataCache(&cache_context, ReadThroughCache, WriteThroughCache,
                                          ResolveCachedSize,
                                          TruncateCache) == os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureRegularFileTruncatePreparation(&cache_context, PrepareTruncate) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(file_system_context) == os::kernel::fs::Status::Succeeded;
    const os::kernel::fs::OpenOptions create_options{
        .readable = true,
        .writable = true,
        .create = true,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile first_file{};
    os::kernel::fs::OpenFile second_file{};
    const bool files_open = vfs_ready &&
                            vfs.Open(file_system_context, OS_TEST_VFS_INODE_IO_FIRST_PATH,
                                     sizeof(OS_TEST_VFS_INODE_IO_FIRST_PATH), create_options,
                                     first_file) == os::kernel::fs::Status::Succeeded &&
                            vfs.Open(file_system_context, OS_TEST_VFS_INODE_IO_SECOND_PATH,
                                     sizeof(OS_TEST_VFS_INODE_IO_SECOND_PATH), create_options,
                                     second_file) == os::kernel::fs::Status::Succeeded;
    cache_context.first_node_identifier.store(first_file.path.vnode.identifier,
                                              std::memory_order_release);
    cache_context.second_node_identifier.store(second_file.path.vnode.identifier,
                                               std::memory_order_release);

    cache_context.mode.store(static_cast<uint64_t>(CacheTestMode::ParallelFiles),
                             std::memory_order_release);
    std::atomic<uint64_t> parallel_failure_count{0ULL};
    const auto parallel_writer = [&](const os::kernel::fs::OpenFile *const open_file,
                                     const uint8_t value) noexcept {
        uint64_t written_bytes = 0ULL;
        if (open_file == nullptr ||
            vfs.WriteAt(*open_file, 0ULL, &value, sizeof(value), written_bytes) !=
                os::kernel::fs::Status::Succeeded ||
            written_bytes != sizeof(value)) {
            parallel_failure_count.fetch_add(1ULL, std::memory_order_relaxed);
        }
    };
    std::thread first_write_thread{parallel_writer, &first_file, OS_TEST_VFS_INODE_IO_FIRST_BYTE};
    std::thread second_write_thread{parallel_writer, &second_file,
                                    OS_TEST_VFS_INODE_IO_SECOND_BYTE};
    const bool parallel_entry_reached = WaitUntilAtLeast(cache_context.parallel_entry_count, 2ULL);
    cache_context.release_parallel_writes.store(true, std::memory_order_release);
    first_write_thread.join();
    second_write_thread.join();
    const bool parallel_writes_valid =
        files_open && parallel_entry_reached &&
        parallel_failure_count.load(std::memory_order_relaxed) == 0ULL;
    test_context.Expect(parallel_writes_valid, OS_TEST_VFS_INODE_IO_PARALLEL_MESSAGE);

    cache_context.mode.store(static_cast<uint64_t>(CacheTestMode::TruncateSameFile),
                             std::memory_order_release);
    std::atomic<uint64_t> truncate_failure_count{0ULL};
    std::thread truncate_thread{[&]() noexcept {
        if (vfs.TruncateOpenFile(first_file, 0ULL) != os::kernel::fs::Status::Succeeded) {
            truncate_failure_count.fetch_add(1ULL, std::memory_order_relaxed);
        }
    }};
    const bool truncate_prepare_entered =
        WaitUntilAtLeast(cache_context.truncate_prepare_entry_count, 1ULL);
    std::thread same_file_write_thread{[&]() noexcept {
        cache_context.same_file_write_attempt_count.fetch_add(1ULL, std::memory_order_release);
        uint64_t written_bytes = 0ULL;
        if (vfs.WriteAt(first_file, 0ULL, &OS_TEST_VFS_INODE_IO_AFTER_TRUNCATE_BYTE,
                        sizeof(OS_TEST_VFS_INODE_IO_AFTER_TRUNCATE_BYTE),
                        written_bytes) != os::kernel::fs::Status::Succeeded ||
            written_bytes != sizeof(OS_TEST_VFS_INODE_IO_AFTER_TRUNCATE_BYTE)) {
            truncate_failure_count.fetch_add(1ULL, std::memory_order_relaxed);
        }
    }};
    const bool same_file_write_attempted =
        WaitUntilAtLeast(cache_context.same_file_write_attempt_count, 1ULL);
    const bool same_file_write_waiting = WaitUntilActiveInodeIoReferences(vfs, 2ULL);
    const bool same_file_write_blocked =
        cache_context.same_file_write_entry_count.load(std::memory_order_acquire) == 0ULL;
    cache_context.release_truncate_prepare.store(true, std::memory_order_release);
    truncate_thread.join();
    same_file_write_thread.join();
    os::kernel::fs::NodeInformation first_information{};
    const bool truncate_serialized =
        truncate_prepare_entered && same_file_write_attempted && same_file_write_waiting &&
        same_file_write_blocked && truncate_failure_count.load(std::memory_order_relaxed) == 0ULL &&
        cache_context.same_file_write_entry_count.load(std::memory_order_acquire) == 1ULL &&
        cache_context.truncate_cache_entry_count.load(std::memory_order_acquire) == 1ULL &&
        vfs.StatOpenFile(first_file, first_information) == os::kernel::fs::Status::Succeeded &&
        first_information.size_bytes == sizeof(OS_TEST_VFS_INODE_IO_AFTER_TRUNCATE_BYTE);
    test_context.Expect(truncate_serialized, OS_TEST_VFS_INODE_IO_TRUNCATE_MESSAGE);

    const uint64_t truncate_cache_count_before_failure =
        cache_context.truncate_cache_entry_count.load(std::memory_order_acquire);
    cache_context.fail_truncate_prepare.store(true, std::memory_order_release);
    const os::kernel::fs::Status failed_truncate_status = vfs.TruncateOpenFile(first_file, 0ULL);
    os::kernel::fs::NodeInformation information_after_failed_truncate{};
    const bool truncate_failure_atomic =
        failed_truncate_status == os::kernel::fs::Status::DeviceFailure &&
        vfs.StatOpenFile(first_file, information_after_failed_truncate) ==
            os::kernel::fs::Status::Succeeded &&
        information_after_failed_truncate.size_bytes ==
            sizeof(OS_TEST_VFS_INODE_IO_AFTER_TRUNCATE_BYTE) &&
        cache_context.truncate_cache_entry_count.load(std::memory_order_acquire) ==
            truncate_cache_count_before_failure;
    cache_context.fail_truncate_prepare.store(false, std::memory_order_release);
    test_context.Expect(truncate_failure_atomic, OS_TEST_VFS_INODE_IO_TRUNCATE_FAILURE_MESSAGE);

    const os::kernel::fs::InodeIoCoordinatorStatistics io_statistics = vfs.ReadInodeIoStatistics();
    const bool vfs_valid_before_release =
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        io_statistics.active_reference_count == 0ULL &&
        io_statistics.referenced_slot_count == 0ULL &&
        io_statistics.peak_active_reference_count >= 2ULL &&
        io_statistics.acquisition_count == io_statistics.release_count;
    const bool resources_released =
        (!first_file.open || vfs.Close(first_file) == os::kernel::fs::Status::Succeeded) &&
        (!second_file.open || vfs.Close(second_file) == os::kernel::fs::Status::Succeeded) &&
        (!file_system_context.initialized ||
         vfs.ReleaseContext(file_system_context) == os::kernel::fs::Status::Succeeded) &&
        memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Statistics().allocation_count == 0ULL;
    test_context.Expect(vfs_valid_before_release && resources_released,
                        OS_TEST_VFS_INODE_IO_RESOURCE_MESSAGE);
    return test_context.ExitCode();
}
