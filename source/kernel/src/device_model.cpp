#include "os/kernel/device_model.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_DEVICE_PIC_MASK_BIT_COUNT = 16ULL;
constexpr uint16_t OS_KERNEL_DEVICE_PIC_SINGLE_MASK_BIT = 0x0001U;
constexpr uint64_t OS_KERNEL_DEVICE_PIT_MINIMUM_DIVISOR = 1ULL;
constexpr uint64_t OS_KERNEL_DEVICE_PIT_MAXIMUM_DIVISOR = 0x0000FFFFULL;
constexpr uint64_t OS_KERNEL_DEVICE_PIT_ROUNDING_DIVISOR = 2ULL;
constexpr uint64_t OS_KERNEL_DEVICE_MILLISECONDS_PER_SECOND = 1000ULL;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_EXTENDED_PREFIX = 0xE0U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_RELEASE_BIT = 0x80U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_MAKE_CODE_MASK = 0x7FU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_ESCAPE_MAKE_CODE = 0x01U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_BACKSPACE_MAKE_CODE = 0x0EU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_ENTER_MAKE_CODE = 0x1CU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_TAB_MAKE_CODE = 0x0FU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_A_MAKE_CODE = 0x1EU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_SPACE_MAKE_CODE = 0x39U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_LEFT_SHIFT_MAKE_CODE = 0x2AU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_RIGHT_SHIFT_MAKE_CODE = 0x36U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_CAPS_LOCK_MAKE_CODE = 0x3AU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_BACKTICK_MAKE_CODE = 0x29U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_BACKSLASH_MAKE_CODE = 0x2BU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE = 0x02U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE = 0x10U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE = 0x1EU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE = 0x2CU;
constexpr uint64_t OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr char OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW[] = "1234567890-=";
constexpr char OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_DIGIT_ROW[] = "!@#$%^&*()_+";
constexpr char OS_KERNEL_DEVICE_KEYBOARD_Q_ROW[] = "qwertyuiop[]";
constexpr char OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_Q_ROW[] = "QWERTYUIOP{}";
constexpr char OS_KERNEL_DEVICE_KEYBOARD_A_ROW[] = "asdfghjkl;'";
constexpr char OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_A_ROW[] = "ASDFGHJKL:\"";
constexpr char OS_KERNEL_DEVICE_KEYBOARD_Z_ROW[] = "zxcvbnm,./";
constexpr char OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_Z_ROW[] = "ZXCVBNM<>?";
constexpr uint64_t OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_LETTER_COUNT = 10ULL;
constexpr uint64_t OS_KERNEL_DEVICE_KEYBOARD_A_ROW_LETTER_COUNT = 9ULL;
constexpr uint64_t OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_LETTER_COUNT = 7ULL;
constexpr char OS_KERNEL_DEVICE_KEYBOARD_BACKTICK_CHARACTER = '`';
constexpr char OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_BACKTICK_CHARACTER = '~';
constexpr char OS_KERNEL_DEVICE_KEYBOARD_BACKSLASH_CHARACTER = '\\';
constexpr char OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_BACKSLASH_CHARACTER = '|';
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_NO_CHARACTER = 0U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_ENTER_CHARACTER =
    static_cast<uint8_t>('\n');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_BACKSPACE_CHARACTER =
    static_cast<uint8_t>('\b');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_TAB_CHARACTER =
    static_cast<uint8_t>('\t');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_SPACE_CHARACTER =
    static_cast<uint8_t>(' ');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_ARROW_UP_MAKE_CODE = 0x48U;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_ARROW_LEFT_MAKE_CODE = 0x4BU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_ARROW_RIGHT_MAKE_CODE = 0x4DU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_ARROW_DOWN_MAKE_CODE = 0x50U;
constexpr uint64_t OS_KERNEL_DEVICE_STAGE1_MAGIC_SIZE_BYTES = 8ULL;
constexpr uint8_t OS_KERNEL_DEVICE_STAGE1_MAGIC[OS_KERNEL_DEVICE_STAGE1_MAGIC_SIZE_BYTES] = {
    static_cast<uint8_t>('O'), static_cast<uint8_t>('S'), static_cast<uint8_t>('S'),
    static_cast<uint8_t>('T'), static_cast<uint8_t>('A'), static_cast<uint8_t>('G'),
    static_cast<uint8_t>('E'), static_cast<uint8_t>('1'),
};
constexpr uint64_t OS_KERNEL_DEVICE_ATA_LBA28_MAXIMUM = 0x0FFFFFFFULL;

}

