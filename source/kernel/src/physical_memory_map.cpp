#include "os/kernel/physical_memory_map.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MEMORY_MAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_MAP_PREVIOUS_ENTRY_OFFSET = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_MAP_ALIGNMENT_DECREMENT = 1ULL;

[[nodiscard]] bool TryAdd(const uint64_t left, const uint64_t right, uint64_t &result) noexcept {
    const uint64_t maximumValue = UINT64_MAX;
    if (right > maximumValue - left) {
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
                              uint64_t &alignedValue) noexcept {
    const uint64_t alignmentMask = alignment - OS_KERNEL_MEMORY_MAP_ALIGNMENT_DECREMENT;
    if (value > UINT64_MAX - alignmentMask) {
        return false;
    }
    alignedValue = (value + alignmentMask) & ~alignmentMask;
    return true;
}

}

PhysicalMemoryMapValidationStatus
ValidateAndSummarizePhysicalMemoryMap(const PhysicalMemoryMapEntry *entries,
                                      const uint64_t entryCount, const uint64_t managedLimitAddress,
                                      PhysicalMemorySummary &summary) noexcept {
    if (entries == nullptr) {
        return PhysicalMemoryMapValidationStatus::NullEntries;
    }
    if (entryCount == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE ||
        entryCount > OS_KERNEL_MEMORY_MAP_MAXIMUM_ENTRY_COUNT) {
        return PhysicalMemoryMapValidationStatus::InvalidEntryCount;
    }
    if (managedLimitAddress == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE) {
        return PhysicalMemoryMapValidationStatus::InvalidManagedLimit;
    }

    PhysicalMemorySummary candidateSummary{};
    uint64_t previousEndAddress = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
    bool hasPreviousRegion = false;

    for (uint64_t entryIndex = 0ULL; entryIndex < entryCount; ++entryIndex) {
        const PhysicalMemoryMapEntry &entry = entries[entryIndex];
        if (entry.lengthBytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE) {
            return PhysicalMemoryMapValidationStatus::EmptyRegion;
        }

        uint64_t endAddress = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
        if (!TryAdd(entry.baseAddress, entry.lengthBytes, endAddress)) {
            return PhysicalMemoryMapValidationStatus::AddressOverflow;
        }
        if (hasPreviousRegion && entry.baseAddress < previousEndAddress) {
            if (entry.baseAddress <
                entries[entryIndex - OS_KERNEL_MEMORY_MAP_PREVIOUS_ENTRY_OFFSET].baseAddress) {
                return PhysicalMemoryMapValidationStatus::UnsortedRegions;
            }
            return PhysicalMemoryMapValidationStatus::OverlappingRegions;
        }
        previousEndAddress = endAddress;
        hasPreviousRegion = true;

        if (!TryAdd(candidateSummary.totalBytes, entry.lengthBytes, candidateSummary.totalBytes)) {
            return PhysicalMemoryMapValidationStatus::TotalSizeOverflow;
        }
        if (endAddress > candidateSummary.highestAddressExclusive) {
            candidateSummary.highestAddressExclusive = endAddress;
        }

        if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE) {
            continue;
        }
        ++candidateSummary.usableRegionCount;
        if (endAddress > candidateSummary.highestUsableAddressExclusive) {
            candidateSummary.highestUsableAddressExclusive = endAddress;
        }
        if (!TryAdd(candidateSummary.usableBytes, entry.lengthBytes,
                    candidateSummary.usableBytes)) {
            return PhysicalMemoryMapValidationStatus::UsableSizeOverflow;
        }

        const uint64_t managedBegin =
            entry.baseAddress < managedLimitAddress ? entry.baseAddress : managedLimitAddress;
        const uint64_t managedEnd =
            endAddress < managedLimitAddress ? endAddress : managedLimitAddress;
        if (managedBegin < managedEnd &&
            !TryAdd(candidateSummary.managedUsableBytes, managedEnd - managedBegin,
                    candidateSummary.managedUsableBytes)) {
            return PhysicalMemoryMapValidationStatus::UsableSizeOverflow;
        }
    }

    if (candidateSummary.managedUsableBytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE) {
        return PhysicalMemoryMapValidationStatus::NoManagedUsableMemory;
    }
    summary = candidateSummary;
    return PhysicalMemoryMapValidationStatus::Succeeded;
}

