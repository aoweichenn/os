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
    os::test::TestContext testContext{OS_TEST_SYNCHRONIZATION_INTEGRATION_SUITE_NAME};
    os::kernel::SpinLock spinLock{};
    uint64_t protectedCounter = OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT;

    std::thread workers[OS_TEST_SYNCHRONIZATION_INTEGRATION_THREAD_COUNT];
    for (uint64_t workerIndex = OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT;
         workerIndex < OS_TEST_SYNCHRONIZATION_INTEGRATION_THREAD_COUNT; ++workerIndex) {
        workers[workerIndex] = std::thread([&spinLock, &protectedCounter]() {
            for (uint64_t iteration = OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT;
                 iteration < OS_TEST_SYNCHRONIZATION_INTEGRATION_ITERATIONS_PER_THREAD;
                 ++iteration) {
                os::kernel::SpinLockGuard guard{spinLock};
                protectedCounter += OS_TEST_SYNCHRONIZATION_INTEGRATION_COUNTER_INCREMENT;
            }
        });
    }
    for (uint64_t workerIndex = OS_TEST_SYNCHRONIZATION_INTEGRATION_EMPTY_COUNT;
         workerIndex < OS_TEST_SYNCHRONIZATION_INTEGRATION_THREAD_COUNT; ++workerIndex) {
        workers[workerIndex].join();
    }
    testContext.Expect(protectedCounter == OS_TEST_SYNCHRONIZATION_INTEGRATION_EXPECTED_COUNT,
                       OS_TEST_SYNCHRONIZATION_INTEGRATION_MUTUAL_EXCLUSION);

    {
        os::kernel::SpinLockGuard guard{spinLock};
        testContext.Expect(spinLock.IsLocked(), OS_TEST_SYNCHRONIZATION_INTEGRATION_GUARD_RELEASE);
    }
    testContext.Expect(!spinLock.IsLocked(), OS_TEST_SYNCHRONIZATION_INTEGRATION_GUARD_RELEASE);
    return testContext.ExitCode();
}
