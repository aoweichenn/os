#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr uint64_t OS_USER_CORE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_CORE_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_USER_CORE_COMMAND_INDEX = 0ULL;
constexpr uint64_t OS_USER_CORE_FIRST_ARGUMENT_INDEX = 1ULL;
constexpr uint64_t OS_USER_CORE_SECOND_ARGUMENT_INDEX = 2ULL;
constexpr uint64_t OS_USER_CORE_NO_OPERAND_ARGUMENT_COUNT = 1ULL;
constexpr uint64_t OS_USER_CORE_SINGLE_ARGUMENT_COUNT = 2ULL;
constexpr uint64_t OS_USER_CORE_TWO_ARGUMENT_COUNT = 3ULL;
constexpr uint64_t OS_USER_CORE_TRANSFER_SIZE_BYTES =
    os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES;
constexpr uint64_t OS_USER_CORE_PATH_CAPACITY_BYTES =
    os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES;
constexpr uint64_t OS_USER_CORE_DECIMAL_CAPACITY_BYTES = 20ULL;
constexpr uint64_t OS_USER_CORE_DECIMAL_RADIX = 10ULL;
constexpr uint64_t OS_USER_CORE_HEAD_DEFAULT_LINE_COUNT = 10ULL;
constexpr uint64_t OS_USER_CORE_SEQUENCE_MAXIMUM_VALUE_COUNT = 100000ULL;
constexpr uint64_t OS_USER_CORE_TEXT_LINE_CAPACITY_BYTES = 256ULL;
constexpr uint64_t OS_USER_CORE_TEXT_LINE_COUNT = 64ULL;
constexpr uint64_t OS_USER_CORE_TAIL_LINE_COUNT = 10ULL;
constexpr uint64_t OS_USER_CORE_TRAVERSAL_PATH_COUNT = 128ULL;
constexpr uint64_t OS_USER_CORE_TRAVERSAL_PATH_CAPACITY_BYTES = 513ULL;
constexpr uint64_t OS_USER_CORE_HEXDUMP_LINE_SIZE_BYTES = 16ULL;
constexpr uint64_t OS_USER_CORE_REFERENCE_DISK_SIZE_BYTES = 128ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_USER_CORE_ROOTFS_START_BYTES = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_USER_CORE_ROOTFS_REGION_SIZE_BYTES =
    OS_USER_CORE_REFERENCE_DISK_SIZE_BYTES - OS_USER_CORE_ROOTFS_START_BYTES;
constexpr uint64_t OS_USER_CORE_NANOSECONDS_PER_MILLISECOND = 1'000'000ULL;
constexpr uint64_t OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_CORE_MAXIMUM_UNSIGNED_VALUE = ~0ULL;
constexpr int64_t OS_USER_CORE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_CORE_DIRECTORY_ENTRY_RESULT = 1LL;
constexpr int64_t OS_USER_CORE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_CORE_FAILURE_EXIT_CODE = 1LL;
constexpr uint8_t OS_USER_CORE_NEWLINE_CHARACTER = static_cast<uint8_t>('\n');
constexpr uint8_t OS_USER_CORE_SPACE_CHARACTER = static_cast<uint8_t>(' ');
constexpr uint8_t OS_USER_CORE_TAB_CHARACTER = static_cast<uint8_t>('\t');
constexpr uint8_t OS_USER_CORE_CARRIAGE_RETURN_CHARACTER = static_cast<uint8_t>('\r');
constexpr char OS_USER_CORE_STRING_TERMINATOR = '\0';
constexpr char OS_USER_CORE_PATH_SEPARATOR = '/';
constexpr char OS_USER_CORE_FIRST_DECIMAL_CHARACTER = '0';
constexpr char OS_USER_CORE_LAST_DECIMAL_CHARACTER = '9';
constexpr char OS_USER_CORE_CURRENT_DIRECTORY[] = ".";
constexpr char OS_USER_CORE_DIRECTORY_SUFFIX[] = "/";
constexpr char OS_USER_CORE_SPACE[] = " ";
constexpr char OS_USER_CORE_NEWLINE[] = "\r\n";
constexpr char OS_USER_CORE_CLEAR_SEQUENCE[] = "\x1b[2J\x1b[H";
constexpr char OS_USER_CORE_HELP_TEXT[] =
    "内置于 rootfs 的外置工具：\r\n"
    "help echo err cat wc head tee true false pwd ls stat mkdir write touch\r\n"
    "rm rmdir mv truncate sync basename dirname cp seq uptime ps free\r\n"
    "uname mounts resources sleep kill id env grep find sort tail df du hexdump clear date\r\n"
    "Shell 状态命令含 cd、export、unset；支持变量、glob、控制操作符、重定向与 16 级管线。\r\n";
constexpr char OS_USER_CORE_OPERATION_ERROR[] = "error: 操作失败\r\n";
constexpr char OS_USER_CORE_UNKNOWN_TOOL_ERROR[] = "error: 未知核心工具\r\n";
constexpr char OS_USER_CORE_STAT_INODE_PREFIX[] = "inode=";
constexpr char OS_USER_CORE_STAT_SIZE_PREFIX[] = " size=";
constexpr char OS_USER_CORE_STAT_ALLOCATED_PREFIX[] = " allocated=";
constexpr char OS_USER_CORE_STAT_LINKS_PREFIX[] = " links=";
constexpr char OS_USER_CORE_STAT_ACCESS_TIME_PREFIX[] = " atime_ns=";
constexpr char OS_USER_CORE_STAT_MODIFICATION_TIME_PREFIX[] = " mtime_ns=";
constexpr char OS_USER_CORE_STAT_CHANGE_TIME_PREFIX[] = " ctime_ns=";
constexpr char OS_USER_CORE_STAT_BIRTH_TIME_PREFIX[] = " btime_ns=";
constexpr char OS_USER_CORE_STAT_TYPE_PREFIX[] = " type=";
constexpr char OS_USER_CORE_STAT_FILE_TYPE[] = "file";
constexpr char OS_USER_CORE_STAT_DIRECTORY_TYPE[] = "directory";
constexpr char OS_USER_CORE_WC_SEPARATOR[] = " ";
constexpr char OS_USER_CORE_HELP_COMMAND[] = "help";
constexpr char OS_USER_CORE_ECHO_COMMAND[] = "echo";
constexpr char OS_USER_CORE_ERROR_COMMAND[] = "err";
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
constexpr char OS_USER_CORE_BASENAME_COMMAND[] = "basename";
constexpr char OS_USER_CORE_DIRNAME_COMMAND[] = "dirname";
constexpr char OS_USER_CORE_COPY_COMMAND[] = "cp";
constexpr char OS_USER_CORE_SEQUENCE_COMMAND[] = "seq";
constexpr char OS_USER_CORE_UPTIME_COMMAND[] = "uptime";
constexpr char OS_USER_CORE_PROCESS_COMMAND[] = "ps";
constexpr char OS_USER_CORE_FREE_COMMAND[] = "free";
constexpr char OS_USER_CORE_UNAME_COMMAND[] = "uname";
constexpr char OS_USER_CORE_MOUNTS_COMMAND[] = "mounts";
constexpr char OS_USER_CORE_RESOURCES_COMMAND[] = "resources";
constexpr char OS_USER_CORE_SLEEP_COMMAND[] = "sleep";
constexpr char OS_USER_CORE_KILL_COMMAND[] = "kill";
constexpr char OS_USER_CORE_ID_COMMAND[] = "id";
constexpr char OS_USER_CORE_ENVIRONMENT_COMMAND[] = "env";
constexpr char OS_USER_CORE_GREP_COMMAND[] = "grep";
constexpr char OS_USER_CORE_FIND_COMMAND[] = "find";
constexpr char OS_USER_CORE_SORT_COMMAND[] = "sort";
constexpr char OS_USER_CORE_TAIL_COMMAND[] = "tail";
constexpr char OS_USER_CORE_DISK_FREE_COMMAND[] = "df";
constexpr char OS_USER_CORE_DISK_USAGE_COMMAND[] = "du";
constexpr char OS_USER_CORE_HEXDUMP_COMMAND[] = "hexdump";
constexpr char OS_USER_CORE_CLEAR_COMMAND[] = "clear";
constexpr char OS_USER_CORE_DATE_COMMAND[] = "date";
constexpr char OS_USER_CORE_ROOT_PATH[] = "/";
constexpr char OS_USER_CORE_DISK_TOTAL_PREFIX[] = "total_bytes ";
constexpr char OS_USER_CORE_DISK_USED_PREFIX[] = " file_bytes ";
constexpr char OS_USER_CORE_DISK_AVAILABLE_PREFIX[] = " available_bytes ";
constexpr char OS_USER_CORE_DATE_UTC_PREFIX[] = "utc ";
constexpr char OS_USER_CORE_PROC_UPTIME_PATH[] = "/proc/uptime";
constexpr char OS_USER_CORE_PROC_PROCESSES_PATH[] = "/proc/processes";
constexpr char OS_USER_CORE_PROC_MEMORY_INFORMATION_PATH[] = "/proc/meminfo";
constexpr char OS_USER_CORE_PROC_VERSION_PATH[] = "/proc/version";
constexpr char OS_USER_CORE_PROC_MOUNTS_PATH[] = "/proc/mounts";
constexpr char OS_USER_CORE_PROC_RESOURCES_PATH[] = "/proc/resources";
constexpr uint64_t OS_USER_CORE_READ_OPEN_FLAGS = os::abi::OS_ABI_FILE_OPEN_READ_FLAG;
constexpr uint64_t OS_USER_CORE_WRITE_OPEN_FLAGS = os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
                                                   os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
                                                   os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG;
