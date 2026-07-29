#include "os/book/paging/page_table_walker.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace os::book::paging {
namespace {

constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_PML4 = 0x0010'1000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_PDPT = 0x0010'2000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_PD = 0x0010'3000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_PT = 0x0010'4000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_HIGH_PDPT = 0x0010'5000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_HIGH_PD = 0x0010'6000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_ENTRY_BYTE_COUNT = 8U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_INDEX_MASK = 0x1FFU;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_PRESENT = 1U << 0U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_WRITABLE = 1U << 1U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_USER = 1U << 2U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_PAGE_SIZE = 1U << 7U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_NO_EXECUTE = 1ULL << 63U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS =
    OS_BOOK_PAGING_EXAMPLE_PRESENT
    | OS_BOOK_PAGING_EXAMPLE_WRITABLE
    | OS_BOOK_PAGING_EXAMPLE_USER;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_4K_ADDRESS =
    0x0000'0000'0040'1234U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_4K_FRAME = 0x1234'5000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_2M_ADDRESS =
    0xFFFF'8880'005A'BCDEU;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_2M_FRAME = 0x0040'0000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_NX_ADDRESS =
    0x0000'0000'0040'3ABCU;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_READ_ONLY_ADDRESS =
    0x0000'0000'0040'2ABCU;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_NON_PRESENT_ADDRESS =
    0x0000'0000'0080'0000U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PML4 = 39U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PDPT = 30U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PD = 21U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PT = 12U;
constexpr std::string_view OS_BOOK_PAGING_EXAMPLE_SCENARIO_4K = "4k-read";
constexpr std::string_view OS_BOOK_PAGING_EXAMPLE_SCENARIO_2M = "2m-read";
constexpr std::string_view OS_BOOK_PAGING_EXAMPLE_SCENARIO_NX = "nx-execute";
constexpr std::string_view OS_BOOK_PAGING_EXAMPLE_SCENARIO_READ_ONLY =
    "parent-read-only-write";
constexpr std::string_view OS_BOOK_PAGING_EXAMPLE_SCENARIO_NOT_PRESENT =
    "not-present-user-read";
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_EXPECTED_4K_PHYSICAL_ADDRESS =
    0x1234'5234U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_EXPECTED_2M_PHYSICAL_ADDRESS =
    0x005A'BCDEU;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_EXPECTED_NX_ERROR_CODE = 0x15U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_EXPECTED_WRITE_ERROR_CODE =
    0x7U;
constexpr std::uint64_t OS_BOOK_PAGING_EXAMPLE_EXPECTED_ABSENT_ERROR_CODE =
    0x4U;
constexpr std::int32_t OS_BOOK_PAGING_EXAMPLE_EXIT_SUCCESS = 0;
constexpr std::int32_t OS_BOOK_PAGING_EXAMPLE_EXIT_FAILURE = 1;

[[nodiscard]] std::uint64_t EntryAddress(
    const std::uint64_t table_address,
    const std::uint64_t virtual_address,
    const std::uint64_t shift) noexcept {
    const std::uint64_t index =
        (virtual_address >> shift) & OS_BOOK_PAGING_EXAMPLE_INDEX_MASK;
    return table_address + index * OS_BOOK_PAGING_EXAMPLE_ENTRY_BYTE_COUNT;
}

[[nodiscard]] bool InstallFourKilobytePath(
    PageTableImage& image,
    const std::uint64_t virtual_address,
    const std::uint64_t page_frame,
    const std::uint64_t pd_flags,
    const std::uint64_t leaf_flags) noexcept {
    return image.SetEntry(
               EntryAddress(
                   OS_BOOK_PAGING_EXAMPLE_PML4,
                   virtual_address,
                   OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PML4),
               OS_BOOK_PAGING_EXAMPLE_PDPT
                   | OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS)
        && image.SetEntry(
               EntryAddress(
                   OS_BOOK_PAGING_EXAMPLE_PDPT,
                   virtual_address,
                   OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PDPT),
               OS_BOOK_PAGING_EXAMPLE_PD
                   | OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS)
        && image.SetEntry(
               EntryAddress(
                   OS_BOOK_PAGING_EXAMPLE_PD,
                   virtual_address,
                   OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PD),
               OS_BOOK_PAGING_EXAMPLE_PT | pd_flags)
        && image.SetEntry(
               EntryAddress(
                   OS_BOOK_PAGING_EXAMPLE_PT,
                   virtual_address,
                   OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PT),
               page_frame | leaf_flags);
}

[[nodiscard]] bool InstallTwoMegabytePath(
    PageTableImage& image,
    const std::uint64_t virtual_address,
    const std::uint64_t page_frame) noexcept {
    return image.SetEntry(
               EntryAddress(
                   OS_BOOK_PAGING_EXAMPLE_PML4,
                   virtual_address,
                   OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PML4),
               OS_BOOK_PAGING_EXAMPLE_HIGH_PDPT
                   | OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS)
        && image.SetEntry(
               EntryAddress(
                   OS_BOOK_PAGING_EXAMPLE_HIGH_PDPT,
                   virtual_address,
                   OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PDPT),
               OS_BOOK_PAGING_EXAMPLE_HIGH_PD
                   | OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS)
        && image.SetEntry(
               EntryAddress(
                   OS_BOOK_PAGING_EXAMPLE_HIGH_PD,
                   virtual_address,
                   OS_BOOK_PAGING_EXAMPLE_LEVEL_SHIFT_PD),
               page_frame
                   | OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS
                   | OS_BOOK_PAGING_EXAMPLE_PAGE_SIZE);
}

void PrintResult(
    const std::string_view scenario_name,
    const WalkResult& result) {
    std::cout
        << "scenario=" << scenario_name
        << " status=" << WalkStatusName(result.status)
        << " pa=0x" << std::hex << result.physical_address
        << " error=0x" << result.page_fault_error_code
        << std::dec
        << " levels=" << result.visited_level_count
        << " rw=" << result.permissions.writable
        << " user=" << result.permissions.user_accessible
        << " execute=" << result.permissions.executable
        << '\n';

    for (
        std::uint64_t path_index = 0U;
        path_index < result.visited_level_count;
        ++path_index) {
        const EntrySnapshot& entry = result.path[path_index];
        std::cout
            << "  level=" << static_cast<std::uint16_t>(entry.level)
            << " entry-address=0x" << std::hex << entry.entry_address
            << " entry-value=0x" << entry.entry_value
            << std::dec << '\n';
    }
}

}  // namespace
}  // namespace os::book::paging

