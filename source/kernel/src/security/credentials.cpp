#include "os/kernel/security/credentials.hpp"

namespace os::kernel::security {

namespace {

constexpr os::abi::FileMode OS_KERNEL_CREDENTIAL_OWNER_PERMISSION_SHIFT = 6U;
constexpr os::abi::FileMode OS_KERNEL_CREDENTIAL_GROUP_PERMISSION_SHIFT = 3U;
constexpr os::abi::FileMode OS_KERNEL_CREDENTIAL_CLASS_PERMISSION_MASK = 0000007U;

[[nodiscard]] os::abi::FileMode RequestedPermissionBits(const uint32_t requested_access) noexcept {
    os::abi::FileMode permission_bits = 0U;
    if ((requested_access & OS_KERNEL_ACCESS_READ) != 0U) {
        permission_bits |= os::abi::OS_ABI_FILE_MODE_OTHER_READ;
    }
    if ((requested_access & OS_KERNEL_ACCESS_WRITE) != 0U) {
        permission_bits |= os::abi::OS_ABI_FILE_MODE_OTHER_WRITE;
    }
    if ((requested_access & OS_KERNEL_ACCESS_EXECUTE) != 0U) {
        permission_bits |= os::abi::OS_ABI_FILE_MODE_OTHER_EXECUTE;
    }
    return permission_bits;
}

}

Credentials RootCredentials() noexcept {
    Credentials credentials{};
    credentials.real_user_identifier = os::abi::OS_ABI_ROOT_USER_IDENTIFIER;
    credentials.effective_user_identifier = os::abi::OS_ABI_ROOT_USER_IDENTIFIER;
    credentials.saved_user_identifier = os::abi::OS_ABI_ROOT_USER_IDENTIFIER;
    credentials.real_group_identifier = os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER;
    credentials.effective_group_identifier = os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER;
    credentials.saved_group_identifier = os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER;
    return credentials;
}

bool IsSuperuser(const Credentials &credentials) noexcept {
    return credentials.effective_user_identifier == os::abi::OS_ABI_ROOT_USER_IDENTIFIER;
}

bool IsMemberOfGroup(const Credentials &credentials,
                     const os::abi::GroupIdentifier group_identifier) noexcept {
    if (credentials.effective_group_identifier == group_identifier) {
        return true;
    }
    for (uint64_t group_index = 0ULL; group_index < credentials.supplementary_group_count;
         ++group_index) {
        if (credentials.supplementary_groups[group_index] == group_identifier) {
            return true;
        }
    }
    return false;
}

bool HasAccess(const Credentials &credentials, const os::abi::UserIdentifier owner_user_identifier,
               const os::abi::GroupIdentifier owner_group_identifier, const os::abi::FileMode mode,
               const uint32_t requested_access, const bool directory) noexcept {
    if ((requested_access & ~OS_KERNEL_ACCESS_VALID_MASK) != 0U || !ModeHasKnownBits(mode)) {
        return false;
    }
    if (requested_access == 0U) {
        return true;
    }
    if (IsSuperuser(credentials)) {
        if ((requested_access & OS_KERNEL_ACCESS_EXECUTE) == 0U || directory) {
            return true;
        }
        return (mode &
                (os::abi::OS_ABI_FILE_MODE_OWNER_EXECUTE | os::abi::OS_ABI_FILE_MODE_GROUP_EXECUTE |
                 os::abi::OS_ABI_FILE_MODE_OTHER_EXECUTE)) != 0U;
    }

    os::abi::FileMode permissions = mode & os::abi::OS_ABI_FILE_MODE_PERMISSION_MASK;
    if (credentials.effective_user_identifier == owner_user_identifier) {
        permissions >>= OS_KERNEL_CREDENTIAL_OWNER_PERMISSION_SHIFT;
    } else if (IsMemberOfGroup(credentials, owner_group_identifier)) {
        permissions >>= OS_KERNEL_CREDENTIAL_GROUP_PERMISSION_SHIFT;
    }
    permissions &= OS_KERNEL_CREDENTIAL_CLASS_PERMISSION_MASK;
    const os::abi::FileMode requested_permissions = RequestedPermissionBits(requested_access);
    return (permissions & requested_permissions) == requested_permissions;
}

os::abi::FileMode ApplyCreationMask(const os::abi::FileMode requested_mode,
                                    const os::abi::FileMode creation_mask) noexcept {
    return (requested_mode & ~os::abi::OS_ABI_FILE_MODE_PERMISSION_MASK) |
           ((requested_mode & os::abi::OS_ABI_FILE_MODE_PERMISSION_MASK) &
            ~(creation_mask & os::abi::OS_ABI_FILE_MODE_PERMISSION_MASK));
}

bool ModeHasKnownBits(const os::abi::FileMode mode) noexcept {
    const os::abi::FileMode type = mode & os::abi::OS_ABI_FILE_MODE_TYPE_MASK;
    const bool type_valid = type == os::abi::OS_ABI_FILE_MODE_REGULAR ||
                            type == os::abi::OS_ABI_FILE_MODE_DIRECTORY ||
                            type == os::abi::OS_ABI_FILE_MODE_CHARACTER_DEVICE ||
                            type == os::abi::OS_ABI_FILE_MODE_SYMBOLIC_LINK;
    return type_valid && (mode & ~(os::abi::OS_ABI_FILE_MODE_TYPE_MASK |
                                   os::abi::OS_ABI_FILE_MODE_CHANGEABLE_MASK)) == 0U;
}

bool ModeTypeMatches(const os::abi::FileMode mode, const os::abi::FileMode expected_type) noexcept {
    return ModeHasKnownBits(mode) && (mode & os::abi::OS_ABI_FILE_MODE_TYPE_MASK) == expected_type;
}

bool CanChangeMode(const Credentials &credentials,
                   const os::abi::UserIdentifier owner_user_identifier) noexcept {
    return IsSuperuser(credentials) ||
           credentials.effective_user_identifier == owner_user_identifier;
}

bool CanChangeOwner(const Credentials &credentials,
                    const os::abi::UserIdentifier owner_user_identifier,
                    const os::abi::GroupIdentifier owner_group_identifier,
                    const os::abi::UserIdentifier requested_user_identifier,
                    const os::abi::GroupIdentifier requested_group_identifier) noexcept {
    if (IsSuperuser(credentials)) {
        return true;
    }
    const bool user_unchanged = requested_user_identifier == os::abi::OS_ABI_IDENTIFIER_UNCHANGED ||
                                requested_user_identifier == owner_user_identifier;
    const bool group_unchanged =
        requested_group_identifier == os::abi::OS_ABI_GROUP_IDENTIFIER_UNCHANGED ||
        requested_group_identifier == owner_group_identifier;
    const bool group_selectable =
        requested_group_identifier != os::abi::OS_ABI_GROUP_IDENTIFIER_UNCHANGED &&
        IsMemberOfGroup(credentials, requested_group_identifier);
    return credentials.effective_user_identifier == owner_user_identifier && user_unchanged &&
           (group_unchanged || group_selectable);
}

CredentialStatus SetSupplementaryGroups(Credentials &credentials,
                                        const os::abi::GroupIdentifier *const groups,
                                        const uint64_t group_count) noexcept {
    if (group_count > OS_KERNEL_CREDENTIAL_SUPPLEMENTARY_GROUP_CAPACITY) {
        return CredentialStatus::CapacityExhausted;
    }
    if (group_count != 0ULL && groups == nullptr) {
        return CredentialStatus::InvalidArgument;
    }
    for (uint64_t group_index = 0ULL; group_index < group_count; ++group_index) {
        for (uint64_t previous_index = 0ULL; previous_index < group_index; ++previous_index) {
            if (groups[previous_index] == groups[group_index]) {
                return CredentialStatus::InvalidArgument;
            }
        }
    }
    for (uint64_t group_index = 0ULL;
         group_index < OS_KERNEL_CREDENTIAL_SUPPLEMENTARY_GROUP_CAPACITY; ++group_index) {
        credentials.supplementary_groups[group_index] =
            group_index < group_count ? groups[group_index] : os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER;
    }
    credentials.supplementary_group_count = group_count;
    return CredentialStatus::Succeeded;
}

}
