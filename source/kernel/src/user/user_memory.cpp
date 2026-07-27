#include "os/kernel/user/user_memory.hpp"

#include "os/kernel/memory/memory_manager.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_MEMORY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_MASK =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
constexpr uint8_t OS_KERNEL_USER_MEMORY_ZERO_BYTE = 0U;

struct MemoryImageReaderContext final {
    const uint8_t *image;
    uint64_t image_size_bytes;
};

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] uint8_t *PhysicalPagePointer(const uint64_t physical_address) noexcept {
    const uint64_t direct_map_address = PhysicalMemoryDirectMapAddress(physical_address);
    return direct_map_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE
               ? nullptr
               : reinterpret_cast<uint8_t *>(direct_map_address);
}

[[nodiscard]] bool ZeroPhysicalPage(const uint64_t physical_address) noexcept {
    uint8_t *const page = PhysicalPagePointer(physical_address);
    if (page == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         byte_index < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byte_index) {
        page[byte_index] = OS_KERNEL_USER_MEMORY_ZERO_BYTE;
    }
    return true;
}

void CopyBytes(uint8_t *destination, const uint8_t *source, const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool ReadMemoryImage(void *const context, const uint64_t offset_bytes,
                                   uint8_t *const destination,
                                   const uint64_t length_bytes) noexcept {
    if (context == nullptr || destination == nullptr) {
        return false;
    }
    const MemoryImageReaderContext &reader_context =
        *static_cast<const MemoryImageReaderContext *>(context);
    if (offset_bytes > reader_context.image_size_bytes ||
        length_bytes > reader_context.image_size_bytes - offset_bytes) {
        return false;
    }
    CopyBytes(destination, reader_context.image + offset_bytes, length_bytes);
    return true;
}

[[nodiscard]] UserMemoryCopyStatus ValidateUserMemory(const uint64_t user_address,
                                                      const uint64_t length_bytes,
                                                      const bool require_writable) noexcept {
    if (!IsUserVirtualAddressRange(user_address, length_bytes)) {
        return UserMemoryCopyStatus::InvalidUserRange;
    }

    const uint64_t inclusive_end_address =
        user_address + length_bytes - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
    uint64_t page_address = user_address & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
    const uint64_t final_page_address = inclusive_end_address & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
    while (true) {
        PageMapping mapping{};
        if (QueryActivePage(page_address, mapping) != PageTableStatus::Succeeded) {
            return UserMemoryCopyStatus::PageNotMapped;
        }
        if (!mapping.permissions.user_accessible) {
            return UserMemoryCopyStatus::PageNotUserAccessible;
        }
        if (require_writable && !mapping.permissions.writable) {
            return UserMemoryCopyStatus::PageNotWritable;
        }
        if (page_address == final_page_address) {
            break;
        }
        page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }
    return UserMemoryCopyStatus::Succeeded;
}

[[nodiscard]] UserAddressSpaceStatus MapElfSegment(const UserElfReader &reader,
                                                   const UserElfLoadSegment &segment,
                                                   const uint64_t root_physical_address,
                                                   uint64_t &mapped_page_count) noexcept {
    const uint64_t segment_page_count =
        (segment.memory_size_bytes + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES -
         OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) /
        OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    uint64_t remaining_file_size_bytes = segment.file_size_bytes;
    for (uint64_t page_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; page_index < segment_page_count;
         ++page_index) {
        const uint64_t page_virtual_address =
            segment.virtual_address + page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        uint64_t page_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        const KernelUserPageStatus page_status =
            AllocateAndMapUserPage(root_physical_address, page_virtual_address, segment.writable,
                                   segment.executable, page_physical_address);
        if (page_status == KernelUserPageStatus::FrameAllocationFailed) {
            return UserAddressSpaceStatus::PageAllocationFailed;
        }
        if (page_status != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
        ++mapped_page_count;

        uint8_t *const page = PhysicalPagePointer(page_physical_address);
        if (page == nullptr || !ZeroPhysicalPage(page_physical_address)) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
        const uint64_t page_file_size_bytes =
            Minimum(remaining_file_size_bytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
        if (page_file_size_bytes > OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
            const uint64_t page_file_offset =
                segment.file_offset + page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            if (reader.read == nullptr ||
                !reader.read(reader.context, page_file_offset, page, page_file_size_bytes)) {
                return UserAddressSpaceStatus::ImageReadFailed;
            }
            remaining_file_size_bytes -= page_file_size_bytes;
        }
    }
    return UserAddressSpaceStatus::Succeeded;
}

[[nodiscard]] UserAddressSpaceStatus MapUserStack(const uint64_t root_physical_address,
                                                  uint64_t &mapped_page_count) noexcept {
    for (uint64_t page_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         page_index < OS_KERNEL_USER_STACK_PAGE_COUNT; ++page_index) {
        const uint64_t page_virtual_address = OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS +
                                              page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        uint64_t page_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        const KernelUserPageStatus page_status = AllocateAndMapUserPage(
            root_physical_address, page_virtual_address, true, false, page_physical_address);
        if (page_status == KernelUserPageStatus::FrameAllocationFailed) {
            return UserAddressSpaceStatus::PageAllocationFailed;
        }
        if (page_status != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
        ++mapped_page_count;
        if (!ZeroPhysicalPage(page_physical_address)) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
    }
    return UserAddressSpaceStatus::Succeeded;
}

[[nodiscard]] bool ElfOverlapsStackReservation(const UserElfLayout &layout) noexcept {
    for (uint64_t segment_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segment_index < layout.load_segment_count; ++segment_index) {
        const UserElfLoadSegment &segment = layout.load_segments[segment_index];
        const uint64_t segment_end_address = segment.virtual_address + segment.memory_size_bytes;
        if (segment.virtual_address < OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS &&
            OS_KERNEL_USER_STACK_GUARD_VIRTUAL_ADDRESS < segment_end_address) {
            return true;
        }
    }
    return false;
}

}

UserAddressSpaceStatus
LoadUserAddressSpace(const uint8_t *image, const uint64_t image_size_bytes,
                     UserAddressSpace &address_space,
                     UserElfValidationStatus &elf_validation_status) noexcept {
    if (image == nullptr) {
        elf_validation_status = UserElfValidationStatus::NullImage;
        return UserAddressSpaceStatus::InvalidElf;
    }
    MemoryImageReaderContext context{
        .image = image,
        .image_size_bytes = image_size_bytes,
    };
    return LoadUserAddressSpace(
        UserElfReader{
            .context = &context,
            .image_size_bytes = image_size_bytes,
            .read = ReadMemoryImage,
        },
        address_space, elf_validation_status);
}

UserAddressSpaceStatus
LoadUserAddressSpace(const UserElfReader &reader, UserAddressSpace &address_space,
                     UserElfValidationStatus &elf_validation_status) noexcept {
    address_space = UserAddressSpace{};
    UserElfLayout layout{};
    elf_validation_status = ValidateUserElf(reader, layout);
    if (elf_validation_status != UserElfValidationStatus::Succeeded) {
        return UserAddressSpaceStatus::InvalidElf;
    }
    if (ElfOverlapsStackReservation(layout)) {
        return UserAddressSpaceStatus::StackCollision;
    }

    uint64_t root_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (CreateUserPageTable(root_physical_address) != KernelUserPageStatus::Succeeded) {
        return UserAddressSpaceStatus::PageTableCreationFailed;
    }
    uint64_t mapped_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    for (uint64_t segment_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segment_index < layout.load_segment_count; ++segment_index) {
        const UserAddressSpaceStatus segment_status = MapElfSegment(
            reader, layout.load_segments[segment_index], root_physical_address, mapped_page_count);
        if (segment_status != UserAddressSpaceStatus::Succeeded) {
            if (DestroyUserPageTable(root_physical_address) != KernelUserPageStatus::Succeeded) {
                return UserAddressSpaceStatus::RollbackFailed;
            }
            return segment_status;
        }
    }
    const UserAddressSpaceStatus stack_status =
        MapUserStack(root_physical_address, mapped_page_count);
    if (stack_status != UserAddressSpaceStatus::Succeeded) {
        if (DestroyUserPageTable(root_physical_address) != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::RollbackFailed;
        }
        return stack_status;
    }

    address_space = UserAddressSpace{
        .root_physical_address = root_physical_address,
        .entry_virtual_address = layout.entry_virtual_address,
        .stack_top_virtual_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS,
        .mapped_page_count = mapped_page_count,
    };
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus DestroyUserAddressSpace(UserAddressSpace &address_space) noexcept {
    if (DestroyUserPageTable(address_space.root_physical_address) !=
        KernelUserPageStatus::Succeeded) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    address_space = UserAddressSpace{};
    return UserAddressSpaceStatus::Succeeded;
}

UserMemoryCopyStatus CopyToUserAddressSpace(const uint64_t root_physical_address,
                                            const uint64_t user_address,
                                            const uint64_t length_bytes,
                                            const uint8_t *const source,
                                            const uint64_t source_size_bytes) noexcept {
    if (source == nullptr) {
        return UserMemoryCopyStatus::NullSource;
    }
    if (length_bytes > source_size_bytes) {
        return UserMemoryCopyStatus::SourceTooSmall;
    }
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserMemoryCopyStatus::Succeeded;
    }
    if (!IsUserVirtualAddressRange(user_address, length_bytes)) {
        return UserMemoryCopyStatus::InvalidUserRange;
    }

    uint64_t copied_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    while (copied_bytes < length_bytes) {
        const uint64_t current_user_address = user_address + copied_bytes;
        const uint64_t page_virtual_address =
            current_user_address & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
        const uint64_t page_offset = current_user_address & OS_KERNEL_USER_MEMORY_PAGE_MASK;
        const uint64_t chunk_bytes =
            Minimum(length_bytes - copied_bytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - page_offset);
        PageMapping mapping{};
        if (QueryAddressSpacePage(root_physical_address, page_virtual_address, mapping) !=
            PageTableStatus::Succeeded) {
            return UserMemoryCopyStatus::PageNotMapped;
        }
        if (!mapping.permissions.user_accessible) {
            return UserMemoryCopyStatus::PageNotUserAccessible;
        }
        if (!mapping.permissions.writable) {
            return UserMemoryCopyStatus::PageNotWritable;
        }
        uint8_t *const page = PhysicalPagePointer(mapping.physical_address);
        if (page == nullptr) {
            return UserMemoryCopyStatus::PageNotMapped;
        }
        CopyBytes(page + page_offset, source + copied_bytes, chunk_bytes);
        copied_bytes += chunk_bytes;
    }
    return UserMemoryCopyStatus::Succeeded;
}

UserMemoryCopyStatus CopyFromUser(const uint64_t user_address, const uint64_t length_bytes,
                                  uint8_t *destination,
                                  const uint64_t destination_capacity_bytes) noexcept {
    if (destination == nullptr) {
        return UserMemoryCopyStatus::NullDestination;
    }
    if (length_bytes > destination_capacity_bytes) {
        return UserMemoryCopyStatus::DestinationTooSmall;
    }
    const UserMemoryCopyStatus validation_status =
        ValidateUserMemory(user_address, length_bytes, false);
    if (validation_status != UserMemoryCopyStatus::Succeeded) {
        return validation_status;
    }
    CopyBytes(destination, reinterpret_cast<const uint8_t *>(user_address), length_bytes);
    return UserMemoryCopyStatus::Succeeded;
}

UserMemoryCopyStatus ValidateUserWritableMemory(const uint64_t user_address,
                                                const uint64_t length_bytes) noexcept {
    return ValidateUserMemory(user_address, length_bytes, true);
}

UserMemoryCopyStatus CopyToUser(const uint64_t user_address, const uint64_t length_bytes,
                                const uint8_t *source, const uint64_t source_size_bytes) noexcept {
    if (source == nullptr) {
        return UserMemoryCopyStatus::NullSource;
    }
    if (length_bytes > source_size_bytes) {
        return UserMemoryCopyStatus::SourceTooSmall;
    }
    const UserMemoryCopyStatus validation_status =
        ValidateUserWritableMemory(user_address, length_bytes);
    if (validation_status != UserMemoryCopyStatus::Succeeded) {
        return validation_status;
    }
    CopyBytes(reinterpret_cast<uint8_t *>(user_address), source, length_bytes);
    return UserMemoryCopyStatus::Succeeded;
}
}
