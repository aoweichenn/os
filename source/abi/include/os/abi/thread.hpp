#pragma once

#include <stdint.h>

namespace os::abi {

inline constexpr uint64_t OS_ABI_THREAD_CREATE_REQUEST_SIZE_BYTES = 48ULL;
inline constexpr uint64_t OS_ABI_THREAD_JOIN_RESULT_SIZE_BYTES = 16ULL;
inline constexpr uint64_t OS_ABI_THREAD_STACK_MINIMUM_SIZE_BYTES = 16ULL * 1024ULL;
inline constexpr uint64_t OS_ABI_THREAD_STACK_DEFAULT_SIZE_BYTES = 64ULL * 1024ULL;
inline constexpr uint64_t OS_ABI_THREAD_STACK_ALIGNMENT_BYTES = 16ULL;
inline constexpr uint64_t OS_ABI_THREAD_ENTRY_STACK_REMAINDER_BYTES = 8ULL;
inline constexpr uint64_t OS_ABI_THREAD_LOCAL_STORAGE_ALIGNMENT_BYTES = 16ULL;
inline constexpr uint64_t OS_ABI_THREAD_WAIT_ANY_COUNT = UINT64_MAX;
inline constexpr uint64_t OS_ABI_PRIVATE_FUTEX_WORD_SIZE_BYTES = sizeof(uint32_t);

using UserThreadEntry = void (*)(uint64_t argument) noexcept;

struct ThreadCreateRequest final {
    uint64_t entry_address;
    uint64_t argument;
    uint64_t stack_base_address;
    uint64_t stack_size_bytes;
    uint64_t stack_pointer;
    uint64_t thread_local_storage_base;
};

static_assert(sizeof(ThreadCreateRequest) == OS_ABI_THREAD_CREATE_REQUEST_SIZE_BYTES);

struct ThreadJoinResult final {
    uint64_t thread_id;
    uint64_t exit_value;
};

static_assert(sizeof(ThreadJoinResult) == OS_ABI_THREAD_JOIN_RESULT_SIZE_BYTES);

}
