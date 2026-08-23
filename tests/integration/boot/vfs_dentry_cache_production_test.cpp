#include <os/kernel/fs/memfs.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/fs/vfs_namespace_cache.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <test_context.hpp>

#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_SUITE_NAME =
    "kernel/vfs_dentry_cache/production";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_HIT_MESSAGE =
    "Positive/Negative dentry 必须减少重复 backend lookup 且 create 后撤销 Negative";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_MUTATION_MESSAGE =
    "rename/remove 必须撤销旧 key 并允许新 key 重新发布";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_MESSAGE =
    "backend EIO 不得发布为 Negative，后续 NotFound 才允许缓存";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_CONCURRENCY_MESSAGE =
    "并发同组件 miss 必须由 resolution transaction 合并为一次 backend lookup";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_RECLAIM_MESSAGE =
    "namespace shrinker 必须回收零引用 dentry/inode 且下一次解析正确回填";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_RMDIR_MESSAGE =
    "rmdir 必须级联撤销旧目录 identity 下的 Negative child";
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_NODE_LIMIT = 32ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_MAXIMUM_FILE_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_MOUNT_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT = 8ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_SUPERBLOCK_IDENTIFIER = 61ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP = 1ULL;
constexpr uint8_t OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('a'), static_cast<uint8_t>('l'),
    static_cast<uint8_t>('p'), static_cast<uint8_t>('h'), static_cast<uint8_t>('a'),
};
constexpr uint8_t OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('b'), static_cast<uint8_t>('e'),
    static_cast<uint8_t>('t'), static_cast<uint8_t>('a'),
};
constexpr uint8_t OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('m'), static_cast<uint8_t>('i'),
    static_cast<uint8_t>('s'), static_cast<uint8_t>('s'), static_cast<uint8_t>('i'),
    static_cast<uint8_t>('n'), static_cast<uint8_t>('g'),
};
constexpr uint8_t OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('e'), static_cast<uint8_t>('r'),
    static_cast<uint8_t>('r'), static_cast<uint8_t>('o'), static_cast<uint8_t>('r'),
};
constexpr uint8_t OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_NAME[] = {
    static_cast<uint8_t>('e'), static_cast<uint8_t>('r'), static_cast<uint8_t>('r'),
    static_cast<uint8_t>('o'), static_cast<uint8_t>('r'),
};
constexpr uint8_t OS_TEST_VFS_DENTRY_PRODUCTION_DIRECTORY_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('d'), static_cast<uint8_t>('i'),
    static_cast<uint8_t>('r'),
};
constexpr uint8_t OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('d'), static_cast<uint8_t>('i'),
    static_cast<uint8_t>('r'), static_cast<uint8_t>('/'), static_cast<uint8_t>('g'),
    static_cast<uint8_t>('h'), static_cast<uint8_t>('o'), static_cast<uint8_t>('s'),
    static_cast<uint8_t>('t'),
};
constexpr uint8_t OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_NAME[] = {
    static_cast<uint8_t>('g'), static_cast<uint8_t>('h'), static_cast<uint8_t>('o'),
    static_cast<uint8_t>('s'), static_cast<uint8_t>('t'),
};

using BackendLookupOperation = os::kernel::fs::Status (*)(
    void *context, const os::kernel::fs::Vnode &directory, const uint8_t *name,
    uint64_t name_length_bytes, os::kernel::fs::Vnode &vnode) noexcept;

BackendLookupOperation backend_lookup_operation = nullptr;
uint64_t backend_lookup_count = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
bool backend_failure_injection_armed = false;

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
         byte_index < length_bytes; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] os::kernel::fs::Status
CountedLookup(void *const context, const os::kernel::fs::Vnode &directory,
              const uint8_t *const name, const uint64_t name_length_bytes,
              os::kernel::fs::Vnode &vnode) noexcept {
    if (backend_lookup_operation == nullptr || backend_lookup_count == UINT64_MAX) {
        return os::kernel::fs::Status::Corrupt;
    }
    ++backend_lookup_count;
    if (backend_failure_injection_armed &&
        name_length_bytes == sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_NAME) &&
        BytesEqual(name, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_NAME, name_length_bytes)) {
        backend_failure_injection_armed = false;
        vnode = os::kernel::fs::Vnode{};
        return os::kernel::fs::Status::DeviceFailure;
    }
    return backend_lookup_operation(context, directory, name, name_length_bytes, vnode);
}

