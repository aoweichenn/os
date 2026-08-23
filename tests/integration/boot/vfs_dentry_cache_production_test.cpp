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

constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_SUITE_NAME =
    "kernel/vfs_dentry_cache/production";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_HIT_MESSAGE =
    "Positive/Negative dentry 必须减少重复 backend lookup 且 create 后撤销 Negative";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_MUTATION_MESSAGE =
    "rename/remove 必须撤销旧 key 并允许新 key 重新发布";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_MESSAGE =
    "backend EIO 不得发布为 Negative，后续 NotFound 才允许缓存";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_CONCURRENCY_MESSAGE =
    "并发同组件 miss 必须合并为一次 backend lookup";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_PARALLEL_MESSAGE =
    "不同 lookup shard 必须使用独立解析上下文并行进入 backend";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_SEQUENCE_MESSAGE =
    "命名空间提交跨越解析窗口时必须检测 sequence 变化并重试";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_RECLAIM_MESSAGE =
    "namespace shrinker 必须回收条目、重建 compact hash 并释放 preferred 页层";
constexpr std::string_view OS_TEST_VFS_DENTRY_PRODUCTION_RMDIR_MESSAGE =
    "rmdir 必须级联撤销旧目录 identity 下的 Negative child";
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_NODE_LIMIT = 32ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_MAXIMUM_FILE_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_MOUNT_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_COMPACT_HASH_BUCKET_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT = 8ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_BACKING_PAGE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_STABLE_PAGE_COUNT = 5ULL;
constexpr uint64_t OS_TEST_VFS_DENTRY_PRODUCTION_LOOKUP_DELAY_MILLISECONDS = 50ULL;
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
    static_cast<uint8_t>('/'),
    static_cast<uint8_t>('d'),
    static_cast<uint8_t>('i'),
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

using BackendLookupOperation = os::kernel::fs::Status (*)(void *context,
                                                          const os::kernel::fs::Vnode &directory,
                                                          const uint8_t *name,
                                                          uint64_t name_length_bytes,
                                                          os::kernel::fs::Vnode &vnode) noexcept;

BackendLookupOperation backend_lookup_operation = nullptr;
std::atomic<uint64_t> backend_lookup_count{OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE};
std::atomic<uint64_t> active_backend_lookup_count{OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE};
std::atomic<uint64_t> peak_backend_lookup_count{OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE};
std::atomic<bool> backend_failure_injection_armed{false};
std::atomic<bool> backend_lookup_delay_enabled{false};

struct TestBackingRelease final {
    uint64_t page_count;
    bool released;
};

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] os::kernel::fs::Status CountedLookup(void *const context,
                                                   const os::kernel::fs::Vnode &directory,
                                                   const uint8_t *const name,
                                                   const uint64_t name_length_bytes,
                                                   os::kernel::fs::Vnode &vnode) noexcept {
    if (backend_lookup_operation == nullptr ||
        backend_lookup_count.load(std::memory_order_relaxed) == UINT64_MAX) {
        return os::kernel::fs::Status::Corrupt;
    }
    backend_lookup_count.fetch_add(1ULL, std::memory_order_relaxed);
    const uint64_t active_count =
        active_backend_lookup_count.fetch_add(1ULL, std::memory_order_acq_rel) + 1ULL;
    uint64_t observed_peak = peak_backend_lookup_count.load(std::memory_order_relaxed);
    while (observed_peak < active_count &&
           !peak_backend_lookup_count.compare_exchange_weak(
               observed_peak, active_count, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
    if (backend_lookup_delay_enabled.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(OS_TEST_VFS_DENTRY_PRODUCTION_LOOKUP_DELAY_MILLISECONDS));
    }
    os::kernel::fs::Status status{};
    if (backend_failure_injection_armed.load(std::memory_order_acquire) &&
        name_length_bytes == sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_NAME) &&
        BytesEqual(name, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_NAME, name_length_bytes)) {
        backend_failure_injection_armed.store(false, std::memory_order_release);
        vnode = os::kernel::fs::Vnode{};
        status = os::kernel::fs::Status::DeviceFailure;
    } else {
        status = backend_lookup_operation(context, directory, name, name_length_bytes, vnode);
    }
    active_backend_lookup_count.fetch_sub(1ULL, std::memory_order_acq_rel);
    return status;
}

