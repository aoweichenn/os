#include "os/kernel/process/thread_scheduler.hpp"
#include "os/kernel/sync/private_futex.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PRIVATE_FUTEX_UNIT_SUITE_NAME = "kernel/private_futex/unit";
constexpr std::string_view OS_TEST_PRIVATE_FUTEX_UNIT_KEY_ISOLATION =
    "同一 VA 必须按 AddressSpaceId 隔离且重复 Acquire 复用同一等待队列";
constexpr std::string_view OS_TEST_PRIVATE_FUTEX_UNIT_WAIT_WAKE =
    "Thread 阻塞与单赢家唤醒必须保持 futex entry 和调度队列守恒";
constexpr std::string_view OS_TEST_PRIVATE_FUTEX_UNIT_BOUNDARIES =
    "未对齐地址、零 AddressSpaceId、容量耗尽和有 waiter 释放必须明确失败";
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_ENTRY_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_QUEUE_IDENTIFIER = 0x100ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_ADDRESS_SPACE = 17ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_OTHER_ADDRESS_SPACE = 18ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_USER_ADDRESS = 0x60001000ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_CAPACITY_ADDRESS_BASE = 0x61000000ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_PROCESS_CAPACITY = 1ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_THREAD_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_THREAD_LIMIT = 4ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_QUANTUM = 4ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_PAGE_TABLE_ROOT = 0x1000ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_UNIT_USER_STACK = 0x70000000ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_PRIVATE_FUTEX_UNIT_SUITE_NAME};

    os::kernel::PrivateFutexEntry entries[OS_TEST_PRIVATE_FUTEX_UNIT_ENTRY_CAPACITY]{};
    os::kernel::PrivateFutexManager manager{};
    bool boundaries_valid = manager.Initialize(entries, OS_TEST_PRIVATE_FUTEX_UNIT_ENTRY_CAPACITY,
                                               OS_TEST_PRIVATE_FUTEX_UNIT_QUEUE_IDENTIFIER) ==
                            os::kernel::PrivateFutexStatus::Succeeded;
    uint64_t first_entry_index = os::kernel::OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
    os::kernel::WaitQueue *first_queue = nullptr;
    const os::kernel::PrivateFutexKey first_key{
        .address_space_identifier = OS_TEST_PRIVATE_FUTEX_UNIT_ADDRESS_SPACE,
        .user_address = OS_TEST_PRIVATE_FUTEX_UNIT_USER_ADDRESS,
    };
    boundaries_valid = boundaries_valid &&
                       manager.Acquire(first_key, first_entry_index, first_queue) ==
                           os::kernel::PrivateFutexStatus::Succeeded &&
                       first_queue != nullptr;
    uint64_t repeated_entry_index = os::kernel::OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
    os::kernel::WaitQueue *repeated_queue = nullptr;
    const bool key_isolation = manager.Acquire(first_key, repeated_entry_index, repeated_queue) ==
                                   os::kernel::PrivateFutexStatus::Succeeded &&
                               repeated_entry_index == first_entry_index &&
                               repeated_queue == first_queue;
    uint64_t isolated_entry_index = os::kernel::OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
    os::kernel::WaitQueue *isolated_queue = nullptr;
    const os::kernel::PrivateFutexKey isolated_key{
        .address_space_identifier = OS_TEST_PRIVATE_FUTEX_UNIT_OTHER_ADDRESS_SPACE,
        .user_address = OS_TEST_PRIVATE_FUTEX_UNIT_USER_ADDRESS,
    };
    test_context.Expect(key_isolation &&
                            manager.Acquire(isolated_key, isolated_entry_index, isolated_queue) ==
                                os::kernel::PrivateFutexStatus::Succeeded &&
                            isolated_entry_index != first_entry_index &&
                            isolated_queue != first_queue,
                        OS_TEST_PRIVATE_FUTEX_UNIT_KEY_ISOLATION);

    os::kernel::ProcessEntry processes[OS_TEST_PRIVATE_FUTEX_UNIT_PROCESS_CAPACITY]{};
    os::kernel::ThreadEntry threads[OS_TEST_PRIVATE_FUTEX_UNIT_THREAD_CAPACITY]{};
    os::kernel::ThreadScheduler scheduler{};
    uint64_t process_index = os::kernel::OS_KERNEL_PROCESS_INVALID_INDEX;
    os::kernel::ProcessId process_id{};
    uint64_t thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    os::kernel::ThreadId thread_id{};
    bool wait_wake_valid =
        scheduler.Initialize(
            processes, OS_TEST_PRIVATE_FUTEX_UNIT_PROCESS_CAPACITY, threads,
            OS_TEST_PRIVATE_FUTEX_UNIT_THREAD_CAPACITY, OS_TEST_PRIVATE_FUTEX_UNIT_THREAD_LIMIT,
            OS_TEST_PRIVATE_FUTEX_UNIT_QUANTUM) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.CreateProcess(OS_TEST_PRIVATE_FUTEX_UNIT_PAGE_TABLE_ROOT, process_index,
                                process_id) == os::kernel::ThreadSchedulerStatus::Succeeded;
    for (uint64_t ordinal = OS_TEST_PRIVATE_FUTEX_UNIT_EMPTY_VALUE;
         wait_wake_valid && ordinal < OS_TEST_PRIVATE_FUTEX_UNIT_THREAD_CAPACITY; ++ordinal) {
        wait_wake_valid =
            scheduler.CreateThread(
                process_index, ordinal, OS_TEST_PRIVATE_FUTEX_UNIT_USER_STACK + ordinal,
                OS_TEST_PRIVATE_FUTEX_UNIT_EMPTY_VALUE, OS_TEST_PRIVATE_FUTEX_UNIT_EMPTY_VALUE,
                thread_index, thread_id) == os::kernel::ThreadSchedulerStatus::Succeeded;
    }
    os::kernel::ThreadSchedulingDecision decision{};
    wait_wake_valid =
        wait_wake_valid &&
        scheduler.Start(decision) == os::kernel::ThreadSchedulerStatus::Succeeded &&
        scheduler.BlockCurrentThread(*first_queue, os::kernel::WaitCondition::PrivateFutex,
                                     decision) == os::kernel::ThreadSchedulerStatus::Succeeded;
    bool released = false;
    boundaries_valid = boundaries_valid &&
                       manager.ReleaseIfEmpty(first_entry_index, released) ==
                           os::kernel::PrivateFutexStatus::WaitersRemain &&
                       !released;
    uint64_t woken_thread_index = os::kernel::OS_KERNEL_THREAD_INVALID_INDEX;
    bool wake_won = false;
    wait_wake_valid = wait_wake_valid &&
                      scheduler.WakeOne(*first_queue, os::kernel::WakeReason::ConditionSatisfied,
                                        woken_thread_index,
                                        wake_won) == os::kernel::ThreadSchedulerStatus::Succeeded &&
                      wake_won &&
                      manager.ReleaseIfEmpty(first_entry_index, released) ==
                          os::kernel::PrivateFutexStatus::Succeeded &&
                      released && manager.Validate() == os::kernel::PrivateFutexStatus::Succeeded &&
                      scheduler.Validate() == os::kernel::ThreadSchedulerStatus::Succeeded;
    test_context.Expect(wait_wake_valid, OS_TEST_PRIVATE_FUTEX_UNIT_WAIT_WAKE);

    uint64_t invalid_index = os::kernel::OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
    os::kernel::WaitQueue *invalid_queue = nullptr;
    const os::kernel::PrivateFutexKey zero_space_key{
        .address_space_identifier = OS_TEST_PRIVATE_FUTEX_UNIT_EMPTY_VALUE,
        .user_address = OS_TEST_PRIVATE_FUTEX_UNIT_USER_ADDRESS,
    };
    const os::kernel::PrivateFutexKey unaligned_key{
        .address_space_identifier = OS_TEST_PRIVATE_FUTEX_UNIT_ADDRESS_SPACE,
        .user_address =
            OS_TEST_PRIVATE_FUTEX_UNIT_USER_ADDRESS + OS_TEST_PRIVATE_FUTEX_UNIT_FIRST_VALUE,
    };
    boundaries_valid = boundaries_valid &&
                       manager.Acquire(zero_space_key, invalid_index, invalid_queue) ==
                           os::kernel::PrivateFutexStatus::InvalidAddressSpace &&
                       manager.Acquire(unaligned_key, invalid_index, invalid_queue) ==
                           os::kernel::PrivateFutexStatus::InvalidAddress;

    os::kernel::PrivateFutexEntry capacity_entries[OS_TEST_PRIVATE_FUTEX_UNIT_ENTRY_CAPACITY]{};
    os::kernel::PrivateFutexManager capacity_manager{};
    boundaries_valid =
        boundaries_valid &&
        capacity_manager.Initialize(capacity_entries, OS_TEST_PRIVATE_FUTEX_UNIT_ENTRY_CAPACITY,
                                    OS_TEST_PRIVATE_FUTEX_UNIT_QUEUE_IDENTIFIER +
                                        OS_TEST_PRIVATE_FUTEX_UNIT_ENTRY_CAPACITY) ==
            os::kernel::PrivateFutexStatus::Succeeded;
    for (uint64_t entry_ordinal = OS_TEST_PRIVATE_FUTEX_UNIT_EMPTY_VALUE;
         boundaries_valid && entry_ordinal < OS_TEST_PRIVATE_FUTEX_UNIT_ENTRY_CAPACITY;
         ++entry_ordinal) {
        const os::kernel::PrivateFutexKey capacity_key{
            .address_space_identifier = OS_TEST_PRIVATE_FUTEX_UNIT_ADDRESS_SPACE,
            .user_address =
                OS_TEST_PRIVATE_FUTEX_UNIT_CAPACITY_ADDRESS_BASE +
                entry_ordinal * os::kernel::OS_KERNEL_PRIVATE_FUTEX_WORD_ALIGNMENT_BYTES,
        };
        boundaries_valid = capacity_manager.Acquire(capacity_key, invalid_index, invalid_queue) ==
                               os::kernel::PrivateFutexStatus::Succeeded &&
                           invalid_queue != nullptr;
    }
    const os::kernel::PrivateFutexKey exhausted_key{
        .address_space_identifier = OS_TEST_PRIVATE_FUTEX_UNIT_ADDRESS_SPACE,
        .user_address = OS_TEST_PRIVATE_FUTEX_UNIT_CAPACITY_ADDRESS_BASE +
                        OS_TEST_PRIVATE_FUTEX_UNIT_ENTRY_CAPACITY *
                            os::kernel::OS_KERNEL_PRIVATE_FUTEX_WORD_ALIGNMENT_BYTES,
    };
    boundaries_valid =
        boundaries_valid && capacity_manager.Acquire(exhausted_key, invalid_index, invalid_queue) ==
                                os::kernel::PrivateFutexStatus::EntryCapacityExhausted;
    test_context.Expect(boundaries_valid, OS_TEST_PRIVATE_FUTEX_UNIT_BOUNDARIES);
    return test_context.ExitCode();
}
