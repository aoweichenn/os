#include "os/user/system_call.hpp"

#include "os/abi/elf.hpp"
#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace {

constexpr uint64_t OS_USER_TOOL_PROBE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_USER_TOOL_PROBE_REQUIRED_TOOL_COUNT = 32ULL;
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
constexpr char OS_USER_TOOL_PROBE_VERIFIED_MESSAGE[] =
    "[OS][USER][TOOLS] ELF32_VERIFIED\r\n";

struct ToolPath final {
    const char *bytes;
    uint64_t length_bytes;
};

template <uint64_t SizeBytes>
[[nodiscard]] constexpr ToolPath MakeToolPath(
    const char (&path)[SizeBytes]) noexcept {
    return ToolPath{
        .bytes = path,
        .length_bytes =
            SizeBytes - OS_USER_TOOL_PROBE_STRING_TERMINATOR_SIZE_BYTES,
    };
}

constexpr ToolPath OS_USER_TOOL_PROBE_PATHS[]{
    MakeToolPath("/bin/help"),      MakeToolPath("/bin/echo"),
    MakeToolPath("/bin/cat"),       MakeToolPath("/bin/wc"),
    MakeToolPath("/bin/head"),      MakeToolPath("/bin/tee"),
    MakeToolPath("/bin/true"),      MakeToolPath("/bin/false"),
    MakeToolPath("/bin/pwd"),       MakeToolPath("/bin/ls"),
    MakeToolPath("/bin/stat"),      MakeToolPath("/bin/mkdir"),
    MakeToolPath("/bin/write"),     MakeToolPath("/bin/touch"),
    MakeToolPath("/bin/rm"),        MakeToolPath("/bin/rmdir"),
    MakeToolPath("/bin/mv"),        MakeToolPath("/bin/truncate"),
    MakeToolPath("/bin/sync"),      MakeToolPath("/bin/basename"),
    MakeToolPath("/bin/dirname"),   MakeToolPath("/bin/cp"),
    MakeToolPath("/bin/seq"),       MakeToolPath("/bin/uptime"),
    MakeToolPath("/bin/ps"),        MakeToolPath("/bin/free"),
    MakeToolPath("/bin/uname"),     MakeToolPath("/bin/mounts"),
    MakeToolPath("/bin/resources"), MakeToolPath("/bin/sleep"),
    MakeToolPath("/bin/kill"),      MakeToolPath("/bin/id"),
};

static_assert(sizeof(OS_USER_TOOL_PROBE_PATHS) /
                      sizeof(OS_USER_TOOL_PROBE_PATHS[OS_USER_TOOL_PROBE_EMPTY_VALUE]) ==
                  OS_USER_TOOL_PROBE_REQUIRED_TOOL_COUNT,
              "用户工具验收清单必须精确覆盖 32 个独立 ELF 路径");

[[nodiscard]] bool MagicIsValid(const uint8_t *const magic) noexcept {
    for (uint64_t byte_index = OS_USER_TOOL_PROBE_EMPTY_VALUE;
         byte_index < OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES; ++byte_index) {
        if (magic[byte_index] != OS_USER_TOOL_PROBE_ELF_MAGIC[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool InodeWasAlreadyObserved(
    const uint64_t *const inode_numbers, const uint64_t observed_count,
    const uint64_t inode_number) noexcept {
    for (uint64_t inode_index = OS_USER_TOOL_PROBE_EMPTY_VALUE;
         inode_index < observed_count; ++inode_index) {
        if (inode_numbers[inode_index] == inode_number) {
            return true;
        }
    }
    return false;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry(const uint64_t argument_count,
                 const char *const *const arguments) noexcept {
    static_cast<void>(arguments);
    if (argument_count != OS_USER_TOOL_PROBE_REQUIRED_ARGUMENT_COUNT) {
        os::user::ExitProcess(OS_USER_TOOL_PROBE_FAILURE_EXIT_CODE);
    }
    uint64_t inode_numbers[OS_USER_TOOL_PROBE_REQUIRED_TOOL_COUNT]{};
    for (uint64_t tool_index = OS_USER_TOOL_PROBE_EMPTY_VALUE;
         tool_index < OS_USER_TOOL_PROBE_REQUIRED_TOOL_COUNT; ++tool_index) {
        const ToolPath &path = OS_USER_TOOL_PROBE_PATHS[tool_index];
        os::abi::FileInformation information{};
        if (os::user::StatFile(path.bytes, path.length_bytes, information) !=
                OS_USER_TOOL_PROBE_SUCCESS_RESULT ||
            information.type != os::abi::DirectoryEntryType::RegularFile ||
            information.size_bytes <
                OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES ||
            InodeWasAlreadyObserved(inode_numbers, tool_index,
                                    information.inode_number)) {
            os::user::ExitProcess(OS_USER_TOOL_PROBE_FAILURE_EXIT_CODE);
        }
        inode_numbers[tool_index] = information.inode_number;

        const int64_t descriptor = os::user::OpenFile(
            path.bytes, path.length_bytes, os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
        if (descriptor < OS_USER_TOOL_PROBE_SUCCESS_RESULT) {
            os::user::ExitProcess(OS_USER_TOOL_PROBE_FAILURE_EXIT_CODE);
        }
        uint8_t magic[OS_USER_TOOL_PROBE_ELF_MAGIC_SIZE_BYTES]{};
        const bool content_is_valid =
            os::user::ReadDescriptor(static_cast<uint64_t>(descriptor), magic,
                                     sizeof(magic)) ==
                static_cast<int64_t>(sizeof(magic)) &&
            MagicIsValid(magic);
        const bool close_succeeded =
            os::user::CloseDescriptor(static_cast<uint64_t>(descriptor)) ==
            OS_USER_TOOL_PROBE_SUCCESS_RESULT;
        if (!content_is_valid || !close_succeeded) {
            os::user::ExitProcess(OS_USER_TOOL_PROBE_FAILURE_EXIT_CODE);
        }
    }
    if (os::user::WriteLog(
            OS_USER_TOOL_PROBE_VERIFIED_MESSAGE,
            sizeof(OS_USER_TOOL_PROBE_VERIFIED_MESSAGE) -
                OS_USER_TOOL_PROBE_STRING_TERMINATOR_SIZE_BYTES) <
        OS_USER_TOOL_PROBE_SUCCESS_RESULT) {
        os::user::ExitProcess(OS_USER_TOOL_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_TOOL_PROBE_SUCCESS_EXIT_CODE);
}
