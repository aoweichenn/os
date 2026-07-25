#include "os/kernel/physical_memory_map.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MEMORY_MAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_MAP_PREVIOUS_ENTRY_OFFSET = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_MAP_ALIGNMENT_DECREMENT = 1ULL;

[[nodiscard]] bool TryAdd(const uint64_t left, const uint64_t right, uint64_t &result) noexcept {
    const uint64_t maximum_value = UINT64_MAX;
    if (right > maximum_value - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != OS_KERNEL_MEMORY_MAP_EMPTY_VALUE &&
           (value & (value - OS_KERNEL_MEMORY_MAP_ALIGNMENT_DECREMENT)) ==
               OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
}

[[nodiscard]] bool TryAlignUp(const uint64_t value, const uint64_t alignment,
                              uint64_t &aligned_value) noexcept {
    const uint64_t alignment_mask = alignment - OS_KERNEL_MEMORY_MAP_ALIGNMENT_DECREMENT;
    if (value > UINT64_MAX - alignment_mask) {
        return false;
    }
    aligned_value = (value + alignment_mask) & ~alignment_mask;
    return true;
}
}

PhysicalMemoryMapValidationStatus ValidateAndSummarizePhysicalMemoryMap(
    const PhysicalMemoryMapEntry *entries, const uint64_t entry_count,
    const uint64_t managed_limit_address, PhysicalMemorySummary &summary) noexcept {
    if (entries == nullptr) {
        return PhysicalMemoryMapValidationStatus::NullEntries;
    }
    if (entry_count == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE ||
        entry_count > OS_KERNEL_MEMORY_MAP_MAXIMUM_ENTRY_COUNT) {
        return PhysicalMemoryMapValidationStatus::InvalidEntryCount;
    }
    if (managed_limit_address == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE) {
        return PhysicalMemoryMapValidationStatus::InvalidManagedLimit;
    }

    PhysicalMemorySummary candidate_summary{};
    uint64_t previous_end_address = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
    bool has_previous_region = false;

    for (uint64_t entry_index = 0ULL; entry_index < entry_count; ++entry_index) {
        const PhysicalMemoryMapEntry &entry = entries[entry_index];
        if (entry.length_bytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE) {
            return PhysicalMemoryMapValidationStatus::EmptyRegion;
        }

        uint64_t end_address = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
        if (!TryAdd(entry.base_address, entry.length_bytes, end_address)) {
            return PhysicalMemoryMapValidationStatus::AddressOverflow;
        }
        if (has_previous_region && entry.base_address < previous_end_address) {
            if (entry.base_address <
                entries[entry_index - OS_KERNEL_MEMORY_MAP_PREVIOUS_ENTRY_OFFSET].base_address) {
                return PhysicalMemoryMapValidationStatus::UnsortedRegions;
            }
            return PhysicalMemoryMapValidationStatus::OverlappingRegions;
        }
        previous_end_address = end_address;
        has_previous_region = true;

        if (!TryAdd(candidate_summary.total_bytes, entry.length_bytes,
                    candidate_summary.total_bytes)) {
            return PhysicalMemoryMapValidationStatus::TotalSizeOverflow;
        }
        if (end_address > candidate_summary.highest_address_exclusive) {
            candidate_summary.highest_address_exclusive = end_address;
        }

        if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE) {
            continue;
        }
        ++candidate_summary.usable_region_count;
        if (end_address > candidate_summary.highest_usable_address_exclusive) {
            candidate_summary.highest_usable_address_exclusive = end_address;
        }
        if (!TryAdd(candidate_summary.usable_bytes, entry.length_bytes,
                    candidate_summary.usable_bytes)) {
            return PhysicalMemoryMapValidationStatus::UsableSizeOverflow;
        }

        const uint64_t managed_begin =
            entry.base_address < managed_limit_address ? entry.base_address : managed_limit_address;
        const uint64_t managed_end =
            end_address < managed_limit_address ? end_address : managed_limit_address;
        if (managed_begin < managed_end &&
            !TryAdd(candidate_summary.managed_usable_bytes, managed_end - managed_begin,
                    candidate_summary.managed_usable_bytes)) {
            return PhysicalMemoryMapValidationStatus::UsableSizeOverflow;
        }
    }

    if (candidate_summary.managed_usable_bytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE) {
        return PhysicalMemoryMapValidationStatus::NoManagedUsableMemory;
    }
    summary = candidate_summary;
    return PhysicalMemoryMapValidationStatus::Succeeded;
}

