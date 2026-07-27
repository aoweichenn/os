#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"
#include "os/abi/virtual_memory.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_FORK_PROBE_STARTED_MESSAGE[] =
    "[OS][USER][FORK] STARTED\r\n";
constexpr char OS_USER_FORK_PROBE_COPY_ON_WRITE_MESSAGE[] =
    "[OS][USER][FORK] COW_ISOLATION_VERIFIED\r\n";
constexpr char OS_USER_FORK_PROBE_INHERITANCE_MESSAGE[] =
    "[OS][USER][FORK] FD_OFFSET_AND_CWD_VERIFIED\r\n";
constexpr char OS_USER_FORK_PROBE_LIFECYCLE_MESSAGE[] =
    "[OS][USER][FORK] FORK_EXEC_WAIT_32_VERIFIED\r\n";
constexpr char OS_USER_FORK_PROBE_COMPLETED_MESSAGE[] =
    "[OS][USER][FORK] COMPLETED\r\n";
constexpr char OS_USER_FORK_PROBE_CHILD_STATE_FAILURE_MESSAGE[] =
    "[OS][USER][FORK] CHILD_STATE_FAILURE\r\n";
constexpr char OS_USER_FORK_PROBE_CHILD_STATISTICS_FAILURE_MESSAGE[] =
    "[OS][USER][FORK] CHILD_STATISTICS_FAILURE\r\n";
constexpr char OS_USER_FORK_PROBE_CHILD_DESCRIPTOR_FAILURE_MESSAGE[] =
    "[OS][USER][FORK] CHILD_DESCRIPTOR_FAILURE\r\n";
constexpr char OS_USER_FORK_PROBE_PARENT_STATE_FAILURE_MESSAGE[] =
    "[OS][USER][FORK] PARENT_STATE_FAILURE\r\n";
constexpr char OS_USER_FORK_PROBE_FILE_PATH[] = "/bin/smoke";
constexpr char OS_USER_FORK_PROBE_WORKING_DIRECTORY[] = "/bin";
constexpr char OS_USER_FORK_PROBE_EXEC_PATH[] =
    "/bin/exec_target";
constexpr char OS_USER_FORK_PROBE_EXEC_ARGUMENT[] = "committed";
constexpr char OS_USER_FORK_PROBE_EXEC_ENVIRONMENT[] =
    "OS_EXEC=atomic";

constexpr uint64_t OS_USER_FORK_PROBE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_FORK_PROBE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_USER_FORK_PROBE_SECOND_INDEX = 1ULL;
constexpr uint64_t OS_USER_FORK_PROBE_ANONYMOUS_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_USER_FORK_PROBE_ANONYMOUS_SIZE_BYTES =
    OS_USER_FORK_PROBE_ANONYMOUS_PAGE_COUNT *
    os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_USER_FORK_PROBE_LIFECYCLE_COUNT = 32ULL;
constexpr uint64_t OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES =
    1ULL;
constexpr uint64_t OS_USER_FORK_PROBE_SINGLE_BYTE = 1ULL;
constexpr uint64_t OS_USER_FORK_PROBE_READ_BUFFER_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_USER_FORK_PROBE_CWD_BUFFER_SIZE_BYTES = 32ULL;
constexpr uint8_t OS_USER_FORK_PROBE_ELF_MAGIC = 0x7FU;
constexpr uint8_t OS_USER_FORK_PROBE_ELF_SECOND_BYTE = 0x45U;
constexpr uint8_t OS_USER_FORK_PROBE_ELF_THIRD_BYTE = 0x4CU;
constexpr uint8_t OS_USER_FORK_PROBE_PARENT_PATTERN = 0x39U;
constexpr uint8_t OS_USER_FORK_PROBE_CHILD_PATTERN = 0xA7U;
constexpr uint64_t OS_USER_FORK_PROBE_STATISTICS_SENTINEL =
    0x434F575354415453ULL;
constexpr int64_t OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_FORK_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_FORK_PROBE_FIRST_ERROR_RESULT = -1LL;

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool
WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(
               message, MessageSizeBytes -
                            OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_FORK_PROBE_FIRST_ERROR_RESULT;
}

