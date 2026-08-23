#include <os/kernel/memory/file_page_cache.hpp>
#include <os/kernel/process/file_page_load.hpp>
#include <test_context.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SUITE_NAME =
    "kernel/file_page_cache_loading_merge/integration";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SUCCESS_MESSAGE =
    "同页并发 miss 必须只读取一次并向 waiter 交付同一物理页";
constexpr std::string_view OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_FAILURE_MESSAGE =
    "同页来源失败必须只读取一次并向 owner 与 waiter 交付同一错误";
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_MANAGED_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_STATE_VALUES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_STATE_STORAGE_SIZE_BYTES =
    (OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_MANAGED_PAGE_COUNT +
     OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_STATE_VALUES_PER_BYTE - 1ULL) /
    OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_STATE_VALUES_PER_BYTE;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_MEMORY_SIZE_BYTES =
    OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_MANAGED_PAGE_COUNT *
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_METADATA_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_CACHE_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_DIRTY_LIMIT = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_COORDINATOR_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_THREAD_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_OWNER_THREAD_INDEX = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_WAITER_THREAD_INDEX = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE = 1ULL;
constexpr uint8_t OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_PATTERN = 0xA7U;
constexpr std::chrono::seconds OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_TIMEOUT{5};

thread_local uint64_t current_thread_index = UINT64_MAX;

struct TestMemory final {
    uint8_t bytes[OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_MEMORY_SIZE_BYTES];
};

struct LoadingMergeScenario final {
    os::kernel::FilePageLoadSlot
        load_slots[OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_COORDINATOR_CAPACITY];
    os::kernel::FilePageLoadWaiter waiters[OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_THREAD_CAPACITY];
    os::kernel::FilePageLoadCoordinator coordinator;
    std::mutex coordinator_mutex;
    std::condition_variable completion_changed;
    std::mutex source_mutex;
    std::condition_variable source_changed;
    uint64_t source_read_count;
    uint64_t completion_wake_count;
    bool source_entered;
    bool source_may_finish;
    bool waiter_prepared;
    bool waiter_completion_observed;
    bool waiter_may_take_result;
    bool completed;
    bool source_succeeds;
};

[[nodiscard]] os::kernel::FilePageIdentity TestIdentity() noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            os::kernel::FileIdentity{
                .superblock_identifier = 31ULL,
                .superblock_generation = 7ULL,
                .node_identifier = 47ULL,
                .node_generation = 11ULL,
            },
        .page_index = 3ULL,
    };
}

[[nodiscard]] uint8_t *AccessPage(void *const context, const uint64_t physical_address) noexcept {
    if (context == nullptr ||
        physical_address > OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_MEMORY_SIZE_BYTES -
                               os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return nullptr;
    }
    auto &memory = *static_cast<TestMemory *>(context);
    return memory.bytes + physical_address;
}

[[nodiscard]] bool LoadingWaitAvailable(void *const context) noexcept { return context != nullptr; }

[[nodiscard]] bool BeginLoading(void *const context, const os::kernel::FilePageIdentity &identity,
                                const uint64_t physical_address, const uint64_t load_generation,
                                os::kernel::FilePageLoadToken &token) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto &scenario = *static_cast<LoadingMergeScenario *>(context);
    std::lock_guard guard{scenario.coordinator_mutex};
    return scenario.coordinator.Begin(identity, physical_address, load_generation,
                                      current_thread_index,
                                      token) == os::kernel::FilePageLoadStatus::Succeeded;
}

[[nodiscard]] bool RegisterLoadingWaiter(void *const context,
                                         const os::kernel::FilePageIdentity &identity,
                                         const uint64_t physical_address,
                                         const uint64_t load_generation,
                                         os::kernel::FilePageLoadToken &token) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto &scenario = *static_cast<LoadingMergeScenario *>(context);
    std::lock_guard guard{scenario.coordinator_mutex};
    return scenario.coordinator.RegisterWaiter(identity, physical_address, load_generation,
                                               current_thread_index,
                                               token) == os::kernel::FilePageLoadStatus::Succeeded;
}

[[nodiscard]] bool WaitForLoading(void *const context, const os::kernel::FilePageLoadToken token,
                                  os::kernel::FilePageCacheStatus &result) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto &scenario = *static_cast<LoadingMergeScenario *>(context);
    std::unique_lock coordinator_guard{scenario.coordinator_mutex};
    bool wait_required = false;
    if (scenario.coordinator.PrepareWait(token, current_thread_index, wait_required) !=
            os::kernel::FilePageLoadStatus::Succeeded ||
        !wait_required) {
        return false;
    }
    {
        std::lock_guard source_guard{scenario.source_mutex};
        scenario.waiter_prepared = true;
    }
    scenario.source_changed.notify_all();
    scenario.completion_changed.wait(coordinator_guard,
                                     [&scenario]() { return scenario.completed; });
    coordinator_guard.unlock();
    {
        std::unique_lock source_guard{scenario.source_mutex};
        scenario.waiter_completion_observed = true;
        scenario.source_changed.notify_all();
        scenario.source_changed.wait(source_guard,
                                     [&scenario]() { return scenario.waiter_may_take_result; });
    }
    coordinator_guard.lock();
    return scenario.coordinator.TakeResult(token, current_thread_index, result) ==
           os::kernel::FilePageLoadStatus::Succeeded;
}

