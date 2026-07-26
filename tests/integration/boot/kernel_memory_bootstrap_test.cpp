#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_MEMORY_BOOTSTRAP_SUITE_NAME =
    "boot/kernel_memory_bootstrap/integration";
constexpr std::string_view OS_TEST_MEMORY_BOOTSTRAP_INITIALIZE =
    "QEMU 内存图必须建立 64 MiB 管理范围";
constexpr std::string_view OS_TEST_MEMORY_BOOTSTRAP_RESERVATIONS =
    "低端平台、内核和初始栈保留后统计必须准确";
constexpr std::string_view OS_TEST_MEMORY_BOOTSTRAP_FIRST_FRAME =
    "首次分配不能覆盖任何启动关键区域";
constexpr std::string_view OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_CAPACITY =
    "64 GiB 带物理地址洞的内存图必须全部纳入页帧管理";
constexpr std::string_view OS_TEST_MEMORY_BOOTSTRAP_HIGH_FRAME =
    "64 GiB 规格必须能够分配并回收 4 GiB 以上页帧";

constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_MANAGED_SIZE_BYTES = 64ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PAGE_COUNT =
    OS_TEST_MEMORY_BOOTSTRAP_MANAGED_SIZE_BYTES / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_STORAGE_STATES_PER_BYTE = 4ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_STATE_STORAGE_SIZE_BYTES =
    OS_TEST_MEMORY_BOOTSTRAP_PAGE_COUNT / OS_TEST_MEMORY_BOOTSTRAP_STORAGE_STATES_PER_BYTE;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_LOW_RESERVED_SIZE_BYTES = 1ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_KERNEL_BEGIN = 1ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_KERNEL_SIZE_BYTES = 1ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_STACK_BEGIN = 0x0000000003FEF000ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_STACK_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_HIGH_RESERVED_BASE = 0x000000FD00000000ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_HIGH_RESERVED_LENGTH = 0x0000000300000000ULL;
