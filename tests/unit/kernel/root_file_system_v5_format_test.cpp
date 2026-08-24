#include <os/kernel/fs/root_file_system_v5_format.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOTFS_V5_FORMAT_SUITE_NAME =
    "kernel/root_file_system_v5_format/unit";
constexpr std::string_view OS_TEST_ROOTFS_V5_FORMAT_CRC32C_MESSAGE =
    "rootfs v5 CRC32C 必须匹配 Castagnoli 标准向量";
constexpr std::string_view OS_TEST_ROOTFS_V5_FORMAT_PRODUCTION_MESSAGE =
    "128 GiB profile 必须形成 4 KiB、1024 group 与约两百万 inode 的稳定几何";
constexpr std::string_view OS_TEST_ROOTFS_V5_FORMAT_GROUP_MESSAGE =
    "primary、sparse backup、普通与尾 group 必须拥有互不重叠的位图、inode table 和数据区";
constexpr std::string_view OS_TEST_ROOTFS_V5_FORMAT_ROUND_TRIP_MESSAGE =
    "superblock、group descriptor 与 inode 必须按固定小端布局和 CRC32C 无损往返";
constexpr std::string_view OS_TEST_ROOTFS_V5_FORMAT_FEATURE_MESSAGE =
    "未知 compat 可忽略，未知 ro-compat/incompat 与缺少 required feature 必须 fail closed";
constexpr std::string_view OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_MESSAGE =
    "superblock、descriptor 与 inode 的 checksum 损坏必须逐类拒绝";
constexpr uint64_t OS_TEST_ROOTFS_V5_FORMAT_CREATION_TIME_NANOSECONDS = 123456789ULL;
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOTFS_V5_FORMAT_UUID{
    .low = 0x0123456789ABCDEFULL,
    .high = 0xFEDCBA9876543210ULL,
};
constexpr uint32_t OS_TEST_ROOTFS_V5_FORMAT_BLOCK_BITMAP_CHECKSUM = 0x10203040U;
constexpr uint32_t OS_TEST_ROOTFS_V5_FORMAT_INODE_BITMAP_CHECKSUM = 0x50607080U;
constexpr uint64_t OS_TEST_ROOTFS_V5_FORMAT_LAST_GROUP_INDEX =
    os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_COUNT - 1ULL;
constexpr uint8_t OS_TEST_ROOTFS_V5_FORMAT_CRC_VECTOR[] = {
    static_cast<uint8_t>('1'), static_cast<uint8_t>('2'), static_cast<uint8_t>('3'),
    static_cast<uint8_t>('4'), static_cast<uint8_t>('5'), static_cast<uint8_t>('6'),
    static_cast<uint8_t>('7'), static_cast<uint8_t>('8'), static_cast<uint8_t>('9'),
};
constexpr uint32_t OS_TEST_ROOTFS_V5_FORMAT_EXPECTED_CRC32C = 0xE3069283U;
constexpr uint32_t OS_TEST_ROOTFS_V5_FORMAT_EXPECTED_SUPERBLOCK_CHECKSUM = 0x9B5E7B2EU;
constexpr uint32_t OS_TEST_ROOTFS_V5_FORMAT_EXPECTED_DESCRIPTOR_CHECKSUM = 0x5FD1FE47U;
constexpr uint32_t OS_TEST_ROOTFS_V5_FORMAT_EXPECTED_INODE_CHECKSUM = 0x339E28BAU;
constexpr uint64_t OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_OFFSET_BYTES = 64ULL;
constexpr uint8_t OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_MASK = 0x40U;

[[nodiscard]] uint32_t ReadChecksum(const uint8_t *const bytes,
                                    const uint64_t offset_bytes) noexcept {
    return static_cast<uint32_t>(bytes[offset_bytes]) |
           static_cast<uint32_t>(bytes[offset_bytes + 1ULL]) << 8U |
           static_cast<uint32_t>(bytes[offset_bytes + 2ULL]) << 16U |
           static_cast<uint32_t>(bytes[offset_bytes + 3ULL]) << 24U;
}

