#include "os/kernel/fs/root_file_system.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "root_file_system_capacity_test_support.hpp"
#include "sparse_memory_block_device.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOTFS_CAPACITY_SUITE_NAME =
    "kernel/root_file_system/capacity/integration";
constexpr std::string_view OS_TEST_ROOTFS_CAPACITY_SHORT_WRITE =
    "真实 256 MiB rootfs 填满后必须先短写成功，再以零字节明确报告 ENOSPC";
constexpr std::string_view OS_TEST_ROOTFS_CAPACITY_CONSISTENCY =
    "容量耗尽后的盘面必须保持 clean、一致且零数据块继续稀疏存储";

constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_SUPERBLOCK_IDENTIFIER = 51ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_MOUNT_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_REQUESTED_FREE_BLOCK_COUNT = 8ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_WRITE_BLOCK_COUNT = 16ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_WRITE_SIZE_BYTES =
    OS_TEST_ROOTFS_CAPACITY_WRITE_BLOCK_COUNT * os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_PROBE_SIZE_BYTES =
    os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_EXPECTED_SHORT_WRITE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_MAXIMUM_REMAINING_BLOCK_COUNT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_CAPACITY_MAXIMUM_STORED_BLOCK_COUNT = 32768ULL;
constexpr uint8_t OS_TEST_ROOTFS_CAPACITY_PAYLOAD_BYTE = 0xA5U;

}

int main() {
    os::test::TestContext test_context{OS_TEST_ROOTFS_CAPACITY_SUITE_NAME};
    static os::test::SparseMemoryBlockDevice device{};
    os::test::RootFileSystemCapacityImageInformation image_information{};
    static os::kernel::fs::RootFileSystem root_file_system{};
    os::kernel::fs::Mount mounts[OS_TEST_ROOTFS_CAPACITY_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};
    const bool mounted =
        os::test::FormatNearCapacityRootFileSystem(
            device, OS_TEST_ROOTFS_CAPACITY_REQUESTED_FREE_BLOCK_COUNT, image_information) &&
        root_file_system.Initialize(device, OS_TEST_ROOTFS_CAPACITY_SUPERBLOCK_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_ROOTFS_CAPACITY_MOUNT_CAPACITY,
                       root_file_system.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded;
    if (!mounted) {
        test_context.Expect(false, OS_TEST_ROOTFS_CAPACITY_SHORT_WRITE);
        return test_context.ExitCode();
    }

    uint8_t payload[OS_TEST_ROOTFS_CAPACITY_WRITE_SIZE_BYTES]{};
    for (uint64_t byte_index = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE; byte_index < sizeof(payload);
         ++byte_index) {
        payload[byte_index] = OS_TEST_ROOTFS_CAPACITY_PAYLOAD_BYTE;
    }
    const os::kernel::fs::OpenOptions options{
        .readable = false,
        .writable = true,
        .create = false,
        .truncate = false,
    };
    os::kernel::fs::OpenFile file{};
    uint64_t short_written_bytes = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    uint64_t rejected_written_bytes = OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE;
    const bool file_opened = vfs.Open(context, os::test::OS_TEST_ROOTFS_CAPACITY_TARGET_PATH,
                                      sizeof(os::test::OS_TEST_ROOTFS_CAPACITY_TARGET_PATH),
                                      options, file) == os::kernel::fs::Status::Succeeded;
    const bool short_write_succeeded =
        file_opened &&
        vfs.Write(file, payload, sizeof(payload), short_written_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        short_written_bytes > OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE &&
        short_written_bytes < sizeof(payload);
    const bool capacity_rejected =
        short_write_succeeded &&
        vfs.Write(file, payload, OS_TEST_ROOTFS_CAPACITY_PROBE_SIZE_BYTES,
                  rejected_written_bytes) == os::kernel::fs::Status::CapacityExhausted &&
        rejected_written_bytes == OS_TEST_ROOTFS_CAPACITY_EMPTY_VALUE &&
        vfs.Close(file) == os::kernel::fs::Status::Succeeded;
    os::kernel::fs::NodeInformation information{};
    const bool size_preserved = capacity_rejected &&
                                vfs.Stat(context, os::test::OS_TEST_ROOTFS_CAPACITY_TARGET_PATH,
                                         sizeof(os::test::OS_TEST_ROOTFS_CAPACITY_TARGET_PATH),
                                         information) == os::kernel::fs::Status::Succeeded &&
                                information.size_bytes == short_written_bytes;
    test_context.Expect(size_preserved, OS_TEST_ROOTFS_CAPACITY_SHORT_WRITE);

    const os::kernel::fs::RootFileSystemStatistics statistics = root_file_system.ReadStatistics();
    const bool consistent =
        size_preserved &&
        image_information.free_data_block_count >=
            OS_TEST_ROOTFS_CAPACITY_REQUESTED_FREE_BLOCK_COUNT &&
        statistics.short_write_count == OS_TEST_ROOTFS_CAPACITY_EXPECTED_SHORT_WRITE_COUNT &&
        statistics.free_data_block_count <= OS_TEST_ROOTFS_CAPACITY_MAXIMUM_REMAINING_BLOCK_COUNT &&
        vfs.Sync() == os::kernel::fs::Status::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        device.StoredBlockCount() < OS_TEST_ROOTFS_CAPACITY_MAXIMUM_STORED_BLOCK_COUNT &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(consistent, OS_TEST_ROOTFS_CAPACITY_CONSISTENCY);
    return test_context.ExitCode();
}
