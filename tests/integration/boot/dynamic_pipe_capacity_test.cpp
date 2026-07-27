#include "os/kernel/ipc/pipe_manager.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PIPE_CAPACITY_SUITE_NAME =
    "kernel/dynamic_pipe_capacity/integration";
constexpr std::string_view OS_TEST_PIPE_CAPACITY_FULL_PROFILE =
    "能力档必须精确创建 1024 个无缓冲页管道并无泄漏地整体回收";
constexpr uint64_t OS_TEST_PIPE_CAPACITY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PIPE_CAPACITY_FIRST_VALUE = 1ULL;

struct CapacityPage final {
    alignas(os::kernel::OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES)
        uint8_t bytes[os::kernel::OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES];
    bool active;
};

[[nodiscard]] bool AllocatePage(void *const context, uint64_t &physical_address,
                                uint8_t *&virtual_address) noexcept {
    auto *const page = static_cast<CapacityPage *>(context);
    if (page == nullptr || page->active) {
        return false;
    }
    page->active = true;
    physical_address = OS_TEST_PIPE_CAPACITY_FIRST_VALUE;
    virtual_address = page->bytes;
    return true;
}

[[nodiscard]] bool ReleasePage(void *const context, const uint64_t physical_address,
                               uint8_t *const virtual_address) noexcept {
    auto *const page = static_cast<CapacityPage *>(context);
    if (page == nullptr || !page->active ||
        physical_address != OS_TEST_PIPE_CAPACITY_FIRST_VALUE ||
        virtual_address != page->bytes) {
        return false;
    }
    page->active = false;
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PIPE_CAPACITY_SUITE_NAME};
    static os::kernel::PipeManager manager{};
    static os::kernel::Pipe *pipes[os::kernel::OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY]{};
    CapacityPage page{};
    const os::kernel::PipePageAllocator allocator{
        .allocate_page = AllocatePage,
        .release_page = ReleasePage,
        .context = &page,
    };
    bool valid =
        manager.Initialize(allocator, os::kernel::OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY) ==
        os::kernel::PipeManagerStatus::Succeeded;
    for (uint64_t pipe_index = OS_TEST_PIPE_CAPACITY_EMPTY_VALUE;
         pipe_index < os::kernel::OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY; ++pipe_index) {
        valid = valid &&
                manager.Create(pipes[pipe_index]) == os::kernel::PipeManagerStatus::Succeeded;
    }
    os::kernel::Pipe *rejected_pipe = nullptr;
    valid = valid &&
            manager.Create(rejected_pipe) == os::kernel::PipeManagerStatus::CapacityExhausted &&
            rejected_pipe == nullptr && !page.active;
    for (uint64_t pipe_index = OS_TEST_PIPE_CAPACITY_EMPTY_VALUE;
         pipe_index < os::kernel::OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY; ++pipe_index) {
        valid = valid &&
                manager.CloseWriter(*pipes[pipe_index]) ==
                    os::kernel::PipeManagerStatus::Succeeded &&
                manager.CloseReader(*pipes[pipe_index]) ==
                    os::kernel::PipeManagerStatus::Succeeded;
    }
    const os::kernel::PipeManagerStatistics statistics = manager.Statistics();
    valid = valid && manager.Validate() == os::kernel::PipeManagerStatus::Succeeded &&
            statistics.active_pipe_count == OS_TEST_PIPE_CAPACITY_EMPTY_VALUE &&
            statistics.peak_active_pipe_count ==
                os::kernel::OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY &&
            statistics.creation_count == os::kernel::OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY &&
            statistics.release_count == os::kernel::OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY &&
            statistics.capacity_rejection_count == OS_TEST_PIPE_CAPACITY_FIRST_VALUE;
    test_context.Expect(valid, OS_TEST_PIPE_CAPACITY_FULL_PROFILE);
    return test_context.ExitCode();
}
