#include "os/kernel/user_memory.hpp"

#include "os/kernel/memory_manager.hpp"
#include "os/kernel/physical_frame_allocator.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_TOTAL_MAPPED_PAGE_LIMIT =
    OS_KERNEL_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT + OS_KERNEL_USER_STACK_PAGE_COUNT;
constexpr uint8_t OS_KERNEL_USER_MEMORY_ZERO_BYTE = 0U;

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

void ZeroPhysicalPage(const uint64_t physicalAddress) noexcept {
    uint8_t *const page = reinterpret_cast<uint8_t *>(physicalAddress);
    for (uint64_t byteIndex = 0ULL; byteIndex < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byteIndex) {
        page[byteIndex] = OS_KERNEL_USER_MEMORY_ZERO_BYTE;
    }
}

void CopyBytes(uint8_t *destination, const uint8_t *source, const uint64_t lengthBytes) noexcept {
    for (uint64_t byteIndex = 0ULL; byteIndex < lengthBytes; ++byteIndex) {
        destination[byteIndex] = source[byteIndex];
    }
}

[[nodiscard]] bool RollbackMappedPages(const uint64_t *mappedVirtualAddresses,
                                       const uint64_t mappedPageCount) noexcept {
    uint64_t remainingPageCount = mappedPageCount;
    while (remainingPageCount > 0ULL) {
        --remainingPageCount;
        if (ReleaseUserPage(mappedVirtualAddresses[remainingPageCount]) !=
            KernelUserPageStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] UserAddressSpaceStatus MapElfSegment(const uint8_t *image,
                                                   const UserElfLoadSegment &segment,
                                                   uint64_t *mappedVirtualAddresses,
                                                   uint64_t &mappedPageCount) noexcept {
    const uint64_t segmentPageCount =
        (segment.memorySizeBytes + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL) /
        OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    uint64_t remainingFileSizeBytes = segment.fileSizeBytes;
    for (uint64_t pageIndex = 0ULL; pageIndex < segmentPageCount; ++pageIndex) {
        const uint64_t pageVirtualAddress =
            segment.virtualAddress + pageIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        uint64_t pagePhysicalAddress = 0ULL;
        const KernelUserPageStatus pageStatus = AllocateAndMapUserPage(
            pageVirtualAddress, segment.writable, segment.executable, pagePhysicalAddress);
        if (pageStatus == KernelUserPageStatus::FrameAllocationFailed) {
            return UserAddressSpaceStatus::PageAllocationFailed;
        }
        if (pageStatus != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
        mappedVirtualAddresses[mappedPageCount] = pageVirtualAddress;
        ++mappedPageCount;

        ZeroPhysicalPage(pagePhysicalAddress);
        const uint64_t pageFileSizeBytes =
            Minimum(remainingFileSizeBytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
        if (pageFileSizeBytes > 0ULL) {
            CopyBytes(reinterpret_cast<uint8_t *>(pagePhysicalAddress),
                      image + segment.fileOffset + pageIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                      pageFileSizeBytes);
            remainingFileSizeBytes -= pageFileSizeBytes;
        }
    }
    return UserAddressSpaceStatus::Succeeded;
}

[[nodiscard]] UserAddressSpaceStatus MapUserStack(uint64_t *mappedVirtualAddresses,
                                                  uint64_t &mappedPageCount) noexcept {
    for (uint64_t pageIndex = 0ULL; pageIndex < OS_KERNEL_USER_STACK_PAGE_COUNT; ++pageIndex) {
        const uint64_t pageVirtualAddress = OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS +
                                            pageIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        uint64_t pagePhysicalAddress = 0ULL;
        const KernelUserPageStatus pageStatus =
            AllocateAndMapUserPage(pageVirtualAddress, true, false, pagePhysicalAddress);
        if (pageStatus == KernelUserPageStatus::FrameAllocationFailed) {
            return UserAddressSpaceStatus::PageAllocationFailed;
        }
        if (pageStatus != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
        mappedVirtualAddresses[mappedPageCount] = pageVirtualAddress;
        ++mappedPageCount;
        ZeroPhysicalPage(pagePhysicalAddress);
    }
    return UserAddressSpaceStatus::Succeeded;
}

[[nodiscard]] bool ElfOverlapsStackReservation(const UserElfLayout &layout) noexcept {
    for (uint64_t segmentIndex = 0ULL; segmentIndex < layout.loadSegmentCount; ++segmentIndex) {
        const UserElfLoadSegment &segment = layout.loadSegments[segmentIndex];
        const uint64_t segmentEndAddress = segment.virtualAddress + segment.memorySizeBytes;
        if (segment.virtualAddress < OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS &&
            OS_KERNEL_USER_STACK_GUARD_VIRTUAL_ADDRESS < segmentEndAddress) {
            return true;
        }
    }
    return false;
}

}

UserAddressSpaceStatus LoadUserAddressSpace(const uint8_t *image, const uint64_t imageSizeBytes,
                                            UserAddressSpace &addressSpace,
                                            UserElfValidationStatus &elfValidationStatus) noexcept {
    UserElfLayout layout{};
    elfValidationStatus = ValidateUserElf(image, imageSizeBytes, layout);
    if (elfValidationStatus != UserElfValidationStatus::Succeeded) {
        return UserAddressSpaceStatus::InvalidElf;
    }
    if (ElfOverlapsStackReservation(layout)) {
        return UserAddressSpaceStatus::StackCollision;
    }

    uint64_t mappedVirtualAddresses[OS_KERNEL_USER_MEMORY_TOTAL_MAPPED_PAGE_LIMIT]{};
    uint64_t mappedPageCount = 0ULL;
    for (uint64_t segmentIndex = 0ULL; segmentIndex < layout.loadSegmentCount; ++segmentIndex) {
        const UserAddressSpaceStatus segmentStatus = MapElfSegment(
            image, layout.loadSegments[segmentIndex], mappedVirtualAddresses, mappedPageCount);
        if (segmentStatus != UserAddressSpaceStatus::Succeeded) {
            if (!RollbackMappedPages(mappedVirtualAddresses, mappedPageCount)) {
                return UserAddressSpaceStatus::RollbackFailed;
            }
            return segmentStatus;
        }
    }
    const UserAddressSpaceStatus stackStatus =
        MapUserStack(mappedVirtualAddresses, mappedPageCount);
    if (stackStatus != UserAddressSpaceStatus::Succeeded) {
        if (!RollbackMappedPages(mappedVirtualAddresses, mappedPageCount)) {
            return UserAddressSpaceStatus::RollbackFailed;
        }
        return stackStatus;
    }

    addressSpace = UserAddressSpace{
        .entryVirtualAddress = layout.entryVirtualAddress,
        .stackTopVirtualAddress = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS,
        .mappedPageCount = mappedPageCount,
    };
    return UserAddressSpaceStatus::Succeeded;
}

UserMemoryCopyStatus CopyFromUser(const uint64_t userAddress, const uint64_t lengthBytes,
                                  uint8_t *destination,
                                  const uint64_t destinationCapacityBytes) noexcept {
    if (destination == nullptr) {
        return UserMemoryCopyStatus::NullDestination;
    }
    if (lengthBytes > destinationCapacityBytes) {
        return UserMemoryCopyStatus::DestinationTooSmall;
    }
    if (!IsUserVirtualAddressRange(userAddress, lengthBytes)) {
        return UserMemoryCopyStatus::InvalidUserRange;
    }

    const uint64_t inclusiveEndAddress = userAddress + lengthBytes - 1ULL;
    uint64_t pageAddress = userAddress & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
    const uint64_t finalPageAddress = inclusiveEndAddress & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
    while (true) {
        PageMapping mapping{};
        if (QueryKernelPage(pageAddress, mapping) != PageTableStatus::Succeeded) {
            return UserMemoryCopyStatus::PageNotMapped;
        }
        if (!mapping.permissions.userAccessible) {
            return UserMemoryCopyStatus::PageNotUserAccessible;
        }
        if (pageAddress == finalPageAddress) {
            break;
        }
        pageAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }

    CopyBytes(destination, reinterpret_cast<const uint8_t *>(userAddress), lengthBytes);
    return UserMemoryCopyStatus::Succeeded;
}

}
