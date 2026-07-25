#include "os/kernel/user_memory.hpp"

#include "os/kernel/memory_manager.hpp"
#include "os/kernel/physical_frame_allocator.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_MEMORY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_MASK =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
constexpr uint8_t OS_KERNEL_USER_MEMORY_ZERO_BYTE = 0U;

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

void ZeroPhysicalPage(const uint64_t physicalAddress) noexcept {
    uint8_t *const page = reinterpret_cast<uint8_t *>(physicalAddress);
    for (uint64_t byteIndex = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         byteIndex < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byteIndex) {
        page[byteIndex] = OS_KERNEL_USER_MEMORY_ZERO_BYTE;
    }
}

void CopyBytes(uint8_t *destination, const uint8_t *source, const uint64_t lengthBytes) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; byteIndex < lengthBytes;
         ++byteIndex) {
        destination[byteIndex] = source[byteIndex];
    }
}

[[nodiscard]] UserMemoryCopyStatus ValidateUserMemory(const uint64_t userAddress,
                                                      const uint64_t lengthBytes,
                                                      const bool requireWritable) noexcept {
    if (!IsUserVirtualAddressRange(userAddress, lengthBytes)) {
        return UserMemoryCopyStatus::InvalidUserRange;
    }

    const uint64_t inclusiveEndAddress =
        userAddress + lengthBytes - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
    uint64_t pageAddress = userAddress & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
    const uint64_t finalPageAddress = inclusiveEndAddress & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
    while (true) {
        PageMapping mapping{};
        if (QueryActivePage(pageAddress, mapping) != PageTableStatus::Succeeded) {
            return UserMemoryCopyStatus::PageNotMapped;
        }
        if (!mapping.permissions.userAccessible) {
            return UserMemoryCopyStatus::PageNotUserAccessible;
        }
        if (requireWritable && !mapping.permissions.writable) {
            return UserMemoryCopyStatus::PageNotWritable;
        }
        if (pageAddress == finalPageAddress) {
            break;
        }
        pageAddress += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }
    return UserMemoryCopyStatus::Succeeded;
}