LegacyPicModelStatus CalculateLegacyPicVector(const uint64_t interruptRequest,
                                              uint64_t &vector) noexcept {
    if (interruptRequest >= OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT) {
        return LegacyPicModelStatus::InvalidInterruptRequest;
    }
    vector = OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE + interruptRequest;
    return LegacyPicModelStatus::Succeeded;
}

LegacyPicModelStatus CalculateLegacyPicInterruptRequest(const uint64_t vector,
                                                        uint64_t &interruptRequest) noexcept {
    if (vector < OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE ||
        vector >= OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE + OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT) {
        return LegacyPicModelStatus::InvalidVector;
    }
    interruptRequest = vector - OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE;
    return LegacyPicModelStatus::Succeeded;
}

LegacyPicModelStatus EnableLegacyPicInterruptRequest(const uint16_t currentMask,
                                                     const uint64_t interruptRequest,
                                                     uint16_t &updatedMask) noexcept {
    if (interruptRequest >= OS_KERNEL_DEVICE_PIC_MASK_BIT_COUNT) {
        return LegacyPicModelStatus::InvalidInterruptRequest;
    }
    updatedMask = static_cast<uint16_t>(
        currentMask & static_cast<uint16_t>(~(static_cast<uint16_t>(
                          OS_KERNEL_DEVICE_PIC_SINGLE_MASK_BIT << interruptRequest))));
    return LegacyPicModelStatus::Succeeded;
}

PitConfigurationStatus CreatePitConfiguration(const uint64_t requestedFrequencyHz,
                                              PitConfiguration &configuration) noexcept {
    if (requestedFrequencyHz == 0ULL) {
        return PitConfigurationStatus::InvalidFrequency;
    }

    const uint64_t roundedDivisor = (OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ +
                                     requestedFrequencyHz / OS_KERNEL_DEVICE_PIT_ROUNDING_DIVISOR) /
                                    requestedFrequencyHz;
    if (roundedDivisor < OS_KERNEL_DEVICE_PIT_MINIMUM_DIVISOR ||
        roundedDivisor > OS_KERNEL_DEVICE_PIT_MAXIMUM_DIVISOR) {
        return PitConfigurationStatus::FrequencyOutOfRange;
    }

    configuration = PitConfiguration{
        .divisor = static_cast<uint16_t>(roundedDivisor),
        .requestedFrequencyHz = requestedFrequencyHz,
        .actualFrequencyHz = OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ / roundedDivisor,
    };
    return PitConfigurationStatus::Succeeded;
}

uint64_t CalculatePitElapsedMilliseconds(const uint64_t tickCount,
                                         const uint16_t divisor) noexcept {
    if (divisor == 0U) {
        return 0ULL;
    }
    const uint64_t ticksPerCalculationLimit =
        UINT64_MAX / (static_cast<uint64_t>(divisor) * OS_KERNEL_DEVICE_MILLISECONDS_PER_SECOND);
    if (tickCount > ticksPerCalculationLimit) {
        return UINT64_MAX;
    }
    return tickCount * static_cast<uint64_t>(divisor) * OS_KERNEL_DEVICE_MILLISECONDS_PER_SECOND /
           OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ;
}

KeyboardDecodeStatus ScanCodeSet1Decoder::Decode(const uint8_t scanCode,
                                                 KeyboardEvent &event) noexcept {
    if (scanCode == OS_KERNEL_DEVICE_KEYBOARD_EXTENDED_PREFIX) {
        this->extendedPrefixPending_ = true;
        return KeyboardDecodeStatus::AwaitingSequence;
    }

    const bool released = (scanCode & OS_KERNEL_DEVICE_KEYBOARD_RELEASE_BIT) != 0U;
    const uint8_t makeCode =
        static_cast<uint8_t>(scanCode & OS_KERNEL_DEVICE_KEYBOARD_MAKE_CODE_MASK);
    const bool extended = this->extendedPrefixPending_;
    this->extendedPrefixPending_ = false;
    const KeyboardKey key = this->KeyForScanCode(makeCode, extended);
    if (key == KeyboardKey::Unknown) {
        return KeyboardDecodeStatus::UnsupportedScanCode;
    }

    const bool pressed = !released;
    if (key == KeyboardKey::LeftShift) {
        this->leftShiftPressed_ = pressed;
    }
    if (key == KeyboardKey::RightShift) {
        this->rightShiftPressed_ = pressed;
    }
    if (key == KeyboardKey::CapsLock && pressed) {
        this->capsLockEnabled_ = !this->capsLockEnabled_;
    }
    uint8_t character = OS_KERNEL_DEVICE_KEYBOARD_NO_CHARACTER;
    if (pressed) {
        if (key == KeyboardKey::Enter) {
            character = OS_KERNEL_DEVICE_KEYBOARD_ENTER_CHARACTER;
        } else if (key == KeyboardKey::Backspace) {
            character = OS_KERNEL_DEVICE_KEYBOARD_BACKSPACE_CHARACTER;
        } else if (key == KeyboardKey::Tab) {
            character = OS_KERNEL_DEVICE_KEYBOARD_TAB_CHARACTER;
        } else if (key == KeyboardKey::Space) {
            character = OS_KERNEL_DEVICE_KEYBOARD_SPACE_CHARACTER;
        } else if (key == KeyboardKey::A ||
                   key == KeyboardKey::Printable) {
            character = this->CharacterForScanCode(makeCode);
        }
    }
    event = KeyboardEvent{
        .key = key,
        .scanCode = scanCode,
        .character = character,
        .pressed = pressed,
        .extended = extended,
    };
    return KeyboardDecodeStatus::EventReady;
}

