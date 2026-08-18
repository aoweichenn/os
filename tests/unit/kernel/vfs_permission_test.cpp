#include "os/kernel/fs/memfs.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_PERMISSION_SUITE_NAME = "kernel/vfs_permission/unit";
constexpr std::string_view OS_TEST_VFS_PERMISSION_DEFAULTS =
    "默认 0022 umask 必须生成 root:root 0755 目录和 0644 文件";
constexpr std::string_view OS_TEST_VFS_PERMISSION_TRAVERSAL =
    "路径逐级 search 和最终文件读写必须按有效身份与补充组判定";
constexpr std::string_view OS_TEST_VFS_PERMISSION_UMASK_INHERITANCE =
    "umask、setgid 目录组继承和 fork 文件系统上下文必须保持一致";
constexpr std::string_view OS_TEST_VFS_PERMISSION_STICKY =
    "sticky 目录必须只允许 root、目录 owner 或目标 owner 删除条目";
constexpr std::string_view OS_TEST_VFS_PERMISSION_CHOWN =
    "非 root 只能把自己的文件改到所属组且不能改变 owner UID";

constexpr uint64_t OS_TEST_VFS_PERMISSION_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_PERMISSION_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_PERMISSION_NODE_LIMIT = 64ULL;
constexpr uint64_t OS_TEST_VFS_PERMISSION_MAXIMUM_FILE_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_PERMISSION_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_VFS_PERMISSION_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr os::abi::UserIdentifier OS_TEST_VFS_PERMISSION_USER_A = 1000U;
constexpr os::abi::UserIdentifier OS_TEST_VFS_PERMISSION_USER_B = 1001U;
constexpr os::abi::GroupIdentifier OS_TEST_VFS_PERMISSION_PRIMARY_GROUP = 100U;
constexpr os::abi::GroupIdentifier OS_TEST_VFS_PERMISSION_SHARED_GROUP = 200U;
constexpr uint8_t OS_TEST_VFS_PERMISSION_PRIVATE_PATH[] = {'/', 'p', 'r', 'i', 'v', 'a', 't', 'e'};
constexpr uint8_t OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH[] = {
    '/', 'p', 'r', 'i', 'v', 'a', 't', 'e', '/', 'f', 'i', 'l', 'e',
};
constexpr uint8_t OS_TEST_VFS_PERMISSION_SHARED_PATH[] = {'/', 's', 'h', 'a', 'r', 'e', 'd'};
constexpr uint8_t OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH[] = {
    '/', 's', 'h', 'a', 'r', 'e', 'd', '/', 'f', 'i', 'l', 'e',
};
constexpr uint8_t OS_TEST_VFS_PERMISSION_SHARED_DIRECTORY_PATH[] = {
    '/', 's', 'h', 'a', 'r', 'e', 'd', '/', 'd', 'i', 'r',
};
constexpr uint8_t OS_TEST_VFS_PERMISSION_STICKY_PATH[] = {'/', 's', 't', 'i', 'c', 'k', 'y'};
constexpr uint8_t OS_TEST_VFS_PERMISSION_STICKY_FILE_PATH[] = {
    '/', 's', 't', 'i', 'c', 'k', 'y', '/', 'o', 'w', 'n', 'e', 'd',
};

void SetIdentity(os::kernel::fs::FsContext &context, const os::abi::UserIdentifier user_identifier,
                 const os::abi::GroupIdentifier group_identifier) noexcept {
    context.credentials.real_user_identifier = user_identifier;
    context.credentials.effective_user_identifier = user_identifier;
    context.credentials.saved_user_identifier = user_identifier;
    context.credentials.real_group_identifier = group_identifier;
    context.credentials.effective_group_identifier = group_identifier;
    context.credentials.saved_group_identifier = group_identifier;
}

