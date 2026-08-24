#include <os/kernel/fs/inode_io_coordinator.hpp>
#include <test_context.hpp>

#include <atomic>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_INODE_IO_COORDINATOR_SUITE_NAME = "kernel/inode_io_coordinator";
constexpr std::string_view OS_TEST_INODE_IO_COORDINATOR_REJECTION_MESSAGE =
    "协调器必须拒绝非法存储、容量、wait queue 范围、identity 与过期 token";
constexpr std::string_view OS_TEST_INODE_IO_COORDINATOR_CAPACITY_MESSAGE =
    "全部槽被引用时必须明确拒绝第三个 inode，释放后按 LRU 复用";
constexpr std::string_view OS_TEST_INODE_IO_COORDINATOR_SERIALIZATION_MESSAGE =
    "同 inode guard 必须串行进入临界区";
constexpr std::string_view OS_TEST_INODE_IO_COORDINATOR_PARALLELISM_MESSAGE =
    "不同 inode guard 必须能同时进入各自临界区";
constexpr std::string_view OS_TEST_INODE_IO_COORDINATOR_LIFECYCLE_MESSAGE =
    "释放后引用、统计和内部不变量必须守恒";
constexpr uint64_t OS_TEST_INODE_IO_COORDINATOR_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_INODE_IO_COORDINATOR_WAIT_QUEUE_BASE = 0x7100ULL;
constexpr uint64_t OS_TEST_INODE_IO_COORDINATOR_SPIN_LIMIT = 10000000ULL;

[[nodiscard]] os::kernel::fs::InodeIoIdentity
MakeIdentity(const uint64_t node_identifier) noexcept {
    return os::kernel::fs::InodeIoIdentity{
        .superblock_identifier = 7ULL,
        .superblock_generation = 3ULL,
        .node_identifier = node_identifier,
        .node_generation = 5ULL,
    };
}

