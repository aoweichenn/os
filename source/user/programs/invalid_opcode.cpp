extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    asm volatile("ud2");
    while (true) {
        asm volatile("ud2");
    }
}
