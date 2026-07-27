#include "os/kernel/user/user_memory.hpp"

#include "os/kernel/memory/memory_manager.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_MEMORY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_MASK =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
constexpr uint64_t OS_KERNEL_USER_MEMORY_STACK_GROWTH_GAP_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_PRESENT_BIT = 1ULL << 0ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_WRITE_BIT = 1ULL << 1ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_USER_BIT = 1ULL << 2ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_RESERVED_BIT = 1ULL << 3ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_INSTRUCTION_BIT = 1ULL << 4ULL;
constexpr uint8_t OS_KERNEL_USER_MEMORY_ZERO_BYTE = 0U;

struct MemoryImageReaderContext final {
    const uint8_t *image;
    uint64_t image_size_bytes;
};

VirtualMemoryAreaDescriptor
    user_virtual_memory_descriptors[OS_KERNEL_USER_VMA_DESCRIPTOR_POOL_CAPACITY]{};
VirtualMemoryAreaPool user_virtual_memory_pool{};
UserAddressSpace *active_user_address_space;
bool user_virtual_memory_initialized;

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] uint64_t AlignDownToPage(const uint64_t value) noexcept {
    return value & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
}

[[nodiscard]] bool AlignUpToPage(const uint64_t value, uint64_t &aligned_value) noexcept {
    if (value > UINT64_MAX - OS_KERNEL_USER_MEMORY_PAGE_MASK) {
        return false;
    }
    aligned_value = (value + OS_KERNEL_USER_MEMORY_PAGE_MASK) & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
    return true;
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

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
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

[[nodiscard]] bool PermissionsAllow(const VirtualMemoryArea &area, const bool require_writable,
                                    const bool require_executable) noexcept {
    if (!area.permissions.readable) {
        return false;
    }
    if (require_writable && !area.permissions.writable) {
        return false;
    }
    if (require_executable && !area.permissions.executable) {
        return false;
    }
    return true;
}

[[nodiscard]] UserVirtualMemoryStatus MapDemandPage(UserAddressSpace &address_space,
                                                    const uint64_t page_virtual_address,
                                                    const VirtualMemoryArea &area) noexcept {
    uint64_t page_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const KernelUserPageStatus page_status = AllocateAndMapUserPage(
        address_space.root_physical_address, page_virtual_address, area.permissions.writable,
        area.permissions.executable, page_physical_address);
    if (page_status == KernelUserPageStatus::FrameAllocationFailed) {
        return UserVirtualMemoryStatus::PageAllocationFailed;
    }
    if (page_status != KernelUserPageStatus::Succeeded) {
        return UserVirtualMemoryStatus::PageMappingFailed;
    }
    if (!ZeroPhysicalPage(page_physical_address)) {
        uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        return ReleaseUserPage(address_space.root_physical_address, page_virtual_address,
                               reclaimed_table_frame_count) == KernelUserPageStatus::Succeeded
                   ? UserVirtualMemoryStatus::PageMappingFailed
                   : UserVirtualMemoryStatus::Corrupt;
    }
    ++address_space.mapped_page_count;
    if (address_space.mapped_page_count > address_space.peak_mapped_page_count) {
        address_space.peak_mapped_page_count = address_space.mapped_page_count;
    }
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus ReleaseMappedPages(UserAddressSpace &address_space,
                                                         const uint64_t begin_address,
                                                         const uint64_t end_address,
                                                         const bool count_as_unmap) noexcept {
    for (uint64_t page_address = begin_address; page_address < end_address;
         page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        PageMapping mapping{};
        const PageTableStatus query_status =
            QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping);
        if (query_status == PageTableStatus::NotMapped) {
            continue;
        }
        if (query_status != PageTableStatus::Succeeded || !mapping.permissions.user_accessible ||
            address_space.mapped_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        if (ReleaseUserPage(address_space.root_physical_address, page_address,
                            reclaimed_table_frame_count) != KernelUserPageStatus::Succeeded) {
            return UserVirtualMemoryStatus::PageReleaseFailed;
        }
        --address_space.mapped_page_count;
        address_space.page_table_reclaimed_frame_count += reclaimed_table_frame_count;
        if (count_as_unmap) {
            ++address_space.unmap_released_page_count;
        }
    }
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus ResolveNonStackPage(UserAddressSpace &address_space,
                                                          const uint64_t page_address,
                                                          const bool require_writable) noexcept {
    PageMapping mapping{};
    const PageTableStatus query_status =
        QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping);
    if (query_status == PageTableStatus::Succeeded) {
        if (!mapping.permissions.user_accessible) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (require_writable && !mapping.permissions.writable) {
            return UserVirtualMemoryStatus::InvalidProtection;
        }
        return UserVirtualMemoryStatus::Succeeded;
    }
    if (query_status != PageTableStatus::NotMapped) {
        return UserVirtualMemoryStatus::Corrupt;
    }

    VirtualMemoryArea area{};
    if (address_space.virtual_memory_map.FindContaining(page_address, area) !=
        VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    if (area.kind == VirtualMemoryAreaKind::UserStack ||
        area.kind == VirtualMemoryAreaKind::ExecutableImage ||
        !PermissionsAllow(area, require_writable, false)) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    const UserVirtualMemoryStatus map_status = MapDemandPage(address_space, page_address, area);
    if (map_status == UserVirtualMemoryStatus::Succeeded) {
        ++address_space.demand_page_fault_count;
    }
    return map_status;
}

[[nodiscard]] UserMemoryCopyStatus ValidateUserMemory(const uint64_t user_address,
                                                      const uint64_t length_bytes,
                                                      const bool require_writable) noexcept {
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserMemoryCopyStatus::Succeeded;
    }
    if (!IsUserVirtualAddressRange(user_address, length_bytes)) {
        return UserMemoryCopyStatus::InvalidUserRange;
    }

    const uint64_t inclusive_end_address =
        user_address + length_bytes - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
    uint64_t page_address = AlignDownToPage(user_address);
    const uint64_t final_page_address = AlignDownToPage(inclusive_end_address);
    while (true) {
        PageMapping mapping{};
        PageTableStatus query_status = QueryActivePage(page_address, mapping);
        if (query_status == PageTableStatus::NotMapped && active_user_address_space != nullptr) {
            if (ResolveNonStackPage(*active_user_address_space, page_address, require_writable) !=
                UserVirtualMemoryStatus::Succeeded) {
                return UserMemoryCopyStatus::PageResolutionFailed;
            }
            query_status = QueryActivePage(page_address, mapping);
        }
        if (query_status != PageTableStatus::Succeeded) {
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
                                                   UserAddressSpace &address_space) noexcept {
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
            AllocateAndMapUserPage(address_space.root_physical_address, page_virtual_address,
                                   segment.writable, segment.executable, page_physical_address);
        if (page_status == KernelUserPageStatus::FrameAllocationFailed) {
            return UserAddressSpaceStatus::PageAllocationFailed;
        }
        if (page_status != KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::PageMappingFailed;
        }
        ++address_space.mapped_page_count;
        if (address_space.mapped_page_count > address_space.peak_mapped_page_count) {
            address_space.peak_mapped_page_count = address_space.mapped_page_count;
        }

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

[[nodiscard]] bool CalculateProgramBreakBase(const UserElfLayout &layout,
                                             uint64_t &program_break_base) noexcept {
    uint64_t highest_segment_end = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    for (uint64_t segment_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segment_index < layout.load_segment_count; ++segment_index) {
        const UserElfLoadSegment &segment = layout.load_segments[segment_index];
        const uint64_t segment_end = segment.virtual_address + segment.memory_size_bytes;
        if (segment_end > highest_segment_end) {
            highest_segment_end = segment_end;
        }
    }
    return AlignUpToPage(highest_segment_end, program_break_base) &&
           program_break_base < os::abi::OS_ABI_USER_PROGRAM_BREAK_LIMIT_ADDRESS;
}

[[nodiscard]] bool InsertInitialAreas(const UserElfLayout &layout,
                                      UserAddressSpace &address_space) noexcept {
    for (uint64_t segment_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segment_index < layout.load_segment_count; ++segment_index) {
        const UserElfLoadSegment &segment = layout.load_segments[segment_index];
        uint64_t segment_end_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        if (!AlignUpToPage(segment.virtual_address + segment.memory_size_bytes,
                           segment_end_address) ||
            address_space.virtual_memory_map.Insert(VirtualMemoryArea{
                .begin_address = segment.virtual_address,
                .end_address = segment_end_address,
                .permissions =
                    {
                        .readable = true,
                        .writable = segment.writable,
                        .executable = segment.executable,
                    },
                .kind = VirtualMemoryAreaKind::ExecutableImage,
            }) != VirtualMemoryAreaStatus::Succeeded) {
            return false;
        }
    }
    return address_space.virtual_memory_map.Insert(VirtualMemoryArea{
               .begin_address = OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS,
               .end_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS,
               .permissions =
                   {
                       .readable = true,
                       .writable = true,
                       .executable = false,
                   },
               .kind = VirtualMemoryAreaKind::UserStack,
           }) == VirtualMemoryAreaStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus
MapVirtualMemoryAreaStatus(const VirtualMemoryAreaStatus status) noexcept {
    if (status == VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::Succeeded;
    }
    if (status == VirtualMemoryAreaStatus::Overlap) {
        return UserVirtualMemoryStatus::AddressInUse;
    }
    if (status == VirtualMemoryAreaStatus::MetadataExhausted ||
        status == VirtualMemoryAreaStatus::AreaLimitExceeded) {
        return UserVirtualMemoryStatus::MetadataExhausted;
    }
    if (status == VirtualMemoryAreaStatus::NotMapped ||
        status == VirtualMemoryAreaStatus::KindMismatch ||
        status == VirtualMemoryAreaStatus::InvalidRange ||
        status == VirtualMemoryAreaStatus::InvalidAlignment) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    if (status == VirtualMemoryAreaStatus::NotInitialized) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    return UserVirtualMemoryStatus::Corrupt;
}

[[nodiscard]] bool ProtectionFlagsAreValid(const uint64_t protection_flags) noexcept {
    if ((protection_flags & ~os::abi::OS_ABI_MEMORY_PROTECTION_VALID_MASK) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return false;
    }
    const bool readable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_READ) !=
                          OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool writable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_WRITE) !=
                          OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool executable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_EXECUTE) !=
                            OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    return (!writable && !executable) || (readable && !(writable && executable));
}

[[nodiscard]] VirtualMemoryAreaPermissions
DecodeProtectionFlags(const uint64_t protection_flags) noexcept {
    return VirtualMemoryAreaPermissions{
        .readable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_READ) !=
                    OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
        .writable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_WRITE) !=
                    OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
        .executable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_EXECUTE) !=
                      OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
    };
}

