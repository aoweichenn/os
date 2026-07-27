#include "os/kernel/ipc/pipe.hpp"
#include "os/kernel/ipc/pipe_manager.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DYNAMIC_PIPE_SUITE_NAME = "kernel/dynamic_pipe/unit";
constexpr std::string_view OS_TEST_DYNAMIC_PIPE_LAZY_STORAGE =
    "64 KiB 管道必须按页懒分配、跨页保持字节流并在双端关闭后回收";
constexpr std::string_view OS_TEST_DYNAMIC_PIPE_ALLOCATION_ROLLBACK =
    "页分配失败必须回滚本次写入且不改变可见字节流";
constexpr std::string_view OS_TEST_DYNAMIC_PIPE_MANAGER_CAPACITY =
    "功能档必须容纳 128 个并发管道、拒绝第 129 个并允许槽位复用";
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES =
    os::kernel::OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES * 2ULL + 17ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_EXPECTED_PAGE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_FAILURE_SIZE_BYTES =
    os::kernel::OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES + OS_TEST_DYNAMIC_PIPE_FIRST_VALUE;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_PATTERN_MODULUS = 251ULL;

struct PagePool final {
    alignas(os::kernel::OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES)
        uint8_t pages[os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT]
                     [os::kernel::OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES];
    bool used[os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT];
    uint64_t capacity;
};

[[nodiscard]] bool AllocatePage(void *const context, uint64_t &physical_address,
                                uint8_t *&virtual_address) noexcept {
    auto *const pool = static_cast<PagePool *>(context);
    physical_address = OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
    virtual_address = nullptr;
    if (pool == nullptr) {
        return false;
    }
    for (uint64_t page_index = OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
         page_index < pool->capacity; ++page_index) {
        if (pool->used[page_index]) {
            continue;
        }
        pool->used[page_index] = true;
        physical_address = page_index + OS_TEST_DYNAMIC_PIPE_FIRST_VALUE;
        virtual_address = pool->pages[page_index];
        return true;
    }
    return false;
}

[[nodiscard]] bool ReleasePage(void *const context, const uint64_t physical_address,
                               uint8_t *const virtual_address) noexcept {
    auto *const pool = static_cast<PagePool *>(context);
    if (pool == nullptr || physical_address == OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE) {
        return false;
    }
    const uint64_t page_index = physical_address - OS_TEST_DYNAMIC_PIPE_FIRST_VALUE;
    if (page_index >= pool->capacity || !pool->used[page_index] ||
        virtual_address != pool->pages[page_index]) {
        return false;
    }
    pool->used[page_index] = false;
    return true;
}

