#include "os/kernel/spin_lock.hpp"
#include "test_context.hpp"

#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_SYNCHRONIZATION_INTEGRATION_SUITE_NAME =
    "kernel/synchronization/integration";
constexpr std::string_view OS_TEST_SYNCHRONIZATION_INTEGRATION_MUTUAL_EXCLUSION =
    "多线程压力下自旋锁必须保持临界区计数守恒";
constexpr std::string_view OS_TEST_SYNCHRONIZATION_INTEGRATION_GUARD_RELEASE =
    "RAII guard 离开作用域时必须释放锁";
constexpr uint64_t OS_TEST_SYNCHRONIZATION_INTEGRATION_THREAD_COUNT = 4ULL;
constexpr uint64_t OS_TEST_SYNCHRONIZATION_INTEGRATION_ITERATIONS_PER_THREAD = 50000ULL;
constexpr uint64_t OS_TEST_SYNCHRONIZATION_INTEGRATION_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_TEST_SYNCHRONIZATION_INTEGRATION_EXPECTED_COUNT =
    OS_TEST_SYNCHRONIZATION_INTEGRATION_THREAD_COUNT *
    OS_TEST_SYNCHRONIZATION_INTEGRATION_ITERATIONS_PER_THREAD;

}

int main() {
    os::test::TestContext test_context{OS_TEST_SYNCHRONIZATION_INTEGRATION_SUITE_NAME};
    os::kernel::SpinLock spin_lock{};
    uint64_t protected_counter = OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT;

    std::thread workers[OS_TEST_SYNCHRONIZATION_INTEGRATION_THREAD_COUNT];
    for (uint64_t worker_index = OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT;
         worker_index < OS_TEST_SYNCHRONIZATION_INTEGRATION_THREAD_COUNT; ++worker_index) {
        workers[worker_index] = std::thread([&spin_lock, &protected_counter]() {
            for (uint64_t iteration = OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT;
                 iteration < OS_TEST_SYNCHRONIZATION_INTEGRATION_ITERATIONS_PER_THREAD;
                 ++iteration) {
                os::kernel::SpinLockGuard guard{spin_lock};
                protected_counter += OS_TEST_SYNCHRONIZATION_INTEGRATION_COUNTER_INCREMENT;
            }
        });
    }
    for (uint64_t worker_index = OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT;
         worker_index < OS_TEST_SYNCHRONIZATION_INTEGRATION_THREAD_COUNT; ++worker_index) {
        workers[worker_index].join();
    }
    test_context.Expect(protected_counter == OS_TEST_SYNCHRONIZATION_INTEGRATION_EXPECTED_COUNT,
                        OS_TEST_SYNCHRONIZATION_INTEGRATION_MUTUAL_EXCLUSION);

    {
        os::kernel::SpinLockGuard guard{spin_lock};
        test_context.Expect(spin_lock.IsLocked(),
                            OS_TEST_SYNCHRONIZATION_INTEGRATION_GUARD_RELEASE);
    }
    test_context.Expect(!spin_lock.IsLocked(), OS_TEST_SYNCHRONIZATION_INTEGRATION_GUARD_RELEASE);
    return test_context.ExitCode();
}
