#include "os/kernel/sync/private_futex.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PRIVATE_FUTEX_RANDOM_SUITE_NAME =
    "kernel/private_futex/randomized";
constexpr std::string_view OS_TEST_PRIVATE_FUTEX_RANDOM_LIFECYCLE =
    "十万步 key Acquire/复用/释放随机模型不得合并地址空间或遗留 entry";
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_CAPACITY = 64ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_QUEUE_IDENTIFIER = 0x2000ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_SPACE_BASE = 1ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_SPACE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_BASE = 0x60000000ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_COUNT = 8ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_SEED = 0x4655544558313230ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_PRIVATE_FUTEX_RANDOM_RELEASE_MASK = 3ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state =
        state * OS_TEST_PRIVATE_FUTEX_RANDOM_MULTIPLIER + OS_TEST_PRIVATE_FUTEX_RANDOM_INCREMENT;
    return state;
}

[[nodiscard]] uint64_t KeyOrdinal(const uint64_t address_space_ordinal,
                                  const uint64_t address_ordinal) noexcept {
    return address_space_ordinal * OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_COUNT + address_ordinal;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PRIVATE_FUTEX_RANDOM_SUITE_NAME};
    os::kernel::PrivateFutexEntry entries[OS_TEST_PRIVATE_FUTEX_RANDOM_CAPACITY]{};
    os::kernel::PrivateFutexManager manager{};
    bool active[OS_TEST_PRIVATE_FUTEX_RANDOM_CAPACITY]{};
    uint64_t entry_indices[OS_TEST_PRIVATE_FUTEX_RANDOM_CAPACITY]{};
    bool consistent = manager.Initialize(entries, OS_TEST_PRIVATE_FUTEX_RANDOM_CAPACITY,
                                         OS_TEST_PRIVATE_FUTEX_RANDOM_QUEUE_IDENTIFIER) ==
                      os::kernel::PrivateFutexStatus::Succeeded;
    uint64_t random_state = OS_TEST_PRIVATE_FUTEX_RANDOM_SEED;
    for (uint64_t operation_index = OS_TEST_PRIVATE_FUTEX_RANDOM_EMPTY_VALUE;
         consistent && operation_index < OS_TEST_PRIVATE_FUTEX_RANDOM_OPERATION_COUNT;
         ++operation_index) {
        const uint64_t address_space_ordinal =
            NextRandom(random_state) % OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_SPACE_COUNT;
        const uint64_t address_ordinal =
            NextRandom(random_state) % OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_COUNT;
        const uint64_t key_ordinal = KeyOrdinal(address_space_ordinal, address_ordinal);
        const os::kernel::PrivateFutexKey key{
            .address_space_identifier =
                OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_SPACE_BASE + address_space_ordinal,
            .user_address =
                OS_TEST_PRIVATE_FUTEX_RANDOM_ADDRESS_BASE +
                address_ordinal * os::kernel::OS_KERNEL_PRIVATE_FUTEX_WORD_ALIGNMENT_BYTES,
        };
        uint64_t entry_index = os::kernel::OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
        os::kernel::WaitQueue *wait_queue = nullptr;
        consistent = manager.Acquire(key, entry_index, wait_queue) ==
                         os::kernel::PrivateFutexStatus::Succeeded &&
                     wait_queue != nullptr;
        if (!consistent) {
            break;
        }
        if (active[key_ordinal]) {
            consistent = entry_indices[key_ordinal] == entry_index;
        } else {
            active[key_ordinal] = true;
            entry_indices[key_ordinal] = entry_index;
        }
        if ((NextRandom(random_state) & OS_TEST_PRIVATE_FUTEX_RANDOM_RELEASE_MASK) ==
            OS_TEST_PRIVATE_FUTEX_RANDOM_EMPTY_VALUE) {
            bool released = false;
            consistent = manager.ReleaseIfEmpty(entry_index, released) ==
                             os::kernel::PrivateFutexStatus::Succeeded &&
                         released;
            active[key_ordinal] = false;
        }
        if (operation_index % OS_TEST_PRIVATE_FUTEX_RANDOM_CAPACITY ==
                OS_TEST_PRIVATE_FUTEX_RANDOM_EMPTY_VALUE &&
            manager.Validate() != os::kernel::PrivateFutexStatus::Succeeded) {
            consistent = false;
        }
    }
    for (uint64_t key_ordinal = OS_TEST_PRIVATE_FUTEX_RANDOM_EMPTY_VALUE;
         consistent && key_ordinal < OS_TEST_PRIVATE_FUTEX_RANDOM_CAPACITY; ++key_ordinal) {
        if (!active[key_ordinal]) {
            continue;
        }
        bool released = false;
        consistent = manager.ReleaseIfEmpty(entry_indices[key_ordinal], released) ==
                         os::kernel::PrivateFutexStatus::Succeeded &&
                     released;
    }
    const os::kernel::PrivateFutexStatistics statistics = manager.Statistics();
    test_context.ExpectRandom(
        consistent && manager.Validate() == os::kernel::PrivateFutexStatus::Succeeded &&
            statistics.active_entry_count == OS_TEST_PRIVATE_FUTEX_RANDOM_EMPTY_VALUE &&
            statistics.waiting_thread_count == OS_TEST_PRIVATE_FUTEX_RANDOM_EMPTY_VALUE &&
            statistics.acquire_count == statistics.release_count,
        OS_TEST_PRIVATE_FUTEX_RANDOM_LIFECYCLE, OS_TEST_PRIVATE_FUTEX_RANDOM_SEED,
        OS_TEST_PRIVATE_FUTEX_RANDOM_OPERATION_COUNT);
    return test_context.ExitCode();
}
