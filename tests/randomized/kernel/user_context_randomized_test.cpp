#include "os/kernel/arch/user_context.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_CONTEXT_RANDOM_SUITE_NAME =
    "kernel/user_context/randomized";
constexpr std::string_view OS_TEST_USER_CONTEXT_RANDOM_CANONICAL_MESSAGE =
    "随机地址的规范性判定必须与 48 位符号扩展定义一致";
constexpr std::string_view OS_TEST_USER_CONTEXT_RANDOM_VALID_MESSAGE =
    "随机合法用户现场必须稳定通过完整校验";
constexpr std::string_view OS_TEST_USER_CONTEXT_RANDOM_RETURN_MESSAGE =
    "随机返回现场只能在满足 SYSRET 白名单时选择快速路径";
constexpr std::string_view OS_TEST_USER_CONTEXT_RANDOM_REJECTION_MESSAGE =
    "随机非规范 RIP 必须始终被返回校验拒绝";

constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_SEED = 0x5A17C011BADC0FFEULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_FIRST_SHIFT = 12ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_SECOND_SHIFT = 25ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_THIRD_SHIFT = 27ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_VIRTUAL_WIDTH_BITS = 48ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_LOWER_MASK = 0x00007FFFFFFFFFFFULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_UPPER_MINIMUM = 0xFFFF800000000000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_NON_CANONICAL_BIT = 0x0000800000000000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_INSTRUCTION_BASE = 0x0000000040000000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_INSTRUCTION_MASK = 0x00000000000FFFFFULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_STACK_BASE = 0x0000700000000000ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_STACK_MASK = 0x00000FFFFFFFFFFFULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_USER_CODE_SEGMENT = 0x23ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_USER_STACK_SEGMENT = 0x1BULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_ENTRY_SELECTOR_MASK = 0x03ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_REQUIRED_FLAGS = 0x202ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_INITIAL_SELECTOR = 0ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_LEGACY_SELECTOR = 1ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_NATIVE_SELECTOR = 2ULL;
constexpr uint64_t OS_TEST_USER_CONTEXT_RANDOM_ITERATION_INCREMENT = 1ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_USER_CONTEXT_RANDOM_FIRST_SHIFT;
    state ^= state << OS_TEST_USER_CONTEXT_RANDOM_SECOND_SHIFT;
    state ^= state >> OS_TEST_USER_CONTEXT_RANDOM_THIRD_SHIFT;
    return state * OS_TEST_USER_CONTEXT_RANDOM_MULTIPLIER;
}

