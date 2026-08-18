#include "os/user/shell_line_editor.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_EDITOR_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_EDITOR_FIRST_VALUE = 1ULL;
constexpr char OS_USER_SHELL_EDITOR_STRING_TERMINATOR = '\0';

[[nodiscard]] bool BytesEqual(const char *const first, const char *const second,
                              const uint64_t length_bytes) noexcept {
    if (first == nullptr || second == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (first[byte_index] != second[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool StartsWith(const ShellCompletionCandidate &candidate, const char *const prefix,
                              const uint64_t prefix_length_bytes) noexcept {
    return candidate.bytes != nullptr && candidate.length_bytes >= prefix_length_bytes &&
           BytesEqual(candidate.bytes, prefix, prefix_length_bytes);
}

}

ShellLineEditor::ShellLineEditor() noexcept = default;

void ShellLineEditor::Clear() noexcept {
    for (uint64_t byte_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE;
         byte_index <= OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES; ++byte_index) {
        this->line_[byte_index] = OS_USER_SHELL_EDITOR_STRING_TERMINATOR;
    }
    this->length_bytes_ = 0U;
    this->cursor_ = 0U;
    this->ResetBrowse();
}

bool ShellLineEditor::Insert(const char character) noexcept {
    if (this->length_bytes_ >= OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES) {
        return false;
    }
    for (uint64_t byte_index = this->length_bytes_; byte_index > this->cursor_; --byte_index) {
        this->line_[byte_index] = this->line_[byte_index - OS_USER_SHELL_EDITOR_FIRST_VALUE];
    }
    this->line_[this->cursor_] = character;
    ++this->cursor_;
    ++this->length_bytes_;
    this->line_[this->length_bytes_] = OS_USER_SHELL_EDITOR_STRING_TERMINATOR;
    this->ResetBrowse();
    return true;
}

bool ShellLineEditor::Backspace() noexcept {
    if (this->cursor_ == 0U) {
        return false;
    }
    const uint64_t removed_index = this->cursor_ - OS_USER_SHELL_EDITOR_FIRST_VALUE;
    for (uint64_t byte_index = removed_index; byte_index < this->length_bytes_; ++byte_index) {
        this->line_[byte_index] = this->line_[byte_index + OS_USER_SHELL_EDITOR_FIRST_VALUE];
    }
    --this->cursor_;
    --this->length_bytes_;
    this->ResetBrowse();
    return true;
}

bool ShellLineEditor::MoveLeft() noexcept {
    if (this->cursor_ == 0U) {
        return false;
    }
    --this->cursor_;
    return true;
}

bool ShellLineEditor::MoveRight() noexcept {
    if (this->cursor_ >= this->length_bytes_) {
        return false;
    }
    ++this->cursor_;
    return true;
}

bool ShellLineEditor::SelectPreviousHistory() noexcept {
    if (this->history_count_ == 0U) {
        return false;
    }
    if (!this->browsing_) {
        for (uint64_t byte_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE;
             byte_index <= this->length_bytes_; ++byte_index) {
            this->draft_[byte_index] = this->line_[byte_index];
        }
        this->draft_length_bytes_ = this->length_bytes_;
        this->browse_index_ = static_cast<uint16_t>(this->history_count_ - 1U);
        this->browsing_ = true;
    } else if (this->browse_index_ > 0U) {
        --this->browse_index_;
    } else {
        return false;
    }
    this->CopyLine(this->history_[this->browse_index_],
                   this->history_lengths_[this->browse_index_]);
    return true;
}

bool ShellLineEditor::SelectNextHistory() noexcept {
    if (!this->browsing_) {
        return false;
    }
    if (this->browse_index_ + 1U < this->history_count_) {
        ++this->browse_index_;
        this->CopyLine(this->history_[this->browse_index_],
                       this->history_lengths_[this->browse_index_]);
        return true;
    }
    this->CopyLine(this->draft_, this->draft_length_bytes_);
    this->ResetBrowse();
    return true;
}

void ShellLineEditor::CommitHistory() noexcept {
    if (this->length_bytes_ == 0U) {
        this->ResetBrowse();
        return;
    }
    if (this->history_count_ > 0U &&
        this->history_lengths_[this->history_count_ - 1U] == this->length_bytes_ &&
        BytesEqual(this->history_[this->history_count_ - 1U], this->line_, this->length_bytes_)) {
        this->ResetBrowse();
        return;
    }
    if (this->history_count_ == OS_USER_SHELL_EDITOR_HISTORY_CAPACITY) {
        for (uint64_t history_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE;
             history_index + OS_USER_SHELL_EDITOR_FIRST_VALUE < this->history_count_;
             ++history_index) {
            this->history_lengths_[history_index] = this->history_lengths_[history_index + 1ULL];
            for (uint64_t byte_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE;
                 byte_index <= OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES; ++byte_index) {
                this->history_[history_index][byte_index] =
                    this->history_[history_index + 1ULL][byte_index];
            }
        }
        --this->history_count_;
    }
    const uint64_t destination_index = this->history_count_;
    this->history_lengths_[destination_index] = this->length_bytes_;
    for (uint64_t byte_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE; byte_index <= this->length_bytes_;
         ++byte_index) {
        this->history_[destination_index][byte_index] = this->line_[byte_index];
    }
    ++this->history_count_;
    this->ResetBrowse();
}

bool ShellLineEditor::CompleteCommand(const ShellCompletionCandidate *const candidates,
                                      const uint64_t candidate_count) noexcept {
    if (candidates == nullptr || candidate_count == OS_USER_SHELL_EDITOR_EMPTY_VALUE ||
        this->cursor_ != this->length_bytes_ || this->cursor_ == 0U) {
        return false;
    }
    for (uint64_t byte_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE; byte_index < this->cursor_;
         ++byte_index) {
        if (this->line_[byte_index] == ' ' || this->line_[byte_index] == '\t' ||
            this->line_[byte_index] == '/' || this->line_[byte_index] == '|' ||
            this->line_[byte_index] == '&' || this->line_[byte_index] == ';') {
            return false;
        }
    }

    uint64_t first_match_index = candidate_count;
    uint64_t match_count = OS_USER_SHELL_EDITOR_EMPTY_VALUE;
    uint64_t common_length_bytes = OS_USER_SHELL_EDITOR_EMPTY_VALUE;
    for (uint64_t candidate_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE;
         candidate_index < candidate_count; ++candidate_index) {
        const ShellCompletionCandidate &candidate = candidates[candidate_index];
        if (!StartsWith(candidate, this->line_, this->cursor_)) {
            continue;
        }
        if (first_match_index == candidate_count) {
            first_match_index = candidate_index;
            common_length_bytes = candidate.length_bytes;
        } else {
            uint64_t common_index = this->cursor_;
            while (common_index < common_length_bytes && common_index < candidate.length_bytes &&
                   candidates[first_match_index].bytes[common_index] ==
                       candidate.bytes[common_index]) {
                ++common_index;
            }
            common_length_bytes = common_index;
        }
        ++match_count;
    }
    if (match_count == OS_USER_SHELL_EDITOR_EMPTY_VALUE) {
        return false;
    }

    bool changed = false;
    const ShellCompletionCandidate &first_match = candidates[first_match_index];
    while (this->length_bytes_ < common_length_bytes) {
        if (!this->Insert(first_match.bytes[this->length_bytes_])) {
            return changed;
        }
        changed = true;
    }
    if (match_count == OS_USER_SHELL_EDITOR_FIRST_VALUE &&
        this->length_bytes_ < OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES && this->Insert(' ')) {
        changed = true;
    }
    return changed;
}

const char *ShellLineEditor::Bytes() const noexcept { return this->line_; }

uint64_t ShellLineEditor::Length() const noexcept { return this->length_bytes_; }

uint64_t ShellLineEditor::Cursor() const noexcept { return this->cursor_; }

uint64_t ShellLineEditor::HistoryCount() const noexcept { return this->history_count_; }

bool ShellLineEditor::Validate() const noexcept {
    if (this->length_bytes_ > OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES ||
        this->cursor_ > this->length_bytes_ ||
        this->line_[this->length_bytes_] != OS_USER_SHELL_EDITOR_STRING_TERMINATOR ||
        this->history_count_ > OS_USER_SHELL_EDITOR_HISTORY_CAPACITY ||
        (this->browsing_ && this->browse_index_ >= this->history_count_)) {
        return false;
    }
    for (uint64_t history_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE;
         history_index < this->history_count_; ++history_index) {
        if (this->history_lengths_[history_index] == 0U ||
            this->history_lengths_[history_index] > OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES ||
            this->history_[history_index][this->history_lengths_[history_index]] !=
                OS_USER_SHELL_EDITOR_STRING_TERMINATOR) {
            return false;
        }
    }
    return true;
}

void ShellLineEditor::CopyLine(const char *const bytes, const uint64_t length_bytes) noexcept {
    if (bytes == nullptr || length_bytes > OS_USER_SHELL_EDITOR_LINE_CAPACITY_BYTES) {
        return;
    }
    for (uint64_t byte_index = OS_USER_SHELL_EDITOR_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        this->line_[byte_index] = bytes[byte_index];
    }
    this->line_[length_bytes] = OS_USER_SHELL_EDITOR_STRING_TERMINATOR;
    this->length_bytes_ = static_cast<uint16_t>(length_bytes);
    this->cursor_ = static_cast<uint16_t>(length_bytes);
}

void ShellLineEditor::ResetBrowse() noexcept {
    this->browsing_ = false;
    this->browse_index_ = 0U;
    this->draft_length_bytes_ = 0U;
}

}
