#include "os/kernel/boot_info.hpp"

namespace os::kernel {

const uint64_t OS_KERNEL_BOOT_INFO_MAGIC = 0x3436544F4F42534FULL;
const uint64_t OS_KERNEL_BOOT_INFO_VERSION = 2ULL;
const uint64_t OS_KERNEL_BOOT_INFO_STRUCTURE_SIZE_BYTES = OS_KERNEL_BOOT_INFO_ABI_SIZE_BYTES;
const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_FILE_PHYSICAL_ADDRESS = 0x0000000003E00000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_MAXIMUM_KERNEL_FILE_SIZE_BYTES = 0x0000000000100000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_ENTRY_ADDRESS = 0x0000000000100000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_MAXIMUM_LOAD_SEGMENT_COUNT = 64ULL;
const uint64_t OS_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT_PHYSICAL_ADDRESS = 0x0000000000010000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_IDENTITY_MAPPED_SIZE_BYTES = 0x0000000004000000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_STACK_TOP_PHYSICAL_ADDRESS = 0x0000000003FFF000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ADDRESS = 0x0000000000018000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES = 24ULL;
const uint64_t OS_KERNEL_BOOT_INFO_MAXIMUM_PHYSICAL_MEMORY_MAP_ENTRY_COUNT = 128ULL;

BootInfoValidationStatus ValidateBootInfo(const BootInfo *boot_info) noexcept {
    if (boot_info == nullptr) {
        return BootInfoValidationStatus::NullPointer;
    }
    if (boot_info->magic != OS_KERNEL_BOOT_INFO_MAGIC) {
        return BootInfoValidationStatus::InvalidMagic;
    }
    if (boot_info->version != OS_KERNEL_BOOT_INFO_VERSION) {
        return BootInfoValidationStatus::UnsupportedVersion;
    }
    if (boot_info->structure_size_bytes != OS_KERNEL_BOOT_INFO_STRUCTURE_SIZE_BYTES) {
        return BootInfoValidationStatus::InvalidStructureSize;
    }
    if (boot_info->kernel_file_physical_address !=
            OS_KERNEL_BOOT_INFO_KERNEL_FILE_PHYSICAL_ADDRESS ||
        boot_info->kernel_file_size_bytes == 0ULL ||
        boot_info->kernel_file_size_bytes > OS_KERNEL_BOOT_INFO_MAXIMUM_KERNEL_FILE_SIZE_BYTES) {
        return BootInfoValidationStatus::InvalidKernelFileRange;
    }
    if (boot_info->kernel_entry_address != OS_KERNEL_BOOT_INFO_KERNEL_ENTRY_ADDRESS) {
        return BootInfoValidationStatus::InvalidKernelEntry;
    }
    if (boot_info->kernel_load_segment_count == 0ULL ||
        boot_info->kernel_load_segment_count > OS_KERNEL_BOOT_INFO_MAXIMUM_LOAD_SEGMENT_COUNT) {
        return BootInfoValidationStatus::InvalidLoadSegmentCount;
    }
    if (boot_info->page_table_root_physical_address !=
        OS_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT_PHYSICAL_ADDRESS) {
        return BootInfoValidationStatus::InvalidPageTableRoot;
    }
    if (boot_info->identity_mapped_size_bytes != OS_KERNEL_BOOT_INFO_IDENTITY_MAPPED_SIZE_BYTES) {
        return BootInfoValidationStatus::InvalidIdentityMapSize;
    }
    if (boot_info->kernel_stack_top_physical_address !=
        OS_KERNEL_BOOT_INFO_KERNEL_STACK_TOP_PHYSICAL_ADDRESS) {
        return BootInfoValidationStatus::InvalidKernelStack;
    }
    if (boot_info->physical_memory_map_address != OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ADDRESS ||
        boot_info->physical_memory_map_entry_count == 0ULL ||
        boot_info->physical_memory_map_entry_count >
            OS_KERNEL_BOOT_INFO_MAXIMUM_PHYSICAL_MEMORY_MAP_ENTRY_COUNT ||
        boot_info->physical_memory_map_entry_size_bytes !=
            OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES) {
        return BootInfoValidationStatus::InvalidPhysicalMemoryMap;
    }
    return BootInfoValidationStatus::Succeeded;
}
}
