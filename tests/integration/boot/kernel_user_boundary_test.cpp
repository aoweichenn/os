#include "os/abi/system_call.hpp"
#include "os/kernel/exception_frame.hpp"
#include "os/kernel/physical_frame_allocator.hpp"
#include "os/kernel/user_elf.hpp"
#include "os/kernel/user_memory.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_BOUNDARY_SUITE_NAME = "kernel/user_boundary/integration";
constexpr std::string_view OS_TEST_USER_BOUNDARY_FRAME_MESSAGE =
    "异常帧必须按 CS.RPL 区分 Ring 0 与 Ring 3";
constexpr std::string_view OS_TEST_USER_BOUNDARY_STACK_MESSAGE =
    "用户栈必须包含四个数据页和一个未映射保护页";
constexpr std::string_view OS_TEST_USER_BOUNDARY_ADDRESS_MESSAGE =
    "用户地址边界必须排除低地址和非规范高半区";
constexpr std::string_view OS_TEST_USER_BOUNDARY_PROGRAM_ADDRESS_MESSAGE =
    "用户 ELF 必须限制在独立的 1 GiB 进程程序窗口";
constexpr std::string_view OS_TEST_USER_BOUNDARY_ABI_MESSAGE = "系统调用 ABI 编号和向量必须稳定";
constexpr uint64_t OS_TEST_USER_BOUNDARY_KERNEL_CODE_SELECTOR = 0x0008ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_USER_CODE_SELECTOR = 0x0023ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_STACK_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_VECTOR = 0x80ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_WRITE_NUMBER = 1ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_EXIT_NUMBER = 2ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_EXPECTED_GET_PROCESS_ID_NUMBER = 3ULL;
constexpr uint64_t OS_TEST_USER_BOUNDARY_ADDRESS_PROBE_SIZE_BYTES = 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_BOUNDARY_SUITE_NAME};
    os::kernel::ExceptionFrame kernel_frame{};
    kernel_frame.code_segment = OS_TEST_USER_BOUNDARY_KERNEL_CODE_SELECTOR;
    os::kernel::UserPrivilegeFrame user_frame{};
    user_frame.common.code_segment = OS_TEST_USER_BOUNDARY_USER_CODE_SELECTOR;
    test_context.Expect(!os::kernel::FrameOriginatedFromUser(kernel_frame) &&
                            os::kernel::FrameOriginatedFromUser(user_frame.common) &&
                            &os::kernel::AsUserPrivilegeFrame(user_frame.common) == &user_frame,
                        OS_TEST_USER_BOUNDARY_FRAME_MESSAGE);

    test_context.Expect(os::kernel::OS_KERNEL_USER_STACK_PAGE_COUNT ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_STACK_PAGE_COUNT &&
                            os::kernel::OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS -
                                    os::kernel::OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_STACK_PAGE_COUNT *
                                    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES &&
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

    test_context.Expect(os::abi::OS_ABI_SYSTEM_CALL_VECTOR ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_VECTOR &&
                            static_cast<uint64_t>(os::abi::SystemCallNumber::WriteLog) ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_WRITE_NUMBER &&
                            static_cast<uint64_t>(os::abi::SystemCallNumber::ExitProcess) ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_EXIT_NUMBER &&
                            static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessId) ==
                                OS_TEST_USER_BOUNDARY_EXPECTED_GET_PROCESS_ID_NUMBER,
                        OS_TEST_USER_BOUNDARY_ABI_MESSAGE);
    return test_context.ExitCode();
}
