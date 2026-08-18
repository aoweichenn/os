#include "os/abi/signal.hpp"
#include "os/abi/system_call.hpp"
#include "os/kernel/arch/exception_frame.hpp"
#include "os/kernel/arch/user_context.hpp"
#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "os/kernel/user/user_elf.hpp"
#include "os/kernel/user/user_memory.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_BOUNDARY_SUITE_NAME = "kernel/user_boundary/integration";
constexpr std::string_view OS_TEST_USER_BOUNDARY_FRAME_MESSAGE =
    "异常帧必须按 CS.RPL 区分 Ring 0 与 Ring 3";
constexpr std::string_view OS_TEST_USER_BOUNDARY_STACK_MESSAGE =
    "用户栈必须预留 8 MiB 虚拟窗口并在下方保留一页永久保护区";
constexpr std::string_view OS_TEST_USER_BOUNDARY_ADDRESS_MESSAGE =
    "用户地址边界必须排除低地址和非规范高半区";
constexpr std::string_view OS_TEST_USER_BOUNDARY_PROGRAM_ADDRESS_MESSAGE =
    "用户 ELF 必须限制在独立的 1 GiB 进程程序窗口";
constexpr std::string_view OS_TEST_USER_BOUNDARY_ABI_MESSAGE =
    "系统调用 ABI 编号、线程边界结构和向量必须稳定";