constexpr uint64_t OS_USER_CORE_TOUCH_OPEN_FLAGS =
    os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG | os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG;

char text_lines[OS_USER_CORE_TEXT_LINE_COUNT][OS_USER_CORE_TEXT_LINE_CAPACITY_BYTES + 1ULL]{};
uint16_t text_line_lengths[OS_USER_CORE_TEXT_LINE_COUNT]{};
char traversal_paths[OS_USER_CORE_TRAVERSAL_PATH_COUNT]
                    [OS_USER_CORE_TRAVERSAL_PATH_CAPACITY_BYTES]{};
uint16_t traversal_path_lengths[OS_USER_CORE_TRAVERSAL_PATH_COUNT]{};
char traversal_current_path[OS_USER_CORE_TRAVERSAL_PATH_CAPACITY_BYTES]{};

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

[[nodiscard]] uint64_t NormalizePathSeparators(const char *const path,
                                               const uint64_t path_length_bytes,
                                               char *const normalized_path) noexcept {
    if (path == nullptr || normalized_path == nullptr ||
        path_length_bytes > OS_USER_CORE_PATH_CAPACITY_BYTES) {
        return OS_USER_CORE_EMPTY_VALUE;
    }
    uint64_t normalized_length_bytes = OS_USER_CORE_EMPTY_VALUE;
    bool previous_was_separator = false;
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < path_length_bytes;
         ++byte_index) {
        const bool is_separator = path[byte_index] == OS_USER_CORE_PATH_SEPARATOR;
        if (is_separator && previous_was_separator) {
            continue;
        }
        normalized_path[normalized_length_bytes] = path[byte_index];
        ++normalized_length_bytes;
        previous_was_separator = is_separator;
    }
    return normalized_length_bytes;
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
[[nodiscard]] bool EqualsLiteral(const char *const actual, const uint64_t actual_length_bytes,
                                 const char (&expected)[SizeBytes]) noexcept {
    return BytesEqual(actual, actual_length_bytes, expected,
                      SizeBytes - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES);
}

[[nodiscard]] bool WriteBytes(const uint64_t descriptor, const uint8_t *const bytes,
                              const uint64_t length_bytes) noexcept {
    return bytes != nullptr && os::user::WriteDescriptor(descriptor, bytes, length_bytes) ==
                                   static_cast<int64_t>(length_bytes);
}

[[nodiscard]] bool WriteText(const uint64_t descriptor, const char *const text,
                             const uint64_t length_bytes) noexcept {
    return WriteBytes(descriptor, reinterpret_cast<const uint8_t *>(text), length_bytes);
}

template <uint64_t SizeBytes>
[[nodiscard]] bool WriteLiteral(const uint64_t descriptor,
                                const char (&literal)[SizeBytes]) noexcept {
    return WriteText(descriptor, literal, SizeBytes - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES);
}

[[nodiscard]] bool WriteDecimal(const uint64_t descriptor, uint64_t value) noexcept {
    char digits[OS_USER_CORE_DECIMAL_CAPACITY_BYTES]{};
    uint64_t digit_count = OS_USER_CORE_EMPTY_VALUE;
    do {
        digits[digit_count] = static_cast<char>(OS_USER_CORE_FIRST_DECIMAL_CHARACTER +
                                                value % OS_USER_CORE_DECIMAL_RADIX);
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

[[nodiscard]] bool WritePaddedDecimal(const uint64_t descriptor, uint64_t value,
                                      const uint64_t width) noexcept {
    if (width == OS_USER_CORE_EMPTY_VALUE || width > OS_USER_CORE_DECIMAL_CAPACITY_BYTES) {
        return false;
    }
    char output[OS_USER_CORE_DECIMAL_CAPACITY_BYTES]{};
    for (uint64_t index = OS_USER_CORE_EMPTY_VALUE; index < width; ++index) {
        output[width - index - OS_USER_CORE_FIRST_VALUE] = static_cast<char>(
            OS_USER_CORE_FIRST_DECIMAL_CHARACTER + value % OS_USER_CORE_DECIMAL_RADIX);
        value /= OS_USER_CORE_DECIMAL_RADIX;
    }
    return value == OS_USER_CORE_EMPTY_VALUE && WriteText(descriptor, output, width);
}

[[nodiscard]] bool WriteHexadecimal(const uint64_t descriptor, const uint64_t value,
                                    const uint64_t digit_count) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    if (digit_count == OS_USER_CORE_EMPTY_VALUE || digit_count > 16ULL) {
        return false;
    }
    char output[16]{};
    for (uint64_t digit_index = OS_USER_CORE_EMPTY_VALUE; digit_index < digit_count;
         ++digit_index) {
        const uint64_t shift = (digit_count - digit_index - OS_USER_CORE_FIRST_VALUE) * 4ULL;
        output[digit_index] = digits[(value >> shift) & 0x0FULL];
    }
    return WriteText(descriptor, output, digit_count);
}

struct InputSource final {
    uint64_t descriptor;
    bool close_descriptor;
};

[[nodiscard]] bool OpenOptionalInput(const uint64_t argument_count,
                                     const char *const *const arguments,
                                     const uint64_t maximum_argument_count,
                                     const uint64_t path_argument_index,
                                     InputSource &source) noexcept {
    source = InputSource{
        .descriptor = os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR,
        .close_descriptor = false,
    };
    if (arguments == nullptr || argument_count > maximum_argument_count) {
        return false;
    }
    if (argument_count <= path_argument_index) {
        return true;
    }
    const int64_t descriptor = os::user::OpenFile(arguments[path_argument_index],
                                                  StringLength(arguments[path_argument_index]),
                                                  OS_USER_CORE_READ_OPEN_FLAGS);
    if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
        return false;
    }
    source.descriptor = static_cast<uint64_t>(descriptor);
    source.close_descriptor = true;
    return true;
}