[[nodiscard]] uint64_t CountKindPages(const UserAddressSpace &address_space,
                                      const VirtualMemoryAreaKind kind) noexcept {
    uint64_t page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         area_index < address_space.virtual_memory_map.AreaCount(); ++area_index) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        }
        if (area.kind == kind) {
            page_count +=
                (area.end_address - area.begin_address) / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        }
    }
    return page_count;
}

}

UserAddressSpaceStatus InitializeUserVirtualMemory() noexcept {
    if (user_virtual_memory_initialized) {
        return UserAddressSpaceStatus::Succeeded;
    }
    if (user_virtual_memory_pool.Initialize(user_virtual_memory_descriptors,
                                            OS_KERNEL_USER_VMA_DESCRIPTOR_POOL_CAPACITY) !=
        VirtualMemoryAreaStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    active_user_address_space = nullptr;
    user_virtual_memory_initialized = true;
    return UserAddressSpaceStatus::Succeeded;
}

VirtualMemoryAreaPoolStatistics GetUserVirtualMemoryPoolStatistics() noexcept {
    return user_virtual_memory_pool.Statistics();
}

UserAddressSpaceStatus
LoadUserAddressSpace(const uint8_t *const image, const uint64_t image_size_bytes,
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
    if (InitializeUserVirtualMemory() != UserAddressSpaceStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    UserElfLayout layout{};
    elf_validation_status = ValidateUserElf(reader, layout);
    if (elf_validation_status != UserElfValidationStatus::Succeeded) {
        return UserAddressSpaceStatus::InvalidElf;
    }
    if (ElfOverlapsStackReservation(layout)) {
        return UserAddressSpaceStatus::StackCollision;
    }
    uint64_t program_break_base = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!CalculateProgramBreakBase(layout, program_break_base)) {
        return UserAddressSpaceStatus::ProgramBreakCollision;
    }
    if (address_space.virtual_memory_map.Initialize(
            user_virtual_memory_pool, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            OS_KERNEL_USER_VMA_PER_PROCESS_HARD_LIMIT) != VirtualMemoryAreaStatus::Succeeded ||
        !InsertInitialAreas(layout, address_space)) {
        if (address_space.virtual_memory_map.Validate() == VirtualMemoryAreaStatus::Succeeded) {
            static_cast<void>(address_space.virtual_memory_map.Destroy());
        }
        address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::VirtualMemoryAreaFailure;
    }

    if (CreateUserPageTable(address_space.root_physical_address) !=
        KernelUserPageStatus::Succeeded) {
        static_cast<void>(address_space.virtual_memory_map.Destroy());
        address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::PageTableCreationFailed;
    }
    address_space.entry_virtual_address = layout.entry_virtual_address;
    address_space.stack_top_virtual_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS;
    address_space.stack_committed_bottom_virtual_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS;
    address_space.program_break_base_address = program_break_base;
    address_space.program_break_address = program_break_base;
    address_space.program_break_limit_address = os::abi::OS_ABI_USER_PROGRAM_BREAK_LIMIT_ADDRESS;

    for (uint64_t segment_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segment_index < layout.load_segment_count; ++segment_index) {
        const UserAddressSpaceStatus segment_status =
            MapElfSegment(reader, layout.load_segments[segment_index], address_space);
        if (segment_status != UserAddressSpaceStatus::Succeeded) {
            if (DestroyUserPageTable(address_space.root_physical_address) !=
                    KernelUserPageStatus::Succeeded ||
                address_space.virtual_memory_map.Destroy() != VirtualMemoryAreaStatus::Succeeded) {
                address_space = UserAddressSpace{};
                return UserAddressSpaceStatus::RollbackFailed;
            }
            address_space = UserAddressSpace{};
            return segment_status;
        }
    }
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus DestroyUserAddressSpace(UserAddressSpace &address_space) noexcept {
    if (active_user_address_space == &address_space) {
        active_user_address_space = nullptr;
    }
    if (address_space.root_physical_address != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
        DestroyUserPageTable(address_space.root_physical_address) !=
            KernelUserPageStatus::Succeeded) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    const VirtualMemoryAreaStatus validation_status = address_space.virtual_memory_map.Validate();
    if (validation_status == VirtualMemoryAreaStatus::Succeeded &&
        address_space.virtual_memory_map.Destroy() != VirtualMemoryAreaStatus::Succeeded) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    if (validation_status != VirtualMemoryAreaStatus::Succeeded &&
        validation_status != VirtualMemoryAreaStatus::NotInitialized) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    address_space = UserAddressSpace{};
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus PrepareUserStack(UserAddressSpace &address_space,
                                        const uint64_t lowest_required_address) noexcept {
    if (lowest_required_address < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
        lowest_required_address >= OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS ||
        address_space.stack_committed_bottom_virtual_address !=
            OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return UserAddressSpaceStatus::StackPreparationFailed;
    }
    const uint64_t first_page_address = AlignDownToPage(lowest_required_address);
    VirtualMemoryArea stack_area{};
    if (address_space.virtual_memory_map.FindContaining(first_page_address, stack_area) !=
            VirtualMemoryAreaStatus::Succeeded ||
        stack_area.kind != VirtualMemoryAreaKind::UserStack) {
        return UserAddressSpaceStatus::StackPreparationFailed;
    }
    for (uint64_t page_address = first_page_address;
         page_address < OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS;
         page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        const UserVirtualMemoryStatus map_status =
            MapDemandPage(address_space, page_address, stack_area);
        if (map_status != UserVirtualMemoryStatus::Succeeded) {
            return UserAddressSpaceStatus::StackPreparationFailed;
        }
    }
    address_space.stack_committed_bottom_virtual_address = first_page_address;
    return UserAddressSpaceStatus::Succeeded;
}

UserVirtualMemoryStatus
MapAnonymousMemory(UserAddressSpace &address_space, const uint64_t requested_address,
                   const uint64_t length_bytes, const uint64_t protection_flags,
                   const uint64_t map_flags, uint64_t &mapped_address) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        (map_flags & ~os::abi::OS_ABI_MEMORY_MAP_VALID_FLAG_MASK) !=
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        !ProtectionFlagsAreValid(protection_flags)) {
        return length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE
                   ? UserVirtualMemoryStatus::InvalidRange
                   : UserVirtualMemoryStatus::InvalidProtection;
    }
    uint64_t aligned_length_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!AlignUpToPage(length_bytes, aligned_length_bytes) ||
        aligned_length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        aligned_length_bytes > os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS -
                                   os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS) {
        return UserVirtualMemoryStatus::InvalidRange;
    }

    const bool fixed_mapping =
        (map_flags & os::abi::OS_ABI_MEMORY_MAP_FIXED) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t area_begin_address = requested_address;
    if (fixed_mapping) {
        if (requested_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
            (requested_address & OS_KERNEL_USER_MEMORY_PAGE_MASK) !=
                OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
            requested_address < os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS ||
            requested_address >
                os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS - aligned_length_bytes) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
    } else {
        if (requested_address != os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
        const VirtualMemoryAreaStatus gap_status = address_space.virtual_memory_map.FindFirstGap(
            os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS,
            os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS, aligned_length_bytes,
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES, area_begin_address);
        if (gap_status != VirtualMemoryAreaStatus::Succeeded) {
            return gap_status == VirtualMemoryAreaStatus::NotMapped
                       ? UserVirtualMemoryStatus::AddressSpaceExhausted
                       : MapVirtualMemoryAreaStatus(gap_status);
        }
    }

    const VirtualMemoryAreaStatus insert_status =
        address_space.virtual_memory_map.Insert(VirtualMemoryArea{
            .begin_address = area_begin_address,
            .end_address = area_begin_address + aligned_length_bytes,
            .permissions = DecodeProtectionFlags(protection_flags),
            .kind = VirtualMemoryAreaKind::Anonymous,
        });
    if (insert_status != VirtualMemoryAreaStatus::Succeeded) {
        return MapVirtualMemoryAreaStatus(insert_status);
    }
    mapped_address = area_begin_address;
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus UnmapAnonymousMemory(UserAddressSpace &address_space,
                                             const uint64_t address,
                                             const uint64_t length_bytes) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    uint64_t aligned_length_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        (address & OS_KERNEL_USER_MEMORY_PAGE_MASK) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        !AlignUpToPage(length_bytes, aligned_length_bytes) ||
        address < os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS ||
        address > os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS - aligned_length_bytes) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    const uint64_t end_address = address + aligned_length_bytes;
    const VirtualMemoryAreaStatus remove_status = address_space.virtual_memory_map.Remove(
        address, end_address, VirtualMemoryAreaKind::Anonymous);
    if (remove_status != VirtualMemoryAreaStatus::Succeeded) {
        return MapVirtualMemoryAreaStatus(remove_status);
    }
    return ReleaseMappedPages(address_space, address, end_address, true);
}