[[nodiscard]] UserAddressSpaceStatus MapElfSegment(const uint8_t *image,
                                                   const UserElfLoadSegment &segment,
                                                   const uint64_t rootPhysicalAddress,
                                                   uint64_t &mappedPageCount) noexcept {
    const uint64_t segmentPageCount = (segment.memorySizeBytes + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES -
                                       OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) /
                                      OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    uint64_t remainingFileSizeBytes = segment.fileSizeBytes;
    for (uint64_t pageIndex = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; pageIndex < segmentPageCount;
         ++pageIndex) {
        const uint64_t pageVirtualAddress =
            segment.virtualAddress + pageIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        uint64_t pagePhysicalAddress = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        const KernelUserPageStatus pageStatus =
            AllocateAndMapUserPage(rootPhysicalAddress, pageVirtualAddress, segment.writable,
                                   segment.executable, pagePhysicalAddress);
        if (pageStatus == KernelUserPageStatus::FrameAllocationFailed) {
            return UserAddressSpaceStatus::PageAllocationFailed;
        }
        if (pageStatus != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
        ++mappedPageCount;

        ZeroPhysicalPage(pagePhysicalAddress);
        const uint64_t pageFileSizeBytes =
            Minimum(remainingFileSizeBytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
        if (pageFileSizeBytes > OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
            CopyBytes(reinterpret_cast<uint8_t *>(pagePhysicalAddress),
                      image + segment.fileOffset + pageIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                      pageFileSizeBytes);
            remainingFileSizeBytes -= pageFileSizeBytes;
        }
    }
    return UserAddressSpaceStatus::Succeeded;
}

[[nodiscard]] UserAddressSpaceStatus MapUserStack(const uint64_t rootPhysicalAddress,
                                                  uint64_t &mappedPageCount) noexcept {
    for (uint64_t pageIndex = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         pageIndex < OS_KERNEL_USER_STACK_PAGE_COUNT; ++pageIndex) {
        const uint64_t pageVirtualAddress = OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS +
                                            pageIndex * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        uint64_t pagePhysicalAddress = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        const KernelUserPageStatus pageStatus = AllocateAndMapUserPage(
            rootPhysicalAddress, pageVirtualAddress, true, false, pagePhysicalAddress);
        if (pageStatus == KernelUserPageStatus::FrameAllocationFailed) {
            return UserAddressSpaceStatus::PageAllocationFailed;
        }
        if (pageStatus != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
        ++mappedPageCount;
        ZeroPhysicalPage(pagePhysicalAddress);
    }
    return UserAddressSpaceStatus::Succeeded;
}

[[nodiscard]] bool ElfOverlapsStackReservation(const UserElfLayout &layout) noexcept {
    for (uint64_t segmentIndex = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segmentIndex < layout.loadSegmentCount; ++segmentIndex) {
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

    uint64_t rootPhysicalAddress = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (CreateUserPageTable(rootPhysicalAddress) != KernelUserPageStatus::Succeeded) {
        return UserAddressSpaceStatus::PageTableCreationFailed;
    }
    uint64_t mappedPageCount = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    for (uint64_t segmentIndex = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segmentIndex < layout.loadSegmentCount; ++segmentIndex) {
        const UserAddressSpaceStatus segmentStatus = MapElfSegment(
            image, layout.loadSegments[segmentIndex], rootPhysicalAddress, mappedPageCount);
        if (segmentStatus != UserAddressSpaceStatus::Succeeded) {
            if (DestroyUserPageTable(rootPhysicalAddress) != KernelUserPageStatus::Succeeded) {
                return UserAddressSpaceStatus::RollbackFailed;
            }
            return segmentStatus;
        }
    }
    const UserAddressSpaceStatus stackStatus = MapUserStack(rootPhysicalAddress, mappedPageCount);
    if (stackStatus != UserAddressSpaceStatus::Succeeded) {
        if (DestroyUserPageTable(rootPhysicalAddress) != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::RollbackFailed;
        }
        return stackStatus;
    }

    addressSpace = UserAddressSpace{
        .rootPhysicalAddress = rootPhysicalAddress,
        .entryVirtualAddress = layout.entryVirtualAddress,
        .stackTopVirtualAddress = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS,
        .mappedPageCount = mappedPageCount,
    };
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus DestroyUserAddressSpace(UserAddressSpace &addressSpace) noexcept {
    if (DestroyUserPageTable(addressSpace.rootPhysicalAddress) != KernelUserPageStatus::Succeeded) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    addressSpace = UserAddressSpace{};
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
    const UserMemoryCopyStatus validationStatus =
        ValidateUserMemory(userAddress, lengthBytes, false);
    if (validationStatus != UserMemoryCopyStatus::Succeeded) {
        return validationStatus;
    }
    CopyBytes(destination, reinterpret_cast<const uint8_t *>(userAddress), lengthBytes);
    return UserMemoryCopyStatus::Succeeded;
}

UserMemoryCopyStatus ValidateUserWritableMemory(const uint64_t userAddress,
                                                const uint64_t lengthBytes) noexcept {
    return ValidateUserMemory(userAddress, lengthBytes, true);
}

UserMemoryCopyStatus CopyToUser(const uint64_t userAddress, const uint64_t lengthBytes,
                                const uint8_t *source, const uint64_t sourceSizeBytes) noexcept {
    if (source == nullptr) {
        return UserMemoryCopyStatus::NullSource;
    }
    if (lengthBytes > sourceSizeBytes) {
        return UserMemoryCopyStatus::SourceTooSmall;
    }
    const UserMemoryCopyStatus validationStatus =
        ValidateUserWritableMemory(userAddress, lengthBytes);
    if (validationStatus != UserMemoryCopyStatus::Succeeded) {
        return validationStatus;
    }
    CopyBytes(reinterpret_cast<uint8_t *>(userAddress), source, lengthBytes);
    return UserMemoryCopyStatus::Succeeded;
}

}
