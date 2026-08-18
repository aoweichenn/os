#include "os/kernel/fs/root_file_system.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "sparse_memory_block_device.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOTFS_HIGH_LBA_SUITE_NAME =
    "kernel/root_file_system/high_lba/integration";
constexpr std::string_view OS_TEST_ROOTFS_HIGH_LBA_MOUNT =
    "rootfs v4 必须挂载覆盖完整 LBA28 几何的稀疏镜像";
constexpr std::string_view OS_TEST_ROOTFS_HIGH_LBA_READ =
    "rootfs v4 必须经 VFS 从 LBA 0x0FFFFFFF 读取文件数据";
constexpr uint64_t OS_TEST_ROOTFS_HIGH_LBA_SUPERBLOCK_IDENTIFIER = 61ULL;
constexpr uint64_t OS_TEST_ROOTFS_HIGH_LBA_MOUNT_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_HIGH_LBA_FILE_INODE_NUMBER = 2ULL;
constexpr uint64_t OS_TEST_ROOTFS_HIGH_LBA_NEXT_INODE_GENERATION = 3ULL;
constexpr uint64_t OS_TEST_ROOTFS_HIGH_LBA_TIMESTAMP_NANOSECONDS = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_ROOTFS_HIGH_LBA_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_ROOTFS_HIGH_LBA_BITS_PER_BYTE = 8ULL;
constexpr uint8_t OS_TEST_ROOTFS_HIGH_LBA_INODE_BITMAP_MASK = 0x03U;
constexpr uint8_t OS_TEST_ROOTFS_HIGH_LBA_DATA_BITMAP_MASK = 0x01U;
constexpr uint8_t OS_TEST_ROOTFS_HIGH_LBA_NAME[] = {
    'h', 'i', 'g', 'h', '-', 'l', 'b', 'a',
};
constexpr uint8_t OS_TEST_ROOTFS_HIGH_LBA_PATH[] = {
    '/', 'h', 'i', 'g', 'h', '-', 'l', 'b', 'a',
};
constexpr uint8_t OS_TEST_ROOTFS_HIGH_LBA_PAYLOAD[] = {
    'r', 'o', 'o', 't', 'f', 's', '-', 'v', '4', '-', 'h', 'i', 'g', 'h', '-', 'l', 'b', 'a',
};

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left, const uint8_t *const right,
                                 const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool WriteRelativeBlock(os::test::SparseMemoryBlockDevice &device,
                                      const uint64_t relative_block,
                                      const uint8_t *const block) noexcept {
    return device.WriteBlock(os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA + relative_block, block,
                             os::kernel::fs::OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES) ==
           os::kernel::FileSystemBlockDeviceStatus::Succeeded;
}

[[nodiscard]] os::kernel::fs::RootInode MakeInode(const os::kernel::fs::RootNodeType type,
                                                  const uint64_t size_bytes,
                                                  const uint64_t generation,
                                                  const uint64_t direct_block) noexcept {
    os::kernel::fs::RootInode inode{
        .type = type,
        .flags = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE,
        .size_bytes = size_bytes,
        .generation = generation,
        .link_count = OS_TEST_ROOTFS_HIGH_LBA_COUNTER_INCREMENT,
        .allocated_data_block_count = OS_TEST_ROOTFS_HIGH_LBA_COUNTER_INCREMENT,
        .allocated_metadata_block_count = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE,
        .parent_inode_number = os::kernel::fs::OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER,
        .direct_blocks = {},
        .single_indirect_block = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE,
        .double_indirect_block = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE,
        .triple_indirect_block = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE,
        .quadruple_indirect_block = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE,
        .quintuple_indirect_block = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE,
        .access_time_nanoseconds = OS_TEST_ROOTFS_HIGH_LBA_TIMESTAMP_NANOSECONDS,
        .modification_time_nanoseconds = OS_TEST_ROOTFS_HIGH_LBA_TIMESTAMP_NANOSECONDS,
        .change_time_nanoseconds = OS_TEST_ROOTFS_HIGH_LBA_TIMESTAMP_NANOSECONDS,
        .birth_time_nanoseconds = OS_TEST_ROOTFS_HIGH_LBA_TIMESTAMP_NANOSECONDS,
    };
    inode.direct_blocks[OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE] = direct_block;
    return inode;
}

