#pragma once

#include <stdint.h>

namespace os::abi {

using UserIdentifier = uint32_t;
using GroupIdentifier = uint32_t;
using FileMode = uint32_t;

inline constexpr UserIdentifier OS_ABI_ROOT_USER_IDENTIFIER = 0U;
inline constexpr GroupIdentifier OS_ABI_ROOT_GROUP_IDENTIFIER = 0U;
inline constexpr GroupIdentifier OS_ABI_TTY_GROUP_IDENTIFIER = 5U;
inline constexpr UserIdentifier OS_ABI_IDENTIFIER_UNCHANGED = UINT32_MAX;
inline constexpr GroupIdentifier OS_ABI_GROUP_IDENTIFIER_UNCHANGED = UINT32_MAX;

// mode 的位值与 Linux UAPI 保持一致；文件类型和权限位共用 st_mode 字段。
inline constexpr FileMode OS_ABI_FILE_MODE_TYPE_MASK = 00170000U;
inline constexpr FileMode OS_ABI_FILE_MODE_SOCKET = 0140000U;
inline constexpr FileMode OS_ABI_FILE_MODE_SYMBOLIC_LINK = 0120000U;
inline constexpr FileMode OS_ABI_FILE_MODE_REGULAR = 0100000U;
inline constexpr FileMode OS_ABI_FILE_MODE_BLOCK_DEVICE = 0060000U;
inline constexpr FileMode OS_ABI_FILE_MODE_DIRECTORY = 0040000U;
inline constexpr FileMode OS_ABI_FILE_MODE_CHARACTER_DEVICE = 0020000U;
inline constexpr FileMode OS_ABI_FILE_MODE_FIFO = 0010000U;
inline constexpr FileMode OS_ABI_FILE_MODE_SET_USER_IDENTIFIER = 0004000U;
inline constexpr FileMode OS_ABI_FILE_MODE_SET_GROUP_IDENTIFIER = 0002000U;
inline constexpr FileMode OS_ABI_FILE_MODE_STICKY = 0001000U;
inline constexpr FileMode OS_ABI_FILE_MODE_OWNER_READ = 0000400U;
inline constexpr FileMode OS_ABI_FILE_MODE_OWNER_WRITE = 0000200U;
inline constexpr FileMode OS_ABI_FILE_MODE_OWNER_EXECUTE = 0000100U;
inline constexpr FileMode OS_ABI_FILE_MODE_GROUP_READ = 0000040U;
inline constexpr FileMode OS_ABI_FILE_MODE_GROUP_WRITE = 0000020U;
inline constexpr FileMode OS_ABI_FILE_MODE_GROUP_EXECUTE = 0000010U;
inline constexpr FileMode OS_ABI_FILE_MODE_OTHER_READ = 0000004U;
inline constexpr FileMode OS_ABI_FILE_MODE_OTHER_WRITE = 0000002U;
inline constexpr FileMode OS_ABI_FILE_MODE_OTHER_EXECUTE = 0000001U;
inline constexpr FileMode OS_ABI_FILE_MODE_SPECIAL_MASK = 0007000U;
inline constexpr FileMode OS_ABI_FILE_MODE_PERMISSION_MASK = 0000777U;
inline constexpr FileMode OS_ABI_FILE_MODE_CHANGEABLE_MASK = 0007777U;
inline constexpr FileMode OS_ABI_DEFAULT_CREATION_MASK = 0000022U;
inline constexpr FileMode OS_ABI_DEFAULT_FILE_CREATION_MODE = 0000666U;
inline constexpr FileMode OS_ABI_DEFAULT_DIRECTORY_CREATION_MODE = 0000777U;
inline constexpr FileMode OS_ABI_DEFAULT_SYMBOLIC_LINK_MODE = 0000777U;

inline constexpr uint64_t OS_ABI_SUPPLEMENTARY_GROUP_MAXIMUM_COUNT = 65536ULL;
inline constexpr uint64_t OS_ABI_CREDENTIAL_INFORMATION_SIZE_BYTES = 32ULL;
inline constexpr uint64_t OS_ABI_IDENTIFIER_CHANGE_REQUEST_SIZE_BYTES = 16ULL;

struct CredentialInformation final {
    UserIdentifier real_user_identifier;
    UserIdentifier effective_user_identifier;
    UserIdentifier saved_user_identifier;
    GroupIdentifier real_group_identifier;
    GroupIdentifier effective_group_identifier;
    GroupIdentifier saved_group_identifier;
    uint32_t supplementary_group_count;
    FileMode creation_mask;
};

struct IdentifierChangeRequest final {
    uint32_t real_identifier;
    uint32_t effective_identifier;
    uint32_t saved_identifier;
    uint32_t reserved;
};

static_assert(sizeof(CredentialInformation) == OS_ABI_CREDENTIAL_INFORMATION_SIZE_BYTES);
static_assert(sizeof(IdentifierChangeRequest) == OS_ABI_IDENTIFIER_CHANGE_REQUEST_SIZE_BYTES);

}
