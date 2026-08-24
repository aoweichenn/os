#pragma once

#include <os/abi/security.hpp>
#include <os/kernel/fs/root_file_system_v5_format.hpp>

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_ENTRY_COUNT = 16ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_NAME_LENGTH_BYTES = 63ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_NAME_STORAGE_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_VALUE_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_BLOCK_HEADER_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_XATTR_CHECKSUM_OFFSET_BYTES = 4092ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_ACL_MAXIMUM_ENTRY_COUNT = 16ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_MAXIMUM_RECORD_COUNT = 48ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_BLOCK_HEADER_SIZE_BYTES = 128ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_RECORD_SIZE_BYTES = 72ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_QUOTA_CHECKSUM_OFFSET_BYTES = 4092ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_DIRECTORY_INDEX = 1ULL << 1ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_XATTR = 1ULL << 2ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_ACL = 1ULL << 3ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_QUOTA = 1ULL << 4ULL;
inline constexpr uint64_t OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_SUPPORTED_FLAGS =
    OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_EXTENTS |
    OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_DIRECTORY_INDEX |
    OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_XATTR | OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_ACL |
    OS_KERNEL_ROOTFS_V5_INODE_EXTENSION_FLAG_QUOTA;

enum class RootInodeMetadataStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidMagic,
    InvalidVersion,
    InvalidChecksum,
    InvalidArgument,
    InvalidEntry,
    DuplicateEntry,
    NotFound,
    CapacityExhausted,
    PermissionDenied,
    QuotaExceeded,
    NonZeroReservedBytes,
};

enum class RootXattrNamespace : uint64_t {
    User = 1ULL,
    System = 2ULL,
    Security = 3ULL,
    Trusted = 4ULL,
};

enum class RootAclEntryType : uint64_t {
    UserObject = 1ULL,
    NamedUser = 2ULL,
    GroupObject = 3ULL,
    NamedGroup = 4ULL,
    Mask = 5ULL,
    Other = 6ULL,
};

enum class RootQuotaType : uint64_t {
    User = 1ULL,
    Group = 2ULL,
};

struct RootInodeExtension final {
    uint64_t flags;
    uint64_t extent_root_relative_block;
    uint64_t xattr_relative_block;
    uint64_t directory_index_root_relative_block;
    uint64_t project_identifier;
    uint64_t acl_generation;
    uint64_t quota_generation;
};

struct RootXattrEntry final {
    RootXattrNamespace name_space;
    uint64_t name_length_bytes;
    uint64_t value_size_bytes;
    uint8_t name[OS_KERNEL_ROOTFS_V5_XATTR_NAME_STORAGE_SIZE_BYTES];
    uint8_t value[OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_VALUE_SIZE_BYTES];
};

struct RootXattrBlock final {
    uint64_t inode_number;
    uint64_t inode_generation;
    uint64_t block_generation;
    uint64_t entry_count;
    RootV5Uuid file_system_uuid;
    RootXattrEntry entries[OS_KERNEL_ROOTFS_V5_XATTR_MAXIMUM_ENTRY_COUNT];
};

struct RootAclEntry final {
    RootAclEntryType type;
    uint64_t identifier;
    os::abi::FileMode permissions;
};

struct RootAcl final {
    RootAclEntry entries[OS_KERNEL_ROOTFS_V5_ACL_MAXIMUM_ENTRY_COUNT];
    uint64_t entry_count;
    uint64_t generation;
};

struct RootQuotaRecord final {
    RootQuotaType type;
    uint64_t identifier;
    uint64_t used_block_count;
    uint64_t used_inode_count;
    uint64_t soft_block_limit;
    uint64_t hard_block_limit;
    uint64_t soft_inode_limit;
    uint64_t hard_inode_limit;
    uint64_t grace_deadline_nanoseconds;
};

struct RootQuotaBlock final {
    uint64_t generation;
    uint64_t record_count;
    RootV5Uuid file_system_uuid;
    RootQuotaRecord records[OS_KERNEL_ROOTFS_V5_QUOTA_MAXIMUM_RECORD_COUNT];
};

