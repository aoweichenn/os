#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr uint64_t OS_USER_CORE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_CORE_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_USER_CORE_COMMAND_INDEX = 0ULL;
constexpr uint64_t OS_USER_CORE_FIRST_ARGUMENT_INDEX = 1ULL;
constexpr uint64_t OS_USER_CORE_SECOND_ARGUMENT_INDEX = 2ULL;
constexpr uint64_t OS_USER_CORE_SINGLE_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_CORE_TWO_ARGUMENT_COUNT = 3ULL;
constexpr uint64_t OS_USER_CORE_TRANSFER_SIZE_BYTES =
    os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES;
constexpr uint64_t OS_USER_CORE_PATH_CAPACITY_BYTES =
    os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES;
constexpr uint64_t OS_USER_CORE_DECIMAL_CAPACITY_BYTES = 20ULL;
constexpr uint64_t OS_USER_CORE_DECIMAL_RADIX = 10ULL;
constexpr uint64_t OS_USER_CORE_HEAD_DEFAULT_LINE_COUNT = 10ULL;
constexpr uint64_t OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr int64_t OS_USER_CORE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_CORE_DIRECTORY_ENTRY_RESULT = 1LL;
constexpr int64_t OS_USER_CORE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_CORE_FAILURE_EXIT_CODE = 1LL;
constexpr uint8_t OS_USER_CORE_NEWLINE_CHARACTER = static_cast<uint8_t>('\n');
constexpr uint8_t OS_USER_CORE_SPACE_CHARACTER = static_cast<uint8_t>(' ');
constexpr uint8_t OS_USER_CORE_TAB_CHARACTER = static_cast<uint8_t>('\t');
constexpr uint8_t OS_USER_CORE_CARRIAGE_RETURN_CHARACTER =
    static_cast<uint8_t>('\r');
constexpr char OS_USER_CORE_STRING_TERMINATOR = '\0';
constexpr char OS_USER_CORE_PATH_SEPARATOR = '/';
constexpr char OS_USER_CORE_FIRST_DECIMAL_CHARACTER = '0';
constexpr char OS_USER_CORE_LAST_DECIMAL_CHARACTER = '9';
constexpr char OS_USER_CORE_CURRENT_DIRECTORY[] = ".";
constexpr char OS_USER_CORE_DIRECTORY_SUFFIX[] = "/";
constexpr char OS_USER_CORE_SPACE[] = " ";
constexpr char OS_USER_CORE_NEWLINE[] = "\r\n";
constexpr char OS_USER_CORE_HELP_TEXT[] =
    "内置于 rootfs 的外置工具：\r\n"
    "help echo cat wc head tee true false pwd ls stat mkdir write touch\r\n"
    "rm rmdir mv truncate sync\r\n"
    "Shell 仅保留 cd 与 exit；支持 <、> 与最多 16 级管线。\r\n";
constexpr char OS_USER_CORE_OPERATION_ERROR[] = "error: 操作失败\r\n";
constexpr char OS_USER_CORE_UNKNOWN_TOOL_ERROR[] = "error: 未知核心工具\r\n";
constexpr char OS_USER_CORE_STAT_INODE_PREFIX[] = "inode=";
constexpr char OS_USER_CORE_STAT_SIZE_PREFIX[] = " size=";
constexpr char OS_USER_CORE_STAT_ALLOCATED_PREFIX[] = " allocated=";
constexpr char OS_USER_CORE_STAT_LINKS_PREFIX[] = " links=";
constexpr char OS_USER_CORE_STAT_TYPE_PREFIX[] = " type=";
constexpr char OS_USER_CORE_STAT_FILE_TYPE[] = "file";
constexpr char OS_USER_CORE_STAT_DIRECTORY_TYPE[] = "directory";
constexpr char OS_USER_CORE_WC_SEPARATOR[] = " ";
constexpr char OS_USER_CORE_HELP_COMMAND[] = "help";
constexpr char OS_USER_CORE_ECHO_COMMAND[] = "echo";
constexpr char OS_USER_CORE_CAT_COMMAND[] = "cat";
constexpr char OS_USER_CORE_WC_COMMAND[] = "wc";
constexpr char OS_USER_CORE_HEAD_COMMAND[] = "head";
constexpr char OS_USER_CORE_TEE_COMMAND[] = "tee";
constexpr char OS_USER_CORE_TRUE_COMMAND[] = "true";
constexpr char OS_USER_CORE_FALSE_COMMAND[] = "false";
constexpr char OS_USER_CORE_PWD_COMMAND[] = "pwd";
constexpr char OS_USER_CORE_LIST_COMMAND[] = "ls";
constexpr char OS_USER_CORE_STAT_COMMAND[] = "stat";
constexpr char OS_USER_CORE_MKDIR_COMMAND[] = "mkdir";
constexpr char OS_USER_CORE_WRITE_COMMAND[] = "write";
constexpr char OS_USER_CORE_TOUCH_COMMAND[] = "touch";
constexpr char OS_USER_CORE_REMOVE_COMMAND[] = "rm";
constexpr char OS_USER_CORE_REMOVE_DIRECTORY_COMMAND[] = "rmdir";
constexpr char OS_USER_CORE_MOVE_COMMAND[] = "mv";
constexpr char OS_USER_CORE_TRUNCATE_COMMAND[] = "truncate";
constexpr char OS_USER_CORE_SYNC_COMMAND[] = "sync";
constexpr uint64_t OS_USER_CORE_READ_OPEN_FLAGS = os::abi::OS_ABI_FILE_OPEN_READ_FLAG;
constexpr uint64_t OS_USER_CORE_WRITE_OPEN_FLAGS =
    os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG | os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
    os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
