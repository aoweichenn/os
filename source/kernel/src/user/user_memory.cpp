#include "os/kernel/user/user_memory.hpp"

#include "os/kernel/memory/memory_manager.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "os/kernel/user/file_backing.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_MEMORY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_MASK =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
constexpr uint64_t OS_KERNEL_USER_MEMORY_STACK_GROWTH_GAP_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_FRAME_DIVISOR = 16ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_MINIMUM_CAPACITY = 256ULL;
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

struct VfsImageReaderContext final {
    fs::Vfs *vfs;
    fs::OpenFile open_file;
};

VirtualMemoryAreaDescriptor
    user_virtual_memory_descriptors[OS_KERNEL_USER_VMA_DESCRIPTOR_POOL_CAPACITY]{};
VirtualMemoryAreaPool user_virtual_memory_pool{};
UserFileBackingDescriptor user_file_backing_descriptors[OS_KERNEL_USER_FILE_BACKING_CAPACITY]{};
UserFileBackingManager user_file_backing_manager{};
FilePageCacheEntry user_file_page_cache_entries[OS_KERNEL_USER_FILE_PAGE_CACHE_MAXIMUM_CAPACITY]{};
FilePageCache user_file_page_cache{};
UserPageReferenceEntry user_page_reference_entries[OS_KERNEL_USER_PAGE_REFERENCE_CAPACITY]{};
UserPageReferenceManager user_page_reference_manager{};
UserAddressSpace *active_user_address_space;
uint64_t next_address_space_identifier = OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
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