[[nodiscard]] bool CompleteLoading(void *const context, const os::kernel::FilePageLoadToken token,
                                   const os::kernel::FilePageCacheStatus result) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto &scenario = *static_cast<LoadingMergeScenario *>(context);
    std::unique_lock coordinator_guard{scenario.coordinator_mutex};
    os::kernel::FilePageLoadCompletionDecision decision{};
    if (scenario.coordinator.Complete(token, current_thread_index, result, decision) !=
        os::kernel::FilePageLoadStatus::Succeeded) {
        return false;
    }
    scenario.completion_wake_count = decision.wake_count;
    scenario.completed = true;
    coordinator_guard.unlock();
    scenario.completion_changed.notify_all();
    return true;
}

[[nodiscard]] bool ReadLoadingWaiterCount(void *const context,
                                          const os::kernel::FilePageLoadToken token,
                                          uint64_t &waiter_count) noexcept {
    if (context == nullptr) {
        waiter_count = OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE;
        return false;
    }
    auto &scenario = *static_cast<LoadingMergeScenario *>(context);
    std::lock_guard guard{scenario.coordinator_mutex};
    return scenario.coordinator.RegisteredWaiterCount(token, current_thread_index, waiter_count) ==
           os::kernel::FilePageLoadStatus::Succeeded;
}

