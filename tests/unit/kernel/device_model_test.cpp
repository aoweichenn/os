#include "os/kernel/device/device_model.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DEVICE_MODEL_SUITE_NAME = "kernel/device_model/unit";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIC_VECTOR_ROUND_TRIP =
    "PIC 的 IRQ 与重映射向量必须双向一致";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIC_INVALID = "非法 PIC 向量必须失败且不修改输出";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIC_MASK = "启用 IRQ0 和 IRQ1 只能清除对应屏蔽位";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIC_SLAVE_CASCADE =
    "启用从片 IRQ14 必须同时开放主片 IRQ2 级联线";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIT_CONFIGURATION =
    "PIT 目标频率必须转换为可表示的硬件分频值";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIT_REJECTS_RANGE =
    "PIT 必须拒绝零频率和超出 16 位分频范围的频率";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIT_ELAPSED_TIME =
    "PIT tick 必须按实际分频换算单调毫秒";
constexpr std::string_view OS_TEST_DEVICE_MODEL_KEYBOARD_A =
    "Set 1 字母 A 的按下与释放必须正确解码";
constexpr std::string_view OS_TEST_DEVICE_MODEL_KEYBOARD_EXTENDED =
    "Set 1 扩展方向键必须跨前缀保存状态";
constexpr std::string_view OS_TEST_DEVICE_MODEL_KEYBOARD_CAPS_PUNCTUATION =
    "Caps Lock 只能改变字母，标点必须仅由 Shift 改变";
constexpr std::string_view OS_TEST_DEVICE_MODEL_KEYBOARD_CONTROL_CHARACTERS =
    "左右 Ctrl 与字母扫描码必须生成对应 C0 控制字符并在释放后恢复";
constexpr std::string_view OS_TEST_DEVICE_MODEL_KEYBOARD_UNSUPPORTED = "未知扫描码必须返回明确状态";
constexpr std::string_view OS_TEST_DEVICE_MODEL_ATA_REQUEST =
    "ATA 读取请求必须校验指针、扇区长度和 LBA28";
constexpr std::string_view OS_TEST_DEVICE_MODEL_ATA_MAGIC =
    "ATA 读取的 LBA0 必须识别 Stage 1 描述符 magic";

constexpr uint64_t OS_TEST_DEVICE_MODEL_MASTER_TIMER_IRQ = 0ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_MASTER_KEYBOARD_IRQ = 1ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_SLAVE_ATA_IRQ = 14ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_SLAVE_LAST_IRQ = 15ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_INVALID_IRQ = 16ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_EXPECTED_TIMER_VECTOR = 32ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_EXPECTED_LAST_VECTOR = 47ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_UNCHANGED_VALUE = 0xA5A5A5A5A5A5A5A5ULL;
constexpr uint16_t OS_TEST_DEVICE_MODEL_INITIAL_PIC_MASK = 0xFFFFU;
constexpr uint16_t OS_TEST_DEVICE_MODEL_TIMER_KEYBOARD_PIC_MASK = 0xFFFCU;
constexpr uint16_t OS_TEST_DEVICE_MODEL_ATA_PIC_MASK = 0xBFFBU;
constexpr uint64_t OS_TEST_DEVICE_MODEL_PIT_TARGET_FREQUENCY_HZ = 1000ULL;
constexpr uint16_t OS_TEST_DEVICE_MODEL_EXPECTED_PIT_DIVISOR = 1193U;
constexpr uint64_t OS_TEST_DEVICE_MODEL_EXPECTED_PIT_ACTUAL_FREQUENCY_HZ = 1000ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_INVALID_PIT_ZERO_FREQUENCY_HZ = 0ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_INVALID_PIT_LOW_FREQUENCY_HZ = 1ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_ELAPSED_TICK_COUNT = 1000ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_EXPECTED_ELAPSED_MILLISECONDS = 999ULL;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_A_MAKE = 0x1EU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_A_BREAK = 0x9EU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_EXTENDED_PREFIX = 0xE0U;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_ARROW_UP_MAKE = 0x48U;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_UNSUPPORTED_CODE = 0x3BU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_CAPS_LOCK_MAKE = 0x3AU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_LEFT_SHIFT_MAKE = 0x2AU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_LEFT_CONTROL_MAKE = 0x1DU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_LEFT_CONTROL_BREAK = 0x9DU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_RIGHT_CONTROL_MAKE = 0x1DU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_RIGHT_CONTROL_BREAK = 0x9DU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_C_MAKE = 0x2EU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_Z_MAKE = 0x2CU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_SEMICOLON_MAKE = 0x27U;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_A_CHARACTER = static_cast<uint8_t>('a');
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_SEMICOLON_CHARACTER = static_cast<uint8_t>(';');
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_COLON_CHARACTER = static_cast<uint8_t>(':');
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_INTERRUPT_CHARACTER = 0x03U;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_STOP_CHARACTER = 0x1AU;
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_NO_CHARACTER = 0U;
constexpr uint64_t OS_TEST_DEVICE_MODEL_ATA_VALID_LBA = 0x00000042ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_ATA_INVALID_LBA = 0x10000000ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_ATA_INVALID_SIZE_BYTES = 511ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_STAGE1_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint8_t OS_TEST_DEVICE_MODEL_STAGE1_MAGIC[OS_TEST_DEVICE_MODEL_STAGE1_MAGIC_SIZE_BYTES] =
    {
        static_cast<uint8_t>('O'), static_cast<uint8_t>('S'), static_cast<uint8_t>('S'),
        static_cast<uint8_t>('T'), static_cast<uint8_t>('A'), static_cast<uint8_t>('G'),
        static_cast<uint8_t>('E'), static_cast<uint8_t>('1'),
};

}