constexpr uint64_t OS_USER_CORE_TOUCH_OPEN_FLAGS =
    os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
    os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG;

[[nodiscard]] uint64_t StringLength(const char *const text) noexcept {
    if (text == nullptr) {
        return OS_USER_CORE_EMPTY_VALUE;
    }
    uint64_t length_bytes = OS_USER_CORE_EMPTY_VALUE;
    while (length_bytes < OS_USER_CORE_PATH_CAPACITY_BYTES &&
           text[length_bytes] != OS_USER_CORE_STRING_TERMINATOR) {
        ++length_bytes;
    }
    return length_bytes;
}

[[nodiscard]] bool BytesEqual(const char *const first, const uint64_t first_length_bytes,
                              const char *const second,
                              const uint64_t second_length_bytes) noexcept {
    if (first == nullptr || second == nullptr || first_length_bytes != second_length_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < first_length_bytes;
         ++byte_index) {
        if (first[byte_index] != second[byte_index]) {
            return false;
        }
    }
    return true;
}

template <uint64_t SizeBytes>
[[nodiscard]] bool EqualsLiteral(const char *const actual,
                                 const uint64_t actual_length_bytes,
                                 const char (&expected)[SizeBytes]) noexcept {
    return BytesEqual(actual, actual_length_bytes, expected,
                      SizeBytes - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES);
}

[[nodiscard]] bool WriteBytes(const uint64_t descriptor, const uint8_t *const bytes,
                              const uint64_t length_bytes) noexcept {
    return bytes != nullptr &&
           os::user::WriteDescriptor(descriptor, bytes, length_bytes) ==
               static_cast<int64_t>(length_bytes);
}

[[nodiscard]] bool WriteText(const uint64_t descriptor, const char *const text,
                             const uint64_t length_bytes) noexcept {
    return WriteBytes(descriptor, reinterpret_cast<const uint8_t *>(text), length_bytes);
}

template <uint64_t SizeBytes>
[[nodiscard]] bool WriteLiteral(const uint64_t descriptor,
                                const char (&literal)[SizeBytes]) noexcept {
    return WriteText(descriptor, literal,
                     SizeBytes - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES);
}

[[nodiscard]] bool WriteDecimal(const uint64_t descriptor, uint64_t value) noexcept {
    char digits[OS_USER_CORE_DECIMAL_CAPACITY_BYTES]{};
    uint64_t digit_count = OS_USER_CORE_EMPTY_VALUE;
    do {
        digits[digit_count] = static_cast<char>(
            OS_USER_CORE_FIRST_DECIMAL_CHARACTER + value % OS_USER_CORE_DECIMAL_RADIX);
        value /= OS_USER_CORE_DECIMAL_RADIX;
        ++digit_count;
    } while (value != OS_USER_CORE_EMPTY_VALUE &&
             digit_count < OS_USER_CORE_DECIMAL_CAPACITY_BYTES);
    for (uint64_t first_index = OS_USER_CORE_EMPTY_VALUE,
                  last_index = digit_count - OS_USER_CORE_FIRST_VALUE;
         first_index < last_index; ++first_index, --last_index) {
        const char temporary = digits[first_index];
        digits[first_index] = digits[last_index];
        digits[last_index] = temporary;
    }
    return WriteText(descriptor, digits, digit_count);
}

