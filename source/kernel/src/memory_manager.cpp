#include "os/kernel/memory_manager.hpp"

#include "os/kernel/descriptor_tables.hpp"
#include "os/kernel/page_table.hpp"
#include "os/kernel/physical_frame_allocator.hpp"
#include "os/kernel/physical_memory_map.hpp"
#include "os/kernel/processor.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_MEMORY_MANAGED_LIMIT_BYTES = 64ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_MEMORY_MANAGED_FRAME_COUNT =
    OS_KERNEL_MEMORY_MANAGED_LIMIT_BYTES / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_FRAME_STATES_PER_BYTE = 4ULL;
constexpr uint64_t OS_KERNEL_MEMORY_FRAME_STATE_STORAGE_SIZE_BYTES =
    OS_KERNEL_MEMORY_MANAGED_FRAME_COUNT / OS_KERNEL_MEMORY_FRAME_STATES_PER_BYTE;
constexpr uint64_t OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES = 1ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_MEMORY_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_PAGE_COUNT =
    OS_KERNEL_MEMORY_HEAP_SIZE_BYTES / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_SIZE_BYTES =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_ALIGNMENT_BYTES =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_MEMORY_FRAME_STATE_STORAGE_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_FIRST_PATTERN = 0x13579BDF2468ACE0ULL;
constexpr uint64_t OS_KERNEL_MEMORY_HEAP_SELF_TEST_SECOND_PATTERN = 0xC001D00DC0FFEE11ULL;

alignas(OS_KERNEL_MEMORY_FRAME_STATE_STORAGE_ALIGNMENT_BYTES) uint8_t
    kernelFrameStateStorage[OS_KERNEL_MEMORY_FRAME_STATE_STORAGE_SIZE_BYTES];
KernelMemoryStatistics currentKernelMemoryStatistics{};

extern "C" uint8_t osKernelImageStart[];
extern "C" uint8_t osKernelImageEnd[];
extern "C" uint8_t osKernelTextStart[];
extern "C" uint8_t osKernelTextEnd[];
extern "C" uint8_t osKernelReadOnlyDataStart[];
extern "C" uint8_t osKernelReadOnlyDataEnd[];
extern "C" uint8_t osKernelWritableDataStart[];
extern "C" uint8_t osKernelWritableDataEnd[];

[[nodiscard]] uint64_t addressOf(uint8_t *symbol) noexcept {
    return reinterpret_cast<uint64_t>(symbol);
}

[[nodiscard]] uint64_t alignUpToPage(const uint64_t address) noexcept {
    return (address + OS_KERNEL_MEMORY_PAGE_MASK) & ~OS_KERNEL_MEMORY_PAGE_MASK;
}

[[nodiscard]] PhysicalFrameAllocator &frameAllocator() noexcept {
    static PhysicalFrameAllocator allocator{
        kernelFrameStateStorage,
        OS_KERNEL_MEMORY_FRAME_STATE_STORAGE_SIZE_BYTES,
    };
    return allocator;
}

[[nodiscard]] PageTableManager &pageTableManager() noexcept {
    static PageTableManager manager{frameAllocator()};
    return manager;
}

