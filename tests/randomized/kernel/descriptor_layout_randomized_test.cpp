#include "os/kernel/descriptor_layout.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SUITE_NAME =
    "kernel/descriptor_layout/randomized";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_RANDOM_TSS_MESSAGE =
    "随机 TSS 描述符必须往返保留基址";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_RANDOM_GATE_MESSAGE =
    "随机 IDT gate 必须往返保留处理器地址";
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SEED = 0xD35C71A05EED6405ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_RANDOM_ITERATION_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_LOW_MASK = 0x0000000000FFFFFFULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_LOW_SHIFT = 16ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_BYTE_MASK = 0xFFULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_BYTE_SHIFT = 56ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_BYTE_TARGET_SHIFT = 24ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_SHIFT = 32ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_GATE_MIDDLE_SHIFT = 16ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_GATE_HIGH_SHIFT = 32ULL;
constexpr uint32_t OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT = 103U;
constexpr uint16_t OS_TEST_KERNEL_DESCRIPTOR_GATE_SELECTOR = 0x0008U;
constexpr uint8_t OS_TEST_KERNEL_DESCRIPTOR_GATE_IST = 0x03U;
constexpr uint8_t OS_TEST_KERNEL_DESCRIPTOR_GATE_ATTRIBUTES = 0x8EU;

[[nodiscard]] uint64_t nextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SHIFT_THIRD;
    return state * OS_TEST_KERNEL_DESCRIPTOR_RANDOM_MULTIPLIER;
}

[[nodiscard]] uint64_t
decodeTaskStateSegmentBase(const os::kernel::SystemSegmentDescriptor &descriptor) noexcept {
    return ((descriptor.low >> OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_LOW_SHIFT) &
            OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_LOW_MASK) |
           (((descriptor.low >> OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_BYTE_SHIFT) &
             OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_BYTE_MASK)
            << OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_BYTE_TARGET_SHIFT) |
           (descriptor.high << OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_SHIFT);
}

[[nodiscard]] uint64_t
decodeInterruptGateAddress(const os::kernel::InterruptGateDescriptor &descriptor) noexcept {
    return static_cast<uint64_t>(descriptor.offsetLow) |
           (static_cast<uint64_t>(descriptor.offsetMiddle)
            << OS_TEST_KERNEL_DESCRIPTOR_GATE_MIDDLE_SHIFT) |
           (static_cast<uint64_t>(descriptor.offsetHigh)
            << OS_TEST_KERNEL_DESCRIPTOR_GATE_HIGH_SHIFT);
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SUITE_NAME};
    uint64_t randomState = OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SEED;

    for (uint64_t iteration = 0ULL; iteration < OS_TEST_KERNEL_DESCRIPTOR_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t address = nextRandom(randomState);
        const os::kernel::SystemSegmentDescriptor taskStateDescriptor =
            os::kernel::createTaskStateSegmentDescriptor(address,
                                                         OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT);
        testContext.expectRandom(decodeTaskStateSegmentBase(taskStateDescriptor) == address,
                                 OS_TEST_KERNEL_DESCRIPTOR_RANDOM_TSS_MESSAGE,
                                 OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SEED, iteration);

        const os::kernel::InterruptGateDescriptor interruptGate =
            os::kernel::createInterruptGateDescriptor(
                address, OS_TEST_KERNEL_DESCRIPTOR_GATE_SELECTOR,
                OS_TEST_KERNEL_DESCRIPTOR_GATE_IST, OS_TEST_KERNEL_DESCRIPTOR_GATE_ATTRIBUTES);
        testContext.expectRandom(decodeInterruptGateAddress(interruptGate) == address,
                                 OS_TEST_KERNEL_DESCRIPTOR_RANDOM_GATE_MESSAGE,
                                 OS_TEST_KERNEL_DESCRIPTOR_RANDOM_SEED, iteration);
    }

    return testContext.exitCode();
}
