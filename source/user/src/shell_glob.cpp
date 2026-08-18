#include "os/user/shell_glob.hpp"

#include "os/user/shell_execution.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SHELL_GLOB_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_SHELL_GLOB_NO_STAR = UINT64_MAX;

[[nodiscard]] bool IsStar(const char character, const uint8_t flags) noexcept {
    return character == '*' && (flags & OS_USER_SHELL_STORAGE_GLOB_STAR_FLAG) != 0U;
}

[[nodiscard]] bool IsQuestion(const char character, const uint8_t flags) noexcept {
    return character == '?' && (flags & OS_USER_SHELL_STORAGE_GLOB_QUESTION_FLAG) != 0U;
}

}

bool MatchShellGlobPattern(const char *const pattern, const uint8_t *const pattern_flags,
                           const uint64_t pattern_length_bytes, const char *const candidate,
                           const uint64_t candidate_length_bytes) noexcept {
    if ((pattern == nullptr && pattern_length_bytes != OS_USER_SHELL_GLOB_EMPTY_VALUE) ||
        (pattern_flags == nullptr && pattern_length_bytes != OS_USER_SHELL_GLOB_EMPTY_VALUE) ||
        (candidate == nullptr && candidate_length_bytes != OS_USER_SHELL_GLOB_EMPTY_VALUE)) {
        return false;
    }

    uint64_t pattern_index = OS_USER_SHELL_GLOB_EMPTY_VALUE;
    uint64_t candidate_index = OS_USER_SHELL_GLOB_EMPTY_VALUE;
    uint64_t last_star_index = OS_USER_SHELL_GLOB_NO_STAR;
    uint64_t star_candidate_index = OS_USER_SHELL_GLOB_EMPTY_VALUE;
    while (candidate_index < candidate_length_bytes) {
        if (pattern_index < pattern_length_bytes &&
            IsStar(pattern[pattern_index], pattern_flags[pattern_index])) {
            last_star_index = pattern_index;
            ++pattern_index;
            star_candidate_index = candidate_index;
            continue;
        }
        if (pattern_index < pattern_length_bytes &&
            (IsQuestion(pattern[pattern_index], pattern_flags[pattern_index]) ||
             pattern[pattern_index] == candidate[candidate_index])) {
            ++pattern_index;
            ++candidate_index;
            continue;
        }
        if (last_star_index == OS_USER_SHELL_GLOB_NO_STAR) {
            return false;
        }
        pattern_index = last_star_index + 1ULL;
        ++star_candidate_index;
        candidate_index = star_candidate_index;
    }
    while (pattern_index < pattern_length_bytes &&
           IsStar(pattern[pattern_index], pattern_flags[pattern_index])) {
        ++pattern_index;
    }
    return pattern_index == pattern_length_bytes;
}

}
