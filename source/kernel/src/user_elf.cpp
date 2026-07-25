#include "os/kernel/user_elf.hpp"

#include "os/kernel/physical_frame_allocator.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_ELF_HEADER_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_MAGIC0_OFFSET = 0ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_MAGIC1_OFFSET = 1ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_MAGIC2_OFFSET = 2ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_MAGIC3_OFFSET = 3ULL;
constexpr uint8_t OS_KERNEL_USER_ELF_MAGIC0 = 0x7FU;
constexpr uint8_t OS_KERNEL_USER_ELF_MAGIC1 = 0x45U;
constexpr uint8_t OS_KERNEL_USER_ELF_MAGIC2 = 0x4CU;
constexpr uint8_t OS_KERNEL_USER_ELF_MAGIC3 = 0x46U;
constexpr uint8_t OS_KERNEL_USER_ELF_CLASS_64 = 0x02U;
constexpr uint8_t OS_KERNEL_USER_ELF_LITTLE_ENDIAN = 0x01U;
constexpr uint8_t OS_KERNEL_USER_ELF_IDENTIFICATION_VERSION = 0x01U;
constexpr uint16_t OS_KERNEL_USER_ELF_EXECUTABLE_TYPE = 0x0002U;
constexpr uint16_t OS_KERNEL_USER_ELF_X86_64_MACHINE = 0x003EU;
constexpr uint32_t OS_KERNEL_USER_ELF_CURRENT_VERSION = 0x00000001U;
constexpr uint32_t OS_KERNEL_USER_ELF_LOAD_PROGRAM_TYPE = 0x00000001U;
constexpr uint32_t OS_KERNEL_USER_ELF_EXECUTE_FLAG = 0x00000001U;
constexpr uint32_t OS_KERNEL_USER_ELF_WRITE_FLAG = 0x00000002U;
constexpr uint32_t OS_KERNEL_USER_ELF_READ_FLAG = 0x00000004U;
constexpr uint32_t OS_KERNEL_USER_ELF_KNOWN_FLAG_MASK =
    OS_KERNEL_USER_ELF_EXECUTE_FLAG | OS_KERNEL_USER_ELF_WRITE_FLAG | OS_KERNEL_USER_ELF_READ_FLAG;
constexpr uint64_t OS_KERNEL_USER_ELF_IDENTIFICATION_CLASS_OFFSET = 4ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_IDENTIFICATION_ENDIAN_OFFSET = 5ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_IDENTIFICATION_VERSION_OFFSET = 6ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_TYPE_OFFSET = 16ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_MACHINE_OFFSET = 18ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_VERSION_OFFSET = 20ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_ENTRY_OFFSET = 24ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_HEADER_OFFSET_OFFSET = 32ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_HEADER_SIZE_OFFSET = 52ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_OFFSET = 54ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_HEADER_COUNT_OFFSET = 56ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_TYPE_OFFSET = 0ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_FLAGS_OFFSET = 4ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_FILE_OFFSET_OFFSET = 8ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET = 16ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET = 24ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_FILE_SIZE_OFFSET = 32ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_MEMORY_SIZE_OFFSET = 40ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PROGRAM_ALIGNMENT_OFFSET = 48ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PAGE_MASK = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_PAGE_ROUNDING = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - 1ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_BYTE0_OFFSET = 0ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_BYTE1_OFFSET = 1ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_BYTE2_OFFSET = 2ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_BYTE3_OFFSET = 3ULL;
constexpr uint64_t OS_KERNEL_USER_ELF_LOW_WORD_SIZE_BYTES = 4ULL;
constexpr uint32_t OS_KERNEL_USER_ELF_BYTE1_SHIFT_BITS = 8U;
constexpr uint32_t OS_KERNEL_USER_ELF_BYTE2_SHIFT_BITS = 16U;
constexpr uint32_t OS_KERNEL_USER_ELF_BYTE3_SHIFT_BITS = 24U;
constexpr uint64_t OS_KERNEL_USER_ELF_HIGH_WORD_SHIFT_BITS = 32ULL;

[[nodiscard]] uint16_t ReadLittleEndian16(const uint8_t *bytes) noexcept {
    return static_cast<uint16_t>(bytes[OS_KERNEL_USER_ELF_BYTE0_OFFSET]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[OS_KERNEL_USER_ELF_BYTE1_OFFSET])
                                 << OS_KERNEL_USER_ELF_BYTE1_SHIFT_BITS);
}

