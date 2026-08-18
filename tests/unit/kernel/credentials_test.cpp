#include "os/kernel/security/credentials.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_CREDENTIALS_SUITE_NAME = "kernel/credentials/unit";
constexpr std::string_view OS_TEST_CREDENTIALS_LINUX_MODE_VALUES =
    "文件类型、权限和默认 umask 必须保持 Linux UAPI 位值";
constexpr std::string_view OS_TEST_CREDENTIALS_ACCESS_CLASSES =
    "访问检查必须按 owner、补充组和 other 三类选择权限";
constexpr std::string_view OS_TEST_CREDENTIALS_ROOT_EXECUTE =
    "root 可越过 DAC 读写但不能执行没有任一执行位的普通文件";
constexpr std::string_view OS_TEST_CREDENTIALS_CREATION_MASK =
    "umask 只清除九个 rwx 位并保留文件类型和特殊位";
constexpr std::string_view OS_TEST_CREDENTIALS_OWNER_CHANGES =
    "非 root 只能修改自己的 mode 并把组改为所属组";
constexpr std::string_view OS_TEST_CREDENTIALS_GROUP_BOUND =
    "补充组必须去重且服从 Kernel 固定容量";

constexpr os::abi::UserIdentifier OS_TEST_CREDENTIALS_OWNER_USER = 1000U;
constexpr os::abi::UserIdentifier OS_TEST_CREDENTIALS_OTHER_USER = 1001U;
constexpr os::abi::GroupIdentifier OS_TEST_CREDENTIALS_OWNER_GROUP = 100U;
constexpr os::abi::GroupIdentifier OS_TEST_CREDENTIALS_SUPPLEMENTARY_GROUP = 200U;