[[nodiscard]] bool isGuardPage(const uint64_t pageAddress, const BootInfo &bootInfo) noexcept {
    const uint64_t earlyStackGuardAddress =
        bootInfo.kernelStackTopPhysicalAddress - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    if (pageAddress == earlyStackGuardAddress) {
        return true;
    }
    for (uint64_t guardPageIndex = 0ULL;
         guardPageIndex < OS_KERNEL_DESCRIPTOR_INTERRUPT_STACK_GUARD_PAGE_COUNT; ++guardPageIndex) {
        if (pageAddress == interruptStackGuardPageAddress(guardPageIndex)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] PagePermissions identityPermissions(const uint64_t pageAddress) noexcept {
    const uint64_t textBegin = addressOf(osKernelTextStart);
    const uint64_t textEnd = alignUpToPage(addressOf(osKernelTextEnd));
    if (pageAddress >= textBegin && pageAddress < textEnd) {
        return PagePermissions{
            .writable = false,
            .executable = true,
            .userAccessible = false,
        };
    }
    const uint64_t readOnlyBegin = addressOf(osKernelReadOnlyDataStart);
    const uint64_t readOnlyEnd = alignUpToPage(addressOf(osKernelReadOnlyDataEnd));
    if (pageAddress >= readOnlyBegin && pageAddress < readOnlyEnd) {
        return PagePermissions{
            .writable = false,
            .executable = false,
            .userAccessible = false,
        };
    }
    return PagePermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
    };
}

[[nodiscard]] bool reserveBootCriticalRanges(const BootInfo &bootInfo) noexcept {
    PhysicalFrameAllocator &allocator = frameAllocator();
    const uint64_t kernelBegin = addressOf(osKernelImageStart);
    const uint64_t kernelEnd = addressOf(osKernelImageEnd);
    const uint64_t earlyStackBegin =
        bootInfo.kernelStackTopPhysicalAddress - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    return allocator.reserveRange(0ULL, OS_KERNEL_MEMORY_LOW_PLATFORM_RESERVATION_SIZE_BYTES) ==
               PhysicalFrameAllocatorStatus::Succeeded &&
           allocator.reserveRange(kernelBegin, kernelEnd - kernelBegin) ==
               PhysicalFrameAllocatorStatus::Succeeded &&
           allocator.reserveRange(earlyStackBegin, OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES) ==
               PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool mapIdentityRange(const BootInfo &bootInfo) noexcept {
    PageTableManager &manager = pageTableManager();
    for (uint64_t pageAddress = 0ULL; pageAddress < bootInfo.identityMappedSizeBytes;
         pageAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        if (pageAddress == 0ULL || isGuardPage(pageAddress, bootInfo)) {
            continue;
        }
        if (manager.mapPage(pageAddress, pageAddress, identityPermissions(pageAddress)) !=
            PageTableStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool mapKernelHeap() noexcept {
    PhysicalFrameAllocator &allocator = frameAllocator();
    PageTableManager &manager = pageTableManager();
    const PagePermissions heapPermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
    };
    for (uint64_t pageIndex = 0ULL; pageIndex < OS_KERNEL_MEMORY_HEAP_PAGE_COUNT; ++pageIndex) {
        PhysicalFrame frame{};
        if (allocator.allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
            return false;
        }
        const uint64_t virtualAddress =
            OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE + pageIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (manager.mapPage(virtualAddress, frame.physicalAddress, heapPermissions) !=
            PageTableStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool mapWriteProtectionTestPage() noexcept {
    PhysicalFrame frame{};
    if (frameAllocator().allocate(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    const PagePermissions readOnlyPermissions{
        .writable = false,
        .executable = false,
        .userAccessible = false,
    };
    PageTableManager &manager = pageTableManager();
    if (manager.mapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                        frame.physicalAddress, readOnlyPermissions) != PageTableStatus::Succeeded ||
        manager.mapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                        frame.physicalAddress,
                        readOnlyPermissions) != PageTableStatus::AlreadyMapped ||
        manager.unmapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS) !=
            PageTableStatus::Succeeded) {
        return false;
    }
    PageMapping removedMapping{};
    if (manager.queryPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS, removedMapping) !=
        PageTableStatus::NotMapped) {
        return false;
    }
    return manager.mapPage(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                           frame.physicalAddress,
                           readOnlyPermissions) == PageTableStatus::Succeeded;
}

[[nodiscard]] bool validateMapping(const uint64_t virtualAddress,
                                   const uint64_t expectedPhysicalAddress,
                                   const PagePermissions expectedPermissions) noexcept {
    PageMapping mapping{};
    if (pageTableManager().queryPage(virtualAddress, mapping) != PageTableStatus::Succeeded) {
        return false;
    }
    return mapping.physicalAddress == expectedPhysicalAddress &&
           mapping.permissions.writable == expectedPermissions.writable &&
           mapping.permissions.executable == expectedPermissions.executable &&
           mapping.permissions.userAccessible == expectedPermissions.userAccessible;
}

[[nodiscard]] bool validatePermissions(const uint64_t virtualAddress,
                                       const PagePermissions expectedPermissions) noexcept {
    PageMapping mapping{};
    if (pageTableManager().queryPage(virtualAddress, mapping) != PageTableStatus::Succeeded) {
        return false;
    }
    return mapping.physicalAddress != 0ULL &&
           mapping.permissions.writable == expectedPermissions.writable &&
           mapping.permissions.executable == expectedPermissions.executable &&
           mapping.permissions.userAccessible == expectedPermissions.userAccessible;
}

[[nodiscard]] bool validateKernelMappings(const BootInfo &bootInfo) noexcept {
    const PagePermissions textPermissions{
        .writable = false,
        .executable = true,
        .userAccessible = false,
    };
    const PagePermissions readOnlyPermissions{
        .writable = false,
        .executable = false,
        .userAccessible = false,
    };
    const PagePermissions writablePermissions{
        .writable = true,
        .executable = false,
        .userAccessible = false,
    };
    PageMapping ignoredMapping{};
    if (!validateMapping(addressOf(osKernelTextStart), addressOf(osKernelTextStart),
                         textPermissions) ||
        !validateMapping(addressOf(osKernelReadOnlyDataStart), addressOf(osKernelReadOnlyDataStart),
                         readOnlyPermissions) ||
        !validateMapping(addressOf(osKernelWritableDataStart), addressOf(osKernelWritableDataStart),
                         writablePermissions) ||
        !validatePermissions(OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE, writablePermissions) ||
        !validatePermissions(OS_KERNEL_MEMORY_WRITE_PROTECTION_TEST_VIRTUAL_ADDRESS,
                             readOnlyPermissions)) {
        return false;
    }
    if (pageTableManager().queryPage(0ULL, ignoredMapping) != PageTableStatus::NotMapped ||
        pageTableManager().queryPage(OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS, ignoredMapping) !=
            PageTableStatus::NotMapped) {
        return false;
    }
    const uint64_t earlyStackGuardAddress =
        bootInfo.kernelStackTopPhysicalAddress - OS_KERNEL_MEMORY_EARLY_STACK_SIZE_BYTES;
    if (pageTableManager().queryPage(earlyStackGuardAddress, ignoredMapping) !=
        PageTableStatus::NotMapped) {
        return false;
    }
    for (uint64_t guardPageIndex = 0ULL;
         guardPageIndex < OS_KERNEL_DESCRIPTOR_INTERRUPT_STACK_GUARD_PAGE_COUNT; ++guardPageIndex) {
        if (pageTableManager().queryPage(interruptStackGuardPageAddress(guardPageIndex),
                                         ignoredMapping) != PageTableStatus::NotMapped) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool runHeapSelfTest() noexcept {
    void *smallAllocation = nullptr;
    if (kernelHeap().tryAllocate(OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_SIZE_BYTES,
                                 OS_KERNEL_MEMORY_HEAP_SELF_TEST_SMALL_ALIGNMENT_BYTES,
                                 smallAllocation) != KernelHeapStatus::Succeeded) {
        return false;
    }
    void *pageAllocation = nullptr;
    if (kernelHeap().tryAllocate(OS_KERNEL_MEMORY_HEAP_SELF_TEST_PAGE_SIZE_BYTES,
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

}

KernelMemoryInitializationStatus initializeKernelMemory(const BootInfo &bootInfo) noexcept {
    const auto *memoryMap =
        reinterpret_cast<const PhysicalMemoryMapEntry *>(bootInfo.physicalMemoryMapAddress);
    PhysicalMemorySummary memorySummary{};
    if (validateAndSummarizePhysicalMemoryMap(memoryMap, bootInfo.physicalMemoryMapEntryCount,
                                              bootInfo.identityMappedSizeBytes, memorySummary) !=
        PhysicalMemoryMapValidationStatus::Succeeded) {
        return KernelMemoryInitializationStatus::InvalidMemoryMap;
    }
    if (frameAllocator().initialize(memoryMap, bootInfo.physicalMemoryMapEntryCount,
                                    bootInfo.identityMappedSizeBytes) !=
        PhysicalFrameAllocatorStatus::Succeeded) {
        return KernelMemoryInitializationStatus::FrameAllocatorInitializationFailed;
    }
    if (!reserveBootCriticalRanges(bootInfo)) {
        return KernelMemoryInitializationStatus::ReservationFailed;
    }
    if (pageTableManager().initialize() != PageTableStatus::Succeeded) {
        return KernelMemoryInitializationStatus::PageTableInitializationFailed;
    }
    if (!mapIdentityRange(bootInfo)) {
        return KernelMemoryInitializationStatus::IdentityMappingFailed;
    }
    if (!mapKernelHeap()) {
        return KernelMemoryInitializationStatus::HeapMappingFailed;
    }
    if (!mapWriteProtectionTestPage()) {
        return KernelMemoryInitializationStatus::ProtectionTestMappingFailed;
    }
    if (!enableKernelMemoryProtection()) {
        return KernelMemoryInitializationStatus::MemoryProtectionUnsupported;
    }
    activatePageTable(pageTableManager().rootPhysicalAddress());
    if (readPageTableRoot() != pageTableManager().rootPhysicalAddress() ||
        !kernelMemoryProtectionEnabled()) {
        return KernelMemoryInitializationStatus::PageTableActivationFailed;
    }
    if (!validateKernelMappings(bootInfo)) {
        return KernelMemoryInitializationStatus::PermissionValidationFailed;
    }
    if (kernelHeap().initialize(OS_KERNEL_MEMORY_HEAP_VIRTUAL_BASE,
                                OS_KERNEL_MEMORY_HEAP_SIZE_BYTES) != KernelHeapStatus::Succeeded) {
        return KernelMemoryInitializationStatus::HeapInitializationFailed;
    }
    if (!runHeapSelfTest()) {
        return KernelMemoryInitializationStatus::HeapSelfTestFailed;
    }

    const PhysicalFrameAllocatorStatistics frameStatistics = frameAllocator().statistics();
    currentKernelMemoryStatistics = KernelMemoryStatistics{
        .memoryMapEntryCount = bootInfo.physicalMemoryMapEntryCount,
        .describedAddressBytes = memorySummary.totalBytes,
        .reportedUsableMemoryBytes = memorySummary.usableBytes,
        .managedUsableMemoryBytes = memorySummary.managedUsableBytes,
        .freeFrameCount = frameStatistics.freeFrameCount,
        .allocatedFrameCount = frameStatistics.allocatedFrameCount,
        .reservedFrameCount = frameStatistics.reservedFrameCount,
        .pageTableRootPhysicalAddress = pageTableManager().rootPhysicalAddress(),
        .heapCapacityBytes = OS_KERNEL_MEMORY_HEAP_SIZE_BYTES,
    };
    return KernelMemoryInitializationStatus::Succeeded;
}

const KernelMemoryStatistics &kernelMemoryStatistics() noexcept {
    return currentKernelMemoryStatistics;
}

KernelHeap &kernelHeap() noexcept {
    static KernelHeap heap{};
    return heap;
}

}
