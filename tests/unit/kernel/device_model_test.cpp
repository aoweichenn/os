#include "os/kernel/device_model.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DEVICE_MODEL_SUITE_NAME = "kernel/device_model/unit";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIC_VECTOR_ROUND_TRIP =
    "PIC 的 IRQ 与重映射向量必须双向一致";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIC_INVALID = "非法 PIC 向量必须失败且不修改输出";
constexpr std::string_view OS_TEST_DEVICE_MODEL_PIC_MASK = "启用 IRQ0 和 IRQ1 只能清除对应屏蔽位";
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
constexpr std::string_view OS_TEST_DEVICE_MODEL_KEYBOARD_UNSUPPORTED = "未知扫描码必须返回明确状态";
constexpr std::string_view OS_TEST_DEVICE_MODEL_ATA_REQUEST =
    "ATA 读取请求必须校验指针、扇区长度和 LBA28";
constexpr std::string_view OS_TEST_DEVICE_MODEL_ATA_MAGIC =
    "ATA 读取的 LBA0 必须识别 Stage 1 描述符 magic";

constexpr uint64_t OS_TEST_DEVICE_MODEL_MASTER_TIMER_IRQ = 0ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_MASTER_KEYBOARD_IRQ = 1ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_SLAVE_LAST_IRQ = 15ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_INVALID_IRQ = 16ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_EXPECTED_TIMER_VECTOR = 32ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_EXPECTED_LAST_VECTOR = 47ULL;
constexpr uint64_t OS_TEST_DEVICE_MODEL_UNCHANGED_VALUE = 0xA5A5A5A5A5A5A5A5ULL;
constexpr uint16_t OS_TEST_DEVICE_MODEL_INITIAL_PIC_MASK = 0xFFFFU;
constexpr uint16_t OS_TEST_DEVICE_MODEL_TIMER_KEYBOARD_PIC_MASK = 0xFFFCU;
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
constexpr uint8_t OS_TEST_DEVICE_MODEL_KEYBOARD_UNSUPPORTED_CODE = 0x02U;
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
    os::test::TestContext testContext{OS_TEST_DEVICE_MODEL_SUITE_NAME};

    uint64_t timerVector = 0ULL;
    uint64_t lastVector = 0ULL;
    uint64_t roundTripInterruptRequest = 0ULL;
    testContext.expect(
        os::kernel::calculateLegacyPicVector(OS_TEST_DEVICE_MODEL_MASTER_TIMER_IRQ, timerVector) ==
                os::kernel::LegacyPicModelStatus::Succeeded &&
            os::kernel::calculateLegacyPicVector(OS_TEST_DEVICE_MODEL_SLAVE_LAST_IRQ, lastVector) ==
                os::kernel::LegacyPicModelStatus::Succeeded &&
            timerVector == OS_TEST_DEVICE_MODEL_EXPECTED_TIMER_VECTOR &&
            lastVector == OS_TEST_DEVICE_MODEL_EXPECTED_LAST_VECTOR &&
            os::kernel::calculateLegacyPicInterruptRequest(lastVector, roundTripInterruptRequest) ==
                os::kernel::LegacyPicModelStatus::Succeeded &&
            roundTripInterruptRequest == OS_TEST_DEVICE_MODEL_SLAVE_LAST_IRQ,
        OS_TEST_DEVICE_MODEL_PIC_VECTOR_ROUND_TRIP);

    uint64_t unchangedVector = OS_TEST_DEVICE_MODEL_UNCHANGED_VALUE;
    testContext.expect(
        os::kernel::calculateLegacyPicVector(OS_TEST_DEVICE_MODEL_INVALID_IRQ, unchangedVector) ==
                os::kernel::LegacyPicModelStatus::InvalidInterruptRequest &&
            unchangedVector == OS_TEST_DEVICE_MODEL_UNCHANGED_VALUE,
        OS_TEST_DEVICE_MODEL_PIC_INVALID);

    uint16_t timerEnabledMask = 0U;
    uint16_t timerKeyboardEnabledMask = 0U;
    testContext.expect(
        os::kernel::enableLegacyPicInterruptRequest(
            OS_TEST_DEVICE_MODEL_INITIAL_PIC_MASK, OS_TEST_DEVICE_MODEL_MASTER_TIMER_IRQ,
            timerEnabledMask) == os::kernel::LegacyPicModelStatus::Succeeded &&
            os::kernel::enableLegacyPicInterruptRequest(
                timerEnabledMask, OS_TEST_DEVICE_MODEL_MASTER_KEYBOARD_IRQ,
                timerKeyboardEnabledMask) == os::kernel::LegacyPicModelStatus::Succeeded &&
            timerKeyboardEnabledMask == OS_TEST_DEVICE_MODEL_TIMER_KEYBOARD_PIC_MASK,
        OS_TEST_DEVICE_MODEL_PIC_MASK);

    os::kernel::PitConfiguration pitConfiguration{};
    testContext.expect(os::kernel::createPitConfiguration(
                           OS_TEST_DEVICE_MODEL_PIT_TARGET_FREQUENCY_HZ, pitConfiguration) ==
                               os::kernel::PitConfigurationStatus::Succeeded &&
                           pitConfiguration.divisor == OS_TEST_DEVICE_MODEL_EXPECTED_PIT_DIVISOR &&
                           pitConfiguration.actualFrequencyHz ==
                               OS_TEST_DEVICE_MODEL_EXPECTED_PIT_ACTUAL_FREQUENCY_HZ,
                       OS_TEST_DEVICE_MODEL_PIT_CONFIGURATION);
    testContext.expect(
        os::kernel::createPitConfiguration(OS_TEST_DEVICE_MODEL_INVALID_PIT_ZERO_FREQUENCY_HZ,
                                           pitConfiguration) ==
                os::kernel::PitConfigurationStatus::InvalidFrequency &&
            os::kernel::createPitConfiguration(OS_TEST_DEVICE_MODEL_INVALID_PIT_LOW_FREQUENCY_HZ,
                                               pitConfiguration) ==
                os::kernel::PitConfigurationStatus::FrequencyOutOfRange,
        OS_TEST_DEVICE_MODEL_PIT_REJECTS_RANGE);
    testContext.expect(
        os::kernel::calculatePitElapsedMilliseconds(OS_TEST_DEVICE_MODEL_ELAPSED_TICK_COUNT,
                                                    OS_TEST_DEVICE_MODEL_EXPECTED_PIT_DIVISOR) ==
            OS_TEST_DEVICE_MODEL_EXPECTED_ELAPSED_MILLISECONDS,
        OS_TEST_DEVICE_MODEL_PIT_ELAPSED_TIME);

    os::kernel::ScanCodeSet1Decoder keyboardDecoder{};
    os::kernel::KeyboardEvent keyboardEvent{};
    const bool decodedA =
        keyboardDecoder.decode(OS_TEST_DEVICE_MODEL_KEYBOARD_A_MAKE, keyboardEvent) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboardEvent.key == os::kernel::KeyboardKey::A && keyboardEvent.pressed &&
        !keyboardEvent.extended;
    const bool decodedARelease =
        keyboardDecoder.decode(OS_TEST_DEVICE_MODEL_KEYBOARD_A_BREAK, keyboardEvent) ==
            os::kernel::KeyboardDecodeStatus::EventReady &&
        keyboardEvent.key == os::kernel::KeyboardKey::A && !keyboardEvent.pressed;
    testContext.expect(decodedA && decodedARelease, OS_TEST_DEVICE_MODEL_KEYBOARD_A);
    testContext.expect(
        keyboardDecoder.decode(OS_TEST_DEVICE_MODEL_KEYBOARD_EXTENDED_PREFIX, keyboardEvent) ==
                os::kernel::KeyboardDecodeStatus::AwaitingSequence &&
            keyboardDecoder.decode(OS_TEST_DEVICE_MODEL_KEYBOARD_ARROW_UP_MAKE, keyboardEvent) ==
                os::kernel::KeyboardDecodeStatus::EventReady &&
            keyboardEvent.key == os::kernel::KeyboardKey::ArrowUp && keyboardEvent.pressed &&
            keyboardEvent.extended,
        OS_TEST_DEVICE_MODEL_KEYBOARD_EXTENDED);
    testContext.expect(
        keyboardDecoder.decode(OS_TEST_DEVICE_MODEL_KEYBOARD_UNSUPPORTED_CODE, keyboardEvent) ==
            os::kernel::KeyboardDecodeStatus::UnsupportedScanCode,
        OS_TEST_DEVICE_MODEL_KEYBOARD_UNSUPPORTED);

    uint8_t ataSector[os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES]{};
    testContext.expect(
        os::kernel::validateAtaReadRequest(OS_TEST_DEVICE_MODEL_ATA_VALID_LBA, ataSector,
                                           os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::Succeeded &&
            os::kernel::validateAtaReadRequest(
                OS_TEST_DEVICE_MODEL_ATA_INVALID_LBA, ataSector,
                os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::InvalidLogicalBlockAddress &&
            os::kernel::validateAtaReadRequest(OS_TEST_DEVICE_MODEL_ATA_VALID_LBA, ataSector,
                                               OS_TEST_DEVICE_MODEL_ATA_INVALID_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::InvalidBufferSize &&
            os::kernel::validateAtaReadRequest(
                OS_TEST_DEVICE_MODEL_ATA_VALID_LBA, nullptr,
                os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::NullBuffer,
        OS_TEST_DEVICE_MODEL_ATA_REQUEST);

    for (uint64_t index = 0ULL; index < OS_TEST_DEVICE_MODEL_STAGE1_MAGIC_SIZE_BYTES; ++index) {
        ataSector[index] = OS_TEST_DEVICE_MODEL_STAGE1_MAGIC[index];
    }
    const bool validMagic = os::kernel::stage1BootDescriptorMagicMatches(
        ataSector, os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES);
    ataSector[0] = static_cast<uint8_t>('X');
    testContext.expect(validMagic &&
                           !os::kernel::stage1BootDescriptorMagicMatches(
                               ataSector, os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES),
                       OS_TEST_DEVICE_MODEL_ATA_MAGIC);

    return testContext.exitCode();
}