PhysicalMemoryRangeSearchStatus FindUsablePhysicalMemoryRange(
    const PhysicalMemoryMapEntry *entries, const uint64_t entry_count,
    const PhysicalMemoryRange *reservations, const uint64_t reservation_count,
    const uint64_t minimum_address, const uint64_t maximum_address_exclusive,
    const uint64_t required_length_bytes, const uint64_t required_alignment_bytes,
    PhysicalMemoryRange &range) noexcept {
    if (entries == nullptr) {
        return PhysicalMemoryRangeSearchStatus::NullEntries;
    }
    if (entry_count == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE ||
        entry_count > OS_KERNEL_MEMORY_MAP_MAXIMUM_ENTRY_COUNT) {
        return PhysicalMemoryRangeSearchStatus::InvalidEntryCount;
    }
    if (reservation_count != OS_KERNEL_MEMORY_MAP_EMPTY_VALUE && reservations == nullptr) {
        return PhysicalMemoryRangeSearchStatus::NullReservations;
    }
    if (minimum_address >= maximum_address_exclusive) {
        return PhysicalMemoryRangeSearchStatus::InvalidSearchRange;
    }
    if (required_length_bytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE) {
        return PhysicalMemoryRangeSearchStatus::InvalidLength;
    }
    if (!IsPowerOfTwo(required_alignment_bytes)) {
        return PhysicalMemoryRangeSearchStatus::InvalidAlignment;
    }
    for (uint64_t reservation_index = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
         reservation_index < reservation_count; ++reservation_index) {
        const PhysicalMemoryRange &reservation = reservations[reservation_index];
        if (reservation.length_bytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE ||
            reservation.begin_address > UINT64_MAX - reservation.length_bytes) {
            return PhysicalMemoryRangeSearchStatus::InvalidReservation;
        }
    }

    for (uint64_t entry_index = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE; entry_index < entry_count;
         ++entry_index) {
        const PhysicalMemoryMapEntry &entry = entries[entry_index];
        if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE ||
            entry.length_bytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE ||
            entry.base_address > UINT64_MAX - entry.length_bytes) {
            continue;
        }
        const uint64_t entry_end_address = entry.base_address + entry.length_bytes;
        const uint64_t search_begin_address =
            entry.base_address > minimum_address ? entry.base_address : minimum_address;
        const uint64_t search_end_address = entry_end_address < maximum_address_exclusive
                                                ? entry_end_address
                                                : maximum_address_exclusive;
        uint64_t candidate_begin_address = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
        if (search_begin_address >= search_end_address ||
            !TryAlignUp(search_begin_address, required_alignment_bytes, candidate_begin_address)) {
            continue;
        }

        while (candidate_begin_address < search_end_address &&
               required_length_bytes <= search_end_address - candidate_begin_address) {
            const uint64_t candidate_end_address = candidate_begin_address + required_length_bytes;
            uint64_t blocking_reservation_end_address = candidate_begin_address;
            for (uint64_t reservation_index = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
                 reservation_index < reservation_count; ++reservation_index) {
                const PhysicalMemoryRange &reservation = reservations[reservation_index];
                const uint64_t reservation_end_address =
                    reservation.begin_address + reservation.length_bytes;
                if (candidate_begin_address < reservation_end_address &&
                    reservation.begin_address < candidate_end_address &&
                    reservation_end_address > blocking_reservation_end_address) {
                    blocking_reservation_end_address = reservation_end_address;
                }
            }
            if (blocking_reservation_end_address == candidate_begin_address) {
                range = PhysicalMemoryRange{
                    .begin_address = candidate_begin_address,
                    .length_bytes = required_length_bytes,
                };
                return PhysicalMemoryRangeSearchStatus::Succeeded;
            }
            if (!TryAlignUp(blocking_reservation_end_address, required_alignment_bytes,
                            candidate_begin_address)) {
                break;
            }
        }
    }
    return PhysicalMemoryRangeSearchStatus::NoSuitableRange;
}
}
