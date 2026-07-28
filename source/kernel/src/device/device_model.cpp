#include "os/kernel/device/device_model.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_DEVICE_PIC_MASK_BIT_COUNT = 16ULL;
constexpr uint64_t OS_KERNEL_DEVICE_PIC_MASTER_INTERRUPT_REQUEST_COUNT = 8ULL;
constexpr uint64_t OS_KERNEL_DEVICE_PIC_CASCADE_INTERRUPT_REQUEST = 2ULL;
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
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_CONTROL_MAKE_CODE = 0x1DU;
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
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_ENTER_CHARACTER = static_cast<uint8_t>('\n');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_BACKSPACE_CHARACTER = static_cast<uint8_t>('\b');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_TAB_CHARACTER = static_cast<uint8_t>('\t');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_SPACE_CHARACTER = static_cast<uint8_t>(' ');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_CONTROL_CHARACTER_MASK = 0x1FU;
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_LOWERCASE_FIRST_CHARACTER =
    static_cast<uint8_t>('a');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_LOWERCASE_LAST_CHARACTER =
    static_cast<uint8_t>('z');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_UPPERCASE_FIRST_CHARACTER =
    static_cast<uint8_t>('A');
constexpr uint8_t OS_KERNEL_DEVICE_KEYBOARD_UPPERCASE_LAST_CHARACTER =
    static_cast<uint8_t>('Z');
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

LegacyPicModelStatus CalculateLegacyPicVector(const uint64_t interrupt_request,
                                              uint64_t &vector) noexcept {
    if (interrupt_request >= OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT) {
        return LegacyPicModelStatus::InvalidInterruptRequest;
    }
    vector = OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE + interrupt_request;
    return LegacyPicModelStatus::Succeeded;
}

LegacyPicModelStatus CalculateLegacyPicInterruptRequest(const uint64_t vector,
                                                        uint64_t &interrupt_request) noexcept {
    if (vector < OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE ||
        vector >= OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE + OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT) {
        return LegacyPicModelStatus::InvalidVector;
    }
    interrupt_request = vector - OS_KERNEL_DEVICE_PIC_MASTER_VECTOR_BASE;
    return LegacyPicModelStatus::Succeeded;
}

LegacyPicModelStatus EnableLegacyPicInterruptRequest(const uint16_t current_mask,
                                                     const uint64_t interrupt_request,
                                                     uint16_t &updated_mask) noexcept {
    if (interrupt_request >= OS_KERNEL_DEVICE_PIC_MASK_BIT_COUNT) {
        return LegacyPicModelStatus::InvalidInterruptRequest;
    }
    updated_mask = static_cast<uint16_t>(
        current_mask & static_cast<uint16_t>(~(static_cast<uint16_t>(
                           OS_KERNEL_DEVICE_PIC_SINGLE_MASK_BIT << interrupt_request))));
    if (interrupt_request >=
        OS_KERNEL_DEVICE_PIC_MASTER_INTERRUPT_REQUEST_COUNT) {
        // 从片的中断输出接到主片 IRQ2；只开放从片位而不开放级联位不会抵达 CPU。
        updated_mask = static_cast<uint16_t>(
            updated_mask &
            static_cast<uint16_t>(~(static_cast<uint16_t>(
                OS_KERNEL_DEVICE_PIC_SINGLE_MASK_BIT
                << OS_KERNEL_DEVICE_PIC_CASCADE_INTERRUPT_REQUEST))));
    }
    return LegacyPicModelStatus::Succeeded;
}

PitConfigurationStatus CreatePitConfiguration(const uint64_t requested_frequency_hz,
                                              PitConfiguration &configuration) noexcept {
    if (requested_frequency_hz == 0ULL) {
        return PitConfigurationStatus::InvalidFrequency;
    }

    const uint64_t rounded_divisor =
        (OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ +
         requested_frequency_hz / OS_KERNEL_DEVICE_PIT_ROUNDING_DIVISOR) /
        requested_frequency_hz;
    if (rounded_divisor < OS_KERNEL_DEVICE_PIT_MINIMUM_DIVISOR ||
        rounded_divisor > OS_KERNEL_DEVICE_PIT_MAXIMUM_DIVISOR) {
        return PitConfigurationStatus::FrequencyOutOfRange;
    }

    configuration = PitConfiguration{
        .divisor = static_cast<uint16_t>(rounded_divisor),
        .requested_frequency_hz = requested_frequency_hz,
        .actual_frequency_hz = OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ / rounded_divisor,
    };
    return PitConfigurationStatus::Succeeded;
}

uint64_t CalculatePitElapsedMilliseconds(const uint64_t tick_count,
                                         const uint16_t divisor) noexcept {
    if (divisor == 0U) {
        return 0ULL;
    }
    const uint64_t ticks_per_calculation_limit =
        UINT64_MAX / (static_cast<uint64_t>(divisor) * OS_KERNEL_DEVICE_MILLISECONDS_PER_SECOND);
    if (tick_count > ticks_per_calculation_limit) {
        return UINT64_MAX;
    }
    return tick_count * static_cast<uint64_t>(divisor) * OS_KERNEL_DEVICE_MILLISECONDS_PER_SECOND /
           OS_KERNEL_DEVICE_PIT_INPUT_FREQUENCY_HZ;
}

