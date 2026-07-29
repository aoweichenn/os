#include "os/book/paging/page_table_walker.hpp"

#include <algorithm>

namespace os::book::paging {
namespace {

constexpr std::uint64_t OS_BOOK_PAGING_ENTRY_BYTE_COUNT =
    sizeof(std::uint64_t);
constexpr std::uint64_t OS_BOOK_PAGING_INDEX_MASK = 0x1FFU;
constexpr std::uint64_t OS_BOOK_PAGING_PRESENT = 1U << 0U;
constexpr std::uint64_t OS_BOOK_PAGING_WRITABLE = 1U << 1U;
constexpr std::uint64_t OS_BOOK_PAGING_USER = 1U << 2U;
constexpr std::uint64_t OS_BOOK_PAGING_PAGE_SIZE = 1U << 7U;
constexpr std::uint64_t OS_BOOK_PAGING_NO_EXECUTE = 1ULL << 63U;
constexpr std::uint64_t OS_BOOK_PAGING_TABLE_ADDRESS_MASK =
    0x000F'FFFF'FFFF'F000U;
constexpr std::uint64_t OS_BOOK_PAGING_LARGE_ADDRESS_MASK =
    0x000F'FFFF'FFE0'0000U;
constexpr std::uint64_t OS_BOOK_PAGING_4K_OFFSET_MASK = 0xFFFU;
constexpr std::uint64_t OS_BOOK_PAGING_2M_OFFSET_MASK = 0x1F'FFFFU;
constexpr std::uint64_t OS_BOOK_PAGING_CANONICAL_SIGN_SHIFT = 47U;
constexpr std::uint64_t OS_BOOK_PAGING_CANONICAL_HIGH_SHIFT = 48U;
constexpr std::uint64_t OS_BOOK_PAGING_CANONICAL_HIGH_ONES = 0xFFFFU;
constexpr std::uint64_t OS_BOOK_PAGING_PAGE_FAULT_PRESENT = 1U << 0U;
constexpr std::uint64_t OS_BOOK_PAGING_PAGE_FAULT_WRITE = 1U << 1U;
constexpr std::uint64_t OS_BOOK_PAGING_PAGE_FAULT_USER = 1U << 2U;
constexpr std::uint64_t OS_BOOK_PAGING_PAGE_FAULT_INSTRUCTION = 1U << 4U;
constexpr std::uint8_t OS_BOOK_PAGING_PML4_LEVEL = 4U;
constexpr std::uint8_t OS_BOOK_PAGING_PDPT_LEVEL = 3U;
constexpr std::uint8_t OS_BOOK_PAGING_PD_LEVEL = 2U;
constexpr std::uint8_t OS_BOOK_PAGING_PT_LEVEL = 1U;
constexpr std::uint64_t OS_BOOK_PAGING_PD_LEVEL_INDEX = 2U;
constexpr std::uint64_t OS_BOOK_PAGING_PT_LEVEL_INDEX = 3U;
constexpr std::array<std::uint64_t, OS_BOOK_PAGING_MAXIMUM_LEVEL_COUNT>
    OS_BOOK_PAGING_LEVEL_SHIFTS{39U, 30U, 21U, 12U};
constexpr std::array<std::uint8_t, OS_BOOK_PAGING_MAXIMUM_LEVEL_COUNT>
    OS_BOOK_PAGING_LEVEL_NAMES{
        OS_BOOK_PAGING_PML4_LEVEL,
        OS_BOOK_PAGING_PDPT_LEVEL,
        OS_BOOK_PAGING_PD_LEVEL,
        OS_BOOK_PAGING_PT_LEVEL};

[[nodiscard]] bool IsCanonical(
    const std::uint64_t virtual_address) noexcept {
    const std::uint64_t sign =
        (virtual_address >> OS_BOOK_PAGING_CANONICAL_SIGN_SHIFT) & 1U;
    const std::uint64_t high =
        virtual_address >> OS_BOOK_PAGING_CANONICAL_HIGH_SHIFT;
    const std::uint64_t expected_high =
        sign == 0U ? 0U : OS_BOOK_PAGING_CANONICAL_HIGH_ONES;
    return high == expected_high;
}

[[nodiscard]] std::uint64_t BuildPageFaultErrorCode(
    const bool protection_violation,
    const AccessType access_type,
    const PrivilegeLevel privilege_level) noexcept {
    std::uint64_t error_code =
        protection_violation ? OS_BOOK_PAGING_PAGE_FAULT_PRESENT : 0U;
    if (access_type == AccessType::Write) {
        error_code |= OS_BOOK_PAGING_PAGE_FAULT_WRITE;
    }
    if (privilege_level == PrivilegeLevel::User) {
        error_code |= OS_BOOK_PAGING_PAGE_FAULT_USER;
    }
    if (access_type == AccessType::Execute) {
        error_code |= OS_BOOK_PAGING_PAGE_FAULT_INSTRUCTION;
    }
    return error_code;
}

[[nodiscard]] WalkResult MakeInitialResult() noexcept {
    return WalkResult{
        WalkStatus::Translated,
        0U,
        0U,
        EffectivePermissions{true, true, true},
        0U,
        {}};
}

void RecordEntry(
    WalkResult& result,
    const std::uint8_t level,
    const std::uint64_t entry_address,
    const std::uint64_t entry_value) noexcept {
    const std::uint64_t path_index = result.visited_level_count;
    result.path[path_index] = EntrySnapshot{level, entry_address, entry_value};
    ++result.visited_level_count;
}

void MergePermissions(
    WalkResult& result,
    const std::uint64_t entry_value) noexcept {
    result.permissions.writable =
        result.permissions.writable
        && ((entry_value & OS_BOOK_PAGING_WRITABLE) != 0U);
    result.permissions.user_accessible =
        result.permissions.user_accessible
        && ((entry_value & OS_BOOK_PAGING_USER) != 0U);
    result.permissions.executable =
        result.permissions.executable
        && ((entry_value & OS_BOOK_PAGING_NO_EXECUTE) == 0U);
}

[[nodiscard]] WalkStatus CheckPermission(
    const EffectivePermissions& permissions,
    const AccessType access_type,
    const PrivilegeLevel privilege_level,
    const bool cr0_write_protect) noexcept {
    if (
        privilege_level == PrivilegeLevel::User
        && !permissions.user_accessible) {
        return WalkStatus::UserDenied;
    }
    if (
        access_type == AccessType::Write
        && !permissions.writable
        && (
            privilege_level == PrivilegeLevel::User
            || cr0_write_protect)) {
        return WalkStatus::WriteDenied;
    }
    if (
        access_type == AccessType::Execute
        && !permissions.executable) {
        return WalkStatus::ExecuteDenied;
    }
    return WalkStatus::Translated;
}

}  // namespace

PageTableImage::PageTableImage() noexcept
    : entries_{} {
}

bool PageTableImage::SetEntry(
    const std::uint64_t physical_address,
    const std::uint64_t entry_value) noexcept {
    std::uint64_t entry_index = 0U;
    if (!this->ResolveIndex(physical_address, entry_index)) {
        return false;
    }
    this->entries_[entry_index] = entry_value;
    return true;
}

bool PageTableImage::ReadEntry(
    const std::uint64_t physical_address,
    std::uint64_t& entry_value) const noexcept {
    std::uint64_t entry_index = 0U;
    if (!this->ResolveIndex(physical_address, entry_index)) {
        return false;
    }
    entry_value = this->entries_[entry_index];
    return true;
}

bool PageTableImage::ResolveIndex(
    const std::uint64_t physical_address,
    std::uint64_t& entry_index) const noexcept {
    if (physical_address < OS_BOOK_PAGING_IMAGE_BASE) {
        return false;
    }
    const std::uint64_t image_offset =
        physical_address - OS_BOOK_PAGING_IMAGE_BASE;
    if (
        image_offset >= OS_BOOK_PAGING_IMAGE_BYTE_COUNT
        || (image_offset % OS_BOOK_PAGING_ENTRY_BYTE_COUNT) != 0U) {
        return false;
    }
    entry_index = image_offset / OS_BOOK_PAGING_ENTRY_BYTE_COUNT;
    return true;
}

WalkResult WalkFourLevel(
    const PageTableImage& image,
    const std::uint64_t virtual_address,
    const std::uint64_t cr3,
    const AccessType access_type,
    const PrivilegeLevel privilege_level,
    const bool cr0_write_protect) noexcept {
    WalkResult result = MakeInitialResult();
    if (!IsCanonical(virtual_address)) {
        result.status = WalkStatus::NonCanonical;
        return result;
    }

    std::uint64_t table_address =
        cr3 & OS_BOOK_PAGING_TABLE_ADDRESS_MASK;
    for (
        std::uint64_t level_index = 0U;
        level_index < OS_BOOK_PAGING_MAXIMUM_LEVEL_COUNT;
        ++level_index) {
        const std::uint64_t table_index =
            (virtual_address >> OS_BOOK_PAGING_LEVEL_SHIFTS[level_index])
            & OS_BOOK_PAGING_INDEX_MASK;
        const std::uint64_t entry_address =
            table_address + table_index * OS_BOOK_PAGING_ENTRY_BYTE_COUNT;
        std::uint64_t entry_value = 0U;
        if (!image.ReadEntry(entry_address, entry_value)) {
            result.status = WalkStatus::EntryOutsideImage;
            return result;
        }

        RecordEntry(
            result,
            OS_BOOK_PAGING_LEVEL_NAMES[level_index],
            entry_address,
            entry_value);
        if ((entry_value & OS_BOOK_PAGING_PRESENT) == 0U) {
            result.status = WalkStatus::NotPresent;
            result.page_fault_error_code = BuildPageFaultErrorCode(
                false,
                access_type,
                privilege_level);
            return result;
        }
        MergePermissions(result, entry_value);

        const bool page_size =
            (entry_value & OS_BOOK_PAGING_PAGE_SIZE) != 0U;
        if (page_size) {
            if (level_index != OS_BOOK_PAGING_PD_LEVEL_INDEX) {
                result.status = WalkStatus::UnsupportedLargePage;
                return result;
            }
            result.physical_address =
                (entry_value & OS_BOOK_PAGING_LARGE_ADDRESS_MASK)
                + (virtual_address & OS_BOOK_PAGING_2M_OFFSET_MASK);
            break;
        }

        table_address = entry_value & OS_BOOK_PAGING_TABLE_ADDRESS_MASK;
        if (level_index == OS_BOOK_PAGING_PT_LEVEL_INDEX) {
            result.physical_address =
                table_address
                + (virtual_address & OS_BOOK_PAGING_4K_OFFSET_MASK);
        }
    }

    result.status = CheckPermission(
        result.permissions,
        access_type,
        privilege_level,
        cr0_write_protect);
    if (result.status != WalkStatus::Translated) {
        result.page_fault_error_code = BuildPageFaultErrorCode(
            true,
            access_type,
            privilege_level);
    }
    return result;
}

const char* WalkStatusName(const WalkStatus status) noexcept {
    switch (status) {
        case WalkStatus::Translated:
            return "translated";
        case WalkStatus::NonCanonical:
            return "non-canonical";
        case WalkStatus::EntryOutsideImage:
            return "entry-outside-image";
        case WalkStatus::NotPresent:
            return "not-present";
        case WalkStatus::UnsupportedLargePage:
            return "unsupported-large-page";
        case WalkStatus::WriteDenied:
            return "write-denied";
        case WalkStatus::UserDenied:
            return "user-denied";
        case WalkStatus::ExecuteDenied:
            return "execute-denied";
    }
    return "unknown";
}

}  // namespace os::book::paging