[[nodiscard]] uint32_t ReadLittleEndian32(const uint8_t *bytes) noexcept {
    return static_cast<uint32_t>(bytes[OS_KERNEL_USER_ELF_BYTE0_OFFSET]) |
           (static_cast<uint32_t>(bytes[OS_KERNEL_USER_ELF_BYTE1_OFFSET])
            << OS_KERNEL_USER_ELF_BYTE1_SHIFT_BITS) |
           (static_cast<uint32_t>(bytes[OS_KERNEL_USER_ELF_BYTE2_OFFSET])
            << OS_KERNEL_USER_ELF_BYTE2_SHIFT_BITS) |
           (static_cast<uint32_t>(bytes[OS_KERNEL_USER_ELF_BYTE3_OFFSET])
            << OS_KERNEL_USER_ELF_BYTE3_SHIFT_BITS);
}

[[nodiscard]] uint64_t ReadLittleEndian64(const uint8_t *bytes) noexcept {
    return static_cast<uint64_t>(ReadLittleEndian32(bytes)) |
           (static_cast<uint64_t>(
                ReadLittleEndian32(bytes + OS_KERNEL_USER_ELF_LOW_WORD_SIZE_BYTES))
            << OS_KERNEL_USER_ELF_HIGH_WORD_SHIFT_BITS);
}

[[nodiscard]] bool CheckedRangeEnd(const uint64_t beginAddress, const uint64_t lengthBytes,
                                   uint64_t &endAddress) noexcept {
    if (beginAddress > UINT64_MAX - lengthBytes) {
        return false;
    }
    endAddress = beginAddress + lengthBytes;
    return true;
}

[[nodiscard]] bool SegmentRangesOverlap(const UserElfLoadSegment &left,
                                        const UserElfLoadSegment &right) noexcept {
    const uint64_t leftEnd = left.virtualAddress + left.memorySizeBytes;
    const uint64_t rightEnd = right.virtualAddress + right.memorySizeBytes;
    return left.virtualAddress < rightEnd && right.virtualAddress < leftEnd;
}

[[nodiscard]] bool EntryBelongsToExecutableSegment(const uint64_t entryVirtualAddress,
                                                   const UserElfLayout &layout) noexcept {
    for (uint64_t segmentIndex = 0ULL; segmentIndex < layout.loadSegmentCount; ++segmentIndex) {
        const UserElfLoadSegment &segment = layout.loadSegments[segmentIndex];
        if (segment.executable && entryVirtualAddress >= segment.virtualAddress &&
            entryVirtualAddress < segment.virtualAddress + segment.memorySizeBytes) {
            return true;
        }
    }
    return false;
}

}

bool IsUserVirtualAddressRange(const uint64_t beginAddress, const uint64_t lengthBytes) noexcept {
    if (lengthBytes == OS_KERNEL_USER_ELF_EMPTY_VALUE ||
        beginAddress < OS_KERNEL_USER_MINIMUM_VIRTUAL_ADDRESS) {
        return false;
    }
    uint64_t endAddress = 0ULL;
    return CheckedRangeEnd(beginAddress, lengthBytes, endAddress) &&
           endAddress <= OS_KERNEL_USER_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE;
}

bool IsUserProgramVirtualAddressRange(const uint64_t beginAddress,
                                      const uint64_t lengthBytes) noexcept {
    if (lengthBytes == OS_KERNEL_USER_ELF_EMPTY_VALUE ||
        beginAddress < OS_KERNEL_USER_PROGRAM_MINIMUM_VIRTUAL_ADDRESS) {
        return false;
    }
    uint64_t endAddress = 0ULL;
    return CheckedRangeEnd(beginAddress, lengthBytes, endAddress) &&
           endAddress <= OS_KERNEL_USER_PROGRAM_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE;
}