KeyboardKey ScanCodeSet1Decoder::KeyForScanCode(const uint8_t makeCode,
                                                const bool extended) const noexcept {
    if (extended) {
        switch (makeCode) {
        case OS_KERNEL_DEVICE_KEYBOARD_ARROW_UP_MAKE_CODE:
            return KeyboardKey::ArrowUp;
        case OS_KERNEL_DEVICE_KEYBOARD_ARROW_DOWN_MAKE_CODE:
            return KeyboardKey::ArrowDown;
        case OS_KERNEL_DEVICE_KEYBOARD_ARROW_LEFT_MAKE_CODE:
            return KeyboardKey::ArrowLeft;
        case OS_KERNEL_DEVICE_KEYBOARD_ARROW_RIGHT_MAKE_CODE:
            return KeyboardKey::ArrowRight;
        default:
            return KeyboardKey::Unknown;
        }
    }

    switch (makeCode) {
    case OS_KERNEL_DEVICE_KEYBOARD_ESCAPE_MAKE_CODE:
        return KeyboardKey::Escape;
    case OS_KERNEL_DEVICE_KEYBOARD_BACKSPACE_MAKE_CODE:
        return KeyboardKey::Backspace;
    case OS_KERNEL_DEVICE_KEYBOARD_ENTER_MAKE_CODE:
        return KeyboardKey::Enter;
    case OS_KERNEL_DEVICE_KEYBOARD_TAB_MAKE_CODE:
        return KeyboardKey::Tab;
    case OS_KERNEL_DEVICE_KEYBOARD_SPACE_MAKE_CODE:
        return KeyboardKey::Space;
    case OS_KERNEL_DEVICE_KEYBOARD_A_MAKE_CODE:
        return KeyboardKey::A;
    case OS_KERNEL_DEVICE_KEYBOARD_LEFT_SHIFT_MAKE_CODE:
        return KeyboardKey::LeftShift;
    case OS_KERNEL_DEVICE_KEYBOARD_RIGHT_SHIFT_MAKE_CODE:
        return KeyboardKey::RightShift;
    case OS_KERNEL_DEVICE_KEYBOARD_CAPS_LOCK_MAKE_CODE:
        return KeyboardKey::CapsLock;
    default:
        break;
    }
    if ((makeCode >= OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE &&
         makeCode < OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE +
                        sizeof(OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW) -
                            OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) ||
        (makeCode >= OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE &&
         makeCode < OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE +
                        sizeof(OS_KERNEL_DEVICE_KEYBOARD_Q_ROW) -
                            OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) ||
        (makeCode >= OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE &&
         makeCode < OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE +
                        sizeof(OS_KERNEL_DEVICE_KEYBOARD_A_ROW) -
                            OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) ||
        (makeCode >= OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE &&
         makeCode < OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE +
                        sizeof(OS_KERNEL_DEVICE_KEYBOARD_Z_ROW) -
                            OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) ||
        makeCode == OS_KERNEL_DEVICE_KEYBOARD_BACKTICK_MAKE_CODE ||
        makeCode == OS_KERNEL_DEVICE_KEYBOARD_BACKSLASH_MAKE_CODE) {
        return KeyboardKey::Printable;
    }
    return KeyboardKey::Unknown;
}

