#include <os/user/system_call.hpp>

#include <os/abi/system_call.hpp>

namespace os::user {

int64_t SeekDescriptor(const uint64_t descriptor, const int64_t displacement_bytes,
                       const uint64_t origin) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SeekDescriptor),
                            descriptor, static_cast<uint64_t>(displacement_bytes), origin);
}

int64_t ReadDescriptorAt(const uint64_t descriptor, uint8_t *const destination,
                         const uint64_t capacity_bytes, const uint64_t offset_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ReadDescriptorAt),
                            descriptor, reinterpret_cast<uint64_t>(destination), capacity_bytes,
                            offset_bytes);
}

int64_t WriteDescriptorAt(const uint64_t descriptor, const uint8_t *const source,
                          const uint64_t length_bytes, const uint64_t offset_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WriteDescriptorAt),
                            descriptor, reinterpret_cast<uint64_t>(source), length_bytes,
                            offset_bytes);
}

int64_t StatDescriptor(const uint64_t descriptor, os::abi::FileInformation &information) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::StatDescriptor),
                            descriptor, reinterpret_cast<uint64_t>(&information),
                            sizeof(information));
}

int64_t TruncateDescriptor(const uint64_t descriptor, const uint64_t size_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::TruncateDescriptor),
                            descriptor, size_bytes, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ChangeDescriptorMode(const uint64_t descriptor, const os::abi::FileMode mode) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeDescriptorMode),
                            descriptor, static_cast<uint64_t>(mode),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ChangeDescriptorOwner(const uint64_t descriptor,
                              const os::abi::UserIdentifier user_identifier,
                              const os::abi::GroupIdentifier group_identifier) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeDescriptorOwner),
                            descriptor, static_cast<uint64_t>(user_identifier),
                            static_cast<uint64_t>(group_identifier));
}

int64_t GetFileStatusFlags(const uint64_t descriptor) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetFileStatusFlags),
                            descriptor, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetFileStatusFlags(const uint64_t descriptor, const uint64_t file_status_flags) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetFileStatusFlags),
                            descriptor, file_status_flags, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

}
