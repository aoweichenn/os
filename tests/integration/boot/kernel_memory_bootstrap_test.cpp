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
    testContext.expect(allocator.initialize(qemuMemoryMap,
                                            OS_TEST_MEMORY_BOOTSTRAP_MEMORY_MAP_ENTRY_COUNT,
                                            OS_TEST_MEMORY_BOOTSTRAP_MANAGED_SIZE_BYTES) ==
                           os::kernel::PhysicalFrameAllocatorStatus::Succeeded,
                       OS_TEST_MEMORY_BOOTSTRAP_INITIALIZE);

    const bool reservationsSucceeded =
        allocator.reserveRange(0ULL, OS_TEST_MEMORY_BOOTSTRAP_LOW_RESERVED_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.reserveRange(OS_TEST_MEMORY_BOOTSTRAP_KERNEL_BEGIN,
                               OS_TEST_MEMORY_BOOTSTRAP_KERNEL_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
        allocator.reserveRange(OS_TEST_MEMORY_BOOTSTRAP_STACK_BEGIN,
                               OS_TEST_MEMORY_BOOTSTRAP_STACK_SIZE_BYTES) ==
            os::kernel::PhysicalFrameAllocatorStatus::Succeeded;
    const os::kernel::PhysicalFrameAllocatorStatistics statistics = allocator.statistics();
    testContext.expect(reservationsSucceeded &&
                           statistics.reservedFrameCount ==
                               OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_RESERVED_FRAME_COUNT &&
                           statistics.freeFrameCount ==
                               OS_TEST_MEMORY_BOOTSTRAP_EXPECTED_FREE_FRAME_COUNT,
                       OS_TEST_MEMORY_BOOTSTRAP_RESERVATIONS);

    os::kernel::PhysicalFrame firstFrame{};
    testContext.expect(
        allocator.allocate(firstFrame) == os::kernel::PhysicalFrameAllocatorStatus::Succeeded &&
            firstFrame.physicalAddress ==
                OS_TEST_MEMORY_BOOTSTRAP_KERNEL_BEGIN + OS_TEST_MEMORY_BOOTSTRAP_KERNEL_SIZE_BYTES,
        OS_TEST_MEMORY_BOOTSTRAP_FIRST_FRAME);

    return testContext.exitCode();
}