UserElfValidationStatus ValidateUserElf(const uint8_t *image, const uint64_t imageSizeBytes,
                                        UserElfLayout &layout) noexcept {
    if (image == nullptr) {
        return UserElfValidationStatus::NullImage;
    }
    if (imageSizeBytes < OS_KERNEL_USER_ELF_HEADER_SIZE_BYTES) {
        return UserElfValidationStatus::HeaderTruncated;
    }
    if (image[OS_KERNEL_USER_ELF_MAGIC0_OFFSET] != OS_KERNEL_USER_ELF_MAGIC0 ||
        image[OS_KERNEL_USER_ELF_MAGIC1_OFFSET] != OS_KERNEL_USER_ELF_MAGIC1 ||
        image[OS_KERNEL_USER_ELF_MAGIC2_OFFSET] != OS_KERNEL_USER_ELF_MAGIC2 ||
        image[OS_KERNEL_USER_ELF_MAGIC3_OFFSET] != OS_KERNEL_USER_ELF_MAGIC3 ||
        image[OS_KERNEL_USER_ELF_IDENTIFICATION_CLASS_OFFSET] != OS_KERNEL_USER_ELF_CLASS_64 ||
        image[OS_KERNEL_USER_ELF_IDENTIFICATION_ENDIAN_OFFSET] !=
            OS_KERNEL_USER_ELF_LITTLE_ENDIAN ||
        image[OS_KERNEL_USER_ELF_IDENTIFICATION_VERSION_OFFSET] !=
            OS_KERNEL_USER_ELF_IDENTIFICATION_VERSION) {
        return UserElfValidationStatus::InvalidIdentification;
    }
    if (ReadLittleEndian16(image + OS_KERNEL_USER_ELF_TYPE_OFFSET) !=
        OS_KERNEL_USER_ELF_EXECUTABLE_TYPE) {
        return UserElfValidationStatus::InvalidExecutableType;
    }
    if (ReadLittleEndian16(image + OS_KERNEL_USER_ELF_MACHINE_OFFSET) !=
        OS_KERNEL_USER_ELF_X86_64_MACHINE) {
        return UserElfValidationStatus::InvalidMachine;
    }
    if (ReadLittleEndian32(image + OS_KERNEL_USER_ELF_VERSION_OFFSET) !=
        OS_KERNEL_USER_ELF_CURRENT_VERSION) {
        return UserElfValidationStatus::InvalidVersion;
    }
    if (ReadLittleEndian16(image + OS_KERNEL_USER_ELF_HEADER_SIZE_OFFSET) !=
        OS_KERNEL_USER_ELF_HEADER_SIZE_BYTES) {
        return UserElfValidationStatus::InvalidHeaderSize;
    }
    if (ReadLittleEndian16(image + OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_OFFSET) !=
        OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_BYTES) {
        return UserElfValidationStatus::InvalidProgramHeaderSize;
    }

    const uint64_t programHeaderCount =
        ReadLittleEndian16(image + OS_KERNEL_USER_ELF_PROGRAM_HEADER_COUNT_OFFSET);
    if (programHeaderCount == OS_KERNEL_USER_ELF_EMPTY_VALUE ||
        programHeaderCount > OS_KERNEL_USER_ELF_MAXIMUM_LOAD_SEGMENT_COUNT) {
        return UserElfValidationStatus::InvalidProgramHeaderCount;
    }
    const uint64_t programHeaderOffset =
        ReadLittleEndian64(image + OS_KERNEL_USER_ELF_PROGRAM_HEADER_OFFSET_OFFSET);
    if (programHeaderCount > UINT64_MAX / OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_BYTES) {
        return UserElfValidationStatus::ProgramHeaderTableOutOfRange;
    }
    const uint64_t programHeaderTableSizeBytes =
        programHeaderCount * OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_BYTES;
    uint64_t programHeaderTableEnd = 0ULL;
    if (!CheckedRangeEnd(programHeaderOffset, programHeaderTableSizeBytes, programHeaderTableEnd) ||
        programHeaderOffset < OS_KERNEL_USER_ELF_HEADER_SIZE_BYTES ||
        programHeaderTableEnd > imageSizeBytes) {
        return UserElfValidationStatus::ProgramHeaderTableOutOfRange;
    }

    UserElfLayout candidateLayout{};
    candidateLayout.entryVirtualAddress =
        ReadLittleEndian64(image + OS_KERNEL_USER_ELF_ENTRY_OFFSET);
    uint64_t mappedPageCount = 0ULL;
    for (uint64_t programIndex = 0ULL; programIndex < programHeaderCount; ++programIndex) {
        const uint8_t *const programHeader =
            image + programHeaderOffset +
            programIndex * OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_BYTES;
        if (ReadLittleEndian32(programHeader + OS_KERNEL_USER_ELF_PROGRAM_TYPE_OFFSET) !=
            OS_KERNEL_USER_ELF_LOAD_PROGRAM_TYPE) {
            return UserElfValidationStatus::UnsupportedProgramHeader;
        }
        const uint32_t flags =
            ReadLittleEndian32(programHeader + OS_KERNEL_USER_ELF_PROGRAM_FLAGS_OFFSET);
        if ((flags & ~OS_KERNEL_USER_ELF_KNOWN_FLAG_MASK) != 0U ||
            (flags & OS_KERNEL_USER_ELF_READ_FLAG) == 0U ||
            ((flags & OS_KERNEL_USER_ELF_WRITE_FLAG) != 0U &&
             (flags & OS_KERNEL_USER_ELF_EXECUTE_FLAG) != 0U)) {
            return UserElfValidationStatus::InvalidSegmentFlags;
        }

        UserElfLoadSegment segment{
            .fileOffset =
                ReadLittleEndian64(programHeader + OS_KERNEL_USER_ELF_PROGRAM_FILE_OFFSET_OFFSET),
            .virtualAddress = ReadLittleEndian64(programHeader +
                                                 OS_KERNEL_USER_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET),
            .fileSizeBytes =
                ReadLittleEndian64(programHeader + OS_KERNEL_USER_ELF_PROGRAM_FILE_SIZE_OFFSET),
            .memorySizeBytes =
                ReadLittleEndian64(programHeader + OS_KERNEL_USER_ELF_PROGRAM_MEMORY_SIZE_OFFSET),
            .writable = (flags & OS_KERNEL_USER_ELF_WRITE_FLAG) != 0U,
            .executable = (flags & OS_KERNEL_USER_ELF_EXECUTE_FLAG) != 0U,
        };
        const uint64_t physicalAddress =
            ReadLittleEndian64(programHeader + OS_KERNEL_USER_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET);
        const uint64_t alignment =
            ReadLittleEndian64(programHeader + OS_KERNEL_USER_ELF_PROGRAM_ALIGNMENT_OFFSET);
        if (alignment != OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
            (segment.fileOffset & OS_KERNEL_USER_ELF_PAGE_MASK) != 0ULL ||
            (segment.virtualAddress & OS_KERNEL_USER_ELF_PAGE_MASK) != 0ULL ||
            physicalAddress != segment.virtualAddress) {
            return UserElfValidationStatus::InvalidSegmentAlignment;
        }
        if (segment.memorySizeBytes == OS_KERNEL_USER_ELF_EMPTY_VALUE ||
            segment.fileSizeBytes > segment.memorySizeBytes) {
            return UserElfValidationStatus::InvalidSegmentMemoryRange;
        }
        uint64_t fileEnd = 0ULL;
        if (!CheckedRangeEnd(segment.fileOffset, segment.fileSizeBytes, fileEnd) ||
            fileEnd > imageSizeBytes) {
            return UserElfValidationStatus::InvalidSegmentFileRange;
        }
        if (!IsUserProgramVirtualAddressRange(segment.virtualAddress, segment.memorySizeBytes)) {
            return UserElfValidationStatus::InvalidSegmentMemoryRange;
        }
        for (uint64_t existingIndex = 0ULL; existingIndex < candidateLayout.loadSegmentCount;
             ++existingIndex) {
            if (SegmentRangesOverlap(segment, candidateLayout.loadSegments[existingIndex])) {
                return UserElfValidationStatus::OverlappingSegments;
            }
        }
        const uint64_t segmentPageCount =
            (segment.memorySizeBytes + OS_KERNEL_USER_ELF_PAGE_ROUNDING) /
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (segmentPageCount > OS_KERNEL_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT ||
            mappedPageCount > OS_KERNEL_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT - segmentPageCount) {
            return UserElfValidationStatus::TooManyMappedPages;
        }
        mappedPageCount += segmentPageCount;
        candidateLayout.loadSegments[candidateLayout.loadSegmentCount] = segment;
        ++candidateLayout.loadSegmentCount;
    }
    if (!EntryBelongsToExecutableSegment(candidateLayout.entryVirtualAddress, candidateLayout)) {
        return UserElfValidationStatus::EntryNotExecutable;
    }
    layout = candidateLayout;
    return UserElfValidationStatus::Succeeded;
}

}
