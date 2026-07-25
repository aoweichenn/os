#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_MEMORY_MAP_ENTRY_SIZE_BYTES = 24ULL;
inline constexpr uint64_t OS_KERNEL_MEMORY_MAP_MAXIMUM_ENTRY_COUNT = 128ULL;
inline constexpr uint32_t OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE = 1U;

struct PhysicalMemoryMapEntry final {
    uint64_t base_address;
    uint64_t length_bytes;
    uint32_t type;
    uint32_t attributes;
};

struct PhysicalMemorySummary final {
    uint64_t total_bytes;
    uint64_t usable_bytes;
    uint64_t managed_usable_bytes;
    uint64_t highest_address_exclusive;
    uint64_t highest_usable_address_exclusive;
    uint64_t usable_region_count;
};

struct PhysicalMemoryRange final {
    uint64_t begin_address;
    uint64_t length_bytes;
};

enum class PhysicalMemoryMapValidationStatus : uint64_t {
    Succeeded,
    NullEntries,
    InvalidEntryCount,
    InvalidManagedLimit,
    EmptyRegion,
    AddressOverflow,
    UnsortedRegions,
    OverlappingRegions,
    TotalSizeOverflow,
    UsableSizeOverflow,
    NoManagedUsableMemory,
};

enum class PhysicalMemoryRangeSearchStatus : uint64_t {
    Succeeded,
    NullEntries,
    InvalidEntryCount,
    NullReservations,
    InvalidSearchRange,
    InvalidLength,
    InvalidAlignment,
    InvalidReservation,
    NoSuitableRange,
};

[[nodiscard]] PhysicalMemoryMapValidationStatus
ValidateAndSummarizePhysicalMemoryMap(const PhysicalMemoryMapEntry *entries, uint64_t entry_count,
                                      uint64_t managed_limit_address,
                                      PhysicalMemorySummary &summary) noexcept;
[[nodiscard]] PhysicalMemoryRangeSearchStatus
FindUsablePhysicalMemoryRange(const PhysicalMemoryMapEntry *entries, uint64_t entry_count,
                              const PhysicalMemoryRange *reservations, uint64_t reservation_count,
                              uint64_t minimum_address, uint64_t maximum_address_exclusive,
                              uint64_t required_length_bytes, uint64_t required_alignment_bytes,
                              PhysicalMemoryRange &range) noexcept;

static_assert(sizeof(PhysicalMemoryMapEntry) == OS_KERNEL_MEMORY_MAP_ENTRY_SIZE_BYTES);

}