[[nodiscard]] bool CreateEmptyFile(os::kernel::fs::Vfs &vfs,
                                   const os::kernel::fs::FsContext &context,
                                   const uint8_t *const path,
                                   const uint64_t path_length_bytes) noexcept {
    const os::kernel::fs::OpenOptions options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile file{};
    return vfs.Open(context, path, path_length_bytes, options, file) ==
               os::kernel::fs::Status::Succeeded &&
           vfs.Close(file) == os::kernel::fs::Status::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_PERMISSION_SUITE_NAME};
    alignas(OS_TEST_VFS_PERMISSION_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_bytes[OS_TEST_VFS_PERMISSION_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs memfs{};
    os::kernel::fs::Mount mounts[OS_TEST_VFS_PERMISSION_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext root_context{};
    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_bytes), sizeof(heap_bytes)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        memfs.Initialize(
            heap, OS_TEST_VFS_PERMISSION_SUPERBLOCK_IDENTIFIER, OS_TEST_VFS_PERMISSION_NODE_LIMIT,
            OS_TEST_VFS_PERMISSION_MAXIMUM_FILE_SIZE_BYTES) == os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_VFS_PERMISSION_MOUNT_CAPACITY, memfs.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(root_context) == os::kernel::fs::Status::Succeeded;

    os::kernel::fs::NodeInformation information{};
    const bool defaults_valid =
        initialized &&
        vfs.CreateDirectory(root_context, OS_TEST_VFS_PERMISSION_PRIVATE_PATH,
                            sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        CreateEmptyFile(vfs, root_context, OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH,
                        sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH)) &&
        vfs.Stat(root_context, OS_TEST_VFS_PERMISSION_PRIVATE_PATH,
                 sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_PATH),
                 information) == os::kernel::fs::Status::Succeeded &&
        information.owner_user_identifier == os::abi::OS_ABI_ROOT_USER_IDENTIFIER &&
        information.owner_group_identifier == os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER &&
        information.mode == (os::abi::OS_ABI_FILE_MODE_DIRECTORY | 0000755U) &&
        vfs.Stat(root_context, OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH,
                 sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH),
                 information) == os::kernel::fs::Status::Succeeded &&
        information.mode == (os::abi::OS_ABI_FILE_MODE_REGULAR | 0000644U);
    test_context.Expect(defaults_valid, OS_TEST_VFS_PERMISSION_DEFAULTS);

    os::kernel::fs::FsContext user_context{};
    const os::abi::GroupIdentifier shared_groups[]{OS_TEST_VFS_PERMISSION_SHARED_GROUP};
    const bool user_context_ready =
        defaults_valid &&
        vfs.ChangeOwner(root_context, OS_TEST_VFS_PERMISSION_PRIVATE_PATH,
                        sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_PATH),
                        os::abi::OS_ABI_IDENTIFIER_UNCHANGED,
                        OS_TEST_VFS_PERMISSION_SHARED_GROUP) == os::kernel::fs::Status::Succeeded &&
        vfs.ChangeMode(root_context, OS_TEST_VFS_PERMISSION_PRIVATE_PATH,
                       sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_PATH),
                       0000710U) == os::kernel::fs::Status::Succeeded &&
        vfs.ChangeOwner(root_context, OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH,
                        sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH),
                        OS_TEST_VFS_PERMISSION_USER_A,
                        OS_TEST_VFS_PERMISSION_SHARED_GROUP) == os::kernel::fs::Status::Succeeded &&
        vfs.ChangeMode(root_context, OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH,
                       sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH),
                       0000640U) == os::kernel::fs::Status::Succeeded &&
        vfs.CloneContext(root_context, user_context) == os::kernel::fs::Status::Succeeded;
    SetIdentity(user_context, OS_TEST_VFS_PERMISSION_USER_A, OS_TEST_VFS_PERMISSION_PRIMARY_GROUP);
    const bool groups_installed = os::kernel::security::SetSupplementaryGroups(
                                      user_context.credentials, shared_groups,
                                      sizeof(shared_groups) / sizeof(shared_groups[0ULL])) ==
                                  os::kernel::security::CredentialStatus::Succeeded;
    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    const os::kernel::fs::OpenOptions write_options{
        .readable = false,
        .writable = true,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile opened_file{};
    const bool traversal_valid =
        user_context_ready && groups_installed &&
        vfs.Open(user_context, OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH,
                 sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH), read_options,
                 opened_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Close(opened_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Open(user_context, OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH,
                 sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH), write_options,
                 opened_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Close(opened_file) == os::kernel::fs::Status::Succeeded &&
        vfs.ChangeMode(root_context, OS_TEST_VFS_PERMISSION_PRIVATE_PATH,
                       sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_PATH),
                       0000700U) == os::kernel::fs::Status::Succeeded &&
        vfs.Open(user_context, OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH,
                 sizeof(OS_TEST_VFS_PERMISSION_PRIVATE_FILE_PATH), read_options,
                 opened_file) == os::kernel::fs::Status::PermissionDenied;
    test_context.Expect(traversal_valid, OS_TEST_VFS_PERMISSION_TRAVERSAL);

    user_context.creation_mask = 0000027U;
    const bool inheritance_valid =
        traversal_valid &&
        vfs.CreateDirectory(root_context, OS_TEST_VFS_PERMISSION_SHARED_PATH,
                            sizeof(OS_TEST_VFS_PERMISSION_SHARED_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ChangeOwner(root_context, OS_TEST_VFS_PERMISSION_SHARED_PATH,
                        sizeof(OS_TEST_VFS_PERMISSION_SHARED_PATH), OS_TEST_VFS_PERMISSION_USER_A,
                        OS_TEST_VFS_PERMISSION_SHARED_GROUP) == os::kernel::fs::Status::Succeeded &&
        vfs.ChangeMode(root_context, OS_TEST_VFS_PERMISSION_SHARED_PATH,
                       sizeof(OS_TEST_VFS_PERMISSION_SHARED_PATH),
                       0002770U) == os::kernel::fs::Status::Succeeded &&
        CreateEmptyFile(vfs, user_context, OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH,
                        sizeof(OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH)) &&
        vfs.CreateDirectory(user_context, OS_TEST_VFS_PERMISSION_SHARED_DIRECTORY_PATH,
                            sizeof(OS_TEST_VFS_PERMISSION_SHARED_DIRECTORY_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Stat(root_context, OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH,
                 sizeof(OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH),
                 information) == os::kernel::fs::Status::Succeeded &&
        information.owner_user_identifier == OS_TEST_VFS_PERMISSION_USER_A &&
        information.owner_group_identifier == OS_TEST_VFS_PERMISSION_SHARED_GROUP &&
        information.mode == (os::abi::OS_ABI_FILE_MODE_REGULAR | 0000640U) &&
        vfs.Stat(root_context, OS_TEST_VFS_PERMISSION_SHARED_DIRECTORY_PATH,
                 sizeof(OS_TEST_VFS_PERMISSION_SHARED_DIRECTORY_PATH),
                 information) == os::kernel::fs::Status::Succeeded &&
        information.mode == (os::abi::OS_ABI_FILE_MODE_DIRECTORY | 0002750U);
    os::kernel::fs::FsContext inherited_context{};
    const bool clone_inheritance_valid =
        inheritance_valid &&
        vfs.CloneContext(user_context, inherited_context) == os::kernel::fs::Status::Succeeded &&
        inherited_context.creation_mask == user_context.creation_mask &&
        inherited_context.credentials.effective_user_identifier == OS_TEST_VFS_PERMISSION_USER_A &&
        inherited_context.credentials.supplementary_group_count == 1ULL &&
        inherited_context.credentials.supplementary_groups[0ULL] ==
            OS_TEST_VFS_PERMISSION_SHARED_GROUP;
    test_context.Expect(clone_inheritance_valid, OS_TEST_VFS_PERMISSION_UMASK_INHERITANCE);

    const bool chown_valid = clone_inheritance_valid &&
                             vfs.ChangeOwner(user_context, OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH,
                                             sizeof(OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH),
                                             os::abi::OS_ABI_IDENTIFIER_UNCHANGED,
                                             OS_TEST_VFS_PERMISSION_PRIMARY_GROUP) ==
                                 os::kernel::fs::Status::Succeeded &&
                             vfs.ChangeOwner(user_context, OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH,
                                             sizeof(OS_TEST_VFS_PERMISSION_SHARED_FILE_PATH),
                                             OS_TEST_VFS_PERMISSION_USER_B,
                                             os::abi::OS_ABI_GROUP_IDENTIFIER_UNCHANGED) ==
                                 os::kernel::fs::Status::PermissionDenied;
    test_context.Expect(chown_valid, OS_TEST_VFS_PERMISSION_CHOWN);

    const bool sticky_ready =
        chown_valid &&
        vfs.CreateDirectory(root_context, OS_TEST_VFS_PERMISSION_STICKY_PATH,
                            sizeof(OS_TEST_VFS_PERMISSION_STICKY_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ChangeMode(root_context, OS_TEST_VFS_PERMISSION_STICKY_PATH,
                       sizeof(OS_TEST_VFS_PERMISSION_STICKY_PATH),
                       0001777U) == os::kernel::fs::Status::Succeeded &&
        CreateEmptyFile(vfs, user_context, OS_TEST_VFS_PERMISSION_STICKY_FILE_PATH,
                        sizeof(OS_TEST_VFS_PERMISSION_STICKY_FILE_PATH));
    os::kernel::fs::FsContext other_context{};
    bool other_context_ready = sticky_ready && vfs.CloneContext(root_context, other_context) ==
                                                   os::kernel::fs::Status::Succeeded;
    SetIdentity(other_context, OS_TEST_VFS_PERMISSION_USER_B, OS_TEST_VFS_PERMISSION_PRIMARY_GROUP);
    const bool sticky_valid = other_context_ready &&
                              vfs.RemoveFile(other_context, OS_TEST_VFS_PERMISSION_STICKY_FILE_PATH,
                                             sizeof(OS_TEST_VFS_PERMISSION_STICKY_FILE_PATH)) ==
                                  os::kernel::fs::Status::PermissionDenied &&
                              vfs.RemoveFile(user_context, OS_TEST_VFS_PERMISSION_STICKY_FILE_PATH,
                                             sizeof(OS_TEST_VFS_PERMISSION_STICKY_FILE_PATH)) ==
                                  os::kernel::fs::Status::Succeeded;
    test_context.Expect(sticky_valid, OS_TEST_VFS_PERMISSION_STICKY);

    const bool released =
        vfs.ReleaseContext(other_context) == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(inherited_context) == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(user_context) == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(root_context) == os::kernel::fs::Status::Succeeded &&
        memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded;
    return sticky_valid && released ? test_context.ExitCode() : 1;
}