UserVirtualMemoryStatus SetProgramBreak(UserAddressSpace &address_space,
                                        const uint64_t requested_address,
                                        uint64_t &program_break_address) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    if (requested_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        program_break_address = address_space.program_break_address;
        return UserVirtualMemoryStatus::Succeeded;
    }
    if (requested_address < address_space.program_break_base_address ||
        requested_address > address_space.program_break_limit_address) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    uint64_t previous_page_end = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t requested_page_end = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!AlignUpToPage(address_space.program_break_address, previous_page_end) ||
        !AlignUpToPage(requested_address, requested_page_end)) {
        return UserVirtualMemoryStatus::InvalidRange;
    }

    if (requested_page_end > previous_page_end) {
        const VirtualMemoryAreaStatus insert_status =
            address_space.virtual_memory_map.Insert(VirtualMemoryArea{
                .begin_address = previous_page_end,
                .end_address = requested_page_end,
                .permissions =
                    {
                        .readable = true,
                        .writable = true,
                        .executable = false,
                    },
                .kind = VirtualMemoryAreaKind::ProgramBreak,
            });
        if (insert_status != VirtualMemoryAreaStatus::Succeeded) {
            return MapVirtualMemoryAreaStatus(insert_status);
        }
    } else if (requested_page_end < previous_page_end) {
        const VirtualMemoryAreaStatus remove_status = address_space.virtual_memory_map.Remove(
            requested_page_end, previous_page_end, VirtualMemoryAreaKind::ProgramBreak);
        if (remove_status != VirtualMemoryAreaStatus::Succeeded) {
            return MapVirtualMemoryAreaStatus(remove_status);
        }
        const UserVirtualMemoryStatus release_status =
            ReleaseMappedPages(address_space, requested_page_end, previous_page_end, true);
        if (release_status != UserVirtualMemoryStatus::Succeeded) {
            return release_status;
        }
    }
    address_space.program_break_address = requested_address;
    program_break_address = requested_address;
    return UserVirtualMemoryStatus::Succeeded;
}

