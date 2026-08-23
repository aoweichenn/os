#include <os/kernel/process/file_readahead_request.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_READAHEAD_REQUEST_SUITE_NAME =
    "kernel/file_readahead_request/unit";
constexpr std::string_view OS_TEST_FILE_READAHEAD_REQUEST_INITIALIZATION =
    "预读请求队列必须拒绝无效存储、零容量和重复初始化";
constexpr std::string_view OS_TEST_FILE_READAHEAD_REQUEST_FIFO =
    "预读请求必须按 FIFO 从 queued 转为 running 并由 generation token 完成";
constexpr std::string_view OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY_REUSE =
    "满队列必须显式拒绝且槽位复用后旧 token 不得影响新请求";

constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY = 3ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_READAHEAD_REQUEST_FIRST_VALUE = 1ULL;

[[nodiscard]] os::kernel::FileReadaheadRequest
MakeRequest(os::kernel::fs::Vfs &vfs, const uint64_t start_page_index,
            const uint64_t policy_generation) noexcept {
    return os::kernel::FileReadaheadRequest{
        .vfs = &vfs,
        .open_file =
            os::kernel::fs::OpenFile{
                .path =
                    os::kernel::fs::Path{
                        .mount_identifier = OS_TEST_FILE_READAHEAD_REQUEST_FIRST_VALUE,
                        .vnode =
                            os::kernel::fs::Vnode{
                                .superblock = nullptr,
                                .identifier = 17ULL,
                                .generation = 5ULL,
                                .type = os::kernel::fs::NodeType::RegularFile,
                            },
                    },
                .offset_bytes = OS_TEST_FILE_READAHEAD_REQUEST_EMPTY_VALUE,
                .readable = true,
                .writable = false,
                .open = true,
            },
        .start_page_index = start_page_index,
        .page_count = 2ULL,
        .policy_generation = policy_generation,
    };
}

[[nodiscard]] bool TokensEqual(const os::kernel::FileReadaheadRequestToken left,
                               const os::kernel::FileReadaheadRequestToken right) noexcept {
    return left.slot_index == right.slot_index && left.generation == right.generation;
}

[[nodiscard]] bool ValidateInitialization() noexcept {
    os::kernel::FileReadaheadRequestQueue queue{};
    os::kernel::FileReadaheadRequestSlot slots[OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY]{};
    uint64_t ready[OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY]{};
    return queue.Initialize(nullptr, ready, OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY) ==
               os::kernel::FileReadaheadRequestStatus::InvalidStorage &&
           queue.Initialize(slots, nullptr, OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY) ==
               os::kernel::FileReadaheadRequestStatus::InvalidStorage &&
           queue.Initialize(slots, ready, OS_TEST_FILE_READAHEAD_REQUEST_EMPTY_VALUE) ==
               os::kernel::FileReadaheadRequestStatus::InvalidCapacity &&
           queue.Initialize(slots, ready, OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY) ==
               os::kernel::FileReadaheadRequestStatus::Succeeded &&
           queue.Initialize(slots, ready, OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY) ==
               os::kernel::FileReadaheadRequestStatus::AlreadyInitialized &&
           queue.Validate() == os::kernel::FileReadaheadRequestStatus::Succeeded;
}