[[noreturn]] void Fail() noexcept {
    os::user::ExitProcess(OS_USER_FORK_PROBE_FAILURE_EXIT_CODE);
}

[[nodiscard]] bool WorkingDirectoryIsBin() noexcept {
    char path[OS_USER_FORK_PROBE_CWD_BUFFER_SIZE_BYTES]{};
    const int64_t path_length = os::user::GetWorkingDirectory(
        path, sizeof(path));
    return path_length ==
               static_cast<int64_t>(
                   sizeof(OS_USER_FORK_PROBE_WORKING_DIRECTORY) -
                   OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES) &&
           path[OS_USER_FORK_PROBE_FIRST_INDEX] == '/' &&
           path[OS_USER_FORK_PROBE_SECOND_INDEX] == 'b' &&
           path[OS_USER_FORK_PROBE_SECOND_INDEX +
                OS_USER_FORK_PROBE_SECOND_INDEX] == 'i' &&
           path[OS_USER_FORK_PROBE_SECOND_INDEX +
                OS_USER_FORK_PROBE_SECOND_INDEX +
                OS_USER_FORK_PROBE_SECOND_INDEX] == 'n';
}

[[noreturn]] void ExecTarget() noexcept {
    const os::abi::ProcessString arguments[]{
        {
            .address =
                reinterpret_cast<uint64_t>(
                    OS_USER_FORK_PROBE_EXEC_PATH),
            .length_bytes =
                sizeof(OS_USER_FORK_PROBE_EXEC_PATH) -
                OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        },
        {
            .address =
                reinterpret_cast<uint64_t>(
                    OS_USER_FORK_PROBE_EXEC_ARGUMENT),
            .length_bytes =
                sizeof(OS_USER_FORK_PROBE_EXEC_ARGUMENT) -
                OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        },
    };
    const os::abi::ProcessString environment[]{
        {
            .address =
                reinterpret_cast<uint64_t>(
                    OS_USER_FORK_PROBE_EXEC_ENVIRONMENT),
            .length_bytes =
                sizeof(OS_USER_FORK_PROBE_EXEC_ENVIRONMENT) -
                OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        },
    };
    const os::abi::ProcessLaunchRequest request{
        .path_address =
            reinterpret_cast<uint64_t>(
                OS_USER_FORK_PROBE_EXEC_PATH),
        .path_length_bytes =
            sizeof(OS_USER_FORK_PROBE_EXEC_PATH) -
            OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        .argument_vector_address =
            reinterpret_cast<uint64_t>(arguments),
        .argument_count =
            sizeof(arguments) / sizeof(arguments[OS_USER_FORK_PROBE_FIRST_INDEX]),
        .environment_vector_address =
            reinterpret_cast<uint64_t>(environment),
        .environment_count =
            sizeof(environment) /
            sizeof(environment[OS_USER_FORK_PROBE_FIRST_INDEX]),
    };
    static_cast<void>(os::user::ExecProcess(request));
    Fail();
}

