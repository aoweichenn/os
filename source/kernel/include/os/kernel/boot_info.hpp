#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_BOOT_INFO_ABI_SIZE_BYTES = 104ULL;

struct BootInfo final {
    uint64_t magic;
    uint64_t version;
    uint64_t structure_size_bytes;
    uint64_t kernel_file_physical_address;
    uint64_t kernel_file_size_bytes;
    uint64_t kernel_entry_address;
    uint64_t kernel_load_segment_count;
    uint64_t page_table_root_physical_address;
    uint64_t identity_mapped_size_bytes;
    uint64_t kernel_stack_top_physical_address;
    uint64_t physical_memory_map_address;
    uint64_t physical_memory_map_entry_count;
    uint64_t physical_memory_map_entry_size_bytes;
};

enum class BootInfoValidationStatus : uint64_t {
    Succeeded,
    NullPointer,
    InvalidMagic,
    UnsupportedVersion,
    InvalidStructureSize,
    InvalidKernelFileRange,
    InvalidKernelEntry,
    InvalidLoadSegmentCount,
    InvalidPageTableRoot,
    InvalidIdentityMapSize,
    InvalidKernelStack,
    InvalidPhysicalMemoryMap,
};

extern const uint64_t OS_KERNEL_BOOT_INFO_MAGIC;
extern const uint64_t OS_KERNEL_BOOT_INFO_VERSION;
extern const uint64_t OS_KERNEL_BOOT_INFO_STRUCTURE_SIZE_BYTES;
extern const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_FILE_PHYSICAL_ADDRESS;
extern const uint64_t OS_KERNEL_BOOT_INFO_MAXIMUM_KERNEL_FILE_SIZE_BYTES;
extern const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_ENTRY_ADDRESS;
extern const uint64_t OS_KERNEL_BOOT_INFO_MAXIMUM_LOAD_SEGMENT_COUNT;
extern const uint64_t OS_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT_PHYSICAL_ADDRESS;
extern const uint64_t OS_KERNEL_BOOT_INFO_IDENTITY_MAPPED_SIZE_BYTES;
extern const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_STACK_TOP_PHYSICAL_ADDRESS;
extern const uint64_t OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ADDRESS;
extern const uint64_t OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES;
extern const uint64_t OS_KERNEL_BOOT_INFO_MAXIMUM_PHYSICAL_MEMORY_MAP_ENTRY_COUNT;

[[nodiscard]] BootInfoValidationStatus ValidateBootInfo(const BootInfo *boot_info) noexcept;

static_assert(sizeof(BootInfo) == OS_KERNEL_BOOT_INFO_ABI_SIZE_BYTES);

}
