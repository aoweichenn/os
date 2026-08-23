#include "os/user/system_call.hpp"

#include "os/abi/elf.hpp"
#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr uint64_t OS_USER_TOOL_PROBE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_PAGE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_SEQUENTIAL_READ_LIMIT_BYTES =
    OS_USER_TOOL_PROBE_PAGE_SIZE_BYTES * 2ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_REQUIRED_TOOL_COUNT = 47ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_REQUIRED_ARGUMENT_COUNT = 1ULL;
constexpr int64_t OS_USER_TOOL_PROBE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_TOOL_PROBE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_TOOL_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr uint8_t OS_USER_TOOL_PROBE_ELF_MAGIC[]{
    os::abi::OS_ABI_ELF_MAGIC_BYTE_0,
    os::abi::OS_ABI_ELF_MAGIC_BYTE_1,
    os::abi::OS_ABI_ELF_MAGIC_BYTE_2,
    os::abi::OS_ABI_ELF_MAGIC_BYTE_3,
};
constexpr char OS_USER_TOOL_PROBE_VERIFIED_MESSAGE[] = "[OS][USER][TOOLS] ELF_SET_VERIFIED\r\n";
constexpr char OS_USER_TOOL_PROBE_READAHEAD_VERIFIED_MESSAGE[] =
    "[OS][USER][TOOLS] SEQUENTIAL_READAHEAD_VERIFIED\r\n";
constexpr char OS_USER_TOOL_PROBE_ARGUMENT_FAILURE_MESSAGE[] =
    "[OS][USER][TOOLS][FAIL] ARGUMENTS\r\n";
constexpr char OS_USER_TOOL_PROBE_STAT_FAILURE_MESSAGE[] = "[OS][USER][TOOLS][FAIL] STAT\r\n";
constexpr char OS_USER_TOOL_PROBE_OPEN_FAILURE_MESSAGE[] = "[OS][USER][TOOLS][FAIL] OPEN\r\n";
constexpr char OS_USER_TOOL_PROBE_MAGIC_FAILURE_MESSAGE[] =
    "[OS][USER][TOOLS][FAIL] MAGIC_READ\r\n";
constexpr char OS_USER_TOOL_PROBE_SEQUENTIAL_FAILURE_MESSAGE[] =
    "[OS][USER][TOOLS][FAIL] SEQUENTIAL_READ\r\n";
constexpr char OS_USER_TOOL_PROBE_CLOSE_FAILURE_MESSAGE[] = "[OS][USER][TOOLS][FAIL] CLOSE\r\n";

struct ToolPath final {
    const char *bytes;
    uint64_t length_bytes;
};

template <uint64_t SizeBytes>
[[nodiscard]] constexpr ToolPath MakeToolPath(const char (&path)[SizeBytes]) noexcept {
    return ToolPath{
        .bytes = path,
        .length_bytes = SizeBytes - OS_USER_TOOL_PROBE_STRING_TERMINATOR_SIZE_BYTES,
    };
}

constexpr ToolPath OS_USER_TOOL_PROBE_PATHS[]{
    MakeToolPath("/bin/help"),      MakeToolPath("/bin/echo"),     MakeToolPath("/bin/err"),
    MakeToolPath("/bin/cat"),       MakeToolPath("/bin/wc"),       MakeToolPath("/bin/head"),
    MakeToolPath("/bin/tee"),       MakeToolPath("/bin/true"),     MakeToolPath("/bin/false"),
    MakeToolPath("/bin/pwd"),       MakeToolPath("/bin/ls"),       MakeToolPath("/bin/stat"),
    MakeToolPath("/bin/chmod"),     MakeToolPath("/bin/chown"),    MakeToolPath("/bin/ln"),
    MakeToolPath("/bin/readlink"),  MakeToolPath("/bin/mkdir"),    MakeToolPath("/bin/write"),
    MakeToolPath("/bin/touch"),     MakeToolPath("/bin/rm"),       MakeToolPath("/bin/rmdir"),
    MakeToolPath("/bin/mv"),        MakeToolPath("/bin/truncate"), MakeToolPath("/bin/sync"),
    MakeToolPath("/bin/basename"),  MakeToolPath("/bin/dirname"),  MakeToolPath("/bin/cp"),
    MakeToolPath("/bin/seq"),       MakeToolPath("/bin/uptime"),   MakeToolPath("/bin/ps"),
    MakeToolPath("/bin/free"),      MakeToolPath("/bin/uname"),    MakeToolPath("/bin/mounts"),
    MakeToolPath("/bin/resources"), MakeToolPath("/bin/sleep"),    MakeToolPath("/bin/kill"),
    MakeToolPath("/bin/id"),        MakeToolPath("/bin/env"),      MakeToolPath("/bin/grep"),
    MakeToolPath("/bin/find"),      MakeToolPath("/bin/sort"),     MakeToolPath("/bin/tail"),
    MakeToolPath("/bin/df"),        MakeToolPath("/bin/du"),       MakeToolPath("/bin/hexdump"),
    MakeToolPath("/bin/clear"),     MakeToolPath("/bin/date"),
};
constexpr ToolPath OS_USER_TOOL_PROBE_READAHEAD_PATH = MakeToolPath("/bin/smoke");

