#pragma once

#include <stdint.h>

namespace os::user {

inline constexpr uint64_t OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES = 512ULL;
inline constexpr uint64_t OS_USER_SHELL_EDITOR_HISTORY_CAPACITY = 16ULL;

struct ShellCompletionCandidate final {
    const char *bytes;
    uint16_t length_bytes;
};

class ShellLineEditor final {
  public:
    ShellLineEditor() noexcept;

    void Clear() noexcept;
    [[nodiscard]] bool Insert(char character) noexcept;
    [[nodiscard]] bool Backspace() noexcept;
    [[nodiscard]] bool MoveLeft() noexcept;
    [[nodiscard]] bool MoveRight() noexcept;
    [[nodiscard]] bool SelectPreviousHistory() noexcept;
    [[nodiscard]] bool SelectNextHistory() noexcept;
    void CommitHistory() noexcept;
    [[nodiscard]] bool CompleteCommand(const ShellCompletionCandidate *candidates,
                                       uint64_t candidate_count) noexcept;
    [[nodiscard]] const char *Bytes() const noexcept;
    [[nodiscard]] uint64_t Length() const noexcept;
    [[nodiscard]] uint64_t Cursor() const noexcept;
    [[nodiscard]] uint64_t HistoryCount() const noexcept;
    [[nodiscard]] bool Validate() const noexcept;

  private:
    void CopyLine(const char *bytes, uint64_t length_bytes) noexcept;
    void ResetBrowse() noexcept;

    char line_[OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES + 1ULL]{};
    char history_[OS_USER_SHELL_EDITOR_HISTORY_CAPACITY]
                 [OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES + 1ULL]{};
    char draft_[OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES + 1ULL]{};
    uint16_t history_lengths_[OS_USER_SHELL_EDITOR_HISTORY_CAPACITY]{};
    uint16_t length_bytes_{};
    uint16_t cursor_{};
    uint16_t history_count_{};
    uint16_t browse_index_{};
    uint16_t draft_length_bytes_{};
    bool browsing_{};
};

}
