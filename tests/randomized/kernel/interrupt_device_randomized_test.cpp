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
constexpr uint8_t OS_TEST_INTERRUPT_RANDOM_KEYBOARD_A_CHARACTER = static_cast<uint8_t>('a');
constexpr uint8_t OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SPACE_CHARACTER = static_cast<uint8_t>(' ');
constexpr uint8_t OS_TEST_INTERRUPT_RANDOM_KEYBOARD_NO_CHARACTER = 0U;

}

int main() {
    os::test::TestContext test_context{OS_TEST_INTERRUPT_RANDOM_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_INTERRUPT_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> irq_distribution{OS_TEST_INTERRUPT_RANDOM_MINIMUM_IRQ,
                                                             OS_TEST_INTERRUPT_RANDOM_MAXIMUM_IRQ};
    std::uniform_int_distribution<uint64_t> frequency_distribution{
        OS_TEST_INTERRUPT_RANDOM_MINIMUM_PIT_FREQUENCY_HZ,
        OS_TEST_INTERRUPT_RANDOM_MAXIMUM_PIT_FREQUENCY_HZ};
    std::uniform_int_distribution<uint64_t> key_distribution{
        OS_TEST_INTERRUPT_RANDOM_FIRST_KEY_SELECTION, OS_TEST_INTERRUPT_RANDOM_LAST_KEY_SELECTION};

    for (os::test::TestCount iteration = 0ULL; iteration < OS_TEST_INTERRUPT_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t expected_interrupt_request = irq_distribution(generator);
        uint64_t vector = 0ULL;
        uint64_t actual_interrupt_request = UINT64_MAX;
        test_context.ExpectRandom(
            os::kernel::CalculateLegacyPicVector(expected_interrupt_request, vector) ==
                    os::kernel::LegacyPicModelStatus::Succeeded &&
                os::kernel::CalculateLegacyPicInterruptRequest(vector, actual_interrupt_request) ==
                    os::kernel::LegacyPicModelStatus::Succeeded &&
                actual_interrupt_request == expected_interrupt_request,
            OS_TEST_INTERRUPT_RANDOM_PIC_ROUND_TRIP, OS_TEST_INTERRUPT_RANDOM_SEED, iteration);

        const uint64_t requested_frequency_hz = frequency_distribution(generator);
        os::kernel::PitConfiguration pit_configuration{};
        test_context.ExpectRandom(
            os::kernel::CreatePitConfiguration(requested_frequency_hz, pit_configuration) ==
                    os::kernel::PitConfigurationStatus::Succeeded &&
                pit_configuration.divisor != 0U && pit_configuration.actual_frequency_hz != 0ULL,
            OS_TEST_INTERRUPT_RANDOM_PIT_CONFIGURATION, OS_TEST_INTERRUPT_RANDOM_SEED, iteration);

        const bool select_a =
            key_distribution(generator) == OS_TEST_INTERRUPT_RANDOM_FIRST_KEY_SELECTION;
        const uint8_t make_code = select_a ? OS_TEST_INTERRUPT_RANDOM_KEYBOARD_A_MAKE
                                           : OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SPACE_MAKE;
        const uint8_t break_code = select_a ? OS_TEST_INTERRUPT_RANDOM_KEYBOARD_A_BREAK
                                            : OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SPACE_BREAK;
        const os::kernel::KeyboardKey expected_key =
            select_a ? os::kernel::KeyboardKey::A : os::kernel::KeyboardKey::Space;
        const uint8_t expected_character = select_a
                                               ? OS_TEST_INTERRUPT_RANDOM_KEYBOARD_A_CHARACTER
                                               : OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SPACE_CHARACTER;
        os::kernel::ScanCodeSet1Decoder keyboard_decoder{};
        os::kernel::KeyboardEvent keyboard_event{};
        const bool make_decoded = keyboard_decoder.Decode(make_code, keyboard_event) ==
                                      os::kernel::KeyboardDecodeStatus::EventReady &&
                                  keyboard_event.key == expected_key && keyboard_event.pressed;
        const bool make_character_matches = keyboard_event.character == expected_character;
        const bool break_decoded = keyboard_decoder.Decode(break_code, keyboard_event) ==
                                       os::kernel::KeyboardDecodeStatus::EventReady &&
                                   keyboard_event.key == expected_key && !keyboard_event.pressed;
        const bool break_character_matches =
            keyboard_event.character == OS_TEST_INTERRUPT_RANDOM_KEYBOARD_NO_CHARACTER;
        test_context.ExpectRandom(
            make_decoded && make_character_matches && break_decoded && break_character_matches,
            OS_TEST_INTERRUPT_RANDOM_KEYBOARD_SEQUENCE, OS_TEST_INTERRUPT_RANDOM_SEED, iteration);
    }

    return test_context.ExitCode();
}
