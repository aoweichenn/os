#include <os/abi/elf.hpp>
#include <os/abi/layout.hpp>
#include <os/abi/version.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ABI_V2_SUITE_NAME = "abi/v2/unit";
constexpr std::string_view OS_TEST_ABI_V2_VERSION_CONTRACT =
    "ABI v2.6 必须保留旧编号并追加九个打开文件描述操作";
constexpr std::string_view OS_TEST_ABI_V2_ELF_CONTRACT =
    "ELF64 x86-64 小端静态可执行契约必须与加载器共享";
constexpr std::string_view OS_TEST_ABI_V2_SECURITY_CONTRACT =
    "mode、RLIMIT 编号及身份结构必须与冻结的 Linux 兼容矩阵一致";
constexpr std::string_view OS_TEST_ABI_V2_AT_PATH_CONTRACT =
    "目录句柄相对路径调用的编号、标志与请求布局必须保持冻结";
constexpr std::string_view OS_TEST_ABI_V2_OPEN_FILE_DESCRIPTION_CONTRACT =
    "seek、定位 I/O、fd metadata 与 status flags 编号和常量必须保持冻结";
constexpr uint64_t OS_TEST_ABI_V2_EXPECTED_MAJOR_VERSION = 2ULL;
constexpr uint64_t OS_TEST_ABI_V2_EXPECTED_MINOR_VERSION = 6ULL;
constexpr uint64_t OS_TEST_ABI_V2_EXPECTED_SYSTEM_CALL_COUNT = 105ULL;
constexpr int64_t OS_TEST_ABI_V2_EXPECTED_FIRST_ERROR = -1LL;
constexpr int64_t OS_TEST_ABI_V2_EXPECTED_LAST_ERROR = -60LL;
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
    test_context.Expect(
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetRealtime) == 71ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::GetCredentials) == 72ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SetResourceLimit) == 84ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SynchronizeFile) == 85ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SynchronizeFileData) == 86ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SynchronizeMemory) == 87ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::OpenFileAt) == 88ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::CreateSymbolicLinkAt) == 96ULL &&
            os::abi::OS_ABI_MEMORY_SYNC_ASYNCHRONOUS == 1ULL &&
            os::abi::OS_ABI_MEMORY_SYNC_INVALIDATE == 2ULL &&
            os::abi::OS_ABI_MEMORY_SYNC_SYNCHRONOUS == 4ULL &&
            static_cast<uint64_t>(os::abi::ResourceLimitKind::ProcessorTime) == 0ULL &&
            static_cast<uint64_t>(os::abi::ResourceLimitKind::OpenFileCount) == 7ULL &&
            static_cast<uint64_t>(os::abi::ResourceLimitKind::AddressSpace) == 9ULL &&
            static_cast<uint64_t>(os::abi::ResourceLimitKind::RealtimeProcessorTime) == 15ULL &&
            os::abi::OS_ABI_FILE_MODE_REGULAR == 0100000U &&
            os::abi::OS_ABI_FILE_MODE_DIRECTORY == 0040000U &&
            os::abi::OS_ABI_DEFAULT_CREATION_MASK == 0000022U &&
            sizeof(os::abi::CredentialInformation) == 32ULL &&
            sizeof(os::abi::ResourceLimit) == 16ULL && sizeof(os::abi::FileInformation) == 112ULL,
        OS_TEST_ABI_V2_SECURITY_CONTRACT);
    test_context.Expect(
        static_cast<uint64_t>(os::abi::SystemCallNumber::OpenFileAt) == 88ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::OpenDirectoryAt) == 89ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::CreateDirectoryAt) == 90ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::RemoveAt) == 91ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::StatAt) == 92ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::ReadSymbolicLinkAt) == 93ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::RenameAt) == 94ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::LinkAt) == 95ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::CreateSymbolicLinkAt) == 96ULL &&
            os::abi::OS_ABI_AT_CURRENT_WORKING_DIRECTORY == UINT64_MAX &&
            os::abi::OS_ABI_AT_REMOVE_DIRECTORY_FLAG == 1ULL &&
            os::abi::OS_ABI_AT_REMOVE_VALID_FLAG_MASK == 1ULL &&
            os::abi::OS_ABI_AT_STAT_NO_FOLLOW_FLAG == 1ULL &&
            os::abi::OS_ABI_AT_STAT_VALID_FLAG_MASK == 1ULL &&
            sizeof(os::abi::AtStatRequest) == os::abi::OS_ABI_AT_STAT_REQUEST_SIZE_BYTES &&
            sizeof(os::abi::AtReadSymbolicLinkRequest) ==
                os::abi::OS_ABI_AT_READ_SYMBOLIC_LINK_REQUEST_SIZE_BYTES &&
            sizeof(os::abi::AtDualPathRequest) == os::abi::OS_ABI_AT_DUAL_PATH_REQUEST_SIZE_BYTES &&
            sizeof(os::abi::AtSymbolicLinkRequest) ==
                os::abi::OS_ABI_AT_SYMBOLIC_LINK_REQUEST_SIZE_BYTES,
        OS_TEST_ABI_V2_AT_PATH_CONTRACT);
    test_context.Expect(
        static_cast<uint64_t>(os::abi::SystemCallNumber::SeekDescriptor) == 97ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::ReadDescriptorAt) == 98ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::WriteDescriptorAt) == 99ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::StatDescriptor) == 100ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::TruncateDescriptor) == 101ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeDescriptorMode) == 102ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeDescriptorOwner) == 103ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::GetFileStatusFlags) == 104ULL &&
            static_cast<uint64_t>(os::abi::SystemCallNumber::SetFileStatusFlags) == 105ULL &&
            os::abi::OS_ABI_SEEK_FROM_BEGINNING == 0ULL &&
            os::abi::OS_ABI_SEEK_FROM_CURRENT == 1ULL && os::abi::OS_ABI_SEEK_FROM_END == 2ULL &&
            os::abi::OS_ABI_FILE_STATUS_READABLE_FLAG == 1ULL &&
            os::abi::OS_ABI_FILE_STATUS_WRITABLE_FLAG == 2ULL &&
            os::abi::OS_ABI_FILE_STATUS_APPEND_FLAG == 4ULL &&
            os::abi::OS_ABI_FILE_STATUS_VALID_FLAG_MASK == 7ULL &&
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_NOT_SEEKABLE == OS_TEST_ABI_V2_EXPECTED_LAST_ERROR,
        OS_TEST_ABI_V2_OPEN_FILE_DESCRIPTION_CONTRACT);
    return test_context.ExitCode();
}
