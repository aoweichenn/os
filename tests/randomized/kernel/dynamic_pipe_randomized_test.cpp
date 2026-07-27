#include "os/kernel/ipc/pipe.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_DYNAMIC_PIPE_RANDOM_SUITE_NAME =
    "kernel/dynamic_pipe/randomized";
constexpr std::string_view OS_TEST_DYNAMIC_PIPE_RANDOM_MODEL =
    "十万次跨页随机部分读写必须与 64 KiB 参考环形队列逐字节一致";
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_SEED = 0xD19A64B120260111ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_MAXIMUM_TRANSFER_BYTES = 1024ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_WRITE_MASK = 1ULL;
constexpr uint64_t OS_TEST_DYNAMIC_PIPE_RANDOM_BYTE_MASK = 0xFFULL;

struct PagePool final {
    alignas(os::kernel::OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES)
        uint8_t pages[os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT]
                     [os::kernel::OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES];
    bool used[os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT];
};

[[nodiscard]] bool AllocatePage(void *const context, uint64_t &physical_address,
                                uint8_t *&virtual_address) noexcept {
    auto *const pool = static_cast<PagePool *>(context);
    for (uint64_t page_index = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
         pool != nullptr && page_index < os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT;
         ++page_index) {
        if (!pool->used[page_index]) {
            pool->used[page_index] = true;
            physical_address = page_index + OS_TEST_DYNAMIC_PIPE_RANDOM_FIRST_VALUE;
            virtual_address = pool->pages[page_index];
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ReleasePage(void *const context, const uint64_t physical_address,
                               uint8_t *const virtual_address) noexcept {
    auto *const pool = static_cast<PagePool *>(context);
    if (pool == nullptr || physical_address == OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE) {
        return false;
    }
    const uint64_t page_index = physical_address - OS_TEST_DYNAMIC_PIPE_RANDOM_FIRST_VALUE;
    if (page_index >= os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT ||
        !pool->used[page_index] || virtual_address != pool->pages[page_index]) {
        return false;
    }
    pool->used[page_index] = false;
    return true;
}

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_DYNAMIC_PIPE_RANDOM_MULTIPLIER +
            OS_TEST_DYNAMIC_PIPE_RANDOM_INCREMENT;
    return state;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_DYNAMIC_PIPE_RANDOM_SUITE_NAME};
    PagePool page_pool{};
    os::kernel::Pipe pipe{};
    const os::kernel::PipePageAllocator allocator{
        .allocate_page = AllocatePage,
        .release_page = ReleasePage,
        .context = &page_pool,
    };
    bool consistent =
        pipe.Initialize(allocator, os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES) ==
        os::kernel::PipeStatus::Succeeded;
    static uint8_t reference[os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES]{};
    uint64_t reference_read_index = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
    uint64_t reference_write_index = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
    uint64_t reference_size = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
    uint64_t random_state = OS_TEST_DYNAMIC_PIPE_RANDOM_SEED;

    for (uint64_t operation_index = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
         operation_index < OS_TEST_DYNAMIC_PIPE_RANDOM_OPERATION_COUNT; ++operation_index) {
        const bool choose_write =
            (NextRandom(random_state) & OS_TEST_DYNAMIC_PIPE_RANDOM_WRITE_MASK) !=
            OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
        const uint64_t transfer_bytes =
            NextRandom(random_state) % OS_TEST_DYNAMIC_PIPE_RANDOM_MAXIMUM_TRANSFER_BYTES +
            OS_TEST_DYNAMIC_PIPE_RANDOM_FIRST_VALUE;
        uint8_t transfer[OS_TEST_DYNAMIC_PIPE_RANDOM_MAXIMUM_TRANSFER_BYTES]{};
        if (choose_write) {
            for (uint64_t byte_index = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
                 byte_index < transfer_bytes; ++byte_index) {
                transfer[byte_index] =
                    static_cast<uint8_t>(NextRandom(random_state) &
                                         OS_TEST_DYNAMIC_PIPE_RANDOM_BYTE_MASK);
            }
            uint64_t written_bytes = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
            const os::kernel::PipeStatus status =
                pipe.TryWrite(transfer, transfer_bytes, written_bytes);
            const uint64_t expected_written =
                transfer_bytes <
                        os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES - reference_size
                    ? transfer_bytes
                    : os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES - reference_size;
            consistent =
                consistent &&
                (expected_written == OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE
                     ? status == os::kernel::PipeStatus::WouldBlock
                     : status == os::kernel::PipeStatus::Succeeded &&
                           written_bytes == expected_written);
            for (uint64_t byte_index = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
                 byte_index < written_bytes; ++byte_index) {
                reference[reference_write_index] = transfer[byte_index];
                reference_write_index =
                    (reference_write_index + OS_TEST_DYNAMIC_PIPE_RANDOM_FIRST_VALUE) %
                    os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES;
            }
            reference_size += written_bytes;
        } else {
            uint64_t read_bytes = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
            const os::kernel::PipeStatus status =
                pipe.TryRead(transfer, transfer_bytes, read_bytes);
            const uint64_t expected_read =
                transfer_bytes < reference_size ? transfer_bytes : reference_size;
            consistent =
                consistent &&
                (expected_read == OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE
                     ? status == os::kernel::PipeStatus::WouldBlock
                     : status == os::kernel::PipeStatus::Succeeded &&
                           read_bytes == expected_read);
            for (uint64_t byte_index = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
                 byte_index < read_bytes; ++byte_index) {
                consistent = consistent && transfer[byte_index] == reference[reference_read_index];
                reference_read_index =
                    (reference_read_index + OS_TEST_DYNAMIC_PIPE_RANDOM_FIRST_VALUE) %
                    os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES;
            }
            reference_size -= read_bytes;
        }
        const os::kernel::PipeStatistics statistics = pipe.Statistics();
        consistent =
            consistent && statistics.buffered_byte_count == reference_size &&
            statistics.allocated_page_count <=
                os::kernel::OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT;
    }
    consistent = consistent &&
                 pipe.CloseWriter() == os::kernel::PipeStatus::Succeeded;
    uint8_t drain[OS_TEST_DYNAMIC_PIPE_RANDOM_MAXIMUM_TRANSFER_BYTES]{};
    while (consistent && reference_size != OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE) {
        uint64_t read_bytes = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
        consistent =
            pipe.TryRead(drain, OS_TEST_DYNAMIC_PIPE_RANDOM_MAXIMUM_TRANSFER_BYTES, read_bytes) ==
            os::kernel::PipeStatus::Succeeded;
        for (uint64_t byte_index = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
             byte_index < read_bytes; ++byte_index) {
            consistent = consistent && drain[byte_index] == reference[reference_read_index];
            reference_read_index =
                (reference_read_index + OS_TEST_DYNAMIC_PIPE_RANDOM_FIRST_VALUE) %
                os::kernel::OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES;
        }
        reference_size -= read_bytes;
    }
    uint64_t final_read_bytes = OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
    consistent =
        consistent &&
        pipe.TryRead(drain, OS_TEST_DYNAMIC_PIPE_RANDOM_MAXIMUM_TRANSFER_BYTES,
                     final_read_bytes) == os::kernel::PipeStatus::EndOfFile &&
        pipe.CloseReader() == os::kernel::PipeStatus::Succeeded &&
        pipe.Statistics().allocated_page_count == OS_TEST_DYNAMIC_PIPE_RANDOM_EMPTY_VALUE;
    test_context.Expect(consistent, OS_TEST_DYNAMIC_PIPE_RANDOM_MODEL);
    return test_context.ExitCode();
}
