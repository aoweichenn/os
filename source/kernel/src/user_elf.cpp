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

[[nodiscard]] bool CheckedRangeEnd(const uint64_t begin_address, const uint64_t length_bytes,
                                   uint64_t &end_address) noexcept {
    if (begin_address > UINT64_MAX - length_bytes) {
        return false;
    }
    end_address = begin_address + length_bytes;
    return true;
}

[[nodiscard]] bool SegmentRangesOverlap(const UserElfLoadSegment &left,
                                        const UserElfLoadSegment &right) noexcept {
    const uint64_t left_end = left.virtual_address + left.memory_size_bytes;
    const uint64_t right_end = right.virtual_address + right.memory_size_bytes;
    return left.virtual_address < right_end && right.virtual_address < left_end;
}

[[nodiscard]] bool EntryBelongsToExecutableSegment(const uint64_t entry_virtual_address,
                                                   const UserElfLayout &layout) noexcept {
    for (uint64_t segment_index = 0ULL; segment_index < layout.load_segment_count;
         ++segment_index) {
        const UserElfLoadSegment &segment = layout.load_segments[segment_index];
        if (segment.executable && entry_virtual_address >= segment.virtual_address &&
            entry_virtual_address < segment.virtual_address + segment.memory_size_bytes) {
            return true;
        }
    }
    return false;
}
}

bool IsUserVirtualAddressRange(const uint64_t begin_address, const uint64_t length_bytes) noexcept {
    if (length_bytes == OS_KERNEL_USER_ELF_EMPTY_VALUE ||
        begin_address < OS_KERNEL_USER_MINIMUM_VIRTUAL_ADDRESS) {
        return false;
    }
    uint64_t end_address = 0ULL;
    return CheckedRangeEnd(begin_address, length_bytes, end_address) &&
           end_address <= OS_KERNEL_USER_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE;
}

bool IsUserProgramVirtualAddressRange(const uint64_t begin_address,
                                      const uint64_t length_bytes) noexcept {
    if (length_bytes == OS_KERNEL_USER_ELF_EMPTY_VALUE ||
        begin_address < OS_KERNEL_USER_PROGRAM_MINIMUM_VIRTUAL_ADDRESS) {
        return false;
    }
    uint64_t end_address = 0ULL;
    return CheckedRangeEnd(begin_address, length_bytes, end_address) &&
           end_address <= OS_KERNEL_USER_PROGRAM_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE;
}

