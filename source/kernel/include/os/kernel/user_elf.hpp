#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_USER_MINIMUM_VIRTUAL_ADDRESS = 0x0000000000010000ULL;
inline constexpr uint64_t OS_KERNEL_USER_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE = 0x0000800000000000ULL;
inline constexpr uint64_t OS_KERNEL_USER_ELF_MAXIMUM_LOAD_SEGMENT_COUNT = 8ULL;
inline constexpr uint64_t OS_KERNEL_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT = 32ULL;

struct UserElfLoadSegment final {
    uint64_t fileOffset;
    uint64_t virtualAddress;
    uint64_t fileSizeBytes;
    uint64_t memorySizeBytes;
    bool writable;
    bool executable;
};

struct UserElfLayout final {
    uint64_t entryVirtualAddress;
    uint64_t loadSegmentCount;
    UserElfLoadSegment loadSegments[OS_KERNEL_USER_ELF_MAXIMUM_LOAD_SEGMENT_COUNT];
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
};

[[nodiscard]] bool IsUserVirtualAddressRange(uint64_t beginAddress, uint64_t lengthBytes) noexcept;
[[nodiscard]] UserElfValidationStatus ValidateUserElf(const uint8_t *image, uint64_t imageSizeBytes,
                                                      UserElfLayout &layout) noexcept;

}
