#include "os/kernel/freestanding_memory.hpp"

extern "C" void *memset(void *destination, const int value, const uint64_t length_bytes) noexcept {
    uint8_t *const destination_bytes = static_cast<uint8_t *>(destination);
    const uint8_t byte_value = static_cast<uint8_t>(value);
    for (uint64_t byte_index = 0ULL; byte_index < length_bytes; ++byte_index) {
        destination_bytes[byte_index] = byte_value;
    }
    return destination;
}

extern "C" void *memcpy(void *destination, const void *source,
                        const uint64_t length_bytes) noexcept {
    uint8_t *const destination_bytes = static_cast<uint8_t *>(destination);
    const uint8_t *const source_bytes = static_cast<const uint8_t *>(source);
    for (uint64_t byte_index = 0ULL; byte_index < length_bytes; ++byte_index) {
        destination_bytes[byte_index] = source_bytes[byte_index];
    }
    return destination;
}