[[nodiscard]] bool CloseInput(const InputSource &source) noexcept {
    return !source.close_descriptor ||
           os::user::CloseDescriptor(source.descriptor) == OS_USER_CORE_SUCCESS_RESULT;
}

[[nodiscard]] bool ContainsBytes(const char *const haystack, const uint64_t haystack_length_bytes,
                                 const char *const needle,
                                 const uint64_t needle_length_bytes) noexcept {
    if (haystack == nullptr || needle == nullptr || needle_length_bytes > haystack_length_bytes) {
        return false;
    }
    for (uint64_t offset = OS_USER_CORE_EMPTY_VALUE;
         offset + needle_length_bytes <= haystack_length_bytes; ++offset) {
        bool matches = true;
        for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < needle_length_bytes;
             ++byte_index) {
            matches = matches && haystack[offset + byte_index] == needle[byte_index];
        }
        if (matches) {
            return true;
        }
    }
    return needle_length_bytes == OS_USER_CORE_EMPTY_VALUE;
}

[[nodiscard]] int64_t CompareLines(const uint64_t first_index,
                                   const uint64_t second_index) noexcept {
    const uint64_t common_length = text_line_lengths[first_index] < text_line_lengths[second_index]
                                       ? text_line_lengths[first_index]
                                       : text_line_lengths[second_index];
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < common_length; ++byte_index) {
        if (text_lines[first_index][byte_index] < text_lines[second_index][byte_index]) {
            return -1LL;
        }
        if (text_lines[first_index][byte_index] > text_lines[second_index][byte_index]) {
            return 1LL;
        }
    }
    return text_line_lengths[first_index] < text_line_lengths[second_index]    ? -1LL
           : text_line_lengths[first_index] == text_line_lengths[second_index] ? 0LL
                                                                               : 1LL;
}

[[nodiscard]] bool ParseDecimal(const char *const text, const uint64_t length_bytes,
                                uint64_t &value) noexcept {
    value = OS_USER_CORE_EMPTY_VALUE;
    if (text == nullptr || length_bytes == OS_USER_CORE_EMPTY_VALUE) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < length_bytes; ++byte_index) {
        const char character = text[byte_index];
        if (character < OS_USER_CORE_FIRST_DECIMAL_CHARACTER ||
            character > OS_USER_CORE_LAST_DECIMAL_CHARACTER) {
            return false;
        }
        const uint64_t digit =
            static_cast<uint64_t>(character - OS_USER_CORE_FIRST_DECIMAL_CHARACTER);
        if (value > (OS_USER_CORE_MAXIMUM_UNSIGNED_VALUE - digit) / OS_USER_CORE_DECIMAL_RADIX) {
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
                                     const bool has_second_destination, const uint64_t line_limit,
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
            (has_second_destination && !WriteBytes(second_destination, buffer, output_bytes))) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
    }
    return OS_USER_CORE_SUCCESS_EXIT_CODE;
}

