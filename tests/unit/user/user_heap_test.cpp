#include "os/user/user_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_USER_HEAP_SUITE_NAME = "user/user_heap/unit";
constexpr std::string_view OS_TEST_USER_HEAP_INITIALIZATION =
    "用户堆必须拒绝无效配置并从查询到的 break 开始";
constexpr std::string_view OS_TEST_USER_HEAP_ALLOCATE =
    "申请必须满足对齐、保留独立内容并更新有界统计";
constexpr std::string_view OS_TEST_USER_HEAP_REUSE = "释放后的空洞必须复用，邻接空闲块必须合并";
constexpr std::string_view OS_TEST_USER_HEAP_DIAGNOSTICS =
    "内部地址、重复释放和零字节申请必须明确失败";
constexpr std::string_view OS_TEST_USER_HEAP_FAILURE_ATOMIC =
    "brk 扩容失败和容量耗尽不得改变已有分配与堆容量";

constexpr uint64_t OS_TEST_USER_HEAP_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_USER_HEAP_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_USER_HEAP_EXPECTED_ACTIVE_ALLOCATION_COUNT = 2ULL;
constexpr uint64_t OS_TEST_USER_HEAP_EXPECTED_FREE_BLOCK_COUNT = OS_TEST_USER_HEAP_SINGLE_UNIT;
constexpr uint64_t OS_TEST_USER_HEAP_PAGE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_USER_HEAP_STORAGE_SIZE_BYTES = 256ULL * 1024ULL;
constexpr uint64_t OS_TEST_USER_HEAP_GROWTH_QUANTUM_BYTES = 16ULL * 1024ULL;
constexpr uint64_t OS_TEST_USER_HEAP_FIRST_SIZE_BYTES = 24ULL;
constexpr uint64_t OS_TEST_USER_HEAP_SECOND_SIZE_BYTES = 4097ULL;
constexpr uint64_t OS_TEST_USER_HEAP_REUSE_SIZE_BYTES = 16ULL;
constexpr uint64_t OS_TEST_USER_HEAP_FAILURE_SIZE_BYTES = 96ULL * 1024ULL;
constexpr uint64_t OS_TEST_USER_HEAP_ALIGNMENT_BYTES = 16ULL;
constexpr uint8_t OS_TEST_USER_HEAP_FIRST_PATTERN = 0x5AU;
constexpr uint8_t OS_TEST_USER_HEAP_SECOND_PATTERN = 0xA5U;
constexpr int64_t OS_TEST_USER_HEAP_BREAK_FAILURE = -1LL;

struct ProgramBreakContext final {
    uint8_t *base_address;
    uint64_t capacity_bytes;
    uint64_t current_size_bytes;
    bool fail_next_growth;
};

alignas(OS_TEST_USER_HEAP_PAGE_SIZE_BYTES) uint8_t
    heap_storage[OS_TEST_USER_HEAP_STORAGE_SIZE_BYTES]{};

