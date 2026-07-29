#pragma once

#include <array>
#include <cstdint>

namespace os::book::paging {

inline constexpr std::uint64_t OS_BOOK_PAGING_IMAGE_BASE = 0x0010'0000U;
inline constexpr std::uint64_t OS_BOOK_PAGING_IMAGE_BYTE_COUNT = 0x0001'0000U;
inline constexpr std::uint64_t OS_BOOK_PAGING_IMAGE_ENTRY_COUNT =
    OS_BOOK_PAGING_IMAGE_BYTE_COUNT / sizeof(std::uint64_t);
inline constexpr std::uint64_t OS_BOOK_PAGING_MAXIMUM_LEVEL_COUNT = 4U;

enum class AccessType : std::uint8_t {
    Read,
    Write,
    Execute,
};

enum class PrivilegeLevel : std::uint8_t {
    Supervisor,
    User,
};

enum class WalkStatus : std::uint8_t {
    Translated,
    NonCanonical,
    EntryOutsideImage,
    NotPresent,
    UnsupportedLargePage,
    WriteDenied,
    UserDenied,
    ExecuteDenied,
};

struct EntrySnapshot final {
    std::uint8_t level;
    std::uint64_t entry_address;
    std::uint64_t entry_value;
};

struct EffectivePermissions final {
    bool writable;
    bool user_accessible;
    bool executable;
};

struct WalkResult final {
    WalkStatus status;
    std::uint64_t physical_address;
    std::uint64_t page_fault_error_code;
    EffectivePermissions permissions;
    std::uint64_t visited_level_count;
    std::array<EntrySnapshot, OS_BOOK_PAGING_MAXIMUM_LEVEL_COUNT> path;
};

class PageTableImage final {
public:
    PageTableImage() noexcept;

    [[nodiscard]] bool SetEntry(
        std::uint64_t physical_address,
        std::uint64_t entry_value) noexcept;
    [[nodiscard]] bool ReadEntry(
        std::uint64_t physical_address,
        std::uint64_t& entry_value) const noexcept;

private:
    [[nodiscard]] bool ResolveIndex(
        std::uint64_t physical_address,
        std::uint64_t& entry_index) const noexcept;

    std::array<std::uint64_t, OS_BOOK_PAGING_IMAGE_ENTRY_COUNT> entries_;
};

[[nodiscard]] WalkResult WalkFourLevel(
    const PageTableImage& image,
    std::uint64_t virtual_address,
    std::uint64_t cr3,
    AccessType access_type,
    PrivilegeLevel privilege_level,
    bool cr0_write_protect) noexcept;

[[nodiscard]] const char* WalkStatusName(WalkStatus status) noexcept;

}  // namespace os::book::paging
