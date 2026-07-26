#include "os/kernel/user/user_elf.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_ELF_SUITE_NAME = "kernel/user_elf/unit";
constexpr std::string_view OS_TEST_USER_ELF_VALID_MESSAGE = "合法用户 ELF 必须通过验证";
constexpr std::string_view OS_TEST_USER_ELF_NULL_MESSAGE = "空镜像指针必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_TRUNCATED_MESSAGE = "截断 ELF 头必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_IDENTIFICATION_MESSAGE = "错误 ELF 标识必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_TYPE_MESSAGE = "非 ET_EXEC 文件必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_MACHINE_MESSAGE = "非 x86-64 文件必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_VERSION_MESSAGE = "错误 ELF 版本必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_HEADER_SIZE_MESSAGE = "错误 ELF 头长度必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_PROGRAM_SIZE_MESSAGE = "错误程序头长度必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_PROGRAM_COUNT_MESSAGE = "空程序头集合必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_PROGRAM_TABLE_MESSAGE = "越界程序头表必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_PROGRAM_TYPE_MESSAGE = "非 PT_LOAD 程序头必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_FLAGS_MESSAGE = "违反 W^X 的段必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_ALIGNMENT_MESSAGE = "未按页对齐的段必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_FILE_RANGE_MESSAGE = "越界文件段必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_MEMORY_RANGE_MESSAGE = "非法内存段必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_OVERLAP_MESSAGE = "重叠加载段必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_PAGE_LIMIT_MESSAGE = "超出页数上限必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_ENTRY_MESSAGE = "不在可执行段内的入口必须被拒绝";
constexpr std::string_view OS_TEST_USER_ELF_ATOMIC_OUTPUT_MESSAGE = "验证失败不得覆盖调用方布局";
constexpr uint64_t OS_TEST_USER_ELF_IMAGE_SIZE_BYTES = 16384ULL;
constexpr uint64_t OS_TEST_USER_ELF_HEADER_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_HEADER_SIZE_BYTES = 56ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET = 64ULL;
constexpr uint64_t OS_TEST_USER_ELF_IDENTIFICATION_TYPE_OFFSET = 16ULL;
constexpr uint64_t OS_TEST_USER_ELF_IDENTIFICATION_MACHINE_OFFSET = 18ULL;
constexpr uint64_t OS_TEST_USER_ELF_VERSION_OFFSET = 20ULL;
constexpr uint64_t OS_TEST_USER_ELF_ENTRY_OFFSET = 24ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_TABLE_OFFSET = 32ULL;
constexpr uint64_t OS_TEST_USER_ELF_HEADER_SIZE_OFFSET = 52ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_HEADER_SIZE_OFFSET = 54ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_OFFSET = 56ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_TYPE_OFFSET = 0ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_FLAGS_OFFSET = 4ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_FILE_OFFSET = 8ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET = 16ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET = 24ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_FILE_SIZE_OFFSET = 32ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_MEMORY_SIZE_OFFSET = 40ULL;
constexpr uint64_t OS_TEST_USER_ELF_PROGRAM_ALIGNMENT_OFFSET = 48ULL;
constexpr uint64_t OS_TEST_USER_ELF_SEGMENT_FILE_OFFSET = 4096ULL;
constexpr uint64_t OS_TEST_USER_ELF_SECOND_SEGMENT_FILE_OFFSET = 8192ULL;
constexpr uint64_t OS_TEST_USER_ELF_ENTRY_ADDRESS = 0x0000000040000000ULL;
constexpr uint64_t OS_TEST_USER_ELF_PAGE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_USER_ELF_SINGLE_FILE_BYTE = 1ULL;
constexpr uint64_t OS_TEST_USER_ELF_TRUNCATED_SIZE_BYTES = 63ULL;
constexpr uint64_t OS_TEST_USER_ELF_INVALID_TABLE_OFFSET = 16360ULL;
constexpr uint64_t OS_TEST_USER_ELF_TOO_MANY_PAGE_COUNT = 33ULL;
constexpr uint64_t OS_TEST_USER_ELF_NON_EXECUTABLE_ENTRY = 0x0000000040010000ULL;
constexpr uint64_t OS_TEST_USER_ELF_SENTINEL_ENTRY = 0xA5A5A5A5A5A5A5A5ULL;
constexpr uint16_t OS_TEST_USER_ELF_EXECUTABLE_TYPE = 2U;
constexpr uint16_t OS_TEST_USER_ELF_X86_64_MACHINE = 62U;
constexpr uint32_t OS_TEST_USER_ELF_CURRENT_VERSION = 1U;
constexpr uint32_t OS_TEST_USER_ELF_LOAD_TYPE = 1U;
constexpr uint32_t OS_TEST_USER_ELF_READ_EXECUTE_FLAGS = 5U;
constexpr uint32_t OS_TEST_USER_ELF_READ_WRITE_EXECUTE_FLAGS = 7U;
constexpr uint16_t OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_ONE = 1U;
constexpr uint16_t OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_TWO = 2U;
constexpr uint8_t OS_TEST_USER_ELF_MAGIC0 = 0x7FU;
constexpr uint8_t OS_TEST_USER_ELF_MAGIC1 = 0x45U;
constexpr uint8_t OS_TEST_USER_ELF_MAGIC2 = 0x4CU;
constexpr uint8_t OS_TEST_USER_ELF_MAGIC3 = 0x46U;
constexpr uint8_t OS_TEST_USER_ELF_CLASS_64 = 2U;
constexpr uint8_t OS_TEST_USER_ELF_LITTLE_ENDIAN = 1U;
constexpr uint8_t OS_TEST_USER_ELF_IDENTIFICATION_VERSION = 1U;
constexpr uint64_t OS_TEST_USER_ELF_WORD32_BYTE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_USER_ELF_WORD64_BYTE_COUNT = 8ULL;
constexpr uint64_t OS_TEST_USER_ELF_BITS_PER_BYTE = 8ULL;

