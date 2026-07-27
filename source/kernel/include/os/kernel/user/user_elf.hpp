#pragma once

#include "os/kernel/memory/physical_frame_allocator.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_USER_MINIMUM_VIRTUAL_ADDRESS = 0x0000000000010000ULL;
inline constexpr uint64_t OS_KERNEL_USER_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE = 0x0000800000000000ULL;
inline constexpr uint64_t OS_KERNEL_USER_PROGRAM_MINIMUM_VIRTUAL_ADDRESS = 0x0000000040000000ULL;
inline constexpr uint64_t OS_KERNEL_USER_PROGRAM_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE =
    0x0000000080000000ULL;
inline constexpr uint64_t OS_KERNEL_USER_ELF_MAXIMUM_LOAD_SEGMENT_COUNT = 8ULL;
inline constexpr uint64_t OS_KERNEL_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT =
    (OS_KERNEL_USER_PROGRAM_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE -
     OS_KERNEL_USER_PROGRAM_MINIMUM_VIRTUAL_ADDRESS) /
        OS_KERNEL_MEMORY_PAGE_SIZE_BYTES -
    1ULL;

using UserElfReadOperation = bool (*)(void *context, uint64_t offset_bytes, uint8_t *destination,
                                      uint64_t length_bytes) noexcept;

struct UserElfReader final {
    void *context;
    uint64_t image_size_bytes;
    UserElfReadOperation read;
};

struct UserElfLoadSegment final {
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t file_size_bytes;
    uint64_t memory_size_bytes;
    bool writable;
    bool executable;
};

struct UserElfLayout final {
    uint64_t entry_virtual_address;
    uint64_t load_segment_count;
    UserElfLoadSegment load_segments[OS_KERNEL_USER_ELF_MAXIMUM_LOAD_SEGMENT_COUNT];
};

enum class UserElfValidationStatus : uint64_t {
    Succeeded,
    NullImage,
    HeaderTruncated,
    InvalidIdentification,
    InvalidExecutableType,
    InvalidMachine,
    InvalidVersion,
    InvalidHeaderSize,
    InvalidProgramHeaderSize,
    InvalidProgramHeaderCount,
    ProgramHeaderTableOutOfRange,
    UnsupportedProgramHeader,
    InvalidSegmentFlags,
    InvalidSegmentAlignment,
    InvalidSegmentFileRange,
    InvalidSegmentMemoryRange,
    OverlappingSegments,
    TooManyMappedPages,
    EntryNotExecutable,
    NullReader,
    ReadFailed,
};

[[nodiscard]] bool IsUserVirtualAddressRange(uint64_t begin_address,
                                             uint64_t length_bytes) noexcept;
[[nodiscard]] bool IsUserProgramVirtualAddressRange(uint64_t begin_address,
                                                    uint64_t length_bytes) noexcept;
[[nodiscard]] UserElfValidationStatus
ValidateUserElf(const uint8_t *image, uint64_t image_size_bytes, UserElfLayout &layout) noexcept;
[[nodiscard]] UserElfValidationStatus ValidateUserElf(const UserElfReader &reader,
                                                      UserElfLayout &layout) noexcept;
}