[[nodiscard]] bool FormatHighLbaImage(os::test::SparseMemoryBlockDevice &device) noexcept {
    using namespace os::kernel::fs;
    const uint64_t last_relative_block =
        OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT - OS_TEST_ROOTFS_HIGH_LBA_COUNTER_INCREMENT;
    RootDirectoryEntry entry{
        .inode_number = OS_TEST_ROOTFS_HIGH_LBA_FILE_INODE_NUMBER,
        .inode_generation = OS_TEST_ROOTFS_HIGH_LBA_FILE_INODE_NUMBER,
        .type = RootNodeType::RegularFile,
        .name_length_bytes = sizeof(OS_TEST_ROOTFS_HIGH_LBA_NAME),
        .name = {},
    };
    CopyBytes(entry.name, OS_TEST_ROOTFS_HIGH_LBA_NAME, sizeof(OS_TEST_ROOTFS_HIGH_LBA_NAME));
    uint8_t encoded_entry[OS_KERNEL_ROOTFS_DIRECTORY_ENTRY_SIZE_BYTES]{};
    if (EncodeRootDirectoryEntry(entry, encoded_entry, sizeof(encoded_entry)) !=
        RootFormatStatus::Succeeded) {
        return false;
    }
    uint8_t directory_block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    CopyBytes(directory_block, encoded_entry, sizeof(encoded_entry));

    const RootInode root_inode =
        MakeInode(RootNodeType::Directory, sizeof(encoded_entry),
                  OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER, OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK);
    const RootInode file_inode =
        MakeInode(RootNodeType::RegularFile, sizeof(OS_TEST_ROOTFS_HIGH_LBA_PAYLOAD),
                  OS_TEST_ROOTFS_HIGH_LBA_FILE_INODE_NUMBER, last_relative_block);
    uint8_t inode_block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    if (EncodeRootInode(root_inode, inode_block, OS_KERNEL_ROOTFS_INODE_SIZE_BYTES) !=
            RootFormatStatus::Succeeded ||
        EncodeRootInode(file_inode, inode_block + OS_KERNEL_ROOTFS_INODE_SIZE_BYTES,
                        OS_KERNEL_ROOTFS_INODE_SIZE_BYTES) != RootFormatStatus::Succeeded) {
        return false;
    }

    uint8_t inode_bitmap[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    inode_bitmap[OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE] = OS_TEST_ROOTFS_HIGH_LBA_INODE_BITMAP_MASK;
    uint8_t first_data_bitmap[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    first_data_bitmap[OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE] =
        OS_TEST_ROOTFS_HIGH_LBA_DATA_BITMAP_MASK;
    const uint64_t last_data_bit =
        OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT - OS_TEST_ROOTFS_HIGH_LBA_COUNTER_INCREMENT;
    const uint64_t bits_per_bitmap_block =
        OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES * OS_TEST_ROOTFS_HIGH_LBA_BITS_PER_BYTE;
    const uint64_t last_bitmap_block_index = last_data_bit / bits_per_bitmap_block;
    const uint64_t last_bitmap_block_bit = last_data_bit % bits_per_bitmap_block;
    uint8_t last_data_bitmap[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    last_data_bitmap[last_bitmap_block_bit / OS_TEST_ROOTFS_HIGH_LBA_BITS_PER_BYTE] =
        static_cast<uint8_t>(OS_TEST_ROOTFS_HIGH_LBA_COUNTER_INCREMENT
                             << (last_bitmap_block_bit % OS_TEST_ROOTFS_HIGH_LBA_BITS_PER_BYTE));

    uint8_t payload_block[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    CopyBytes(payload_block, OS_TEST_ROOTFS_HIGH_LBA_PAYLOAD,
              sizeof(OS_TEST_ROOTFS_HIGH_LBA_PAYLOAD));
    const RootSuperblock superblock{
        .version = OS_KERNEL_ROOTFS_FORMAT_VERSION,
        .block_size_bytes = OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES,
        .total_block_count = OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT,
        .journal_start_relative_block = OS_KERNEL_ROOTFS_JOURNAL_START_RELATIVE_BLOCK,
        .journal_block_count = OS_KERNEL_ROOTFS_JOURNAL_BLOCK_COUNT,
        .inode_bitmap_start_relative_block = OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK,
        .inode_bitmap_block_count = OS_KERNEL_ROOTFS_INODE_BITMAP_BLOCK_COUNT,
        .inode_table_start_relative_block = OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK,
        .inode_table_block_count = OS_KERNEL_ROOTFS_INODE_TABLE_BLOCK_COUNT,
        .data_bitmap_start_relative_block = OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK,
        .data_bitmap_block_count = OS_KERNEL_ROOTFS_DATA_BITMAP_BLOCK_COUNT,
        .data_start_relative_block = OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK,
        .data_block_count = OS_KERNEL_ROOTFS_DATA_BLOCK_COUNT,
        .inode_count = OS_KERNEL_ROOTFS_INODE_COUNT,
        .root_inode_number = OS_KERNEL_ROOTFS_ROOT_INODE_NUMBER,
        .maximum_file_size_bytes = OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES,
        .transaction_state = RootTransactionState::Clean,
        .transaction_generation = OS_TEST_ROOTFS_HIGH_LBA_TIMESTAMP_NANOSECONDS,
        .next_inode_generation = OS_TEST_ROOTFS_HIGH_LBA_NEXT_INODE_GENERATION,
        .feature_flags = OS_KERNEL_ROOTFS_REQUIRED_FEATURES,
        .allocated_inode_count = 2ULL,
        .allocated_data_block_count = 2ULL,
        .allocated_metadata_block_count = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE,
    };
    uint8_t superblock_bytes[OS_KERNEL_ROOTFS_BLOCK_SIZE_BYTES]{};
    return EncodeRootSuperblock(superblock, superblock_bytes, sizeof(superblock_bytes)) ==
               RootFormatStatus::Succeeded &&
           WriteRelativeBlock(device, OS_KERNEL_ROOTFS_SUPERBLOCK_RELATIVE_BLOCK,
                              superblock_bytes) &&
           WriteRelativeBlock(device, OS_KERNEL_ROOTFS_INODE_BITMAP_START_RELATIVE_BLOCK,
                              inode_bitmap) &&
           WriteRelativeBlock(device, OS_KERNEL_ROOTFS_INODE_TABLE_START_RELATIVE_BLOCK,
                              inode_block) &&
           WriteRelativeBlock(device, OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK,
                              first_data_bitmap) &&
           WriteRelativeBlock(
               device, OS_KERNEL_ROOTFS_DATA_BITMAP_START_RELATIVE_BLOCK + last_bitmap_block_index,
               last_data_bitmap) &&
           WriteRelativeBlock(device, OS_KERNEL_ROOTFS_DATA_START_RELATIVE_BLOCK,
                              directory_block) &&
           WriteRelativeBlock(device, last_relative_block, payload_block) &&
           device.Flush() == os::kernel::FileSystemBlockDeviceStatus::Succeeded;
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOTFS_HIGH_LBA_SUITE_NAME};
    static os::test::SparseMemoryBlockDevice device{};
    static os::kernel::fs::RootFileSystem file_system{};
    os::kernel::fs::Mount mounts[OS_TEST_ROOTFS_HIGH_LBA_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext fs_context{};
    const bool mounted =
        FormatHighLbaImage(device) &&
        file_system.Initialize(device, OS_TEST_ROOTFS_HIGH_LBA_SUPERBLOCK_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_ROOTFS_HIGH_LBA_MOUNT_CAPACITY,
                       file_system.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(fs_context) == os::kernel::fs::Status::Succeeded;
    context.Expect(mounted, OS_TEST_ROOTFS_HIGH_LBA_MOUNT);
    if (!mounted) {
        return context.ExitCode();
    }

    const os::kernel::fs::OpenOptions options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile file{};
    uint8_t observed[sizeof(OS_TEST_ROOTFS_HIGH_LBA_PAYLOAD)]{};
    uint64_t read_bytes = OS_TEST_ROOTFS_HIGH_LBA_EMPTY_VALUE;
    const bool read =
        vfs.Open(fs_context, OS_TEST_ROOTFS_HIGH_LBA_PATH, sizeof(OS_TEST_ROOTFS_HIGH_LBA_PATH),
                 options, file) == os::kernel::fs::Status::Succeeded &&
        vfs.Read(file, observed, sizeof(observed), read_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        read_bytes == sizeof(observed) &&
        BytesAreEqual(observed, OS_TEST_ROOTFS_HIGH_LBA_PAYLOAD, sizeof(observed)) &&
        vfs.Close(file) == os::kernel::fs::Status::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(fs_context) == os::kernel::fs::Status::Succeeded;
    context.Expect(read, OS_TEST_ROOTFS_HIGH_LBA_READ);
    return context.ExitCode();
}