[[nodiscard]] bool ParseDecimal(const char *const text, const uint64_t length_bytes,
                                uint64_t &value) noexcept {
    value = OS_USER_CORE_EMPTY_VALUE;
    if (text == nullptr || length_bytes == OS_USER_CORE_EMPTY_VALUE) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        const char character = text[byte_index];
        if (character < OS_USER_CORE_FIRST_DECIMAL_CHARACTER ||
            character > OS_USER_CORE_LAST_DECIMAL_CHARACTER) {
            return false;
        }
        const uint64_t digit =
            static_cast<uint64_t>(character - OS_USER_CORE_FIRST_DECIMAL_CHARACTER);
        if (value > (UINT64_MAX - digit) / OS_USER_CORE_DECIMAL_RADIX) {
            return false;
        }
        value = value * OS_USER_CORE_DECIMAL_RADIX + digit;
    }
    return true;
}

[[nodiscard]] const char *ToolName(const char *const argument_zero,
                                   uint64_t &name_length_bytes) noexcept {
    const uint64_t path_length_bytes = StringLength(argument_zero);
    uint64_t name_offset = OS_USER_CORE_EMPTY_VALUE;
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < path_length_bytes;
         ++byte_index) {
        if (argument_zero[byte_index] == OS_USER_CORE_PATH_SEPARATOR) {
            name_offset = byte_index + OS_USER_CORE_FIRST_VALUE;
        }
    }
    name_length_bytes = path_length_bytes - name_offset;
    return argument_zero + name_offset;
}

[[nodiscard]] int64_t CopyDescriptor(const uint64_t source_descriptor,
                                     const uint64_t first_destination,
                                     const uint64_t second_destination,
                                     const bool has_second_destination,
                                     const uint64_t line_limit,
                                     const bool limit_lines) noexcept {
    uint8_t buffer[OS_USER_CORE_TRANSFER_SIZE_BYTES]{};
    uint64_t line_count = OS_USER_CORE_EMPTY_VALUE;
    while (!limit_lines || line_count < line_limit) {
        const int64_t read_result =
            os::user::ReadDescriptor(source_descriptor, buffer, OS_USER_CORE_TRANSFER_SIZE_BYTES);
        if (read_result < OS_USER_CORE_SUCCESS_RESULT) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
        if (read_result == OS_USER_CORE_SUCCESS_RESULT) {
            return OS_USER_CORE_SUCCESS_EXIT_CODE;
        }
        uint64_t output_bytes = static_cast<uint64_t>(read_result);
        if (limit_lines) {
            for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < output_bytes;
                 ++byte_index) {
                if (buffer[byte_index] == OS_USER_CORE_NEWLINE_CHARACTER) {
                    ++line_count;
                    if (line_count == line_limit) {
                        output_bytes = byte_index + OS_USER_CORE_FIRST_VALUE;
                        break;
                    }
                }
            }
        }
        if (!WriteBytes(first_destination, buffer, output_bytes) ||
            (has_second_destination &&
             !WriteBytes(second_destination, buffer, output_bytes))) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
    }
    return OS_USER_CORE_SUCCESS_EXIT_CODE;
}