[[nodiscard]] int64_t ProgramBreak(void *const context, const uint64_t requested_address) noexcept {
    if (context == nullptr) {
        return OS_TEST_USER_HEAP_BREAK_FAILURE;
    }
    ProgramBreakContext &break_context = *static_cast<ProgramBreakContext *>(context);
    const uint64_t base_address = reinterpret_cast<uint64_t>(break_context.base_address);
    if (requested_address == OS_TEST_USER_HEAP_EMPTY_VALUE) {
        return static_cast<int64_t>(base_address + break_context.current_size_bytes);
    }
    if (break_context.fail_next_growth) {
        break_context.fail_next_growth = false;
        return OS_TEST_USER_HEAP_BREAK_FAILURE;
    }
    if (requested_address < base_address ||
        requested_address > base_address + break_context.capacity_bytes) {
        return OS_TEST_USER_HEAP_BREAK_FAILURE;
    }
    break_context.current_size_bytes = requested_address - base_address;
    return static_cast<int64_t>(requested_address);
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_USER_HEAP_SUITE_NAME};
    ProgramBreakContext break_context{
        .base_address = heap_storage,
        .capacity_bytes = sizeof(heap_storage),
        .current_size_bytes = OS_TEST_USER_HEAP_EMPTY_VALUE,
        .fail_next_growth = false,
    };
    os::user::UserHeap heap{};
    os::user::UserHeap invalid_heap{};
    const bool initialization_valid =
        invalid_heap.Initialize(os::user::UserHeapConfiguration{
            .context = &break_context,
            .program_break_operation = nullptr,
            .maximum_capacity_bytes = OS_TEST_USER_HEAP_STORAGE_SIZE_BYTES,
            .page_size_bytes = OS_TEST_USER_HEAP_PAGE_SIZE_BYTES,
            .growth_quantum_bytes = OS_TEST_USER_HEAP_GROWTH_QUANTUM_BYTES,
        }) == os::user::UserHeapStatus::InvalidConfiguration &&
        heap.Initialize(os::user::UserHeapConfiguration{
            .context = &break_context,
            .program_break_operation = ProgramBreak,
            .maximum_capacity_bytes = OS_TEST_USER_HEAP_STORAGE_SIZE_BYTES,
            .page_size_bytes = OS_TEST_USER_HEAP_PAGE_SIZE_BYTES,
            .growth_quantum_bytes = OS_TEST_USER_HEAP_GROWTH_QUANTUM_BYTES,
        }) == os::user::UserHeapStatus::Succeeded &&
        heap.Validate() == os::user::UserHeapStatus::Succeeded &&
        heap.Statistics().capacity_bytes == OS_TEST_USER_HEAP_EMPTY_VALUE;
    test_context.Expect(initialization_valid, OS_TEST_USER_HEAP_INITIALIZATION);

    void *first_allocation = nullptr;
    void *second_allocation = nullptr;
    const bool allocations_created =
        heap.Allocate(OS_TEST_USER_HEAP_FIRST_SIZE_BYTES, first_allocation) ==
            os::user::UserHeapStatus::Succeeded &&
        heap.Allocate(OS_TEST_USER_HEAP_SECOND_SIZE_BYTES, second_allocation) ==
            os::user::UserHeapStatus::Succeeded;
    if (allocations_created) {
        uint8_t *const first_bytes = static_cast<uint8_t *>(first_allocation);
        uint8_t *const second_bytes = static_cast<uint8_t *>(second_allocation);
        first_bytes[OS_TEST_USER_HEAP_EMPTY_VALUE] = OS_TEST_USER_HEAP_FIRST_PATTERN;
        first_bytes[OS_TEST_USER_HEAP_FIRST_SIZE_BYTES - OS_TEST_USER_HEAP_SINGLE_UNIT] =
            OS_TEST_USER_HEAP_SECOND_PATTERN;
        second_bytes[OS_TEST_USER_HEAP_EMPTY_VALUE] = OS_TEST_USER_HEAP_SECOND_PATTERN;
        second_bytes[OS_TEST_USER_HEAP_SECOND_SIZE_BYTES - OS_TEST_USER_HEAP_SINGLE_UNIT] =
            OS_TEST_USER_HEAP_FIRST_PATTERN;
    }
    const os::user::UserHeapStatistics active_statistics = heap.Statistics();
    const bool allocation_valid =
        allocations_created && first_allocation != second_allocation &&
        reinterpret_cast<uint64_t>(first_allocation) % OS_TEST_USER_HEAP_ALIGNMENT_BYTES ==
            OS_TEST_USER_HEAP_EMPTY_VALUE &&
        reinterpret_cast<uint64_t>(second_allocation) % OS_TEST_USER_HEAP_ALIGNMENT_BYTES ==
            OS_TEST_USER_HEAP_EMPTY_VALUE &&
        active_statistics.active_allocation_count ==
            OS_TEST_USER_HEAP_EXPECTED_ACTIVE_ALLOCATION_COUNT &&
        active_statistics.active_requested_bytes ==
            OS_TEST_USER_HEAP_FIRST_SIZE_BYTES + OS_TEST_USER_HEAP_SECOND_SIZE_BYTES &&
        active_statistics.capacity_bytes == break_context.current_size_bytes &&
        heap.Validate() == os::user::UserHeapStatus::Succeeded;
    test_context.Expect(allocation_valid, OS_TEST_USER_HEAP_ALLOCATE);

    void *reused_allocation = nullptr;
    const bool reuse_valid =
        heap.Release(first_allocation) == os::user::UserHeapStatus::Succeeded &&
        heap.Allocate(OS_TEST_USER_HEAP_REUSE_SIZE_BYTES, reused_allocation) ==
            os::user::UserHeapStatus::Succeeded &&
        reused_allocation == first_allocation &&
        heap.Release(reused_allocation) == os::user::UserHeapStatus::Succeeded &&
        heap.Release(second_allocation) == os::user::UserHeapStatus::Succeeded &&
        heap.Statistics().active_allocation_count == OS_TEST_USER_HEAP_EMPTY_VALUE &&
        heap.Statistics().free_block_count == OS_TEST_USER_HEAP_EXPECTED_FREE_BLOCK_COUNT &&
        heap.Validate() == os::user::UserHeapStatus::Succeeded;
    test_context.Expect(reuse_valid, OS_TEST_USER_HEAP_REUSE);

    void *zero_size_output = reinterpret_cast<void *>(UINT64_MAX);
    const bool diagnostics_valid =
        heap.Release(second_allocation) == os::user::UserHeapStatus::AllocationNotFound &&
        heap.Release(static_cast<uint8_t *>(second_allocation) + OS_TEST_USER_HEAP_SINGLE_UNIT) ==
            os::user::UserHeapStatus::AllocationNotFound &&
        heap.Allocate(OS_TEST_USER_HEAP_EMPTY_VALUE, zero_size_output) ==
            os::user::UserHeapStatus::InvalidSize &&
        zero_size_output == reinterpret_cast<void *>(UINT64_MAX) &&
        heap.Validate() == os::user::UserHeapStatus::Succeeded;
    test_context.Expect(diagnostics_valid, OS_TEST_USER_HEAP_DIAGNOSTICS);

    const os::user::UserHeapStatistics before_failure = heap.Statistics();
    break_context.fail_next_growth = true;
    void *failed_output = reinterpret_cast<void *>(UINT64_MAX);
    const bool failure_atomic_valid =
        heap.Allocate(OS_TEST_USER_HEAP_FAILURE_SIZE_BYTES, failed_output) ==
            os::user::UserHeapStatus::ProgramBreakFailed &&
        failed_output == reinterpret_cast<void *>(UINT64_MAX) &&
        heap.Statistics().capacity_bytes == before_failure.capacity_bytes &&
        heap.Statistics().active_allocation_count == before_failure.active_allocation_count &&
        heap.Statistics().active_requested_bytes == before_failure.active_requested_bytes &&
        heap.Allocate(OS_TEST_USER_HEAP_STORAGE_SIZE_BYTES, failed_output) ==
            os::user::UserHeapStatus::CapacityExhausted &&
        heap.Statistics().capacity_bytes == before_failure.capacity_bytes &&
        heap.Validate() == os::user::UserHeapStatus::Succeeded;
    test_context.Expect(failure_atomic_valid, OS_TEST_USER_HEAP_FAILURE_ATOMIC);

    return test_context.ExitCode();
}
