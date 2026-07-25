#include "os/kernel/physical_frame_allocator.hpp"
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
    os::test::TestContext testContext{OS_TEST_MEMORY_BOOTSTRAP_SUITE_NAME};
    uint8_t stateStorage[OS_TEST_MEMORY_BOOTSTRAP_STATE_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator allocator{
        stateStorage,
        OS_TEST_MEMORY_BOOTSTRAP_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry qemuMemoryMap[] = {
        {
            .baseAddress = 0ULL,
            .lengthBytes = OS_TEST_MEMORY_BOOTSTRAP_MANAGED_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .baseAddress = OS_TEST_MEMORY_BOOTSTRAP_HIGH_RESERVED_BASE,
            .lengthBytes = OS_TEST_MEMORY_BOOTSTRAP_HIGH_RESERVED_LENGTH,
            .type = OS_TEST_MEMORY_BOOTSTRAP_RESERVED_TYPE,
            .attributes = 0U,
        },
    };
    testContext.Expect(allocator.Initialize(qemuMemoryMap,
                                            OS_TEST_MEMORY_BOOTSTRAP_MEMORY_MAP_ENTRY_COUNT,
                                            OS_TEST_MEMORY_BOOTSTRAP_MANAGED_SIZE_BYTES) ==
                           os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                       OS_TEST_MEMORY_BOOTSTRAP_INITIALIZE);

    const bool reservationsSucceeded =
        allocator.ReserveRange(0ULL, OS_TEST_MEMORY_BOOTSTRAP_LOW_RESERVED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReserveRange(OS_TEST_MEMORY_BOOTSTRAP_KERNEL_BEGIN,
                               OS_TEST_MEMORY_BOOTSTRAP_KERNEL_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.ReserveRange(OS_TEST_MEMORY_BOOTSTRAP_STACK_BEGIN,
                               OS_TEST_MEMORY_BOOTSTRAP_STACK_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics statistics = allocator.Statistics();
    testContext.Expect(reservationsSucceeded &&
                           statistics.reservedFrameCount ==
                               OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_RESERVED_FRAME_COUNT &&
                           statistics.freeFrameCount ==
                               OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_FREE_FRAME_COUNT,
                       OS_TEST_MEMORY_BOOTSTRAP_RESERVATIONS);

    os::kernel::PhysicalFrame firstFrame{};
    testContext.Expect(
        allocator.Allocate(firstFrame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            firstFrame.physicalAddress ==
                OS_TEST_MEMORY_BOOTSTRAP_KERNEL_BEGIN + OS_TEST_MEMORY_BOOTSTRAP_KERNEL_SIZE_BYTES,
        OS_TEST_MEMORY_BOOTSTRAP_FIRST_FRAME);

    static uint8_t primaryStateStorage[OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_STATE_STORAGE_SIZE_BYTES]{};
    os::kernel::PhysicalFrameAllocator primaryAllocator{
        primaryStateStorage,
        OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_STATE_STORAGE_SIZE_BYTES,
    };
    const os::kernel::PhysicalMemoryMapEntry primaryMemoryMap[] = {
        {
            .baseAddress = 0ULL,
            .lengthBytes = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_LOW_USABLE_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
        {
            .baseAddress = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_LOW_USABLE_SIZE_BYTES,
            .lengthBytes = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HOLE_SIZE_BYTES,
            .type = OS_TEST_MEMORY_BOOTSTRAP_RESERVED_TYPE,
            .attributes = 0U,
        },
        {
            .baseAddress = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_BEGIN,
            .lengthBytes = OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_USABLE_SIZE_BYTES,
            .type = os::kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = 0U,
        },
    };
    const bool primaryInitialized =
        primaryAllocator.Initialize(primaryMemoryMap,
                                    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MEMORY_MAP_ENTRY_COUNT,
                                    OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MANAGED_LIMIT) ==
        os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics primaryStatistics =
        primaryAllocator.Statistics();
    testContext.Expect(primaryInitialized &&
                           primaryStatistics.freeFrameCount ==
                               OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_USABLE_PAGE_COUNT &&
                           primaryStatistics.managedFrameCount ==
                               OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_PAGE_COUNT,
                       OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_CAPACITY);

    os::kernel::PhysicalFrame primaryHighFrame{};
    testContext.Expect(
        primaryInitialized &&
            primaryAllocator.AllocateInRange(OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_TEST_BEGIN,
                                             OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_MANAGED_LIMIT,
                                             primaryHighFrame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            primaryHighFrame.physicalAddress >= OS_TEST_MEMORY_BOOTSTRAP_PRIMARY_HIGH_TEST_BEGIN &&
            primaryAllocator.Release(primaryHighFrame) ==
                os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
        OS_TEST_MEMORY_BOOTSTRAP_HIGH_FRAME);

    return testContext.ExitCode();
}
