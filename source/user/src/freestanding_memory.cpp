#include "os/user/freestanding_memory.hpp"

namespace {

constexpr uint64_t OS_USER_FREESTANDING_MEMORY_FIRST_BYTE_INDEX = 0ULL;

}

extern "C" void *memset(void *destination, const int value,
                        const uint64_t lengthBytes) noexcept {
    uint8_t *const destinationBytes =
        static_cast<uint8_t *>(destination);
    const uint8_t byteValue = static_cast<uint8_t>(value);
    for (uint64_t byteIndex =
             OS_USER_FREESTANDING_MEMORY_FIRST_BYTE_INDEX;
         byteIndex < lengthBytes;
         ++byteIndex) {
        destinationBytes[byteIndex] = byteValue;
    }
    return destination;
}

extern "C" void *memcpy(void *destination, const void *source,
                        const uint64_t lengthBytes) noexcept {
    uint8_t *const destinationBytes =
        static_cast<uint8_t *>(destination);
    const uint8_t *const sourceBytes =
        static_cast<const uint8_t *>(source);
    for (uint64_t byteIndex =
             OS_USER_FREESTANDING_MEMORY_FIRST_BYTE_INDEX;
         byteIndex < lengthBytes;
         ++byteIndex) {
        destinationBytes[byteIndex] = sourceBytes[byteIndex];
    }
    return destination;
}