[[nodiscard]] bool ReadPage(void *const context, const os::kernel::FilePageIdentity &identity,
                            uint8_t *const destination, const uint64_t capacity_bytes) noexcept {
    static_cast<void>(identity);
    if (context == nullptr || destination == nullptr ||
        capacity_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    auto &scenario = *static_cast<LoadingMergeScenario *>(context);
    std::unique_lock source_guard{scenario.source_mutex};
    ++scenario.source_read_count;
    scenario.source_entered = true;
    scenario.source_changed.notify_all();
    scenario.source_changed.wait(source_guard,
                                 [&scenario]() { return scenario.source_may_finish; });
    if (!scenario.source_succeeds) {
        return false;
    }
    destination[OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE] =
        OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_PATTERN;
    return true;
}

[[nodiscard]] bool WaitForFlag(LoadingMergeScenario &scenario,
                               bool LoadingMergeScenario::*const flag) {
    std::unique_lock guard{scenario.source_mutex};
    return scenario.source_changed.wait_for(guard, OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_TIMEOUT,
                                            [&scenario, flag]() { return scenario.*flag; });
}

[[nodiscard]] bool RunScenario(const bool source_succeeds) {
    uint8_t state_storage[OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_STATE_STORAGE_SIZE_BYTES]{};
    TestMemory memory{};
    os::kernel::PhysicalFrameAllocator frame_allocator{
        state_storage,
        OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[]{
        {
            .base_address = OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE,
            .length_bytes = OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_MEMORY_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    if (frame_allocator.Initialize(memory_map, 1ULL,
                                   OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_MEMORY_SIZE_BYTES) !=
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before = frame_allocator.Statistics();
    alignas(OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_ALIGNMENT_BYTES)
        uint8_t metadata_storage[OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_METADATA_SIZE_BYTES]{};
    os::kernel::KernelHeap metadata_heap{};
    os::kernel::FilePageCache cache{};
    LoadingMergeScenario scenario{};
    scenario.source_succeeds = source_succeeds;
    if (metadata_heap.Initialize(reinterpret_cast<uint64_t>(metadata_storage),
                                 sizeof(metadata_storage)) !=
            os::kernel::KernelHeapStatus::Succeeded ||
        scenario.coordinator.Initialize(
            scenario.load_slots, OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_COORDINATOR_CAPACITY,
            scenario.waiters, OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_THREAD_CAPACITY) !=
            os::kernel::FilePageLoadStatus::Succeeded ||
        cache.Initialize(metadata_heap, OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_CACHE_CAPACITY,
                         OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_DIRTY_LIMIT, frame_allocator,
                         &memory, AccessPage) != os::kernel::FilePageCacheStatus::Succeeded ||
        cache.ConfigureLoadingWait(os::kernel::FilePageLoadWaitOperations{
            .context = &scenario,
            .owner_available = LoadingWaitAvailable,
            .available = LoadingWaitAvailable,
            .begin = BeginLoading,
            .register_waiter = RegisterLoadingWaiter,
            .wait = WaitForLoading,
            .waiter_count = ReadLoadingWaiterCount,
            .complete = CompleteLoading,
        }) != os::kernel::FilePageCacheStatus::Succeeded) {
        return false;
    }

    const os::kernel::FilePageIdentity identity = TestIdentity();
    os::kernel::FilePageCacheStatus owner_status = os::kernel::FilePageCacheStatus::Corrupt;
    os::kernel::FilePageCacheStatus waiter_status = os::kernel::FilePageCacheStatus::Corrupt;
    uint64_t owner_physical_address = OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE;
    uint64_t waiter_physical_address = OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE;
    bool owner_cache_hit = true;
    bool waiter_cache_hit = false;
    os::kernel::FilePageCacheStatus owner_release_status = os::kernel::FilePageCacheStatus::Corrupt;
    std::thread owner([&]() {
        current_thread_index = OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_OWNER_THREAD_INDEX;
        owner_status =
            cache.Acquire(identity, &scenario, ReadPage, owner_physical_address, owner_cache_hit);
        if (owner_status == os::kernel::FilePageCacheStatus::Succeeded) {
            owner_release_status = cache.Release(identity, owner_physical_address);
        }
    });
    const bool owner_entered = WaitForFlag(scenario, &LoadingMergeScenario::source_entered);
    std::thread waiter([&]() {
        current_thread_index = OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_WAITER_THREAD_INDEX;
        waiter_status =
            cache.Acquire(identity, &scenario, ReadPage, waiter_physical_address, waiter_cache_hit);
    });
    const bool waiter_prepared = WaitForFlag(scenario, &LoadingMergeScenario::waiter_prepared);
    {
        std::lock_guard guard{scenario.source_mutex};
        scenario.source_may_finish = true;
    }
    scenario.source_changed.notify_all();
    const bool waiter_observed_completion =
        WaitForFlag(scenario, &LoadingMergeScenario::waiter_completion_observed);
    owner.join();
    const bool waiter_reference_prevented_invalidation =
        !source_succeeds ||
        cache.Invalidate(identity.file) == os::kernel::FilePageCacheStatus::EntryBusy;
    {
        std::lock_guard guard{scenario.source_mutex};
        scenario.waiter_may_take_result = true;
    }
    scenario.source_changed.notify_all();
    waiter.join();

    const os::kernel::FilePageLoadStatistics load_statistics = scenario.coordinator.Statistics();
    bool consistent =
        owner_entered && waiter_prepared && waiter_observed_completion &&
        waiter_reference_prevented_invalidation &&
        scenario.source_read_count == OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        scenario.completion_wake_count == OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        cache.Statistics().loading_collision_count ==
            OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        load_statistics.active_load_count == OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE &&
        load_statistics.begin_count == OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        load_statistics.waiter_registration_count ==
            OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        load_statistics.wait_commit_count == OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        load_statistics.completion_count == OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        load_statistics.broadcast_wake_count ==
            OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        load_statistics.result_take_count == OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
        scenario.coordinator.Validate() == os::kernel::FilePageLoadStatus::Succeeded;
    if (source_succeeds) {
        consistent =
            consistent && owner_status == os::kernel::FilePageCacheStatus::Succeeded &&
            waiter_status == os::kernel::FilePageCacheStatus::Succeeded &&
            owner_release_status == os::kernel::FilePageCacheStatus::Succeeded &&
            !owner_cache_hit && waiter_cache_hit &&
            owner_physical_address == waiter_physical_address &&
            memory.bytes[owner_physical_address] == OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_PATTERN &&
            cache.Release(identity, waiter_physical_address) ==
                os::kernel::FilePageCacheStatus::Succeeded;
    } else {
        consistent = consistent &&
                     owner_status == os::kernel::FilePageCacheStatus::SourceReadFailed &&
                     waiter_status == os::kernel::FilePageCacheStatus::SourceReadFailed &&
                     load_statistics.failure_broadcast_count ==
                         OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SINGLE_VALUE &&
                     cache.Statistics().resident_page_count ==
                         OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE;
    }
    return consistent && cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded &&
           cache.Destroy() == os::kernel::FilePageCacheStatus::Succeeded &&
           metadata_heap.Statistics().allocation_count ==
               OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_EMPTY_VALUE &&
           frame_allocator.Statistics().allocated_frame_count ==
               frames_before.allocated_frame_count;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SUITE_NAME};
    test_context.Expect(RunScenario(true), OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_SUCCESS_MESSAGE);
    test_context.Expect(RunScenario(false), OS_TEST_FILE_PAGE_CACHE_LOADING_MERGE_FAILURE_MESSAGE);
    return test_context.ExitCode();
}