struct UserElfTestImage final {
    uint8_t bytes[OS_TEST_USER_ELF_IMAGE_SIZE_BYTES];
};

void WriteLittleEndian16(uint8_t *bytes, const uint64_t offset, const uint16_t value) noexcept {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1ULL] =
        static_cast<uint8_t>(value >> static_cast<uint16_t>(OS_TEST_USER_ELF_BITS_PER_BYTE));
}

void WriteLittleEndian32(uint8_t *bytes, const uint64_t offset, const uint32_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_USER_ELF_WORD32_BYTE_COUNT;
         ++byte_index) {
        bytes[offset + byte_index] =
            static_cast<uint8_t>(value >> (byte_index * OS_TEST_USER_ELF_BITS_PER_BYTE));
    }
}

void WriteLittleEndian64(uint8_t *bytes, const uint64_t offset, const uint64_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_USER_ELF_WORD64_BYTE_COUNT;
         ++byte_index) {
        bytes[offset + byte_index] =
            static_cast<uint8_t>(value >> (byte_index * OS_TEST_USER_ELF_BITS_PER_BYTE));
    }
}

void WriteLoadSegment(uint8_t *bytes, const uint64_t program_header_offset,
                      const uint64_t file_offset, const uint64_t virtual_address,
                      const uint64_t memory_size_bytes) noexcept {
    WriteLittleEndian32(bytes, program_header_offset + OS_TEST_USER_ELF_PROGRAM_TYPE_OFFSET,
                        OS_TEST_USER_ELF_LOAD_TYPE);
    WriteLittleEndian32(bytes, program_header_offset + OS_TEST_USER_ELF_PROGRAM_FLAGS_OFFSET,
                        OS_TEST_USER_ELF_READ_EXECUTE_FLAGS);
    WriteLittleEndian64(bytes, program_header_offset + OS_TEST_USER_ELF_PROGRAM_FILE_OFFSET,
                        file_offset);
    WriteLittleEndian64(bytes,
                        program_header_offset + OS_TEST_USER_ELF_PROGRAM_VIRTUAL_ADDRESS_OFFSET,
                        virtual_address);
    WriteLittleEndian64(bytes,
                        program_header_offset + OS_TEST_USER_ELF_PROGRAM_PHYSICAL_ADDRESS_OFFSET,
                        virtual_address);
    WriteLittleEndian64(bytes, program_header_offset + OS_TEST_USER_ELF_PROGRAM_FILE_SIZE_OFFSET,
                        OS_TEST_USER_ELF_SINGLE_FILE_BYTE);
    WriteLittleEndian64(bytes, program_header_offset + OS_TEST_USER_ELF_PROGRAM_MEMORY_SIZE_OFFSET,
                        memory_size_bytes);
    WriteLittleEndian64(bytes, program_header_offset + OS_TEST_USER_ELF_PROGRAM_ALIGNMENT_OFFSET,
                        OS_TEST_USER_ELF_PAGE_SIZE_BYTES);
}