KeyboardDecodeStatus ScanCodeSet1Decoder::Decode(const uint8_t scan_code,
                                                 KeyboardEvent &event) noexcept {
    if (scan_code == OS_KERNEL_DEVICE_KEYBOARD_EXTENDED_PREFIX) {
        this->extended_prefix_pending_ = true;
        return KeyboardDecodeStatus::AwaitingSequence;
    }

    const bool released = (scan_code & OS_KERNEL_DEVICE_KEYBOARD_RELEASE_BIT) != 0U;
    const uint8_t make_code =
        static_cast<uint8_t>(scan_code & OS_KERNEL_DEVICE_KEYBOARD_MAKE_CODE_MASK);
    const bool extended = this->extended_prefix_pending_;
    this->extended_prefix_pending_ = false;
    const KeyboardKey key = this->KeyForScanCode(make_code, extended);
    if (key == KeyboardKey::Unknown) {
        return KeyboardDecodeStatus::UnsupportedScanCode;
    }

    const bool pressed = !released;
    if (key == KeyboardKey::LeftShift) {
        this->left_shift_pressed_ = pressed;
    }
    if (key == KeyboardKey::RightShift) {
        this->right_shift_pressed_ = pressed;
    }
    if (key == KeyboardKey::LeftControl) {
        this->left_control_pressed_ = pressed;
    }
    if (key == KeyboardKey::RightControl) {
        this->right_control_pressed_ = pressed;
    }
    if (key == KeyboardKey::CapsLock && pressed) {
        this->caps_lock_enabled_ = !this->caps_lock_enabled_;
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
        } else if (key == KeyboardKey::A || key == KeyboardKey::Printable) {
            character = this->CharacterForScanCode(make_code);
            const bool control_pressed =
                this->left_control_pressed_ || this->right_control_pressed_;
            const bool alphabetic_character =
                (character >= OS_KERNEL_DEVICE_KEYBOARD_LOWERCASE_FIRST_CHARACTER &&
                 character <= OS_KERNEL_DEVICE_KEYBOARD_LOWERCASE_LAST_CHARACTER) ||
                (character >= OS_KERNEL_DEVICE_KEYBOARD_UPPERCASE_FIRST_CHARACTER &&
                 character <= OS_KERNEL_DEVICE_KEYBOARD_UPPERCASE_LAST_CHARACTER);
            if (control_pressed && alphabetic_character) {
                // ASCII 字母低五位正好编码 C0 控制字符；先确认字母可避免误改标点。
                character = static_cast<uint8_t>(
                    character & OS_KERNEL_DEVICE_KEYBOARD_CONTROL_CHARACTER_MASK);
            }
        }
    }
    event = KeyboardEvent{
        .key = key,
        .scan_code = scan_code,
        .character = character,
        .pressed = pressed,
        .extended = extended,
    };
    return KeyboardDecodeStatus::EventReady;
}

