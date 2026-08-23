#include <os/kernel/fs/memfs.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/fs/vfs_namespace_cache.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <test_context.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_SUITE_NAME =
    "kernel/vfs_inode_metadata_cache/integration";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_SHARED_MESSAGE =
    "stat、access、exec 与打开文件 stat 必须共享同一 inode metadata fill";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_MUTATION_MESSAGE =
    "chmod、write、truncate、link、rename 与 unlink 后不得观察旧元数据";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_MODE_MESSAGE =
    "chmod 成功后下一次打开文件 stat 必须重新读取 mode";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_WRITE_MESSAGE =
    "write 成功后下一次打开文件 stat 必须重新读取 size";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_TRUNCATE_MESSAGE =
    "truncate 成功后下一次打开文件 stat 必须重新读取 size";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_LINK_MESSAGE =
    "link 成功后下一次打开文件 stat 必须重新读取 link count";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAME_MESSAGE =
    "rename 成功后下一次打开文件 stat 必须重新读取 inode metadata";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_UNLINK_MESSAGE =
    "unlink 成功后被删除 inode 的 metadata identity 必须从缓存消失";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_BYPASS_MESSAGE =
    "并发 Loading 与容量耗尽必须旁路缓存且不得改变 stat 结果";
constexpr std::string_view OS_TEST_VFS_INODE_METADATA_INTEGRATION_PARALLEL_MESSAGE =
    "不同 inode shard 的 metadata miss 必须并行进入 backend stat";
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_NODE_LIMIT = 32ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_MAXIMUM_FILE_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_MOUNT_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_DENTRY_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_INODE_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT = 4ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_STAT_DELAY_MILLISECONDS = 50ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_SUPERBLOCK_IDENTIFIER = 41ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_FAKE_NODE_IDENTIFIER_BASE = 1000ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_NODE_GENERATION = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_EXPECTED_LINK_COUNT = 2ULL;
constexpr uint64_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE = 0ULL;
constexpr os::abi::FileMode OS_TEST_VFS_INODE_METADATA_INTEGRATION_EXECUTABLE_MODE = 0000755U;
constexpr os::abi::FileMode OS_TEST_VFS_INODE_METADATA_INTEGRATION_RESTRICTED_MODE = 0000700U;
constexpr uint8_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('t'), static_cast<uint8_t>('o'),
    static_cast<uint8_t>('o'), static_cast<uint8_t>('l'),
};
constexpr uint8_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_ALIAS_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('a'), static_cast<uint8_t>('l'),
    static_cast<uint8_t>('i'), static_cast<uint8_t>('a'), static_cast<uint8_t>('s'),
};
constexpr uint8_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAMED_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('r'), static_cast<uint8_t>('e'),
    static_cast<uint8_t>('n'), static_cast<uint8_t>('a'), static_cast<uint8_t>('m'),
    static_cast<uint8_t>('e'), static_cast<uint8_t>('d'),
};
constexpr uint8_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_PAYLOAD[] = {
    static_cast<uint8_t>('m'),
    static_cast<uint8_t>('e'),
    static_cast<uint8_t>('t'),
    static_cast<uint8_t>('a'),
};
constexpr uint8_t OS_TEST_VFS_INODE_METADATA_INTEGRATION_ROOT_PATH[] = {
    static_cast<uint8_t>('/'),
};

using BackendStatOperation =
    os::kernel::fs::Status (*)(void *context, const os::kernel::fs::Vnode &vnode,
                               os::kernel::fs::BackendNodeInformation &information) noexcept;

BackendStatOperation backend_stat_operation = nullptr;
std::atomic<uint64_t> backend_stat_invocation_count{
    OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE};
std::atomic<uint64_t> active_backend_stat_count{OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE};
std::atomic<uint64_t> peak_backend_stat_count{OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE};
std::atomic<bool> backend_stat_delay_enabled{false};
uint64_t linked_node_identifier = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
uint64_t reported_link_count = OS_TEST_VFS_INODE_METADATA_INTEGRATION_COUNTER_INCREMENT;