constexpr uint32_t OS_TEST_MEMORY_BOOTSTRAP_RESERVED_TYPE = 2U;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_MEMORY_MAP_ENTRY_COUNT = 2ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_LOW_RESERVED_FRAME_COUNT =
    OS_TEST_MEMORY_BOOTSTRAP_LOW_RESERVED_SIZE_BYTES / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_KERNEL_FRAME_COUNT =
    OS_TEST_MEMORY_BOOTSTRAP_KERNEL_SIZE_BYTES / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_STACK_FRAME_COUNT =
    OS_TEST_MEMORY_BOOTSTRAP_STACK_SIZE_BYTES / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_RESERVED_FRAME_COUNT =
    OS_TEST_MEMORY_BOOTSTRAP_LOW_RESERVED_FRAME_COUNT +
    OS_TEST_MEMORY_BOOTSTRAP_KERNEL_FRAME_COUNT + OS_TEST_MEMORY_BOOTSTRAP_STACK_FRAME_COUNT;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_FREE_FRAME_COUNT =
    OS_TEST_MEMORY_BOOTSTRAP_PAGE_COUNT - OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_RESERVED_FRAME_COUNT;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_GIBIBYTE_SIZE_BYTES = 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_USABLE_SIZE_BYTES =
    64ULL * OS_TEST_MEMORY_BOOTSTRAP_GIBIBYTE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_LOW_USABLE_SIZE_BYTES =
    3ULL * OS_TEST_MEMORY_BOOTSTRAP_GIBIBYTE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HOLE_SIZE_BYTES =
    OS_TEST_MEMORY_BOOTSTRAP_GIBIBYTE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_BEGIN =
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_LOW_USABLE_SIZE_BYTES +
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HOLE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_SIZE_BYTES =
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_USABLE_SIZE_BYTES -
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_LOW_USABLE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MANAGED_LIMIT =
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_BEGIN +
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_PAGE_COUNT =
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MANAGED_LIMIT / os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_STATE_STORAGE_SIZE_BYTES =
    (OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_PAGE_COUNT +
     OS_TEST_MEMORY_BOOTSTRAP_STORAGE_STATES_PER_BYTE - 1ULL) /
    OS_TEST_MEMORY_BOOTSTRAP_STORAGE_STATES_PER_BYTE;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_USABLE_PAGE_COUNT =
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_USABLE_SIZE_BYTES /
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_TEST_BEGIN =
    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_BEGIN +
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MEMORY_MAP_ENTRY_COUNT = 3ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_MEMORY_BOOTSTRAP_SUITE_NAME};
    uint8_t state_storage[OS_TEST_MEMORY_BOOTSTRAP_STATE_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator allocator{
        state_storage,
        OS_TEST_MEMORY_BOOTSTRAP_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry qemu_memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_MEMORY_BOOTSTRAP_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .base_address = OS_TEST_MEMORY_BOOTSTRAP_HIGH_RESERVED_BASE,
            .length_bytes = OS_TEST_MEMORY_BOOTSTRAP_HIGH_RESERVED_LENGTH,
            .type = OS_TEST_MEMORY_BOOTSTRAP_RESERVED_TYPE,
            .attributes = 0U,
        },
    };
    test_context.Expect(allocator.Initialize(qemu_memory_map,
                                             OS_TEST_MEMORY_BOOTSTRAP_MEMORY_MAP_ENTRY_COUNT,
                                             OS_TEST_MEMORY_BOOTSTRAP_MANAGED_SIZE_BYTES) ==
                            os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                        OS_TEST_MEMORY_BOOTSTRAP_INITIALIZE);

    const bool reservations_succeeded =
        allocator.ReserveRange(0ULL, OS_TEST_MEMORY_BOOTSTRAP_LOW_RESERVED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReserveRange(OS_TEST_MEMORY_BOOTSTRAP_KERNEL_BEGIN,
                               OS_TEST_MEMORY_BOOTSTRAP_KERNEL_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReserveRange(OS_TEST_MEMORY_BOOTSTRAP_STACK_BEGIN,
                               OS_TEST_MEMORY_BOOTSTRAP_STACK_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics statistics = allocator.Statistics();
    test_context.Expect(reservations_succeeded &&
                            statistics.reserved_frame_count ==
                                OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_RESERVED_FRAME_COUNT &&
                            statistics.free_frame_count ==
                                OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_FREE_FRAME_COUNT,
                        OS_TEST_MEMORY_BOOTSTRAP_RESERVATIONS);

    os::kernel::PhysicalFrame first_frame{};
    test_context.Expect(
        allocator.Allocate(first_frame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            first_frame.physical_address ==
                OS_TEST_MEMORY_BOOTSTRAP_KERNEL_BEGIN + OS_TEST_MEMORY_BOOTSTRAP_KERNEL_SIZE_BYTES,
        OS_TEST_MEMORY_BOOTSTRAP_FIRST_FRAME);

    static uint8_t
        primary_state_storage[OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_STATE_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator primary_allocator{
        primary_state_storage,
        OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry primary_memory_map[] = {
        {
            .base_address = 0ULL,
            .length_bytes = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_LOW_USABLE_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .base_address = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_LOW_USABLE_SIZE_BYTES,
            .length_bytes = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HOLE_SIZE_BYTES,
            .type = OS_TEST_MEMORY_BOOTSTRAP_RESERVED_TYPE,
            .attributes = 0U,
        },
        {
            .base_address = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_BEGIN,
            .length_bytes = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    const bool primary_initialized =
        primary_allocator.Initialize(primary_memory_map,
                                     OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MEMORY_MAP_ENTRY_COUNT,
                                     OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MANAGED_LIMIT) ==
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics primary_statistics =
        primary_allocator.Statistics();
    test_context.Expect(primary_initialized &&
                            primary_statistics.free_frame_count ==
                                OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_USABLE_PAGE_COUNT &&
                            primary_statistics.managed_frame_count ==
                                OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_PAGE_COUNT,
                        OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_CAPACITY);

    os::kernel::PhysicalFrame primary_high_frame{};
    test_context.Expect(
        primary_initialized &&
            primary_allocator.AllocateInRange(OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_TEST_BEGIN,
                                              OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MANAGED_LIMIT,
                                              primary_high_frame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            primary_high_frame.physical_address >=
                OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_TEST_BEGIN &&
            primary_allocator.Release(primary_high_frame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
        OS_TEST_MEMORY_BOOTSTRAP_HIGH_FRAME);

    return test_context.ExitCode();
}