[[nodiscard]] os::kernel::PipePageAllocator MakeAllocator(PagePool &pool) noexcept {
    return os::kernel::PipePageAllocator{
        .allocate_page = AllocatePage,
        .release_page = ReleasePage,
        .context = &pool,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_DYNAMIC_PIPE_SUITE_NAME};

    PagePool page_pool{};
    page_pool.capacity = os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT;
    os::kernel::Pipe pipe{};
    uint8_t input[OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES]{};
    uint8_t output[OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES]{};
    for (uint64_t byte_index = OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
         byte_index < OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES; ++byte_index) {
        input[byte_index] = static_cast<uint8_t>(byte_index % OS_TEST_DYNAMIC_PIPE_PATTERN_MODULUS);
    }
    const bool initialized =
        pipe.Initialize(MakeAllocator(page_pool),
                        os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES) ==
            os::kernel::PipeStatus::Succeeded &&
        pipe.Statistics().allocated_page_count == OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
    uint64_t written_bytes = OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
    uint64_t read_bytes = OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
    bool stream_preserved =
        initialized &&
        pipe.TryWrite(input, OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES, written_bytes) ==
            os::kernel::PipeStatus::Succeeded &&
        written_bytes == OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES &&
        pipe.Statistics().allocated_page_count == OS_TEST_DYNAMIC_PIPE_EXPECTED_PAGE_COUNT &&
        pipe.TryRead(output, OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES, read_bytes) ==
            os::kernel::PipeStatus::Succeeded &&
        read_bytes == OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES;
    for (uint64_t byte_index = OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
         byte_index < OS_TEST_DYNAMIC_PIPE_CROSS_PAGE_SIZE_BYTES; ++byte_index) {
        stream_preserved = stream_preserved && output[byte_index] == input[byte_index];
    }
    const bool released =
        pipe.CloseReader() == os::kernel::PipeStatus::Succeeded &&
        pipe.CloseWriter() == os::kernel::PipeStatus::Succeeded &&
        pipe.Statistics().allocated_page_count == OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE &&
        pipe.Statistics().page_release_count == OS_TEST_DYNAMIC_PIPE_EXPECTED_PAGE_COUNT;
    test_context.Expect(stream_preserved && released, OS_TEST_DYNAMIC_PIPE_LAZY_STORAGE);

    PagePool failure_pool{};
    failure_pool.capacity = OS_TEST_DYNAMIC_PIPE_FIRST_VALUE;
    os::kernel::Pipe failure_pipe{};
    uint8_t failure_input[OS_TEST_DYNAMIC_PIPE_FAILURE_SIZE_BYTES]{};
    const bool failure_rolled_back =
        failure_pipe.Initialize(MakeAllocator(failure_pool),
                                os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES) ==
            os::kernel::PipeStatus::Succeeded &&
        failure_pipe.TryWrite(failure_input, OS_TEST_DYNAMIC_PIPE_FAILURE_SIZE_BYTES,
                              written_bytes) == os::kernel::PipeStatus::OutOfMemory &&
        written_bytes == OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE &&
        failure_pipe.Statistics().buffered_byte_count == OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE &&
        failure_pipe.Statistics().allocated_page_count == OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE &&
        failure_pipe.Statistics().page_allocation_count == OS_TEST_DYNAMIC_PIPE_FIRST_VALUE &&
        failure_pipe.Statistics().page_release_count == OS_TEST_DYNAMIC_PIPE_FIRST_VALUE;
    test_context.Expect(failure_rolled_back, OS_TEST_DYNAMIC_PIPE_ALLOCATION_ROLLBACK);

    static os::kernel::PipeManager manager{};
    PagePool manager_pool{};
    manager_pool.capacity = os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT;
    os::kernel::Pipe *pipes[os::kernel::OS_KERNEL_PIPE_MANAGER_FUNCTIONAL_CAPACITY]{};
    bool manager_valid =
        manager.Initialize(MakeAllocator(manager_pool),
                           os::kernel::OS_KERNEL_PIPE_MANAGER_FUNCTIONAL_CAPACITY) ==
        os::kernel::PipeManagerStatus::Succeeded;
    for (uint64_t pipe_index = OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
         pipe_index < os::kernel::OS_KERNEL_PIPE_MANAGER_FUNCTIONAL_CAPACITY; ++pipe_index) {
        manager_valid =
            manager_valid &&
            manager.Create(pipes[pipe_index]) == os::kernel::PipeManagerStatus::Succeeded &&
            pipes[pipe_index] != nullptr;
    }
    os::kernel::Pipe *extra_pipe = nullptr;
    manager_valid =
        manager_valid &&
        manager.Create(extra_pipe) == os::kernel::PipeManagerStatus::CapacityExhausted &&
        extra_pipe == nullptr;
    for (uint64_t pipe_index = OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE;
         pipe_index < os::kernel::OS_KERNEL_PIPE_MANAGER_FUNCTIONAL_CAPACITY; ++pipe_index) {
        manager_valid =
            manager_valid &&
            manager.CloseReader(*pipes[pipe_index]) == os::kernel::PipeManagerStatus::Succeeded &&
            manager.CloseWriter(*pipes[pipe_index]) == os::kernel::PipeManagerStatus::Succeeded;
    }
    manager_valid =
        manager_valid &&
        manager.Create(extra_pipe) == os::kernel::PipeManagerStatus::Succeeded &&
        manager.CloseReader(*extra_pipe) == os::kernel::PipeManagerStatus::Succeeded &&
        manager.CloseWriter(*extra_pipe) == os::kernel::PipeManagerStatus::Succeeded &&
        manager.Validate() == os::kernel::PipeManagerStatus::Succeeded &&
        manager.Statistics().active_pipe_count == OS_TEST_DYNAMIC_PIPE_EMPTY_VALUE &&
        manager.Statistics().peak_active_pipe_count ==
            os::kernel::OS_KERNEL_PIPE_MANAGER_FUNCTIONAL_CAPACITY;
    test_context.Expect(manager_valid, OS_TEST_DYNAMIC_PIPE_MANAGER_CAPACITY);

    return test_context.ExitCode();
}