class RootXattrSet final {
  public:
    [[nodiscard]] RootInodeMetadataStatus Initialize(uint64_t inode_number,
                                                     uint64_t inode_generation,
                                                     RootV5Uuid file_system_uuid) noexcept;
    [[nodiscard]] RootInodeMetadataStatus Set(RootXattrNamespace name_space, const uint8_t *name,
                                              uint64_t name_length_bytes, const uint8_t *value,
                                              uint64_t value_size_bytes) noexcept;
    [[nodiscard]] RootInodeMetadataStatus Get(RootXattrNamespace name_space, const uint8_t *name,
                                              uint64_t name_length_bytes,
                                              RootXattrEntry &entry) const noexcept;
    [[nodiscard]] RootInodeMetadataStatus Remove(RootXattrNamespace name_space, const uint8_t *name,
                                                 uint64_t name_length_bytes) noexcept;
    [[nodiscard]] RootInodeMetadataStatus Export(RootXattrBlock &block) const noexcept;
    [[nodiscard]] RootInodeMetadataStatus Validate() const noexcept;

  private:
    RootXattrBlock block_{};
    bool initialized_{};
};

class RootQuotaManager final {
  public:
    [[nodiscard]] RootInodeMetadataStatus Initialize(RootV5Uuid file_system_uuid) noexcept;
    [[nodiscard]] RootInodeMetadataStatus
    SetLimits(RootQuotaType type, uint64_t identifier, uint64_t soft_block_limit,
              uint64_t hard_block_limit, uint64_t soft_inode_limit, uint64_t hard_inode_limit,
              uint64_t grace_deadline_nanoseconds) noexcept;
    [[nodiscard]] RootInodeMetadataStatus Charge(RootQuotaType type, uint64_t identifier,
                                                 uint64_t block_count, uint64_t inode_count,
                                                 uint64_t now_nanoseconds) noexcept;
    [[nodiscard]] RootInodeMetadataStatus Release(RootQuotaType type, uint64_t identifier,
                                                  uint64_t block_count,
                                                  uint64_t inode_count) noexcept;
    [[nodiscard]] RootInodeMetadataStatus Export(RootQuotaBlock &block) const noexcept;
    [[nodiscard]] RootInodeMetadataStatus Validate() const noexcept;

  private:
    RootQuotaBlock block_{};
    bool initialized_{};
};

[[nodiscard]] RootInodeMetadataStatus EncodeRootInodeExtension(const RootInodeExtension &extension,
                                                               uint8_t *bytes,
                                                               uint64_t byte_count) noexcept;
[[nodiscard]] RootInodeMetadataStatus
DecodeRootInodeExtension(const uint8_t *bytes, uint64_t byte_count,
                         RootInodeExtension &extension) noexcept;
[[nodiscard]] RootInodeMetadataStatus EncodeRootXattrBlock(const RootXattrBlock &xattr_block,
                                                           uint8_t *block,
                                                           uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootInodeMetadataStatus DecodeRootXattrBlock(const uint8_t *block,
                                                           uint64_t block_size_bytes,
                                                           RootXattrBlock &xattr_block) noexcept;
[[nodiscard]] RootInodeMetadataStatus ValidateRootAcl(const RootAcl &acl) noexcept;
[[nodiscard]] RootInodeMetadataStatus
EvaluateRootAcl(const RootAcl &acl, os::abi::UserIdentifier owner_user_identifier,
                os::abi::GroupIdentifier owner_group_identifier,
                os::abi::UserIdentifier user_identifier,
                const os::abi::GroupIdentifier *group_identifiers, uint64_t group_identifier_count,
                os::abi::FileMode requested_permissions) noexcept;
[[nodiscard]] RootInodeMetadataStatus EncodeRootQuotaBlock(const RootQuotaBlock &quota_block,
                                                           uint8_t *block,
                                                           uint64_t block_size_bytes) noexcept;
[[nodiscard]] RootInodeMetadataStatus DecodeRootQuotaBlock(const uint8_t *block,
                                                           uint64_t block_size_bytes,
                                                           RootQuotaBlock &quota_block) noexcept;

}
