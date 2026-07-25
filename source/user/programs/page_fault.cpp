#include <stdint.h>

namespace {

constexpr uint64_t OS_USER_PAGE_FAULT_UNMAPPED_ADDRESS = 0x0000000030000000ULL;

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void osUserEntry() noexcept {
    const volatile uint64_t *const unmappedAddress =
        reinterpret_cast<const volatile uint64_t *>(OS_USER_PAGE_FAULT_UNMAPPED_ADDRESS);
    static_cast<void>(*unmappedAddress);
    while (true) {
        asm volatile("ud2");
    }
}
