#include "os/kernel/physical_memory_map.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MEMORY_MAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_MAP_PREVIOUS_ENTRY_OFFSET = 1ULL;

[[nodiscard]] bool tryAdd(const uint64_t left, const uint64_t right, uint64_t &result) noexcept {
    const uint64_t maximumValue = UINT64_MAX;
    if (right > maximumValue - left) {
        return false;
    }
    result = left + right;
    return true;
}

}

PhysicalMemoryMapValidationStatus
validateAndSummarizePhysicalMemoryMap(const PhysicalMemoryMapEntry *entries,
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
        if (!tryAdd(entry.baseAddress, entry.lengthBytes, endAddress)) {
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

        if (!tryAdd(candidateSummary.totalBytes, entry.lengthBytes, candidateSummary.totalBytes)) {
            return PhysicalMemoryMapValidationStatus::TotalSizeOverflow;
        }
        if (endAddress > candidateSummary.highestAddressExclusive) {
            candidateSummary.highestAddressExclusive = endAddress;
        }

        if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE) {
            continue;
        }
        ++candidateSummary.usableRegionCount;
        if (!tryAdd(candidateSummary.usableBytes, entry.lengthBytes,
                    candidateSummary.usableBytes)) {
            return PhysicalMemoryMapValidationStatus::UsableSizeOverflow;
        }

        const uint64_t managedBegin =
            entry.baseAddress < managedLimitAddress ? entry.baseAddress : managedLimitAddress;
        const uint64_t managedEnd =
            endAddress < managedLimitAddress ? endAddress : managedLimitAddress;
        if (managedBegin < managedEnd &&
            !tryAdd(candidateSummary.managedUsableBytes, managedEnd - managedBegin,
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

}
