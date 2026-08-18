#pragma once

#include <stdint.h>

namespace os::user {

inline constexpr uint64_t OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT = 32ULL;
inline constexpr uint64_t OS_USER_SHELL_ENVIRONMENT_MAXIMUM_NAME_SIZE_BYTES = 31ULL;
inline constexpr uint64_t OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_SIZE_BYTES = 128ULL;

enum class ShellEnvironmentStatus : uint64_t {
    Succeeded,
    NotFound,
    CapacityExceeded,
    EntryTooLong,
    InvalidName,
    InvalidAssignment,
    InvalidArgument,
};

struct ShellEnvironmentEntry final {
    char bytes[OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_SIZE_BYTES];
    uint16_t name_length_bytes;
    uint16_t length_bytes;
    bool occupied;
};

class ShellEnvironmentTable final {
  public:
    ShellEnvironmentTable() noexcept;

    [[nodiscard]] ShellEnvironmentStatus Initialize(const char *const *environment) noexcept;
    void Clear() noexcept;
    [[nodiscard]] ShellEnvironmentStatus Set(const char *name, uint64_t name_length_bytes,
                                             const char *value,
                                             uint64_t value_length_bytes) noexcept;
    [[nodiscard]] ShellEnvironmentStatus SetAssignment(const char *assignment,
                                                       uint64_t assignment_length_bytes) noexcept;
    [[nodiscard]] ShellEnvironmentStatus Unset(const char *name,
                                               uint64_t name_length_bytes) noexcept;
    [[nodiscard]] ShellEnvironmentStatus Find(const char *name, uint64_t name_length_bytes,
                                              const char *&value,
                                              uint64_t &value_length_bytes) const noexcept;
    [[nodiscard]] ShellEnvironmentStatus Read(uint64_t logical_index, const char *&entry,
                                              uint64_t &entry_length_bytes) const noexcept;
    [[nodiscard]] uint64_t Count() const noexcept;
    [[nodiscard]] bool Validate() const noexcept;

  private:
    [[nodiscard]] int64_t FindEntryIndex(const char *name,
                                         uint64_t name_length_bytes) const noexcept;

    ShellEnvironmentEntry entries_[OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT]{};
    uint64_t entry_count_{};
};

[[nodiscard]] bool IsShellEnvironmentNameValid(const char *name,
                                               uint64_t name_length_bytes) noexcept;

}