[[nodiscard]] os::kernel::fs::Status ReleaseTestBacking(void *const context,
                                                        uint64_t &released_page_count) noexcept {
    released_page_count = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
    if (context == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    TestBackingRelease &backing = *static_cast<TestBackingRelease *>(context);
    if (backing.released || backing.page_count == OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    backing.released = true;
    released_page_count = backing.page_count;
    return os::kernel::fs::Status::Succeeded;
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
    os::kernel::fs::VfsDentrySlot dentry_storage[OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY]{};
    os::kernel::fs::VfsInodeSlot inode_storage[OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY]{};
    os::kernel::fs::VfsNamespaceHashEntry
        dentry_hash_entries[OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY]{};
    uint64_t dentry_hash_buckets[OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY]{};
    uint64_t
        compact_dentry_hash_buckets[OS_TEST_VFS_DENTRY_PRODUCTION_COMPACT_HASH_BUCKET_CAPACITY]{};
    os::kernel::fs::VfsNamespaceHashEntry
        inode_hash_entries[OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY]{};
    uint64_t inode_hash_buckets[OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY]{};
    uint64_t
        compact_inode_hash_buckets[OS_TEST_VFS_DENTRY_PRODUCTION_COMPACT_HASH_BUCKET_CAPACITY]{};
    os::kernel::fs::VfsNamespaceCache namespace_cache{};
    os::kernel::fs::Mount mounts[OS_TEST_VFS_DENTRY_PRODUCTION_MOUNT_CAPACITY]{};
    os::kernel::fs::VfsResolutionContext
        resolution_contexts[OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT]{};
    TestBackingRelease backing_release{
        .page_count = OS_TEST_VFS_DENTRY_PRODUCTION_BACKING_PAGE_COUNT,
        .released = false,
    };
    const os::kernel::fs::NamespaceBackingResourceUsage stable_backing_usage{
        .page_count = OS_TEST_VFS_DENTRY_PRODUCTION_STABLE_PAGE_COUNT,
        .allocated_frame_count = OS_TEST_VFS_DENTRY_PRODUCTION_STABLE_PAGE_COUNT,
        .buddy_active_block_count = OS_TEST_VFS_DENTRY_PRODUCTION_STABLE_PAGE_COUNT,
        .virtual_address_allocated_page_count = OS_TEST_VFS_DENTRY_PRODUCTION_STABLE_PAGE_COUNT,
        .virtual_address_active_descriptor_count = 1ULL,
        .virtual_address_active_allocation_count = 1ULL,
    };
    const os::kernel::fs::NamespaceBackingResourceUsage preferred_backing_usage{
        .page_count = OS_TEST_VFS_DENTRY_PRODUCTION_BACKING_PAGE_COUNT,
        .allocated_frame_count = OS_TEST_VFS_DENTRY_PRODUCTION_BACKING_PAGE_COUNT,
        .buddy_active_block_count = OS_TEST_VFS_DENTRY_PRODUCTION_BACKING_PAGE_COUNT,
        .virtual_address_allocated_page_count = OS_TEST_VFS_DENTRY_PRODUCTION_BACKING_PAGE_COUNT,
        .virtual_address_active_descriptor_count = 1ULL,
        .virtual_address_active_allocation_count = 1ULL,
    };
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
            dentry_hash_entries, OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY, dentry_hash_buckets,
            OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY, inode_hash_entries,
            OS_TEST_VFS_DENTRY_PRODUCTION_CACHE_CAPACITY, inode_hash_buckets,
            OS_TEST_VFS_DENTRY_PRODUCTION_HASH_BUCKET_CAPACITY) ==
            os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_VFS_DENTRY_PRODUCTION_MOUNT_CAPACITY,
                       memfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureNamespaceCache(namespace_cache) == os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureResolutionContexts(resolution_contexts,
                                        OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureNamespaceCacheShrinkTier(
            compact_dentry_hash_buckets, OS_TEST_VFS_DENTRY_PRODUCTION_COMPACT_HASH_BUCKET_CAPACITY,
            compact_inode_hash_buckets, OS_TEST_VFS_DENTRY_PRODUCTION_COMPACT_HASH_BUCKET_CAPACITY,
            stable_backing_usage, preferred_backing_usage, &backing_release,
            ReleaseTestBacking) == os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        OpenAndCloseCreatedFile(vfs, context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                                sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH));
    uint64_t evicted_count = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
    initialized = initialized && namespace_cache.EvictDentries(UINT64_MAX, evicted_count) ==
                                     os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;

    backend_lookup_count = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
    os::kernel::fs::Path resolved{};
    bool hits_consistent = initialized &&
                           vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                                       sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH),
                                       resolved) == os::kernel::fs::Status::Succeeded &&
                           backend_lookup_count == OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
                           vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                                       sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH),
                                       resolved) == os::kernel::fs::Status::Succeeded &&
                           backend_lookup_count == OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
                           vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                                       sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH),
                                       resolved) == os::kernel::fs::Status::NotFound;
    const uint64_t missing_fill_count = backend_lookup_count;
    hits_consistent =
        hits_consistent &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH),
                    resolved) == os::kernel::fs::Status::NotFound &&
        backend_lookup_count == missing_fill_count &&
        OpenAndCloseCreatedFile(vfs, context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                                sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH)) &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH),
                    resolved) == os::kernel::fs::Status::Succeeded &&
        backend_lookup_count > missing_fill_count;
    test_context.Expect(hits_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_HIT_MESSAGE);

    backend_failure_injection_armed = true;
    const uint64_t lookup_count_before_failure = backend_lookup_count;
    bool error_consistent = hits_consistent &&
                            vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH,
                                        sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH),
                                        resolved) == os::kernel::fs::Status::DeviceFailure &&
                            backend_lookup_count == lookup_count_before_failure +
                                                        OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
                            vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH,
                                        sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH),
                                        resolved) == os::kernel::fs::Status::NotFound;
    const uint64_t lookup_count_after_not_found = backend_lookup_count;
    error_consistent = error_consistent &&
                       vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH,
                                   sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_PATH),
                                   resolved) == os::kernel::fs::Status::NotFound &&
                       backend_lookup_count == lookup_count_after_not_found;
    test_context.Expect(error_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_ERROR_MESSAGE);

    uint64_t previous_lookup_count = backend_lookup_count;
    bool mutation_consistent = error_consistent &&
                               vfs.Rename(context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                                          sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH),
                                          OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH,
                                          sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH),
                                          false) == os::kernel::fs::Status::Succeeded &&
                               vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH,
                                           sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_ALPHA_PATH),
                                           resolved) == os::kernel::fs::Status::NotFound &&
                               backend_lookup_count > previous_lookup_count;
    previous_lookup_count = backend_lookup_count;
    mutation_consistent = mutation_consistent &&
                          vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH,
                                      sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH),
                                      resolved) == os::kernel::fs::Status::Succeeded &&
                          backend_lookup_count > previous_lookup_count &&
                          vfs.RemoveFile(context, OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH,
                                         sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH)) ==
                              os::kernel::fs::Status::Succeeded;
    previous_lookup_count = backend_lookup_count;
    mutation_consistent = mutation_consistent &&
                          vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH,
                                      sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_BETA_PATH),
                                      resolved) == os::kernel::fs::Status::NotFound &&
                          backend_lookup_count > previous_lookup_count;
    test_context.Expect(mutation_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_MUTATION_MESSAGE);

    bool concurrent_results[OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT]{};
    std::thread workers[OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT];
    const bool concurrency_ready =
        mutation_consistent && namespace_cache.EvictDentries(UINT64_MAX, evicted_count) ==
                                   os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    backend_lookup_count.store(OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE,
                               std::memory_order_relaxed);
    backend_lookup_delay_enabled.store(true, std::memory_order_release);
    for (uint64_t thread_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
         thread_index < OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT; ++thread_index) {
        workers[thread_index] = std::thread([&vfs, &context, &concurrent_results, thread_index]() {
            os::kernel::fs::Path thread_path{};
            concurrent_results[thread_index] =
                vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                            sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH),
                            thread_path) == os::kernel::fs::Status::Succeeded;
        });
    }
    bool concurrency_consistent = concurrency_ready;
    for (uint64_t thread_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
         thread_index < OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT; ++thread_index) {
        workers[thread_index].join();
        concurrency_consistent = concurrency_consistent && concurrent_results[thread_index];
    }
    backend_lookup_delay_enabled.store(false, std::memory_order_release);
    concurrency_consistent =
        concurrency_consistent && backend_lookup_count.load(std::memory_order_relaxed) ==
                                      OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP;
    test_context.Expect(concurrency_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_CONCURRENCY_MESSAGE);

    bool lookup_shard_selected[os::kernel::fs::OS_KERNEL_VFS_LOOKUP_LOCK_SHARD_COUNT]{};
    std::string parallel_paths[OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT]{};
    uint64_t selected_path_count = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
    for (uint64_t candidate_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
         candidate_index < 1024ULL &&
         selected_path_count < OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT;
         ++candidate_index) {
        const std::string candidate_path = "/parallel_" + std::to_string(candidate_index);
        os::kernel::fs::VfsDentryKey candidate_key{};
        if (os::kernel::fs::BuildVfsDentryKey(
                context.root.mount_identifier,
                os::kernel::fs::VfsInodeIdentity{
                    .superblock_identifier = context.root.vnode.superblock->identifier,
                    .superblock_generation = context.root.vnode.superblock->generation,
                    .node_identifier = context.root.vnode.identifier,
                    .node_generation = context.root.vnode.generation,
                },
                reinterpret_cast<const uint8_t *>(candidate_path.data() + 1ULL),
                candidate_path.size() - 1ULL,
                candidate_key) != os::kernel::fs::VfsNamespaceCacheStatus::Succeeded) {
            continue;
        }
        const uint64_t shard_index = os::kernel::fs::VfsDentryKeyHash(candidate_key) %
                                     os::kernel::fs::OS_KERNEL_VFS_LOOKUP_LOCK_SHARD_COUNT;
        if (!lookup_shard_selected[shard_index]) {
            lookup_shard_selected[shard_index] = true;
            parallel_paths[selected_path_count] = candidate_path;
            ++selected_path_count;
        }
    }
    const bool parallel_ready = concurrency_consistent &&
                                selected_path_count == OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT &&
                                namespace_cache.EvictDentries(UINT64_MAX, evicted_count) ==
                                    os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    backend_lookup_count.store(OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE,
                               std::memory_order_relaxed);
    peak_backend_lookup_count.store(OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE,
                                    std::memory_order_relaxed);
    backend_lookup_delay_enabled.store(true, std::memory_order_release);
    for (uint64_t thread_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
         thread_index < OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT; ++thread_index) {
        concurrent_results[thread_index] = false;
        workers[thread_index] = std::thread([&vfs, &context, &concurrent_results, &parallel_paths,
                                             thread_index]() {
            os::kernel::fs::Path thread_path{};
            concurrent_results[thread_index] =
                vfs.Resolve(context,
                            reinterpret_cast<const uint8_t *>(parallel_paths[thread_index].data()),
                            parallel_paths[thread_index].size(),
                            thread_path) == os::kernel::fs::Status::NotFound;
        });
    }
    bool parallel_consistent = parallel_ready;
    for (uint64_t thread_index = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
         thread_index < OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT; ++thread_index) {
        workers[thread_index].join();
        parallel_consistent = parallel_consistent && concurrent_results[thread_index];
    }
    backend_lookup_delay_enabled.store(false, std::memory_order_release);
    parallel_consistent = parallel_consistent &&
                          backend_lookup_count.load(std::memory_order_relaxed) ==
                              OS_TEST_VFS_DENTRY_PRODUCTION_THREAD_COUNT &&
                          peak_backend_lookup_count.load(std::memory_order_relaxed) >
                              OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
                          vfs.ReadStatistics().peak_resolution_context_count >
                              OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP;
    test_context.Expect(parallel_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_PARALLEL_MESSAGE);

    bool sequence_result = false;
    const uint64_t retry_count_before_sequence = vfs.ReadStatistics().namespace_retry_count;
    bool sequence_ready =
        parallel_consistent && namespace_cache.EvictDentries(UINT64_MAX, evicted_count) ==
                                   os::kernel::fs::VfsNamespaceCacheStatus::Succeeded;
    backend_lookup_count.store(OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE,
                               std::memory_order_relaxed);
    backend_lookup_delay_enabled.store(true, std::memory_order_release);
    std::thread sequence_worker{[&vfs, &context, &parallel_paths, &sequence_result]() {
        os::kernel::fs::Path thread_path{};
        sequence_result =
            vfs.Resolve(context, reinterpret_cast<const uint8_t *>(parallel_paths[0ULL].data()),
                        parallel_paths[0ULL].size(),
                        thread_path) == os::kernel::fs::Status::NotFound;
    }};
    uint64_t wait_iteration = OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE;
    while (active_backend_lookup_count.load(std::memory_order_acquire) ==
               OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE &&
           wait_iteration < 1000000ULL) {
        ++wait_iteration;
        std::this_thread::yield();
    }
    sequence_ready =
        sequence_ready && wait_iteration < 1000000ULL &&
        vfs.CreateDirectory(context, reinterpret_cast<const uint8_t *>(parallel_paths[1ULL].data()),
                            parallel_paths[1ULL].size()) == os::kernel::fs::Status::Succeeded;
    sequence_worker.join();
    backend_lookup_delay_enabled.store(false, std::memory_order_release);
    const bool sequence_consistent =
        sequence_ready && sequence_result &&
        backend_lookup_count.load(std::memory_order_relaxed) >= 2ULL &&
        vfs.ReadStatistics().namespace_retry_count > retry_count_before_sequence &&
        vfs.RemoveDirectory(context, reinterpret_cast<const uint8_t *>(parallel_paths[1ULL].data()),
                            parallel_paths[1ULL].size()) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(sequence_consistent, OS_TEST_VFS_DENTRY_PRODUCTION_SEQUENCE_MESSAGE);

    os::kernel::fs::NamespaceCacheReclaimResult reclaim_result{};
    const uint64_t lookup_count_before_reclaim = backend_lookup_count;
    const bool reclaim_consistent =
        sequence_consistent &&
        vfs.ReclaimNamespaceCache(UINT64_MAX, UINT64_MAX, reclaim_result) ==
            os::kernel::fs::Status::Succeeded &&
        reclaim_result.dentry_count != OS_TEST_VFS_DENTRY_PRODUCTION_EMPTY_VALUE &&
        reclaim_result.hash_tier_shrunk &&
        reclaim_result.released_page_count == OS_TEST_VFS_DENTRY_PRODUCTION_BACKING_PAGE_COUNT &&
        backing_release.released &&
        vfs.ReadStatistics().namespace_reclaim_operation_count ==
            OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
        vfs.ReadStatistics().reclaimed_dentry_count == reclaim_result.dentry_count &&
        vfs.ReadStatistics().namespace_hash_shrink_count ==
            OS_TEST_VFS_DENTRY_PRODUCTION_ONE_LOOKUP &&
        vfs.ReadStatistics().released_namespace_page_count ==
            OS_TEST_VFS_DENTRY_PRODUCTION_BACKING_PAGE_COUNT &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_MISSING_PATH),
                    resolved) == os::kernel::fs::Status::Succeeded &&
        backend_lookup_count > lookup_count_before_reclaim &&
        namespace_cache.Statistics().dentry_hash_bucket_capacity ==
            OS_TEST_VFS_DENTRY_PRODUCTION_COMPACT_HASH_BUCKET_CAPACITY &&
        namespace_cache.Statistics().inode_hash_bucket_capacity ==
            OS_TEST_VFS_DENTRY_PRODUCTION_COMPACT_HASH_BUCKET_CAPACITY &&
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
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_DIRECTORY_PATH),
                    directory_path) == os::kernel::fs::Status::Succeeded &&
        vfs.Resolve(context, OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_PATH,
                    sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_PATH),
                    resolved) == os::kernel::fs::Status::NotFound &&
        os::kernel::fs::BuildVfsDentryKey(
            directory_path.mount_identifier,
            os::kernel::fs::VfsInodeIdentity{
                .superblock_identifier = directory_path.vnode.superblock->identifier,
                .superblock_generation = directory_path.vnode.superblock->generation,
                .node_identifier = directory_path.vnode.identifier,
                .node_generation = directory_path.vnode.generation,
            },
            OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_NAME,
            sizeof(OS_TEST_VFS_DENTRY_PRODUCTION_CHILD_NAME),
            child_key) == os::kernel::fs::VfsNamespaceCacheStatus::Succeeded &&
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
