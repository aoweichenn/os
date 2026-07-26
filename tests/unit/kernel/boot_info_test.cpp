#include "os/kernel/boot/boot_info.hpp"
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

os::kernel::BootInfo CreateValidBootInfo() {
    return os::kernel::BootInfo{
        .magic = os::kernel::OS_KERNEL_BOOT_INFO_MAGIC,
        .version = os::kernel::OS_KERNEL_BOOT_INFO_VERSION,
        .structure_size_bytes = os::kernel::OS_KERNEL_BOOT_INFO_STRUCTURE_SIZE_BYTES,
        .kernel_file_physical_address =
            os::kernel::OS_KERNEL_BOOT_INFO_KERNEL_FILE_PHYSICAL_ADDRESS,
        .kernel_file_size_bytes = OS_TEST_KERNEL_BOOT_INFO_VALID_FILE_SIZE_BYTES,
        .kernel_entry_address = os::kernel::OS_KERNEL_BOOT_INFO_KERNEL_ENTRY_ADDRESS,
        .kernel_load_segment_count = OS_TEST_KERNEL_BOOT_INFO_VALID_SEGMENT_COUNT,
        .page_table_root_physical_address =
            os::kernel::OS_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT_PHYSICAL_ADDRESS,
        .identity_mapped_size_bytes = os::kernel::OS_KERNEL_BOOT_INFO_IDENTITY_MAPPED_SIZE_BYTES,
        .kernel_stack_top_physical_address =
            os::kernel::OS_KERNEL_BOOT_INFO_KERNEL_STACK_TOP_PHYSICAL_ADDRESS,
        .physical_memory_map_address = os::kernel::OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ADDRESS,
        .physical_memory_map_entry_count = OS_TEST_KERNEL_BOOT_INFO_VALID_MEMORY_MAP_ENTRY_COUNT,
        .physical_memory_map_entry_size_bytes =
            os::kernel::OS_KERNEL_BOOT_INFO_PHYSICAL_MEMORY_MAP_ENTRY_SIZE_BYTES,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_KERNEL_BOOT_INFO_SUITE_NAME};

    os::kernel::BootInfo boot_info = CreateValidBootInfo();
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::Succeeded,
                        OS_TEST_KERNEL_BOOT_INFO_VALID);
    test_context.Expect(os::kernel::ValidateBootInfo(nullptr) ==
                            os::kernel::BootInfoValidationStatus::NullPointer,
                        OS_TEST_KERNEL_BOOT_INFO_NULL);

    boot_info = CreateValidBootInfo();
    boot_info.magic = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidMagic,
                        OS_TEST_KERNEL_BOOT_INFO_MAGIC);

    boot_info = CreateValidBootInfo();
    boot_info.version = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::UnsupportedVersion,
                        OS_TEST_KERNEL_BOOT_INFO_VERSION);

    boot_info = CreateValidBootInfo();
    boot_info.structure_size_bytes = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidStructureSize,
                        OS_TEST_KERNEL_BOOT_INFO_SIZE);

    boot_info = CreateValidBootInfo();
    boot_info.kernel_file_size_bytes = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidKernelFileRange,
                        OS_TEST_KERNEL_BOOT_INFO_FILE_RANGE);

    boot_info = CreateValidBootInfo();
    boot_info.kernel_file_size_bytes =
        os::kernel::OS_KERNEL_BOOT_INFO_MAXIMUM_KERNEL_FILE_SIZE_BYTES +
        OS_TEST_KERNEL_BOOT_INFO_LIMIT_EXCESS;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidKernelFileRange,
                        OS_TEST_KERNEL_BOOT_INFO_FILE_TOO_LARGE);

    boot_info = CreateValidBootInfo();
    boot_info.kernel_entry_address = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidKernelEntry,
                        OS_TEST_KERNEL_BOOT_INFO_ENTRY);

    boot_info = CreateValidBootInfo();
    boot_info.kernel_load_segment_count = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidLoadSegmentCount,
                        OS_TEST_KERNEL_BOOT_INFO_SEGMENT_COUNT);

    boot_info = CreateValidBootInfo();
    boot_info.kernel_load_segment_count =
        os::kernel::OS_KERNEL_BOOT_INFO_MAXIMUM_LOAD_SEGMENT_COUNT +
        OS_TEST_KERNEL_BOOT_INFO_LIMIT_EXCESS;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidLoadSegmentCount,
                        OS_TEST_KERNEL_BOOT_INFO_TOO_MANY_SEGMENTS);

    boot_info = CreateValidBootInfo();
    boot_info.page_table_root_physical_address = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidPageTableRoot,
                        OS_TEST_KERNEL_BOOT_INFO_PAGE_TABLE_ROOT);

    boot_info = CreateValidBootInfo();
    boot_info.identity_mapped_size_bytes = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidIdentityMapSize,
                        OS_TEST_KERNEL_BOOT_INFO_MAP_SIZE);

    boot_info = CreateValidBootInfo();
    boot_info.kernel_stack_top_physical_address = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidKernelStack,
                        OS_TEST_KERNEL_BOOT_INFO_STACK);

    boot_info = CreateValidBootInfo();
    boot_info.physical_memory_map_address = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidPhysicalMemoryMap,
                        OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_ADDRESS);

    boot_info = CreateValidBootInfo();
    boot_info.physical_memory_map_entry_count = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidPhysicalMemoryMap,
                        OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_COUNT);

    boot_info = CreateValidBootInfo();
    boot_info.physical_memory_map_entry_count =
        os::kernel::OS_KERNEL_BOOT_INFO_MAXIMUM_PHYSICAL_MEMORY_MAP_ENTRY_COUNT +
        OS_TEST_KERNEL_BOOT_INFO_LIMIT_EXCESS;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidPhysicalMemoryMap,
                        OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_TOO_LARGE);

    boot_info = CreateValidBootInfo();
    boot_info.physical_memory_map_entry_size_bytes = OS_TEST_KERNEL_BOOT_INFO_INVALID_VALUE;
    test_context.Expect(os::kernel::ValidateBootInfo(&boot_info) ==
                            os::kernel::BootInfoValidationStatus::InvalidPhysicalMemoryMap,
                        OS_TEST_KERNEL_BOOT_INFO_MEMORY_MAP_ENTRY_SIZE);

    return test_context.ExitCode();
}
