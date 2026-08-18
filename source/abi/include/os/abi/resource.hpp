#pragma once

#include <stdint.h>

namespace os::abi {

// 编号与 Linux UAPI 的 RLIMIT_* 保持一致，便于移植用户态和测试模型。
enum class ResourceLimitKind : uint64_t {
    ProcessorTime = 0ULL,
    FileSize = 1ULL,
    Data = 2ULL,
    Stack = 3ULL,
    Core = 4ULL,
    ResidentSet = 5ULL,
    ProcessCount = 6ULL,
    OpenFileCount = 7ULL,
    LockedMemory = 8ULL,
    AddressSpace = 9ULL,
    FileLockCount = 10ULL,
    PendingSignalCount = 11ULL,
    MessageQueueBytes = 12ULL,
    Nice = 13ULL,
    RealtimePriority = 14ULL,
    RealtimeProcessorTime = 15ULL,
};

inline constexpr uint64_t OS_ABI_RESOURCE_LIMIT_KIND_COUNT = 16ULL;
inline constexpr uint64_t OS_ABI_RESOURCE_LIMIT_INFINITY = UINT64_MAX;
inline constexpr uint64_t OS_ABI_RESOURCE_LIMIT_SIZE_BYTES = 16ULL;
inline constexpr uint64_t OS_ABI_RESOURCE_LIMIT_DEFAULT_STACK_SIZE_BYTES = 8ULL * 1024ULL * 1024ULL;

struct ResourceLimit final {
    uint64_t current;
    uint64_t maximum;
};

static_assert(sizeof(ResourceLimit) == OS_ABI_RESOURCE_LIMIT_SIZE_BYTES);

}