uint8_t ScanCodeSet1Decoder::CharacterForScanCode(
    const uint8_t makeCode) const noexcept {
    const bool shiftPressed =
        this->leftShiftPressed_ || this->rightShiftPressed_;
    if (makeCode >= OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE &&
        makeCode < OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE +
                       sizeof(OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW) -
                           OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) {
        const uint64_t characterIndex =
            makeCode - OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE;
        return static_cast<uint8_t>(
            shiftPressed
                ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_DIGIT_ROW[characterIndex]
                : OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW[characterIndex]);
    }
    const bool uppercaseLetter = shiftPressed != this->capsLockEnabled_;
    if (makeCode >= OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE &&
        makeCode < OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE +
                       sizeof(OS_KERNEL_DEVICE_KEYBOARD_Q_ROW) -
                           OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) {
        const uint64_t characterIndex =
            makeCode - OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE;
        const bool useShiftedCharacter =
            characterIndex < OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_LETTER_COUNT
                ? uppercaseLetter
                : shiftPressed;
        return static_cast<uint8_t>(
            useShiftedCharacter
                ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_Q_ROW[characterIndex]
                : OS_KERNEL_DEVICE_KEYBOARD_Q_ROW[characterIndex]);
    }
    if (makeCode >= OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE &&
        makeCode < OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE +
                       sizeof(OS_KERNEL_DEVICE_KEYBOARD_A_ROW) -
                           OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) {
        const uint64_t characterIndex =
            makeCode - OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE;
        const bool useShiftedCharacter =
            characterIndex < OS_KERNEL_DEVICE_KEYBOARD_A_ROW_LETTER_COUNT
                ? uppercaseLetter
                : shiftPressed;
        return static_cast<uint8_t>(
            useShiftedCharacter
                ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_A_ROW[characterIndex]
                : OS_KERNEL_DEVICE_KEYBOARD_A_ROW[characterIndex]);
    }
    if (makeCode >= OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE &&
        makeCode < OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE +
                       sizeof(OS_KERNEL_DEVICE_KEYBOARD_Z_ROW) -
                           OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) {
        const uint64_t characterIndex =
            makeCode - OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE;
        const bool useShiftedCharacter =
            characterIndex < OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_LETTER_COUNT
                ? uppercaseLetter
                : shiftPressed;
        return static_cast<uint8_t>(
            useShiftedCharacter
                ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_Z_ROW[characterIndex]
                : OS_KERNEL_DEVICE_KEYBOARD_Z_ROW[characterIndex]);
    }
    if (makeCode == OS_KERNEL_DEVICE_KEYBOARD_BACKTICK_MAKE_CODE) {
        return static_cast<uint8_t>(
            shiftPressed
                ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_BACKTICK_CHARACTER
                : OS_KERNEL_DEVICE_KEYBOARD_BACKTICK_CHARACTER);
    }
    if (makeCode == OS_KERNEL_DEVICE_KEYBOARD_BACKSLASH_MAKE_CODE) {
        return static_cast<uint8_t>(
            shiftPressed
                ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_BACKSLASH_CHARACTER
                : OS_KERNEL_DEVICE_KEYBOARD_BACKSLASH_CHARACTER);
    }
    return OS_KERNEL_DEVICE_KEYBOARD_NO_CHARACTER;
}

AtaReadRequestStatus ValidateAtaReadRequest(const uint64_t logicalBlockAddress,
                                            const uint8_t *buffer,
                                            const uint64_t bufferSizeBytes) noexcept {
    if (buffer == nullptr) {
        return AtaReadRequestStatus::NullBuffer;
    }
    if (bufferSizeBytes != OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) {
        return AtaReadRequestStatus::InvalidBufferSize;
    }
    if (logicalBlockAddress > OS_KERNEL_DEVICE_ATA_LBA28_MAXIMUM) {
        return AtaReadRequestStatus::InvalidLogicalBlockAddress;
    }
    return AtaReadRequestStatus::Succeeded;
}

bool Stage1BootDescriptorMagicMatches(const uint8_t *sector,
                                      const uint64_t sectorSizeBytes) noexcept {
    if (sector == nullptr || sectorSizeBytes < OS_KERNEL_DEVICE_STAGE1_MAGIC_SIZE_BYTES) {
        return false;
    }
    for (uint64_t index = 0ULL; index < OS_KERNEL_DEVICE_STAGE1_MAGIC_SIZE_BYTES; ++index) {
        if (sector[index] != OS_KERNEL_DEVICE_STAGE1_MAGIC[index]) {
            return false;
        }
    }
    return true;
}

}
