#pragma once

#include <stdint.h>

namespace os::abi {

inline constexpr uint64_t OS_ABI_MEMORY_PAGE_SIZE_BYTES = 4096ULL;
inline constexpr uint64_t OS_ABI_MEMORY_PROTECTION_READ = 1ULL << 0ULL;
inline constexpr uint64_t OS_ABI_MEMORY_PROTECTION_WRITE = 1ULL << 1ULL;
inline constexpr uint64_t OS_ABI_MEMORY_PROTECTION_EXECUTE = 1ULL << 2ULL;
inline constexpr uint64_t OS_ABI_MEMORY_PROTECTION_VALID_MASK = OS_ABI_MEMORY_PROTECTION_READ |
                                                                OS_ABI_MEMORY_PROTECTION_WRITE |
                                                                OS_ABI_MEMORY_PROTECTION_EXECUTE;
inline constexpr uint64_t OS_ABI_MEMORY_MAP_FIXED = 1ULL << 0ULL;
inline constexpr uint64_t OS_ABI_MEMORY_MAP_PRIVATE = 1ULL << 1ULL;
inline constexpr uint64_t OS_ABI_MEMORY_MAP_SHARED = 1ULL << 2ULL;
inline constexpr uint64_t OS_ABI_MEMORY_MAP_NO_FLAGS = 0ULL;
inline constexpr uint64_t OS_ABI_ANONYMOUS_MEMORY_MAP_VALID_FLAG_MASK =
    OS_ABI_MEMORY_MAP_FIXED;
inline constexpr uint64_t OS_ABI_FILE_MEMORY_MAP_VALID_FLAG_MASK =
    OS_ABI_MEMORY_MAP_FIXED | OS_ABI_MEMORY_MAP_PRIVATE |
    OS_ABI_MEMORY_MAP_SHARED;
inline constexpr uint64_t OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS = 0ULL;

// 匿名映射只开放进程低地址 1 GiB 分支中的 512 MiB 专用窗口。
inline constexpr uint64_t OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS = 0x0000000060000000ULL;
inline constexpr uint64_t OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS = 0x0000000080000000ULL;
inline constexpr uint64_t OS_ABI_USER_PROGRAM_BREAK_LIMIT_ADDRESS =
    OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS;

// 栈顶保持稳定，向下预留 8 MiB；再下一页永久没有 VMA，构成 guard page。
inline constexpr uint64_t OS_ABI_USER_STACK_TOP_ADDRESS = 0x00007FFFFFFF0000ULL;
inline constexpr uint64_t OS_ABI_USER_STACK_MAXIMUM_SIZE_BYTES = 8ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t OS_ABI_USER_STACK_BOTTOM_ADDRESS =
    OS_ABI_USER_STACK_TOP_ADDRESS - OS_ABI_USER_STACK_MAXIMUM_SIZE_BYTES;
inline constexpr uint64_t OS_ABI_USER_STACK_GUARD_ADDRESS =
    OS_ABI_USER_STACK_BOTTOM_ADDRESS - OS_ABI_MEMORY_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_ABI_USER_HEAP_MAXIMUM_SIZE_BYTES = 8ULL * 1024ULL * 1024ULL;

inline constexpr uint64_t OS_ABI_FILE_MEMORY_MAP_REQUEST_SIZE_BYTES = 48ULL;

struct FileMemoryMapRequest final {
    uint64_t requested_address;
    uint64_t length_bytes;
    uint64_t protection_flags;
    uint64_t map_flags;
    uint64_t file_descriptor;
    uint64_t file_offset_bytes;
};

static_assert(sizeof(FileMemoryMapRequest) ==
              OS_ABI_FILE_MEMORY_MAP_REQUEST_SIZE_BYTES);

inline constexpr uint64_t OS_ABI_VIRTUAL_MEMORY_STATISTICS_SIZE_BYTES = 160ULL;

struct VirtualMemoryStatistics final {
    uint64_t area_count;
    uint64_t virtual_page_count;
    uint64_t resident_page_count;
    uint64_t peak_resident_page_count;
    uint64_t executable_image_page_count;
    uint64_t anonymous_page_count;
    uint64_t program_break_page_count;
    uint64_t stack_reserved_page_count;
    uint64_t stack_resident_page_count;
    uint64_t demand_page_fault_count;
    uint64_t stack_growth_page_fault_count;
    uint64_t unmap_released_page_count;
    uint64_t page_table_reclaimed_frame_count;
    uint64_t program_break_address;
    uint64_t file_private_page_count;
    uint64_t file_shared_page_count;
    uint64_t file_page_fault_count;
    uint64_t page_cache_hit_count;
    uint64_t private_file_resident_page_count;
    uint64_t shared_file_resident_page_count;
};

static_assert(sizeof(VirtualMemoryStatistics) == OS_ABI_VIRTUAL_MEMORY_STATISTICS_SIZE_BYTES);

}
