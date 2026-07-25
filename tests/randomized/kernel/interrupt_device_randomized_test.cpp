#include "os/kernel/device_model.hpp"
#include "test_context.hpp"

#include <cstdint>
#include <random>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_INTERRUPT_RANDOM_SUITE_NAME =
    "kernel/interrupt_device/randomized";
constexpr std::string_view OS_TEST_INTERRUPT_RANDOM_PIC_ROUND_TRIP =
    "随机 PIC IRQ 必须与重映射向量往返一致";
constexpr std::string_view OS_TEST_INTERRUPT_RANDOM_PIT_CONFIGURATION =
    "随机可表示 PIT 频率必须产生合法 16 位分频";
constexpr std::string_view OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SEQUENCE =
    "随机支持键的 make/break 序列必须保持按键身份";
constexpr os::test::RandomSeed OS_TEST_INTERRUPT_RANDOM_SEED = 0x1A7E22D3C4B5A697ULL;
constexpr os::test::TestCount OS_TEST_INTERRUPT_RANDOM_ITERATION_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_INTERRUPT_RANDOM_MINIMUM_IRQ = 0ULL;
constexpr uint64_t OS_TEST_INTERRUPT_RANDOM_FIRST_KEY_SELECTION = 0ULL;
constexpr uint64_t OS_TEST_INTERRUPT_RANDOM_LAST_KEY_SELECTION = 1ULL;
constexpr uint64_t OS_TEST_INTERRUPT_RANDOM_MAXIMUM_IRQ = 15ULL;
constexpr uint64_t OS_TEST_INTERRUPT_RANDOM_MINIMUM_PIT_FREQUENCY_HZ = 20ULL;
constexpr uint64_t OS_TEST_INTERRUPT_RANDOM_MAXIMUM_PIT_FREQUENCY_HZ = 100000ULL;
constexpr uint8_t OS_TEST_INTERRUPT_RANDOM_KEYBOARD_A_MAKE = 0x1EU;
constexpr uint8_t OS_TEST_INTERRUPT_RANDOM_KEYBOARD_A_BREAK = 0x9EU;
constexpr uint8_t OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SPACE_MAKE = 0x39U;
constexpr uint8_t OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SPACE_BREAK = 0xB9U;

}

int main() {
    os::test::TestContext testContext{OS_TEST_INTERRUPT_RANDOM_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_INTERRUPT_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> irqDistribution{OS_TEST_INTERRUPT_RANDOM_MINIMUM_IRQ,
                                                            OS_TEST_INTERRUPT_RANDOM_MAXIMUM_IRQ};
    std::uniform_int_distribution<uint64_t> frequencyDistribution{
        OS_TEST_INTERRUPT_RANDOM_MINIMUM_PIT_FREQUENCY_HZ,
        OS_TEST_INTERRUPT_RANDOM_MAXIMUM_PIT_FREQUENCY_HZ};
    std::uniform_int_distribution<uint64_t> keyDistribution{
        OS_TEST_INTERRUPT_RANDOM_FIRST_KEY_SELECTION, OS_TEST_INTERRUPT_RANDOM_LAST_KEY_SELECTION};

    for (os::test::TestCount iteration = 0ULL; iteration < OS_TEST_INTERRUPT_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t expectedInterruptRequest = irqDistribution(generator);
        uint64_t vector = 0ULL;
        uint64_t actualInterruptRequest = UINT64_MAX;
        testContext.ExpectRandom(
            os::kernel::CalculateLegacyPicVector(expectedInterruptRequest, vector) ==
                    os::kernel::LegacyPicModelStatus::Succeeded &&
                os::kernel::CalculateLegacyPicInterruptRequest(vector, actualInterruptRequest) ==
                    os::kernel::LegacyPicModelStatus::Succeeded &&
                actualInterruptRequest == expectedInterruptRequest,
            OS_TEST_INTERRUPT_RANDOM_PIC_ROUND_TRIP, OS_TEST_INTERRUPT_RANDOM_SEED, iteration);

        const uint64_t requestedFrequencyHz = frequencyDistribution(generator);
        os::kernel::PitConfiguration pitConfiguration{};
        testContext.ExpectRandom(
            os::kernel::CreatePitConfiguration(requestedFrequencyHz, pitConfiguration) ==
                    os::kernel::PitConfigurationStatus::Succeeded &&
                pitConfiguration.divisor != 0U && pitConfiguration.actualFrequencyHz != 0ULL,
            OS_TEST_INTERRUPT_RANDOM_PIT_CONFIGURATION, OS_TEST_INTERRUPT_RANDOM_SEED, iteration);

        const bool selectA =
            keyDistribution(generator) == OS_TEST_INTERRUPT_RANDOM_FIRST_KEY_SELECTION;
        const uint8_t makeCode = selectA ? OS_TEST_INTERRUPT_RANDOM_KEYBOARD_A_MAKE
                                         : OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SPACE_MAKE;
        const uint8_t breakCode = selectA ? OS_TEST_INTERRUPT_RANDOM_KEYBOARD_A_BREAK
                                          : OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SPACE_BREAK;
        const os::kernel::KeyboardKey expectedKey =
            selectA ? os::kernel::KeyboardKey::A : os::kernel::KeyboardKey::Space;
        os::kernel::ScanCodeSet1Decoder keyboardDecoder{};
        os::kernel::KeyboardEvent keyboardEvent{};
        const bool makeDecoded = keyboardDecoder.Decode(makeCode, keyboardEvent) ==
                                     os::kernel::KeyboardDecodeStatus::EventReady &&
                                 keyboardEvent.key == expectedKey && keyboardEvent.pressed;
        const bool breakDecoded = keyboardDecoder.Decode(breakCode, keyboardEvent) ==
                                      os::kernel::KeyboardDecodeStatus::EventReady &&
                                  keyboardEvent.key == expectedKey && !keyboardEvent.pressed;
        testContext.ExpectRandom(makeDecoded && breakDecoded,
                                 OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SEQUENCE,
                                 OS_TEST_INTERRUPT_RANDOM_SEED, iteration);
    }

    return testContext.ExitCode();
}