[[nodiscard]] int64_t RunHelp(const uint64_t argument_count) noexcept {
    return argument_count == OS_USER_CORE_FIRST_VALUE &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_HELP_TEXT)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunEcho(const uint64_t descriptor, const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    for (uint64_t argument_index = OS_USER_CORE_FIRST_ARGUMENT_INDEX;
         argument_index < argument_count; ++argument_index) {
        if (argument_index != OS_USER_CORE_FIRST_ARGUMENT_INDEX &&
            !WriteLiteral(descriptor, OS_USER_CORE_SPACE)) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
        if (!WriteText(descriptor, arguments[argument_index],
                       StringLength(arguments[argument_index]))) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
    }
    return WriteLiteral(descriptor, OS_USER_CORE_NEWLINE) ? OS_USER_CORE_SUCCESS_EXIT_CODE
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
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunCat(const uint64_t argument_count,
                             const char *const *const arguments) noexcept {
    if (argument_count == OS_USER_CORE_FIRST_VALUE) {
        return CopyDescriptor(os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR,
                              os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_EMPTY_VALUE,
                              false, OS_USER_CORE_EMPTY_VALUE, false);
    }
    for (uint64_t argument_index = OS_USER_CORE_FIRST_ARGUMENT_INDEX;
         argument_index < argument_count; ++argument_index) {
        const int64_t descriptor =
            os::user::OpenFile(arguments[argument_index], StringLength(arguments[argument_index]),
                               OS_USER_CORE_READ_OPEN_FLAGS);
        if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
        const int64_t copy_result = CopyDescriptor(
            static_cast<uint64_t>(descriptor), os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
            OS_USER_CORE_EMPTY_VALUE, false, OS_USER_CORE_EMPTY_VALUE, false);
        const int64_t close_result = os::user::CloseDescriptor(static_cast<uint64_t>(descriptor));
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
    if (close_descriptor && os::user::CloseDescriptor(descriptor) != OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    return result;
}

[[nodiscard]] int64_t RunTee(const uint64_t argument_count,
                             const char *const *const arguments) noexcept {
    if (argument_count != OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t descriptor = os::user::OpenFile(
        arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
        StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]), OS_USER_CORE_WRITE_OPEN_FLAGS);
    if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t copy_result = CopyDescriptor(
        os::abi::OS_ABI_STANDARD_INPUT_DESCRIPTOR, os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
        static_cast<uint64_t>(descriptor), true, OS_USER_CORE_EMPTY_VALUE, false);
    const int64_t close_result = os::user::CloseDescriptor(static_cast<uint64_t>(descriptor));
    return copy_result == OS_USER_CORE_SUCCESS_RESULT && close_result == OS_USER_CORE_SUCCESS_RESULT
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
            const bool whitespace = character == OS_USER_CORE_SPACE_CHARACTER ||
                                    character == OS_USER_CORE_NEWLINE_CHARACTER ||
                                    character == OS_USER_CORE_TAB_CHARACTER ||
                                    character == OS_USER_CORE_CARRIAGE_RETURN_CHARACTER;
            if (!whitespace && !inside_word) {
                ++word_count;
            }
            inside_word = !whitespace;
        }
    }
    if (close_descriptor && os::user::CloseDescriptor(descriptor) != OS_USER_CORE_SUCCESS_RESULT) {
        succeeded = false;
    }
    succeeded =
        succeeded && WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, line_count) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_WC_SEPARATOR) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, word_count) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_WC_SEPARATOR) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, byte_count) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE);
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
            !WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)) {
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
    const char *const type_name = information.type == os::abi::DirectoryEntryType::RegularFile
                                      ? OS_USER_CORE_STAT_FILE_TYPE
                                      : OS_USER_CORE_STAT_DIRECTORY_TYPE;
    const uint64_t type_name_length_bytes =
        information.type == os::abi::DirectoryEntryType::RegularFile
            ? sizeof(OS_USER_CORE_STAT_FILE_TYPE) - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES
            : sizeof(OS_USER_CORE_STAT_DIRECTORY_TYPE) - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES;
    const bool succeeded =
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_STAT_INODE_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.inode_number) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_STAT_TYPE_PREFIX) &&
        WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, type_name, type_name_length_bytes) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_STAT_SIZE_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.size_bytes) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_ALLOCATED_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     information.allocated_size_bytes) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_STAT_LINKS_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.link_count) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_ACCESS_TIME_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     information.access_time_nanoseconds) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_MODIFICATION_TIME_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     information.modification_time_nanoseconds) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_CHANGE_TIME_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     information.change_time_nanoseconds) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     OS_USER_CORE_STAT_BIRTH_TIME_PREFIX) &&
        WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                     information.birth_time_nanoseconds) &&
        WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE);
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunWrite(const uint64_t argument_count,
                               const char *const *const arguments) noexcept {
    if (argument_count < OS_USER_CORE_TWO_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t descriptor = os::user::OpenFile(
        arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
        StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]), OS_USER_CORE_WRITE_OPEN_FLAGS);
    if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    bool succeeded = true;
    for (uint64_t argument_index = OS_USER_CORE_SECOND_ARGUMENT_INDEX;
         argument_index < argument_count && succeeded; ++argument_index) {
        succeeded = (argument_index == OS_USER_CORE_SECOND_ARGUMENT_INDEX ||
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
    const int64_t descriptor = os::user::OpenFile(
        arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
        StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]), OS_USER_CORE_TOUCH_OPEN_FLAGS);
    return descriptor >= OS_USER_CORE_SUCCESS_RESULT &&
                   os::user::CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
                       OS_USER_CORE_SUCCESS_RESULT
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunFixedFile(const uint64_t argument_count, const char *const path,
                                   const uint64_t path_length_bytes) noexcept {
    if (argument_count != OS_USER_CORE_NO_OPERAND_ARGUMENT_COUNT || path == nullptr) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t descriptor =
        os::user::OpenFile(path, path_length_bytes, OS_USER_CORE_READ_OPEN_FLAGS);
    if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t copy_result = CopyDescriptor(
        static_cast<uint64_t>(descriptor), os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
        OS_USER_CORE_EMPTY_VALUE, false, OS_USER_CORE_EMPTY_VALUE, false);
    const int64_t close_result = os::user::CloseDescriptor(static_cast<uint64_t>(descriptor));
    return copy_result == OS_USER_CORE_SUCCESS_RESULT && close_result == OS_USER_CORE_SUCCESS_RESULT
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

template <uint64_t SizeBytes>
[[nodiscard]] int64_t RunFixedFile(const uint64_t argument_count,
                                   const char (&path)[SizeBytes]) noexcept {
    return RunFixedFile(argument_count, path,
                        SizeBytes - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES);
}

[[nodiscard]] int64_t RunBaseName(const uint64_t argument_count,
                                  const char *const *const arguments) noexcept {
    if (argument_count != OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const char *const path = arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX];
    uint64_t path_length_bytes = StringLength(path);
    while (path_length_bytes > OS_USER_CORE_FIRST_VALUE &&
           path[path_length_bytes - OS_USER_CORE_FIRST_VALUE] == OS_USER_CORE_PATH_SEPARATOR) {
        --path_length_bytes;
    }
    if (path_length_bytes == OS_USER_CORE_FIRST_VALUE &&
        path[OS_USER_CORE_EMPTY_VALUE] == OS_USER_CORE_PATH_SEPARATOR) {
        return WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                            OS_USER_CORE_DIRECTORY_SUFFIX) &&
                       WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                    OS_USER_CORE_NEWLINE)
                   ? OS_USER_CORE_SUCCESS_EXIT_CODE
                   : OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    uint64_t name_offset = OS_USER_CORE_EMPTY_VALUE;
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < path_length_bytes;
         ++byte_index) {
        if (path[byte_index] == OS_USER_CORE_PATH_SEPARATOR) {
            name_offset = byte_index + OS_USER_CORE_FIRST_VALUE;
        }
    }
    const uint64_t name_length_bytes = path_length_bytes - name_offset;
    return WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, path + name_offset,
                     name_length_bytes) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunDirectoryName(const uint64_t argument_count,
                                       const char *const *const arguments) noexcept {
    if (argument_count != OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const char *const path = arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX];
    uint64_t path_length_bytes = StringLength(path);
    while (path_length_bytes > OS_USER_CORE_FIRST_VALUE &&
           path[path_length_bytes - OS_USER_CORE_FIRST_VALUE] == OS_USER_CORE_PATH_SEPARATOR) {
        --path_length_bytes;
    }
    uint64_t separator_offset = OS_USER_CORE_EMPTY_VALUE;
    bool separator_found = false;
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < path_length_bytes;
         ++byte_index) {
        if (path[byte_index] == OS_USER_CORE_PATH_SEPARATOR) {
            separator_offset = byte_index;
            separator_found = true;
        }
    }
    const char *directory = OS_USER_CORE_CURRENT_DIRECTORY;
    uint64_t directory_length_bytes =
        sizeof(OS_USER_CORE_CURRENT_DIRECTORY) - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES;
    char normalized_directory[OS_USER_CORE_PATH_CAPACITY_BYTES]{};
    if (separator_found) {
        directory_length_bytes = separator_offset == OS_USER_CORE_EMPTY_VALUE
                                     ? OS_USER_CORE_FIRST_VALUE
                                     : separator_offset;
        while (directory_length_bytes > OS_USER_CORE_FIRST_VALUE &&
               path[directory_length_bytes - OS_USER_CORE_FIRST_VALUE] ==
                   OS_USER_CORE_PATH_SEPARATOR) {
            --directory_length_bytes;
        }
        directory_length_bytes =
            NormalizePathSeparators(path, directory_length_bytes, normalized_directory);
        directory = normalized_directory;
    }
    return WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, directory,
                     directory_length_bytes) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunCopy(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    if (argument_count != OS_USER_CORE_TWO_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const char *const source_path = arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX];
    const char *const destination_path = arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX];
    const uint64_t source_path_length_bytes = StringLength(source_path);
    const uint64_t destination_path_length_bytes = StringLength(destination_path);
    os::abi::FileInformation source_information{};
    if (os::user::StatFile(source_path, source_path_length_bytes, source_information) !=
        OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    os::abi::FileInformation destination_information{};
    if (os::user::StatFile(destination_path, destination_path_length_bytes,
                           destination_information) == OS_USER_CORE_SUCCESS_RESULT &&
        source_information.mount_identifier == destination_information.mount_identifier &&
        source_information.superblock_identifier == destination_information.superblock_identifier &&
        source_information.inode_number == destination_information.inode_number &&
        source_information.generation == destination_information.generation) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t source_descriptor =
        os::user::OpenFile(source_path, source_path_length_bytes, OS_USER_CORE_READ_OPEN_FLAGS);
    if (source_descriptor < OS_USER_CORE_SUCCESS_RESULT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const int64_t destination_descriptor = os::user::OpenFile(
        destination_path, destination_path_length_bytes, OS_USER_CORE_WRITE_OPEN_FLAGS);
    bool succeeded = destination_descriptor >= OS_USER_CORE_SUCCESS_RESULT;
    if (succeeded) {
        succeeded =
            CopyDescriptor(static_cast<uint64_t>(source_descriptor),
                           static_cast<uint64_t>(destination_descriptor), OS_USER_CORE_EMPTY_VALUE,
                           false, OS_USER_CORE_EMPTY_VALUE, false) == OS_USER_CORE_SUCCESS_RESULT;
        succeeded = os::user::CloseDescriptor(static_cast<uint64_t>(destination_descriptor)) ==
                        OS_USER_CORE_SUCCESS_RESULT &&
                    succeeded;
    }
    succeeded = os::user::CloseDescriptor(static_cast<uint64_t>(source_descriptor)) ==
                    OS_USER_CORE_SUCCESS_RESULT &&
                succeeded;
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunSequence(const uint64_t argument_count,
                                  const char *const *const arguments) noexcept {
    uint64_t first_value = OS_USER_CORE_FIRST_VALUE;
    uint64_t last_value = OS_USER_CORE_EMPTY_VALUE;
    if (argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        if (!ParseDecimal(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                          StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]), last_value)) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
    } else if (argument_count == OS_USER_CORE_TWO_ARGUMENT_COUNT) {
        if (!ParseDecimal(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                          StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                          first_value) ||
            !ParseDecimal(arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX],
                          StringLength(arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX]),
                          last_value)) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
    } else {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    if (last_value < first_value ||
        last_value - first_value >= OS_USER_CORE_SEQUENCE_MAXIMUM_VALUE_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    for (uint64_t value = first_value;; ++value) {
        if (!WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, value) ||
            !WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
        if (value == last_value) {
            break;
        }
    }
    return OS_USER_CORE_SUCCESS_EXIT_CODE;
}

[[nodiscard]] int64_t RunSleep(const uint64_t argument_count,
                               const char *const *const arguments) noexcept {
    uint64_t milliseconds = OS_USER_CORE_EMPTY_VALUE;
    if (argument_count != OS_USER_CORE_SINGLE_ARGUMENT_COUNT ||
        !ParseDecimal(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                      StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]), milliseconds) ||
        milliseconds >
            OS_USER_CORE_MAXIMUM_UNSIGNED_VALUE / OS_USER_CORE_NANOSECONDS_PER_MILLISECOND) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    return os::user::SleepFor(milliseconds * OS_USER_CORE_NANOSECONDS_PER_MILLISECOND) ==
                   OS_USER_CORE_SUCCESS_RESULT
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunKill(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    uint64_t process_id = OS_USER_CORE_EMPTY_VALUE;
    uint64_t signal_number = OS_USER_CORE_EMPTY_VALUE;
    return argument_count == OS_USER_CORE_TWO_ARGUMENT_COUNT &&
                   ParseDecimal(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX],
                                StringLength(arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]),
                                process_id) &&
                   ParseDecimal(arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX],
                                StringLength(arguments[OS_USER_CORE_SECOND_ARGUMENT_INDEX]),
                                signal_number) &&
                   os::user::SendProcessSignal(process_id, signal_number) ==
                       OS_USER_CORE_SUCCESS_RESULT
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunIdentity(const uint64_t argument_count) noexcept {
    return argument_count == OS_USER_CORE_NO_OPERAND_ARGUMENT_COUNT &&
                   WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                os::user::GetProcessId()) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunEnvironment(const uint64_t argument_count,
                                     const char *const *const environment) noexcept {
    if (argument_count != OS_USER_CORE_NO_OPERAND_ARGUMENT_COUNT || environment == nullptr) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    for (uint64_t environment_index = OS_USER_CORE_EMPTY_VALUE;
         environment_index <= os::abi::OS_ABI_PROCESS_MAXIMUM_ENVIRONMENT_COUNT;
         ++environment_index) {
        const char *const entry = environment[environment_index];
        if (entry == nullptr) {
            return OS_USER_CORE_SUCCESS_EXIT_CODE;
        }
        if (environment_index == os::abi::OS_ABI_PROCESS_MAXIMUM_ENVIRONMENT_COUNT ||
            !WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, entry, StringLength(entry)) ||
            !WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)) {
            return OS_USER_CORE_FAILURE_EXIT_CODE;
        }
    }
    return OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunGrep(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    if (argument_count < OS_USER_CORE_SINGLE_ARGUMENT_COUNT ||
        argument_count > OS_USER_CORE_TWO_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    InputSource source{};
    if (!OpenOptionalInput(argument_count, arguments, OS_USER_CORE_TWO_ARGUMENT_COUNT,
                           OS_USER_CORE_SECOND_ARGUMENT_INDEX, source)) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const char *const pattern = arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX];
    const uint64_t pattern_length_bytes = StringLength(pattern);
    uint8_t buffer[OS_USER_CORE_TRANSFER_SIZE_BYTES]{};
    uint64_t line_length_bytes = OS_USER_CORE_EMPTY_VALUE;
    bool succeeded = true;
    bool matched = false;
    while (succeeded) {
        const int64_t read_result =
            os::user::ReadDescriptor(source.descriptor, buffer, sizeof(buffer));
        if (read_result < OS_USER_CORE_SUCCESS_RESULT) {
            succeeded = false;
            break;
        }
        if (read_result == OS_USER_CORE_SUCCESS_RESULT) {
            if (line_length_bytes != OS_USER_CORE_EMPTY_VALUE &&
                ContainsBytes(text_lines[0], line_length_bytes, pattern, pattern_length_bytes)) {
                succeeded =
                    WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, text_lines[0],
                              line_length_bytes) &&
                    WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE);
                matched = matched || succeeded;
            }
            break;
        }
        for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE;
             byte_index < static_cast<uint64_t>(read_result) && succeeded; ++byte_index) {
            if (buffer[byte_index] == OS_USER_CORE_NEWLINE_CHARACTER) {
                if (ContainsBytes(text_lines[0], line_length_bytes, pattern,
                                  pattern_length_bytes)) {
                    succeeded = WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, text_lines[0],
                                          line_length_bytes) &&
                                WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                             OS_USER_CORE_NEWLINE);
                    matched = matched || succeeded;
                }
                line_length_bytes = OS_USER_CORE_EMPTY_VALUE;
                continue;
            }
            if (line_length_bytes >= OS_USER_CORE_TEXT_LINE_CAPACITY_BYTES) {
                succeeded = false;
                break;
            }
            text_lines[0][line_length_bytes] = static_cast<char>(buffer[byte_index]);
            ++line_length_bytes;
        }
    }
    succeeded = CloseInput(source) && succeeded;
    return succeeded && matched ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] bool ReadBoundedLines(const InputSource &source, uint64_t &line_count) noexcept {
    line_count = OS_USER_CORE_EMPTY_VALUE;
    uint64_t line_length_bytes = OS_USER_CORE_EMPTY_VALUE;
    uint8_t buffer[OS_USER_CORE_TRANSFER_SIZE_BYTES]{};
    while (true) {
        const int64_t read_result =
            os::user::ReadDescriptor(source.descriptor, buffer, sizeof(buffer));
        if (read_result < OS_USER_CORE_SUCCESS_RESULT) {
            return false;
        }
        if (read_result == OS_USER_CORE_SUCCESS_RESULT) {
            if (line_length_bytes != OS_USER_CORE_EMPTY_VALUE) {
                if (line_count >= OS_USER_CORE_TEXT_LINE_COUNT) {
                    return false;
                }
                text_line_lengths[line_count] = static_cast<uint16_t>(line_length_bytes);
                ++line_count;
            }
            return true;
        }
        for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE;
             byte_index < static_cast<uint64_t>(read_result); ++byte_index) {
            if (buffer[byte_index] == OS_USER_CORE_NEWLINE_CHARACTER) {
                if (line_count >= OS_USER_CORE_TEXT_LINE_COUNT) {
                    return false;
                }
                text_lines[line_count][line_length_bytes] = OS_USER_CORE_STRING_TERMINATOR;
                text_line_lengths[line_count] = static_cast<uint16_t>(line_length_bytes);
                ++line_count;
                line_length_bytes = OS_USER_CORE_EMPTY_VALUE;
                continue;
            }
            if (line_length_bytes >= OS_USER_CORE_TEXT_LINE_CAPACITY_BYTES) {
                return false;
            }
            if (line_count >= OS_USER_CORE_TEXT_LINE_COUNT) {
                return false;
            }
            text_lines[line_count][line_length_bytes] = static_cast<char>(buffer[byte_index]);
            ++line_length_bytes;
        }
    }
}

