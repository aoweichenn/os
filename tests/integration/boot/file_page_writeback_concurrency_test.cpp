#include <os/kernel/memory/file_page_cache.hpp>
#include <os/kernel/process/file_page_writeback.hpp>
#include <test_context.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SUITE_NAME =
    "kernel/file_page_writeback_concurrency/integration";
constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SUCCESS_MESSAGE =
    "同页重新脏化必须等待 Writeback，其他 Clean 页仍可被 reclaim";
constexpr std::string_view OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_FAILURE_MESSAGE =
    "设备写失败必须原样广播给同页 waiter，Error 页可显式重试并恢复";
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_MANAGED_PAGE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_STATE_VALUES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_STATE_STORAGE_SIZE_BYTES =
    (OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_MANAGED_PAGE_COUNT +
     OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_STATE_VALUES_PER_BYTE - 1ULL) /
    OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_STATE_VALUES_PER_BYTE;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_MEMORY_SIZE_BYTES =
    OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_MANAGED_PAGE_COUNT *
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_METADATA_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_CACHE_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_DIRTY_LIMIT = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_THREAD_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_OWNER_THREAD_INDEX = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_WAITER_THREAD_INDEX = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_CONTROL_THREAD_INDEX = 2ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SINGLE_VALUE = 1ULL;
constexpr uint64_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_FILE_SIZE_BYTES =
    2ULL * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint8_t OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_PATTERN = 0xC7U;
constexpr std::chrono::seconds OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_TIMEOUT{5};

thread_local uint64_t current_thread_index = UINT64_MAX;

struct TestMemory final {
    uint8_t bytes[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_MEMORY_SIZE_BYTES];
};

struct WritebackScenario final {
    os::kernel::FilePageWritebackSlot
        writeback_slots[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_THREAD_CAPACITY];
    os::kernel::FilePageWritebackWaiter
        waiters[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_THREAD_CAPACITY];
    os::kernel::FilePageWritebackCoordinator coordinator;
    std::mutex coordinator_mutex;
    std::condition_variable completion_changed;
    std::mutex state_mutex;
    std::condition_variable state_changed;
    uint64_t writer_call_count;
    uint64_t completion_wake_count;
    uint64_t waiter_prepared_count;
    bool writer_entered;
    bool writer_may_finish;
    bool completed;
    bool writer_succeeds;
};

[[nodiscard]] os::kernel::FilePageIdentity TestIdentity(const uint64_t page_index) noexcept {
    return os::kernel::FilePageIdentity{
        .file =
            os::kernel::FileIdentity{
                .superblock_identifier = 37ULL,
                .superblock_generation = 5ULL,
                .node_identifier = 41ULL,
                .node_generation = 7ULL,
            },
        .page_index = page_index,
    };
}

[[nodiscard]] uint8_t *AccessPage(void *const context, const uint64_t physical_address) noexcept {
    if (context == nullptr ||
        physical_address > OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_MEMORY_SIZE_BYTES -
                               os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return nullptr;
    }
    auto &memory = *static_cast<TestMemory *>(context);
    return memory.bytes + physical_address;
}

[[nodiscard]] bool ReadPage(void *const context, const os::kernel::FilePageIdentity &identity,
                            uint8_t *const destination, const uint64_t capacity_bytes) noexcept {
    static_cast<void>(identity);
    if (context == nullptr || destination == nullptr ||
        capacity_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    destination[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE] =
        OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_PATTERN;
    return true;
}

[[nodiscard]] bool WritebackAvailable(void *const context) noexcept {
    return context != nullptr &&
           current_thread_index < OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_THREAD_CAPACITY;
}

[[nodiscard]] bool BeginWriteback(void *const context, const os::kernel::FilePageIdentity &identity,
                                  const uint64_t physical_address,
                                  const uint64_t writeback_generation,
                                  os::kernel::FilePageWritebackToken &token) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto &scenario = *static_cast<WritebackScenario *>(context);
    std::lock_guard guard{scenario.coordinator_mutex};
    return scenario.coordinator.Begin(identity, physical_address, writeback_generation,
                                      current_thread_index,
                                      token) == os::kernel::FilePageWritebackStatus::Succeeded;
}

[[nodiscard]] bool RegisterWritebackWaiter(void *const context,
                                           const os::kernel::FilePageIdentity &identity,
                                           const uint64_t physical_address,
                                           const uint64_t writeback_generation,
                                           os::kernel::FilePageWritebackToken &token) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto &scenario = *static_cast<WritebackScenario *>(context);
    std::lock_guard guard{scenario.coordinator_mutex};
    return scenario.coordinator.RegisterWaiter(identity, physical_address, writeback_generation,
                                               current_thread_index, token) ==
           os::kernel::FilePageWritebackStatus::Succeeded;
}