[[nodiscard]] int64_t RunHelp(const uint64_t argument_count) noexcept {
    return argument_count == OS_USER_CORE_FIRST_VALUE &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                OS_USER_CORE_HELP_TEXT)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunEcho(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    for (uint64_t argument_index = OS_USER_CORE_FIRST_ARGUMENT_INDEX;
         argument_index < argument_count; ++argument_index) {
        if (argument_index != OS_USER_CORE_FIRST_ARGUMENT_INDEX &&
            !WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_SPACE)) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
        if (!WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, arguments[argument_index],
                       StringLength(arguments[argument_index]))) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
    }
    return WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunPwd(const uint64_t argument_count) noexcept {
    if (argument_count != OS_USER_CORE_FIRST_VALUE) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    char path[OS_USER_CORE_PATH_CAPACITY_BYTES]{};
    const int64_t path_length_bytes =
        os::user::GetWorkingDirectory(path, OS_USER_CORE_PATH_CAPACITY_BYTES);
    return path_length_bytes > OS_USER_CORE_SUCCESS_RESULT &&
                   WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, path,
                             static_cast<uint64_t>(path_length_bytes)) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunCat(const uint64_t argument_count,
                             const char *const *const arguments) noexcept {
    if (argument_count == OS_USER_CORE_FIRST_VALUE) {
        return CopyDescriptor(os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR,
                              os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                              OS_USER_CORE_EMPTY_VALUE, false, OS_USER_CORE_EMPTY_VALUE, false);
    }
    for (uint64_t argument_index = OS_USER_CORE_FIRST_ARGUMENT_INDEX;
         argument_index < argument_count; ++argument_index) {
        const int64_t descriptor =
            os::user::OpenFile(arguments[argument_index], StringLength(arguments[argument_index]),
                               OS_USER_CORE_READ_OPEN_FLAGS);
        if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
        const int64_t copy_result =
            CopyDescriptor(static_cast<uint64_t>(descriptor),
                           os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_EMPTY_VALUE,
                           false, OS_USER_CORE_EMPTY_VALUE, false);
        const int64_t close_result =
            os::user::CloseDescriptor(static_cast<uint64_t>(descriptor));
        if (copy_result != OS_USER_CORE_SUCCESS_RESULT ||
            close_result != OS_USER_CORE_SUCCESS_RESULT) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
    }
    return OS_USER_CORE_SUCCESS_EXIT_CODE;
}