os::abi::VirtualMemoryStatistics
GetUserVirtualMemoryStatistics(const UserAddressSpace &address_space) noexcept {
    const VirtualMemoryMapStatistics map_statistics = address_space.virtual_memory_map.Statistics();
    return os::abi::VirtualMemoryStatistics{
        .area_count = map_statistics.area_count,
        .virtual_page_count = map_statistics.mapped_page_count,
        .resident_page_count = address_space.mapped_page_count,
        .peak_resident_page_count = address_space.peak_mapped_page_count,
        .executable_image_page_count =
            CountKindPages(address_space, VirtualMemoryAreaKind::ExecutableImage),
        .anonymous_page_count = CountKindPages(address_space, VirtualMemoryAreaKind::Anonymous),
        .program_break_page_count =
            CountKindPages(address_space, VirtualMemoryAreaKind::ProgramBreak),
        .stack_reserved_page_count =
            CountKindPages(address_space, VirtualMemoryAreaKind::UserStack),
        .stack_resident_page_count = (address_space.stack_top_virtual_address -
                                      address_space.stack_committed_bottom_virtual_address) /
                                     OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
        .demand_page_fault_count = address_space.demand_page_fault_count,
        .stack_growth_page_fault_count = address_space.stack_growth_page_fault_count,
        .unmap_released_page_count = address_space.unmap_released_page_count,
        .page_table_reclaimed_frame_count = address_space.page_table_reclaimed_frame_count,
        .program_break_address = address_space.program_break_address,
    };
}