[[nodiscard]] UserElfTestImage CreateValidUserElf() noexcept {
    UserElfTestImage image{};
    image.bytes[0] = OS_TEST_USER_ELF_MAGIC0;
    image.bytes[1] = OS_TEST_USER_ELF_MAGIC1;
    image.bytes[2] = OS_TEST_USER_ELF_MAGIC2;
    image.bytes[3] = OS_TEST_USER_ELF_MAGIC3;
    image.bytes[4] = OS_TEST_USER_ELF_CLASS_64;
    image.bytes[5] = OS_TEST_USER_ELF_LITTLE_ENDIAN;
    image.bytes[6] = OS_TEST_USER_ELF_IDENTIFICATION_VERSION;
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_IDENTIFICATION_TYPE_OFFSET,
                        OS_TEST_USER_ELF_EXECUTABLE_TYPE);
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_IDENTIFICATION_MACHINE_OFFSET,
                        OS_TEST_USER_ELF_X86_64_MACHINE);
    WriteLittleEndian32(image.bytes, OS_TEST_USER_ELF_VERSION_OFFSET,
                        OS_TEST_USER_ELF_CURRENT_VERSION);
    WriteLittleEndian64(image.bytes, OS_TEST_USER_ELF_ENTRY_OFFSET, OS_TEST_USER_ELF_ENTRY_ADDRESS);
    WriteLittleEndian64(image.bytes, OS_TEST_USER_ELF_PROGRAM_TABLE_OFFSET,
                        OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET);
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_HEADER_SIZE_OFFSET,
                        static_cast<uint16_t>(OS_TEST_USER_ELF_HEADER_SIZE_BYTES));
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_PROGRAM_HEADER_SIZE_OFFSET,
                        static_cast<uint16_t>(OS_TEST_USER_ELF_PROGRAM_HEADER_SIZE_BYTES));
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_OFFSET,
                        OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_ONE);
    WriteLoadSegment(image.bytes, OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET,
                     OS_TEST_USER_ELF_SEGMENT_FILE_OFFSET, OS_TEST_USER_ELF_ENTRY_ADDRESS,
                     OS_TEST_USER_ELF_PAGE_SIZE_BYTES);
    return image;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_ELF_SUITE_NAME};
    os::kernel::UserElfLayout layout{};
    UserElfTestImage image = CreateValidUserElf();
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
                os::kernel::UserElfValidationStatus::Succeeded &&
            layout.entry_virtual_address == OS_TEST_USER_ELF_ENTRY_ADDRESS &&
            layout.load_segment_count == OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_ONE,
        OS_TEST_USER_ELF_VALID_MESSAGE);
    test_context.Expect(
        os::kernel::ValidateUserElf(nullptr, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::NullImage,
        OS_TEST_USER_ELF_NULL_MESSAGE);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_TRUNCATED_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::HeaderTruncated,
        OS_TEST_USER_ELF_TRUNCATED_MESSAGE);

    image = CreateValidUserElf();
    image.bytes[0] = 0U;
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidIdentification,
        OS_TEST_USER_ELF_IDENTIFICATION_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_IDENTIFICATION_TYPE_OFFSET, 0U);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidExecutableType,
        OS_TEST_USER_ELF_TYPE_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_IDENTIFICATION_MACHINE_OFFSET, 0U);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidMachine,
        OS_TEST_USER_ELF_MACHINE_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian32(image.bytes, OS_TEST_USER_ELF_VERSION_OFFSET, 0U);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidVersion,
        OS_TEST_USER_ELF_VERSION_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_HEADER_SIZE_OFFSET, 0U);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidHeaderSize,
        OS_TEST_USER_ELF_HEADER_SIZE_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_PROGRAM_HEADER_SIZE_OFFSET, 0U);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidProgramHeaderSize,
        OS_TEST_USER_ELF_PROGRAM_SIZE_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_OFFSET, 0U);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidProgramHeaderCount,
        OS_TEST_USER_ELF_PROGRAM_COUNT_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian64(image.bytes, OS_TEST_USER_ELF_PROGRAM_TABLE_OFFSET,
                        OS_TEST_USER_ELF_INVALID_TABLE_OFFSET);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::ProgramHeaderTableOutOfRange,
        OS_TEST_USER_ELF_PROGRAM_TABLE_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian32(
        image.bytes, OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET + OS_TEST_USER_ELF_PROGRAM_TYPE_OFFSET,
        0U);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::UnsupportedProgramHeader,
        OS_TEST_USER_ELF_PROGRAM_TYPE_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian32(
        image.bytes, OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET + OS_TEST_USER_ELF_PROGRAM_FLAGS_OFFSET,
        OS_TEST_USER_ELF_READ_WRITE_EXECUTE_FLAGS);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidSegmentFlags,
        OS_TEST_USER_ELF_FLAGS_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian64(
        image.bytes,
        OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET + OS_TEST_USER_ELF_PROGRAM_ALIGNMENT_OFFSET, 1ULL);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidSegmentAlignment,
        OS_TEST_USER_ELF_ALIGNMENT_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian64(image.bytes,
                        OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET +
                            OS_TEST_USER_ELF_PROGRAM_FILE_SIZE_OFFSET,
                        OS_TEST_USER_ELF_IMAGE_SIZE_BYTES);
    WriteLittleEndian64(image.bytes,
                        OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET +
                            OS_TEST_USER_ELF_PROGRAM_MEMORY_SIZE_OFFSET,
                        OS_TEST_USER_ELF_IMAGE_SIZE_BYTES);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidSegmentFileRange,
        OS_TEST_USER_ELF_FILE_RANGE_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian64(
        image.bytes,
        OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET + OS_TEST_USER_ELF_PROGRAM_MEMORY_SIZE_OFFSET, 0ULL);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::InvalidSegmentMemoryRange,
        OS_TEST_USER_ELF_MEMORY_RANGE_MESSAGE);

    image = CreateValidUserElf();
    WriteLittleEndian16(image.bytes, OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_OFFSET,
                        OS_TEST_USER_ELF_PROGRAM_HEADER_COUNT_TWO);
    WriteLoadSegment(image.bytes,
                     OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET +
                         OS_TEST_USER_ELF_PROGRAM_HEADER_SIZE_BYTES,
                     OS_TEST_USER_ELF_SECOND_SEGMENT_FILE_OFFSET, OS_TEST_USER_ELF_ENTRY_ADDRESS,
                     OS_TEST_USER_ELF_PAGE_SIZE_BYTES);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::OverlappingSegments,
        OS_TEST_USER_ELF_OVERLAP_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian64(image.bytes,
                        OS_TEST_USER_ELF_PROGRAM_HEADER_OFFSET +
                            OS_TEST_USER_ELF_PROGRAM_MEMORY_SIZE_OFFSET,
                        OS_TEST_USER_ELF_TOO_MANY_PAGE_COUNT * OS_TEST_USER_ELF_PAGE_SIZE_BYTES);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::TooManyMappedPages,
        OS_TEST_USER_ELF_PAGE_LIMIT_MESSAGE);
    image = CreateValidUserElf();
    WriteLittleEndian64(image.bytes, OS_TEST_USER_ELF_ENTRY_OFFSET,
                        OS_TEST_USER_ELF_NON_EXECUTABLE_ENTRY);
    test_context.Expect(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout) ==
            os::kernel::UserElfValidationStatus::EntryNotExecutable,
        OS_TEST_USER_ELF_ENTRY_MESSAGE);

    image = CreateValidUserElf();
    layout.entry_virtual_address = OS_TEST_USER_ELF_SENTINEL_ENTRY;
    image.bytes[0] = 0U;
    static_cast<void>(
        os::kernel::ValidateUserElf(image.bytes, OS_TEST_USER_ELF_IMAGE_SIZE_BYTES, layout));
    test_context.Expect(layout.entry_virtual_address == OS_TEST_USER_ELF_SENTINEL_ENTRY,
                        OS_TEST_USER_ELF_ATOMIC_OUTPUT_MESSAGE);
    return test_context.ExitCode();
}
