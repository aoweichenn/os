#include <os/kernel/fs/root_directory_index.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_DIRECTORY_SUITE_NAME = "kernel/root_directory_index/unit";
constexpr std::string_view OS_TEST_ROOT_DIRECTORY_FORMAT_MESSAGE =
    "variable dirent/HTree block 必须小端 CRC32C 往返并拒绝 record/checksum 损坏";
constexpr std::string_view OS_TEST_ROOT_DIRECTORY_TREE_MESSAGE =
    "512 entry 必须形成 73 node/深度 2，lookup 受树高约束且删除后收缩";
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_DIRECTORY_UUID{
    .low = 0x4449524854524545ULL,
    .high = 0x1020304050607080ULL,
};
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_ENTRY_COUNT = 512ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_REMOVE_COUNT = 448ULL;
constexpr uint8_t OS_TEST_ROOT_DIRECTORY_CORRUPTION_MASK = 0x40U;

void MakeName(const uint64_t value, uint8_t *const name, uint64_t &length) noexcept {
    constexpr uint64_t OS_TEST_ROOT_DIRECTORY_DIGIT_COUNT = 8ULL;
    name[0] = static_cast<uint8_t>('f');
    for (uint64_t digit = 0ULL; digit < OS_TEST_ROOT_DIRECTORY_DIGIT_COUNT; ++digit) {
        const uint64_t shift = (OS_TEST_ROOT_DIRECTORY_DIGIT_COUNT - digit - 1ULL) * 4ULL;
        const uint8_t nibble = static_cast<uint8_t>((value >> shift) & 0xFULL);
        name[digit + 1ULL] =
            static_cast<uint8_t>(nibble < 10U ? static_cast<uint8_t>('0') + nibble
                                              : static_cast<uint8_t>('a') + nibble - 10U);
    }
    length = OS_TEST_ROOT_DIRECTORY_DIGIT_COUNT + 1ULL;
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_DIRECTORY_SUITE_NAME};
    static os::kernel::fs::RootDirectoryBlock directory{};
    directory.directory_inode_number = 2ULL;
    directory.directory_inode_generation = 1ULL;
    directory.block_generation = 3ULL;
    directory.entry_count = 2ULL;
    directory.file_system_uuid = OS_TEST_ROOT_DIRECTORY_UUID;
    const uint8_t alpha[] = {'a', 'l', 'p', 'h', 'a'};
    const uint8_t beta[] = {'b', 'e', 't', 'a'};
    directory.entries[0] = os::kernel::fs::RootDirectoryEntryV2{
        .inode_number = 16ULL,
        .inode_generation = 2ULL,
        .name_hash = os::kernel::fs::CalculateRootDirectoryNameHash(OS_TEST_ROOT_DIRECTORY_UUID,
                                                                    alpha, sizeof(alpha)),
        .type = os::kernel::fs::RootV5NodeType::RegularFile,
        .name_length_bytes = sizeof(alpha),
        .name = {},
    };
    directory.entries[1] = os::kernel::fs::RootDirectoryEntryV2{
        .inode_number = 17ULL,
        .inode_generation = 3ULL,
        .name_hash = os::kernel::fs::CalculateRootDirectoryNameHash(OS_TEST_ROOT_DIRECTORY_UUID,
                                                                    beta, sizeof(beta)),
        .type = os::kernel::fs::RootV5NodeType::Directory,
        .name_length_bytes = sizeof(beta),
        .name = {},
    };
    for (uint64_t index = 0ULL; index < sizeof(alpha); ++index) {
        directory.entries[0].name[index] = alpha[index];
    }
    for (uint64_t index = 0ULL; index < sizeof(beta); ++index) {
        directory.entries[1].name[index] = beta[index];
    }
    uint8_t directory_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    static os::kernel::fs::RootDirectoryBlock decoded_directory{};
    os::kernel::fs::RootDirectoryIndexNode node{
        .directory_inode_number = 2ULL,
        .directory_inode_generation = 1ULL,
        .tree_generation = 3ULL,
        .depth = 1ULL,
        .entry_count = 2ULL,
        .file_system_uuid = OS_TEST_ROOT_DIRECTORY_UUID,
        .entries = {},
    };
    node.entries[0] = os::kernel::fs::RootDirectoryIndexEntry{
        .minimum_hash = 1ULL,
        .child_relative_block = 300ULL,
        .child_generation = 2ULL,
        .covered_entry_count = 8ULL,
    };
    node.entries[1] = os::kernel::fs::RootDirectoryIndexEntry{
        .minimum_hash = UINT64_MAX / 2ULL,
        .child_relative_block = 301ULL,
        .child_generation = 2ULL,
        .covered_entry_count = 8ULL,
    };
    uint8_t index_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootDirectoryIndexNode decoded_node{};
    const bool format_valid =
        os::kernel::fs::EncodeRootDirectoryBlock(directory, directory_bytes,
                                                 sizeof(directory_bytes)) ==
            os::kernel::fs::RootDirectoryStatus::Succeeded &&
        os::kernel::fs::DecodeRootDirectoryBlock(directory_bytes, sizeof(directory_bytes),
                                                 decoded_directory) ==
            os::kernel::fs::RootDirectoryStatus::Succeeded &&
        decoded_directory.entries[1].inode_number == 17ULL &&
        os::kernel::fs::EncodeRootDirectoryIndexNode(node, index_bytes, sizeof(index_bytes)) ==
            os::kernel::fs::RootDirectoryStatus::Succeeded &&
        os::kernel::fs::DecodeRootDirectoryIndexNode(index_bytes, sizeof(index_bytes),
                                                     decoded_node) ==
            os::kernel::fs::RootDirectoryStatus::Succeeded &&
        decoded_node.entries[1].child_relative_block == 301ULL;
    directory_bytes[64] ^= OS_TEST_ROOT_DIRECTORY_CORRUPTION_MASK;
    index_bytes[64] ^= OS_TEST_ROOT_DIRECTORY_CORRUPTION_MASK;
    context.Expect(format_valid &&
                       os::kernel::fs::DecodeRootDirectoryBlock(
                           directory_bytes, sizeof(directory_bytes), decoded_directory) ==
                           os::kernel::fs::RootDirectoryStatus::InvalidChecksum &&
                       os::kernel::fs::DecodeRootDirectoryIndexNode(
                           index_bytes, sizeof(index_bytes), decoded_node) ==
                           os::kernel::fs::RootDirectoryStatus::InvalidChecksum,
                   OS_TEST_ROOT_DIRECTORY_FORMAT_MESSAGE);

    static os::kernel::fs::RootDirectoryIndex index{};
    bool tree_valid = index.Initialize(2ULL, 1ULL, OS_TEST_ROOT_DIRECTORY_UUID) ==
                      os::kernel::fs::RootDirectoryStatus::Succeeded;
    uint8_t name[os::kernel::fs::OS_KERNEL_ROOTFS_V5_DIRECTORY_NAME_STORAGE_SIZE_BYTES]{};
    uint64_t name_length = 0ULL;
    for (uint64_t entry_index = 0ULL;
         tree_valid && entry_index < OS_TEST_ROOT_DIRECTORY_ENTRY_COUNT; ++entry_index) {
        MakeName(entry_index, name, name_length);
        tree_valid = index.Insert(name, name_length, 16ULL + entry_index, entry_index + 1ULL,
                                  os::kernel::fs::RootV5NodeType::RegularFile) ==
                     os::kernel::fs::RootDirectoryStatus::Succeeded;
    }
    MakeName(400ULL, name, name_length);
    os::kernel::fs::RootDirectoryEntryV2 found{};
    tree_valid =
        tree_valid &&
        index.Lookup(name, name_length, found) == os::kernel::fs::RootDirectoryStatus::Succeeded &&
        found.inode_number == 416ULL &&
        index.Statistics().current_node_count ==
            os::kernel::fs::OS_KERNEL_ROOTFS_V5_DIRECTORY_INDEX_MAXIMUM_NODE_COUNT &&
        index.Statistics().current_depth == 2ULL &&
        index.Statistics().last_lookup_node_count <= 3ULL;
    for (uint64_t entry_index = 0ULL;
         tree_valid && entry_index < OS_TEST_ROOT_DIRECTORY_REMOVE_COUNT; ++entry_index) {
        MakeName(entry_index, name, name_length);
        tree_valid =
            index.Remove(name, name_length) == os::kernel::fs::RootDirectoryStatus::Succeeded;
    }
    tree_valid = tree_valid && index.EntryCount() == 64ULL &&
                 index.Statistics().current_depth == 1ULL &&
                 index.Statistics().depth_shrink_count != 0ULL &&
                 index.Validate() == os::kernel::fs::RootDirectoryStatus::Succeeded;
    context.Expect(tree_valid, OS_TEST_ROOT_DIRECTORY_TREE_MESSAGE);
    return context.ExitCode();
}