int main() {
    using namespace os::book::paging;

    PageTableImage image;
    const bool four_kib_installed = InstallFourKilobytePath(
        image,
        OS_BOOK_PAGING_EXAMPLE_4K_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_4K_FRAME,
        OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS,
        OS_BOOK_PAGING_EXAMPLE_PRESENT | OS_BOOK_PAGING_EXAMPLE_USER);
    const bool nx_installed = InstallFourKilobytePath(
        image,
        OS_BOOK_PAGING_EXAMPLE_NX_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_4K_FRAME,
        OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS,
        OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS
            | OS_BOOK_PAGING_EXAMPLE_NO_EXECUTE);
    const bool read_only_installed = InstallFourKilobytePath(
        image,
        OS_BOOK_PAGING_EXAMPLE_READ_ONLY_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_4K_FRAME,
        OS_BOOK_PAGING_EXAMPLE_PRESENT | OS_BOOK_PAGING_EXAMPLE_USER,
        OS_BOOK_PAGING_EXAMPLE_TABLE_FLAGS);
    const bool two_mib_installed = InstallTwoMegabytePath(
        image,
        OS_BOOK_PAGING_EXAMPLE_2M_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_2M_FRAME);
    if (
        !four_kib_installed
        || !nx_installed
        || !read_only_installed
        || !two_mib_installed) {
        return OS_BOOK_PAGING_EXAMPLE_EXIT_FAILURE;
    }

    const WalkResult four_kib_result = WalkFourLevel(
        image,
        OS_BOOK_PAGING_EXAMPLE_4K_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_PML4,
        AccessType::Read,
        PrivilegeLevel::User,
        true);
    const WalkResult two_mib_result = WalkFourLevel(
        image,
        OS_BOOK_PAGING_EXAMPLE_2M_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_PML4,
        AccessType::Read,
        PrivilegeLevel::Supervisor,
        true);
    const WalkResult nx_result = WalkFourLevel(
        image,
        OS_BOOK_PAGING_EXAMPLE_NX_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_PML4,
        AccessType::Execute,
        PrivilegeLevel::User,
        true);
    const WalkResult read_only_result = WalkFourLevel(
        image,
        OS_BOOK_PAGING_EXAMPLE_READ_ONLY_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_PML4,
        AccessType::Write,
        PrivilegeLevel::User,
        true);
    const WalkResult not_present_result = WalkFourLevel(
        image,
        OS_BOOK_PAGING_EXAMPLE_NON_PRESENT_ADDRESS,
        OS_BOOK_PAGING_EXAMPLE_PML4,
        AccessType::Read,
        PrivilegeLevel::User,
        true);

    PrintResult(OS_BOOK_PAGING_EXAMPLE_SCENARIO_4K, four_kib_result);
    PrintResult(OS_BOOK_PAGING_EXAMPLE_SCENARIO_2M, two_mib_result);
    PrintResult(OS_BOOK_PAGING_EXAMPLE_SCENARIO_NX, nx_result);
    PrintResult(
        OS_BOOK_PAGING_EXAMPLE_SCENARIO_READ_ONLY,
        read_only_result);
    PrintResult(
        OS_BOOK_PAGING_EXAMPLE_SCENARIO_NOT_PRESENT,
        not_present_result);

    const bool results_match =
        four_kib_result.status == WalkStatus::Translated
        && four_kib_result.physical_address
            == OS_BOOK_PAGING_EXAMPLE_EXPECTED_4K_PHYSICAL_ADDRESS
        && two_mib_result.status == WalkStatus::Translated
        && two_mib_result.physical_address
            == OS_BOOK_PAGING_EXAMPLE_EXPECTED_2M_PHYSICAL_ADDRESS
        && nx_result.status == WalkStatus::ExecuteDenied
        && nx_result.page_fault_error_code
            == OS_BOOK_PAGING_EXAMPLE_EXPECTED_NX_ERROR_CODE
        && read_only_result.status == WalkStatus::WriteDenied
        && read_only_result.page_fault_error_code
            == OS_BOOK_PAGING_EXAMPLE_EXPECTED_WRITE_ERROR_CODE
        && not_present_result.status == WalkStatus::NotPresent
        && not_present_result.page_fault_error_code
            == OS_BOOK_PAGING_EXAMPLE_EXPECTED_ABSENT_ERROR_CODE;
    return results_match
        ? OS_BOOK_PAGING_EXAMPLE_EXIT_SUCCESS
        : OS_BOOK_PAGING_EXAMPLE_EXIT_FAILURE;
}