static_assert(sizeof(OS_USER_TOOL_PROBE_PATHS) /
                      sizeof(OS_USER_TOOL_PROBE_PATHS[OS_USER_TOOL_PROBE_EMPTY_VALUE]) ==
                  OS_USER_TOOL_PROBE_REQUIRED_TOOL_COUNT,
              "用户工具验收清单必须精确覆盖 47 个独立 ELF 路径");

[[nodiscard]] bool MagicIsValid(const uint8_t *const magic) noexcept {
    for (uint64_t byte_index = OS_USER_TOOL_PROBE_EMPTY_VALUE;
         byte_index < OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES; ++byte_index) {
        if (magic[byte_index] != OS_USER_TOOL_PROBE_ELF_MAGIC[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool InodeWasAlreadyObserved(const uint64_t *const inode_numbers,
                                           const uint64_t observed_count,
                                           const uint64_t inode_number) noexcept {
    for (uint64_t inode_index = OS_USER_TOOL_PROBE_EMPTY_VALUE; inode_index < observed_count;
         ++inode_index) {
        if (inode_numbers[inode_index] == inode_number) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

template <uint64_t SizeBytes>
[[noreturn]] void ExitFailure(const char (&message)[SizeBytes]) noexcept {
    static_cast<void>(
        os::user::WriteLog(message, SizeBytes - OS_USER_TOOL_PROBE_STRING_TERMINATOR_SIZE_BYTES));
    os::user::ExitProcess(OS_USER_TOOL_PROBE_FAILURE_EXIT_CODE);
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count, const char *const *const arguments) noexcept {
    static_cast<void>(arguments);
    if (argument_count != OS_USER_TOOL_PROBE_REQUIRED_ARGUMENT_COUNT) {
        ExitFailure(OS_USER_TOOL_PROBE_ARGUMENT_FAILURE_MESSAGE);
    }
    uint64_t inode_numbers[OS_USER_TOOL_PROBE_REQUIRED_TOOL_COUNT]{};
    for (uint64_t tool_index = OS_USER_TOOL_PROBE_EMPTY_VALUE;
         tool_index < OS_USER_TOOL_PROBE_REQUIRED_TOOL_COUNT; ++tool_index) {
        const ToolPath &path = OS_USER_TOOL_PROBE_PATHS[tool_index];
        os::abi::FileInformation information{};
        if (os::user::StatFile(path.bytes, path.length_bytes, information) !=
                OS_USER_TOOL_PROBE_SUCCESS_RESULT ||
            information.type != os::abi::DirectoryEntryType::RegularFile ||
            information.size_bytes < OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES ||
            (information.mode &
             (os::abi::OS_ABI_FILE_MODE_OWNER_EXECUTE | os::abi::OS_ABI_FILE_MODE_GROUP_EXECUTE |
              os::abi::OS_ABI_FILE_MODE_OTHER_EXECUTE)) == 0U ||
            InodeWasAlreadyObserved(inode_numbers, tool_index, information.inode_number)) {
            ExitFailure(OS_USER_TOOL_PROBE_STAT_FAILURE_MESSAGE);
        }
        inode_numbers[tool_index] = information.inode_number;

        const int64_t descriptor =
            os::user::OpenFile(path.bytes, path.length_bytes, os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
        if (descriptor < OS_USER_TOOL_PROBE_SUCCESS_RESULT) {
            ExitFailure(OS_USER_TOOL_PROBE_OPEN_FAILURE_MESSAGE);
        }
        uint8_t magic[OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES]{};
        const bool content_is_valid =
            os::user::ReadDescriptor(static_cast<uint64_t>(descriptor), magic, sizeof(magic)) ==
                static_cast<int64_t>(sizeof(magic)) &&
            MagicIsValid(magic);
        if (!content_is_valid) {
            ExitFailure(OS_USER_TOOL_PROBE_MAGIC_FAILURE_MESSAGE);
        }
        const bool close_succeeded = os::user::CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
                                     OS_USER_TOOL_PROBE_SUCCESS_RESULT;
        if (!close_succeeded) {
            ExitFailure(OS_USER_TOOL_PROBE_CLOSE_FAILURE_MESSAGE);
        }
    }
    os::abi::FileInformation readahead_information{};
    if (os::user::StatFile(OS_USER_TOOL_PROBE_READAHEAD_PATH.bytes,
                           OS_USER_TOOL_PROBE_READAHEAD_PATH.length_bytes,
                           readahead_information) != OS_USER_TOOL_PROBE_SUCCESS_RESULT ||
        readahead_information.type != os::abi::DirectoryEntryType::RegularFile ||
        readahead_information.size_bytes <= OS_USER_TOOL_PROBE_PAGE_SIZE_BYTES) {
        ExitFailure(OS_USER_TOOL_PROBE_STAT_FAILURE_MESSAGE);
    }
    const int64_t readahead_descriptor = os::user::OpenFile(
        OS_USER_TOOL_PROBE_READAHEAD_PATH.bytes, OS_USER_TOOL_PROBE_READAHEAD_PATH.length_bytes,
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (readahead_descriptor < OS_USER_TOOL_PROBE_SUCCESS_RESULT) {
        ExitFailure(OS_USER_TOOL_PROBE_OPEN_FAILURE_MESSAGE);
    }
    uint8_t readahead_magic[OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES]{};
    if (os::user::ReadDescriptor(static_cast<uint64_t>(readahead_descriptor), readahead_magic,
                                 sizeof(readahead_magic)) !=
            static_cast<int64_t>(sizeof(readahead_magic)) ||
        !MagicIsValid(readahead_magic)) {
        ExitFailure(OS_USER_TOOL_PROBE_MAGIC_FAILURE_MESSAGE);
    }
    uint8_t sequential_buffer[os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES]{};
    uint64_t total_read_bytes = sizeof(readahead_magic);
    const uint64_t sequential_read_limit =
        Minimum(readahead_information.size_bytes, OS_USER_TOOL_PROBE_SEQUENTIAL_READ_LIMIT_BYTES);
    while (total_read_bytes < sequential_read_limit) {
        const uint64_t requested_bytes =
            Minimum(sizeof(sequential_buffer), sequential_read_limit - total_read_bytes);
        if (os::user::ReadDescriptor(static_cast<uint64_t>(readahead_descriptor), sequential_buffer,
                                     requested_bytes) != static_cast<int64_t>(requested_bytes)) {
            ExitFailure(OS_USER_TOOL_PROBE_SEQUENTIAL_FAILURE_MESSAGE);
        }
        total_read_bytes += requested_bytes;
    }
    if (os::user::CloseDescriptor(static_cast<uint64_t>(readahead_descriptor)) !=
        OS_USER_TOOL_PROBE_SUCCESS_RESULT) {
        ExitFailure(OS_USER_TOOL_PROBE_CLOSE_FAILURE_MESSAGE);
    }
    if (os::user::WriteLog(OS_USER_TOOL_PROBE_READAHEAD_VERIFIED_MESSAGE,
                           sizeof(OS_USER_TOOL_PROBE_READAHEAD_VERIFIED_MESSAGE) -
                               OS_USER_TOOL_PROBE_STRING_TERMINATOR_SIZE_BYTES) <
            OS_USER_TOOL_PROBE_SUCCESS_RESULT ||
        os::user::WriteLog(OS_USER_TOOL_PROBE_VERIFIED_MESSAGE,
                           sizeof(OS_USER_TOOL_PROBE_VERIFIED_MESSAGE) -
                               OS_USER_TOOL_PROBE_STRING_TERMINATOR_SIZE_BYTES) <
            OS_USER_TOOL_PROBE_SUCCESS_RESULT) {
        os::user::ExitProcess(OS_USER_TOOL_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_TOOL_PROBE_SUCCESS_EXIT_CODE);
}
