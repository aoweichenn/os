#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT = 16ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE = 32ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_PIC_SLAVE_VECTOR_BASE = 40ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ = 1193182ULL;
inline constexpr uint64_t OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES = 512ULL;

enum class LegacyPicModelStatus : uint64_t {
    Succeeded,
    InvalidInterruptRequest,
    InvalidVector,
};

[[nodiscard]] LegacyPicModelStatus CalculateLegacyPicVector(uint64_t interruptRequest,
                                                            uint64_t &vector) noexcept;
[[nodiscard]] LegacyPicModelStatus
CalculateLegacyPicInterruptRequest(uint64_t vector, uint64_t &interruptRequest) noexcept;
[[nodiscard]] LegacyPicModelStatus EnableLegacyPicInterruptRequest(uint16_t currentMask,
                                                                   uint64_t interruptRequest,
                                                                   uint16_t &updatedMask) noexcept;

struct PitConfiguration final {
    uint16_t divisor;
    uint64_t requestedFrequencyHz;
    uint64_t actualFrequencyHz;
};

enum class PitConfigurationStatus : uint64_t {
    Succeeded,
    InvalidFrequency,
    FrequencyOutOfRange,
};

[[nodiscard]] PitConfigurationStatus
CreatePitConfiguration(uint64_t requestedFrequencyHz, PitConfiguration &configuration) noexcept;
[[nodiscard]] uint64_t CalculatePitElapsedMilliseconds(uint64_t tickCount,
                                                       uint16_t divisor) noexcept;

enum class KeyboardKey : uint64_t {
    Unknown,
    Escape,
    Backspace,
    Enter,
    Space,
    A,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
};

struct KeyboardEvent final {
    KeyboardKey key;
    uint8_t scanCode;
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
    ScanCodeSet1Decoder() noexcept;

    [[nodiscard]] KeyboardDecodeStatus Decode(uint8_t scanCode, KeyboardEvent &event) noexcept;

  private:
    [[nodiscard]] KeyboardKey KeyForScanCode(uint8_t makeCode, bool extended) const noexcept;

    bool extendedPrefixPending_;
};

enum class AtaReadRequestStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidLogicalBlockAddress,
};

[[nodiscard]] AtaReadRequestStatus ValidateAtaReadRequest(uint64_t logicalBlockAddress,
                                                          const uint8_t *buffer,
                                                          uint64_t bufferSizeBytes) noexcept;
[[nodiscard]] bool Stage1BootDescriptorMagicMatches(const uint8_t *sector,
                                                    uint64_t sectorSizeBytes) noexcept;

}