UserElfValidationStatus ValidateUserElf(const uint8_t *image, const uint64_t image_size_bytes,
                                        UserElfLayout &layout) noexcept {
    if (image == nullptr) {
        return UserElfValidationStatus::NullImage;
    }
    if (image_size_bytes < OS_KERNEL_USER_ELF_HEADER_SIZE_BYTES) {
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

    const uint64_t program_header_count =
        ReadLittleEndian16(image + OS_KERNEL_USER_ELF_PROGRAM_HEADER_COUNT_OFFSET);
    if (program_header_count == OS_KERNEL_USER_ELF_EMPTY_VALUE ||
        program_header_count > OS_KERNEL_USER_ELF_MAXIMUM_LOAD_SEGMENT_COUNT) {
        return UserElfValidationStatus::InvalidProgramHeaderCount;
    }
    const uint64_t program_header_offset =
        ReadLittleEndian64(image + OS_KERNEL_USER_ELF_PROGRAM_HEADER_OFFSET_OFFSET);
    if (program_header_count > UINT64_MAX / OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_BYTES) {
        return UserElfValidationStatus::ProgramHeaderTableOutOfRange;
    }
    const uint64_t program_header_table_size_bytes =
        program_header_count * OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_BYTES;
    uint64_t program_header_table_end = 0ULL;
    if (!CheckedRangeEnd(program_header_offset, program_header_table_size_bytes,
                         program_header_table_end) ||
        program_header_offset < OS_KERNEL_USER_ELF_HEADER_SIZE_BYTES ||
        program_header_table_end > image_size_bytes) {
        return UserElfValidationStatus::ProgramHeaderTableOutOfRange;
    }

    UserElfLayout candidate_layout{};
    candidate_layout.entry_virtual_address =
        ReadLittleEndian64(image + OS_KERNEL_USER_ELF_ENTRY_OFFSET);
    uint64_t mapped_page_count = 0ULL;
    for (uint64_t program_index = 0ULL; program_index < program_header_count; ++program_index) {
        const uint8_t *const program_header =
            image + program_header_offset +
            program_index * OS_KERNEL_USER_ELF_PROGRAM_HEADER_SIZE_BYTES;
        if (ReadLittleEndian32(program_header + OS_KERNEL_USER_ELF_PROGRAM_TYPE_OFFSET) !=
            OS_KERNEL_USER_ELF_LOAD_PROGRAM_TYPE) {
            return UserElfValidationStatus::UnsupportedProgramHeader;
        }
        const uint32_t flags =
            ReadLittleEndian32(program_header + OS_KERNEL_USER_ELF_PROGRAM_FLAGS_OFFSET);
        if ((flags & ~OS_KERNEL_USER_ELF_KNOWN_FLAG_MASK) != 0U ||
            (flags & OS_KERNEL_USER_ELF_READ_FLAG) == 0U ||
            ((flags & OS_KERNEL_USER_ELF_WRITE_FLAG) != 0U &&
             (flags & OS_KERNEL_USER_ELF_EXECUTE_FLAG) != 0U)) {
            return UserElfValidationStatus::InvalidSegmentFlags;
        }

        UserElfLoadSegment segment{
            .file_offset =
                ReadLittleEndian64(program_header + OS_KERNEL_USER_ELF_PROGRAM_FILE_OFFSET_OFFSET),
            .virtual_address = ReadLittleEndian64(
                program_header + OS_KERNEL_USER_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET),
            .file_size_bytes =
                ReadLittleEndian64(program_header + OS_KERNEL_USER_ELF_PROGRAM_FILE_SIZE_OFFSET),
            .memory_size_bytes =
                ReadLittleEndian64(program_header + OS_KERNEL_USER_ELF_PROGRAM_MEMORY_SIZE_OFFSET),
            .writable = (flags & OS_KERNEL_USER_ELF_WRITE_FLAG) != 0U,
            .executable = (flags & OS_KERNEL_USER_ELF_EXECUTE_FLAG) != 0U,
        };
        const uint64_t physical_address =
            ReadLittleEndian64(program_header + OS_KERNEL_USER_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET);
        const uint64_t alignment =
            ReadLittleEndian64(program_header + OS_KERNEL_USER_ELF_PROGRAM_ALIGNMENT_OFFSET);
        if (alignment != OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
            (segment.file_offset & OS_KERNEL_USER_ELF_PAGE_MASK) != 0ULL ||
            (segment.virtual_address & OS_KERNEL_USER_ELF_PAGE_MASK) != 0ULL ||
            physical_address != segment.virtual_address) {
            return UserElfValidationStatus::InvalidSegmentAlignment;
        }
        if (segment.memory_size_bytes == OS_KERNEL_USER_ELF_EMPTY_VALUE ||
            segment.file_size_bytes > segment.memory_size_bytes) {
            return UserElfValidationStatus::InvalidSegmentMemoryRange;
        }
        uint64_t file_end = 0ULL;
        if (!CheckedRangeEnd(segment.file_offset, segment.file_size_bytes, file_end) ||
            file_end > image_size_bytes) {
            return UserElfValidationStatus::InvalidSegmentFileRange;
        }
        if (!IsUserProgramVirtualAddressRange(segment.virtual_address, segment.memory_size_bytes)) {
            return UserElfValidationStatus::InvalidSegmentMemoryRange;
        }
        for (uint64_t existing_index = 0ULL; existing_index < candidate_layout.load_segment_count;
             ++existing_index) {
            if (SegmentRangesOverlap(segment, candidate_layout.load_segments[existing_index])) {
                return UserElfValidationStatus::OverlappingSegments;
            }
        }
        const uint64_t segment_page_count =
            (segment.memory_size_bytes + OS_KERNEL_USER_ELF_PAGE_ROUNDING) /
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        if (segment_page_count > OS_KERNEL_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT ||
            mapped_page_count > OS_KERNEL_USER_ELF_MAXIMUM_MAPPED_PAGE_COUNT - segment_page_count) {
            return UserElfValidationStatus::TooManyMappedPages;
        }
        mapped_page_count += segment_page_count;
        candidate_layout.load_segments[candidate_layout.load_segment_count] = segment;
        ++candidate_layout.load_segment_count;
    }
    if (!EntryBelongsToExecutableSegment(candidate_layout.entry_virtual_address,
                                         candidate_layout)) {
        return UserElfValidationStatus::EntryNotExecutable;
    }
    layout = candidate_layout;
    return UserElfValidationStatus::Succeeded;
}
}
