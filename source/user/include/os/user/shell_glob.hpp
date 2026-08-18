#pragma once

#include <stdint.h>

namespace os::user {

[[nodiscard]] bool MatchShellGlobPattern(const char *pattern, const uint8_t *pattern_flags,
                                         uint64_t pattern_length_bytes, const char *candidate,
                                         uint64_t candidate_length_bytes) noexcept;

}