[[nodiscard]] bool ValidateFifo() noexcept {
    os::kernel::FileReadaheadRequestQueue queue{};
    os::kernel::FileReadaheadRequestSlot slots[OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY]{};
    uint64_t ready[OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    if (queue.Initialize(slots, ready, OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY) !=
        os::kernel::FileReadaheadRequestStatus::Succeeded) {
        return false;
    }
    os::kernel::FileReadaheadRequestToken submitted[OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY]{};
    for (uint64_t request_index = OS_TEST_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         request_index < OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY; ++request_index) {
        if (queue.Enqueue(MakeRequest(vfs, request_index + 10ULL, request_index + 1ULL),
                          submitted[request_index]) !=
            os::kernel::FileReadaheadRequestStatus::Succeeded) {
            return false;
        }
    }
    for (uint64_t request_index = OS_TEST_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         request_index < OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY; ++request_index) {
        os::kernel::FileReadaheadRequestToken acquired{};
        os::kernel::FileReadaheadRequest request{};
        if (queue.Acquire(acquired, request) != os::kernel::FileReadaheadRequestStatus::Succeeded ||
            !TokensEqual(acquired, submitted[request_index]) || request.vfs != &vfs ||
            request.start_page_index != request_index + 10ULL ||
            request.policy_generation != request_index + 1ULL ||
            queue.Complete(acquired) != os::kernel::FileReadaheadRequestStatus::Succeeded) {
            return false;
        }
    }
    os::kernel::FileReadaheadRequestToken empty_token{};
    os::kernel::FileReadaheadRequest empty_request{};
    const os::kernel::FileReadaheadRequestStatistics statistics = queue.Statistics();
    return queue.Acquire(empty_token, empty_request) ==
               os::kernel::FileReadaheadRequestStatus::NoQueuedRequest &&
           statistics.active_request_count == OS_TEST_FILE_READAHEAD_REQUEST_EMPTY_VALUE &&
           statistics.enqueue_count == OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY &&
           statistics.acquisition_count == OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY &&
           statistics.completion_count == OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY &&
           statistics.peak_active_request_count == OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY &&
           queue.Validate() == os::kernel::FileReadaheadRequestStatus::Succeeded;
}

[[nodiscard]] bool ValidateCapacityAndStaleToken() noexcept {
    os::kernel::FileReadaheadRequestQueue queue{};
    os::kernel::FileReadaheadRequestSlot slots[OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY]{};
    uint64_t ready[OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    if (queue.Initialize(slots, ready, OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY) !=
        os::kernel::FileReadaheadRequestStatus::Succeeded) {
        return false;
    }
    os::kernel::FileReadaheadRequestToken submitted[OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY]{};
    for (uint64_t request_index = OS_TEST_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
         request_index < OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY; ++request_index) {
        if (queue.Enqueue(MakeRequest(vfs, request_index, request_index + 1ULL),
                          submitted[request_index]) !=
            os::kernel::FileReadaheadRequestStatus::Succeeded) {
            return false;
        }
    }
    os::kernel::FileReadaheadRequest invalid_request = MakeRequest(vfs, 99ULL, 99ULL);
    invalid_request.page_count = OS_TEST_FILE_READAHEAD_REQUEST_EMPTY_VALUE;
    os::kernel::FileReadaheadRequestToken rejected_token{};
    if (queue.Enqueue(invalid_request, rejected_token) !=
            os::kernel::FileReadaheadRequestStatus::InvalidRequest ||
        queue.Enqueue(MakeRequest(vfs, 99ULL, 99ULL), rejected_token) !=
            os::kernel::FileReadaheadRequestStatus::CapacityExhausted) {
        return false;
    }
    os::kernel::FileReadaheadRequestToken acquired{};
    os::kernel::FileReadaheadRequest acquired_request{};
    if (queue.Acquire(acquired, acquired_request) !=
            os::kernel::FileReadaheadRequestStatus::Succeeded ||
        queue.Complete(acquired) != os::kernel::FileReadaheadRequestStatus::Succeeded ||
        queue.Complete(acquired) != os::kernel::FileReadaheadRequestStatus::InvalidToken) {
        return false;
    }
    os::kernel::FileReadaheadRequestToken replacement{};
    if (queue.Enqueue(MakeRequest(vfs, 100ULL, 100ULL), replacement) !=
            os::kernel::FileReadaheadRequestStatus::Succeeded ||
        replacement.slot_index != acquired.slot_index ||
        replacement.generation == acquired.generation ||
        queue.Complete(acquired) != os::kernel::FileReadaheadRequestStatus::InvalidToken) {
        return false;
    }
    while (queue.Acquire(acquired, acquired_request) ==
           os::kernel::FileReadaheadRequestStatus::Succeeded) {
        if (queue.Complete(acquired) != os::kernel::FileReadaheadRequestStatus::Succeeded) {
            return false;
        }
    }
    const os::kernel::FileReadaheadRequestStatistics statistics = queue.Statistics();
    return statistics.capacity_rejection_count == OS_TEST_FILE_READAHEAD_REQUEST_FIRST_VALUE &&
           statistics.enqueue_count == OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY +
                                           OS_TEST_FILE_READAHEAD_REQUEST_FIRST_VALUE &&
           statistics.completion_count == statistics.enqueue_count &&
           queue.Validate() == os::kernel::FileReadaheadRequestStatus::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_READAHEAD_REQUEST_SUITE_NAME};
    test_context.Expect(ValidateInitialization(), OS_TEST_FILE_READAHEAD_REQUEST_INITIALIZATION);
    test_context.Expect(ValidateFifo(), OS_TEST_FILE_READAHEAD_REQUEST_FIFO);
    test_context.Expect(ValidateCapacityAndStaleToken(),
                        OS_TEST_FILE_READAHEAD_REQUEST_CAPACITY_REUSE);
    return test_context.ExitCode();
}
