#include "os/kernel/boot_info.hpp"

namespace os::kernel {

const uint64_t OS_KERNEL_BOOT_INFO_MAGIC = 0x3436544F4F42534FULL;
const uint64_t OS_KERNEL_BOOT_INFO_VERSION = 1ULL;
const uint64_t OS_KERNEL_BOOT_INFO_STRUCTURE_SIZE_BYTES = OS_KERNEL_BOOT_INFO_ABI_SIZE_BYTES;
const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_FILE_PHYSICAL_ADDRESS = 0x0000000000020000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_MAXIMUM_KERNEL_FILE_SIZE_BYTES = 0x0000000000080000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_ENTRY_ADDRESS = 0x0000000000100000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_MAXIMUM_LOAD_SEGMENT_COUNT = 64ULL;
const uint64_t OS_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT_PHYSICAL_ADDRESS = 0x0000000000010000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_IDENTITY_MAPPED_SIZE_BYTES = 0x0000000004000000ULL;
const uint64_t OS_KERNEL_BOOT_INFO_KERNEL_STACK_TOP_PHYSICAL_ADDRESS = 0x0000000003FFF000ULL;

BootInfoValidationStatus validateBootInfo(const BootInfo *bootInfo) noexcept {
    if (bootInfo == nullptr) {
        return BootInfoValidationStatus::NullPointer;
    }
    if (bootInfo->magic != OS_KERNEL_BOOT_INFO_MAGIC) {
        return BootInfoValidationStatus::InvalidMagic;
    }
    if (bootInfo->version != OS_KERNEL_BOOT_INFO_VERSION) {
        return BootInfoValidationStatus::UnsupportedVersion;
    }
    if (bootInfo->structureSizeBytes != OS_KERNEL_BOOT_INFO_STRUCTURE_SIZE_BYTES) {
        return BootInfoValidationStatus::InvalidStructureSize;
    }
    if (bootInfo->kernelFilePhysicalAddress != OS_KERNEL_BOOT_INFO_KERNEL_FILE_PHYSICAL_ADDRESS ||
        bootInfo->kernelFileSizeBytes == 0ULL ||
        bootInfo->kernelFileSizeBytes > OS_KERNEL_BOOT_INFO_MAXIMUM_KERNEL_FILE_SIZE_BYTES) {
        return BootInfoValidationStatus::InvalidKernelFileRange;
    }
    if (bootInfo->kernelEntryAddress != OS_KERNEL_BOOT_INFO_KERNEL_ENTRY_ADDRESS) {
        return BootInfoValidationStatus::InvalidKernelEntry;
    }
    if (bootInfo->kernelLoadSegmentCount == 0ULL ||
        bootInfo->kernelLoadSegmentCount > OS_KERNEL_BOOT_INFO_MAXIMUM_LOAD_SEGMENT_COUNT) {
        return BootInfoValidationStatus::InvalidLoadSegmentCount;
    }
    if (bootInfo->pageTableRootPhysicalAddress !=
        OS_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT_PHYSICAL_ADDRESS) {
        return BootInfoValidationStatus::InvalidPageTableRoot;
    }
    if (bootInfo->identityMappedSizeBytes != OS_KERNEL_BOOT_INFO_IDENTITY_MAPPED_SIZE_BYTES) {
        return BootInfoValidationStatus::InvalidIdentityMapSize;
    }
    if (bootInfo->kernelStackTopPhysicalAddress !=
        OS_KERNEL_BOOT_INFO_KERNEL_STACK_TOP_PHYSICAL_ADDRESS) {
        return BootInfoValidationStatus::InvalidKernelStack;
    }
    return BootInfoValidationStatus::Succeeded;
}

}