constexpr uint64_t OS_TEST_USER_BOUNDARY_KERNEL_CODE_SELECTOR = 0x0008ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_USER_CODE_SELECTOR = 0x0023ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_STACK_PAGE_COUNT = 2048ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_STACK_SIZE_BYTES = 8ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_VECTOR = 0x80ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_WRITE_NUMBER = 1ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_EXIT_NUMBER = 2ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_GET_PROCESS_ID_NUMBER = 3ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_CHANGE_DIRECTORY_NUMBER = 29ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_GET_WORKING_DIRECTORY_NUMBER = 30ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_UNLINK_FILE_NUMBER = 31ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_REMOVE_DIRECTORY_NUMBER = 32ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_RENAME_NUMBER = 33ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_TRUNCATE_FILE_NUMBER = 34ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_STAT_FILE_NUMBER = 35ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_MAP_MEMORY_NUMBER = 39ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_UNMAP_MEMORY_NUMBER = 40ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_PROGRAM_BREAK_NUMBER = 41ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_MEMORY_STATISTICS_NUMBER = 42ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_CREATE_THREAD_NUMBER = 47ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_EXIT_THREAD_NUMBER = 48ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_JOIN_THREAD_NUMBER = 49ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SET_TLS_NUMBER = 50ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_GET_THREAD_ID_NUMBER = 51ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_WAIT_FUTEX_NUMBER = 52ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_WAKE_FUTEX_NUMBER = 53ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_MONOTONIC_TIME_NUMBER = 54ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SLEEP_UNTIL_NUMBER = 55ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_TIMED_FUTEX_NUMBER = 56ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SET_SIGNAL_ACTION_NUMBER = 57ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SET_SIGNAL_MASK_NUMBER = 58ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SEND_PROCESS_SIGNAL_NUMBER = 59ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SEND_GROUP_SIGNAL_NUMBER = 60ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SIGNAL_RETURN_NUMBER = 61ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_GET_PROCESS_GROUP_NUMBER = 62ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SET_PROCESS_GROUP_NUMBER = 63ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SIGNAL_ACTION_SIZE_BYTES = 40ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SIGNAL_CONTEXT_SIZE_BYTES = 176ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_SIGNAL_FRAME_SIZE_BYTES = 240ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_PATH_CAPACITY_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_NAME_CAPACITY_BYTES = 255ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_DIRECTORY_ENTRY_SIZE_BYTES = 280ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_FILE_INFORMATION_SIZE_BYTES = 96ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES = 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_BOUNDARY_SUITE_NAME};
    os::kernel::ExceptionFrame kernel_frame{};
    kernel_frame.code_segment = OS_TEST_USER_BOUNDARY_KERNEL_CODE_SELECTOR;
    os::kernel::UserContext user_frame{};
    user_frame.common.code_segment = OS_TEST_USER_BOUNDARY_USER_CODE_SELECTOR;
    test_context.Expect(!os::kernel::FrameOriginatedFromUser(kernel_frame) &&
                            os::kernel::FrameOriginatedFromUser(user_frame.common) &&
                            &os::kernel::AsUserContext(user_frame.common) == &user_frame,
                        OS_TEST_USER_BOUNDARY_FRAME_MESSAGE);

    test_context.Expect(os::kernel::OS_KERNEL_USER_STACK_PAGE_COUNT ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_STACK_PAGE_COUNT &&
                            os::kernel::OS_KERNEL_USER_STACK_SIZE_BYTES ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_STACK_SIZE_BYTES &&
                            os::kernel::OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS -
                                    os::kernel::OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_STACK_SIZE_BYTES &&
                            os::kernel::OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS -
                                    os::kernel::OS_KERNEL_USER_STACK_GUARD_VIRTUAL_ADDRESS ==
                                os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                        OS_TEST_USER_BOUNDARY_STACK_MESSAGE);

    test_context.Expect(
        !os::kernel::IsUserVirtualAddressRange(os::kernel::OS_KERNEL_USER_MINIMUM_VIRTUAL_ADDRESS -
                                                   OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES,
                                               OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES) &&
            os::kernel::IsUserVirtualAddressRange(
                os::kernel::OS_KERNEL_USER_MINIMUM_VIRTUAL_ADDRESS,
                OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES) &&
            !os::kernel::IsUserVirtualAddressRange(
                os::kernel::OS_KERNEL_USER_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE,
                OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES),
        OS_TEST_USER_BOUNDARY_ADDRESS_MESSAGE);

    test_context.Expect(
        !os::kernel::IsUserProgramVirtualAddressRange(
            os::kernel::OS_KERNEL_USER_PROGRAM_MINIMUM_VIRTUAL_ADDRESS -
                OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES,
            OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES) &&
            os::kernel::IsUserProgramVirtualAddressRange(
                os::kernel::OS_KERNEL_USER_PROGRAM_MINIMUM_VIRTUAL_ADDRESS,
                OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES) &&
            !os::kernel::IsUserProgramVirtualAddressRange(
                os::kernel::OS_KERNEL_USER_PROGRAM_MAXIMUM_VIRTUAL_ADDRESS_EXCLUSIVE,
                OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES),
        OS_TEST_USER_BOUNDARY_PROGRAM_ADDRESS_MESSAGE);

    test_context.Expect(
        os::abi::OS_ABI_SYSTEM_CALL_VECTOR == OS_TEST_USER_BOUNDARY_EXPECTED_VECTOR &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::WriteLog) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_WRITE_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::ExitProcess) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_EXIT_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessId) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_GET_PROCESS_ID_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeDirectory) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_CHANGE_DIRECTORY_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::GetWorkingDirectory) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_GET_WORKING_DIRECTORY_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::UnlinkFile) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_UNLINK_FILE_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::RemoveDirectory) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_REMOVE_DIRECTORY_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::Rename) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_RENAME_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::TruncateFile) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_TRUNCATE_FILE_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::StatFile) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_STAT_FILE_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::MapAnonymousMemory) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_MAP_MEMORY_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::UnmapMemory) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_UNMAP_MEMORY_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SetProgramBreak) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_PROGRAM_BREAK_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::GetVirtualMemoryStatistics) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_MEMORY_STATISTICS_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::CreateThread) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_CREATE_THREAD_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::ExitThread) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_EXIT_THREAD_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::JoinThread) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_JOIN_THREAD_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SetThreadLocalStorage) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SET_TLS_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::GetThreadId) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_GET_THREAD_ID_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPrivateFutex) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_WAIT_FUTEX_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::WakePrivateFutex) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_WAKE_FUTEX_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::GetMonotonicTime) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_MONOTONIC_TIME_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SleepUntil) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SLEEP_UNTIL_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPrivateFutexUntil) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_TIMED_FUTEX_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SetSignalAction) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SET_SIGNAL_ACTION_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SetSignalMask) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SET_SIGNAL_MASK_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SendProcessSignal) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SEND_PROCESS_SIGNAL_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SendProcessGroupSignal) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SEND_GROUP_SIGNAL_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SignalReturn) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SIGNAL_RETURN_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessGroup) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_GET_PROCESS_GROUP_NUMBER &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SetProcessGroup) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SET_PROCESS_GROUP_NUMBER &&
            sizeof(os::abi::SignalAction) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SIGNAL_ACTION_SIZE_BYTES &&
            sizeof(os::abi::SignalUserContext) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SIGNAL_CONTEXT_SIZE_BYTES &&
            sizeof(os::abi::SignalFrame) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_SIGNAL_FRAME_SIZE_BYTES &&
            sizeof(os::abi::ThreadCreateRequest) ==
                os::abi::OS_ABI_THREAD_CREATE_REQUEST_SIZE_BYTES &&
            sizeof(os::abi::ThreadJoinResult) == os::abi::OS_ABI_THREAD_JOIN_RESULT_SIZE_BYTES &&
            os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PATH_SIZE_BYTES ==
                OS_TEST_USER_BOUNDARY_EXPECTED_PATH_CAPACITY_BYTES &&
            os::abi::OS_ABI_DIRECTORY_ENTRY_NAME_CAPACITY_BYTES ==
                OS_TEST_USER_BOUNDARY_EXPECTED_NAME_CAPACITY_BYTES &&
            sizeof(os::abi::DirectoryEntry) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_DIRECTORY_ENTRY_SIZE_BYTES &&
            sizeof(os::abi::FileInformation) ==
                OS_TEST_USER_BOUNDARY_EXPECTED_FILE_INFORMATION_SIZE_BYTES,
        OS_TEST_USER_BOUNDARY_ABI_MESSAGE);
    return test_context.ExitCode();
}