[[nodiscard]] bool OpenAndCloseCreatedFile(os::kernel::fs::Vfs &vfs,
                                           const os::kernel::fs::FsContext &context,
                                           const uint8_t *const path,
                                           const uint64_t path_length_bytes) noexcept {
    const os::kernel::fs::OpenOptions options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile open_file{};
    return vfs.Open(context, path, path_length_bytes, options, open_file) ==
               os::kernel::fs::Status::Succeeded &&
           vfs.Close(open_file) == os::kernel::fs::Status::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_DENTRY_PRODUCTION_SUITE_NAME};
    alignas(OS_TEST_VFS_DENTRY_PRODUCTION_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_bytes[OS_TEST_VFS_DENTRY_PRODUCTION_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs memfs{};
    os::kernel::fs::BackendOperations counted_operations{};
    os::kernel::fs::VfsDentrySlot
        dentry_storage[OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot inode_storage[OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceHashEntry
        dentry_hash_entries[OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY]{};
    uint64_t dentry_hash_buckets[OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY]{};
    os::kernel::fs::VfsNamespaceHashEntry
        inode_hash_entries[OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY]{};
    uint64_t inode_hash_buckets[OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache namespace_cache{};
    os::kernel::fs::Mount mounts[OS_TEST_VFS_DENTRY_PRODUCTION_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};
    bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_bytes), sizeof(heap_bytes)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        memfs.Initialize(heap, OS_TEST_VFS_DENTRY_PRODUCTION_SUPERBLOCK_IDENTIFIER,
                         OS_TEST_VFS_DENTRY_PRODUCTION_NODE_LIMIT,
                         OS_TEST_VFS_DENTRY_PRODUCTION_MAXIMUM_FILE_SIZE_BYTES) ==
            os::kernel::fs::Status::Succeeded;
    if (initialized) {
        counted_operations = *memfs.GetSuperblock().operations;
        backend_lookup_operation = counted_operations.lookup;
        counted_operations.lookup = CountedLookup;
        memfs.GetSuperblock().operations = &counted_operations;
    }
    initialized =
        initialized && backend_lookup_operation != nullptr &&
        namespace_cache.Initialize(dentry_storage, OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY,
                                   inode_storage, OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        namespace_cache.ConfigureHashIndex(
            dentry_hash_entries, OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY,
            dentry_hash_buckets, OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY,
            inode_hash_entries, OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY,
            inode_hash_buckets, OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_VFS_DENTRY_PRODUCTION_MOUNT_CAPACITY,
                       memfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureNamespaceCache(namespace_cache) == os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        OpenAndCloseCreatedFile(vfs, context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                                sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH));
    uint64_t evicted_count = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
    initialized = initialized &&
                  namespace_cache.EvictDentries(UINT64_MAX, evicted_count) ==
                      os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;

    backend_lookup_count = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
    os::kernel::fs::Path resolved{};
    bool hits_consistent =
        initialized &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH), resolved) ==
            os::kernel::fs::Status::Succeeded &&
        backend_lookup_count == OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH), resolved) ==
            os::kernel::fs::Status::Succeeded &&
        backend_lookup_count == OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH), resolved) ==
            os::kernel::fs::Status::NotFound;
    const uint64_t missing_fill_count = backend_lookup_count;
    hits_consistent =
        hits_consistent &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH), resolved) ==
            os::kernel::fs::Status::NotFound &&
        backend_lookup_count == missing_fill_count &&
        OpenAndCloseCreatedFile(vfs, context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                                sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH)) &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH), resolved) ==
            os::kernel::fs::Status::Succeeded &&
        backend_lookup_count > missing_fill_count;
    test_context.Expect(hits_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_HIT_MESSAGE);

    backend_failure_injection_armed = true;
    const uint64_t lookup_count_before_failure = backend_lookup_count;
    bool error_consistent =
        hits_consistent &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH), resolved) ==
            os::kernel::fs::Status::DeviceFailure &&
        backend_lookup_count ==
            lookup_count_before_failure + OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH), resolved) ==
            os::kernel::fs::Status::NotFound;
    const uint64_t lookup_count_after_not_found = backend_lookup_count;
    error_consistent =
        error_consistent &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH), resolved) ==
            os::kernel::fs::Status::NotFound &&
        backend_lookup_count == lookup_count_after_not_found;
    test_context.Expect(error_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_MESSAGE);

    uint64_t previous_lookup_count = backend_lookup_count;
    bool mutation_consistent =
        error_consistent &&
        vfs.Rename(context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                   sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH),
                   OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH,
                   sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH), false) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH), resolved) ==
            os::kernel::fs::Status::NotFound &&
        backend_lookup_count > previous_lookup_count;
    previous_lookup_count = backend_lookup_count;
    mutation_consistent =
        mutation_consistent &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH), resolved) ==
            os::kernel::fs::Status::Succeeded &&
        backend_lookup_count > previous_lookup_count &&
        vfs.RemoveFile(context, OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH,
                       sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH)) ==
            os::kernel::fs::Status::Succeeded;
    previous_lookup_count = backend_lookup_count;
    mutation_consistent =
        mutation_consistent &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH), resolved) ==
            os::kernel::fs::Status::NotFound &&
        backend_lookup_count > previous_lookup_count;
    test_context.Expect(mutation_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_MUTATION_MESSAGE);

    bool concurrent_results[OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT]{};
    std::thread workers[OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT];
    const bool concurrency_ready =
        mutation_consistent &&
        namespace_cache.EvictDentries(UINT64_MAX, evicted_count) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    backend_lookup_count = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
    for (uint64_t thread_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
         thread_index < OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT; ++thread_index) {
        workers[thread_index] = std::thread([&vfs, &context, &concurrent_results, thread_index]() {
            os::kernel::fs::Path thread_path{};
            concurrent_results[thread_index] =
                vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                            sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH), thread_path) ==
                os::kernel::fs::Status::Succeeded;
        });
    }
    bool concurrency_consistent = concurrency_ready;
    for (uint64_t thread_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
         thread_index < OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT; ++thread_index) {
        workers[thread_index].join();
        concurrency_consistent = concurrency_consistent && concurrent_results[thread_index];
    }
    concurrency_consistent =
        concurrency_consistent &&
        backend_lookup_count == OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP;
    test_context.Expect(concurrency_consistent,
                        OS_TEST_VFS_DENTRY_PRODUCTION_CONCURRENCY_MESSAGE);

    os::kernel::fs::NamespaceCacheReclaimResult reclaim_result{};
    const uint64_t lookup_count_before_reclaim = backend_lookup_count;
    const bool reclaim_consistent =
        concurrency_consistent &&
        vfs.ReclaimNamespaceCache(UINT64_MAX, UINT64_MAX, reclaim_result) ==
            os::kernel::fs::Status::Succeeded &&
        reclaim_result.dentry_count != OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE &&
        vfs.ReadStatistics().namespace_reclaim_operation_count ==
            OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
        vfs.ReadStatistics().reclaimed_dentry_count == reclaim_result.dentry_count &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH), resolved) ==
            os::kernel::fs::Status::Succeeded &&
        backend_lookup_count > lookup_count_before_reclaim &&
        namespace_cache.Statistics().dentry_hash_bucket_capacity ==
            OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY &&
        namespace_cache.Statistics().inode_hash_bucket_capacity ==
            OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY &&
        namespace_cache.Validate() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    test_context.Expect(reclaim_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_RECLAIM_MESSAGE);

    os::kernel::fs::Path directory_path{};
    os::kernel::fs::VfsDentryKey child_key{};
    os::kernel::fs::VfsDentrySnapshot child_snapshot{};
    const bool rmdir_consistent =
        reclaim_consistent &&
        vfs.CreateDirectory(context, OS_TEST_VFS_DENTRY_PRODUCTION_DIRECTORY_PATH,
                            sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_DIRECTORY_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_DIRECTORY_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_DIRECTORY_PATH), directory_path) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_PATH), resolved) ==
            os::kernel::fs::Status::NotFound &&
        os::kernel::fs::BuildVfsDentryKey(
            directory_path.mount_identifier,
            os::kernel::fs::VfsInodeIdentity{
                .superblock_identifier = directory_path.vnode.superblock->identifier,
                .superblock_generation = directory_path.vnode.superblock->generation,
                .node_identifier = directory_path.vnode.identifier,
                .node_generation = directory_path.vnode.generation,
            },
            OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_NAME,
            sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_NAME), child_key) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        vfs.RemoveDirectory(context, OS_TEST_VFS_DENTRY_PRODUCTION_DIRECTORY_PATH,
                            sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_DIRECTORY_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        namespace_cache.AcquireDentry(child_key, child_snapshot) ==
            os::kernel::fs::VfsNamespaceCacheStatus::DentryNotFound;
    test_context.Expect(rmdir_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_RMDIR_MESSAGE);

    const bool released =
        rmdir_consistent &&
        vfs.RemoveFile(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                       sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        namespace_cache.Destroy() == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    return released ? test_context.ExitCode() : 1;
}