PhysicalMemoryRangeSearchStatus FindUsablePhysicalMemoryRange(
    const PhysicalMemoryMapEntry *entries, const uint64_t entryCount,
    const PhysicalMemoryRange *reservations, const uint64_t reservationCount,
    const uint64_t minimumAddress, const uint64_t maximumAddressExclusive,
    const uint64_t requiredLengthBytes, const uint64_t requiredAlignmentBytes,
    PhysicalMemoryRange &range) noexcept {
    if (entries == nullptr) {
        return PhysicalMemoryRangeSearchStatus::NullEntries;
    }
    if (entryCount == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE ||
        entryCount > OS_KERNEL_MEMORY_MAP_MAXIMUM_ENTRY_COUNT) {
        return PhysicalMemoryRangeSearchStatus::InvalidEntryCount;
    }
    if (reservationCount != OS_KERNEL_MEMORY_MAP_EMPTY_VALUE && reservations == nullptr) {
        return PhysicalMemoryRangeSearchStatus::NullReservations;
    }
    if (minimumAddress >= maximumAddressExclusive) {
        return PhysicalMemoryRangeSearchStatus::InvalidSearchRange;
    }
    if (requiredLengthBytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE) {
        return PhysicalMemoryRangeSearchStatus::InvalidLength;
    }
    if (!IsPowerOfTwo(requiredAlignmentBytes)) {
        return PhysicalMemoryRangeSearchStatus::InvalidAlignment;
    }
    for (uint64_t reservationIndex = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
         reservationIndex < reservationCount; ++reservationIndex) {
        const PhysicalMemoryRange &reservation = reservations[reservationIndex];
        if (reservation.lengthBytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE ||
            reservation.beginAddress > UINT64_MAX - reservation.lengthBytes) {
            return PhysicalMemoryRangeSearchStatus::InvalidReservation;
        }
    }

    for (uint64_t entryIndex = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE; entryIndex < entryCount;
         ++entryIndex) {
        const PhysicalMemoryMapEntry &entry = entries[entryIndex];
        if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE ||
            entry.lengthBytes == OS_KERNEL_MEMORY_MAP_EMPTY_VALUE ||
            entry.baseAddress > UINT64_MAX - entry.lengthBytes) {
            continue;
        }
        const uint64_t entryEndAddress = entry.baseAddress + entry.lengthBytes;
        const uint64_t searchBeginAddress =
            entry.baseAddress > minimumAddress ? entry.baseAddress : minimumAddress;
        const uint64_t searchEndAddress =
            entryEndAddress < maximumAddressExclusive ? entryEndAddress : maximumAddressExclusive;
        uint64_t candidateBeginAddress = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
        if (searchBeginAddress >= searchEndAddress ||
            !TryAlignUp(searchBeginAddress, requiredAlignmentBytes, candidateBeginAddress)) {
            continue;
        }

        while (candidateBeginAddress < searchEndAddress &&
               requiredLengthBytes <= searchEndAddress - candidateBeginAddress) {
            const uint64_t candidateEndAddress = candidateBeginAddress + requiredLengthBytes;
            uint64_t blockingReservationEndAddress = candidateBeginAddress;
            for (uint64_t reservationIndex = OS_KERNEL_MEMORY_MAP_EMPTY_VALUE;
                 reservationIndex < reservationCount; ++reservationIndex) {
                const PhysicalMemoryRange &reservation = reservations[reservationIndex];
                const uint64_t reservationEndAddress =
                    reservation.beginAddress + reservation.lengthBytes;
                if (candidateBeginAddress < reservationEndAddress &&
                    reservation.beginAddress < candidateEndAddress &&
                    reservationEndAddress > blockingReservationEndAddress) {
                    blockingReservationEndAddress = reservationEndAddress;
                }
            }
            if (blockingReservationEndAddress == candidateBeginAddress) {
                range = PhysicalMemoryRange{
                    .beginAddress = candidateBeginAddress,
                    .lengthBytes = requiredLengthBytes,
                };
                return PhysicalMemoryRangeSearchStatus::Succeeded;
            }
            if (!TryAlignUp(blockingReservationEndAddress, requiredAlignmentBytes,
                            candidateBeginAddress)) {
                break;
            }
        }
    }
    return PhysicalMemoryRangeSearchStatus::NoSuitableRange;
}

}
