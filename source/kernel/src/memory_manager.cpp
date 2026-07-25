#include "os/kernel/memory_manager.hpp"

#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/page_table.hpp"
#include "os/kernel/physical_frame_allocator.hpp"
#include "os/kernel/physical_memory_map.hpp"
#include "os/kernel/process_memory_layout.hpp"
#include "os/kernel/processor.hpp"
#include "os/kernel/user_elf.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES = 1ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_LARGE_PAGE_MASK =
    OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_PAGE_COUNT =
    OS_KERNEL_MEMORY_HEAP_SIZE_BYTES / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_BOOTSTRAP_RESERVATION_COUNT = 3ULL;
constexpr uint64_t OS_KERNEL_MEMORY_BOOTSTRAP_METADATA_ALIGNMENT_BYTES =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_MINIMUM_ADDRESS =
    4ULL * 1024ULL * 1024ULL * 1024ULL + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_SIZE_BYTES =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_ALIGNMENT_BYTES =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_FIRST_PATTERN = 0x13579BDF2468ACE0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SECOND_PATTERN = 0xC001D00DC0FFEE11ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_PATTERN = 0x484947484652414DULL;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_PATTERN = 0x4449524543544D50ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_VALUE_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_VALUE_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_MINIMUM_PAGE_TABLE_PHYSICAL_LIMIT =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;

KernelMemoryStatistics currentKernelMemoryStatistics{};
uint64_t currentManagedPhysicalAddressLimit = 0ULL;
uint64_t currentPageTablePhysicalAddressLimit = 0ULL;
bool directMapActive = false;

struct DirectMapStatistics final {
    uint64_t mappedBytes;
    uint64_t largePageCount;
    uint64_t smallPageCount;
};

extern "C" uint8_t osKernelImageStart[];
extern "C" uint8_t osKernelImageEnd[];
extern "C" uint8_t osKernelTextStart[];
extern "C" uint8_t osKernelTextEnd[];
extern "C" uint8_t osKernelReadOnlyDataStart[];
extern "C" uint8_t osKernelReadOnlyDataEnd[];
extern "C" uint8_t osKernelWritableDataStart[];
extern "C" uint8_t osKernelWritableDataEnd[];

[[nodiscard]] uint64_t AddressOf(uint8_t *symbol) noexcept {
    return reinterpret_cast<uint64_t>(symbol);
}

[[nodiscard]] uint64_t AlignUpToPage(const uint64_t address) noexcept {
    return (address + OS_KERNEL_MEMORY_PAGE_MASK) & ~OS_KERNEL_MEMORY_PAGE_MASK;
}

[[nodiscard]] uint64_t AlignDownToPage(const uint64_t address) noexcept {
    return address & ~OS_KERNEL_MEMORY_PAGE_MASK;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] PhysicalFrameAllocator &FrameAllocator() noexcept {
    static PhysicalFrameAllocator allocator{};
    return allocator;
}

[[nodiscard]] PageTableManager &GetPageTableManager() noexcept {
    static PageTableManager manager{
        FrameAllocator(),
        PageTableMemoryAccess{
            .maximumPhysicalAddressExclusive = OS_KERNEL_MEMORY_MINIMUM_PAGE_TABLE_PHYSICAL_LIMIT,
            .physicalMemoryVirtualBase = 0ULL,
            .allocationMaximumPhysicalAddressExclusive =
                OS_KERNEL_MEMORY_MINIMUM_PAGE_TABLE_PHYSICAL_LIMIT,
            .invalidateActiveMappings = false,
        },
    };
    return manager;
}

[[nodiscard]] PageTableMemoryAccess ActivePageTableMemoryAccess() noexcept {
    return PageTableMemoryAccess{
        .maximumPhysicalAddressExclusive = currentPageTablePhysicalAddressLimit,
        .physicalMemoryVirtualBase = OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE,
        .allocationMaximumPhysicalAddressExclusive = currentManagedPhysicalAddressLimit,
        .invalidateActiveMappings = true,
    };
}

