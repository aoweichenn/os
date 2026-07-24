#include "os/kernel/boot_info.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_SUITE_NAME = "kernel/boot_info/unit";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_VALID = "完整 BootInfo 应通过验证";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_NULL = "空 BootInfo 指针必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_MAGIC = "错误 magic 必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_VERSION = "未知版本必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_SIZE = "错误结构长度必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_FILE_RANGE = "错误内核文件范围必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_FILE_TOO_LARGE =
    "超出暂存区的内核文件必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_ENTRY = "错误内核入口必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_SEGMENT_COUNT = "空加载段集合必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_TOO_MANY_SEGMENTS =
    "超出上限的加载段集合必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT = "错误页表根地址必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_MAP_SIZE = "错误身份映射范围必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_STACK = "错误内核栈地址必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_ADDRESS =
    "错误物理内存图地址必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_COUNT = "空物理内存图必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_TOO_LARGE =
    "超出容量的物理内存图必须被拒绝";
constexpr std::string_view OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_ENTRY_SIZE =
    "错误物理内存图条目宽度必须被拒绝";
constexpr uint64_t OS_TEST_KERNEL_BOOT_INFO_VALID_FILE_SIZE_BYTES = 0x0000000000004000ULL;
constexpr uint64_t OS_TEST_KERNEL_BOOT_INFO_VALID_SEGMENT_COUNT = 3ULL;
constexpr uint64_t OS_TEST_KERNEL_BOOT_INFO_VALID_MEMORY_MAP_ENTRY_COUNT = 4ULL;
constexpr uint64_t OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_BOOT_INFO_LIMIT_EXCESS = 1ULL;

os::kernel::BootInfo createValidBootInfo() {
    return os::kernel::BootInfo{
        .magic = os::kernel::OS_KERNEL_BOOT_INFO_MAGIC,
        .version = os::kernel::OS_KERNEL_BOOT_INFO_VERSION,
        .structureSizeBytes = os::kernel::OS_KERNEL_BOOT_INFO_STRUCTURE_SIZE_BYTES,
        .kernelFilePhysicalAddress = os::kernel::OS_KERNEL_BOOT_INFO_KERNEL_FILE_PHYSICAL_ADDRESS,
        .kernelFileSizeBytes = OS_TEST_KERNEL_BOOT_INFO_VALID_FILE_SIZE_BYTES,
        .kernelEntryAddress = os::kernel::OS_KERNEL_BOOT_INFO_KERNEL_ENTRY_ADDRESS,
        .kernelLoadSegmentCount = OS_TEST_KERNEL_BOOT_INFO_VALID_SEGMENT_COUNT,
        .pageTableRootPhysicalAddress =
            os::kernel::OS_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT_PHYSICAL_ADDRESS,
        .identityMappedSizeBytes = os::kernel::OS_KERNEL_BOOT_INFO_IDENTITY_MAPPED_SIZE_BYTES,
        .kernelStackTopPhysicalAddress =
            os::kernel::OS_KERNEL_BOOT_INFO_KERNEL_STACK_TOP_PHYSICAL_ADDRESS,
        .physicalMemoryMapAddress = os::kernel::OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ADDRESS,
        .physicalMemoryMapEntryCount = OS_TEST_KERNEL_BOOT_INFO_VALID_MEMORY_MAP_ENTRY_COUNT,
        .physicalMemoryMapEntrySizeBytes =
            os::kernel::OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES,
    };
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_KERNEL_BOOT_INFO_SUITE_NAME};

    os::kernel::BootInfo bootInfo = createValidBootInfo();
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::Succeeded,
                       OS_TEST_KERNEL_BOOT_INFO_VALID);
    testContext.expect(os::kernel::validateBootInfo(nullptr) ==
                           os::kernel::BootInfoValidationStatus::NullPointer,
                       OS_TEST_KERNEL_BOOT_INFO_NULL);

    bootInfo = createValidBootInfo();
    bootInfo.magic = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidMagic,
                       OS_TEST_KERNEL_BOOT_INFO_MAGIC);

    bootInfo = createValidBootInfo();
    bootInfo.version = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::UnsupportedVersion,
                       OS_TEST_KERNEL_BOOT_INFO_VERSION);

    bootInfo = createValidBootInfo();
    bootInfo.structureSizeBytes = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidStructureSize,
                       OS_TEST_KERNEL_BOOT_INFO_SIZE);

    bootInfo = createValidBootInfo();
    bootInfo.kernelFileSizeBytes = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidKernelFileRange,
                       OS_TEST_KERNEL_BOOT_INFO_FILE_RANGE);

    bootInfo = createValidBootInfo();
    bootInfo.kernelFileSizeBytes = os::kernel::OS_KERNEL_BOOT_INFO_MAXIMUM_KERNEL_FILE_SIZE_BYTES +
                                   OS_TEST_KERNEL_BOOT_INFO_LIMIT_EXCESS;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidKernelFileRange,
                       OS_TEST_KERNEL_BOOT_INFO_FILE_TOO_LARGE);

    bootInfo = createValidBootInfo();
    bootInfo.kernelEntryAddress = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidKernelEntry,
                       OS_TEST_KERNEL_BOOT_INFO_ENTRY);

    bootInfo = createValidBootInfo();
    bootInfo.kernelLoadSegmentCount = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidLoadSegmentCount,
                       OS_TEST_KERNEL_BOOT_INFO_SEGMENT_COUNT);

    bootInfo = createValidBootInfo();
    bootInfo.kernelLoadSegmentCount = os::kernel::OS_KERNEL_BOOT_INFO_MAXIMUM_LOAD_SEGMENT_COUNT +
                                      OS_TEST_KERNEL_BOOT_INFO_LIMIT_EXCESS;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidLoadSegmentCount,
                       OS_TEST_KERNEL_BOOT_INFO_TOO_MANY_SEGMENTS);

    bootInfo = createValidBootInfo();
    bootInfo.pageTableRootPhysicalAddress = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidPageTableRoot,
                       OS_TEST_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT);

    bootInfo = createValidBootInfo();
    bootInfo.identityMappedSizeBytes = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidIdentityMapSize,
                       OS_TEST_KERNEL_BOOT_INFO_MAP_SIZE);

    bootInfo = createValidBootInfo();
    bootInfo.kernelStackTopPhysicalAddress = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidKernelStack,
                       OS_TEST_KERNEL_BOOT_INFO_STACK);

    bootInfo = createValidBootInfo();
    bootInfo.physicalMemoryMapAddress = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidPhysicalMemoryMap,
                       OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_ADDRESS);

    bootInfo = createValidBootInfo();
    bootInfo.physicalMemoryMapEntryCount = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidPhysicalMemoryMap,
                       OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_COUNT);

    bootInfo = createValidBootInfo();
    bootInfo.physicalMemoryMapEntryCount =
        os::kernel::OS_KERNEL_BOOT_INFO_MAXIMUM_PHYSICAL_MEMORY_MAP_ENTRY_COUNT +
        OS_TEST_KERNEL_BOOT_INFO_LIMIT_EXCESS;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidPhysicalMemoryMap,
                       OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_TOO_LARGE);

    bootInfo = createValidBootInfo();
    bootInfo.physicalMemoryMapEntrySizeBytes = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    testContext.expect(os::kernel::validateBootInfo(&bootInfo) ==
                           os::kernel::BootInfoValidationStatus::InvalidPhysicalMemoryMap,
                       OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_ENTRY_SIZE);

    return testContext.exitCode();
}