[[nodiscard]] bool WaitUntilAtLeast(const std::atomic<uint64_t> &value,
                                    const uint64_t expected) noexcept {
    for (uint64_t spin_count = 0ULL; spin_count < OS_TEST_INODE_IO_COORDINATOR_SPIN_LIMIT;
         ++spin_count) {
        if (value.load(std::memory_order_acquire) >= expected) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

[[nodiscard]] bool WaitUntilActiveReferences(const os::kernel::fs::InodeIoCoordinator &coordinator,
                                             const uint64_t expected) noexcept {
    for (uint64_t spin_count = 0ULL; spin_count < OS_TEST_INODE_IO_COORDINATOR_SPIN_LIMIT;
         ++spin_count) {
        if (coordinator.Statistics().active_reference_count >= expected) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_INODE_IO_COORDINATOR_SUITE_NAME};

    os::kernel::fs::InodeIoCoordinator rejected_coordinator{};
    os::kernel::fs::InodeIoSlot rejected_storage[OS_TEST_INODE_IO_COORDINATOR_CAPACITY]{};
    rejected_storage[0ULL].cached = true;
    const bool initialization_rejected =
        rejected_coordinator.Initialize(nullptr, OS_TEST_INODE_IO_COORDINATOR_CAPACITY,
                                        OS_TEST_INODE_IO_COORDINATOR_WAIT_QUEUE_BASE) ==
            os::kernel::fs::InodeIoCoordinatorStatus::InvalidStorage &&
        rejected_coordinator.Initialize(rejected_storage, 0ULL,
                                        OS_TEST_INODE_IO_COORDINATOR_WAIT_QUEUE_BASE) ==
            os::kernel::fs::InodeIoCoordinatorStatus::InvalidCapacity &&
        rejected_coordinator.Initialize(rejected_storage, OS_TEST_INODE_IO_COORDINATOR_CAPACITY,
                                        0ULL) ==
            os::kernel::fs::InodeIoCoordinatorStatus::InvalidWaitQueueRange &&
        rejected_coordinator.Initialize(rejected_storage, OS_TEST_INODE_IO_COORDINATOR_CAPACITY,
                                        OS_TEST_INODE_IO_COORDINATOR_WAIT_QUEUE_BASE) ==
            os::kernel::fs::InodeIoCoordinatorStatus::InvalidStorage;

    os::kernel::fs::InodeIoSlot storage[OS_TEST_INODE_IO_COORDINATOR_CAPACITY]{};
    os::kernel::fs::InodeIoCoordinator coordinator{};
    os::kernel::fs::InodeIoToken invalid_token{};
    const bool invalid_operations_rejected =
        coordinator.Initialize(storage, OS_TEST_INODE_IO_COORDINATOR_CAPACITY,
                               OS_TEST_INODE_IO_COORDINATOR_WAIT_QUEUE_BASE) ==
            os::kernel::fs::InodeIoCoordinatorStatus::Succeeded &&
        coordinator.Initialize(storage, OS_TEST_INODE_IO_COORDINATOR_CAPACITY,
                               OS_TEST_INODE_IO_COORDINATOR_WAIT_QUEUE_BASE) ==
            os::kernel::fs::InodeIoCoordinatorStatus::AlreadyInitialized &&
        coordinator.Acquire(os::kernel::fs::InodeIoIdentity{}, invalid_token) ==
            os::kernel::fs::InodeIoCoordinatorStatus::InvalidIdentity &&
        coordinator.Release(invalid_token) ==
            os::kernel::fs::InodeIoCoordinatorStatus::InvalidToken;
    test_context.Expect(initialization_rejected && invalid_operations_rejected,
                        OS_TEST_INODE_IO_COORDINATOR_REJECTION_MESSAGE);

    os::kernel::fs::InodeIoToken first_token{};
    os::kernel::fs::InodeIoToken second_token{};
    os::kernel::fs::InodeIoToken rejected_token{};
    const bool capacity_rejected = coordinator.Acquire(MakeIdentity(11ULL), first_token) ==
                                       os::kernel::fs::InodeIoCoordinatorStatus::Succeeded &&
                                   coordinator.Acquire(MakeIdentity(12ULL), second_token) ==
                                       os::kernel::fs::InodeIoCoordinatorStatus::Succeeded &&
                                   coordinator.Acquire(MakeIdentity(13ULL), rejected_token) ==
                                       os::kernel::fs::InodeIoCoordinatorStatus::CapacityExhausted;
    const os::kernel::fs::InodeIoToken stale_first_token = first_token;
    os::kernel::fs::InodeIoToken replacement_token{};
    const bool capacity_recovered =
        coordinator.Release(first_token) == os::kernel::fs::InodeIoCoordinatorStatus::Succeeded &&
        coordinator.Release(second_token) == os::kernel::fs::InodeIoCoordinatorStatus::Succeeded &&
        coordinator.Acquire(MakeIdentity(13ULL), replacement_token) ==
            os::kernel::fs::InodeIoCoordinatorStatus::Succeeded &&
        coordinator.Release(replacement_token) ==
            os::kernel::fs::InodeIoCoordinatorStatus::Succeeded;
    os::kernel::fs::InodeIoToken stale_token = stale_first_token;
    const bool stale_rejected =
        coordinator.Release(stale_token) == os::kernel::fs::InodeIoCoordinatorStatus::InvalidToken;
    test_context.Expect(capacity_rejected && capacity_recovered && stale_rejected,
                        OS_TEST_INODE_IO_COORDINATOR_CAPACITY_MESSAGE);

    std::atomic<uint64_t> serialization_entry_count{0ULL};
    std::atomic<uint64_t> serialization_attempt_count{0ULL};
    std::atomic<uint64_t> serialization_inside_count{0ULL};
    std::atomic<uint64_t> serialization_peak_inside_count{0ULL};
    std::atomic<bool> release_first_serialized_worker{false};
    const os::kernel::fs::InodeIoIdentity serialized_identity = MakeIdentity(21ULL);
    const auto serialized_worker = [&](const bool first_worker) noexcept {
        serialization_attempt_count.fetch_add(1ULL, std::memory_order_release);
        os::kernel::fs::InodeIoToken token{};
        if (coordinator.Acquire(serialized_identity, token) !=
            os::kernel::fs::InodeIoCoordinatorStatus::Succeeded) {
            return;
        }
        const uint64_t inside_count =
            serialization_inside_count.fetch_add(1ULL, std::memory_order_acq_rel) + 1ULL;
        uint64_t observed_peak = serialization_peak_inside_count.load(std::memory_order_acquire);
        while (inside_count > observed_peak &&
               !serialization_peak_inside_count.compare_exchange_weak(observed_peak, inside_count,
                                                                      std::memory_order_acq_rel)) {
        }
        serialization_entry_count.fetch_add(1ULL, std::memory_order_release);
        if (first_worker) {
            while (!release_first_serialized_worker.load(std::memory_order_acquire)) {
            }
        }
        serialization_inside_count.fetch_sub(1ULL, std::memory_order_acq_rel);
        static_cast<void>(coordinator.Release(token));
    };
    std::thread first_serialized_thread{serialized_worker, true};
    const bool first_serialized_entered = WaitUntilAtLeast(serialization_entry_count, 1ULL);
    std::thread second_serialized_thread{serialized_worker, false};
    const bool second_serialized_attempted = WaitUntilAtLeast(serialization_attempt_count, 2ULL);
    const bool second_serialized_waiting = WaitUntilActiveReferences(coordinator, 2ULL);
    const bool second_serialized_blocked =
        serialization_entry_count.load(std::memory_order_acquire) == 1ULL;
    release_first_serialized_worker.store(true, std::memory_order_release);
    first_serialized_thread.join();
    second_serialized_thread.join();
    test_context.Expect(first_serialized_entered && second_serialized_attempted &&
                            second_serialized_waiting && second_serialized_blocked &&
                            serialization_entry_count.load(std::memory_order_acquire) == 2ULL &&
                            serialization_peak_inside_count.load(std::memory_order_acquire) == 1ULL,
                        OS_TEST_INODE_IO_COORDINATOR_SERIALIZATION_MESSAGE);

    std::atomic<uint64_t> parallel_entry_count{0ULL};
    std::atomic<bool> release_parallel_workers{false};
    const auto parallel_worker = [&](const os::kernel::fs::InodeIoIdentity identity) noexcept {
        os::kernel::fs::InodeIoToken token{};
        if (coordinator.Acquire(identity, token) !=
            os::kernel::fs::InodeIoCoordinatorStatus::Succeeded) {
            return;
        }
        parallel_entry_count.fetch_add(1ULL, std::memory_order_release);
        while (!release_parallel_workers.load(std::memory_order_acquire)) {
        }
        static_cast<void>(coordinator.Release(token));
    };
    std::thread first_parallel_thread{parallel_worker, MakeIdentity(31ULL)};
    std::thread second_parallel_thread{parallel_worker, MakeIdentity(32ULL)};
    const bool parallel_workers_entered = WaitUntilAtLeast(parallel_entry_count, 2ULL);
    release_parallel_workers.store(true, std::memory_order_release);
    first_parallel_thread.join();
    second_parallel_thread.join();
    test_context.Expect(parallel_workers_entered, OS_TEST_INODE_IO_COORDINATOR_PARALLELISM_MESSAGE);

    const os::kernel::fs::InodeIoCoordinatorStatistics statistics = coordinator.Statistics();
    const bool lifecycle_valid =
        coordinator.Validate() == os::kernel::fs::InodeIoCoordinatorStatus::Succeeded &&
        statistics.capacity == OS_TEST_INODE_IO_COORDINATOR_CAPACITY &&
        statistics.referenced_slot_count == 0ULL && statistics.active_reference_count == 0ULL &&
        statistics.peak_referenced_slot_count == OS_TEST_INODE_IO_COORDINATOR_CAPACITY &&
        statistics.peak_active_reference_count == OS_TEST_INODE_IO_COORDINATOR_CAPACITY &&
        statistics.acquisition_count == statistics.release_count &&
        statistics.capacity_rejection_count == 1ULL && statistics.slot_replacement_count != 0ULL;
    test_context.Expect(lifecycle_valid, OS_TEST_INODE_IO_COORDINATOR_LIFECYCLE_MESSAGE);
    return test_context.ExitCode();
}
