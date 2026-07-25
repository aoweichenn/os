#include <stdint.h>

namespace {

constexpr uint64_t OS_USER_PAGE_FAULT_UNMAPPED_ADDRESS = 0x0000000030000000ULL;

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    const volatile uint64_t *const unmapped_address =
        reinterpret_cast<const volatile uint64_t *>(OS_USER_PAGE_FAULT_UNMAPPED_ADDRESS);
    static_cast<void>(*unmapped_address);
    while (true) {
        asm volatile("ud2");
    }
}