int main() {
    os::test::TestContext test_context{OS_TEST_DEVICE_MODEL_SUITE_NAME};

    uint64_t timer_vector = 0ULL;
    uint64_t last_vector = 0ULL;
    uint64_t round_trip_interrupt_request = 0ULL;
    test_context.Expect(
        os::kernel::CalculateLegacyPicVector(OS_TEST_DEVICE_MODEL_MASTER_TIMER_IRQ, timer_vector) ==
                os::kernel::LegacyPicModelStatus::Succeeded &&
            os::kernel::CalculateLegacyPicVector(OS_TEST_DEVICE_MODEL_SLAVE_LAST_IRQ,
                                                 last_vector) ==
                os::kernel::LegacyPicModelStatus::Succeeded &&
            timer_vector == OS_TEST_DEVICE_MODEL_EXPECTED_TIMER_VECTOR &&
            last_vector == OS_TEST_DEVICE_MODEL_EXPECTED_LAST_VECTOR &&
            os::kernel::CalculateLegacyPicInterruptRequest(last_vector,
                                                           round_trip_interrupt_request) ==
                os::kernel::LegacyPicModelStatus::Succeeded &&
            round_trip_interrupt_request == OS_TEST_DEVICE_MODEL_SLAVE_LAST_IRQ,
        OS_TEST_DEVICE_MODEL_PIC_VECTOR_ROUND_TRIP);

    uint64_t unchanged_vector = OS_TEST_DEVICE_MODEL_UNCHANGED_VALUE;
    test_context.Expect(
        os::kernel::CalculateLegacyPicVector(OS_TEST_DEVICE_MODEL_INVALID_IRQ, unchanged_vector) ==
                os::kernel::LegacyPicModelStatus::InvalidInterruptRequest &&
            unchanged_vector == OS_TEST_DEVICE_MODEL_UNCHANGED_VALUE,
        OS_TEST_DEVICE_MODEL_PIC_INVALID);

    uint16_t timer_enabled_mask = 0U;
    uint16_t timer_keyboard_enabled_mask = 0U;
    test_context.Expect(
        os::kernel::EnableLegacyPicInterruptRequest(
            OS_TEST_DEVICE_MODEL_INITIAL_PIC_MASK, OS_TEST_DEVICE_MODEL_MASTER_TIMER_IRQ,
            timer_enabled_mask) == os::kernel::LegacyPicModelStatus::Succeeded &&
            os::kernel::EnableLegacyPicInterruptRequest(
                timer_enabled_mask, OS_TEST_DEVICE_MODEL_MASTER_KEYBOARD_IRQ,
                timer_keyboard_enabled_mask) == os::kernel::LegacyPicModelStatus::Succeeded &&
            timer_keyboard_enabled_mask == OS_TEST_DEVICE_MODEL_TIMER_KEYBOARD_PIC_MASK,
        OS_TEST_DEVICE_MODEL_PIC_MASK);

    uint16_t ata_enabled_mask = 0U;
    test_context.Expect(
        os::kernel::EnableLegacyPicInterruptRequest(
            OS_TEST_DEVICE_MODEL_INITIAL_PIC_MASK,
            OS_TEST_DEVICE_MODEL_SLAVE_ATA_IRQ,
            ata_enabled_mask) ==
                os::kernel::LegacyPicModelStatus::Succeeded &&
            ata_enabled_mask == OS_TEST_DEVICE_MODEL_ATA_PIC_MASK,
        OS_TEST_DEVICE_MODEL_PIC_SLAVE_CASCADE);

    os::kernel::PitConfiguration pit_configuration{};
    test_context.Expect(
        os::kernel::CreatePitConfiguration(OS_TEST_DEVICE_MODEL_PIT_TARGET_FREQUENCY_HZ,
                                           pit_configuration) ==
                os::kernel::PitConfigurationStatus::Succeeded &&
            pit_configuration.divisor == OS_TEST_DEVICE_MODEL_EXPECTED_PIT_DIVISOR &&
            pit_configuration.actual_frequency_hz ==
                OS_TEST_DEVICE_MODEL_EXPECTED_PIT_ACTUAL_FREQUENCY_HZ,
        OS_TEST_DEVICE_MODEL_PIT_CONFIGURATION);
    test_context.Expect(
        os::kernel::CreatePitConfiguration(OS_TEST_DEVICE_MODEL_INVALID_PIT_ZERO_FREQUENCY_HZ,
                                           pit_configuration) ==
                os::kernel::PitConfigurationStatus::InvalidFrequency &&
            os::kernel::CreatePitConfiguration(OS_TEST_DEVICE_MODEL_INVALID_PIT_LOW_FREQUENCY_HZ,
                                               pit_configuration) ==
                os::kernel::PitConfigurationStatus::FrequencyOutOfRange,
        OS_TEST_DEVICE_MODEL_PIT_REJECTS_RANGE);
    test_context.Expect(
        os::kernel::CalculatePitElapsedMilliseconds(OS_TEST_DEVICE_MODEL_ELAPSED_TICK_COUNT,
                                                    OS_TEST_DEVICE_MODEL_EXPECTED_PIT_DIVISOR) ==
            OS_TEST_DEVICE_MODEL_EXPECTED_ELAPSED_MILLISECONDS,
        OS_TEST_DEVICE_MODEL_PIT_ELAPSED_TIME);

    os::kernel::ScanCodeSet1Decoder keyboard_decoder{};
    os::kernel::KeyboardEvent keyboard_event{};
    const bool decoded_a =
        keyboard_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_A_MAKE, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboard_event.key == os::kernel::KeyboardKey::A && keyboard_event.pressed &&
        keyboard_event.character == OS_TEST_DEVICE_MODEL_KEYBOARD_A_CHARACTER &&
        !keyboard_event.extended;
    const bool decoded_a_release =
        keyboard_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_A_BREAK, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboard_event.key == os::kernel::KeyboardKey::A &&
        keyboard_event.character == OS_TEST_DEVICE_MODEL_KEYBOARD_NO_CHARACTER &&
        !keyboard_event.pressed;
    test_context.Expect(decoded_a && decoded_a_release, OS_TEST_DEVICE_MODEL_KEYBOARD_A);
    test_context.Expect(
        keyboard_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_EXTENDED_PREFIX, keyboard_event) ==
                os::kernel::KeyboardDecodeStatus::AwaitingSequence &&
            keyboard_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_ARROW_UP_MAKE, keyboard_event) ==
                os::kernel::KeyboardDecodeStatus::EventReady &&
            keyboard_event.key == os::kernel::KeyboardKey::ArrowUp && keyboard_event.pressed &&
            keyboard_event.extended,
        OS_TEST_DEVICE_MODEL_KEYBOARD_EXTENDED);
    os::kernel::ScanCodeSet1Decoder modifier_decoder{};
    const bool caps_lock_preserves_punctuation =
        modifier_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_CAPS_LOCK_MAKE, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        modifier_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_SEMICOLON_MAKE, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboard_event.character == OS_TEST_DEVICE_MODEL_KEYBOARD_SEMICOLON_CHARACTER;
    const bool shift_changes_punctuation =
        modifier_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_LEFT_SHIFT_MAKE, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        modifier_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_SEMICOLON_MAKE, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboard_event.character == OS_TEST_DEVICE_MODEL_KEYBOARD_COLON_CHARACTER;
    test_context.Expect(caps_lock_preserves_punctuation && shift_changes_punctuation,
                        OS_TEST_DEVICE_MODEL_KEYBOARD_CAPS_PUNCTUATION);
    os::kernel::ScanCodeSet1Decoder control_decoder{};
    const bool left_control_stop =
        control_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_LEFT_CONTROL_MAKE,
                               keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboard_event.key == os::kernel::KeyboardKey::LeftControl &&
        control_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_Z_MAKE, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboard_event.character == OS_TEST_DEVICE_MODEL_KEYBOARD_STOP_CHARACTER &&
        control_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_LEFT_CONTROL_BREAK,
                               keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady;
    const bool right_control_interrupt =
        control_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_EXTENDED_PREFIX,
                               keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::AwaitingSequence &&
        control_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_RIGHT_CONTROL_MAKE,
                               keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboard_event.key == os::kernel::KeyboardKey::RightControl &&
        control_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_C_MAKE, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboard_event.character == OS_TEST_DEVICE_MODEL_KEYBOARD_INTERRUPT_CHARACTER &&
        control_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_EXTENDED_PREFIX,
                               keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::AwaitingSequence &&
        control_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_RIGHT_CONTROL_BREAK,
                               keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::EventReady;
    test_context.Expect(left_control_stop && right_control_interrupt,
                        OS_TEST_DEVICE_MODEL_KEYBOARD_CONTROL_CHARACTERS);
    test_context.Expect(
        keyboard_decoder.Decode(OS_TEST_DEVICE_MODEL_KEYBOARD_UNSUPPORTED_CODE, keyboard_event) ==
            os::kernel::KeyboardDecodeStatus::UnsupportedScanCode,
        OS_TEST_DEVICE_MODEL_KEYBOARD_UNSUPPORTED);

    uint8_t ata_sector[os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES]{};
    test_context.Expect(
        os::kernel::ValidateAtaReadRequest(OS_TEST_DEVICE_MODEL_ATA_VALID_LBA, ata_sector,
                                           os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::Succeeded &&
            os::kernel::ValidateAtaReadRequest(
                OS_TEST_DEVICE_MODEL_ATA_INVALID_LBA, ata_sector,
                os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::InvalidLogicalBlockAddress &&
            os::kernel::ValidateAtaReadRequest(OS_TEST_DEVICE_MODEL_ATA_VALID_LBA, ata_sector,
                                               OS_TEST_DEVICE_MODEL_ATA_INVALID_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::InvalidBufferSize &&
            os::kernel::ValidateAtaReadRequest(
                OS_TEST_DEVICE_MODEL_ATA_VALID_LBA, nullptr,
                os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::NullBuffer,
        OS_TEST_DEVICE_MODEL_ATA_REQUEST);

    for (uint64_t index = 0ULL; index < OS_TEST_DEVICE_MODEL_STAGE1_MAGIC_SIZE_BYTES; ++index) {
        ata_sector[index] = OS_TEST_DEVICE_MODEL_STAGE1_MAGIC[index];
    }
    const bool valid_magic = os::kernel::Stage1BootDescriptorMagicMatches(
        ata_sector, os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES);
    ata_sector[0] = static_cast<uint8_t>('X');
    test_context.Expect(valid_magic &&
                            !os::kernel::Stage1BootDescriptorMagicMatches(
                                ata_sector, os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES),
                        OS_TEST_DEVICE_MODEL_ATA_MAGIC);

    return test_context.ExitCode();
}