[[nodiscard]] bool IsGuardPage(const uint64_t pageAddress, const BootInfo &bootInfo) noexcept {
    const uint64_t earlyStackGuardAddress =
        bootInfo.kernelStackTopPhysicalAddress - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    if (pageAddress == earlyStackGuardAddress) {
        return true;
    }
    for (uint64_t guardPageIndex = 0ULL;
         guardPageIndex < OS_KERNEL_DESCRIPTOR_INTERRUPT_STACK_GUARD_PAGE_COUNT; ++guardPageIndex) {
        if (pageAddress == InterruptStackGuardPageAddress(guardPageIndex)) {
            return true;
        }
    }
    for (uint64_t processIndex = 0ULL; processIndex < OS_KERNEL_PROCESS_CAPACITY; ++processIndex) {
        if (pageAddress == ProcessKernelStackGuardPageAddress(processIndex)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] PagePermissions IdentityPermissions(const uint64_t pageAddress) noexcept {
    const uint64_t textBegin = AddressOf(osKernelTextStart);
    const uint64_t textEnd = AlignUpToPage(AddressOf(osKernelTextEnd));
    if (pageAddress >= textBegin && pageAddress < textEnd) {
        return PagePermissions{
            .writable = false,
            .executable = true,
            .userAccessible = false,
            .cacheDisabled = false,
        };
    }
    const uint64_t readOnlyBegin = AddressOf(osKernelReadOnlyDataStart);
    const uint64_t readOnlyEnd = AlignUpToPage(AddressOf(osKernelReadOnlyDataEnd));
    if (pageAddress >= readOnlyBegin && pageAddress < readOnlyEnd) {
        return PagePermissions{
            .writable = false,
            .executable = false,
            .userAccessible = false,
            .cacheDisabled = false,
        };
    }
    return PagePermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
        .cacheDisabled = false,
    };
}

[[nodiscard]] bool ReserveBootCriticalRanges(const BootInfo &bootInfo) noexcept {
    PhysicalFrameAllocator &allocator = FrameAllocator();
    const uint64_t kernelBegin = AddressOf(osKernelImageStart);
    const uint64_t kernelEnd = AddressOf(osKernelImageEnd);
    const uint64_t earlyStackBegin =
        bootInfo.kernelStackTopPhysicalAddress - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    return allocator.ReserveRange(0ULL, OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES) ==
               PhysicalFrameAllocatorStatus::Succeeded &&
           allocator.ReserveRange(kernelBegin, kernelEnd - kernelBegin) ==
               PhysicalFrameAllocatorStatus::Succeeded &&
           allocator.ReserveRange(earlyStackBegin, OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES) ==
               PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool FindFrameStateStorageRange(const PhysicalMemoryMapEntry *memoryMap,
                                              const uint64_t memoryMapEntryCount,
                                              const BootInfo &bootInfo,
                                              const uint64_t requiredStorageSizeBytes,
                                              PhysicalMemoryRange &storageRange) noexcept {
    const uint64_t kernelBegin = AddressOf(osKernelImageStart);
    const uint64_t kernelEnd = AddressOf(osKernelImageEnd);
    const uint64_t earlyStackBegin =
        bootInfo.kernelStackTopPhysicalAddress - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    const PhysicalMemoryRange reservations[OS_KERNEL_MEMORY_BOOTSTRAP_RESERVATION_COUNT] = {
        {
            .beginAddress = 0ULL,
            .lengthBytes = OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES,
        },
        {
            .beginAddress = kernelBegin,
            .lengthBytes = kernelEnd - kernelBegin,
        },
        {
            .beginAddress = earlyStackBegin,
            .lengthBytes = OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES,
        },
    };
    return FindUsablePhysicalMemoryRange(memoryMap, memoryMapEntryCount, reservations,
                                         OS_KERNEL_MEMORY_BOOTSTRAP_RESERVATION_COUNT,
                                         OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES,
                                         bootInfo.identityMappedSizeBytes, requiredStorageSizeBytes,
                                         OS_KERNEL_MEMORY_BOOTSTRAP_METADATA_ALIGNMENT_BYTES,
                                         storageRange) ==
           PhysicalMemoryRangeSearchStatus::Succeeded;
}

[[nodiscard]] bool ReserveFrameStateStorage(const PhysicalMemoryRange storageRange) noexcept {
    return FrameAllocator().ReserveRange(storageRange.beginAddress, storageRange.lengthBytes) ==
           PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool MapIdentityRange(const BootInfo &bootInfo) noexcept {
    PageTableManager &manager = GetPageTableManager();
    for (uint64_t pageAddress = 0ULL; pageAddress < bootInfo.identityMappedSizeBytes;
         pageAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        if (pageAddress == 0ULL || IsGuardPage(pageAddress, bootInfo)) {
            continue;
        }
        if (manager.MapPage(pageAddress, pageAddress, IdentityPermissions(pageAddress)) !=
            PageTableStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool MapDirectPage(const uint64_t physicalAddress,
                                 DirectMapStatistics &statistics) noexcept {
    const uint64_t virtualAddress = OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE + physicalAddress;
    const PagePermissions directMapPermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
        .cacheDisabled = false,
    };
    if (GetPageTableManager().MapPage(virtualAddress, physicalAddress, directMapPermissions) !=
        PageTableStatus::Succeeded) {
        return false;
    }
    statistics.mappedBytes += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    ++statistics.smallPageCount;
    return true;
}

[[nodiscard]] bool MapDirectLargePage(const uint64_t physicalAddress,
                                      DirectMapStatistics &statistics) noexcept {
    const uint64_t virtualAddress = OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE + physicalAddress;
    const PagePermissions directMapPermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
        .cacheDisabled = false,
    };
    if (GetPageTableManager().MapLargePage(virtualAddress, physicalAddress, directMapPermissions) !=
        PageTableStatus::Succeeded) {
        return false;
    }
    statistics.mappedBytes += OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES;
    ++statistics.largePageCount;
    return true;
}

[[nodiscard]] bool MapDirectMemoryRange(const PhysicalMemoryMapEntry &entry,
                                        DirectMapStatistics &statistics) noexcept {
    if (entry.type != OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE ||
        entry.baseAddress >= currentManagedPhysicalAddressLimit) {
        return true;
    }
    const uint64_t entryEndAddress = entry.baseAddress + entry.lengthBytes;
    uint64_t physicalAddress = AlignUpToPage(entry.baseAddress);
    const uint64_t mappedEndAddress =
        AlignDownToPage(Minimum(entryEndAddress, currentManagedPhysicalAddressLimit));
    while (physicalAddress < mappedEndAddress &&
           (physicalAddress & OS_KERNEL_MEMORY_LARGE_PAGE_MASK) != 0ULL) {
        if (!MapDirectPage(physicalAddress, statistics)) {
            return false;
        }
        physicalAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }
    while (physicalAddress < mappedEndAddress &&
           OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES <= mappedEndAddress - physicalAddress) {
        if (!MapDirectLargePage(physicalAddress, statistics)) {
            return false;
        }
        physicalAddress += OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES;
    }
    while (physicalAddress < mappedEndAddress) {
        if (!MapDirectPage(physicalAddress, statistics)) {
            return false;
        }
        physicalAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }
    return true;
}

[[nodiscard]] bool MapDirectMemory(const PhysicalMemoryMapEntry *memoryMap,
                                   const uint64_t memoryMapEntryCount,
                                   DirectMapStatistics &statistics) noexcept {
    for (uint64_t entryIndex = 0ULL; entryIndex < memoryMapEntryCount; ++entryIndex) {
        if (!MapDirectMemoryRange(memoryMap[entryIndex], statistics)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool MapKernelHeap() noexcept {
    PhysicalFrameAllocator &allocator = FrameAllocator();
    PageTableManager &manager = GetPageTableManager();
    const PagePermissions heapPermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
        .cacheDisabled = false,
    };
    for (uint64_t pageIndex = 0ULL; pageIndex < OS_KERNEL_MEMORY_HEAP_PAGE_COUNT; ++pageIndex) {
        PhysicalFrame frame{};
        if (allocator.Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
            return false;
        }
        const uint64_t virtualAddress =
            OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE + pageIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (manager.MapPage(virtualAddress, frame.physicalAddress, heapPermissions) !=
            PageTableStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool MapLocalApicRegisters() noexcept {
    if (!ProcessorSupportsLocalApic()) {
        return true;
    }
    const uint64_t localApicAddress = LocalApicPhysicalAddress();
    if (localApicAddress == 0ULL || (localApicAddress & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL) {
        return false;
    }
    const PagePermissions devicePermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
        .cacheDisabled = true,
    };
    return GetPageTableManager().MapPage(localApicAddress, localApicAddress, devicePermissions) ==
           PageTableStatus::Succeeded;
}

[[nodiscard]] bool MapWriteProtectionTestPage() noexcept {
    PhysicalFrame frame{};
    if (FrameAllocator().Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    const PagePermissions readOnlyPermissions{
        .writable = false,
        .executable = false,
        .userAccessible = false,
        .cacheDisabled = false,
    };
    PageTableManager &manager = GetPageTableManager();
    if (manager.MapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                        frame.physicalAddress, readOnlyPermissions) != PageTableStatus::Succeeded ||
        manager.MapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                        frame.physicalAddress,
                        readOnlyPermissions) != PageTableStatus::AlreadyMapped ||
        manager.UnmapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS) !=
            PageTableStatus::Succeeded) {
        return false;
    }
    PageMapping removedMapping{};
    if (manager.QueryPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS, removedMapping) !=
        PageTableStatus::NotMapped) {
        return false;
    }
    return manager.MapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                           frame.physicalAddress,
                           readOnlyPermissions) == PageTableStatus::Succeeded;
}

[[nodiscard]] bool ValidateMapping(const uint64_t virtualAddress,
                                   const uint64_t expectedPhysicalAddress,
                                   const PagePermissions expectedPermissions) noexcept {
    PageMapping mapping{};
    if (GetPageTableManager().QueryPage(virtualAddress, mapping) != PageTableStatus::Succeeded) {
        return false;
    }
    return mapping.physicalAddress == expectedPhysicalAddress &&
           mapping.permissions.writable == expectedPermissions.writable &&
           mapping.permissions.executable == expectedPermissions.executable &&
           mapping.permissions.userAccessible == expectedPermissions.userAccessible &&
           mapping.permissions.cacheDisabled == expectedPermissions.cacheDisabled;
}

[[nodiscard]] bool ValidatePermissions(const uint64_t virtualAddress,
                                       const PagePermissions expectedPermissions) noexcept {
    PageMapping mapping{};
    if (GetPageTableManager().QueryPage(virtualAddress, mapping) != PageTableStatus::Succeeded) {
        return false;
    }
    return mapping.physicalAddress != 0ULL &&
           mapping.permissions.writable == expectedPermissions.writable &&
           mapping.permissions.executable == expectedPermissions.executable &&
           mapping.permissions.userAccessible == expectedPermissions.userAccessible &&
           mapping.permissions.cacheDisabled == expectedPermissions.cacheDisabled;
}

[[nodiscard]] bool
ValidateKernelMappings(const BootInfo &bootInfo,
                       const PhysicalMemoryRange frameStateStorageRange) noexcept {
    const PagePermissions textPermissions{
        .writable = false,
        .executable = true,
        .userAccessible = false,
        .cacheDisabled = false,
    };
    const PagePermissions readOnlyPermissions{
        .writable = false,
        .executable = false,
        .userAccessible = false,
        .cacheDisabled = false,
    };
    const PagePermissions writablePermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
        .cacheDisabled = false,
    };
    PageMapping ignoredMapping{};
    if (!ValidateMapping(AddressOf(osKernelTextStart), AddressOf(osKernelTextStart),
                         textPermissions) ||
        !ValidateMapping(AddressOf(osKernelReadOnlyDataStart), AddressOf(osKernelReadOnlyDataStart),
                         readOnlyPermissions) ||
        !ValidateMapping(AddressOf(osKernelWritableDataStart), AddressOf(osKernelWritableDataStart),
                         writablePermissions) ||
        !ValidatePermissions(OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE, writablePermissions) ||
        !ValidatePermissions(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                             readOnlyPermissions) ||
        !ValidateMapping(OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE +
                             frameStateStorageRange.beginAddress,
                         frameStateStorageRange.beginAddress, writablePermissions)) {
        return false;
    }
    if (ProcessorSupportsLocalApic()) {
        const PagePermissions devicePermissions{
            .writable = true,
            .executable = false,
            .userAccessible = false,
            .cacheDisabled = true,
        };
        const uint64_t localApicAddress = LocalApicPhysicalAddress();
        if (!ValidateMapping(localApicAddress, localApicAddress, devicePermissions)) {
            return false;
        }
    }
    if (GetPageTableManager().QueryPage(0ULL, ignoredMapping) != PageTableStatus::NotMapped ||
        GetPageTableManager().QueryPage(OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS,
                                        ignoredMapping) != PageTableStatus::NotMapped) {
        return false;
    }
    const uint64_t earlyStackGuardAddress =
        bootInfo.kernelStackTopPhysicalAddress - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    if (GetPageTableManager().QueryPage(earlyStackGuardAddress, ignoredMapping) !=
        PageTableStatus::NotMapped) {
        return false;
    }
    for (uint64_t guardPageIndex = 0ULL;
         guardPageIndex < OS_KERNEL_DESCRIPTOR_INTERRUPT_STACK_GUARD_PAGE_COUNT; ++guardPageIndex) {
        if (GetPageTableManager().QueryPage(InterruptStackGuardPageAddress(guardPageIndex),
                                            ignoredMapping) != PageTableStatus::NotMapped) {
            return false;
        }
    }
    for (uint64_t processIndex = 0ULL; processIndex < OS_KERNEL_PROCESS_CAPACITY; ++processIndex) {
        if (GetPageTableManager().QueryPage(ProcessKernelStackGuardPageAddress(processIndex),
                                            ignoredMapping) != PageTableStatus::NotMapped) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RunHeapSelfTest() noexcept {
    void *smallAllocation = nullptr;
    if (GetKernelHeap().TryAllocate(OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_SIZE_BYTES,
                                    OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_ALIGNMENT_BYTES,
                                    smallAllocation) != KernelHeapStatus::Succeeded) {
        return false;
    }
    void *pageAllocation = nullptr;
    if (GetKernelHeap().TryAllocate(OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_SIZE_BYTES,
                                    OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_ALIGNMENT_BYTES,
                                    pageAllocation) != KernelHeapStatus::Succeeded) {
        return false;
    }
    volatile uint64_t *const smallValue = reinterpret_cast<volatile uint64_t *>(smallAllocation);
    volatile uint64_t *const pageValue = reinterpret_cast<volatile uint64_t *>(pageAllocation);
    *smallValue = OS_KERNEL_MEMORY_HEAP_SELF_TEST_FIRST_PATTERN;
    *pageValue = OS_KERNEL_MEMORY_HEAP_SELF_TEST_SECOND_PATTERN;
    return *smallValue == OS_KERNEL_MEMORY_HEAP_SELF_TEST_FIRST_PATTERN &&
           *pageValue == OS_KERNEL_MEMORY_HEAP_SELF_TEST_SECOND_PATTERN;
}

[[nodiscard]] bool RunHighMemorySelfTest(uint64_t &testPhysicalAddress) noexcept {
    testPhysicalAddress = 0ULL;
    if (currentManagedPhysicalAddressLimit <= OS_KERNEL_MEMORY_HIGH_FRAME_MINIMUM_ADDRESS) {
        return true;
    }
    PhysicalFrame frame{};
    if (FrameAllocator().AllocateInRange(OS_KERNEL_MEMORY_HIGH_FRAME_MINIMUM_ADDRESS,
                                         currentManagedPhysicalAddressLimit,
                                         frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    const uint64_t directMapAddress = PhysicalMemoryDirectMapAddress(frame.physicalAddress);
    PageMapping mapping{};
    if (directMapAddress == 0ULL ||
        GetPageTableManager().QueryPage(directMapAddress, mapping) != PageTableStatus::Succeeded ||
        mapping.physicalAddress != frame.physicalAddress || !mapping.permissions.writable ||
        mapping.permissions.executable || mapping.permissions.userAccessible ||
        mapping.permissions.cacheDisabled) {
        static_cast<void>(FrameAllocator().Release(frame));
        return false;
    }
    volatile uint64_t *const values = reinterpret_cast<volatile uint64_t *>(directMapAddress);
    values[OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_VALUE_INDEX] =
        OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_PATTERN;
    values[OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_VALUE_INDEX] =
        OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_PATTERN;
    const bool patternsValid = values[OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_VALUE_INDEX] ==
                                   OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_FIRST_PATTERN &&
                               values[OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_VALUE_INDEX] ==
                                   OS_KERNEL_MEMORY_HIGH_FRAME_SELF_TEST_SECOND_PATTERN;
    testPhysicalAddress = frame.physicalAddress;
    const PhysicalFrameAllocatorStatus releaseStatus = FrameAllocator().Release(frame);
    return patternsValid && releaseStatus == PhysicalFrameAllocatorStatus::Succeeded;
}

}

KernelMemoryInitializationStatus InitializeKernelMemory(const BootInfo &bootInfo) noexcept {
    const PhysicalMemoryMapEntry *const memoryMap =
        reinterpret_cast<const PhysicalMemoryMapEntry *>(bootInfo.physicalMemoryMapAddress);
    const uint64_t physicalAddressWidthBits = ProcessorPhysicalAddressWidthBits();
    const uint64_t virtualAddressWidthBits = ProcessorVirtualAddressWidthBits();
    const uint64_t fiveLevelPagingSupported = ProcessorSupportsFiveLevelPaging() ? 1ULL : 0ULL;
    const uint64_t processorPhysicalAddressLimit = ProcessorMaximumPhysicalAddressExclusive();
    if (physicalAddressWidthBits == 0ULL || virtualAddressWidthBits == 0ULL ||
        processorPhysicalAddressLimit == 0ULL) {
        return KernelMemoryInitializationStatus::InvalidProcessorAddressWidth;
    }
    currentPageTablePhysicalAddressLimit =
        Minimum(processorPhysicalAddressLimit, OS_KERNEL_MEMORY_DIRECT_MAP_CAPACITY_BYTES);

    PhysicalMemorySummary memorySummary{};
    if (ValidateAndSummarizePhysicalMemoryMap(
            memoryMap, bootInfo.physicalMemoryMapEntryCount, currentPageTablePhysicalAddressLimit,
            memorySummary) != PhysicalMemoryMapValidationStatus::Succeeded) {
        return KernelMemoryInitializationStatus::InvalidMemoryMap;
    }
    currentManagedPhysicalAddressLimit = AlignDownToPage(
        Minimum(memorySummary.highestUsableAddressExclusive, currentPageTablePhysicalAddressLimit));
    if (currentManagedPhysicalAddressLimit == 0ULL ||
        currentManagedPhysicalAddressLimit < bootInfo.identityMappedSizeBytes) {
        return KernelMemoryInitializationStatus::InvalidManagedPhysicalAddressLimit;
    }
    if (ValidateAndSummarizePhysicalMemoryMap(memoryMap, bootInfo.physicalMemoryMapEntryCount,
                                              currentManagedPhysicalAddressLimit, memorySummary) !=
        PhysicalMemoryMapValidationStatus::Succeeded) {
        return KernelMemoryInitializationStatus::InvalidMemoryMap;
    }

    const uint64_t requiredFrameStateStorageSizeBytes =
        CalculatePhysicalFrameStateStorageSizeBytes(currentManagedPhysicalAddressLimit);
    const uint64_t frameStateStorageSizeBytes = AlignUpToPage(requiredFrameStateStorageSizeBytes);
    PhysicalMemoryRange frameStateStorageRange{};
    if (requiredFrameStateStorageSizeBytes == 0ULL || frameStateStorageSizeBytes == 0ULL ||
        !FindFrameStateStorageRange(memoryMap, bootInfo.physicalMemoryMapEntryCount, bootInfo,
                                    frameStateStorageSizeBytes, frameStateStorageRange)) {
        return KernelMemoryInitializationStatus::FrameStateStorageUnavailable;
    }
    if (FrameAllocator().ConfigureStateStorage(
            reinterpret_cast<uint8_t *>(frameStateStorageRange.beginAddress),
            frameStateStorageRange.lengthBytes) != PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelMemoryInitializationStatus::FrameAllocatorConfigurationFailed;
    }
    if (FrameAllocator().Initialize(memoryMap, bootInfo.physicalMemoryMapEntryCount,
                                    currentManagedPhysicalAddressLimit) !=
        PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelMemoryInitializationStatus::FrameAllocatorInitializationFailed;
    }
    if (!ReserveBootCriticalRanges(bootInfo) || !ReserveFrameStateStorage(frameStateStorageRange)) {
        return KernelMemoryInitializationStatus::ReservationFailed;
    }
    if (GetPageTableManager().SetMemoryAccess(PageTableMemoryAccess{
            .maximumPhysicalAddressExclusive = currentPageTablePhysicalAddressLimit,
            .physicalMemoryVirtualBase = 0ULL,
            .allocationMaximumPhysicalAddressExclusive = bootInfo.identityMappedSizeBytes,
            .invalidateActiveMappings = false,
        }) != PageTableStatus::Succeeded) {
        return KernelMemoryInitializationStatus::PageTableMemoryAccessFailed;
    }
    if (GetPageTableManager().Initialize() != PageTableStatus::Succeeded) {
        return KernelMemoryInitializationStatus::PageTableInitializationFailed;
    }
    if (!MapIdentityRange(bootInfo)) {
        return KernelMemoryInitializationStatus::IdentityMappingFailed;
    }
    DirectMapStatistics directMapStatistics{};
    if (!MapDirectMemory(memoryMap, bootInfo.physicalMemoryMapEntryCount, directMapStatistics)) {
        return KernelMemoryInitializationStatus::DirectMapMappingFailed;
    }
    if (!MapLocalApicRegisters()) {
        return KernelMemoryInitializationStatus::LocalApicMappingFailed;
    }
    if (!MapKernelHeap()) {
        return KernelMemoryInitializationStatus::HeapMappingFailed;
    }
    if (!MapWriteProtectionTestPage()) {
        return KernelMemoryInitializationStatus::ProtectionTestMappingFailed;
    }
    if (!EnableKernelMemoryProtection()) {
        return KernelMemoryInitializationStatus::MemoryProtectionUnsupported;
    }
    ActivatePageTable(GetPageTableManager().RootPhysicalAddress());
    if (ReadPageTableRoot() != GetPageTableManager().RootPhysicalAddress() ||
        !KernelMemoryProtectionEnabled()) {
        return KernelMemoryInitializationStatus::PageTableActivationFailed;
    }
    if (GetPageTableManager().SetMemoryAccess(ActivePageTableMemoryAccess()) !=
        PageTableStatus::Succeeded) {
        return KernelMemoryInitializationStatus::PageTableMemoryAccessFailed;
    }
    directMapActive = true;
    if (!ValidateKernelMappings(bootInfo, frameStateStorageRange)) {
        return KernelMemoryInitializationStatus::PermissionValidationFailed;
    }
    if (GetKernelHeap().Initialize(OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE,
                                   OS_KERNEL_MEMORY_HEAP_SIZE_BYTES) !=
        KernelHeapStatus::Succeeded) {
        return KernelMemoryInitializationStatus::HeapInitializationFailed;
    }
    if (!RunHeapSelfTest()) {
        return KernelMemoryInitializationStatus::HeapSelfTestFailed;
    }
    uint64_t highMemoryTestPhysicalAddress = 0ULL;
    if (!RunHighMemorySelfTest(highMemoryTestPhysicalAddress)) {
        return KernelMemoryInitializationStatus::HighMemorySelfTestFailed;
    }

    const PhysicalFrameAllocatorStatistics frameStatistics = FrameAllocator().Statistics();
    const uint64_t managedUsableMemoryBytes =
        (frameStatistics.freeFrameCount + frameStatistics.allocatedFrameCount +
         frameStatistics.reservedFrameCount) *
        OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    currentKernelMemoryStatistics = KernelMemoryStatistics{
        .memoryMapEntryCount = bootInfo.physicalMemoryMapEntryCount,
        .describedAddressBytes = memorySummary.totalBytes,
        .reportedUsableMemoryBytes = memorySummary.usableBytes,
        .managedUsableMemoryBytes = managedUsableMemoryBytes,
        .managedPhysicalAddressLimit = currentManagedPhysicalAddressLimit,
        .physicalAddressWidthBits = physicalAddressWidthBits,
        .virtualAddressWidthBits = virtualAddressWidthBits,
        .fiveLevelPagingSupported = fiveLevelPagingSupported,
        .frameStateStoragePhysicalAddress = frameStateStorageRange.beginAddress,
        .frameStateStorageSizeBytes = frameStateStorageRange.lengthBytes,
        .directMapMappedBytes = directMapStatistics.mappedBytes,
        .directMapLargePageCount = directMapStatistics.largePageCount,
        .directMapSmallPageCount = directMapStatistics.smallPageCount,
        .highMemoryTestPhysicalAddress = highMemoryTestPhysicalAddress,
        .freeFrameCount = frameStatistics.freeFrameCount,
        .allocatedFrameCount = frameStatistics.allocatedFrameCount,
        .reservedFrameCount = frameStatistics.reservedFrameCount,
        .pageTableRootPhysicalAddress = GetPageTableManager().RootPhysicalAddress(),
        .heapCapacityBytes = OS_KERNEL_MEMORY_HEAP_SIZE_BYTES,
    };
    return KernelMemoryInitializationStatus::Succeeded;
}

const KernelMemoryStatistics &GetKernelMemoryStatistics() noexcept {
    return currentKernelMemoryStatistics;
}

PhysicalFrameAllocatorStatistics GetPhysicalFrameAllocatorStatistics() noexcept {
    return FrameAllocator().Statistics();
}

uint64_t PhysicalMemoryDirectMapAddress(const uint64_t physicalAddress) noexcept {
    if (!directMapActive || physicalAddress >= currentManagedPhysicalAddressLimit ||
        physicalAddress > UINT64_MAX - OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE) {
        return 0ULL;
    }
    return OS_KERNEL_MEMORY_DIRECT_MAP_VIRTUAL_BASE + physicalAddress;
}

KernelHeap &GetKernelHeap() noexcept {
    static KernelHeap heap{};
    return heap;
}

KernelUserPageStatus CreateUserPageTable(uint64_t &rootPhysicalAddress) noexcept {
    PageTableManager processPageTable{FrameAllocator(), ActivePageTableMemoryAccess()};
    if (processPageTable.InitializeProcessRoot(GetPageTableManager().RootPhysicalAddress()) !=
        PageTableStatus::Succeeded) {
        return KernelUserPageStatus::PageTableCreationFailed;
    }
    rootPhysicalAddress = processPageTable.RootPhysicalAddress();
    return KernelUserPageStatus::Succeeded;
}

KernelUserPageStatus DestroyUserPageTable(const uint64_t rootPhysicalAddress) noexcept {
    if (rootPhysicalAddress == 0ULL || (rootPhysicalAddress & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL ||
        rootPhysicalAddress == GetPageTableManager().RootPhysicalAddress() ||
        rootPhysicalAddress == ReadPageTableRoot()) {
        return KernelUserPageStatus::InvalidPageTableRoot;
    }
    PageTableManager processPageTable{FrameAllocator(), rootPhysicalAddress,
                                      ActivePageTableMemoryAccess()};
    return processPageTable.ReleaseProcessRoot() == PageTableStatus::Succeeded
               ? KernelUserPageStatus::Succeeded
               : KernelUserPageStatus::PageTableDestructionFailed;
}

KernelUserPageStatus AllocateAndMapUserPage(const uint64_t rootPhysicalAddress,
                                            const uint64_t virtualAddress, const bool writable,
                                            const bool executable,
                                            uint64_t &physicalAddress) noexcept {
    if (rootPhysicalAddress == 0ULL || (rootPhysicalAddress & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL) {
        return KernelUserPageStatus::InvalidPageTableRoot;
    }
    if ((virtualAddress & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL ||
        !IsUserVirtualAddressRange(virtualAddress, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return KernelUserPageStatus::InvalidVirtualAddress;
    }
    if (writable && executable) {
        return KernelUserPageStatus::InvalidPermissions;
    }
    PhysicalFrame frame{};
    if (FrameAllocator().Allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelUserPageStatus::FrameAllocationFailed;
    }
    const PagePermissions permissions{
        .writable = writable,
        .executable = executable,
        .userAccessible = true,
        .cacheDisabled = false,
    };
    PageTableManager processPageTable{FrameAllocator(), rootPhysicalAddress,
                                      ActivePageTableMemoryAccess()};
    if (processPageTable.MapPage(virtualAddress, frame.physicalAddress, permissions) !=
        PageTableStatus::Succeeded) {
        static_cast<void>(FrameAllocator().Release(frame));
        return KernelUserPageStatus::PageMappingFailed;
    }
    physicalAddress = frame.physicalAddress;
    return KernelUserPageStatus::Succeeded;
}

KernelUserPageStatus ReleaseUserPage(const uint64_t rootPhysicalAddress,
                                     const uint64_t virtualAddress) noexcept {
    if (rootPhysicalAddress == 0ULL || (rootPhysicalAddress & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL) {
        return KernelUserPageStatus::InvalidPageTableRoot;
    }
    if ((virtualAddress & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL ||
        !IsUserVirtualAddressRange(virtualAddress, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES)) {
        return KernelUserPageStatus::InvalidVirtualAddress;
    }
    PageTableManager processPageTable{FrameAllocator(), rootPhysicalAddress,
                                      ActivePageTableMemoryAccess()};
    PageMapping mapping{};
    if (processPageTable.QueryPage(virtualAddress, mapping) != PageTableStatus::Succeeded) {
        return KernelUserPageStatus::PageNotMapped;
    }
    if (!mapping.permissions.userAccessible) {
        return KernelUserPageStatus::NotUserAccessible;
    }
    if (processPageTable.UnmapPage(virtualAddress) != PageTableStatus::Succeeded) {
        return KernelUserPageStatus::PageUnmappingFailed;
    }
    if (FrameAllocator().Release(PhysicalFrame{.physicalAddress = mapping.physicalAddress}) !=
        PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelUserPageStatus::FrameReleaseFailed;
    }
    return KernelUserPageStatus::Succeeded;
}

PageTableStatus QueryAddressSpacePage(const uint64_t rootPhysicalAddress,
                                      const uint64_t virtualAddress,
                                      PageMapping &mapping) noexcept {
    if (rootPhysicalAddress == 0ULL || (rootPhysicalAddress & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL) {
        return PageTableStatus::NotInitialized;
    }
    PageTableManager pageTable{FrameAllocator(), rootPhysicalAddress,
                               ActivePageTableMemoryAccess()};
    return pageTable.QueryPage(virtualAddress, mapping);
}

PageTableStatus QueryActivePage(const uint64_t virtualAddress, PageMapping &mapping) noexcept {
    return QueryAddressSpacePage(ReadPageTableRoot(), virtualAddress, mapping);
}

uint64_t GetKernelPageTableRoot() noexcept { return GetPageTableManager().RootPhysicalAddress(); }

bool ActivateUserPageTable(const uint64_t rootPhysicalAddress) noexcept {
    if (rootPhysicalAddress == 0ULL || (rootPhysicalAddress & OS_KERNEL_MEMORY_PAGE_MASK) != 0ULL ||
        rootPhysicalAddress == GetPageTableManager().RootPhysicalAddress()) {
        return false;
    }
    ActivatePageTable(rootPhysicalAddress);
    return ReadPageTableRoot() == rootPhysicalAddress;
}

void ActivateKernelPageTable() noexcept {
    ActivatePageTable(GetPageTableManager().RootPhysicalAddress());
}

}
