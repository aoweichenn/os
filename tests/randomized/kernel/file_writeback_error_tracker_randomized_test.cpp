#include <os/kernel/memory/file_writeback_error_tracker.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <test_context.hpp>

#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_SUITE_NAME =
    "kernel/file_writeback_error_tracker/randomized";
constexpr std::string_view OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_MODEL =
    "十万步打开、关闭、记录和检查必须与独立错误序列模型一致";

constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_HEAP_SIZE_BYTES = 128ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_IDENTITY_COUNT = 16ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_OPERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_VALIDATION_INTERVAL = 256ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_OPERATION_KIND_COUNT = 4ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_REGISTER_OPERATION = 0ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_UNREGISTER_OPERATION = 1ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_RECORD_OPERATION = 2ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_BOOLEAN_BIT_MASK = 1ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_MAXIMUM_OPEN_COUNT = 64ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_SUPERBLOCK_IDENTIFIER = 19ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_SUPERBLOCK_GENERATION = 7ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_FIRST_NODE_IDENTIFIER = 101ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_NODE_GENERATION = 3ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_SEED = 0x5742455252534551ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_INCREMENT = 1442695040888963407ULL;

struct ModelRecord final {
    uint64_t open_count;
    uint64_t sequence;
};

alignas(OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_HEAP_ALIGNMENT_BYTES)
    uint8_t tracker_heap_storage[OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_HEAP_SIZE_BYTES]{};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_MULTIPLIER +
            OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_INCREMENT;
    return state;
}

[[nodiscard]] os::kernel::FileCacheIdentity MakeIdentity(const uint64_t index) noexcept {
    return os::kernel::FileCacheIdentity{
        .superblock_identifier = OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_SUPERBLOCK_IDENTIFIER,
        .superblock_generation = OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_SUPERBLOCK_GENERATION,
        .node_identifier = OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_FIRST_NODE_IDENTIFIER + index,
        .node_generation = OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_NODE_GENERATION,
    };
}

[[nodiscard]] uint64_t AddressOf(void *const pointer) noexcept {
    return reinterpret_cast<uint64_t>(pointer);
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_SUITE_NAME};
    os::kernel::KernelHeap heap{};
    os::kernel::FileWritebackErrorTracker tracker{};
    ModelRecord model[OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_IDENTITY_COUNT]{};
    bool model_valid =
        heap.Initialize(AddressOf(tracker_heap_storage), sizeof(tracker_heap_storage)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        tracker.Initialize(heap) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded;
    uint64_t random_state = OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_SEED;
    for (uint64_t operation_index = 0ULL;
         model_valid && operation_index < OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_OPERATION_COUNT;
         ++operation_index) {
        const uint64_t random_value = NextRandom(random_state);
        const uint64_t identity_index =
            random_value % OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_IDENTITY_COUNT;
        const uint64_t operation_kind =
            (random_value / OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_IDENTITY_COUNT) %
            OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_OPERATION_KIND_COUNT;
        ModelRecord &expected = model[identity_index];
        const os::kernel::FileCacheIdentity identity = MakeIdentity(identity_index);
        if (operation_kind == OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_REGISTER_OPERATION &&
            expected.open_count < OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_MAXIMUM_OPEN_COUNT) {
            uint64_t sampled_sequence = UINT64_MAX;
            model_valid =
                tracker.Register(identity, sampled_sequence) ==
                    os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
                sampled_sequence == expected.sequence;
            ++expected.open_count;
        } else if (operation_kind == OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_UNREGISTER_OPERATION) {
            const os::kernel::FileWritebackErrorTrackerStatus status = tracker.Unregister(identity);
            model_valid = status == (expected.open_count == 0ULL
                                         ? os::kernel::FileWritebackErrorTrackerStatus::NotFound
                                         : os::kernel::FileWritebackErrorTrackerStatus::Succeeded);
            if (expected.open_count != 0ULL) {
                --expected.open_count;
                if (expected.open_count == 0ULL) {
                    expected.sequence = 0ULL;
                }
            }
        } else if (operation_kind == OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_RECORD_OPERATION) {
            model_valid = tracker.Record(identity, os::kernel::FileWritebackError::InputOutput) ==
                          os::kernel::FileWritebackErrorTrackerStatus::Succeeded;
            if (expected.open_count != 0ULL) {
                ++expected.sequence;
            }
        } else {
            const uint64_t sampled_sequence =
                expected.sequence == 0ULL ||
                        (random_value &
                         OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_BOOLEAN_BIT_MASK) == 0ULL
                    ? expected.sequence
                    : expected.sequence - 1ULL;
            uint64_t current_sequence = UINT64_MAX;
            os::kernel::FileWritebackError error = os::kernel::FileWritebackError::None;
            const os::kernel::FileWritebackErrorTrackerStatus status =
                tracker.Check(identity, sampled_sequence, current_sequence, error);
            model_valid =
                status == (expected.open_count == 0ULL
                               ? os::kernel::FileWritebackErrorTrackerStatus::NotFound
                               : os::kernel::FileWritebackErrorTrackerStatus::Succeeded) &&
                (expected.open_count == 0ULL ||
                 (current_sequence == expected.sequence &&
                  error == (sampled_sequence == expected.sequence
                                ? os::kernel::FileWritebackError::None
                                : os::kernel::FileWritebackError::InputOutput)));
        }
        if (model_valid &&
            (operation_index % OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_VALIDATION_INTERVAL) ==
                0ULL) {
            uint64_t expected_record_count = 0ULL;
            uint64_t expected_open_count = 0ULL;
            for (const ModelRecord &record : model) {
                if (record.open_count != 0ULL) {
                    ++expected_record_count;
                    expected_open_count += record.open_count;
                }
            }
            const os::kernel::FileWritebackErrorTrackerStatistics statistics =
                tracker.Statistics();
            model_valid =
                tracker.Validate() == os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
                statistics.active_record_count == expected_record_count &&
                statistics.active_open_description_count == expected_open_count;
        }
    }
    for (uint64_t identity_index = 0ULL;
         model_valid && identity_index < OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_IDENTITY_COUNT;
         ++identity_index) {
        while (model[identity_index].open_count != 0ULL) {
            model_valid = tracker.Unregister(MakeIdentity(identity_index)) ==
                          os::kernel::FileWritebackErrorTrackerStatus::Succeeded;
            --model[identity_index].open_count;
        }
    }
    model_valid = model_valid &&
                  tracker.Validate() ==
                      os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
                  tracker.Destroy() == os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
                  heap.Statistics().allocation_count == 0ULL;
    test_context.Expect(model_valid, OS_TEST_FILE_WRITEBACK_ERROR_RANDOMIZED_MODEL);
    return test_context.ExitCode();
}