[[nodiscard]] int64_t RunSort(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    InputSource source{};
    if (!OpenOptionalInput(argument_count, arguments, OS_USER_CORE_SINGLE_ARGUMENT_COUNT,
                           OS_USER_CORE_FIRST_ARGUMENT_INDEX, source)) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    for (uint64_t line_index = OS_USER_CORE_EMPTY_VALUE; line_index < OS_USER_CORE_TEXT_LINE_COUNT;
         ++line_index) {
        text_line_lengths[line_index] = 0U;
        text_lines[line_index][0] = OS_USER_CORE_STRING_TERMINATOR;
    }
    uint64_t line_count = OS_USER_CORE_EMPTY_VALUE;
    bool succeeded = ReadBoundedLines(source, line_count) && CloseInput(source);
    for (uint64_t insertion_index = OS_USER_CORE_FIRST_VALUE;
         insertion_index < line_count && succeeded; ++insertion_index) {
        uint64_t destination_index = insertion_index;
        while (destination_index > OS_USER_CORE_EMPTY_VALUE &&
               CompareLines(destination_index - OS_USER_CORE_FIRST_VALUE, destination_index) >
                   0LL) {
            for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE;
                 byte_index <= OS_USER_CORE_TEXT_LINE_CAPACITY_BYTES; ++byte_index) {
                const char temporary = text_lines[destination_index - 1ULL][byte_index];
                text_lines[destination_index - 1ULL][byte_index] =
                    text_lines[destination_index][byte_index];
                text_lines[destination_index][byte_index] = temporary;
            }
            const uint16_t temporary_length = text_line_lengths[destination_index - 1ULL];
            text_line_lengths[destination_index - 1ULL] = text_line_lengths[destination_index];
            text_line_lengths[destination_index] = temporary_length;
            --destination_index;
        }
    }
    for (uint64_t line_index = OS_USER_CORE_EMPTY_VALUE; line_index < line_count && succeeded;
         ++line_index) {
        succeeded = WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, text_lines[line_index],
                              text_line_lengths[line_index]) &&
                    WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE);
    }
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunTail(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    InputSource source{};
    if (!OpenOptionalInput(argument_count, arguments, OS_USER_CORE_SINGLE_ARGUMENT_COUNT,
                           OS_USER_CORE_FIRST_ARGUMENT_INDEX, source)) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    uint64_t line_count = OS_USER_CORE_EMPTY_VALUE;
    uint64_t line_length_bytes = OS_USER_CORE_EMPTY_VALUE;
    uint8_t buffer[OS_USER_CORE_TRANSFER_SIZE_BYTES]{};
    bool succeeded = true;
    while (succeeded) {
        const int64_t read_result =
            os::user::ReadDescriptor(source.descriptor, buffer, sizeof(buffer));
        if (read_result < OS_USER_CORE_SUCCESS_RESULT) {
            succeeded = false;
            break;
        }
        if (read_result == OS_USER_CORE_SUCCESS_RESULT) {
            if (line_length_bytes != OS_USER_CORE_EMPTY_VALUE) {
                text_line_lengths[line_count % OS_USER_CORE_TAIL_LINE_COUNT] =
                    static_cast<uint16_t>(line_length_bytes);
                ++line_count;
            }
            break;
        }
        for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE;
             byte_index < static_cast<uint64_t>(read_result) && succeeded; ++byte_index) {
            const uint64_t slot = line_count % OS_USER_CORE_TAIL_LINE_COUNT;
            if (buffer[byte_index] == OS_USER_CORE_NEWLINE_CHARACTER) {
                text_line_lengths[slot] = static_cast<uint16_t>(line_length_bytes);
                ++line_count;
                line_length_bytes = OS_USER_CORE_EMPTY_VALUE;
            } else if (line_length_bytes >= OS_USER_CORE_TEXT_LINE_CAPACITY_BYTES) {
                succeeded = false;
            } else {
                text_lines[slot][line_length_bytes] = static_cast<char>(buffer[byte_index]);
                ++line_length_bytes;
            }
        }
    }
    succeeded = CloseInput(source) && succeeded;
    const uint64_t output_count =
        line_count < OS_USER_CORE_TAIL_LINE_COUNT ? line_count : OS_USER_CORE_TAIL_LINE_COUNT;
    const uint64_t first_line = line_count - output_count;
    for (uint64_t line_offset = OS_USER_CORE_EMPTY_VALUE; line_offset < output_count && succeeded;
         ++line_offset) {
        const uint64_t slot = (first_line + line_offset) % OS_USER_CORE_TAIL_LINE_COUNT;
        succeeded = WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, text_lines[slot],
                              text_line_lengths[slot]) &&
                    WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE);
    }
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] bool IsDotEntry(const os::abi::DirectoryEntry &entry) noexcept {
    return (entry.name_length_bytes == 1ULL && entry.name[0] == '.') ||
           (entry.name_length_bytes == 2ULL && entry.name[0] == '.' && entry.name[1] == '.');
}