[[nodiscard]] os::kernel::fs::RootV5Inode
MakeRootInode(const os::kernel::fs::RootV5Superblock &superblock) noexcept {
    return os::kernel::fs::RootV5Inode{
        .inode_number = superblock.root_inode_number,
        .generation = 1ULL,
        .type = os::kernel::fs::RootV5NodeType::Directory,
        .flags = 0ULL,
        .size_bytes = 0ULL,
        .allocated_block_count = 0ULL,
        .link_count = 1ULL,
        .parent_inode_number = superblock.root_inode_number,
        .access_time_nanoseconds = OS_TEST_ROOTFS_V5_FORMAT_CREATION_TIME_NANOSECONDS,
        .modification_time_nanoseconds = OS_TEST_ROOTFS_V5_FORMAT_CREATION_TIME_NANOSECONDS,
        .change_time_nanoseconds = OS_TEST_ROOTFS_V5_FORMAT_CREATION_TIME_NANOSECONDS,
        .birth_time_nanoseconds = OS_TEST_ROOTFS_V5_FORMAT_CREATION_TIME_NANOSECONDS,
        .owner_user_identifier = os::abi::OS_ABI_ROOT_USER_IDENTIFIER,
        .owner_group_identifier = os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER,
        .mode = os::abi::OS_ABI_FILE_MODE_DIRECTORY | 0000755U,
        .project_identifier = 0U,
        .mapping_root = {},
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_ROOTFS_V5_FORMAT_SUITE_NAME};
    test_context.Expect(
        os::kernel::fs::CalculateRootV5Crc32c(OS_TEST_ROOTFS_V5_FORMAT_CRC_VECTOR,
                                              sizeof(OS_TEST_ROOTFS_V5_FORMAT_CRC_VECTOR)) ==
            OS_TEST_ROOTFS_V5_FORMAT_EXPECTED_CRC32C,
        OS_TEST_ROOTFS_V5_FORMAT_CRC32C_MESSAGE);

    const os::kernel::fs::RootV5FormatProfile profile =
        os::kernel::fs::MakeProductionRootV5FormatProfile(
            OS_TEST_ROOTFS_V5_FORMAT_CREATION_TIME_NANOSECONDS, OS_TEST_ROOTFS_V5_FORMAT_UUID);
    os::kernel::fs::RootV5Superblock superblock{};
    const bool production_valid =
        os::kernel::fs::PlanRootV5Superblock(profile, superblock) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        superblock.total_block_count == os::kernel::fs::OS_KERNEL_ROOTFS_V5_TOTAL_BLOCK_COUNT &&
        superblock.group_count == os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_COUNT &&
        superblock.group_descriptor_table_block_count ==
            os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_TABLE_BLOCK_COUNT &&
        superblock.inode_count == os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_COUNT &&
        superblock.free_block_count == 33416241ULL && superblock.free_inode_count == 2097137ULL &&
        os::kernel::fs::ValidateRootV5Superblock(superblock) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded;
    test_context.Expect(production_valid, OS_TEST_ROOTFS_V5_FORMAT_PRODUCTION_MESSAGE);

    os::kernel::fs::RootV5GroupDescriptor primary_group{};
    os::kernel::fs::RootV5GroupDescriptor backup_group{};
    os::kernel::fs::RootV5GroupDescriptor ordinary_group{};
    os::kernel::fs::RootV5GroupDescriptor final_group{};
    const bool groups_valid =
        os::kernel::fs::BuildInitialRootV5GroupDescriptor(superblock, 0ULL, primary_group) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        os::kernel::fs::BuildInitialRootV5GroupDescriptor(superblock, 1ULL, backup_group) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        os::kernel::fs::BuildInitialRootV5GroupDescriptor(superblock, 2ULL, ordinary_group) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        os::kernel::fs::BuildInitialRootV5GroupDescriptor(
            superblock, OS_TEST_ROOTFS_V5_FORMAT_LAST_GROUP_INDEX, final_group) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        primary_group.superblock_copy_block == 0ULL &&
        primary_group.group_descriptor_copy_start_block == 1ULL &&
        primary_group.block_bitmap_block == 65ULL && primary_group.data_start_block == 195ULL &&
        primary_group.data_block_count == 32573ULL &&
        primary_group.free_inode_count ==
            os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODES_PER_GROUP -
                os::kernel::fs::OS_KERNEL_ROOTFS_V5_RESERVED_INODE_COUNT &&
        backup_group.superblock_copy_block == backup_group.first_block &&
        ordinary_group.superblock_copy_block == os::kernel::fs::OS_KERNEL_ROOTFS_V5_NO_BLOCK &&
        ordinary_group.block_bitmap_block == ordinary_group.first_block &&
        ordinary_group.data_start_block == ordinary_group.first_block + 130ULL &&
        ordinary_group.data_block_count == 32638ULL && final_group.block_count == 28672ULL &&
        final_group.data_block_count == 28542ULL &&
        os::kernel::fs::RootV5GroupHasSuperblockCopy(729ULL) &&
        !os::kernel::fs::RootV5GroupHasSuperblockCopy(1023ULL);
    test_context.Expect(groups_valid, OS_TEST_ROOTFS_V5_FORMAT_GROUP_MESSAGE);

    uint8_t superblock_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_BLOCK_SIZE_BYTES]{};
    os::kernel::fs::RootV5Superblock decoded_superblock{};
    primary_group.block_bitmap_checksum = OS_TEST_ROOTFS_V5_FORMAT_BLOCK_BITMAP_CHECKSUM;
    primary_group.inode_bitmap_checksum = OS_TEST_ROOTFS_V5_FORMAT_INODE_BITMAP_CHECKSUM;
    uint8_t descriptor_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_SIZE_BYTES]{};
    os::kernel::fs::RootV5GroupDescriptor decoded_descriptor{};
    const os::kernel::fs::RootV5Inode root_inode = MakeRootInode(superblock);
    uint8_t inode_bytes[os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_SIZE_BYTES]{};
    os::kernel::fs::RootV5Inode decoded_inode{};
    const bool round_trip_valid =
        os::kernel::fs::EncodeRootV5Superblock(superblock, superblock_bytes,
                                               sizeof(superblock_bytes)) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootV5Superblock(superblock_bytes, sizeof(superblock_bytes),
                                               decoded_superblock) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        decoded_superblock.uuid.high == OS_TEST_ROOTFS_V5_FORMAT_UUID.high &&
        os::kernel::fs::EncodeRootV5GroupDescriptor(superblock, primary_group, descriptor_bytes,
                                                    sizeof(descriptor_bytes)) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootV5GroupDescriptor(superblock, descriptor_bytes,
                                                    sizeof(descriptor_bytes), decoded_descriptor) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        decoded_descriptor.block_bitmap_checksum ==
            OS_TEST_ROOTFS_V5_FORMAT_BLOCK_BITMAP_CHECKSUM &&
        os::kernel::fs::EncodeRootV5Inode(superblock, root_inode, inode_bytes,
                                          sizeof(inode_bytes)) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        os::kernel::fs::DecodeRootV5Inode(superblock, inode_bytes, sizeof(inode_bytes),
                                          decoded_inode) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        decoded_inode.type == os::kernel::fs::RootV5NodeType::Directory &&
        decoded_inode.parent_inode_number == superblock.root_inode_number &&
        decoded_inode.mode == root_inode.mode &&
        ReadChecksum(superblock_bytes,
                     os::kernel::fs::OS_KERNEL_ROOTFS_V5_SUPERBLOCK_CHECKSUM_OFFSET_BYTES) ==
            OS_TEST_ROOTFS_V5_FORMAT_EXPECTED_SUPERBLOCK_CHECKSUM &&
        ReadChecksum(descriptor_bytes,
                     os::kernel::fs::OS_KERNEL_ROOTFS_V5_GROUP_DESCRIPTOR_CHECKSUM_OFFSET_BYTES) ==
            OS_TEST_ROOTFS_V5_FORMAT_EXPECTED_DESCRIPTOR_CHECKSUM &&
        ReadChecksum(inode_bytes,
                     os::kernel::fs::OS_KERNEL_ROOTFS_V5_INODE_CHECKSUM_OFFSET_BYTES) ==
            OS_TEST_ROOTFS_V5_FORMAT_EXPECTED_INODE_CHECKSUM;
    test_context.Expect(round_trip_valid, OS_TEST_ROOTFS_V5_FORMAT_ROUND_TRIP_MESSAGE);

    os::kernel::fs::RootV5Superblock unknown_compat = superblock;
    unknown_compat.compatible_features |= 1ULL << 63ULL;
    os::kernel::fs::RootV5Superblock unknown_read_only = superblock;
    unknown_read_only.read_only_compatible_features |= 1ULL << 63ULL;
    os::kernel::fs::RootV5Superblock unknown_required = superblock;
    unknown_required.incompatible_features |= 1ULL << 63ULL;
    os::kernel::fs::RootV5Superblock missing_required = superblock;
    missing_required.incompatible_features &=
        ~os::kernel::fs::OS_KERNEL_ROOTFS_V5_INCOMPAT_BLOCK_GROUPS;
    const bool features_valid =
        os::kernel::fs::ValidateRootV5Superblock(unknown_compat) ==
            os::kernel::fs::RootV5FormatStatus::Succeeded &&
        os::kernel::fs::ValidateRootV5Superblock(unknown_read_only) ==
            os::kernel::fs::RootV5FormatStatus::UnsupportedReadOnlyFeature &&
        os::kernel::fs::ValidateRootV5Superblock(unknown_required) ==
            os::kernel::fs::RootV5FormatStatus::UnsupportedRequiredFeature &&
        os::kernel::fs::ValidateRootV5Superblock(missing_required) ==
            os::kernel::fs::RootV5FormatStatus::InvalidFeatures;
    test_context.Expect(features_valid, OS_TEST_ROOTFS_V5_FORMAT_FEATURE_MESSAGE);

    superblock_bytes[OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_MASK;
    descriptor_bytes[OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_MASK;
    inode_bytes[OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_OFFSET_BYTES] ^=
        OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_MASK;
    const bool corruption_rejected =
        os::kernel::fs::DecodeRootV5Superblock(superblock_bytes, sizeof(superblock_bytes),
                                               decoded_superblock) ==
            os::kernel::fs::RootV5FormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootV5GroupDescriptor(superblock, descriptor_bytes,
                                                    sizeof(descriptor_bytes), decoded_descriptor) ==
            os::kernel::fs::RootV5FormatStatus::InvalidChecksum &&
        os::kernel::fs::DecodeRootV5Inode(superblock, inode_bytes, sizeof(inode_bytes),
                                          decoded_inode) ==
            os::kernel::fs::RootV5FormatStatus::InvalidChecksum;
    test_context.Expect(corruption_rejected, OS_TEST_ROOTFS_V5_FORMAT_CORRUPTION_MESSAGE);
    return test_context.ExitCode();
}
