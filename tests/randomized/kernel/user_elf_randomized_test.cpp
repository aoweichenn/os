#include "os/kernel/user_elf.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_ELF_RANDOM_SUITE_NAME = "kernel/user_elf/randomized";
constexpr std::string_view OS_TEST_USER_ELF_RANDOM_VALID_RANGE_MESSAGE =
    "随机合法用户范围必须被接受";
constexpr std::string_view OS_TEST_USER_ELF_RANDOM_OVERFLOW_MESSAGE = "随机溢出用户范围必须被拒绝";
constexpr uint64_t OS_TEST_USER_ELF_RANDOM_SEED = 0x55E24F9D7B311A63ULL;
constexpr uint64_t OS_TEST_USER_ELF_RANDOM_ITERATION_COUNT = 8192ULL;
constexpr uint64_t OS_TEST_USER_ELF_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_USER_ELF_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_USER_ELF_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_USER_ELF_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_USER_ELF_RANDOM_MAXIMUM_LENGTH_BYTES = 0x0000000000010000ULL;
constexpr uint64_t OS_TEST_USER_ELF_RANDOM_NONZERO_ADJUSTMENT = 1ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_USER_ELF_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_USER_ELF_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_USER_ELF_RANDOM_SHIFT_THIRD;
    return state * OS_TEST_USER_ELF_RANDOM_MULTIPLIER;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_ELF_RANDOM_SUITE_NAME};
    uint64_t random_state = OS_TEST_USER_ELF_RANDOM_SEED;
    for (uint64_t iteration = 0ULL; iteration < OS_TEST_USER_ELF_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t maximum_begin =
            os::kernel::OS_KERNEL_USER_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE -
            OS_TEST_USER_ELF_RANDOM_MAXIMUM_LENGTH_BYTES;
        const uint64_t begin_address =
            os::kernel::OS_KERNEL_USER_MINIMUM_VIRTUAL_ADDRESS +
            NextRandom(random_state) %
                (maximum_begin - os::kernel::OS_KERNEL_USER_MINIMUM_VIRTUAL_ADDRESS);
        const uint64_t length_bytes =
            NextRandom(random_state) % OS_TEST_USER_ELF_RANDOM_MAXIMUM_LENGTH_BYTES +
            OS_TEST_USER_ELF_RANDOM_NONZERO_ADJUSTMENT;
        test_context.ExpectRandom(
            os::kernel::IsUserVirtualAddressRange(begin_address, length_bytes),
            OS_TEST_USER_ELF_RANDOM_VALID_RANGE_MESSAGE, OS_TEST_USER_ELF_RANDOM_SEED, iteration);

        const uint64_t overflowing_length =
            os::kernel::OS_KERNEL_USER_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE - begin_address +
            OS_TEST_USER_ELF_RANDOM_NONZERO_ADJUSTMENT;
        test_context.ExpectRandom(
            !os::kernel::IsUserVirtualAddressRange(begin_address, overflowing_length),
            OS_TEST_USER_ELF_RANDOM_OVERFLOW_MESSAGE, OS_TEST_USER_ELF_RANDOM_SEED, iteration);
    }
    return test_context.ExitCode();
}