[[nodiscard]] os::kernel::fs::Status
CountedStat(void *const context, const os::kernel::fs::Vnode &vnode,
            os::kernel::fs::BackendNodeInformation &information) noexcept {
    if (backend_stat_operation == nullptr ||
        backend_stat_invocation_count.load(std::memory_order_relaxed) == UINT64_MAX) {
        return os::kernel::fs::Status::Corrupt;
    }
    backend_stat_invocation_count.fetch_add(1ULL, std::memory_order_relaxed);
    const uint64_t active_count =
        active_backend_stat_count.fetch_add(1ULL, std::memory_order_acq_rel) + 1ULL;
    uint64_t observed_peak = peak_backend_stat_count.load(std::memory_order_relaxed);
    while (observed_peak < active_count &&
           !peak_backend_stat_count.compare_exchange_weak(
               observed_peak, active_count, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
    if (backend_stat_delay_enabled.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(
            OS_TEST_VFS_INODE_METADATA_INTEGRATION_STAT_DELAY_MILLISECONDS));
    }
    const os::kernel::fs::Status status = backend_stat_operation(context, vnode, information);
    if (status == os::kernel::fs::Status::Succeeded && vnode.identifier == linked_node_identifier) {
        information.link_count = reported_link_count;
    }
    active_backend_stat_count.fetch_sub(1ULL, std::memory_order_acq_rel);
    return status;
}

[[nodiscard]] os::kernel::fs::Status CountedLink(void *const context,
                                                 const os::kernel::fs::Vnode &source,
                                                 const os::kernel::fs::Vnode &destination_directory,
                                                 const uint8_t *const name,
                                                 const uint64_t name_length_bytes) noexcept {
    if (context == nullptr || source.superblock == nullptr ||
        destination_directory.superblock != source.superblock || name == nullptr ||
        name_length_bytes == OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE ||
        reported_link_count == UINT64_MAX) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    linked_node_identifier = source.identifier;
    ++reported_link_count;
    return os::kernel::fs::Status::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_INODE_METADATA_INTEGRATION_SUITE_NAME};
    alignas(OS_TEST_VFS_INODE_METADATA_INTEGRATION_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_bytes[OS_TEST_VFS_INODE_METADATA_INTEGRATION_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs memfs{};
    os::kernel::fs::Mount mounts[OS_TEST_VFS_INODE_METADATA_INTEGRATION_MOUNT_CAPACITY]{};
    os::kernel::fs::VfsDentrySlot
        dentry_storage[OS_TEST_VFS_INODE_METADATA_INTEGRATION_DENTRY_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot
        inode_storage[OS_TEST_VFS_INODE_METADATA_INTEGRATION_INODE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache namespace_cache{};
    os::kernel::fs::VfsResolutionContext
        resolution_contexts[OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};
    os::kernel::fs::BackendOperations counted_operations{};
    bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_bytes), sizeof(heap_bytes)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        memfs.Initialize(heap, OS_TEST_VFS_INODE_METADATA_INTEGRATION_SUPERBLOCK_IDENTIFIER,
                         OS_TEST_VFS_INODE_METADATA_INTEGRATION_NODE_LIMIT,
                         OS_TEST_VFS_INODE_METADATA_INTEGRATION_MAXIMUM_FILE_SIZE_BYTES) ==
            os::kernel::fs::Status::Succeeded;
    if (initialized) {
        counted_operations = *memfs.GetSuperblock().operations;
        backend_stat_operation = counted_operations.stat;
        counted_operations.stat = CountedStat;
        counted_operations.link = CountedLink;
        memfs.GetSuperblock().operations = &counted_operations;
    }
    initialized =
        initialized && backend_stat_operation != nullptr &&
        namespace_cache.Initialize(
            dentry_storage, OS_TEST_VFS_INODE_METADATA_INTEGRATION_DENTRY_CAPACITY, inode_storage,
            OS_TEST_VFS_INODE_METADATA_INTEGRATION_INODE_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_VFS_INODE_METADATA_INTEGRATION_MOUNT_CAPACITY,
                       memfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureNamespaceCache(namespace_cache) == os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureResolutionContexts(resolution_contexts,
                                        OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded;

    const os::kernel::fs::OpenOptions create_options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile writable_file{};
    bool shared = initialized &&
                  vfs.Open(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH,
                           sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH), create_options,
                           writable_file) == os::kernel::fs::Status::Succeeded &&
                  vfs.ChangeMode(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH,
                                 sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH),
                                 OS_TEST_VFS_INODE_METADATA_INTEGRATION_EXECUTABLE_MODE) ==
                      os::kernel::fs::Status::Succeeded;
    backend_stat_invocation_count = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
    os::kernel::fs::NodeInformation information{};
    shared = shared && vfs.Stat(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH,
                                sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH),
                                information) == os::kernel::fs::Status::Succeeded;
    const uint64_t first_fill_count = backend_stat_invocation_count;
    os::kernel::fs::OpenFile executable_file{};
    shared =
        shared && first_fill_count != OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE &&
        vfs.CheckAccess(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH,
                        sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH),
                        os::kernel::security::OS_KERNEL_ACCESS_EXECUTE) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.OpenExecutable(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH,
                           sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH),
                           executable_file) == os::kernel::fs::Status::Succeeded &&
        vfs.StatOpenFile(writable_file, information) == os::kernel::fs::Status::Succeeded &&
        backend_stat_invocation_count == first_fill_count &&
        vfs.StatOpenFileUncached(writable_file, information) == os::kernel::fs::Status::Succeeded &&
        backend_stat_invocation_count ==
            first_fill_count + OS_TEST_VFS_INODE_METADATA_INTEGRATION_COUNTER_INCREMENT &&
        vfs.Close(executable_file) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(shared, OS_TEST_VFS_INODE_METADATA_INTEGRATION_SHARED_MESSAGE);

    uint64_t previous_stat_count = backend_stat_invocation_count;
    bool mutation_consistent =
        shared &&
        vfs.ChangeMode(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH,
                       sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH),
                       OS_TEST_VFS_INODE_METADATA_INTEGRATION_RESTRICTED_MODE) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.StatOpenFile(writable_file, information) == os::kernel::fs::Status::Succeeded &&
        backend_stat_invocation_count ==
            previous_stat_count + OS_TEST_VFS_INODE_METADATA_INTEGRATION_COUNTER_INCREMENT &&
        information.mode == (os::abi::OS_ABI_FILE_MODE_REGULAR |
                             OS_TEST_VFS_INODE_METADATA_INTEGRATION_RESTRICTED_MODE);
    test_context.Expect(mutation_consistent, OS_TEST_VFS_INODE_METADATA_INTEGRATION_MODE_MESSAGE);
    previous_stat_count = backend_stat_invocation_count;
    uint64_t written_bytes = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
    mutation_consistent =
        mutation_consistent &&
        vfs.WriteAt(writable_file, OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE,
                    OS_TEST_VFS_INODE_METADATA_INTEGRATION_PAYLOAD,
                    sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_PAYLOAD),
                    written_bytes) == os::kernel::fs::Status::Succeeded &&
        written_bytes == sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_PAYLOAD) &&
        vfs.StatOpenFile(writable_file, information) == os::kernel::fs::Status::Succeeded &&
        backend_stat_invocation_count ==
            previous_stat_count + OS_TEST_VFS_INODE_METADATA_INTEGRATION_COUNTER_INCREMENT &&
        information.size_bytes == sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_PAYLOAD);
    test_context.Expect(mutation_consistent, OS_TEST_VFS_INODE_METADATA_INTEGRATION_WRITE_MESSAGE);
    previous_stat_count = backend_stat_invocation_count;
    mutation_consistent =
        mutation_consistent &&
        vfs.TruncateOpenFile(writable_file, OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.StatOpenFile(writable_file, information) == os::kernel::fs::Status::Succeeded &&
        backend_stat_invocation_count ==
            previous_stat_count + OS_TEST_VFS_INODE_METADATA_INTEGRATION_COUNTER_INCREMENT &&
        information.size_bytes == OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
    test_context.Expect(mutation_consistent,
                        OS_TEST_VFS_INODE_METADATA_INTEGRATION_TRUNCATE_MESSAGE);
    previous_stat_count = backend_stat_invocation_count;
    mutation_consistent =
        mutation_consistent &&
        vfs.Link(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH,
                 sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH),
                 OS_TEST_VFS_INODE_METADATA_INTEGRATION_ALIAS_PATH,
                 sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_ALIAS_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.StatOpenFile(writable_file, information) == os::kernel::fs::Status::Succeeded &&
        backend_stat_invocation_count > previous_stat_count &&
        information.link_count == OS_TEST_VFS_INODE_METADATA_INTEGRATION_EXPECTED_LINK_COUNT;
    test_context.Expect(mutation_consistent, OS_TEST_VFS_INODE_METADATA_INTEGRATION_LINK_MESSAGE);
    previous_stat_count = backend_stat_invocation_count;
    mutation_consistent =
        mutation_consistent &&
        vfs.Rename(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH,
                   sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_FILE_PATH),
                   OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAMED_PATH,
                   sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAMED_PATH),
                   false) == os::kernel::fs::Status::Succeeded &&
        vfs.StatOpenFile(writable_file, information) == os::kernel::fs::Status::Succeeded &&
        backend_stat_invocation_count > previous_stat_count;
    test_context.Expect(mutation_consistent, OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAME_MESSAGE);
    os::kernel::fs::Path renamed_path{};
    mutation_consistent =
        mutation_consistent &&
        vfs.Resolve(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAMED_PATH,
                    sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAMED_PATH),
                    renamed_path) == os::kernel::fs::Status::Succeeded &&
        vfs.Close(writable_file) == os::kernel::fs::Status::Succeeded &&
        vfs.RemoveFile(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAMED_PATH,
                       sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_RENAMED_PATH)) ==
            os::kernel::fs::Status::Succeeded;
    const os::kernel::fs::VfsNamespaceCacheStatus removed_metadata_status =
        mutation_consistent
            ? namespace_cache.InvalidateInodeMetadata(os::kernel::fs::VfsInodeIdentity{
                  .superblock_identifier = renamed_path.vnode.superblock->identifier,
                  .superblock_generation = renamed_path.vnode.superblock->generation,
                  .node_identifier = renamed_path.vnode.identifier,
                  .node_generation = renamed_path.vnode.generation,
              })
            : os::kernel::fs::VfsNamespaceCacheStatus::Corrupt;
    mutation_consistent =
        mutation_consistent &&
        (removed_metadata_status == os::kernel::fs::VfsNamespaceCacheStatus::InodeNotFound ||
         removed_metadata_status ==
             os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataNotFound) &&
        namespace_cache.Statistics().inode_metadata_hit_count >
            OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE &&
        namespace_cache.Statistics().inode_metadata_invalidation_count >
            OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE &&
        namespace_cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    test_context.Expect(mutation_consistent, OS_TEST_VFS_INODE_METADATA_INTEGRATION_UNLINK_MESSAGE);
    test_context.Expect(mutation_consistent,
                        OS_TEST_VFS_INODE_METADATA_INTEGRATION_MUTATION_MESSAGE);

    bool metadata_shard_selected[os::kernel::fs::OS_KERNEL_VFS_METADATA_LOCK_SHARD_COUNT]{};
    std::string metadata_paths[OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT]{};
    uint64_t selected_metadata_path_count = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
    bool parallel_ready = mutation_consistent;
    for (uint64_t candidate_index = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
         parallel_ready &&
         candidate_index < OS_TEST_VFS_INODE_METADATA_INTEGRATION_INODE_CAPACITY &&
         selected_metadata_path_count < OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT;
         ++candidate_index) {
        const std::string candidate_path = "/metadata_" + std::to_string(candidate_index);
        os::kernel::fs::OpenFile candidate_file{};
        os::kernel::fs::Path candidate_vfs_path{};
        parallel_ready =
            vfs.Open(context, reinterpret_cast<const uint8_t *>(candidate_path.data()),
                     candidate_path.size(), create_options,
                     candidate_file) == os::kernel::fs::Status::Succeeded &&
            vfs.Close(candidate_file) == os::kernel::fs::Status::Succeeded &&
            vfs.Resolve(context, reinterpret_cast<const uint8_t *>(candidate_path.data()),
                        candidate_path.size(),
                        candidate_vfs_path) == os::kernel::fs::Status::Succeeded;
        if (!parallel_ready) {
            break;
        }
        const os::kernel::fs::VfsInodeIdentity identity{
            .superblock_identifier = candidate_vfs_path.vnode.superblock->identifier,
            .superblock_generation = candidate_vfs_path.vnode.superblock->generation,
            .node_identifier = candidate_vfs_path.vnode.identifier,
            .node_generation = candidate_vfs_path.vnode.generation,
        };
        const uint64_t shard_index = os::kernel::fs::VfsInodeIdentityHash(identity) %
                                     os::kernel::fs::OS_KERNEL_VFS_METADATA_LOCK_SHARD_COUNT;
        if (metadata_shard_selected[shard_index]) {
            continue;
        }
        metadata_shard_selected[shard_index] = true;
        metadata_paths[selected_metadata_path_count] = candidate_path;
        const os::kernel::fs::VfsNamespaceCacheStatus invalidation_status =
            namespace_cache.InvalidateInodeMetadata(identity);
        parallel_ready =
            invalidation_status == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded ||
            invalidation_status == os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataNotFound;
        ++selected_metadata_path_count;
    }
    parallel_ready = parallel_ready && selected_metadata_path_count ==
                                           OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT;
    backend_stat_invocation_count.store(OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE,
                                        std::memory_order_relaxed);
    peak_backend_stat_count.store(OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE,
                                  std::memory_order_relaxed);
    backend_stat_delay_enabled.store(true, std::memory_order_release);
    bool parallel_results[OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT]{};
    std::thread metadata_workers[OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT];
    for (uint64_t thread_index = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
         thread_index < OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT; ++thread_index) {
        metadata_workers[thread_index] =
            std::thread([&vfs, &context, &metadata_paths, &parallel_results, thread_index]() {
                os::kernel::fs::NodeInformation thread_information{};
                parallel_results[thread_index] =
                    vfs.Stat(context,
                             reinterpret_cast<const uint8_t *>(metadata_paths[thread_index].data()),
                             metadata_paths[thread_index].size(),
                             thread_information) == os::kernel::fs::Status::Succeeded;
            });
    }
    bool parallel_consistent = parallel_ready;
    for (uint64_t thread_index = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
         thread_index < OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT; ++thread_index) {
        metadata_workers[thread_index].join();
        parallel_consistent = parallel_consistent && parallel_results[thread_index];
    }
    backend_stat_delay_enabled.store(false, std::memory_order_release);
    parallel_consistent = parallel_consistent &&
                          backend_stat_invocation_count.load(std::memory_order_relaxed) ==
                              OS_TEST_VFS_INODE_METADATA_INTEGRATION_THREAD_COUNT &&
                          peak_backend_stat_count.load(std::memory_order_relaxed) >
                              OS_TEST_VFS_INODE_METADATA_INTEGRATION_COUNTER_INCREMENT;
    test_context.Expect(parallel_consistent,
                        OS_TEST_VFS_INODE_METADATA_INTEGRATION_PARALLEL_MESSAGE);

    uint64_t evicted_dentry_count = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
    uint64_t evicted_inode_count = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
    os::kernel::fs::VfsInodeMetadataToken root_load_token{};
    os::kernel::fs::VfsInodeMetadataSnapshot metadata_snapshot{};
    const os::kernel::fs::VfsInodeIdentity root_identity{
        .superblock_identifier = context.root.vnode.superblock->identifier,
        .superblock_generation = context.root.vnode.superblock->generation,
        .node_identifier = context.root.vnode.identifier,
        .node_generation = context.root.vnode.generation,
    };
    bool bypass_consistent =
        parallel_consistent &&
        namespace_cache.EvictDentries(UINT64_MAX, evicted_dentry_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        namespace_cache.EvictInodes(UINT64_MAX, evicted_inode_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        namespace_cache.PrepareInodeMetadata(root_identity, os::kernel::fs::NodeType::Directory,
                                             root_load_token, metadata_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadRequired;
    previous_stat_count = backend_stat_invocation_count;
    bypass_consistent = bypass_consistent &&
                        vfs.Stat(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_ROOT_PATH,
                                 sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_ROOT_PATH),
                                 information) == os::kernel::fs::Status::Succeeded &&
                        backend_stat_invocation_count > previous_stat_count &&
                        namespace_cache.Statistics().inode_metadata_load_contention_count >
                            OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE &&
                        namespace_cache.CancelInodeMetadata(root_load_token) ==
                            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;

    os::kernel::fs::VfsInodeMetadataToken
        capacity_tokens[OS_TEST_VFS_INODE_METADATA_INTEGRATION_INODE_CAPACITY]{};
    uint64_t prepared_capacity_count = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
    for (uint64_t slot_index = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
         bypass_consistent && slot_index < OS_TEST_VFS_INODE_METADATA_INTEGRATION_INODE_CAPACITY;
         ++slot_index) {
        const os::kernel::fs::VfsInodeIdentity fake_identity{
            .superblock_identifier = OS_TEST_VFS_INODE_METADATA_INTEGRATION_SUPERBLOCK_IDENTIFIER,
            .superblock_generation = context.root.vnode.superblock->generation,
            .node_identifier =
                OS_TEST_VFS_INODE_METADATA_INTEGRATION_FAKE_NODE_IDENTIFIER_BASE + slot_index,
            .node_generation = OS_TEST_VFS_INODE_METADATA_INTEGRATION_NODE_GENERATION,
        };
        bypass_consistent = namespace_cache.PrepareInodeMetadata(
                                fake_identity, os::kernel::fs::NodeType::RegularFile,
                                capacity_tokens[slot_index], metadata_snapshot) ==
                            os::kernel::fs::VfsNamespaceCacheStatus::InodeMetadataLoadRequired;
        if (bypass_consistent) {
            ++prepared_capacity_count;
        }
    }
    previous_stat_count = backend_stat_invocation_count;
    bypass_consistent = bypass_consistent &&
                        vfs.Stat(context, OS_TEST_VFS_INODE_METADATA_INTEGRATION_ROOT_PATH,
                                 sizeof(OS_TEST_VFS_INODE_METADATA_INTEGRATION_ROOT_PATH),
                                 information) == os::kernel::fs::Status::Succeeded &&
                        backend_stat_invocation_count > previous_stat_count &&
                        namespace_cache.Statistics().capacity_rejection_count >
                            OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
    bool capacity_cleanup_succeeded = true;
    for (uint64_t slot_index = OS_TEST_VFS_INODE_METADATA_INTEGRATION_EMPTY_VALUE;
         slot_index < prepared_capacity_count; ++slot_index) {
        if (namespace_cache.CancelInodeMetadata(capacity_tokens[slot_index]) !=
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded) {
            capacity_cleanup_succeeded = false;
        }
    }
    bypass_consistent =
        bypass_consistent && capacity_cleanup_succeeded &&
        namespace_cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    test_context.Expect(bypass_consistent, OS_TEST_VFS_INODE_METADATA_INTEGRATION_BYPASS_MESSAGE);

    const bool released =
        bypass_consistent && vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        namespace_cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    return released ? test_context.ExitCode() : 1;
}
