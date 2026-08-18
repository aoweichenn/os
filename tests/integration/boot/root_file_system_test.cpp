#include "os/kernel/fs/root_file_system.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "root_file_system_test_support.hpp"
#include "sparse_memory_block_device.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOTFS_SUITE_NAME = "kernel/root_file_system/integration";
constexpr std::string_view OS_TEST_ROOTFS_MOUNT_AND_VALIDATE =
    "预格式化 rootfs v2 必须挂载并通过全盘一致性验证";
constexpr std::string_view OS_TEST_ROOTFS_SPARSE_AND_INDIRECT =
    "64 MiB 稀疏文件必须经过三级间接块读写且空洞返回零";
constexpr std::string_view OS_TEST_ROOTFS_NAMESPACE_MUTATIONS =
    "删除、替换重命名、非空目录与环路保护必须符合 VFS 契约";
constexpr std::string_view OS_TEST_ROOTFS_PERSISTENCE =
    "同步后重新挂载必须保留命名空间、数据和 inode 代际";
constexpr std::string_view OS_TEST_ROOTFS_READ_ONLY = "只读挂载必须允许查询并拒绝所有命名空间修改";
constexpr std::string_view OS_TEST_ROOTFS_CORRUPTION =
    "损坏的超级块必须被拒绝且绝不能触发隐式格式化";
constexpr std::string_view OS_TEST_ROOTFS_INCOMPLETE_TRANSACTION =
    "commit 后 checkpoint 写失败必须由挂载 replay 恢复完整新事务";
constexpr std::string_view OS_TEST_ROOTFS_STATISTICS =
    "rootfs v2 必须记录事务、稀疏读取与命名空间操作统计";
constexpr std::string_view OS_TEST_ROOTFS_STABLE_MOUNT_GENERATION =
    "VFS 超级块代际在一次挂载生命周期内必须稳定且不得等同于事务序号";

constexpr uint64_t OS_TEST_ROOTFS_SUPERBLOCK_IDENTIFIER = 41ULL;
constexpr uint64_t OS_TEST_ROOTFS_REMOUNT_IDENTIFIER = 42ULL;
constexpr uint64_t OS_TEST_ROOTFS_READ_ONLY_IDENTIFIER = 43ULL;
constexpr uint64_t OS_TEST_ROOTFS_CORRUPT_IDENTIFIER = 44ULL;
constexpr uint64_t OS_TEST_ROOTFS_FAILURE_IDENTIFIER = 45ULL;
constexpr uint64_t OS_TEST_ROOTFS_INCOMPLETE_IDENTIFIER = 46ULL;
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
    const bool busy_protected =
        busy_file_created &&
        vfs.Open(context, OS_TEST_ROOTFS_TEMPORARY_PATH, sizeof(OS_TEST_ROOTFS_TEMPORARY_PATH),
                 read_options, busy_file) == os::kernel::fs::Status::Succeeded &&
        vfs.RemoveFile(context, OS_TEST_ROOTFS_TEMPORARY_PATH,
                       sizeof(OS_TEST_ROOTFS_TEMPORARY_PATH)) == os::kernel::fs::Status::Busy &&
        vfs.Close(busy_file) == os::kernel::fs::Status::Succeeded &&
        vfs.RemoveFile(context, OS_TEST_ROOTFS_TEMPORARY_PATH,
                       sizeof(OS_TEST_ROOTFS_TEMPORARY_PATH)) == os::kernel::fs::Status::Succeeded;

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
    const bool namespace_valid = replacement_prepared && rename_same_directory &&
                                 original_missing && no_replace_rejected && replacement_succeeded &&
                                 busy_protected && directory_rules_valid && shrunk &&
                                 vfs.Validate() == os::kernel::fs::Status::Succeeded;
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
