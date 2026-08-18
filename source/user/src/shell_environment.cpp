#include "os/user/shell_environment.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE = 1ULL;
constexpr int64_t OS_USER_SHELL_ENVIRONMENT_NOT_FOUND_INDEX = -1LL;
constexpr char OS_USER_SHELL_ENVIRONMENT_ASSIGNMENT_SEPARATOR = '=';
constexpr char OS_USER_SHELL_ENVIRONMENT_STRING_TERMINATOR = '\0';

[[nodiscard]] bool IsAlphabetic(const char character) noexcept {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

[[nodiscard]] bool IsDigit(const char character) noexcept {
    return character >= '0' && character <= '9';
}

[[nodiscard]] bool NamesEqual(const ShellEnvironmentEntry &entry, const char *const name,
                              const uint64_t name_length_bytes) noexcept {
    if (!entry.occupied || name == nullptr || entry.name_length_bytes != name_length_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         byte_index < name_length_bytes; ++byte_index) {
        if (entry.bytes[byte_index] != name[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint64_t BoundedStringLength(const char *const text,
                                           const uint64_t capacity_bytes) noexcept {
    if (text == nullptr) {
        return capacity_bytes;
    }
    uint64_t length_bytes = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
    while (length_bytes < capacity_bytes &&
           text[length_bytes] != OS_USER_SHELL_ENVIRONMENT_STRING_TERMINATOR) {
        ++length_bytes;
    }
    return length_bytes;
}

}

bool IsShellEnvironmentNameValid(const char *const name,
                                 const uint64_t name_length_bytes) noexcept {
    if (name == nullptr || name_length_bytes == OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE ||
        name_length_bytes > OS_USER_SHELL_ENVIRONMENT_MAXIMUM_NAME_SIZE_BYTES ||
        (!IsAlphabetic(name[OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE]) &&
         name[OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE] != '_')) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE;
         byte_index < name_length_bytes; ++byte_index) {
        if (!IsAlphabetic(name[byte_index]) && !IsDigit(name[byte_index]) &&
            name[byte_index] != '_') {
            return false;
        }
    }
    return true;
}

ShellEnvironmentTable::ShellEnvironmentTable() noexcept = default;

ShellEnvironmentStatus
ShellEnvironmentTable::Initialize(const char *const *const environment) noexcept {
    this->Clear();
    if (environment == nullptr) {
        return ShellEnvironmentStatus::InvalidArgument;
    }
    for (uint64_t environment_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         environment_index <= OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT; ++environment_index) {
        const char *const entry = environment[environment_index];
        if (entry == nullptr) {
            return ShellEnvironmentStatus::Succeeded;
        }
        if (environment_index == OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT) {
            this->Clear();
            return ShellEnvironmentStatus::CapacityExceeded;
        }
        const uint64_t entry_length_bytes =
            BoundedStringLength(entry, OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_SIZE_BYTES);
        const ShellEnvironmentStatus status = this->SetAssignment(entry, entry_length_bytes);
        if (status != ShellEnvironmentStatus::Succeeded) {
            this->Clear();
            return status;
        }
    }
    this->Clear();
    return ShellEnvironmentStatus::CapacityExceeded;
}

void ShellEnvironmentTable::Clear() noexcept {
    for (uint64_t entry_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         entry_index < OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT; ++entry_index) {
        this->entries_[entry_index] = ShellEnvironmentEntry{};
    }
    this->entry_count_ = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
}

ShellEnvironmentStatus ShellEnvironmentTable::Set(const char *const name,
                                                  const uint64_t name_length_bytes,
                                                  const char *const value,
                                                  const uint64_t value_length_bytes) noexcept {
    if (!IsShellEnvironmentNameValid(name, name_length_bytes)) {
        return ShellEnvironmentStatus::InvalidName;
    }
    if (value == nullptr && value_length_bytes != OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE) {
        return ShellEnvironmentStatus::InvalidArgument;
    }
    const uint64_t required_length_bytes =
        name_length_bytes + OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE + value_length_bytes;
    if (required_length_bytes >= OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_SIZE_BYTES) {
        return ShellEnvironmentStatus::EntryTooLong;
    }

    int64_t destination_index = this->FindEntryIndex(name, name_length_bytes);
    if (destination_index == OS_USER_SHELL_ENVIRONMENT_NOT_FOUND_INDEX) {
        if (this->entry_count_ >= OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT) {
            return ShellEnvironmentStatus::CapacityExceeded;
        }
        for (uint64_t entry_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
             entry_index < OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT; ++entry_index) {
            if (!this->entries_[entry_index].occupied) {
                destination_index = static_cast<int64_t>(entry_index);
                break;
            }
        }
    }
    if (destination_index == OS_USER_SHELL_ENVIRONMENT_NOT_FOUND_INDEX) {
        return ShellEnvironmentStatus::CapacityExceeded;
    }

    ShellEnvironmentEntry replacement{};
    for (uint64_t byte_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         byte_index < name_length_bytes; ++byte_index) {
        replacement.bytes[byte_index] = name[byte_index];
    }
    replacement.bytes[name_length_bytes] = OS_USER_SHELL_ENVIRONMENT_ASSIGNMENT_SEPARATOR;
    for (uint64_t byte_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         byte_index < value_length_bytes; ++byte_index) {
        replacement.bytes[name_length_bytes + OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE + byte_index] =
            value[byte_index];
    }
    replacement.bytes[required_length_bytes] = OS_USER_SHELL_ENVIRONMENT_STRING_TERMINATOR;
    replacement.name_length_bytes = static_cast<uint16_t>(name_length_bytes);
    replacement.length_bytes = static_cast<uint16_t>(required_length_bytes);
    replacement.occupied = true;

    ShellEnvironmentEntry &destination = this->entries_[static_cast<uint64_t>(destination_index)];
    const bool inserting = !destination.occupied;
    destination = replacement;
    if (inserting) {
        ++this->entry_count_;
    }
    return ShellEnvironmentStatus::Succeeded;
}

ShellEnvironmentStatus
ShellEnvironmentTable::SetAssignment(const char *const assignment,
                                     const uint64_t assignment_length_bytes) noexcept {
    if (assignment == nullptr ||
        assignment_length_bytes >= OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_SIZE_BYTES) {
        return assignment == nullptr ? ShellEnvironmentStatus::InvalidArgument
                                     : ShellEnvironmentStatus::EntryTooLong;
    }
    uint64_t separator_index = assignment_length_bytes;
    for (uint64_t byte_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         byte_index < assignment_length_bytes; ++byte_index) {
        if (assignment[byte_index] == OS_USER_SHELL_ENVIRONMENT_ASSIGNMENT_SEPARATOR) {
            separator_index = byte_index;
            break;
        }
    }
    if (separator_index == assignment_length_bytes) {
        return ShellEnvironmentStatus::InvalidAssignment;
    }
    return this->Set(assignment, separator_index,
                     assignment + separator_index + OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE,
                     assignment_length_bytes - separator_index -
                         OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE);
}

ShellEnvironmentStatus ShellEnvironmentTable::Unset(const char *const name,
                                                    const uint64_t name_length_bytes) noexcept {
    if (!IsShellEnvironmentNameValid(name, name_length_bytes)) {
        return ShellEnvironmentStatus::InvalidName;
    }
    const int64_t entry_index = this->FindEntryIndex(name, name_length_bytes);
    if (entry_index == OS_USER_SHELL_ENVIRONMENT_NOT_FOUND_INDEX) {
        return ShellEnvironmentStatus::NotFound;
    }
    this->entries_[static_cast<uint64_t>(entry_index)] = ShellEnvironmentEntry{};
    --this->entry_count_;
    return ShellEnvironmentStatus::Succeeded;
}

ShellEnvironmentStatus ShellEnvironmentTable::Find(const char *const name,
                                                   const uint64_t name_length_bytes,
                                                   const char *&value,
                                                   uint64_t &value_length_bytes) const noexcept {
    value = nullptr;
    value_length_bytes = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
    if (!IsShellEnvironmentNameValid(name, name_length_bytes)) {
        return ShellEnvironmentStatus::InvalidName;
    }
    const int64_t entry_index = this->FindEntryIndex(name, name_length_bytes);
    if (entry_index == OS_USER_SHELL_ENVIRONMENT_NOT_FOUND_INDEX) {
        return ShellEnvironmentStatus::NotFound;
    }
    const ShellEnvironmentEntry &entry = this->entries_[static_cast<uint64_t>(entry_index)];
    value = entry.bytes + entry.name_length_bytes + OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE;
    value_length_bytes =
        entry.length_bytes - entry.name_length_bytes - OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE;
    return ShellEnvironmentStatus::Succeeded;
}

ShellEnvironmentStatus ShellEnvironmentTable::Read(const uint64_t logical_index, const char *&entry,
                                                   uint64_t &entry_length_bytes) const noexcept {
    entry = nullptr;
    entry_length_bytes = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
    if (logical_index >= this->entry_count_) {
        return ShellEnvironmentStatus::NotFound;
    }
    uint64_t current_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
    for (uint64_t entry_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         entry_index < OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT; ++entry_index) {
        if (!this->entries_[entry_index].occupied) {
            continue;
        }
        if (current_index == logical_index) {
            entry = this->entries_[entry_index].bytes;
            entry_length_bytes = this->entries_[entry_index].length_bytes;
            return ShellEnvironmentStatus::Succeeded;
        }
        ++current_index;
    }
    return ShellEnvironmentStatus::NotFound;
}

uint64_t ShellEnvironmentTable::Count() const noexcept { return this->entry_count_; }

bool ShellEnvironmentTable::Validate() const noexcept {
    uint64_t occupied_count = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
    for (uint64_t entry_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         entry_index < OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT; ++entry_index) {
        const ShellEnvironmentEntry &entry = this->entries_[entry_index];
        if (!entry.occupied) {
            if (entry.name_length_bytes != OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE ||
                entry.length_bytes != OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE) {
                return false;
            }
            continue;
        }
        ++occupied_count;
        if (!IsShellEnvironmentNameValid(entry.bytes, entry.name_length_bytes) ||
            entry.length_bytes <= entry.name_length_bytes ||
            entry.length_bytes >= OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_SIZE_BYTES ||
            entry.bytes[entry.name_length_bytes] !=
                OS_USER_SHELL_ENVIRONMENT_ASSIGNMENT_SEPARATOR ||
            entry.bytes[entry.length_bytes] != OS_USER_SHELL_ENVIRONMENT_STRING_TERMINATOR) {
            return false;
        }
        for (uint64_t other_index = entry_index + OS_USER_SHELL_ENVIRONMENT_FIRST_VALUE;
             other_index < OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT; ++other_index) {
            if (NamesEqual(this->entries_[other_index], entry.bytes, entry.name_length_bytes)) {
                return false;
            }
        }
    }
    return occupied_count == this->entry_count_ &&
           this->entry_count_ <= OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT;
}

int64_t ShellEnvironmentTable::FindEntryIndex(const char *const name,
                                              const uint64_t name_length_bytes) const noexcept {
    for (uint64_t entry_index = OS_USER_SHELL_ENVIRONMENT_EMPTY_VALUE;
         entry_index < OS_USER_SHELL_ENVIRONMENT_MAXIMUM_ENTRY_COUNT; ++entry_index) {
        if (NamesEqual(this->entries_[entry_index], name, name_length_bytes)) {
            return static_cast<int64_t>(entry_index);
        }
    }
    return OS_USER_SHELL_ENVIRONMENT_NOT_FOUND_INDEX;
}

}