KeyboardKey ScanCodeSet1Decoder::KeyForScanCode(const uint8_t make_code,
                                                const bool extended) const noexcept {
    if (extended) {
        switch (make_code) {
        case OS_KERNEL_DEVICE_KEYBOARD_CONTROL_MAKE_CODE:
            return KeyboardKey::RightControl;
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

    switch (make_code) {
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
    case OS_KERNEL_DEVICE_KEYBOARD_CONTROL_MAKE_CODE:
        return KeyboardKey::LeftControl;
    case OS_KERNEL_DEVICE_KEYBOARD_LEFT_SHIFT_MAKE_CODE:
        return KeyboardKey::LeftShift;
    case OS_KERNEL_DEVICE_KEYBOARD_RIGHT_SHIFT_MAKE_CODE:
        return KeyboardKey::RightShift;
    case OS_KERNEL_DEVICE_KEYBOARD_CAPS_LOCK_MAKE_CODE:
        return KeyboardKey::CapsLock;
    default:
        break;
    }
    if ((make_code >= OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE &&
         make_code < OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE +
                         sizeof(OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW) -
                         OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) ||
        (make_code >= OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE &&
         make_code < OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE +
                         sizeof(OS_KERNEL_DEVICE_KEYBOARD_Q_ROW) -
                         OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) ||
        (make_code >= OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE &&
         make_code < OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE +
                         sizeof(OS_KERNEL_DEVICE_KEYBOARD_A_ROW) -
                         OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) ||
        (make_code >= OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE &&
         make_code < OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE +
                         sizeof(OS_KERNEL_DEVICE_KEYBOARD_Z_ROW) -
                         OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) ||
        make_code == OS_KERNEL_DEVICE_KEYBOARD_BACKTICK_MAKE_CODE ||
        make_code == OS_KERNEL_DEVICE_KEYBOARD_BACKSLASH_MAKE_CODE) {
        return KeyboardKey::Printable;
    }
    return KeyboardKey::Unknown;
}

uint8_t ScanCodeSet1Decoder::CharacterForScanCode(const uint8_t make_code) const noexcept {
    const bool shift_pressed = this->left_shift_pressed_ || this->right_shift_pressed_;
    if (make_code >= OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE &&
        make_code < OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE +
                        sizeof(OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW) -
                        OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) {
        const uint64_t character_index =
            make_code - OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW_FIRST_MAKE_CODE;
        return static_cast<uint8_t>(
            shift_pressed ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_DIGIT_ROW[character_index]
                          : OS_KERNEL_DEVICE_KEYBOARD_DIGIT_ROW[character_index]);
    }
    const bool uppercase_letter = shift_pressed != this->caps_lock_enabled_;
    if (make_code >= OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE &&
        make_code < OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE +
                        sizeof(OS_KERNEL_DEVICE_KEYBOARD_Q_ROW) -
                        OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) {
        const uint64_t character_index =
            make_code - OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_FIRST_MAKE_CODE;
        const bool use_shifted_character =
            character_index < OS_KERNEL_DEVICE_KEYBOARD_Q_ROW_LETTER_COUNT ? uppercase_letter
                                                                           : shift_pressed;
        return static_cast<uint8_t>(use_shifted_character
                                        ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_Q_ROW[character_index]
                                        : OS_KERNEL_DEVICE_KEYBOARD_Q_ROW[character_index]);
    }
    if (make_code >= OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE &&
        make_code < OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE +
                        sizeof(OS_KERNEL_DEVICE_KEYBOARD_A_ROW) -
                        OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) {
        const uint64_t character_index =
            make_code - OS_KERNEL_DEVICE_KEYBOARD_A_ROW_FIRST_MAKE_CODE;
        const bool use_shifted_character =
            character_index < OS_KERNEL_DEVICE_KEYBOARD_A_ROW_LETTER_COUNT ? uppercase_letter
                                                                           : shift_pressed;
        return static_cast<uint8_t>(use_shifted_character
                                        ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_A_ROW[character_index]
                                        : OS_KERNEL_DEVICE_KEYBOARD_A_ROW[character_index]);
    }
    if (make_code >= OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE &&
        make_code < OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE +
                        sizeof(OS_KERNEL_DEVICE_KEYBOARD_Z_ROW) -
                        OS_KERNEL_DEVICE_KEYBOARD_ROW_TERMINATOR_SIZE_BYTES) {
        const uint64_t character_index =
            make_code - OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_FIRST_MAKE_CODE;
        const bool use_shifted_character =
            character_index < OS_KERNEL_DEVICE_KEYBOARD_Z_ROW_LETTER_COUNT ? uppercase_letter
                                                                           : shift_pressed;
        return static_cast<uint8_t>(use_shifted_character
                                        ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_Z_ROW[character_index]
                                        : OS_KERNEL_DEVICE_KEYBOARD_Z_ROW[character_index]);
    }
    if (make_code == OS_KERNEL_DEVICE_KEYBOARD_BACKTICK_MAKE_CODE) {
        return static_cast<uint8_t>(shift_pressed
                                        ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_BACKTICK_CHARACTER
                                        : OS_KERNEL_DEVICE_KEYBOARD_BACKTICK_CHARACTER);
    }
    if (make_code == OS_KERNEL_DEVICE_KEYBOARD_BACKSLASH_MAKE_CODE) {
        return static_cast<uint8_t>(shift_pressed
                                        ? OS_KERNEL_DEVICE_KEYBOARD_SHIFTED_BACKSLASH_CHARACTER
                                        : OS_KERNEL_DEVICE_KEYBOARD_BACKSLASH_CHARACTER);
    }
    return OS_KERNEL_DEVICE_KEYBOARD_NO_CHARACTER;
}

AtaReadRequestStatus ValidateAtaReadRequest(const uint64_t logical_block_address,
                                            const uint8_t *buffer,
                                            const uint64_t buffer_size_bytes) noexcept {
    if (buffer == nullptr) {
        return AtaReadRequestStatus::NullBuffer;
    }
    if (buffer_size_bytes != OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES) {
        return AtaReadRequestStatus::InvalidBufferSize;
    }
    if (logical_block_address > OS_KERNEL_DEVICE_ATA_LBA28_MAXIMUM) {
        return AtaReadRequestStatus::InvalidLogicalBlockAddress;
    }
    return AtaReadRequestStatus::Succeeded;
}

bool Stage1BootDescriptorMagicMatches(const uint8_t *sector,
                                      const uint64_t sector_size_bytes) noexcept {
    if (sector == nullptr || sector_size_bytes < OS_KERNEL_DEVICE_STAGE1_MAGIC_SIZE_BYTES) {
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
