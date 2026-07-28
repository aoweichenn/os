#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT = 16ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE = 32ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_PIC_SLAVE_VECTOR_BASE = 40ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ = 1193182ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_ATA_MAXIMUM_LBA28 = 0x0FFFFFFFULL;

enum class LegacyPicModelStatus : uint64_t {
    Succeeded,
    InvalidInterruptRequest,
    InvalidVector,
};

[[nodiscard]] LegacyPicModelStatus CalculateLegacyPicVector(uint64_t interrupt_request,
                                                            uint64_t &vector) noexcept;
[[nodiscard]] LegacyPicModelStatus
CalculateLegacyPicInterruptRequest(uint64_t vector, uint64_t &interrupt_request) noexcept;
[[nodiscard]] LegacyPicModelStatus EnableLegacyPicInterruptRequest(uint16_t current_mask,
                                                                   uint64_t interrupt_request,
                                                                   uint16_t &updated_mask) noexcept;

struct PitConfiguration final {
    uint16_t divisor;
    uint64_t requested_frequency_hz;
    uint64_t actual_frequency_hz;
};

enum class PitConfigurationStatus : uint64_t {
    Succeeded,
    InvalidFrequency,
    FrequencyOutOfRange,
};

[[nodiscard]] PitConfigurationStatus
CreatePitConfiguration(uint64_t requested_frequency_hz, PitConfiguration &configuration) noexcept;
[[nodiscard]] uint64_t CalculatePitElapsedMilliseconds(uint64_t tick_count,
                                                       uint16_t divisor) noexcept;

enum class KeyboardKey : uint64_t {
    Unknown,
    Escape,
    Backspace,
    Enter,
    Tab,
    Space,
    A,
    Printable,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    CapsLock,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
};

struct KeyboardEvent final {
    KeyboardKey key;
    uint8_t scan_code;
    uint8_t character;
    bool pressed;
    bool extended;
};

enum class KeyboardDecodeStatus : uint64_t {
    EventReady,
    AwaitingSequence,
    UnsupportedScanCode,
};

class ScanCodeSet1Decoder final {
  public:
    constexpr ScanCodeSet1Decoder() noexcept = default;

    [[nodiscard]] KeyboardDecodeStatus Decode(uint8_t scan_code, KeyboardEvent &event) noexcept;

  private:
    [[nodiscard]] KeyboardKey KeyForScanCode(uint8_t make_code, bool extended) const noexcept;
    [[nodiscard]] uint8_t CharacterForScanCode(uint8_t make_code) const noexcept;

    bool extended_prefix_pending_{false};
    bool left_shift_pressed_{false};
    bool right_shift_pressed_{false};
    bool left_control_pressed_{false};
    bool right_control_pressed_{false};
    bool caps_lock_enabled_{false};
};

enum class AtaReadRequestStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidLogicalBlockAddress,
};

[[nodiscard]] AtaReadRequestStatus ValidateAtaReadRequest(uint64_t logical_block_address,
                                                          const uint8_t *buffer,
                                                          uint64_t buffer_size_bytes) noexcept;
[[nodiscard]] bool Stage1BootDescriptorMagicMatches(const uint8_t *sector,
                                                    uint64_t sector_size_bytes) noexcept;
}
