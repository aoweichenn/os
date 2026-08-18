#include "os/abi/elf.hpp"
#include "os/abi/layout.hpp"
#include "os/abi/version.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ABI_V2_SUITE_NAME = "abi/v2/unit";
constexpr std::string_view OS_TEST_ABI_V2_VERSION_CONTRACT =
    "ABI v2.2 必须兼容冻结的系统调用编号并扩展文件时间戳";
constexpr std::string_view OS_TEST_ABI_V2_ELF_CONTRACT =
    "ELF64 x86-64 小端静态可执行契约必须与加载器共享";
constexpr uint64_t OS_TEST_ABI_V2_EXPECTED_MAJOR_VERSION = 2ULL;
constexpr uint64_t OS_TEST_ABI_V2_EXPECTED_MINOR_VERSION = 2ULL;
constexpr uint64_t OS_TEST_ABI_V2_EXPECTED_SYSTEM_CALL_COUNT = 71ULL;
constexpr int64_t OS_TEST_ABI_V2_EXPECTED_FIRST_ERROR = -1LL;
constexpr int64_t OS_TEST_ABI_V2_EXPECTED_LAST_ERROR = -57LL;
constexpr uint64_t OS_TEST_ABI_V2_EXPECTED_ELF_HEADER_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_TEST_ABI_V2_EXPECTED_PROGRAM_HEADER_SIZE_BYTES = 56ULL;
constexpr uint16_t OS_TEST_ABI_V2_EXPECTED_X86_64_MACHINE = 0x003EU;
constexpr uint16_t OS_TEST_ABI_V2_EXPECTED_EXECUTABLE_TYPE = 0x0002U;
constexpr uint32_t OS_TEST_ABI_V2_EXPECTED_PROGRAM_FLAG_MASK = 0x00000007U;

}

int main() {
    os::test::TestContext test_context{OS_TEST_ABI_V2_SUITE_NAME};
    test_context.Expect(
        os::abi::OS_ABI_VERSION_MAJOR == OS_TEST_ABI_V2_EXPECTED_MAJOR_VERSION &&
            os::abi::OS_ABI_VERSION_MINOR == OS_TEST_ABI_V2_EXPECTED_MINOR_VERSION &&
            os::abi::OS_ABI_SYSTEM_CALL_COUNT == OS_TEST_ABI_V2_EXPECTED_SYSTEM_CALL_COUNT &&
            os::abi::OS_ABI_SYSTEM_CALL_FIRST_ERROR == OS_TEST_ABI_V2_EXPECTED_FIRST_ERROR &&
            os::abi::OS_ABI_SYSTEM_CALL_LAST_ERROR == OS_TEST_ABI_V2_EXPECTED_LAST_ERROR,
        OS_TEST_ABI_V2_VERSION_CONTRACT);
    test_context.Expect(
        os::abi::OS_ABI_ELF64_HEADER_SIZE_BYTES == OS_TEST_ABI_V2_EXPECTED_ELF_HEADER_SIZE_BYTES &&
            os::abi::OS_ABI_ELF64_PROGRAM_HEADER_SIZE_BYTES ==
                OS_TEST_ABI_V2_EXPECTED_PROGRAM_HEADER_SIZE_BYTES &&
            os::abi::OS_ABI_ELF_X86_64_MACHINE == OS_TEST_ABI_V2_EXPECTED_X86_64_MACHINE &&
            os::abi::OS_ABI_ELF_EXECUTABLE_TYPE == OS_TEST_ABI_V2_EXPECTED_EXECUTABLE_TYPE &&
            os::abi::OS_ABI_ELF_PROGRAM_KNOWN_FLAG_MASK ==
                OS_TEST_ABI_V2_EXPECTED_PROGRAM_FLAG_MASK,
        OS_TEST_ABI_V2_ELF_CONTRACT);
    return test_context.ExitCode();
}
