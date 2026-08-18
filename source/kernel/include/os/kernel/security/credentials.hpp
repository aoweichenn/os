#pragma once

#include "os/abi/security.hpp"

#include <stdint.h>

namespace os::kernel::security {

inline constexpr uint64_t OS_KERNEL_CREDENTIAL_SUPPLEMENTARY_GROUP_CAPACITY = 32ULL;
inline constexpr uint32_t OS_KERNEL_ACCESS_READ = 1U << 0U;
inline constexpr uint32_t OS_KERNEL_ACCESS_WRITE = 1U << 1U;
inline constexpr uint32_t OS_KERNEL_ACCESS_EXECUTE = 1U << 2U;
inline constexpr uint32_t OS_KERNEL_ACCESS_VALID_MASK =
    OS_KERNEL_ACCESS_READ | OS_KERNEL_ACCESS_WRITE | OS_KERNEL_ACCESS_EXECUTE;

enum class CredentialStatus : uint64_t {
    Succeeded,
    InvalidArgument,
    CapacityExhausted,
    PermissionDenied,
};

struct Credentials final {
    os::abi::UserIdentifier real_user_identifier;
    os::abi::UserIdentifier effective_user_identifier;
    os::abi::UserIdentifier saved_user_identifier;
    os::abi::GroupIdentifier real_group_identifier;
    os::abi::GroupIdentifier effective_group_identifier;
    os::abi::GroupIdentifier saved_group_identifier;
    os::abi::GroupIdentifier
        supplementary_groups[OS_KERNEL_CREDENTIAL_SUPPLEMENTARY_GROUP_CAPACITY];
    uint64_t supplementary_group_count;
};

[[nodiscard]] Credentials RootCredentials() noexcept;
[[nodiscard]] bool IsSuperuser(const Credentials &credentials) noexcept;
[[nodiscard]] bool IsMemberOfGroup(const Credentials &credentials,
                                   os::abi::GroupIdentifier group_identifier) noexcept;
[[nodiscard]] bool HasAccess(const Credentials &credentials,
                             os::abi::UserIdentifier owner_user_identifier,
                             os::abi::GroupIdentifier owner_group_identifier,
                             os::abi::FileMode mode, uint32_t requested_access,
                             bool directory) noexcept;
[[nodiscard]] os::abi::FileMode ApplyCreationMask(os::abi::FileMode requested_mode,
                                                  os::abi::FileMode creation_mask) noexcept;
[[nodiscard]] bool ModeHasKnownBits(os::abi::FileMode mode) noexcept;
[[nodiscard]] bool ModeTypeMatches(os::abi::FileMode mode,
                                   os::abi::FileMode expected_type) noexcept;
[[nodiscard]] bool CanChangeMode(const Credentials &credentials,
                                 os::abi::UserIdentifier owner_user_identifier) noexcept;
[[nodiscard]] bool CanChangeOwner(const Credentials &credentials,
                                  os::abi::UserIdentifier owner_user_identifier,
                                  os::abi::GroupIdentifier owner_group_identifier,
                                  os::abi::UserIdentifier requested_user_identifier,
                                  os::abi::GroupIdentifier requested_group_identifier) noexcept;
[[nodiscard]] CredentialStatus SetSupplementaryGroups(Credentials &credentials,
                                                      const os::abi::GroupIdentifier *groups,
                                                      uint64_t group_count) noexcept;

}