[[nodiscard]] bool WaitForWriteback(void *const context,
                                    const os::kernel::FilePageWritebackToken token,
                                    os::kernel::FilePageCacheStatus &result) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto &scenario = *static_cast<WritebackScenario *>(context);
    std::unique_lock coordinator_guard{scenario.coordinator_mutex};
    bool wait_required = false;
    if (scenario.coordinator.PrepareWait(token, current_thread_index, wait_required) !=
            os::kernel::FilePageWritebackStatus::Succeeded ||
        !wait_required) {
        return false;
    }
    {
        std::lock_guard state_guard{scenario.state_mutex};
        ++scenario.waiter_prepared_count;
    }
    scenario.state_changed.notify_all();
    scenario.completion_changed.wait(coordinator_guard,
                                     [&scenario]() { return scenario.completed; });
    return scenario.coordinator.TakeResult(token, current_thread_index, result) ==
           os::kernel::FilePageWritebackStatus::Succeeded;
}

[[nodiscard]] bool CompleteWriteback(void *const context,
                                     const os::kernel::FilePageWritebackToken token,
                                     const os::kernel::FilePageCacheStatus result) noexcept {
    if (context == nullptr) {
        return false;
    }
    auto &scenario = *static_cast<WritebackScenario *>(context);
    std::unique_lock coordinator_guard{scenario.coordinator_mutex};
    os::kernel::FilePageWritebackCompletionDecision decision{};
    if (scenario.coordinator.Complete(token, current_thread_index, result, decision) !=
        os::kernel::FilePageWritebackStatus::Succeeded) {
        return false;
    }
    scenario.completion_wake_count += decision.wake_count;
    scenario.completed = true;
    coordinator_guard.unlock();
    scenario.completion_changed.notify_all();
    return true;
}