[[nodiscard]] bool PushTraversalPath(const char *const path, const uint64_t path_length_bytes,
                                     uint64_t &path_count) noexcept {
    if (path == nullptr || path_length_bytes >= OS_USER_CORE_TRAVERSAL_PATH_CAPACITY_BYTES ||
        path_count >= OS_USER_CORE_TRAVERSAL_PATH_COUNT) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < path_length_bytes;
         ++byte_index) {
        traversal_paths[path_count][byte_index] = path[byte_index];
    }
    traversal_paths[path_count][path_length_bytes] = OS_USER_CORE_STRING_TERMINATOR;
    traversal_path_lengths[path_count] = static_cast<uint16_t>(path_length_bytes);
    ++path_count;
    return true;
}

[[nodiscard]] bool TraversePath(const char *const root, const uint64_t root_length_bytes,
                                const bool write_paths, const bool stay_on_superblock,
                                uint64_t &allocated_size_bytes) noexcept {
    allocated_size_bytes = OS_USER_CORE_EMPTY_VALUE;
    uint64_t path_count = OS_USER_CORE_EMPTY_VALUE;
    if (!PushTraversalPath(root, root_length_bytes, path_count)) {
        return false;
    }
    os::abi::FileInformation root_information{};
    if (os::user::StatFile(root, root_length_bytes, root_information) !=
        OS_USER_CORE_SUCCESS_RESULT) {
        return false;
    }
    while (path_count != OS_USER_CORE_EMPTY_VALUE) {
        --path_count;
        const uint64_t path_length_bytes = traversal_path_lengths[path_count];
        for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < path_length_bytes;
             ++byte_index) {
            traversal_current_path[byte_index] = traversal_paths[path_count][byte_index];
        }
        traversal_current_path[path_length_bytes] = OS_USER_CORE_STRING_TERMINATOR;
        const char *const path = traversal_current_path;
        os::abi::FileInformation information{};
        if (os::user::StatFile(path, path_length_bytes, information) !=
            OS_USER_CORE_SUCCESS_RESULT) {
            return false;
        }
        if (stay_on_superblock &&
            information.superblock_identifier != root_information.superblock_identifier) {
            continue;
        }
        if (allocated_size_bytes >
            OS_USER_CORE_MAXIMUM_UNSIGNED_VALUE - information.allocated_size_bytes) {
            return false;
        }
        allocated_size_bytes += information.allocated_size_bytes;
        if (write_paths &&
            (!WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, path, path_length_bytes) ||
             !WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE))) {
            return false;
        }
        if (information.type != os::abi::DirectoryEntryType::Directory) {
            continue;
        }
        const int64_t descriptor = os::user::OpenDirectory(path, path_length_bytes);
        if (descriptor < OS_USER_CORE_SUCCESS_RESULT) {
            return false;
        }
        bool directory_succeeded = true;
        while (directory_succeeded) {
            os::abi::DirectoryEntry entry{};
            const int64_t read_result =
                os::user::ReadDirectory(static_cast<uint64_t>(descriptor), entry);
            if (read_result == OS_USER_CORE_SUCCESS_RESULT) {
                break;
            }
            if (read_result != OS_USER_CORE_DIRECTORY_ENTRY_RESULT || IsDotEntry(entry)) {
                directory_succeeded = read_result == OS_USER_CORE_DIRECTORY_ENTRY_RESULT;
                continue;
            }
            const bool append_separator =
                path_length_bytes == OS_USER_CORE_EMPTY_VALUE ||
                path[path_length_bytes - OS_USER_CORE_FIRST_VALUE] != OS_USER_CORE_PATH_SEPARATOR;
            const uint64_t child_length_bytes =
                path_length_bytes +
                (append_separator ? OS_USER_CORE_FIRST_VALUE : OS_USER_CORE_EMPTY_VALUE) +
                entry.name_length_bytes;
            if (child_length_bytes >= OS_USER_CORE_TRAVERSAL_PATH_CAPACITY_BYTES ||
                path_count >= OS_USER_CORE_TRAVERSAL_PATH_COUNT) {
                directory_succeeded = false;
                break;
            }
            char *const child = traversal_paths[path_count];
            for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE; byte_index < path_length_bytes;
                 ++byte_index) {
                child[byte_index] = path[byte_index];
            }
            uint64_t child_index = path_length_bytes;
            if (append_separator) {
                child[child_index] = OS_USER_CORE_PATH_SEPARATOR;
                ++child_index;
            }
            for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE;
                 byte_index < entry.name_length_bytes; ++byte_index) {
                child[child_index + byte_index] = static_cast<char>(entry.name[byte_index]);
            }
            child[child_length_bytes] = OS_USER_CORE_STRING_TERMINATOR;
            traversal_path_lengths[path_count] = static_cast<uint16_t>(child_length_bytes);
            ++path_count;
        }
        const bool closed = os::user::CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
                            OS_USER_CORE_SUCCESS_RESULT;
        if (!directory_succeeded || !closed) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] int64_t RunFind(const uint64_t argument_count,
                              const char *const *const arguments) noexcept {
    if (argument_count > OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const char *const path = argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT
                                 ? arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]
                                 : OS_USER_CORE_CURRENT_DIRECTORY;
    uint64_t allocated_size_bytes = OS_USER_CORE_EMPTY_VALUE;
    return TraversePath(path, StringLength(path), true, false, allocated_size_bytes)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunDiskUsage(const uint64_t argument_count,
                                   const char *const *const arguments) noexcept {
    if (argument_count > OS_USER_CORE_SINGLE_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const char *const path = argument_count == OS_USER_CORE_SINGLE_ARGUMENT_COUNT
                                 ? arguments[OS_USER_CORE_FIRST_ARGUMENT_INDEX]
                                 : OS_USER_CORE_CURRENT_DIRECTORY;
    uint64_t allocated_size_bytes = OS_USER_CORE_EMPTY_VALUE;
    return TraversePath(path, StringLength(path), false, false, allocated_size_bytes) &&
                   WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, allocated_size_bytes) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_SPACE) &&
                   WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, path,
                             StringLength(path)) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunDiskFree(const uint64_t argument_count) noexcept {
    if (argument_count != OS_USER_CORE_NO_OPERAND_ARGUMENT_COUNT) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    uint64_t allocated_size_bytes = OS_USER_CORE_EMPTY_VALUE;
    if (!TraversePath(OS_USER_CORE_ROOT_PATH,
                      sizeof(OS_USER_CORE_ROOT_PATH) - OS_USER_CORE_STRING_TERMINATOR_SIZE_BYTES,
                      false, true, allocated_size_bytes)) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    const uint64_t available_size_bytes =
        allocated_size_bytes < OS_USER_CORE_ROOTFS_REGION_SIZE_BYTES
            ? OS_USER_CORE_ROOTFS_REGION_SIZE_BYTES - allocated_size_bytes
            : OS_USER_CORE_EMPTY_VALUE;
    return WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                        OS_USER_CORE_DISK_TOTAL_PREFIX) &&
                   WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                OS_USER_CORE_ROOTFS_REGION_SIZE_BYTES) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                OS_USER_CORE_DISK_USED_PREFIX) &&
                   WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, allocated_size_bytes) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                OS_USER_CORE_DISK_AVAILABLE_PREFIX) &&
                   WriteDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, available_size_bytes) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunHexdump(const uint64_t argument_count,
                                 const char *const *const arguments) noexcept {
    InputSource source{};
    if (!OpenOptionalInput(argument_count, arguments, OS_USER_CORE_SINGLE_ARGUMENT_COUNT,
                           OS_USER_CORE_FIRST_ARGUMENT_INDEX, source)) {
        return OS_USER_CORE_FAILURE_EXIT_CODE;
    }
    uint8_t line[OS_USER_CORE_HEXDUMP_LINE_SIZE_BYTES]{};
    uint64_t line_length_bytes = OS_USER_CORE_EMPTY_VALUE;
    uint64_t offset_bytes = OS_USER_CORE_EMPTY_VALUE;
    bool succeeded = true;
    while (succeeded) {
        const int64_t read_result =
            os::user::ReadDescriptor(source.descriptor, line + line_length_bytes,
                                     OS_USER_CORE_HEXDUMP_LINE_SIZE_BYTES - line_length_bytes);
        if (read_result < OS_USER_CORE_SUCCESS_RESULT) {
            succeeded = false;
            break;
        }
        line_length_bytes += static_cast<uint64_t>(read_result);
        if (line_length_bytes == OS_USER_CORE_EMPTY_VALUE) {
            break;
        }
        if (line_length_bytes < OS_USER_CORE_HEXDUMP_LINE_SIZE_BYTES &&
            read_result != OS_USER_CORE_SUCCESS_RESULT) {
            continue;
        }
        succeeded =
            WriteHexadecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, offset_bytes, 8ULL) &&
            WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_SPACE);
        for (uint64_t byte_index = OS_USER_CORE_EMPTY_VALUE;
             byte_index < line_length_bytes && succeeded; ++byte_index) {
            succeeded =
                WriteHexadecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, line[byte_index],
                                 2ULL) &&
                WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_SPACE);
        }
        succeeded = succeeded &&
                    WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE);
        offset_bytes += line_length_bytes;
        line_length_bytes = OS_USER_CORE_EMPTY_VALUE;
        if (read_result == OS_USER_CORE_SUCCESS_RESULT) {
            break;
        }
    }
    succeeded = CloseInput(source) && succeeded;
    return succeeded ? OS_USER_CORE_SUCCESS_EXIT_CODE : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunClear(const uint64_t argument_count) noexcept {
    return argument_count == OS_USER_CORE_NO_OPERAND_ARGUMENT_COUNT &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                OS_USER_CORE_CLEAR_SEQUENCE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t RunDate(const uint64_t argument_count) noexcept {
    os::abi::RealtimeInformation information{};
    return argument_count == OS_USER_CORE_NO_OPERAND_ARGUMENT_COUNT &&
                   os::user::GetRealtime(information) == OS_USER_CORE_SUCCESS_RESULT &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                OS_USER_CORE_DATE_UTC_PREFIX) &&
                   WritePaddedDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.year,
                                      4ULL) &&
                   WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, "-", 1ULL) &&
                   WritePaddedDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.month,
                                      2ULL) &&
                   WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, "-", 1ULL) &&
                   WritePaddedDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.day,
                                      2ULL) &&
                   WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, "T", 1ULL) &&
                   WritePaddedDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, information.hour,
                                      2ULL) &&
                   WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, ":", 1ULL) &&
                   WritePaddedDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                      information.minute, 2ULL) &&
                   WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, ":", 1ULL) &&
                   WritePaddedDecimal(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR,
                                      information.second, 2ULL) &&
                   WriteText(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, "Z", 1ULL) &&
                   WriteLiteral(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, OS_USER_CORE_NEWLINE)
               ? OS_USER_CORE_SUCCESS_EXIT_CODE
               : OS_USER_CORE_FAILURE_EXIT_CODE;
}

