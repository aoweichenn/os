#include <os/kernel/fs/root_inode_metadata.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_METADATA_SUITE_NAME = "kernel/root_inode_metadata/unit";
constexpr std::string_view OS_TEST_ROOT_METADATA_EXTENSION_MESSAGE =
    "inode extension 必须按 feature 约束 pointer/generation 并拒绝未知位";
constexpr std::string_view OS_TEST_ROOT_METADATA_XATTR_MESSAGE =
    "xattr set/get/replace/remove 与 variable block CRC 必须保持排序和容量";
constexpr std::string_view OS_TEST_ROOT_METADATA_ACL_MESSAGE =
    "POSIX ACL owner/named user/group/mask/other 选择与拒绝必须符合优先级";
constexpr std::string_view OS_TEST_ROOT_METADATA_QUOTA_MESSAGE =
    "user/group quota hard/soft grace、失败不变和 CRC block 必须守恒";
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_METADATA_UUID{
    .low = 0x584154545241434CULL,
    .high = 0x1020304050607080ULL,
};
constexpr uint8_t OS_TEST_ROOT_METADATA_CORRUPTION_MASK = 0x40U;

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_METADATA_SUITE_NAME};
    os::kernel::fs::RootInodeExtension extension{
        .flags = os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS |
                 os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_XATTR |
                 os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_ACL |
                 os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_QUOTA,
        .extent_root_relative_block = 300ULL,
        .xattr_relative_block = 301ULL,
        .directory_index_root_relative_block = 0ULL,
        .project_identifier = 7ULL,
        .acl_generation = 2ULL,
        .quota_generation = 3ULL,
    };
    uint8_t extension_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_SIZE_BYTES]{};
    os::kernel::fs::RootInodeExtension decoded_extension{};
    os::kernel::fs::RootInodeExtension unknown_extension = extension;
    unknown_extension.flags |= 1ULL << 63ULL;
    const bool extension_valid =
        os::kernel::fs::EncodeRootInodeExtension(extension, extension_bytes,
                                                 sizeof(extension_bytes)) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        os::kernel::fs::DecodeRootInodeExtension(extension_bytes, sizeof(extension_bytes),
                                                 decoded_extension) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        decoded_extension.project_identifier == 7ULL &&
        os::kernel::fs::EncodeRootInodeExtension(unknown_extension, extension_bytes,
                                                 sizeof(extension_bytes)) ==
            os::kernel::fs::RootInodeMetadataStatus::InvalidArgument;
    context.Expect(extension_valid, OS_TEST_ROOT_METADATA_EXTENSION_MESSAGE);

    os::kernel::fs::RootXattrSet xattrs{};
    const uint8_t name_one[] = {'c', 'o', 'l', 'o', 'r'};
    const uint8_t value_one[] = {'b', 'l', 'u', 'e'};
    const uint8_t value_two[] = {'g', 'r', 'e', 'e', 'n'};
    const uint8_t acl_name[] = {'p', 'o', 's', 'i', 'x', '_', 'a', 'c', 'l'};
    const uint8_t acl_value[] = {1U, 2U, 3U, 4U};
    os::kernel::fs::RootXattrEntry observed_xattr{};
    static os::kernel::fs::RootXattrBlock xattr_block{};
    static os::kernel::fs::RootXattrBlock decoded_xattr{};
    uint8_t xattr_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    bool xattr_valid =
        xattrs.Initialize(32ULL, 1ULL, OS_TEST_ROOT_METADATA_UUID) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        xattrs.Set(os::kernel::fs::RootXattrNamespace::User, name_one, sizeof(name_one), value_one,
                   sizeof(value_one)) == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        xattrs.Set(os::kernel::fs::RootXattrNamespace::System, acl_name, sizeof(acl_name),
                   acl_value,
                   sizeof(acl_value)) == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        xattrs.Set(os::kernel::fs::RootXattrNamespace::User, name_one, sizeof(name_one), value_two,
                   sizeof(value_two)) == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        xattrs.Get(os::kernel::fs::RootXattrNamespace::User, name_one, sizeof(name_one),
                   observed_xattr) == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        observed_xattr.value_size_bytes == sizeof(value_two) &&
        xattrs.Export(xattr_block) == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        os::kernel::fs::EncodeRootXattrBlock(xattr_block, xattr_bytes, sizeof(xattr_bytes)) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        os::kernel::fs::DecodeRootXattrBlock(xattr_bytes, sizeof(xattr_bytes), decoded_xattr) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        decoded_xattr.entry_count == 2ULL &&
        xattrs.Remove(os::kernel::fs::RootXattrNamespace::User, name_one, sizeof(name_one)) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        xattrs.Validate() == os::kernel::fs::RootInodeMetadataStatus::Succeeded;
    xattr_bytes[64] ^= OS_TEST_ROOT_METADATA_CORRUPTION_MASK;
    xattr_valid = xattr_valid && os::kernel::fs::DecodeRootXattrBlock(
                                     xattr_bytes, sizeof(xattr_bytes), decoded_xattr) ==
                                     os::kernel::fs::RootInodeMetadataStatus::InvalidChecksum;
    context.Expect(xattr_valid, OS_TEST_ROOT_METADATA_XATTR_MESSAGE);

    os::kernel::fs::RootAcl acl{
        .entries = {},
        .entry_count = 7ULL,
        .generation = 1ULL,
    };
    acl.entries[0] = {os::kernel::fs::RootAclEntryType::UserObject, 0ULL, 7U};
    acl.entries[1] = {os::kernel::fs::RootAclEntryType::NamedUser, 2000ULL, 0U};
    acl.entries[2] = {os::kernel::fs::RootAclEntryType::GroupObject, 0ULL, 6U};
    acl.entries[3] = {os::kernel::fs::RootAclEntryType::NamedGroup, 3000ULL, 4U};
    acl.entries[4] = {os::kernel::fs::RootAclEntryType::Mask, 0ULL, 4U};
    acl.entries[5] = {os::kernel::fs::RootAclEntryType::Other, 0ULL, 1U};
    acl.entries[6] = {os::kernel::fs::RootAclEntryType::NamedUser, 2001ULL, 6U};
    const os::abi::GroupIdentifier groups[] = {1000U, 3000U};
    const bool acl_valid =
        os::kernel::fs::ValidateRootAcl(acl) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        os::kernel::fs::EvaluateRootAcl(acl, 1000U, 1000U, 1000U, groups, 2ULL, 6U) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        os::kernel::fs::EvaluateRootAcl(acl, 1000U, 1000U, 2000U, groups, 2ULL, 4U) ==
            os::kernel::fs::RootInodeMetadataStatus::PermissionDenied &&
        os::kernel::fs::EvaluateRootAcl(acl, 1000U, 1000U, 2001U, groups, 2ULL, 4U) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        os::kernel::fs::EvaluateRootAcl(acl, 1000U, 1000U, 4000U, groups, 2ULL, 2U) ==
            os::kernel::fs::RootInodeMetadataStatus::PermissionDenied;
    context.Expect(acl_valid, OS_TEST_ROOT_METADATA_ACL_MESSAGE);

    os::kernel::fs::RootQuotaManager quota{};
    static os::kernel::fs::RootQuotaBlock quota_block{};
    static os::kernel::fs::RootQuotaBlock decoded_quota{};
    uint8_t quota_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    bool quota_valid =
        quota.Initialize(OS_TEST_ROOT_METADATA_UUID) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        quota.SetLimits(os::kernel::fs::RootQuotaType::User, 1000ULL, 8ULL, 10ULL, 4ULL, 5ULL,
                        100ULL) == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        quota.Charge(os::kernel::fs::RootQuotaType::User, 1000ULL, 8ULL, 4ULL, 50ULL) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        quota.Charge(os::kernel::fs::RootQuotaType::User, 1000ULL, 1ULL, 0ULL, 101ULL) ==
            os::kernel::fs::RootInodeMetadataStatus::QuotaExceeded &&
        quota.Charge(os::kernel::fs::RootQuotaType::User, 1000ULL, 3ULL, 0ULL, 50ULL) ==
            os::kernel::fs::RootInodeMetadataStatus::QuotaExceeded &&
        quota.Release(os::kernel::fs::RootQuotaType::User, 1000ULL, 2ULL, 1ULL) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        quota.Validate() == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        quota.Export(quota_block) == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        quota_block.records[0].used_block_count == 6ULL &&
        os::kernel::fs::EncodeRootQuotaBlock(quota_block, quota_bytes, sizeof(quota_bytes)) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
        os::kernel::fs::DecodeRootQuotaBlock(quota_bytes, sizeof(quota_bytes), decoded_quota) ==
            os::kernel::fs::RootInodeMetadataStatus::Succeeded;
    quota_bytes[64] ^= OS_TEST_ROOT_METADATA_CORRUPTION_MASK;
    quota_valid = quota_valid && os::kernel::fs::DecodeRootQuotaBlock(
                                     quota_bytes, sizeof(quota_bytes), decoded_quota) ==
                                     os::kernel::fs::RootInodeMetadataStatus::InvalidChecksum;
    context.Expect(quota_valid, OS_TEST_ROOT_METADATA_QUOTA_MESSAGE);
    return context.ExitCode();
}