[[nodiscard]] os::kernel::security::Credentials UserCredentials() noexcept {
    os::kernel::security::Credentials credentials{};
    credentials.real_user_identifier = OS_TEST_CREDENTIALS_OWNER_USER;
    credentials.effective_user_identifier = OS_TEST_CREDENTIALS_OWNER_USER;
    credentials.saved_user_identifier = OS_TEST_CREDENTIALS_OWNER_USER;
    credentials.real_group_identifier = OS_TEST_CREDENTIALS_OWNER_GROUP;
    credentials.effective_group_identifier = OS_TEST_CREDENTIALS_OWNER_GROUP;
    credentials.saved_group_identifier = OS_TEST_CREDENTIALS_OWNER_GROUP;
    credentials.supplementary_groups[0ULL] = OS_TEST_CREDENTIALS_SUPPLEMENTARY_GROUP;
    credentials.supplementary_group_count = 1ULL;
    return credentials;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_CREDENTIALS_SUITE_NAME};

    test_context.Expect(os::abi::OS_ABI_FILE_MODE_REGULAR == 0100000U &&
                            os::abi::OS_ABI_FILE_MODE_DIRECTORY == 0040000U &&
                            os::abi::OS_ABI_FILE_MODE_SET_USER_IDENTIFIER == 0004000U &&
                            os::abi::OS_ABI_FILE_MODE_OWNER_READ == 0000400U &&
                            os::abi::OS_ABI_FILE_MODE_OTHER_EXECUTE == 0000001U &&
                            os::abi::OS_ABI_DEFAULT_CREATION_MASK == 0000022U,
                        OS_TEST_CREDENTIALS_LINUX_MODE_VALUES);

    const os::kernel::security::Credentials owner = UserCredentials();
    os::kernel::security::Credentials group_member = owner;
    group_member.effective_user_identifier = OS_TEST_CREDENTIALS_OTHER_USER;
    const bool access_classes_valid =
        os::kernel::security::HasAccess(
            owner, OS_TEST_CREDENTIALS_OWNER_USER, OS_TEST_CREDENTIALS_SUPPLEMENTARY_GROUP,
            os::abi::OS_ABI_FILE_MODE_REGULAR | 0000640U,
            os::kernel::security::OS_KERNEL_ACCESS_READ |
                os::kernel::security::OS_KERNEL_ACCESS_WRITE,
            false) &&
        os::kernel::security::HasAccess(
            group_member, OS_TEST_CREDENTIALS_OWNER_USER,
            OS_TEST_CREDENTIALS_SUPPLEMENTARY_GROUP,
            os::abi::OS_ABI_FILE_MODE_REGULAR | 0000640U,
            os::kernel::security::OS_KERNEL_ACCESS_READ, false) &&
        !os::kernel::security::HasAccess(
            group_member, OS_TEST_CREDENTIALS_OWNER_USER,
            OS_TEST_CREDENTIALS_SUPPLEMENTARY_GROUP,
            os::abi::OS_ABI_FILE_MODE_REGULAR | 0000640U,
            os::kernel::security::OS_KERNEL_ACCESS_WRITE, false);
    test_context.Expect(access_classes_valid, OS_TEST_CREDENTIALS_ACCESS_CLASSES);

    const os::kernel::security::Credentials root =
        os::kernel::security::RootCredentials();
    const bool root_execute_valid =
        os::kernel::security::HasAccess(
            root, OS_TEST_CREDENTIALS_OWNER_USER, OS_TEST_CREDENTIALS_OWNER_GROUP,
            os::abi::OS_ABI_FILE_MODE_REGULAR | 0000600U,
            os::kernel::security::OS_KERNEL_ACCESS_READ |
                os::kernel::security::OS_KERNEL_ACCESS_WRITE,
            false) &&
        !os::kernel::security::HasAccess(
            root, OS_TEST_CREDENTIALS_OWNER_USER, OS_TEST_CREDENTIALS_OWNER_GROUP,
            os::abi::OS_ABI_FILE_MODE_REGULAR | 0000600U,
            os::kernel::security::OS_KERNEL_ACCESS_EXECUTE, false) &&
        os::kernel::security::HasAccess(
            root, OS_TEST_CREDENTIALS_OWNER_USER, OS_TEST_CREDENTIALS_OWNER_GROUP,
            os::abi::OS_ABI_FILE_MODE_DIRECTORY | 0000000U,
            os::kernel::security::OS_KERNEL_ACCESS_EXECUTE, true);
    test_context.Expect(root_execute_valid, OS_TEST_CREDENTIALS_ROOT_EXECUTE);

    const os::abi::FileMode masked_mode = os::kernel::security::ApplyCreationMask(
        os::abi::OS_ABI_FILE_MODE_REGULAR |
            os::abi::OS_ABI_FILE_MODE_SET_GROUP_IDENTIFIER | 0000666U,
        0000027U);
    test_context.Expect(masked_mode == (os::abi::OS_ABI_FILE_MODE_REGULAR |
                                        os::abi::OS_ABI_FILE_MODE_SET_GROUP_IDENTIFIER |
                                        0000640U),
                        OS_TEST_CREDENTIALS_CREATION_MASK);

    const bool owner_changes_valid =
        os::kernel::security::CanChangeMode(owner, OS_TEST_CREDENTIALS_OWNER_USER) &&
        !os::kernel::security::CanChangeMode(owner, OS_TEST_CREDENTIALS_OTHER_USER) &&
        os::kernel::security::CanChangeOwner(
            owner, OS_TEST_CREDENTIALS_OWNER_USER, OS_TEST_CREDENTIALS_OWNER_GROUP,
            os::abi::OS_ABI_IDENTIFIER_UNCHANGED, OS_TEST_CREDENTIALS_SUPPLEMENTARY_GROUP) &&
        !os::kernel::security::CanChangeOwner(
            owner, OS_TEST_CREDENTIALS_OWNER_USER, OS_TEST_CREDENTIALS_OWNER_GROUP,
            OS_TEST_CREDENTIALS_OTHER_USER, OS_TEST_CREDENTIALS_SUPPLEMENTARY_GROUP);
    test_context.Expect(owner_changes_valid, OS_TEST_CREDENTIALS_OWNER_CHANGES);

    os::kernel::security::Credentials mutable_credentials = owner;
    const os::abi::GroupIdentifier duplicate_groups[]{
        OS_TEST_CREDENTIALS_OWNER_GROUP,
        OS_TEST_CREDENTIALS_OWNER_GROUP,
    };
    const bool group_bound_valid =
        os::kernel::security::SetSupplementaryGroups(
            mutable_credentials, duplicate_groups,
            sizeof(duplicate_groups) / sizeof(duplicate_groups[0ULL])) ==
            os::kernel::security::CredentialStatus::InvalidArgument &&
        os::kernel::security::SetSupplementaryGroups(
            mutable_credentials, duplicate_groups,
            os::kernel::security::OS_KERNEL_CREDENTIAL_SUPPLEMENTARY_GROUP_CAPACITY + 1ULL) ==
            os::kernel::security::CredentialStatus::CapacityExhausted;
    test_context.Expect(group_bound_valid, OS_TEST_CREDENTIALS_GROUP_BOUND);

    return test_context.ExitCode();
}