[[nodiscard]] bool AssignAddressSpaceIdentifier(UserAddressSpace &address_space) noexcept {
    if (next_address_space_identifier == UINT64_MAX) {
        return false;
    }
    address_space.address_space_identifier = next_address_space_identifier;
    ++next_address_space_identifier;
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

[[nodiscard]] bool ReleasePrivatePhysicalPage(const uint64_t physical_address) noexcept {
    bool release_frame = false;
    if (user_page_reference_manager.Release(physical_address, release_frame) !=
        UserPageReferenceStatus::Succeeded) {
        return false;
    }
    return !release_frame || GetKernelPhysicalFrameAllocator().Release(
                                 PhysicalFrame{.physical_address = physical_address}) ==
                                 PhysicalFrameAllocatorStatus::Succeeded;
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

[[nodiscard]] bool ReadVfsImage(void *const context, const uint64_t offset_bytes,
                                uint8_t *const destination, const uint64_t length_bytes) noexcept {
    if (context == nullptr || destination == nullptr) {
        return false;
    }
    VfsImageReaderContext &reader_context = *static_cast<VfsImageReaderContext *>(context);
    uint64_t read_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    return reader_context.vfs != nullptr &&
           reader_context.vfs->ReadAt(reader_context.open_file, offset_bytes, destination,
                                      length_bytes, read_bytes) == fs::Status::Succeeded &&
           read_bytes == length_bytes;
}

[[nodiscard]] uint8_t *AccessPhysicalPage(void *const context,
                                          const uint64_t physical_address) noexcept {
    static_cast<void>(context);
    return PhysicalPagePointer(physical_address);
}

[[nodiscard]] bool FileIdentitiesEqual(const FileIdentity &left,
                                       const FileIdentity &right) noexcept {
    return left.superblock_identifier == right.superblock_identifier &&
           left.superblock_generation == right.superblock_generation &&
           left.node_identifier == right.node_identifier &&
           left.node_generation == right.node_generation;
}

[[nodiscard]] bool ReadBackingDescriptor(const VirtualMemoryArea &area,
                                         UserFileBackingDescriptor &descriptor) noexcept {
    return IsFileBackedVirtualMemoryAreaKind(area.kind) &&
           user_file_backing_manager.ReadDescriptor(area.backing_descriptor_index,
                                                    area.backing_generation,
                                                    descriptor) == UserFileBackingStatus::Succeeded;
}

[[nodiscard]] bool PageUsesFileCache(const VirtualMemoryArea &area,
                                     const uint64_t page_virtual_address,
                                     const UserFileBackingDescriptor &descriptor,
                                     FilePageIdentity &page_identity) noexcept {
    if (!IsFileBackedVirtualMemoryAreaKind(area.kind) || area.permissions.writable ||
        page_virtual_address < area.begin_address) {
        return false;
    }
    const uint64_t area_offset_bytes = page_virtual_address - area.begin_address;
    if (area_offset_bytes >= area.backing_data_length_bytes ||
        area.backing_data_length_bytes - area_offset_bytes < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        area.backing_file_offset_bytes > UINT64_MAX - area_offset_bytes) {
        return false;
    }
    const uint64_t file_offset_bytes = area.backing_file_offset_bytes + area_offset_bytes;
    if ((file_offset_bytes & OS_KERNEL_USER_MEMORY_PAGE_MASK) !=
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        file_offset_bytes > descriptor.size_bytes ||
        OS_KERNEL_MEMORY_PAGE_SIZE_BYTES > descriptor.size_bytes - file_offset_bytes) {
        return false;
    }
    page_identity = FilePageIdentity{
        .file = descriptor.identity,
        .page_index = file_offset_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
    };
    return true;
}

void RecordMappedPage(UserAddressSpace &address_space, const VirtualMemoryArea &area,
                      const bool cache_backed, const bool cache_hit) noexcept {
    ++address_space.mapped_page_count;
    if (address_space.mapped_page_count > address_space.peak_mapped_page_count) {
        address_space.peak_mapped_page_count = address_space.mapped_page_count;
    }
    if (!IsFileBackedVirtualMemoryAreaKind(area.kind)) {
        return;
    }
    ++address_space.file_page_fault_count;
    if (cache_hit) {
        ++address_space.page_cache_hit_count;
    }
    if (cache_backed) {
        ++address_space.shared_file_resident_page_count;
    } else {
        ++address_space.private_file_resident_page_count;
    }
}

[[nodiscard]] UserVirtualMemoryStatus MapFileDemandPage(UserAddressSpace &address_space,
                                                        const uint64_t page_virtual_address,
                                                        const VirtualMemoryArea &area) noexcept {
    UserFileBackingDescriptor descriptor{};
    if (!ReadBackingDescriptor(area, descriptor)) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    const uint64_t area_offset_bytes = page_virtual_address - area.begin_address;
    const uint64_t remaining_data_bytes = area_offset_bytes < area.backing_data_length_bytes
                                              ? area.backing_data_length_bytes - area_offset_bytes
                                              : OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t read_length_bytes = Minimum(remaining_data_bytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
    if (area.backing_file_offset_bytes > UINT64_MAX - area_offset_bytes) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    const uint64_t file_offset_bytes = area.backing_file_offset_bytes + area_offset_bytes;
    if (read_length_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        if (file_offset_bytes >= descriptor.size_bytes) {
            return UserVirtualMemoryStatus::FileReadFailed;
        }
        read_length_bytes = Minimum(read_length_bytes, descriptor.size_bytes - file_offset_bytes);
    }

    FilePageIdentity page_identity{};
    if (PageUsesFileCache(area, page_virtual_address, descriptor, page_identity)) {
        uint64_t physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        bool cache_hit = false;
        const FilePageCacheStatus cache_status = user_file_page_cache.Acquire(
            page_identity, &descriptor, ReadUserFileBackingPage, physical_address, cache_hit);
        if (cache_status == FilePageCacheStatus::CapacityExhausted) {
            return UserVirtualMemoryStatus::PageCacheExhausted;
        }
        if (cache_status != FilePageCacheStatus::Succeeded) {
            return cache_status == FilePageCacheStatus::SourceReadFailed
                       ? UserVirtualMemoryStatus::FileReadFailed
                       : UserVirtualMemoryStatus::PageMappingFailed;
        }
        if (MapExistingUserPage(address_space.root_physical_address, page_virtual_address,
                                physical_address, area.permissions.writable,
                                area.permissions.executable) != KernelUserPageStatus::Succeeded) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            return UserVirtualMemoryStatus::PageMappingFailed;
        }
        RecordMappedPage(address_space, area, true, cache_hit);
        return UserVirtualMemoryStatus::Succeeded;
    }

    uint64_t physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const KernelUserPageStatus page_status = AllocateAndMapUserPage(
        address_space.root_physical_address, page_virtual_address, area.permissions.writable,
        area.permissions.executable, physical_address);
    if (page_status == KernelUserPageStatus::FrameAllocationFailed) {
        return UserVirtualMemoryStatus::PageAllocationFailed;
    }
    if (page_status != KernelUserPageStatus::Succeeded || !ZeroPhysicalPage(physical_address)) {
        if (page_status == KernelUserPageStatus::Succeeded) {
            uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            static_cast<void>(ReleaseUserPage(address_space.root_physical_address,
                                              page_virtual_address, reclaimed_table_frame_count));
        }
        return UserVirtualMemoryStatus::PageMappingFailed;
    }
    if (read_length_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        uint8_t *const page = PhysicalPagePointer(physical_address);
        if (page == nullptr ||
            user_file_backing_manager.Read(area.backing_descriptor_index, area.backing_generation,
                                           file_offset_bytes, page,
                                           read_length_bytes) != UserFileBackingStatus::Succeeded) {
            uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            static_cast<void>(ReleaseUserPage(address_space.root_physical_address,
                                              page_virtual_address, reclaimed_table_frame_count));
            return UserVirtualMemoryStatus::FileReadFailed;
        }
    }
    RecordMappedPage(address_space, area, false, false);
    return UserVirtualMemoryStatus::Succeeded;
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
    if (IsFileBackedVirtualMemoryAreaKind(area.kind)) {
        return MapFileDemandPage(address_space, page_virtual_address, area);
    }
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
    RecordMappedPage(address_space, area, false, false);
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
        VirtualMemoryArea area{};
        const VirtualMemoryAreaStatus area_status =
            address_space.virtual_memory_map.FindContaining(page_address, area);
        bool cache_mapping = false;
        FilePageIdentity page_identity{};
        if (area_status == VirtualMemoryAreaStatus::Succeeded &&
            IsFileBackedVirtualMemoryAreaKind(area.kind)) {
            UserFileBackingDescriptor descriptor{};
            if (!ReadBackingDescriptor(area, descriptor)) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            cache_mapping = PageUsesFileCache(area, page_address, descriptor, page_identity);
        }
        if (cache_mapping) {
            uint64_t unmapped_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            if (UnmapUserPage(address_space.root_physical_address, page_address,
                              unmapped_physical_address,
                              reclaimed_table_frame_count) != KernelUserPageStatus::Succeeded ||
                unmapped_physical_address != mapping.physical_address ||
                user_file_page_cache.Release(page_identity, unmapped_physical_address) !=
                    FilePageCacheStatus::Succeeded) {
                return UserVirtualMemoryStatus::PageReleaseFailed;
            }
            if (address_space.shared_file_resident_page_count ==
                OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            --address_space.shared_file_resident_page_count;
        } else {
            uint64_t unmapped_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            if (UnmapUserPage(address_space.root_physical_address, page_address,
                              unmapped_physical_address,
                              reclaimed_table_frame_count) != KernelUserPageStatus::Succeeded ||
                unmapped_physical_address != mapping.physical_address ||
                !ReleasePrivatePhysicalPage(unmapped_physical_address)) {
                return UserVirtualMemoryStatus::PageReleaseFailed;
            }
            if (area_status == VirtualMemoryAreaStatus::Succeeded &&
                IsFileBackedVirtualMemoryAreaKind(area.kind)) {
                if (address_space.private_file_resident_page_count ==
                    OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                    return UserVirtualMemoryStatus::Corrupt;
                }
                --address_space.private_file_resident_page_count;
            }
        }
        if (mapping.permissions.copy_on_write) {
            if (address_space.copy_on_write_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            --address_space.copy_on_write_page_count;
        }
        --address_space.mapped_page_count;
        address_space.page_table_reclaimed_frame_count += reclaimed_table_frame_count;
        if (count_as_unmap) {
            ++address_space.unmap_released_page_count;
        }
    }
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus BreakCopyOnWritePage(UserAddressSpace &address_space,
                                                           const uint64_t page_address) noexcept {
    VirtualMemoryArea area{};
    PageMapping mapping{};
    if (address_space.virtual_memory_map.FindContaining(page_address, area) !=
            VirtualMemoryAreaStatus::Succeeded ||
        !area.permissions.writable ||
        QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping) !=
            PageTableStatus::Succeeded ||
        !mapping.permissions.user_accessible || mapping.permissions.writable ||
        !mapping.permissions.copy_on_write) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    uint64_t reference_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (user_page_reference_manager.ReadReferenceCount(mapping.physical_address, reference_count) !=
            UserPageReferenceStatus::Succeeded ||
        reference_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::Corrupt;
    }

    if (reference_count == OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) {
        if (ReplaceUserPage(address_space.root_physical_address, page_address,
                            mapping.physical_address, true, mapping.permissions.executable,
                            false) != KernelUserPageStatus::Succeeded ||
            user_page_reference_manager.RestoreExclusive(mapping.physical_address) !=
                UserPageReferenceStatus::Succeeded) {
            return UserVirtualMemoryStatus::CopyOnWriteFailure;
        }
        ++address_space.copy_on_write_exclusive_restore_count;
    } else {
        PhysicalFrame replacement_frame{};
        if (GetKernelPhysicalFrameAllocator().Allocate(replacement_frame) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
            return UserVirtualMemoryStatus::PageAllocationFailed;
        }
        uint8_t *const source_page = PhysicalPagePointer(mapping.physical_address);
        uint8_t *const replacement_page = PhysicalPagePointer(replacement_frame.physical_address);
        if (source_page == nullptr || replacement_page == nullptr) {
            static_cast<void>(GetKernelPhysicalFrameAllocator().Release(replacement_frame));
            return UserVirtualMemoryStatus::CopyOnWriteFailure;
        }
        CopyBytes(replacement_page, source_page, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
        if (ReplaceUserPage(address_space.root_physical_address, page_address,
                            replacement_frame.physical_address, true,
                            mapping.permissions.executable,
                            false) != KernelUserPageStatus::Succeeded) {
            static_cast<void>(GetKernelPhysicalFrameAllocator().Release(replacement_frame));
            return UserVirtualMemoryStatus::CopyOnWriteFailure;
        }
        bool release_frame = false;
        if (user_page_reference_manager.Release(mapping.physical_address, release_frame) !=
                UserPageReferenceStatus::Succeeded ||
            release_frame) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        ++address_space.copy_on_write_copy_count;
    }
    if (address_space.copy_on_write_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    --address_space.copy_on_write_page_count;
    ++address_space.copy_on_write_fault_count;
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus ResolveNonStackPage(UserAddressSpace &address_space,
                                                          const uint64_t page_address,
                                                          const bool require_writable,
                                                          const bool require_executable) noexcept {
    PageMapping mapping{};
    const PageTableStatus query_status =
        QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping);
    if (query_status == PageTableStatus::Succeeded) {
        if (!mapping.permissions.user_accessible) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (require_writable && !mapping.permissions.writable) {
            if (!mapping.permissions.copy_on_write) {
                return UserVirtualMemoryStatus::InvalidProtection;
            }
            return BreakCopyOnWritePage(address_space, page_address);
        }
        if (require_executable && !mapping.permissions.executable) {
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
        !PermissionsAllow(area, require_writable, require_executable)) {
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
            if (ResolveNonStackPage(*active_user_address_space, page_address, require_writable,
                                    false) != UserVirtualMemoryStatus::Succeeded) {
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
            if (active_user_address_space == nullptr || !mapping.permissions.copy_on_write ||
                BreakCopyOnWritePage(*active_user_address_space, page_address) !=
                    UserVirtualMemoryStatus::Succeeded ||
                QueryActivePage(page_address, mapping) != PageTableStatus::Succeeded ||
                !mapping.permissions.writable) {
                return UserMemoryCopyStatus::PageNotWritable;
            }
        }
        if (page_address == final_page_address) {
            break;
        }
        page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }
    return UserMemoryCopyStatus::Succeeded;
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
                                      const uint64_t backing_descriptor_index,
                                      const uint64_t backing_generation,
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
                .backing_descriptor_index = backing_descriptor_index,
                .backing_generation = backing_generation,
                .backing_file_offset_bytes = segment.file_offset,
                .backing_data_length_bytes = segment.file_size_bytes,
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

[[nodiscard]] bool BackingIsStillReferenced(const UserAddressSpace &address_space,
                                            const uint64_t descriptor_index,
                                            const uint64_t generation) noexcept {
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         area_index < address_space.virtual_memory_map.AreaCount(); ++area_index) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return true;
        }
        if (IsFileBackedVirtualMemoryAreaKind(area.kind) &&
            area.backing_descriptor_index == descriptor_index &&
            area.backing_generation == generation) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] UserVirtualMemoryStatus ReleaseBackingIfUnused(UserAddressSpace &address_space,
                                                             const uint64_t descriptor_index,
                                                             const uint64_t generation) noexcept {
    if (BackingIsStillReferenced(address_space, descriptor_index, generation)) {
        return UserVirtualMemoryStatus::Succeeded;
    }
    return user_file_backing_manager.Release(address_space.root_physical_address, descriptor_index,
                                             generation) == UserFileBackingStatus::Succeeded
               ? UserVirtualMemoryStatus::Succeeded
               : UserVirtualMemoryStatus::Corrupt;
}

[[nodiscard]] UserAddressSpaceStatus
LoadUserAddressSpaceInternal(const UserElfReader &reader, const uint8_t *const memory_image,
                             fs::Vfs *const vfs, const fs::OpenFile *const open_file,
                             UserAddressSpace &address_space,
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
    if (CreateUserPageTable(address_space.root_physical_address) !=
        KernelUserPageStatus::Succeeded) {
        address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::PageTableCreationFailed;
    }

    uint64_t backing_descriptor_index = UINT64_MAX;
    uint64_t backing_generation = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const UserFileBackingStatus backing_status =
        memory_image != nullptr
            ? user_file_backing_manager.AcquireMemoryImage(
                  address_space.root_physical_address, memory_image, reader.image_size_bytes,
                  backing_descriptor_index, backing_generation)
        : vfs != nullptr && open_file != nullptr
            ? user_file_backing_manager.AcquireVfsFile(address_space.root_physical_address, *vfs,
                                                       *open_file, backing_descriptor_index,
                                                       backing_generation)
            : UserFileBackingStatus::InvalidSource;
    if (backing_status != UserFileBackingStatus::Succeeded) {
        static_cast<void>(DestroyUserPageTable(address_space.root_physical_address));
        address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::VirtualMemoryAreaFailure;
    }
    if (address_space.virtual_memory_map.Initialize(
            user_virtual_memory_pool, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            OS_KERNEL_USER_VMA_PER_PROCESS_HARD_LIMIT) != VirtualMemoryAreaStatus::Succeeded ||
        !InsertInitialAreas(layout, backing_descriptor_index, backing_generation, address_space)) {
        if (address_space.virtual_memory_map.Validate() == VirtualMemoryAreaStatus::Succeeded) {
            static_cast<void>(address_space.virtual_memory_map.Destroy());
        }
        static_cast<void>(user_file_backing_manager.Release(
            address_space.root_physical_address, backing_descriptor_index, backing_generation));
        static_cast<void>(DestroyUserPageTable(address_space.root_physical_address));
        address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::VirtualMemoryAreaFailure;
    }

    address_space.entry_virtual_address = layout.entry_virtual_address;
    address_space.stack_top_virtual_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS;
    address_space.stack_committed_bottom_virtual_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS;
    address_space.program_break_base_address = program_break_base;
    address_space.program_break_address = program_break_base;
    address_space.program_break_limit_address = os::abi::OS_ABI_USER_PROGRAM_BREAK_LIMIT_ADDRESS;
    if (!AssignAddressSpaceIdentifier(address_space)) {
        static_cast<void>(DestroyUserAddressSpace(address_space));
        return UserAddressSpaceStatus::AddressSpaceIdentifierExhausted;
    }
    return UserAddressSpaceStatus::Succeeded;
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
    if (user_file_backing_manager.Initialize(user_file_backing_descriptors,
                                             OS_KERNEL_USER_FILE_BACKING_CAPACITY) !=
        UserFileBackingStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    const PhysicalFrameAllocatorStatistics frame_statistics = GetPhysicalFrameAllocatorStatistics();
    uint64_t page_cache_capacity =
        frame_statistics.managed_frame_count / OS_KERNEL_USER_MEMORY_FILE_CACHE_FRAME_DIVISOR;
    if (page_cache_capacity < OS_KERNEL_USER_MEMORY_FILE_CACHE_MINIMUM_CAPACITY) {
        page_cache_capacity = OS_KERNEL_USER_MEMORY_FILE_CACHE_MINIMUM_CAPACITY;
    }
    if (page_cache_capacity > OS_KERNEL_USER_FILE_PAGE_CACHE_MAXIMUM_CAPACITY) {
        page_cache_capacity = OS_KERNEL_USER_FILE_PAGE_CACHE_MAXIMUM_CAPACITY;
    }
    if (user_file_page_cache.Initialize(user_file_page_cache_entries, page_cache_capacity,
                                        GetKernelPhysicalFrameAllocator(), nullptr,
                                        AccessPhysicalPage) != FilePageCacheStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    if (user_page_reference_manager.Initialize(
            user_page_reference_entries, OS_KERNEL_USER_PAGE_REFERENCE_CAPACITY,
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) != UserPageReferenceStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    active_user_address_space = nullptr;
    user_virtual_memory_initialized = true;
    return UserAddressSpaceStatus::Succeeded;
}

VirtualMemoryAreaPoolStatistics GetUserVirtualMemoryPoolStatistics() noexcept {
    return user_virtual_memory_pool.Statistics();
}

FilePageCacheStatistics GetUserFilePageCacheStatistics() noexcept {
    return user_file_page_cache.Statistics();
}

UserPageReferenceStatistics GetUserPageReferenceStatistics() noexcept {
    return user_page_reference_manager.Statistics();
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
    const UserElfReader reader{
        .context = &context,
        .image_size_bytes = image_size_bytes,
        .read = ReadMemoryImage,
    };
    return LoadUserAddressSpaceInternal(reader, image, nullptr, nullptr, address_space,
                                        elf_validation_status);
}

UserAddressSpaceStatus
LoadUserAddressSpace(fs::Vfs &vfs, const fs::OpenFile &open_file, UserAddressSpace &address_space,
                     UserElfValidationStatus &elf_validation_status) noexcept {
    fs::NodeInformation information{};
    if (!open_file.open || !open_file.readable ||
        vfs.StatOpenFile(open_file, information) != fs::Status::Succeeded ||
        information.type != fs::NodeType::RegularFile) {
        elf_validation_status = UserElfValidationStatus::NullReader;
        return UserAddressSpaceStatus::InvalidElf;
    }
    VfsImageReaderContext context{
        .vfs = &vfs,
        .open_file = open_file,
    };
    const UserElfReader reader{
        .context = &context,
        .image_size_bytes = information.size_bytes,
        .read = ReadVfsImage,
    };
    return LoadUserAddressSpaceInternal(reader, nullptr, &vfs, &open_file, address_space,
                                        elf_validation_status);
}

[[nodiscard]] bool RestoreExclusiveForkPages(UserAddressSpace &parent_address_space) noexcept {
    const uint64_t area_count = parent_address_space.virtual_memory_map.AreaCount();
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea area{};
        if (parent_address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return false;
        }
        for (uint64_t page_address = area.begin_address; page_address < area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            PageMapping mapping{};
            const PageTableStatus query_status = QueryAddressSpacePage(
                parent_address_space.root_physical_address, page_address, mapping);
            if (query_status == PageTableStatus::NotMapped) {
                continue;
            }
            if (query_status != PageTableStatus::Succeeded) {
                return false;
            }
            bool cache_mapping = false;
            if (IsFileBackedVirtualMemoryAreaKind(area.kind)) {
                UserFileBackingDescriptor descriptor{};
                FilePageIdentity page_identity{};
                if (!ReadBackingDescriptor(area, descriptor)) {
                    return false;
                }
                cache_mapping = PageUsesFileCache(area, page_address, descriptor, page_identity);
            }
            if (cache_mapping) {
                continue;
            }
            uint64_t reference_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            const UserPageReferenceStatus reference_status =
                user_page_reference_manager.ReadReferenceCount(mapping.physical_address,
                                                               reference_count);
            if (reference_status == UserPageReferenceStatus::ReferenceNotFound) {
                continue;
            }
            if (reference_status != UserPageReferenceStatus::Succeeded) {
                return false;
            }
            if (reference_count != OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) {
                continue;
            }
            if (mapping.permissions.copy_on_write) {
                if (!area.permissions.writable ||
                    ReplaceUserPage(parent_address_space.root_physical_address, page_address,
                                    mapping.physical_address, true, mapping.permissions.executable,
                                    false) != KernelUserPageStatus::Succeeded ||
                    parent_address_space.copy_on_write_page_count ==
                        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                    return false;
                }
                --parent_address_space.copy_on_write_page_count;
            }
            if (user_page_reference_manager.RestoreExclusive(mapping.physical_address) !=
                UserPageReferenceStatus::Succeeded) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] UserAddressSpaceStatus
RollbackForkClone(UserAddressSpace &parent_address_space, UserAddressSpace &child_address_space,
                  const UserAddressSpaceStatus failure_status) noexcept {
    return DestroyUserAddressSpace(child_address_space) == UserAddressSpaceStatus::Succeeded &&
                   RestoreExclusiveForkPages(parent_address_space)
               ? failure_status
               : UserAddressSpaceStatus::RollbackFailed;
}

UserAddressSpaceStatus
CloneUserAddressSpaceForFork(UserAddressSpace &parent_address_space,
                             UserAddressSpace &child_address_space) noexcept {
    child_address_space = UserAddressSpace{};
    if (parent_address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded ||
        parent_address_space.root_physical_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    if (CreateUserPageTable(child_address_space.root_physical_address) !=
        KernelUserPageStatus::Succeeded) {
        return UserAddressSpaceStatus::PageTableCreationFailed;
    }
    if (child_address_space.virtual_memory_map.Initialize(
            user_virtual_memory_pool, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            OS_KERNEL_USER_VMA_PER_PROCESS_HARD_LIMIT) != VirtualMemoryAreaStatus::Succeeded) {
        static_cast<void>(DestroyUserPageTable(child_address_space.root_physical_address));
        child_address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }

    const uint64_t area_count = parent_address_space.virtual_memory_map.AreaCount();
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea parent_area{};
        if (parent_address_space.virtual_memory_map.ReadAt(area_index, parent_area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            static_cast<void>(DestroyUserAddressSpace(child_address_space));
            return UserAddressSpaceStatus::RollbackFailed;
        }
        VirtualMemoryArea child_area = parent_area;
        bool cloned_new_backing = false;
        if (IsFileBackedVirtualMemoryAreaKind(parent_area.kind)) {
            bool reused_backing = false;
            for (uint64_t previous_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
                 previous_index < area_index; ++previous_index) {
                VirtualMemoryArea previous_parent_area{};
                VirtualMemoryArea previous_child_area{};
                if (parent_address_space.virtual_memory_map.ReadAt(previous_index,
                                                                   previous_parent_area) !=
                        VirtualMemoryAreaStatus::Succeeded ||
                    child_address_space.virtual_memory_map.ReadAt(previous_index,
                                                                  previous_child_area) !=
                        VirtualMemoryAreaStatus::Succeeded) {
                    static_cast<void>(DestroyUserAddressSpace(child_address_space));
                    return UserAddressSpaceStatus::RollbackFailed;
                }
                if (previous_parent_area.backing_descriptor_index ==
                        parent_area.backing_descriptor_index &&
                    previous_parent_area.backing_generation == parent_area.backing_generation) {
                    child_area.backing_descriptor_index =
                        previous_child_area.backing_descriptor_index;
                    child_area.backing_generation = previous_child_area.backing_generation;
                    reused_backing = true;
                    break;
                }
            }
            if (!reused_backing) {
                if (user_file_backing_manager.Clone(
                        child_address_space.root_physical_address,
                        parent_area.backing_descriptor_index, parent_area.backing_generation,
                        child_area.backing_descriptor_index,
                        child_area.backing_generation) != UserFileBackingStatus::Succeeded) {
                    static_cast<void>(DestroyUserAddressSpace(child_address_space));
                    return UserAddressSpaceStatus::ForkBackingFailure;
                }
                cloned_new_backing = true;
            }
        }
        if (child_address_space.virtual_memory_map.Insert(child_area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            if (cloned_new_backing) {
                static_cast<void>(user_file_backing_manager.Release(
                    child_address_space.root_physical_address, child_area.backing_descriptor_index,
                    child_area.backing_generation));
            }
            static_cast<void>(DestroyUserAddressSpace(child_address_space));
            return UserAddressSpaceStatus::VirtualMemoryAreaFailure;
        }
    }

    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea parent_area{};
        VirtualMemoryArea child_area{};
        if (parent_address_space.virtual_memory_map.ReadAt(area_index, parent_area) !=
                VirtualMemoryAreaStatus::Succeeded ||
            child_address_space.virtual_memory_map.ReadAt(area_index, child_area) !=
                VirtualMemoryAreaStatus::Succeeded) {
            return RollbackForkClone(parent_address_space, child_address_space,
                                     UserAddressSpaceStatus::RollbackFailed);
        }
        for (uint64_t page_address = parent_area.begin_address;
             page_address < parent_area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            PageMapping mapping{};
            const PageTableStatus query_status = QueryAddressSpacePage(
                parent_address_space.root_physical_address, page_address, mapping);
            if (query_status == PageTableStatus::NotMapped) {
                continue;
            }
            if (query_status != PageTableStatus::Succeeded ||
                !mapping.permissions.user_accessible) {
                return RollbackForkClone(parent_address_space, child_address_space,
                                         UserAddressSpaceStatus::RollbackFailed);
            }
            bool cache_mapping = false;
            FilePageIdentity page_identity{};
            UserFileBackingDescriptor child_backing{};
            if (IsFileBackedVirtualMemoryAreaKind(parent_area.kind) &&
                ReadBackingDescriptor(child_area, child_backing)) {
                cache_mapping =
                    PageUsesFileCache(child_area, page_address, child_backing, page_identity);
            }
            bool retained_private_page = false;
            bool first_share = false;
            if (cache_mapping) {
                uint64_t retained_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
                bool cache_hit = false;
                const FilePageCacheStatus acquire_status = user_file_page_cache.Acquire(
                    page_identity, &child_backing, ReadUserFileBackingPage,
                    retained_physical_address, cache_hit);
                if (acquire_status != FilePageCacheStatus::Succeeded || !cache_hit ||
                    retained_physical_address != mapping.physical_address) {
                    if (acquire_status == FilePageCacheStatus::Succeeded) {
                        static_cast<void>(
                            user_file_page_cache.Release(page_identity, retained_physical_address));
                    }
                    return RollbackForkClone(parent_address_space, child_address_space,
                                             UserAddressSpaceStatus::RollbackFailed);
                }
            } else {
                const UserPageReferenceStatus retain_status =
                    user_page_reference_manager.RetainForFork(mapping.physical_address,
                                                              first_share);
                if (retain_status != UserPageReferenceStatus::Succeeded) {
                    return RollbackForkClone(parent_address_space, child_address_space,
                                             retain_status ==
                                                     UserPageReferenceStatus::CapacityExhausted
                                                 ? UserAddressSpaceStatus::ForkReferenceExhausted
                                                 : UserAddressSpaceStatus::RollbackFailed);
                }
                retained_private_page = true;
            }
            const bool copy_on_write = parent_area.permissions.writable;
            if (MapExistingUserPage(child_address_space.root_physical_address, page_address,
                                    mapping.physical_address, false, mapping.permissions.executable,
                                    copy_on_write) != KernelUserPageStatus::Succeeded) {
                if (cache_mapping) {
                    static_cast<void>(
                        user_file_page_cache.Release(page_identity, mapping.physical_address));
                } else if (retained_private_page) {
                    bool release_frame = false;
                    static_cast<void>(user_page_reference_manager.Release(mapping.physical_address,
                                                                          release_frame));
                    if (first_share) {
                        static_cast<void>(
                            user_page_reference_manager.RestoreExclusive(mapping.physical_address));
                    }
                }
                return RollbackForkClone(parent_address_space, child_address_space,
                                         UserAddressSpaceStatus::PageMappingFailed);
            }
            ++child_address_space.mapped_page_count;
            if (child_address_space.mapped_page_count >
                child_address_space.peak_mapped_page_count) {
                child_address_space.peak_mapped_page_count = child_address_space.mapped_page_count;
            }
            if (cache_mapping) {
                ++child_address_space.shared_file_resident_page_count;
            } else if (IsFileBackedVirtualMemoryAreaKind(child_area.kind)) {
                ++child_address_space.private_file_resident_page_count;
            }
            if (copy_on_write) {
                ++child_address_space.copy_on_write_page_count;
            }
        }
    }

    uint64_t copy_on_write_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea area{};
        if (parent_address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return RollbackForkClone(parent_address_space, child_address_space,
                                     UserAddressSpaceStatus::RollbackFailed);
        }
        if (!area.permissions.writable) {
            continue;
        }
        for (uint64_t page_address = area.begin_address; page_address < area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            PageMapping mapping{};
            const PageTableStatus query_status = QueryAddressSpacePage(
                parent_address_space.root_physical_address, page_address, mapping);
            if (query_status == PageTableStatus::NotMapped) {
                continue;
            }
            if (query_status != PageTableStatus::Succeeded) {
                return RollbackForkClone(parent_address_space, child_address_space,
                                         UserAddressSpaceStatus::RollbackFailed);
            }
            if (!mapping.permissions.copy_on_write) {
                if (ReplaceUserPage(parent_address_space.root_physical_address, page_address,
                                    mapping.physical_address, false, mapping.permissions.executable,
                                    true) != KernelUserPageStatus::Succeeded) {
                    return RollbackForkClone(parent_address_space, child_address_space,
                                             UserAddressSpaceStatus::RollbackFailed);
                }
                ++parent_address_space.copy_on_write_page_count;
            }
            ++copy_on_write_page_count;
        }
    }

    child_address_space.entry_virtual_address = parent_address_space.entry_virtual_address;
    child_address_space.stack_top_virtual_address = parent_address_space.stack_top_virtual_address;
    child_address_space.stack_committed_bottom_virtual_address =
        parent_address_space.stack_committed_bottom_virtual_address;
    child_address_space.program_break_base_address =
        parent_address_space.program_break_base_address;
    child_address_space.program_break_address = parent_address_space.program_break_address;
    child_address_space.program_break_limit_address =
        parent_address_space.program_break_limit_address;
    if (child_address_space.mapped_page_count != parent_address_space.mapped_page_count ||
        child_address_space.private_file_resident_page_count !=
            parent_address_space.private_file_resident_page_count ||
        child_address_space.shared_file_resident_page_count !=
            parent_address_space.shared_file_resident_page_count ||
        child_address_space.copy_on_write_page_count != copy_on_write_page_count ||
        parent_address_space.copy_on_write_page_count != copy_on_write_page_count) {
        return RollbackForkClone(parent_address_space, child_address_space,
                                 UserAddressSpaceStatus::RollbackFailed);
    }
    if (!AssignAddressSpaceIdentifier(child_address_space)) {
        return RollbackForkClone(parent_address_space, child_address_space,
                                 UserAddressSpaceStatus::AddressSpaceIdentifierExhausted);
    }
    ++parent_address_space.fork_clone_count;
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus
RestoreUserAddressSpaceAfterFailedFork(UserAddressSpace &parent_address_space) noexcept {
    if (parent_address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    if (!RestoreExclusiveForkPages(parent_address_space)) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    if (parent_address_space.fork_clone_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    --parent_address_space.fork_clone_count;
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus DestroyUserAddressSpace(UserAddressSpace &address_space) noexcept {
    if (active_user_address_space == &address_space) {
        active_user_address_space = nullptr;
    }
    const VirtualMemoryAreaStatus validation_status = address_space.virtual_memory_map.Validate();
    if (validation_status != VirtualMemoryAreaStatus::Succeeded &&
        validation_status != VirtualMemoryAreaStatus::NotInitialized) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    if (validation_status == VirtualMemoryAreaStatus::Succeeded) {
        const uint64_t area_count = address_space.virtual_memory_map.AreaCount();
        for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
             ++area_index) {
            VirtualMemoryArea area{};
            if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
                    VirtualMemoryAreaStatus::Succeeded ||
                ReleaseMappedPages(address_space, area.begin_address, area.end_address, false) !=
                    UserVirtualMemoryStatus::Succeeded) {
                return UserAddressSpaceStatus::RollbackFailed;
            }
        }
        if (address_space.root_physical_address != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
            DestroyUserPageTable(address_space.root_physical_address) !=
                KernelUserPageStatus::Succeeded) {
            return UserAddressSpaceStatus::RollbackFailed;
        }
        for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
             ++area_index) {
            VirtualMemoryArea area{};
            if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
                VirtualMemoryAreaStatus::Succeeded) {
                return UserAddressSpaceStatus::RollbackFailed;
            }
            if (!IsFileBackedVirtualMemoryAreaKind(area.kind)) {
                continue;
            }
            bool already_released = false;
            for (uint64_t previous_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
                 previous_index < area_index; ++previous_index) {
                VirtualMemoryArea previous_area{};
                if (address_space.virtual_memory_map.ReadAt(previous_index, previous_area) !=
                    VirtualMemoryAreaStatus::Succeeded) {
                    return UserAddressSpaceStatus::RollbackFailed;
                }
                if (previous_area.backing_descriptor_index == area.backing_descriptor_index &&
                    previous_area.backing_generation == area.backing_generation) {
                    already_released = true;
                    break;
                }
            }
            if (!already_released &&
                user_file_backing_manager.Release(
                    address_space.root_physical_address, area.backing_descriptor_index,
                    area.backing_generation) != UserFileBackingStatus::Succeeded) {
                return UserAddressSpaceStatus::RollbackFailed;
            }
        }
        if (address_space.virtual_memory_map.Destroy() != VirtualMemoryAreaStatus::Succeeded) {
            return UserAddressSpaceStatus::RollbackFailed;
        }
    } else if (address_space.root_physical_address != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
               DestroyUserPageTable(address_space.root_physical_address) !=
                   KernelUserPageStatus::Succeeded) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    const FilePageCacheStatus trim_status =
        user_file_page_cache.Trim(OS_KERNEL_USER_MEMORY_EMPTY_VALUE);
    if (trim_status != FilePageCacheStatus::Succeeded &&
        trim_status != FilePageCacheStatus::EntryBusy) {
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

UserAddressSpaceStatus PrepareUserStackRange(UserAddressSpace &address_space,
                                             const uint64_t lowest_required_address,
                                             const uint64_t current_stack_pointer) noexcept {
    if (lowest_required_address < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
        lowest_required_address >= OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS ||
        current_stack_pointer <= lowest_required_address ||
        current_stack_pointer > OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return UserAddressSpaceStatus::StackPreparationFailed;
    }
    if (address_space.stack_committed_bottom_virtual_address ==
        OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return PrepareUserStack(address_space, lowest_required_address);
    }
    while (lowest_required_address < address_space.stack_committed_bottom_virtual_address) {
        if (address_space.stack_committed_bottom_virtual_address <
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            return UserAddressSpaceStatus::StackPreparationFailed;
        }
        const uint64_t next_page_address =
            address_space.stack_committed_bottom_virtual_address - OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        const uint64_t synthetic_write_fault =
            OS_KERNEL_USER_MEMORY_PAGE_FAULT_WRITE_BIT | OS_KERNEL_USER_MEMORY_PAGE_FAULT_USER_BIT;
        if (HandleUserPageFault(address_space, next_page_address, synthetic_write_fault,
                                current_stack_pointer) != UserPageFaultStatus::Handled) {
            return UserAddressSpaceStatus::StackPreparationFailed;
        }
    }
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
        (map_flags & ~os::abi::OS_ABI_ANONYMOUS_MEMORY_MAP_VALID_FLAG_MASK) !=
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

UserVirtualMemoryStatus MapFileMemory(UserAddressSpace &address_space, fs::Vfs &vfs,
                                      const fs::OpenFile &open_file,
                                      const uint64_t requested_address, const uint64_t length_bytes,
                                      const uint64_t protection_flags, const uint64_t map_flags,
                                      const uint64_t file_offset_bytes,
                                      uint64_t &mapped_address) noexcept {
    mapped_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    const bool private_mapping =
        (map_flags & os::abi::OS_ABI_MEMORY_MAP_PRIVATE) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool shared_mapping =
        (map_flags & os::abi::OS_ABI_MEMORY_MAP_SHARED) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool writable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_WRITE) !=
                          OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        (file_offset_bytes & OS_KERNEL_USER_MEMORY_PAGE_MASK) !=
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    if (!ProtectionFlagsAreValid(protection_flags) ||
        (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_READ) ==
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    if ((map_flags & ~os::abi::OS_ABI_FILE_MEMORY_MAP_VALID_FLAG_MASK) !=
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        private_mapping == shared_mapping || (shared_mapping && writable)) {
        return UserVirtualMemoryStatus::UnsupportedMapping;
    }
    fs::NodeInformation information{};
    if (!open_file.open || !open_file.readable ||
        vfs.StatOpenFile(open_file, information) != fs::Status::Succeeded ||
        information.type != fs::NodeType::RegularFile) {
        return UserVirtualMemoryStatus::InvalidFile;
    }
    if (file_offset_bytes > information.size_bytes ||
        length_bytes > information.size_bytes - file_offset_bytes) {
        return UserVirtualMemoryStatus::InvalidRange;
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

    uint64_t backing_descriptor_index = UINT64_MAX;
    uint64_t backing_generation = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (user_file_backing_manager.AcquireVfsFile(
            address_space.root_physical_address, vfs, open_file, backing_descriptor_index,
            backing_generation) != UserFileBackingStatus::Succeeded) {
        return UserVirtualMemoryStatus::MetadataExhausted;
    }
    const VirtualMemoryAreaStatus insert_status =
        address_space.virtual_memory_map.Insert(VirtualMemoryArea{
            .begin_address = area_begin_address,
            .end_address = area_begin_address + aligned_length_bytes,
            .permissions = DecodeProtectionFlags(protection_flags),
            .kind = private_mapping ? VirtualMemoryAreaKind::FilePrivate
                                    : VirtualMemoryAreaKind::FileShared,
            .backing_descriptor_index = backing_descriptor_index,
            .backing_generation = backing_generation,
            .backing_file_offset_bytes = file_offset_bytes,
            .backing_data_length_bytes = length_bytes,
        });
    if (insert_status != VirtualMemoryAreaStatus::Succeeded) {
        static_cast<void>(user_file_backing_manager.Release(
            address_space.root_physical_address, backing_descriptor_index, backing_generation));
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

UserVirtualMemoryStatus UnmapFileMemory(UserAddressSpace &address_space, const uint64_t address,
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
    uint64_t validation_address = address;
    while (validation_address < end_address) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.FindContaining(validation_address, area) !=
                VirtualMemoryAreaStatus::Succeeded ||
            (area.kind != VirtualMemoryAreaKind::FilePrivate &&
             area.kind != VirtualMemoryAreaKind::FileShared)) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
        validation_address = Minimum(end_address, area.end_address);
    }

    uint64_t current_address = address;
    while (current_address < end_address) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.FindContaining(current_address, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        const uint64_t current_end_address = Minimum(end_address, area.end_address);
        const UserVirtualMemoryStatus release_status =
            ReleaseMappedPages(address_space, current_address, current_end_address, true);
        if (release_status != UserVirtualMemoryStatus::Succeeded) {
            return release_status;
        }
        const uint64_t backing_descriptor_index = area.backing_descriptor_index;
        const uint64_t backing_generation = area.backing_generation;
        const VirtualMemoryAreaStatus remove_status = address_space.virtual_memory_map.Remove(
            current_address, current_end_address, area.kind);
        if (remove_status != VirtualMemoryAreaStatus::Succeeded) {
            return MapVirtualMemoryAreaStatus(remove_status);
        }
        const UserVirtualMemoryStatus backing_status =
            ReleaseBackingIfUnused(address_space, backing_descriptor_index, backing_generation);
        if (backing_status != UserVirtualMemoryStatus::Succeeded) {
            return backing_status;
        }
        current_address = current_end_address;
    }
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus RevokeUserFileMappings(UserAddressSpace &address_space,
                                               const FileIdentity &identity) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         area_index < address_space.virtual_memory_map.AreaCount(); ++area_index) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (!IsFileBackedVirtualMemoryAreaKind(area.kind) || area.permissions.writable) {
            continue;
        }
        UserFileBackingDescriptor descriptor{};
        if (!ReadBackingDescriptor(area, descriptor)) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (!FileIdentitiesEqual(descriptor.identity, identity)) {
            continue;
        }
        const UserVirtualMemoryStatus release_status =
            ReleaseMappedPages(address_space, area.begin_address, area.end_address, false);
        if (release_status != UserVirtualMemoryStatus::Succeeded) {
            return release_status;
        }
    }
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus
InvalidateUserFilePageCache(const FileIdentity &identity,
                            const uint64_t current_file_size_bytes) noexcept {
    if (user_file_backing_manager.UpdateFileSize(identity, current_file_size_bytes) !=
        UserFileBackingStatus::Succeeded) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    const FilePageCacheStatus cache_status = user_file_page_cache.Invalidate(identity);
    if (cache_status == FilePageCacheStatus::Succeeded) {
        return UserVirtualMemoryStatus::Succeeded;
    }
    return cache_status == FilePageCacheStatus::EntryBusy
               ? UserVirtualMemoryStatus::PageReleaseFailed
               : UserVirtualMemoryStatus::Corrupt;
}

UserVirtualMemoryStatus TrimUserFilePageCache() noexcept {
    const FilePageCacheStatus trim_status =
        user_file_page_cache.Trim(OS_KERNEL_USER_MEMORY_EMPTY_VALUE);
    return trim_status == FilePageCacheStatus::Succeeded ? UserVirtualMemoryStatus::Succeeded
           : trim_status == FilePageCacheStatus::EntryBusy
               ? UserVirtualMemoryStatus::PageReleaseFailed
               : UserVirtualMemoryStatus::Corrupt;
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
        .file_private_page_count =
            CountKindPages(address_space, VirtualMemoryAreaKind::FilePrivate),
        .file_shared_page_count = CountKindPages(address_space, VirtualMemoryAreaKind::FileShared),
        .file_page_fault_count = address_space.file_page_fault_count,
        .page_cache_hit_count = address_space.page_cache_hit_count,
        .private_file_resident_page_count = address_space.private_file_resident_page_count,
        .shared_file_resident_page_count = address_space.shared_file_resident_page_count,
        .copy_on_write_page_count = address_space.copy_on_write_page_count,
        .copy_on_write_fault_count = address_space.copy_on_write_fault_count,
        .copy_on_write_copy_count = address_space.copy_on_write_copy_count,
        .copy_on_write_exclusive_restore_count =
            address_space.copy_on_write_exclusive_restore_count,
        .fork_clone_count = address_space.fork_clone_count,
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
    const bool write_access = (error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_WRITE_BIT) !=
                              OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool instruction_access =
        (error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_INSTRUCTION_BIT) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const uint64_t page_address = AlignDownToPage(fault_address);
    if ((error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_PRESENT_BIT) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        PageMapping mapping{};
        if (!write_access ||
            QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping) !=
                PageTableStatus::Succeeded ||
            !mapping.permissions.copy_on_write) {
            return UserPageFaultStatus::PresentPageViolation;
        }
        const UserVirtualMemoryStatus break_status =
            BreakCopyOnWritePage(address_space, page_address);
        if (break_status == UserVirtualMemoryStatus::Succeeded) {
            return UserPageFaultStatus::Handled;
        }
        return break_status == UserVirtualMemoryStatus::PageAllocationFailed
                   ? UserPageFaultStatus::PageAllocationFailed
                   : UserPageFaultStatus::CopyOnWriteFailure;
    }
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
    }

    const UserVirtualMemoryStatus map_status = MapDemandPage(address_space, page_address, area);
    if (map_status == UserVirtualMemoryStatus::PageAllocationFailed) {
        return UserPageFaultStatus::PageAllocationFailed;
    }
    if (map_status == UserVirtualMemoryStatus::FileReadFailed) {
        return UserPageFaultStatus::FileReadFailed;
    }
    if (map_status == UserVirtualMemoryStatus::PageCacheExhausted) {
        return UserPageFaultStatus::PageCacheExhausted;
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

UserVirtualMemoryStatus ResolveUserReturnMemory(UserAddressSpace &address_space,
                                                const uint64_t instruction_pointer,
                                                const uint64_t stack_pointer) noexcept {
    const uint64_t stack_probe_address =
        stack_pointer == OS_KERNEL_USER_MEMORY_EMPTY_VALUE
            ? OS_KERNEL_USER_MEMORY_EMPTY_VALUE
            : stack_pointer - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
    if (!IsUserProgramVirtualAddressRange(instruction_pointer,
                                          OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) ||
        !IsUserVirtualAddressRange(stack_probe_address, OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT)) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    const uint64_t instruction_page = AlignDownToPage(instruction_pointer);
    const UserVirtualMemoryStatus resolve_status =
        ResolveNonStackPage(address_space, instruction_page, false, true);
    if (resolve_status != UserVirtualMemoryStatus::Succeeded) {
        return resolve_status;
    }
    PageMapping instruction_mapping{};
    PageMapping stack_mapping{};
    const uint64_t stack_page = AlignDownToPage(stack_probe_address);
    if (QueryAddressSpacePage(address_space.root_physical_address, stack_page, stack_mapping) ==
            PageTableStatus::Succeeded &&
        stack_mapping.permissions.copy_on_write &&
        BreakCopyOnWritePage(address_space, stack_page) != UserVirtualMemoryStatus::Succeeded) {
        return UserVirtualMemoryStatus::CopyOnWriteFailure;
    }
    if (QueryAddressSpacePage(address_space.root_physical_address, instruction_pointer,
                              instruction_mapping) != PageTableStatus::Succeeded ||
        !instruction_mapping.permissions.user_accessible ||
        !instruction_mapping.permissions.executable || instruction_mapping.permissions.writable ||
        QueryAddressSpacePage(address_space.root_physical_address, stack_probe_address,
                              stack_mapping) != PageTableStatus::Succeeded ||
        !stack_mapping.permissions.user_accessible || !stack_mapping.permissions.writable ||
        stack_mapping.permissions.executable) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    return UserVirtualMemoryStatus::Succeeded;
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
            if (!mapping.permissions.copy_on_write ||
                BreakCopyOnWritePage(address_space, page_virtual_address) !=
                    UserVirtualMemoryStatus::Succeeded ||
                QueryAddressSpacePage(address_space.root_physical_address, page_virtual_address,
                                      mapping) != PageTableStatus::Succeeded ||
                !mapping.permissions.writable) {
                return UserMemoryCopyStatus::PageNotWritable;
            }
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