[[nodiscard]] int64_t RunHead(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    if (argument_count > OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    uint64_t descriptor = os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR;
    bool close_descriptor = false;
    if (argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        const int64_t open_result =
            os::user::OpenFile(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                               StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                               OS_USER_CORE_READ_OPEN_FLAGS);
        if (open_result < OS_USER_CORE_SUCCESS_RESULT) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
        descriptor = static_cast<uint64_t>(open_result);
        close_descriptor = true;
    }
    const int64_t result =
        CopyDescriptor(descriptor, os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                       OS_USER_CORE_EMPTY_VALUE, false, OS_USER_CORE_HEAD_DEFAULT_LINE_COUNT, true);
    if (close_descriptor &&
        os::user::CloseDescriptor(descriptor) != OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    return result;
}

[[nodiscard]] int64_t RunTee(const uint64_t argument_count,
                             const char *const *const arguments) noexcept {
    if (argument_count != OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t descriptor =
        os::user::OpenFile(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                           OS_USER_CORE_WRITE_OPEN_FLAGS);
    if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t copy_result =
        CopyDescriptor(os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR,
                       os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                       static_cast<uint64_t>(descriptor), true, OS_USER_CORE_EMPTY_VALUE, false);
    const int64_t close_result = os::user::CloseDescriptor(static_cast<uint64_t>(descriptor));
    return copy_result == OS_USER_CORE_SUCCESS_RESULT &&
                   close_result == OS_USER_CORE_SUCCESS_RESULT
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunWc(const uint64_t argument_count,
                            const char *const *const arguments) noexcept {
    if (argument_count > OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    uint64_t descriptor = os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR;
    bool close_descriptor = false;
    if (argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        const int64_t open_result =
            os::user::OpenFile(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                               StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                               OS_USER_CORE_READ_OPEN_FLAGS);
        if (open_result < OS_USER_CORE_SUCCESS_RESULT) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
        descriptor = static_cast<uint64_t>(open_result);
        close_descriptor = true;
    }
    uint64_t byte_count = OS_USER_CORE_EMPTY_VALUE;
    uint64_t line_count = OS_USER_CORE_EMPTY_VALUE;
    uint64_t word_count = OS_USER_CORE_EMPTY_VALUE;
    bool inside_word = false;
    uint8_t buffer[OS_USER_CORE_TRANSFER_SIZE_BYTES]{};
    bool succeeded = true;
    while (succeeded) {
        const int64_t read_result =
            os::user::ReadDescriptor(descriptor, buffer, OS_USER_CORE_TRANSFER_SIZE_BYTES);
        if (read_result < OS_USER_CORE_SUCCESS_RESULT) {
            succeeded = false;
            break;
        }
        if (read_result == OS_USER_CORE_SUCCESS_RESULT) {
            break;
        }
        byte_count += static_cast<uint64_t>(read_result);
        for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE;
             byte_index < static_cast<uint64_t>(read_result); ++byte_index) {
            const uint8_t character = buffer[byte_index];
            if (character == OS_USER_CORE_NEWLINE_CHARACTER) {
                ++line_count;
            }
            const bool whitespace =
                character == OS_USER_CORE_SPACE_CHARACTER ||
                character == OS_USER_CORE_NEWLINE_CHARACTER ||
                character == OS_USER_CORE_TAB_CHARACTER ||
                character == OS_USER_CORE_CARRIAGE_RETURN_CHARACTER;
            if (!whitespace && !inside_word) {
                ++word_count;
            }
            inside_word = !whitespace;
        }
    }
    if (close_descriptor &&
        os::user::CloseDescriptor(descriptor) != OS_USER_CORE_SUCCESS_RESULT) {
        succeeded = false;
    }
    succeeded = succeeded &&
                WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, line_count) &&
                WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                             OS_USER_CORE_WC_SEPARATOR) &&
                WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, word_count) &&
                WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                             OS_USER_CORE_WC_SEPARATOR) &&
                WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, byte_count) &&
                WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                             OS_USER_CORE_NEWLINE);
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunList(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    if (argument_count > OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const char *const path = argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT
                                 ? arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]
                                 : OS_USER_CORE_CURRENT_DIRECTORY;
    const int64_t descriptor = os::user::OpenDirectory(path, StringLength(path));
    if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    bool succeeded = true;
    while (succeeded) {
        os::abi::DirectoryEntry entry{};
        const int64_t read_result =
            os::user::ReadDirectory(static_cast<uint64_t>(descriptor), entry);
        if (read_result == OS_USER_CORE_SUCCESS_RESULT) {
            break;
        }
        if (read_result != OS_USER_CORE_DIRECTORY_ENTRY_RESULT ||
            !WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                       reinterpret_cast<const char *>(entry.name), entry.name_length_bytes) ||
            (entry.type == os::abi::DirectoryEntryType::Directory &&
             !WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                           OS_USER_CORE_DIRECTORY_SUFFIX)) ||
            !WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                          OS_USER_CORE_NEWLINE)) {
            succeeded = false;
        }
    }
    succeeded = os::user::CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
                    OS_USER_CORE_SUCCESS_RESULT &&
                succeeded;
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunStat(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    if (argument_count != OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    os::abi::FileInformation information{};
    if (os::user::StatFile(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                           information) != OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const char *const type_name =
        information.type == os::abi::DirectoryEntryType::RegularFile
            ? OS_USER_CORE_STAT_FILE_TYPE
            : OS_USER_CORE_STAT_DIRECTORY_TYPE;
    const uint64_t type_name_length_bytes =
        information.type == os::abi::DirectoryEntryType::RegularFile
            ? sizeof(OS_USER_CORE_STAT_FILE_TYPE) - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES
            : sizeof(OS_USER_CORE_STAT_DIRECTORY_TYPE) - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES;
    const bool succeeded =
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_INODE_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.inode_number) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_TYPE_PREFIX) &&
        WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, type_name,
                  type_name_length_bytes) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_SIZE_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.size_bytes) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_ALLOCATED_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     information.allocated_size_bytes) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_LINKS_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.link_count) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE);
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunWrite(const uint64_t argument_count,
                               const char *const *const arguments) noexcept {
    if (argument_count < OS_USER_CORE_TWO_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t descriptor =
        os::user::OpenFile(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                           OS_USER_CORE_WRITE_OPEN_FLAGS);
    if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    bool succeeded = true;
    for (uint64_t argument_index = OS_USER_CORE_SECOND_ARGUMENT_INDEX;
         argument_index < argument_count && succeeded; ++argument_index) {
        succeeded =
            (argument_index == OS_USER_CORE_SECOND_ARGUMENT_INDEX ||
             WriteLiteral(static_cast<uint64_t>(descriptor), OS_USER_CORE_SPACE)) &&
            WriteText(static_cast<uint64_t>(descriptor), arguments[argument_index],
                      StringLength(arguments[argument_index]));
    }
    succeeded = os::user::CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
                    OS_USER_CORE_SUCCESS_RESULT &&
                succeeded;
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunTouch(const uint64_t argument_count,
                               const char *const *const arguments) noexcept {
    if (argument_count != OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t descriptor =
        os::user::OpenFile(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                           OS_USER_CORE_TOUCH_OPEN_FLAGS);
    return descriptor >= OS_USER_CORE_SUCCESS_RESULT &&
                   os::user::CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
                       OS_USER_CORE_SUCCESS_RESULT
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t DispatchTool(const char *const tool_name,
                                   const uint64_t tool_name_length_bytes,
                                   const uint64_t argument_count,
                                   const char *const *const arguments) noexcept {
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_HELP_COMMAND)) {
        return RunHelp(argument_count);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_ECHO_COMMAND)) {
        return RunEcho(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_CAT_COMMAND)) {
        return RunCat(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_WC_COMMAND)) {
        return RunWc(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_HEAD_COMMAND)) {
        return RunHead(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_TEE_COMMAND)) {
        return RunTee(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_TRUE_COMMAND)) {
        return argument_count == OS_USER_CORE_FIRST_VALUE ? OS_USER_CORE_SUCCESS_EXIT_CODE
                                                          : OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_FALSE_COMMAND)) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_PWD_COMMAND)) {
        return RunPwd(argument_count);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_LIST_COMMAND)) {
        return RunList(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_STAT_COMMAND)) {
        return RunStat(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_MKDIR_COMMAND)) {
        return argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT &&
                       os::user::CreateDirectory(
                           arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX])) ==
                           OS_USER_CORE_SUCCESS_RESULT
                   ? OS_USER_CORE_SUCCESS_EXIT_CODE
                   : OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_WRITE_COMMAND)) {
        return RunWrite(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_TOUCH_COMMAND)) {
        return RunTouch(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_REMOVE_COMMAND)) {
        return argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT &&
                       os::user::UnlinkFile(
                           arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX])) ==
                           OS_USER_CORE_SUCCESS_RESULT
                   ? OS_USER_CORE_SUCCESS_EXIT_CODE
                   : OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes,
                      OS_USER_CORE_REMOVE_DIRECTORY_COMMAND)) {
        return argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT &&
                       os::user::RemoveDirectory(
                           arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX])) ==
                           OS_USER_CORE_SUCCESS_RESULT
                   ? OS_USER_CORE_SUCCESS_EXIT_CODE
                   : OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_MOVE_COMMAND)) {
        return argument_count == OS_USER_CORE_TWO_ARGUMENT_COUNT &&
                       os::user::Rename(
                           arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                           arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX])) ==
                           OS_USER_CORE_SUCCESS_RESULT
                   ? OS_USER_CORE_SUCCESS_EXIT_CODE
                   : OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_TRUNCATE_COMMAND)) {
        uint64_t size_bytes = OS_USER_CORE_EMPTY_VALUE;
        return argument_count == OS_USER_CORE_TWO_ARGUMENT_COUNT &&
                       ParseDecimal(arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX],
                                    StringLength(arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX]),
                                    size_bytes) &&
                       os::user::TruncateFile(
                           arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                           StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                           size_bytes) == OS_USER_CORE_SUCCESS_RESULT
                   ? OS_USER_CORE_SUCCESS_EXIT_CODE
                   : OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_SYNC_COMMAND)) {
        return argument_count == OS_USER_CORE_FIRST_VALUE &&
                       os::user::SyncFileSystem() == OS_USER_CORE_SUCCESS_RESULT
                   ? OS_USER_CORE_SUCCESS_EXIT_CODE
                   : OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    static_cast<void>(WriteLiteral(os::abi::OS_ABI_STANDARD_ERROR_DESCRIPTOR,
                                   OS_USER_CORE_UNKNOWN_TOOL_ERROR));
    return OS_USER_CORE_FAILURE_EXIT_CODE;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count,
                 const char *const *const arguments) noexcept {
    if (argument_count == OS_USER_CORE_EMPTY_VALUE || arguments == nullptr ||
        arguments[OS_USER_CORE_COMMAND_INDEX] == nullptr) {
        os::user::ExitProcess(OS_USER_CORE_FAILURE_EXIT_CODE);
    }
    uint64_t tool_name_length_bytes = OS_USER_CORE_EMPTY_VALUE;
    const char *const tool_name =
        ToolName(arguments[OS_USER_CORE_COMMAND_INDEX], tool_name_length_bytes);
    const int64_t result =
        DispatchTool(tool_name, tool_name_length_bytes, argument_count, arguments);
    if (result != OS_USER_CORE_SUCCESS_EXIT_CODE) {
        static_cast<void>(WriteLiteral(os::abi::OS_ABI_STANDARD_ERROR_DESCRIPTOR,
                                       OS_USER_CORE_OPERATION_ERROR));
    }
    os::user::ExitProcess(result);
}
