#include "os/kernel/device_model.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DEVICE_BOOTSTRAP_SUITE_NAME =
    "kernel/device_bootstrap/integration";
constexpr std::string_view OS_TEST_DEVICE_BOOTSTRAP_PIC_LAYOUT =
    "PIC 重映射和启用顺序必须形成仅开放时钟与键盘的掩码";
constexpr std::string_view OS_TEST_DEVICE_BOOTSTRAP_PIT_CLOCK =
    "PIT 配置必须产生可用于启动时钟的单调毫秒";
constexpr std::string_view OS_TEST_DEVICE_BOOTSTRAP_KEYBOARD_STREAM =
    "键盘扫描码流必须跨 IRQ 保持扩展前缀状态";
constexpr std::string_view OS_TEST_DEVICE_BOOTSTRAP_DISK_CONTRACT =
    "内核 ATA 自检必须读取完整 LBA0 并识别启动描述符";

constexpr uint16_t OS_TEST_DEVICE_BOOTSTRAP_INITIAL_PIC_MASK = 0xFFFFU;
constexpr uint16_t OS_TEST_DEVICE_BOOTSTRAP_EXPECTED_PIC_MASK = 0xFFFCU;
constexpr uint64_t OS_TEST_DEVICE_BOOTSTRAP_TIMER_IRQ = 0ULL;
constexpr uint64_t OS_TEST_DEVICE_BOOTSTRAP_KEYBOARD_IRQ = 1ULL;
constexpr uint64_t OS_TEST_DEVICE_BOOTSTRAP_BOOT_DESCRIPTOR_LBA = 0ULL;
constexpr uint64_t OS_TEST_DEVICE_BOOTSTRAP_PIT_FREQUENCY_HZ = 1000ULL;
constexpr uint64_t OS_TEST_DEVICE_BOOTSTRAP_SELF_TEST_TICKS = 16ULL;
constexpr uint64_t OS_TEST_DEVICE_BOOTSTRAP_MINIMUM_ELAPSED_MILLISECONDS = 15ULL;
constexpr uint8_t OS_TEST_DEVICE_BOOTSTRAP_EXTENDED_PREFIX = 0xE0U;
constexpr uint8_t OS_TEST_DEVICE_BOOTSTRAP_ARROW_LEFT_MAKE = 0x4BU;
constexpr uint64_t OS_TEST_DEVICE_BOOTSTRAP_STAGE1_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint8_t
    OS_TEST_DEVICE_BOOTSTRAP_STAGE1_MAGIC[OS_TEST_DEVICE_BOOTSTRAP_STAGE1_MAGIC_SIZE_BYTES] = {
        static_cast<uint8_t>('O'), static_cast<uint8_t>('S'), static_cast<uint8_t>('S'),
        static_cast<uint8_t>('T'), static_cast<uint8_t>('A'), static_cast<uint8_t>('G'),
        static_cast<uint8_t>('E'), static_cast<uint8_t>('1'),
};

}

int main() {
    os::test::TestContext test_context{OS_TEST_DEVICE_BOOTSTRAP_SUITE_NAME};

    uint16_t pic_mask_after_timer = 0U;
    uint16_t pic_mask_after_keyboard = 0U;
    uint64_t keyboard_vector = 0ULL;
    test_context.Expect(
        os::kernel::EnableLegacyPicInterruptRequest(
            OS_TEST_DEVICE_BOOTSTRAP_INITIAL_PIC_MASK, OS_TEST_DEVICE_BOOTSTRAP_TIMER_IRQ,
            pic_mask_after_timer) == os::kernel::LegacyPicModelStatus::Succeeded &&
            os::kernel::EnableLegacyPicInterruptRequest(
                pic_mask_after_timer, OS_TEST_DEVICE_BOOTSTRAP_KEYBOARD_IRQ,
                pic_mask_after_keyboard) == os::kernel::LegacyPicModelStatus::Succeeded &&
            os::kernel::CalculateLegacyPicVector(OS_TEST_DEVICE_BOOTSTRAP_KEYBOARD_IRQ,
                                                 keyboard_vector) ==
                os::kernel::LegacyPicModelStatus::Succeeded &&
            pic_mask_after_keyboard == OS_TEST_DEVICE_BOOTSTRAP_EXPECTED_PIC_MASK &&
            keyboard_vector == os::kernel::OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE +
                                   OS_TEST_DEVICE_BOOTSTRAP_KEYBOARD_IRQ,
        OS_TEST_DEVICE_BOOTSTRAP_PIC_LAYOUT);

    os::kernel::PitConfiguration pit_configuration{};
    test_context.Expect(
        os::kernel::CreatePitConfiguration(OS_TEST_DEVICE_BOOTSTRAP_PIT_FREQUENCY_HZ,
                                           pit_configuration) ==
                os::kernel::PitConfigurationStatus::Succeeded &&
            os::kernel::CalculatePitElapsedMilliseconds(OS_TEST_DEVICE_BOOTSTRAP_SELF_TEST_TICKS,
                                                        pit_configuration.divisor) >=
                OS_TEST_DEVICE_BOOTSTRAP_MINIMUM_ELAPSED_MILLISECONDS,
        OS_TEST_DEVICE_BOOTSTRAP_PIT_CLOCK);

    os::kernel::ScanCodeSet1Decoder keyboard_decoder{};
    os::kernel::KeyboardEvent keyboard_event{};
    test_context.Expect(
        keyboard_decoder.Decode(OS_TEST_DEVICE_BOOTSTRAP_EXTENDED_PREFIX, keyboard_event) ==
                os::kernel::KeyboardDecodeStatus::AwaitingSequence &&
            keyboard_decoder.Decode(OS_TEST_DEVICE_BOOTSTRAP_ARROW_LEFT_MAKE, keyboard_event) ==
                os::kernel::KeyboardDecodeStatus::EventReady &&
            keyboard_event.key == os::kernel::KeyboardKey::ArrowLeft && keyboard_event.extended &&
            keyboard_event.pressed,
        OS_TEST_DEVICE_BOOTSTRAP_KEYBOARD_STREAM);

    uint8_t boot_descriptor_sector[os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES]{};
    for (uint64_t index = 0ULL; index < OS_TEST_DEVICE_BOOTSTRAP_STAGE1_MAGIC_SIZE_BYTES; ++index) {
        boot_descriptor_sector[index] = OS_TEST_DEVICE_BOOTSTRAP_STAGE1_MAGIC[index];
    }
    test_context.Expect(
        os::kernel::ValidateAtaReadRequest(OS_TEST_DEVICE_BOOTSTRAP_BOOT_DESCRIPTOR_LBA,
                                           boot_descriptor_sector,
                                           os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) ==
                os::kernel::AtaReadRequestStatus::Succeeded &&
            os::kernel::Stage1BootDescriptorMagicMatches(
                boot_descriptor_sector, os::kernel::OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES),
        OS_TEST_DEVICE_BOOTSTRAP_DISK_CONTRACT);

    return test_context.ExitCode();
}