[[nodiscard]] bool BlockingWritePage(void *const context,
                                     const os::kernel::FilePageIdentity &identity,
                                     const uint8_t *const source,
                                     const uint64_t length_bytes) noexcept {
    static_cast<void>(identity);
    if (context == nullptr || source == nullptr ||
        length_bytes != os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    auto &scenario = *static_cast<WritebackScenario *>(context);
    std::unique_lock state_guard{scenario.state_mutex};
    ++scenario.writer_call_count;
    scenario.writer_entered = true;
    scenario.state_changed.notify_all();
    scenario.state_changed.wait(state_guard, [&scenario]() { return scenario.writer_may_finish; });
    return scenario.writer_succeeds;
}

[[nodiscard]] bool ImmediateWritePage(void *const context,
                                      const os::kernel::FilePageIdentity &identity,
                                      const uint8_t *const source,
                                      const uint64_t length_bytes) noexcept {
    static_cast<void>(identity);
    return context != nullptr && source != nullptr &&
           length_bytes == os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

[[nodiscard]] bool SelectReclaimPage(void *const context,
                                     const os::kernel::FilePageCacheEntry &entry,
                                     bool &selected) noexcept {
    static_cast<void>(entry);
    selected = context != nullptr;
    return selected;
}

[[nodiscard]] bool CompleteReclaimPage(void *const context,
                                       const os::kernel::FilePageCacheEntry &entry) noexcept {
    static_cast<void>(entry);
    return context != nullptr;
}

[[nodiscard]] bool WaitForFlag(WritebackScenario &scenario, bool WritebackScenario::*const flag) {
    std::unique_lock guard{scenario.state_mutex};
    return scenario.state_changed.wait_for(guard, OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_TIMEOUT,
                                           [&scenario, flag]() { return scenario.*flag; });
}

[[nodiscard]] bool WaitForWaiterCount(WritebackScenario &scenario, const uint64_t waiter_count) {
    std::unique_lock guard{scenario.state_mutex};
    return scenario.state_changed.wait_for(
        guard, OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_TIMEOUT,
        [&scenario, waiter_count]() { return scenario.waiter_prepared_count == waiter_count; });
}

[[nodiscard]] bool RunScenario(const bool writer_succeeds) {
    uint8_t state_storage[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_STATE_STORAGE_SIZE_BYTES]{};
    TestMemory memory{};
    os::kernel::PhysicalFrameAllocator frame_allocator{
        state_storage,
        OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry memory_map[]{
        {
            .base_address = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE,
            .length_bytes = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_MEMORY_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    if (frame_allocator.Initialize(memory_map, OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SINGLE_VALUE,
                                   OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_MEMORY_SIZE_BYTES) !=
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    const os::kernel::PhysicalFrameAllocatorStatistics frames_before = frame_allocator.Statistics();
    alignas(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_ALIGNMENT_BYTES)
        uint8_t metadata_storage[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_METADATA_SIZE_BYTES]{};
    os::kernel::KernelHeap metadata_heap{};
    os::kernel::FilePageCache cache{};
    WritebackScenario scenario{};
    scenario.writer_succeeds = writer_succeeds;
    if (metadata_heap.Initialize(reinterpret_cast<uint64_t>(metadata_storage),
                                 sizeof(metadata_storage)) !=
            os::kernel::KernelHeapStatus::Succeeded ||
        scenario.coordinator.Initialize(
            scenario.writeback_slots, OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_THREAD_CAPACITY,
            scenario.waiters, OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_THREAD_CAPACITY) !=
            os::kernel::FilePageWritebackStatus::Succeeded ||
        cache.Initialize(metadata_heap, OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_CACHE_CAPACITY,
                         OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_DIRTY_LIMIT, frame_allocator,
                         &memory, AccessPage) != os::kernel::FilePageCacheStatus::Succeeded) {
        return false;
    }
    uint64_t physical_addresses[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_CACHE_CAPACITY]{};
    for (uint64_t page_index = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE;
         page_index < OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_CACHE_CAPACITY; ++page_index) {
        bool cache_hit = true;
        if (cache.Acquire(TestIdentity(page_index), &scenario, ReadPage,
                          physical_addresses[page_index],
                          cache_hit) != os::kernel::FilePageCacheStatus::Succeeded ||
            cache_hit ||
            cache.Release(TestIdentity(page_index), physical_addresses[page_index]) !=
                os::kernel::FilePageCacheStatus::Succeeded) {
            return false;
        }
    }
    if (cache.ObserveFileSize(
            TestIdentity(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE).file,
            OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_FILE_SIZE_BYTES) !=
            os::kernel::FilePageCacheStatus::Succeeded ||
        cache.MarkDirty(TestIdentity(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE),
                        physical_addresses[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE]) !=
            os::kernel::FilePageCacheStatus::Succeeded ||
        cache.ConfigureWritebackWait(os::kernel::FilePageWritebackWaitOperations{
            .context = &scenario,
            .owner_available = WritebackAvailable,
            .available = WritebackAvailable,
            .begin = BeginWriteback,
            .register_waiter = RegisterWritebackWaiter,
            .wait = WaitForWriteback,
            .complete = CompleteWriteback,
        }) != os::kernel::FilePageCacheStatus::Succeeded) {
        return false;
    }

    os::kernel::FilePageCacheStatus owner_status = os::kernel::FilePageCacheStatus::Corrupt;
    os::kernel::FilePageCacheStatus reader_status = os::kernel::FilePageCacheStatus::Corrupt;
    os::kernel::FilePageCacheStatus synchronization_status =
        os::kernel::FilePageCacheStatus::Corrupt;
    os::kernel::FilePageCacheStatus waiter_status = os::kernel::FilePageCacheStatus::Corrupt;
    uint64_t owner_written_page_count = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE;
    uint64_t synchronization_written_page_count =
        OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE;
    std::thread owner([&]() {
        current_thread_index = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_OWNER_THREAD_INDEX;
        owner_status = cache.Writeback(&scenario, BlockingWritePage,
                                       OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SINGLE_VALUE,
                                       owner_written_page_count);
    });
    if (!WaitForFlag(scenario, &WritebackScenario::writer_entered)) {
        {
            std::lock_guard state_guard{scenario.state_mutex};
            scenario.writer_may_finish = true;
        }
        scenario.state_changed.notify_all();
        owner.join();
        return false;
    }
    bool reader_cache_hit = false;
    uint64_t reader_physical_address = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE;
    std::thread reader([&]() {
        current_thread_index = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_CONTROL_THREAD_INDEX;
        reader_status =
            cache.Acquire(TestIdentity(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE),
                          &scenario, ReadPage, reader_physical_address, reader_cache_hit);
        if (reader_status == os::kernel::FilePageCacheStatus::Succeeded) {
            reader_status =
                cache.Release(TestIdentity(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE),
                              reader_physical_address);
        }
    });
    reader.join();
    if (reader_status != os::kernel::FilePageCacheStatus::Succeeded || !reader_cache_hit ||
        reader_physical_address !=
            physical_addresses[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE]) {
        {
            std::lock_guard state_guard{scenario.state_mutex};
            scenario.writer_may_finish = true;
        }
        scenario.state_changed.notify_all();
        owner.join();
        return false;
    }
    std::thread waiter([&]() {
        current_thread_index = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_WAITER_THREAD_INDEX;
        waiter_status = cache.MarkDirty(
            TestIdentity(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE),
            physical_addresses[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE]);
    });
    std::thread synchronization_waiter([&]() {
        current_thread_index = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_CONTROL_THREAD_INDEX;
        synchronization_status = cache.WritebackFile(
            TestIdentity(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE).file,
            OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE,
            OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE, &scenario, ImmediateWritePage,
            UINT64_MAX, synchronization_written_page_count);
    });
    if (!WaitForWaiterCount(scenario, 2ULL)) {
        {
            std::lock_guard state_guard{scenario.state_mutex};
            scenario.writer_may_finish = true;
        }
        scenario.state_changed.notify_all();
        owner.join();
        waiter.join();
        synchronization_waiter.join();
        return false;
    }
    current_thread_index = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_CONTROL_THREAD_INDEX;
    uint64_t reclaimed_page_count = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE;
    const os::kernel::FilePageCacheStatus reclaim_status =
        cache.ReclaimCleanPages(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SINGLE_VALUE, &scenario,
                                SelectReclaimPage, CompleteReclaimPage, reclaimed_page_count);
    {
        std::lock_guard state_guard{scenario.state_mutex};
        scenario.writer_may_finish = true;
    }
    scenario.state_changed.notify_all();
    owner.join();
    waiter.join();
    synchronization_waiter.join();

    const os::kernel::FilePageCacheStatus expected_owner_status =
        writer_succeeds ? os::kernel::FilePageCacheStatus::Succeeded
                        : os::kernel::FilePageCacheStatus::SourceWriteFailed;
    const os::kernel::FilePageCacheStatus expected_waiter_status = expected_owner_status;
    if (owner_status != expected_owner_status || waiter_status != expected_waiter_status ||
        synchronization_status != expected_owner_status ||
        owner_written_page_count != (writer_succeeds ? 1ULL : 0ULL) ||
        synchronization_written_page_count > (writer_succeeds ? 1ULL : 0ULL) ||
        reclaim_status != os::kernel::FilePageCacheStatus::Succeeded ||
        reclaimed_page_count != OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SINGLE_VALUE ||
        scenario.completion_wake_count < 2ULL) {
        return false;
    }
    if (cache.MarkDirty(TestIdentity(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE),
                        physical_addresses[OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE]) !=
        os::kernel::FilePageCacheStatus::Succeeded) {
        return false;
    }
    uint64_t retry_written_page_count = OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE;
    os::kernel::FilePageCacheEntry entry{};
    const os::kernel::FilePageCacheStatus retry_status = cache.Writeback(
        &scenario, ImmediateWritePage, OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SINGLE_VALUE,
        retry_written_page_count);
    const os::kernel::FilePageCacheStatistics cache_statistics_before_destroy = cache.Statistics();
    const os::kernel::FilePageWritebackStatistics writeback_statistics =
        scenario.coordinator.Statistics();
    const bool consistent =
        retry_status == os::kernel::FilePageCacheStatus::Succeeded &&
        retry_written_page_count == OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SINGLE_VALUE &&
        cache.ReadEntry(TestIdentity(OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE), entry) ==
            os::kernel::FilePageCacheStatus::Succeeded &&
        entry.state == os::kernel::FilePageCacheEntryState::Clean &&
        cache.Validate() == os::kernel::FilePageCacheStatus::Succeeded &&
        scenario.coordinator.Validate() == os::kernel::FilePageWritebackStatus::Succeeded &&
        writeback_statistics.active_writeback_count ==
            OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_EMPTY_VALUE &&
        writeback_statistics.begin_count >= 2ULL &&
        writeback_statistics.begin_count == writeback_statistics.completion_count &&
        writeback_statistics.waiter_registration_count == 2ULL &&
        writeback_statistics.wait_commit_count == 2ULL &&
        writeback_statistics.broadcast_wake_count >= 2ULL &&
        writeback_statistics.failure_broadcast_count == (writer_succeeds ? 0ULL : 1ULL) &&
        writeback_statistics.result_take_count == 2ULL &&
        cache_statistics_before_destroy.writeback_collision_count == 2ULL &&
        cache_statistics_before_destroy.writeback_wait_count == 2ULL &&
        cache.Destroy() == os::kernel::FilePageCacheStatus::Succeeded &&
        frame_allocator.Statistics().allocated_frame_count == frames_before.allocated_frame_count;
    return consistent;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SUITE_NAME};
    test_context.Expect(RunScenario(true), OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_SUCCESS_MESSAGE);
    test_context.Expect(RunScenario(false),
                        OS_TEST_FILE_PAGE_WRITEBACK_CONCURRENCY_FAILURE_MESSAGE);
    return test_context.ExitCode();
}
