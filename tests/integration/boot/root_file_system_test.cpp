#include <os/kernel/fs/root_file_system.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <root_file_system_test_support.hpp>
#include <sparse_memory_block_device.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOTFS_SUITE_NAME = "kernel/root_file_system/integration";
constexpr std::string_view OS_TEST_ROOTFS_MOUNT_AND_VALIDATE =
    "预格式化 rootfs v4 必须挂载并通过全盘一致性验证";
constexpr std::string_view OS_TEST_ROOTFS_DIRECTORY_CLOSE_WITH_DEVICE_FAILURE =
    "已打开目录的 context 释放不得因底层读故障失败";
constexpr std::string_view OS_TEST_ROOTFS_SPARSE_AND_INDIRECT =
    "完整数据区大小的稀疏文件必须经过五级间接块读写且空洞返回零";
constexpr std::string_view OS_TEST_ROOTFS_NAMESPACE_MUTATIONS =
    "删除、替换重命名、非空目录与环路保护必须符合 VFS 契约";
constexpr std::string_view OS_TEST_ROOTFS_PERSISTENCE =
    "同步后重新挂载必须保留命名空间、数据和 inode 代际";
constexpr std::string_view OS_TEST_ROOTFS_READ_ONLY = "只读挂载必须允许查询并拒绝所有命名空间修改";
constexpr std::string_view OS_TEST_ROOTFS_CORRUPTION =
    "损坏的超级块必须被拒绝且绝不能触发隐式格式化";
constexpr std::string_view OS_TEST_ROOTFS_INCOMPLETE_TRANSACTION =
    "commit 后 checkpoint 写失败必须由挂载 replay 恢复完整新事务";
constexpr std::string_view OS_TEST_ROOTFS_ORPHAN_RECOVERY =
    "打开后删除形成的 orphan 必须在模拟断电后的下一次挂载完整回收";
constexpr std::string_view OS_TEST_ROOTFS_STATISTICS =
    "rootfs v4 必须记录事务、稀疏读取与命名空间操作统计";
constexpr std::string_view OS_TEST_ROOTFS_STABLE_MOUNT_GENERATION =
    "VFS 超级块代际在一次挂载生命周期内必须稳定且不得等同于事务序号";
constexpr std::string_view OS_TEST_ROOTFS_LINKS =
    "硬链接必须共享 inode、维护 link count，并在删除单个名称后保留数据";
constexpr std::string_view OS_TEST_ROOTFS_SYMBOLIC_LINKS =
    "符号链接必须支持相对目标、链式解析、readlink、环路拒绝和独立删除";

constexpr uint64_t OS_TEST_ROOTFS_SUPERBLOCK_IDENTIFIER = 41ULL;
constexpr uint64_t OS_TEST_ROOTFS_REMOUNT_IDENTIFIER = 42ULL;
constexpr uint64_t OS_TEST_ROOTFS_READ_ONLY_IDENTIFIER = 43ULL;
constexpr uint64_t OS_TEST_ROOTFS_CORRUPT_IDENTIFIER = 44ULL;
constexpr uint64_t OS_TEST_ROOTFS_FAILURE_IDENTIFIER = 45ULL;
constexpr uint64_t OS_TEST_ROOTFS_INCOMPLETE_IDENTIFIER = 46ULL;
constexpr uint64_t OS_TEST_ROOTFS_ORPHAN_FIRST_IDENTIFIER = 47ULL;
constexpr uint64_t OS_TEST_ROOTFS_ORPHAN_RECOVERY_IDENTIFIER = 48ULL;
constexpr uint64_t OS_TEST_ROOTFS_MOUNT_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_ROOTFS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_ZERO_PROBE_SIZE_BYTES = 97ULL;
constexpr uint64_t OS_TEST_ROOTFS_ZERO_PROBE_OFFSET_BYTES = 8ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_ROOTFS_SHRUNK_SIZE_BYTES = 1024ULL;
constexpr uint64_t OS_TEST_ROOTFS_CORRUPTION_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_TEST_ROOTFS_MINIMUM_CREATE_COUNT = 5ULL;
constexpr uint64_t OS_TEST_ROOTFS_MINIMUM_REMOVE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_ROOTFS_MINIMUM_RENAME_COUNT = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_MINIMUM_TRUNCATE_COUNT = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_MINIMUM_ALLOCATED_INODE_COUNT = 2ULL;
constexpr uint8_t OS_TEST_ROOTFS_CORRUPTION_MASK = 0x20U;

