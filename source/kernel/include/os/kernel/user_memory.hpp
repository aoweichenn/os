#pragma once

#include "os/kernel/physical_frame_allocator.hpp"
#include "os/kernel/user_elf.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS = 0x00007FFFFFFF0000ULL;
inline constexpr uint64_t OS_KERNEL_USER_STACK_PAGE_COUNT = 4ULL;
inline constexpr uint64_t OS_KERNEL_USER_STACK_SIZE_BYTES =
    OS_KERNEL_USER_STACK_PAGE_COUNT * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS =
    OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS - OS_KERNEL_USER_STACK_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_USER_STACK_GUARD_VIRTUAL_ADDRESS =
    OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS - OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;

struct UserAddressSpace final {
    uint64_t root_physical_address;
    uint64_t entry_virtual_address;
    uint64_t stack_top_virtual_address;
    uint64_t mapped_page_count;
};

enum class UserAddressSpaceStatus : uint64_t {
    Succeeded,
    InvalidElf,
    StackCollision,
    PageTableCreationFailed,
    PageAllocationFailed,
    PageMappingFailed,
    RollbackFailed,
};

enum class UserMemoryCopyStatus : uint64_t {
    Succeeded,
    NullDestination,
    NullSource,
    DestinationTooSmall,
    SourceTooSmall,
    InvalidUserRange,
    PageNotMapped,
    PageNotUserAccessible,
    PageNotWritable,
};

[[nodiscard]] UserAddressSpaceStatus
LoadUserAddressSpace(const uint8_t *image, uint64_t image_size_bytes,
                     UserAddressSpace &address_space,
                     UserElfValidationStatus &elf_validation_status) noexcept;
[[nodiscard]] UserAddressSpaceStatus
DestroyUserAddressSpace(UserAddressSpace &address_space) noexcept;
[[nodiscard]] UserMemoryCopyStatus CopyFromUser(uint64_t user_address, uint64_t length_bytes,
                                                uint8_t *destination,
                                                uint64_t destination_capacity_bytes) noexcept;
[[nodiscard]] UserMemoryCopyStatus ValidateUserWritableMemory(uint64_t user_address,
                                                              uint64_t length_bytes) noexcept;
[[nodiscard]] UserMemoryCopyStatus CopyToUser(uint64_t user_address, uint64_t length_bytes,
                                              const uint8_t *source,
                                              uint64_t source_size_bytes) noexcept;
}