[[nodiscard]] bool WaitForSuccessfulChild(
    const uint64_t process_id) noexcept {
    os::abi::ProcessWaitResult result{};
    return os::user::WaitProcess(process_id, result) ==
               static_cast<int64_t>(process_id) &&
           result.process_id == process_id &&
           result.termination_reason ==
               os::abi::ProcessTerminationReason::Exited &&
           result.exit_code ==
               OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry() noexcept {
    if (!WriteMessage(OS_USER_FORK_PROBE_STARTED_MESSAGE) ||
        os::user::ChangeDirectory(
            OS_USER_FORK_PROBE_WORKING_DIRECTORY,
            sizeof(OS_USER_FORK_PROBE_WORKING_DIRECTORY) -
                OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES) !=
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE ||
        !WorkingDirectoryIsBin()) {
        Fail();
    }

    const int64_t descriptor = os::user::OpenFile(
        OS_USER_FORK_PROBE_FILE_PATH,
        sizeof(OS_USER_FORK_PROBE_FILE_PATH) -
            OS_USER_FORK_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (descriptor < OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE) {
        Fail();
    }
    uint8_t read_buffer[OS_USER_FORK_PROBE_READ_BUFFER_SIZE_BYTES]{};
    if (os::user::ReadFile(
            static_cast<uint64_t>(descriptor), read_buffer,
            OS_USER_FORK_PROBE_SINGLE_BYTE) !=
            static_cast<int64_t>(OS_USER_FORK_PROBE_SINGLE_BYTE) ||
        read_buffer[OS_USER_FORK_PROBE_FIRST_INDEX] !=
            OS_USER_FORK_PROBE_ELF_MAGIC) {
        Fail();
    }

    const int64_t anonymous_result = os::user::MapAnonymousMemory(
        os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS,
        OS_USER_FORK_PROBE_ANONYMOUS_SIZE_BYTES,
        os::abi::OS_ABI_MEMORY_PROTECTION_READ |
            os::abi::OS_ABI_MEMORY_PROTECTION_WRITE,
        os::abi::OS_ABI_MEMORY_MAP_NO_FLAGS);
    const os::abi::FileMemoryMapRequest private_request{
        .requested_address =
            os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS,
        .length_bytes = os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES,
        .protection_flags =
            os::abi::OS_ABI_MEMORY_PROTECTION_READ |
            os::abi::OS_ABI_MEMORY_PROTECTION_WRITE,
        .map_flags = os::abi::OS_ABI_MEMORY_MAP_PRIVATE,
        .file_descriptor = static_cast<uint64_t>(descriptor),
        .file_offset_bytes = OS_USER_FORK_PROBE_EMPTY_VALUE,
    };
    const os::abi::FileMemoryMapRequest read_only_request{
        .requested_address =
            os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS,
        .length_bytes = os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES,
        .protection_flags =
            os::abi::OS_ABI_MEMORY_PROTECTION_READ,
        .map_flags = os::abi::OS_ABI_MEMORY_MAP_PRIVATE,
        .file_descriptor = static_cast<uint64_t>(descriptor),
        .file_offset_bytes = OS_USER_FORK_PROBE_EMPTY_VALUE,
    };
    const int64_t private_result =
        os::user::MapFileMemory(private_request);
    const int64_t read_only_result =
        os::user::MapFileMemory(read_only_request);
    if (anonymous_result < OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE ||
        private_result < OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE ||
        read_only_result < OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE) {
        Fail();
    }
    uint8_t *const anonymous_page =
        reinterpret_cast<uint8_t *>(
            static_cast<uint64_t>(anonymous_result));
    uint8_t *const private_page =
        reinterpret_cast<uint8_t *>(
            static_cast<uint64_t>(private_result));
    const uint8_t *const read_only_page =
        reinterpret_cast<const uint8_t *>(
            static_cast<uint64_t>(read_only_result));
    anonymous_page[OS_USER_FORK_PROBE_FIRST_INDEX] =
        OS_USER_FORK_PROBE_PARENT_PATTERN;
    private_page[OS_USER_FORK_PROBE_SECOND_INDEX] =
        private_page[OS_USER_FORK_PROBE_FIRST_INDEX];
    volatile uint8_t read_only_probe =
        read_only_page[OS_USER_FORK_PROBE_SECOND_INDEX];
    static_cast<void>(read_only_probe);
    uint64_t *const statistics_sentinel =
        reinterpret_cast<uint64_t *>(
            anonymous_page +
            os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES);
    *statistics_sentinel =
        OS_USER_FORK_PROBE_STATISTICS_SENTINEL;

    const int64_t fork_result = os::user::ForkProcess();
    if (fork_result < OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE) {
        Fail();
    }
    if (fork_result == OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE) {
        if (anonymous_page[OS_USER_FORK_PROBE_FIRST_INDEX] !=
                OS_USER_FORK_PROBE_PARENT_PATTERN ||
            private_page[OS_USER_FORK_PROBE_FIRST_INDEX] !=
                OS_USER_FORK_PROBE_ELF_MAGIC ||
            read_only_page[OS_USER_FORK_PROBE_SECOND_INDEX] !=
                OS_USER_FORK_PROBE_ELF_SECOND_BYTE ||
            !WorkingDirectoryIsBin()) {
            static_cast<void>(
                WriteMessage(
                    OS_USER_FORK_PROBE_CHILD_STATE_FAILURE_MESSAGE));
            Fail();
        }
        anonymous_page[OS_USER_FORK_PROBE_FIRST_INDEX] =
            OS_USER_FORK_PROBE_CHILD_PATTERN;
        private_page[OS_USER_FORK_PROBE_FIRST_INDEX] =
            OS_USER_FORK_PROBE_CHILD_PATTERN;
        os::abi::VirtualMemoryStatistics &statistics =
            *reinterpret_cast<os::abi::VirtualMemoryStatistics *>(
                statistics_sentinel);
        if (os::user::GetVirtualMemoryStatistics(statistics) !=
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE) {
            static_cast<void>(
                WriteMessage(
                    OS_USER_FORK_PROBE_CHILD_STATISTICS_FAILURE_MESSAGE));
            Fail();
        }
        if (os::user::ReadFile(
                static_cast<uint64_t>(descriptor), read_buffer,
                OS_USER_FORK_PROBE_SINGLE_BYTE) !=
                static_cast<int64_t>(
                    OS_USER_FORK_PROBE_SINGLE_BYTE) ||
            read_buffer[OS_USER_FORK_PROBE_FIRST_INDEX] !=
                OS_USER_FORK_PROBE_ELF_SECOND_BYTE) {
            static_cast<void>(
                WriteMessage(
                    OS_USER_FORK_PROBE_CHILD_DESCRIPTOR_FAILURE_MESSAGE));
            Fail();
        }
        os::user::ExitProcess(
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE);
    }

    if (!WaitForSuccessfulChild(
            static_cast<uint64_t>(fork_result)) ||
        anonymous_page[OS_USER_FORK_PROBE_FIRST_INDEX] !=
            OS_USER_FORK_PROBE_PARENT_PATTERN ||
        private_page[OS_USER_FORK_PROBE_FIRST_INDEX] !=
            OS_USER_FORK_PROBE_ELF_MAGIC ||
        *statistics_sentinel !=
            OS_USER_FORK_PROBE_STATISTICS_SENTINEL ||
        os::user::ReadFile(
            static_cast<uint64_t>(descriptor), read_buffer,
            OS_USER_FORK_PROBE_SINGLE_BYTE) !=
            static_cast<int64_t>(
                OS_USER_FORK_PROBE_SINGLE_BYTE) ||
        read_buffer[OS_USER_FORK_PROBE_FIRST_INDEX] !=
            OS_USER_FORK_PROBE_ELF_THIRD_BYTE ||
        !WorkingDirectoryIsBin() ||
        !WriteMessage(OS_USER_FORK_PROBE_COPY_ON_WRITE_MESSAGE) ||
        !WriteMessage(OS_USER_FORK_PROBE_INHERITANCE_MESSAGE)) {
        static_cast<void>(
            WriteMessage(
                OS_USER_FORK_PROBE_PARENT_STATE_FAILURE_MESSAGE));
        Fail();
    }

    if (os::user::UnmapMemory(
            static_cast<uint64_t>(anonymous_result),
            OS_USER_FORK_PROBE_ANONYMOUS_SIZE_BYTES) !=
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE ||
        os::user::UnmapMemory(
            static_cast<uint64_t>(private_result),
            os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES) !=
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE ||
        os::user::UnmapMemory(
            static_cast<uint64_t>(read_only_result),
            os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES) !=
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE ||
        os::user::CloseFile(
            static_cast<uint64_t>(descriptor)) !=
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE) {
        Fail();
    }

    for (uint64_t lifecycle_index =
             OS_USER_FORK_PROBE_FIRST_INDEX;
         lifecycle_index <
         OS_USER_FORK_PROBE_LIFECYCLE_COUNT;
         ++lifecycle_index) {
        const int64_t child_process_id =
            os::user::ForkProcess();
        if (child_process_id <
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE) {
            Fail();
        }
        if (child_process_id ==
            OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE) {
            ExecTarget();
        }
        if (!WaitForSuccessfulChild(
                static_cast<uint64_t>(child_process_id))) {
            Fail();
        }
    }
    if (!WriteMessage(OS_USER_FORK_PROBE_LIFECYCLE_MESSAGE) ||
        !WriteMessage(OS_USER_FORK_PROBE_COMPLETED_MESSAGE)) {
        Fail();
    }
    os::user::ExitProcess(OS_USER_FORK_PROBE_SUCCESS_EXIT_CODE);
}