UserPageFaultStatus HandleUserPageFault(UserAddressSpace &address_space,
                                        const uint64_t fault_address, const uint64_t error_code,
                                        const uint64_t user_stack_pointer) noexcept {
    if ((error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_USER_BIT) ==
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserPageFaultStatus::NotUserFault;
    }
    if ((error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_RESERVED_BIT) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserPageFaultStatus::ReservedBitViolation;
    }
    if ((error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_PRESENT_BIT) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserPageFaultStatus::PresentPageViolation;
    }
    const bool write_access = (error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_WRITE_BIT) !=
                              OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool instruction_access =
        (error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_INSTRUCTION_BIT) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const uint64_t page_address = AlignDownToPage(fault_address);
    VirtualMemoryArea area{};
    if (address_space.virtual_memory_map.FindContaining(page_address, area) !=
        VirtualMemoryAreaStatus::Succeeded) {
        return UserPageFaultStatus::AreaNotMapped;
    }
    if (!PermissionsAllow(area, write_access, instruction_access)) {
        return instruction_access ? UserPageFaultStatus::InstructionFetchViolation
                                  : UserPageFaultStatus::PermissionDenied;
    }

    if (area.kind == VirtualMemoryAreaKind::UserStack) {
        if (page_address + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES !=
                address_space.stack_committed_bottom_virtual_address ||
            page_address < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
            (fault_address > user_stack_pointer &&
             fault_address - user_stack_pointer > OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
            (user_stack_pointer > fault_address &&
             user_stack_pointer - fault_address > OS_KERNEL_USER_MEMORY_STACK_GROWTH_GAP_BYTES)) {
            return UserPageFaultStatus::InvalidStackGrowth;
        }
    } else if (area.kind == VirtualMemoryAreaKind::ExecutableImage) {
        return UserPageFaultStatus::Corrupt;
    }

    const UserVirtualMemoryStatus map_status = MapDemandPage(address_space, page_address, area);
    if (map_status == UserVirtualMemoryStatus::PageAllocationFailed) {
        return UserPageFaultStatus::PageAllocationFailed;
    }
    if (map_status != UserVirtualMemoryStatus::Succeeded) {
        return UserPageFaultStatus::PageMappingFailed;
    }
    ++address_space.demand_page_fault_count;
    if (area.kind == VirtualMemoryAreaKind::UserStack) {
        address_space.stack_committed_bottom_virtual_address = page_address;
        ++address_space.stack_growth_page_fault_count;
    }
    return UserPageFaultStatus::Handled;
}

void SetActiveUserAddressSpace(UserAddressSpace *const address_space) noexcept {
    active_user_address_space = address_space;
}

UserMemoryCopyStatus CopyToUserAddressSpace(UserAddressSpace &address_space,
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
        const uint64_t page_virtual_address = AlignDownToPage(current_user_address);
        const uint64_t page_offset = current_user_address & OS_KERNEL_USER_MEMORY_PAGE_MASK;
        const uint64_t chunk_bytes =
            Minimum(length_bytes - copied_bytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - page_offset);
        PageMapping mapping{};
        if (QueryAddressSpacePage(address_space.root_physical_address, page_virtual_address,
                                  mapping) != PageTableStatus::Succeeded) {
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
                                  uint8_t *const destination,
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
                                const uint8_t *const source,
                                const uint64_t source_size_bytes) noexcept {
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