[[nodiscard]] uint64_t SelectVector(const uint64_t selector) noexcept {
    if (selector == OS_TEST_USER_CONTEXT_RANDOM_INITIAL_SELECTOR) {
        return os::kernel::OS_KERNEL_USER_CONTEXT_INITIAL_VECTOR;
    }
    if (selector == OS_TEST_USER_CONTEXT_RANDOM_LEGACY_SELECTOR) {
        return os::kernel::OS_KERNEL_USER_CONTEXT_LEGACY_SYSTEM_CALL_VECTOR;
    }
    if (selector == OS_TEST_USER_CONTEXT_RANDOM_NATIVE_SELECTOR) {
        return os::kernel::OS_KERNEL_USER_CONTEXT_NATIVE_SYSTEM_CALL_VECTOR;
    }
    return os::kernel::OS_KERNEL_USER_CONTEXT_FIRST_HARDWARE_INTERRUPT_VECTOR;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_CONTEXT_RANDOM_SUITE_NAME};
    const os::kernel::UserContextRequirements requirements{
        .virtual_address_width_bits = OS_TEST_USER_CONTEXT_RANDOM_VIRTUAL_WIDTH_BITS,
        .user_code_segment = OS_TEST_USER_CONTEXT_RANDOM_USER_CODE_SEGMENT,
        .user_stack_segment = OS_TEST_USER_CONTEXT_RANDOM_USER_STACK_SEGMENT,
    };
    uint64_t random_state = OS_TEST_USER_CONTEXT_RANDOM_SEED;

    for (uint64_t iteration = OS_TEST_USER_CONTEXT_RANDOM_EMPTY_VALUE;
         iteration < OS_TEST_USER_CONTEXT_RANDOM_ITERATION_COUNT;
         iteration += OS_TEST_USER_CONTEXT_RANDOM_ITERATION_INCREMENT) {
        const uint64_t random_address = NextRandom(random_state);
        const bool expected_canonical = random_address <= OS_TEST_USER_CONTEXT_RANDOM_LOWER_MASK ||
                                        random_address >= OS_TEST_USER_CONTEXT_RANDOM_UPPER_MINIMUM;
        const bool expected_lower = random_address <= OS_TEST_USER_CONTEXT_RANDOM_LOWER_MASK;
        test_context.ExpectRandom(
            os::kernel::IsCanonicalVirtualAddress(random_address,
                                                  OS_TEST_USER_CONTEXT_RANDOM_VIRTUAL_WIDTH_BITS) ==
                    expected_canonical &&
                os::kernel::IsLowerCanonicalUserAddress(
                    random_address, OS_TEST_USER_CONTEXT_RANDOM_VIRTUAL_WIDTH_BITS) ==
                    expected_lower,
            OS_TEST_USER_CONTEXT_RANDOM_CANONICAL_MESSAGE, OS_TEST_USER_CONTEXT_RANDOM_SEED,
            iteration);

        const uint64_t random_flags = NextRandom(random_state);
        os::kernel::UserContext context{};
        context.common.vector =
            SelectVector(random_flags & OS_TEST_USER_CONTEXT_RANDOM_ENTRY_SELECTOR_MASK);
        context.common.instruction_pointer =
            OS_TEST_USER_CONTEXT_RANDOM_INSTRUCTION_BASE |
            (NextRandom(random_state) & OS_TEST_USER_CONTEXT_RANDOM_INSTRUCTION_MASK);
        context.common.code_segment = OS_TEST_USER_CONTEXT_RANDOM_USER_CODE_SEGMENT;
        context.common.flags = OS_TEST_USER_CONTEXT_RANDOM_REQUIRED_FLAGS |
                               (random_flags & os::kernel::OS_KERNEL_USER_CONTEXT_VALID_FLAGS_MASK);
        context.stack_pointer = OS_TEST_USER_CONTEXT_RANDOM_STACK_BASE |
                                (NextRandom(random_state) & OS_TEST_USER_CONTEXT_RANDOM_STACK_MASK);
        context.stack_segment = OS_TEST_USER_CONTEXT_RANDOM_USER_STACK_SEGMENT;

        test_context.ExpectRandom(os::kernel::ValidateUserContext(context, requirements) ==
                                      os::kernel::UserContextStatus::Succeeded,
                                  OS_TEST_USER_CONTEXT_RANDOM_VALID_MESSAGE,
                                  OS_TEST_USER_CONTEXT_RANDOM_SEED, iteration);

        const bool system_return_expected =
            os::kernel::DecodeUserContextEntryMethod(context) ==
                os::kernel::UserContextEntryMethod::NativeSystemCall &&
            (context.common.flags & ~os::kernel::OS_KERNEL_USER_CONTEXT_SYSTEM_RETURN_FLAGS_MASK) ==
                OS_TEST_USER_CONTEXT_RANDOM_EMPTY_VALUE;
        test_context.ExpectRandom(os::kernel::SelectUserReturnMethod(context, requirements) ==
                                      (system_return_expected
                                           ? os::kernel::UserReturnMethod::SystemReturn
                                           : os::kernel::UserReturnMethod::InterruptReturn),
                                  OS_TEST_USER_CONTEXT_RANDOM_RETURN_MESSAGE,
                                  OS_TEST_USER_CONTEXT_RANDOM_SEED, iteration);

        context.common.instruction_pointer =
            OS_TEST_USER_CONTEXT_RANDOM_NON_CANONICAL_BIT |
            (NextRandom(random_state) & OS_TEST_USER_CONTEXT_RANDOM_LOWER_MASK);
        test_context.ExpectRandom(
            os::kernel::ValidateUserContext(context, requirements) ==
                    os::kernel::UserContextStatus::InvalidInstructionPointer &&
                os::kernel::SelectUserReturnMethod(context, requirements) ==
                    os::kernel::UserReturnMethod::Rejected,
            OS_TEST_USER_CONTEXT_RANDOM_REJECTION_MESSAGE, OS_TEST_USER_CONTEXT_RANDOM_SEED,
            iteration);
    }

    return test_context.ExitCode();
}