[[nodiscard]] int64_t DispatchTool(const char *const tool_name,
                                   const uint64_t tool_name_length_bytes,
                                   const uint64_t argument_count,
                                   const char *const *const arguments,
                                   const char *const *const environment) noexcept {
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_HELP_COMMAND)) {
        return RunHelp(argument_count);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_ECHO_COMMAND)) {
        return RunEcho(os::abi::OS_ABI_STANDARD_OUTPUT_DESCRIPTOR, argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_ERROR_COMMAND)) {
        return RunEcho(os::abi::OS_ABI_STANDARD_ERROR_DESCRIPTOR, argument_count, arguments);
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
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_REMOVE_DIRECTORY_COMMAND)) {
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
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_BASENAME_COMMAND)) {
        return RunBaseName(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_DIRNAME_COMMAND)) {
        return RunDirectoryName(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_COPY_COMMAND)) {
        return RunCopy(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_SEQUENCE_COMMAND)) {
        return RunSequence(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_UPTIME_COMMAND)) {
        return RunFixedFile(argument_count, OS_USER_CORE_PROC_UPTIME_PATH);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_PROCESS_COMMAND)) {
        return RunFixedFile(argument_count, OS_USER_CORE_PROC_PROCESSES_PATH);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_FREE_COMMAND)) {
        return RunFixedFile(argument_count, OS_USER_CORE_PROC_MEMORY_INFORMATION_PATH);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_UNAME_COMMAND)) {
        return RunFixedFile(argument_count, OS_USER_CORE_PROC_VERSION_PATH);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_MOUNTS_COMMAND)) {
        return RunFixedFile(argument_count, OS_USER_CORE_PROC_MOUNTS_PATH);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_RESOURCES_COMMAND)) {
        return RunFixedFile(argument_count, OS_USER_CORE_PROC_RESOURCES_PATH);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_SLEEP_COMMAND)) {
        return RunSleep(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_KILL_COMMAND)) {
        return RunKill(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_ID_COMMAND)) {
        return RunIdentity(argument_count);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_ENVIRONMENT_COMMAND)) {
        return RunEnvironment(argument_count, environment);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_GREP_COMMAND)) {
        return RunGrep(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_FIND_COMMAND)) {
        return RunFind(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_SORT_COMMAND)) {
        return RunSort(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_TAIL_COMMAND)) {
        return RunTail(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_DISK_FREE_COMMAND)) {
        return RunDiskFree(argument_count);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_DISK_USAGE_COMMAND)) {
        return RunDiskUsage(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_HEXDUMP_COMMAND)) {
        return RunHexdump(argument_count, arguments);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_CLEAR_COMMAND)) {
        return RunClear(argument_count);
    }
    if (EqualsLiteral(tool_name, tool_name_length_bytes, OS_USER_CORE_DATE_COMMAND)) {
        return RunDate(argument_count);
    }
    static_cast<void>(
        WriteLiteral(os::abi::OS_ABI_STANDARD_ERROR_DESCRIPTOR, OS_USER_CORE_UNKNOWN_TOOL_ERROR));
    return OS_USER_CORE_FAILURE_EXIT_CODE;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count, const char *const *const arguments,
                 const char *const *const environment) noexcept {
    if (argument_count == OS_USER_CORE_EMPTY_VALUE || arguments == nullptr ||
        arguments[OS_USER_CORE_COMMAND_INDEX] == nullptr) {
        os::user::ExitProcess(OS_USER_CORE_FAILURE_EXIT_CODE);
    }
    uint64_t tool_name_length_bytes = OS_USER_CORE_EMPTY_VALUE;
    const char *const tool_name =
        ToolName(arguments[OS_USER_CORE_COMMAND_INDEX], tool_name_length_bytes);
    const int64_t result =
        DispatchTool(tool_name, tool_name_length_bytes, argument_count, arguments, environment);
    if (result != OS_USER_CORE_SUCCESS_EXIT_CODE) {
        static_cast<void>(
            WriteLiteral(os::abi::OS_ABI_STANDARD_ERROR_DESCRIPTOR, OS_USER_CORE_OPERATION_ERROR));
    }
    os::user::ExitProcess(result);
}