constexpr uint8_t OS_TEST_ROOTFS_ALPHA_PATH[] = {'/', 'a', 'l', 'p', 'h', 'a'};
constexpr uint8_t OS_TEST_ROOTFS_BETA_PATH[] = {'/', 'b', 'e', 't', 'a'};
constexpr uint8_t OS_TEST_ROOTFS_FAILURE_PATH[] = {
    '/', 'f', 'a', 'i', 'l', 'u', 'r', 'e',
};
constexpr uint8_t OS_TEST_ROOTFS_SPARSE_PATH[] = {
    '/', 'a', 'l', 'p', 'h', 'a', '/', 's', 'p', 'a', 'r', 's', 'e',
};
constexpr uint8_t OS_TEST_ROOTFS_RENAMED_PATH[] = {
    '/', 'a', 'l', 'p', 'h', 'a', '/', 'r', 'e', 'n', 'a', 'm', 'e', 'd',
};
constexpr uint8_t OS_TEST_ROOTFS_TARGET_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 't', 'a', 'r', 'g', 'e', 't',
};
constexpr uint8_t OS_TEST_ROOTFS_TEMPORARY_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 't', 'e', 'm', 'p',
};
constexpr uint8_t OS_TEST_ROOTFS_ORPHAN_PATH[] = {
    '/', 'o', 'r', 'p', 'h', 'a', 'n',
};
constexpr uint8_t OS_TEST_ROOTFS_LINK_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 'l', 'i', 'n', 'k', 'e', 'd',
};
constexpr uint8_t OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 't', 'a', 'r', 'g', 'e', 't', '-', 'l', 'i', 'n', 'k',
};
constexpr uint8_t OS_TEST_ROOTFS_CHAIN_LINK_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 'c', 'h', 'a', 'i', 'n', '-', 'l', 'i', 'n', 'k',
};
constexpr uint8_t OS_TEST_ROOTFS_SELF_LINK_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 'l', 'o', 'o', 'p', '-', 'l', 'i', 'n', 'k',
};
constexpr uint8_t OS_TEST_ROOTFS_SYMBOLIC_TARGET[] = {
    't', 'a', 'r', 'g', 'e', 't',
};
constexpr uint8_t OS_TEST_ROOTFS_CHAIN_TARGET[] = {
    't', 'a', 'r', 'g', 'e', 't', '-', 'l', 'i', 'n', 'k',
};
constexpr uint8_t OS_TEST_ROOTFS_SELF_TARGET[] = {
    'l', 'o', 'o', 'p', '-', 'l', 'i', 'n', 'k',
};
constexpr uint8_t OS_TEST_ROOTFS_CHILD_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 'c', 'h', 'i', 'l', 'd',
};
constexpr uint8_t OS_TEST_ROOTFS_LOOP_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 'c', 'h', 'i', 'l', 'd', '/', 'l', 'o', 'o', 'p',
};
constexpr uint8_t OS_TEST_ROOTFS_NOT_EMPTY_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 'n', 'o', 't', '-', 'e', 'm', 'p', 't', 'y',
};
constexpr uint8_t OS_TEST_ROOTFS_NOT_EMPTY_FILE_PATH[] = {
    '/', 'b', 'e', 't', 'a', '/', 'n', 'o', 't', '-',
    'e', 'm', 'p', 't', 'y', '/', 'f', 'i', 'l', 'e',
};
constexpr uint8_t OS_TEST_ROOTFS_PAYLOAD[] = {
    'r', 'o', 'o', 't', 'f', 's', '-', 'v', '2', '-', 't', 'r', 'i',
    'p', 'l', 'e', '-', 'i', 'n', 'd', 'i', 'r', 'e', 'c', 't',
};
constexpr uint8_t OS_TEST_ROOTFS_REPLACED_PAYLOAD[] = {
    'o', 'l', 'd', '-', 'd', 'e', 's', 't', 'i', 'n', 'a', 't', 'i', 'o', 'n',
};

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left, const uint8_t *const right,
                                 const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_ROOTFS_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool BytesAreZero(const uint8_t *const bytes, const uint64_t length_bytes) noexcept {
    if (bytes == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_ROOTFS_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (bytes[byte_index] != 0U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CreateAndWrite(os::kernel::fs::Vfs &vfs,
                                  const os::kernel::fs::FsContext &context,
                                  const uint8_t *const path, const uint64_t path_length_bytes,
                                  const uint8_t *const payload,
                                  const uint64_t payload_length_bytes) noexcept {
    const os::kernel::fs::OpenOptions options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = true,
        .append = false,
    };
    os::kernel::fs::OpenFile file{};
    uint64_t written_bytes = OS_TEST_ROOTFS_EMPTY_VALUE;
    return vfs.Open(context, path, path_length_bytes, options, file) ==
               os::kernel::fs::Status::Succeeded &&
           vfs.Write(file, payload, payload_length_bytes, written_bytes) ==
               os::kernel::fs::Status::Succeeded &&
           written_bytes == payload_length_bytes &&
           vfs.Close(file) == os::kernel::fs::Status::Succeeded;
}

[[nodiscard]] bool ReadAt(os::kernel::fs::Vfs &vfs, const os::kernel::fs::FsContext &context,
                          const uint8_t *const path, const uint64_t path_length_bytes,
                          const uint64_t offset_bytes, uint8_t *const destination,
                          const uint64_t capacity_bytes) noexcept {
    const os::kernel::fs::OpenOptions options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile file{};
    uint64_t read_bytes = OS_TEST_ROOTFS_EMPTY_VALUE;
    if (vfs.Open(context, path, path_length_bytes, options, file) !=
        os::kernel::fs::Status::Succeeded) {
        return false;
    }
    file.offset_bytes = offset_bytes;
    const bool read_succeeded = vfs.Read(file, destination, capacity_bytes, read_bytes) ==
                                    os::kernel::fs::Status::Succeeded &&
                                read_bytes == capacity_bytes;
    return vfs.Close(file) == os::kernel::fs::Status::Succeeded && read_succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_ROOTFS_SUITE_NAME};
    static os::test::SparseMemoryBlockDevice device{};
    static os::kernel::fs::RootFileSystem root_file_system{};
    os::kernel::fs::Mount mounts[OS_TEST_ROOTFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};

    const bool mounted =
        os::test::FormatRootFileSystem(device) &&
        root_file_system.Initialize(device, OS_TEST_ROOTFS_SUPERBLOCK_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_ROOTFS_MOUNT_CAPACITY, root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded;
    test_context.Expect(mounted, OS_TEST_ROOTFS_MOUNT_AND_VALIDATE);
    if (!mounted) {
        return test_context.ExitCode();
    }
    const uint64_t mounted_superblock_generation = root_file_system.GetSuperblock().generation;

    os::kernel::fs::FsContext failure_close_context{};
    const bool failure_close_context_initialized =
        vfs.InitializeContext(failure_close_context) == os::kernel::fs::Status::Succeeded;
    device.SetFailureModes(true, false, false);
    const bool directory_close_avoids_device_io =
        failure_close_context_initialized &&
        vfs.ReleaseContext(failure_close_context) == os::kernel::fs::Status::Succeeded;
    device.SetFailureModes(false, false, false);
    test_context.Expect(directory_close_avoids_device_io,
                        OS_TEST_ROOTFS_DIRECTORY_CLOSE_WITH_DEVICE_FAILURE);

    const bool sparse_created =
        vfs.CreateDirectory(context, OS_TEST_ROOTFS_ALPHA_PATH,
                            sizeof(OS_TEST_ROOTFS_ALPHA_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_ROOTFS_BETA_PATH, sizeof(OS_TEST_ROOTFS_BETA_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        CreateAndWrite(vfs, context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
                       OS_TEST_ROOTFS_PAYLOAD, sizeof(OS_TEST_ROOTFS_PAYLOAD)) &&
        vfs.Truncate(context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
                     os::kernel::fs::OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES) ==
            os::kernel::fs::Status::Succeeded;

    const os::kernel::fs::OpenOptions write_options{
        .readable = false,
        .writable = true,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile sparse_file{};
    uint64_t written_bytes = OS_TEST_ROOTFS_EMPTY_VALUE;
    bool tail_written =
        sparse_created &&
        vfs.Open(context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
                 write_options, sparse_file) == os::kernel::fs::Status::Succeeded;
    if (tail_written) {
        sparse_file.offset_bytes = os::kernel::fs::OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES -
                                   sizeof(OS_TEST_ROOTFS_PAYLOAD);
        tail_written =
            vfs.Write(sparse_file, OS_TEST_ROOTFS_PAYLOAD, sizeof(OS_TEST_ROOTFS_PAYLOAD),
                      written_bytes) == os::kernel::fs::Status::Succeeded &&
            written_bytes == sizeof(OS_TEST_ROOTFS_PAYLOAD) &&
            vfs.Close(sparse_file) == os::kernel::fs::Status::Succeeded;
    }

    uint8_t zero_probe[OS_TEST_ROOTFS_ZERO_PROBE_SIZE_BYTES]{};
    uint8_t tail_payload[sizeof(OS_TEST_ROOTFS_PAYLOAD)]{};
    os::kernel::fs::NodeInformation sparse_information{};
    const bool sparse_valid =
        tail_written &&
        ReadAt(vfs, context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
               OS_TEST_ROOTFS_ZERO_PROBE_OFFSET_BYTES, zero_probe, sizeof(zero_probe)) &&
        BytesAreZero(zero_probe, sizeof(zero_probe)) &&
        ReadAt(vfs, context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
               os::kernel::fs::OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES -
                   sizeof(OS_TEST_ROOTFS_PAYLOAD),
               tail_payload, sizeof(tail_payload)) &&
        BytesAreEqual(tail_payload, OS_TEST_ROOTFS_PAYLOAD, sizeof(tail_payload)) &&
        vfs.Stat(context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
                 sparse_information) == os::kernel::fs::Status::Succeeded &&
        sparse_information.size_bytes == os::kernel::fs::OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES &&
        sparse_information.allocated_size_bytes < sparse_information.size_bytes &&
        sparse_information.birth_time_nanoseconds != OS_TEST_ROOTFS_EMPTY_VALUE &&
        sparse_information.modification_time_nanoseconds >=
            sparse_information.birth_time_nanoseconds &&
        sparse_information.change_time_nanoseconds >=
            sparse_information.modification_time_nanoseconds &&
        vfs.Truncate(context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
                     os::kernel::fs::OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES +
                         OS_TEST_ROOTFS_COUNTER_INCREMENT) == os::kernel::fs::Status::FileTooLarge;
    test_context.Expect(sparse_valid, OS_TEST_ROOTFS_SPARSE_AND_INDIRECT);

    const bool replacement_prepared =
        CreateAndWrite(vfs, context, OS_TEST_ROOTFS_TARGET_PATH, sizeof(OS_TEST_ROOTFS_TARGET_PATH),
                       OS_TEST_ROOTFS_REPLACED_PAYLOAD, sizeof(OS_TEST_ROOTFS_REPLACED_PAYLOAD));
    const bool rename_same_directory =
        vfs.Rename(context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
                   OS_TEST_ROOTFS_RENAMED_PATH, sizeof(OS_TEST_ROOTFS_RENAMED_PATH),
                   false) == os::kernel::fs::Status::Succeeded;
    os::kernel::fs::Path missing_path{};
    const bool original_missing =
        vfs.Resolve(context, OS_TEST_ROOTFS_SPARSE_PATH, sizeof(OS_TEST_ROOTFS_SPARSE_PATH),
                    missing_path) == os::kernel::fs::Status::NotFound;
    const bool no_replace_rejected =
        vfs.Rename(context, OS_TEST_ROOTFS_RENAMED_PATH, sizeof(OS_TEST_ROOTFS_RENAMED_PATH),
                   OS_TEST_ROOTFS_TARGET_PATH, sizeof(OS_TEST_ROOTFS_TARGET_PATH),
                   false) == os::kernel::fs::Status::AlreadyExists;
    const bool replacement_succeeded =
        vfs.Rename(context, OS_TEST_ROOTFS_RENAMED_PATH, sizeof(OS_TEST_ROOTFS_RENAMED_PATH),
                   OS_TEST_ROOTFS_TARGET_PATH, sizeof(OS_TEST_ROOTFS_TARGET_PATH),
                   true) == os::kernel::fs::Status::Succeeded;

    const bool busy_file_created = CreateAndWrite(
        vfs, context, OS_TEST_ROOTFS_TEMPORARY_PATH, sizeof(OS_TEST_ROOTFS_TEMPORARY_PATH),
        OS_TEST_ROOTFS_PAYLOAD, sizeof(OS_TEST_ROOTFS_PAYLOAD));
    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile busy_file{};
    uint8_t orphan_payload[sizeof(OS_TEST_ROOTFS_PAYLOAD)]{};
    uint64_t orphan_read_bytes = OS_TEST_ROOTFS_EMPTY_VALUE;
    const bool open_unlink_valid =
        busy_file_created &&
        vfs.Open(context, OS_TEST_ROOTFS_TEMPORARY_PATH, sizeof(OS_TEST_ROOTFS_TEMPORARY_PATH),
                 read_options, busy_file) == os::kernel::fs::Status::Succeeded &&
        vfs.RemoveFile(context, OS_TEST_ROOTFS_TEMPORARY_PATH,
                       sizeof(OS_TEST_ROOTFS_TEMPORARY_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Resolve(context, OS_TEST_ROOTFS_TEMPORARY_PATH, sizeof(OS_TEST_ROOTFS_TEMPORARY_PATH),
                    missing_path) == os::kernel::fs::Status::NotFound &&
        vfs.Read(busy_file, orphan_payload, sizeof(orphan_payload), orphan_read_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        orphan_read_bytes == sizeof(orphan_payload) &&
        BytesAreEqual(orphan_payload, OS_TEST_ROOTFS_PAYLOAD, sizeof(orphan_payload)) &&
        vfs.Close(busy_file) == os::kernel::fs::Status::Succeeded;

    const bool directory_rules_valid =
        vfs.RemoveDirectory(context, OS_TEST_ROOTFS_ALPHA_PATH,
                            sizeof(OS_TEST_ROOTFS_ALPHA_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_ROOTFS_CHILD_PATH,
                            sizeof(OS_TEST_ROOTFS_CHILD_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Rename(context, OS_TEST_ROOTFS_BETA_PATH, sizeof(OS_TEST_ROOTFS_BETA_PATH),
                   OS_TEST_ROOTFS_LOOP_PATH, sizeof(OS_TEST_ROOTFS_LOOP_PATH),
                   false) == os::kernel::fs::Status::LoopDetected &&
        vfs.RemoveDirectory(context, OS_TEST_ROOTFS_CHILD_PATH,
                            sizeof(OS_TEST_ROOTFS_CHILD_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_ROOTFS_NOT_EMPTY_PATH,
                            sizeof(OS_TEST_ROOTFS_NOT_EMPTY_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        CreateAndWrite(vfs, context, OS_TEST_ROOTFS_NOT_EMPTY_FILE_PATH,
                       sizeof(OS_TEST_ROOTFS_NOT_EMPTY_FILE_PATH), OS_TEST_ROOTFS_PAYLOAD,
                       sizeof(OS_TEST_ROOTFS_PAYLOAD)) &&
        vfs.RemoveDirectory(context, OS_TEST_ROOTFS_NOT_EMPTY_PATH,
                            sizeof(OS_TEST_ROOTFS_NOT_EMPTY_PATH)) ==
            os::kernel::fs::Status::DirectoryNotEmpty &&
        vfs.RemoveFile(context, OS_TEST_ROOTFS_NOT_EMPTY_FILE_PATH,
                       sizeof(OS_TEST_ROOTFS_NOT_EMPTY_FILE_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.RemoveDirectory(context, OS_TEST_ROOTFS_NOT_EMPTY_PATH,
                            sizeof(OS_TEST_ROOTFS_NOT_EMPTY_PATH)) ==
            os::kernel::fs::Status::Succeeded;

    const bool shrunk =
        vfs.Truncate(context, OS_TEST_ROOTFS_TARGET_PATH, sizeof(OS_TEST_ROOTFS_TARGET_PATH),
                     OS_TEST_ROOTFS_SHRUNK_SIZE_BYTES) == os::kernel::fs::Status::Succeeded &&
        vfs.Stat(context, OS_TEST_ROOTFS_TARGET_PATH, sizeof(OS_TEST_ROOTFS_TARGET_PATH),
                 sparse_information) == os::kernel::fs::Status::Succeeded &&
        sparse_information.size_bytes == OS_TEST_ROOTFS_SHRUNK_SIZE_BYTES;
    test_context.Expect(replacement_prepared, "替换目标必须成功建立");
    test_context.Expect(rename_same_directory, "同目录 rename 必须成功");
    test_context.Expect(original_missing, "rename 后原路径必须消失");
    test_context.Expect(no_replace_rejected, "未请求替换时必须拒绝已有目标");
    test_context.Expect(replacement_succeeded, "请求替换时 rename 必须成功");
    test_context.Expect(open_unlink_valid, "打开后删除必须保持已打开文件可读并在 close 回收");
    test_context.Expect(directory_rules_valid, "目录删除与环路规则必须保持成立");
    test_context.Expect(shrunk, "替换后的文件必须能够截断");
    os::kernel::fs::NodeInformation linked_information{};
    uint8_t linked_prefix[sizeof(OS_TEST_ROOTFS_PAYLOAD)]{};
    const bool links_valid =
        shrunk &&
        vfs.Link(context, OS_TEST_ROOTFS_TARGET_PATH, sizeof(OS_TEST_ROOTFS_TARGET_PATH),
                 OS_TEST_ROOTFS_LINK_PATH,
                 sizeof(OS_TEST_ROOTFS_LINK_PATH)) == os::kernel::fs::Status::Succeeded &&
        vfs.Stat(context, OS_TEST_ROOTFS_LINK_PATH, sizeof(OS_TEST_ROOTFS_LINK_PATH),
                 linked_information) == os::kernel::fs::Status::Succeeded &&
        linked_information.node_identifier == sparse_information.node_identifier &&
        linked_information.link_count == 2ULL &&
        vfs.RemoveFile(context, OS_TEST_ROOTFS_TARGET_PATH, sizeof(OS_TEST_ROOTFS_TARGET_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        ReadAt(vfs, context, OS_TEST_ROOTFS_LINK_PATH, sizeof(OS_TEST_ROOTFS_LINK_PATH),
               OS_TEST_ROOTFS_EMPTY_VALUE, linked_prefix, sizeof(linked_prefix)) &&
        BytesAreEqual(linked_prefix, OS_TEST_ROOTFS_PAYLOAD, sizeof(linked_prefix)) &&
        vfs.Link(context, OS_TEST_ROOTFS_LINK_PATH, sizeof(OS_TEST_ROOTFS_LINK_PATH),
                 OS_TEST_ROOTFS_TARGET_PATH,
                 sizeof(OS_TEST_ROOTFS_TARGET_PATH)) == os::kernel::fs::Status::Succeeded &&
        vfs.RemoveFile(context, OS_TEST_ROOTFS_LINK_PATH, sizeof(OS_TEST_ROOTFS_LINK_PATH)) ==
            os::kernel::fs::Status::Succeeded;
    test_context.Expect(links_valid, OS_TEST_ROOTFS_LINKS);
    uint8_t observed_symbolic_target[sizeof(OS_TEST_ROOTFS_SYMBOLIC_TARGET)]{};
    uint64_t observed_symbolic_target_length = OS_TEST_ROOTFS_EMPTY_VALUE;
    os::kernel::fs::Path linked_target{};
    const bool symbolic_created =
        links_valid && vfs.CreateSymbolicLink(context, OS_TEST_ROOTFS_SYMBOLIC_TARGET,
                                              sizeof(OS_TEST_ROOTFS_SYMBOLIC_TARGET),
                                              OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH,
                                              sizeof(OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH)) ==
                           os::kernel::fs::Status::Succeeded;
    const bool symbolic_read =
        symbolic_created &&
        vfs.ReadSymbolicLink(context, OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH,
                             sizeof(OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH), observed_symbolic_target,
                             sizeof(observed_symbolic_target), observed_symbolic_target_length) ==
            os::kernel::fs::Status::Succeeded &&
        observed_symbolic_target_length == sizeof(OS_TEST_ROOTFS_SYMBOLIC_TARGET) &&
        BytesAreEqual(observed_symbolic_target, OS_TEST_ROOTFS_SYMBOLIC_TARGET,
                      sizeof(observed_symbolic_target));
    const bool symbolic_resolved =
        symbolic_read &&
        vfs.Resolve(context, OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH,
                    sizeof(OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH),
                    linked_target) == os::kernel::fs::Status::Succeeded &&
        linked_target.vnode.identifier == sparse_information.node_identifier;
    const bool chain_created =
        symbolic_resolved &&
        vfs.CreateSymbolicLink(context, OS_TEST_ROOTFS_CHAIN_TARGET,
                               sizeof(OS_TEST_ROOTFS_CHAIN_TARGET), OS_TEST_ROOTFS_CHAIN_LINK_PATH,
                               sizeof(OS_TEST_ROOTFS_CHAIN_LINK_PATH)) ==
            os::kernel::fs::Status::Succeeded;
    const bool chain_resolved =
        chain_created &&
        vfs.Resolve(context, OS_TEST_ROOTFS_CHAIN_LINK_PATH, sizeof(OS_TEST_ROOTFS_CHAIN_LINK_PATH),
                    linked_target) == os::kernel::fs::Status::Succeeded &&
        linked_target.vnode.identifier == sparse_information.node_identifier;
    const bool loop_created =
        chain_resolved &&
        vfs.CreateSymbolicLink(context, OS_TEST_ROOTFS_SELF_TARGET,
                               sizeof(OS_TEST_ROOTFS_SELF_TARGET), OS_TEST_ROOTFS_SELF_LINK_PATH,
                               sizeof(OS_TEST_ROOTFS_SELF_LINK_PATH)) ==
            os::kernel::fs::Status::Succeeded;
    const bool loop_rejected =
        loop_created &&
        vfs.Resolve(context, OS_TEST_ROOTFS_SELF_LINK_PATH, sizeof(OS_TEST_ROOTFS_SELF_LINK_PATH),
                    linked_target) == os::kernel::fs::Status::LoopDetected;
    const bool chain_removed =
        loop_rejected &&
        vfs.RemoveFile(context, OS_TEST_ROOTFS_CHAIN_LINK_PATH,
                       sizeof(OS_TEST_ROOTFS_CHAIN_LINK_PATH)) == os::kernel::fs::Status::Succeeded;
    const bool symbolic_removed =
        chain_removed && vfs.RemoveFile(context, OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH,
                                        sizeof(OS_TEST_ROOTFS_SYMBOLIC_LINK_PATH)) ==
                             os::kernel::fs::Status::Succeeded;
    const bool symbolic_links_removed =
        symbolic_removed &&
        vfs.RemoveFile(context, OS_TEST_ROOTFS_SELF_LINK_PATH,
                       sizeof(OS_TEST_ROOTFS_SELF_LINK_PATH)) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(symbolic_created, "符号链接必须原子创建");
    test_context.Expect(symbolic_read, "readlink 必须返回原始相对目标");
    test_context.Expect(symbolic_resolved, "相对符号链接必须解析到同目录目标");
    test_context.Expect(chain_resolved, "链式符号链接必须解析到最终目标");
    test_context.Expect(loop_rejected, "符号链接环必须在有界跳数后拒绝");
    test_context.Expect(chain_removed, "链式符号链接名称必须能够独立删除");
    test_context.Expect(symbolic_removed, "普通符号链接名称必须能够独立删除");
    test_context.Expect(symbolic_links_removed, "自环符号链接名称必须能够独立删除");
    const bool symbolic_links_valid = symbolic_links_removed;
    test_context.Expect(symbolic_links_valid, OS_TEST_ROOTFS_SYMBOLIC_LINKS);
    const bool namespace_valid =
        replacement_prepared && rename_same_directory && original_missing && no_replace_rejected &&
        replacement_succeeded && open_unlink_valid && directory_rules_valid &&
        symbolic_links_valid && vfs.Validate() == os::kernel::fs::Status::Succeeded;
    test_context.Expect(namespace_valid, OS_TEST_ROOTFS_NAMESPACE_MUTATIONS);

    const os::kernel::fs::NodeInformation before_remount = sparse_information;
    const bool first_instance_released =
        vfs.Sync() == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded;
    static os::kernel::fs::RootFileSystem remounted_root_file_system{};
    os::kernel::fs::Mount remounted_mounts[OS_TEST_ROOTFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs remounted_vfs{};
    os::kernel::fs::FsContext remounted_context{};
    os::kernel::fs::NodeInformation after_remount{};
    uint8_t remounted_prefix[sizeof(OS_TEST_ROOTFS_PAYLOAD)]{};
    const bool persistence_valid =
        first_instance_released &&
        remounted_root_file_system.Initialize(device, OS_TEST_ROOTFS_REMOUNT_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        remounted_vfs.Initialize(remounted_mounts, OS_TEST_ROOTFS_MOUNT_CAPACITY,
                                 remounted_root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        remounted_vfs.InitializeContext(remounted_context) == os::kernel::fs::Status::Succeeded &&
        remounted_vfs.Stat(remounted_context, OS_TEST_ROOTFS_TARGET_PATH,
                           sizeof(OS_TEST_ROOTFS_TARGET_PATH),
                           after_remount) == os::kernel::fs::Status::Succeeded &&
        before_remount.node_identifier == after_remount.node_identifier &&
        before_remount.generation == after_remount.generation &&
        before_remount.size_bytes == after_remount.size_bytes &&
        ReadAt(remounted_vfs, remounted_context, OS_TEST_ROOTFS_TARGET_PATH,
               sizeof(OS_TEST_ROOTFS_TARGET_PATH), OS_TEST_ROOTFS_EMPTY_VALUE, remounted_prefix,
               sizeof(remounted_prefix)) &&
        BytesAreEqual(remounted_prefix, OS_TEST_ROOTFS_PAYLOAD, sizeof(remounted_prefix)) &&
        remounted_vfs.Validate() == os::kernel::fs::Status::Succeeded;
    test_context.Expect(persistence_valid, OS_TEST_ROOTFS_PERSISTENCE);

    static os::kernel::fs::RootFileSystem read_only_root_file_system{};
    os::kernel::fs::Mount read_only_mounts[OS_TEST_ROOTFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs read_only_vfs{};
    os::kernel::fs::FsContext read_only_context{};
    os::kernel::fs::NodeInformation read_only_information{};
    const bool read_only_valid =
        persistence_valid &&
        read_only_root_file_system.Initialize(device, OS_TEST_ROOTFS_READ_ONLY_IDENTIFIER, true) ==
            os::kernel::fs::Status::Succeeded &&
        read_only_vfs.Initialize(read_only_mounts, OS_TEST_ROOTFS_MOUNT_CAPACITY,
                                 read_only_root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        read_only_vfs.InitializeContext(read_only_context) == os::kernel::fs::Status::Succeeded &&
        read_only_vfs.Stat(read_only_context, OS_TEST_ROOTFS_TARGET_PATH,
                           sizeof(OS_TEST_ROOTFS_TARGET_PATH),
                           read_only_information) == os::kernel::fs::Status::Succeeded &&
        read_only_vfs.RemoveFile(read_only_context, OS_TEST_ROOTFS_TARGET_PATH,
                                 sizeof(OS_TEST_ROOTFS_TARGET_PATH)) ==
            os::kernel::fs::Status::ReadOnly &&
        read_only_vfs.ReleaseContext(read_only_context) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(read_only_valid, OS_TEST_ROOTFS_READ_ONLY);

    static os::test::SparseMemoryBlockDevice corrupt_device{};
    static os::kernel::fs::RootFileSystem corrupt_root_file_system{};
    const bool corruption_rejected =
        os::test::FormatRootFileSystem(corrupt_device) &&
        (corrupt_device.XorByte(os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
                                    os::kernel::fs::OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK,
                                OS_TEST_ROOTFS_CORRUPTION_OFFSET_BYTES,
                                OS_TEST_ROOTFS_CORRUPTION_MASK),
         true) &&
        corrupt_root_file_system.Initialize(corrupt_device, OS_TEST_ROOTFS_CORRUPT_IDENTIFIER) ==
            os::kernel::fs::Status::Corrupt;
    test_context.Expect(corruption_rejected, OS_TEST_ROOTFS_CORRUPTION);

    static os::test::SparseMemoryBlockDevice failure_device{};
    static os::kernel::fs::RootFileSystem failure_root_file_system{};
    os::kernel::fs::Mount failure_mounts[OS_TEST_ROOTFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs failure_vfs{};
    os::kernel::fs::FsContext failure_context{};
    const uint64_t inode_bitmap_logical_block_address =
        os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
        os::kernel::fs::OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK;
    const bool failure_mounted =
        os::test::FormatRootFileSystem(failure_device) &&
        failure_root_file_system.Initialize(failure_device, OS_TEST_ROOTFS_FAILURE_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        failure_vfs.Initialize(failure_mounts, OS_TEST_ROOTFS_MOUNT_CAPACITY,
                               failure_root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        failure_vfs.InitializeContext(failure_context) == os::kernel::fs::Status::Succeeded;
    failure_device.SetTargetedWriteFailure(inode_bitmap_logical_block_address, true);
    const bool mutation_failed =
        failure_mounted && failure_vfs.CreateDirectory(failure_context, OS_TEST_ROOTFS_FAILURE_PATH,
                                                       sizeof(OS_TEST_ROOTFS_FAILURE_PATH)) ==
                               os::kernel::fs::Status::DeviceFailure;
    failure_device.SetTargetedWriteFailure(inode_bitmap_logical_block_address, false);
    static os::kernel::fs::RootFileSystem recovered_root_file_system{};
    os::kernel::fs::Mount recovered_mounts[OS_TEST_ROOTFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs recovered_vfs{};
    os::kernel::fs::FsContext recovered_context{};
    os::kernel::fs::NodeInformation recovered_information{};
    const bool committed_transaction_replayed =
        mutation_failed &&
        recovered_root_file_system.Initialize(failure_device,
                                              OS_TEST_ROOTFS_INCOMPLETE_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        recovered_vfs.Initialize(recovered_mounts, OS_TEST_ROOTFS_MOUNT_CAPACITY,
                                 recovered_root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        recovered_vfs.InitializeContext(recovered_context) == os::kernel::fs::Status::Succeeded &&
        recovered_vfs.Stat(recovered_context, OS_TEST_ROOTFS_FAILURE_PATH,
                           sizeof(OS_TEST_ROOTFS_FAILURE_PATH),
                           recovered_information) == os::kernel::fs::Status::Succeeded &&
        recovered_information.type == os::kernel::fs::NodeType::Directory &&
        recovered_root_file_system.ReadStatistics().journal.replay_count ==
            OS_TEST_ROOTFS_COUNTER_INCREMENT &&
        recovered_vfs.ReleaseContext(recovered_context) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(committed_transaction_replayed, OS_TEST_ROOTFS_INCOMPLETE_TRANSACTION);

    static os::test::SparseMemoryBlockDevice orphan_device{};
    static os::kernel::fs::RootFileSystem orphan_first_file_system{};
    os::kernel::fs::Mount orphan_first_mounts[OS_TEST_ROOTFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs orphan_first_vfs{};
    os::kernel::fs::FsContext orphan_first_context{};
    os::kernel::fs::OpenFile orphan_open_file{};
    const bool orphan_committed =
        os::test::FormatRootFileSystem(orphan_device) &&
        orphan_first_file_system.Initialize(orphan_device,
                                            OS_TEST_ROOTFS_ORPHAN_FIRST_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        orphan_first_vfs.Initialize(orphan_first_mounts, OS_TEST_ROOTFS_MOUNT_CAPACITY,
                                    orphan_first_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        orphan_first_vfs.InitializeContext(orphan_first_context) ==
            os::kernel::fs::Status::Succeeded &&
        CreateAndWrite(orphan_first_vfs, orphan_first_context, OS_TEST_ROOTFS_ORPHAN_PATH,
                       sizeof(OS_TEST_ROOTFS_ORPHAN_PATH), OS_TEST_ROOTFS_PAYLOAD,
                       sizeof(OS_TEST_ROOTFS_PAYLOAD)) &&
        orphan_first_vfs.Open(orphan_first_context, OS_TEST_ROOTFS_ORPHAN_PATH,
                              sizeof(OS_TEST_ROOTFS_ORPHAN_PATH), read_options,
                              orphan_open_file) == os::kernel::fs::Status::Succeeded &&
        orphan_first_vfs.RemoveFile(orphan_first_context, OS_TEST_ROOTFS_ORPHAN_PATH,
                                    sizeof(OS_TEST_ROOTFS_ORPHAN_PATH)) ==
            os::kernel::fs::Status::Succeeded;
    test_context.Expect(orphan_committed, "orphan 删除事务必须先成功提交");
    static os::kernel::fs::RootFileSystem orphan_recovered_file_system{};
    os::kernel::fs::Mount orphan_recovered_mounts[OS_TEST_ROOTFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs orphan_recovered_vfs{};
    os::kernel::fs::FsContext orphan_recovered_context{};
    const os::kernel::fs::Status orphan_recovery_mount_status =
        orphan_committed ? orphan_recovered_file_system.Initialize(
                               orphan_device, OS_TEST_ROOTFS_ORPHAN_RECOVERY_IDENTIFIER)
                         : os::kernel::fs::Status::Corrupt;
    test_context.Expect(orphan_recovery_mount_status == os::kernel::fs::Status::Succeeded,
                        "orphan 镜像必须在下一次挂载时完成回收");
    const bool orphan_recovered =
        orphan_committed && orphan_recovery_mount_status == os::kernel::fs::Status::Succeeded &&
        orphan_recovered_vfs.Initialize(orphan_recovered_mounts, OS_TEST_ROOTFS_MOUNT_CAPACITY,
                                        orphan_recovered_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        orphan_recovered_vfs.InitializeContext(orphan_recovered_context) ==
            os::kernel::fs::Status::Succeeded &&
        orphan_recovered_vfs.Resolve(orphan_recovered_context, OS_TEST_ROOTFS_ORPHAN_PATH,
                                     sizeof(OS_TEST_ROOTFS_ORPHAN_PATH),
                                     missing_path) == os::kernel::fs::Status::NotFound &&
        orphan_recovered_file_system.ReadStatistics().orphan_reap_count ==
            OS_TEST_ROOTFS_COUNTER_INCREMENT &&
        orphan_recovered_vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        orphan_recovered_vfs.ReleaseContext(orphan_recovered_context) ==
            os::kernel::fs::Status::Succeeded;
    test_context.Expect(orphan_recovered, OS_TEST_ROOTFS_ORPHAN_RECOVERY);

    const os::kernel::fs::RootFileSystemStatistics statistics = root_file_system.ReadStatistics();
    const bool statistics_valid =
        statistics.transaction_generation >
            os::test::OS_TEST_ROOTFS_INITIAL_TRANSACTION_GENERATION &&
        statistics.sparse_hole_read_bytes >= OS_TEST_ROOTFS_ZERO_PROBE_SIZE_BYTES &&
        statistics.create_count >= OS_TEST_ROOTFS_MINIMUM_CREATE_COUNT &&
        statistics.remove_count >= OS_TEST_ROOTFS_MINIMUM_REMOVE_COUNT &&
        statistics.rename_count >= OS_TEST_ROOTFS_MINIMUM_RENAME_COUNT &&
        statistics.truncate_count >= OS_TEST_ROOTFS_MINIMUM_TRUNCATE_COUNT &&
        statistics.allocated_inode_count >= OS_TEST_ROOTFS_MINIMUM_ALLOCATED_INODE_COUNT &&
        statistics.open_reference_count == OS_TEST_ROOTFS_EMPTY_VALUE;
    test_context.Expect(statistics_valid, OS_TEST_ROOTFS_STATISTICS);
    test_context.Expect(root_file_system.GetSuperblock().generation ==
                                mounted_superblock_generation &&
                            statistics.transaction_generation > mounted_superblock_generation,
                        OS_TEST_ROOTFS_STABLE_MOUNT_GENERATION);

    if (persistence_valid) {
        static_cast<void>(remounted_vfs.ReleaseContext(remounted_context));
    }
    return test_context.ExitCode();
}
