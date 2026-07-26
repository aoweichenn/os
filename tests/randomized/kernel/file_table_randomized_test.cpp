#include "os/kernel/io/file_description.hpp"
#include "os/kernel/io/file_table.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "os/kernel/object/kernel_object.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

struct DescriptorRecord final {
    uint64_t generation;
    bool active;
};

enum class RandomOperation : uint64_t {
    Open,
    Duplicate,
    Close,
    ChangeLimit,
};

constexpr std::string_view OS_TEST_FILE_TABLE_RANDOM_SUITE_NAME = "kernel/file_table/randomized";
constexpr std::string_view OS_TEST_FILE_TABLE_RANDOM_OPERATION =
    "固定种子十万次 open、dup、close 和限额变化必须匹配参考模型";
constexpr std::string_view OS_TEST_FILE_TABLE_RANDOM_DRAIN =
    "随机序列结束后必须归零描述符、强引用、对象和堆分配";

constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_FIRST_VALUE = 1ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_HEAP_SIZE_BYTES = 2ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT =
    os::kernel::OS_KERNEL_FILE_TABLE_MAXIMUM_HARD_LIMIT;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_INITIAL_SOFT_LIMIT =
    os::kernel::OS_KERNEL_FILE_TABLE_FUNCTIONAL_HARD_LIMIT;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_VALIDATION_INTERVAL = 256ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_OPERATION_COUNT = 4ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_CLOSE_ON_EXEC_BIT = 1ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_SEED = 0x46445441424C4531ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR = UINT64_MAX;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_FILE_TABLE_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_FILE_TABLE_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_FILE_TABLE_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_FILE_TABLE_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] bool DiscardWrite(void *const context, const uint8_t *const source,
                                const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    static_cast<void>(context);
    written_bytes = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
    if (source == nullptr && length_bytes != OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE) {
        return false;
    }
    written_bytes = length_bytes;
    return true;
}

[[nodiscard]] os::kernel::FileDescriptionStatus
CreateOutputDescription(os::kernel::FileDescriptionManager &manager,
                        os::kernel::KernelObjectReference &reference) noexcept {
    const os::kernel::FileDescriptionCreateRequest request{
        .kind = os::kernel::FileDescriptionKind::ConsoleOutput,
        .file_status_flags = os::kernel::OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
        .console_input = nullptr,
        .device_write_operation = DiscardWrite,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .vfs = nullptr,
        .open_file = {},
    };
    return manager.Create(request, reference);
}

[[nodiscard]] uint64_t FindLowestAvailable(const DescriptorRecord *const records,
                                           const uint64_t minimum_descriptor,
                                           const uint64_t soft_limit) noexcept {
    if (records == nullptr || minimum_descriptor >= soft_limit) {
        return OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR;
    }
    for (uint64_t descriptor = minimum_descriptor; descriptor < soft_limit; ++descriptor) {
        if (!records[descriptor].active) {
            return descriptor;
        }
    }
    return OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR;
}

[[nodiscard]] uint64_t FindActiveDescriptor(const DescriptorRecord *const records,
                                            const uint64_t active_ordinal) noexcept {
    if (records == nullptr) {
        return OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR;
    }
    uint64_t remaining_ordinal = active_ordinal;
    for (uint64_t descriptor = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
         descriptor < OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT; ++descriptor) {
        if (!records[descriptor].active) {
            continue;
        }
        if (remaining_ordinal == OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE) {
            return descriptor;
        }
        --remaining_ordinal;
    }
    return OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_TABLE_RANDOM_SUITE_NAME};
    alignas(64) static uint8_t heap_buffer[OS_TEST_FILE_TABLE_RANDOM_HEAP_SIZE_BYTES]{};
    static DescriptorRecord records[OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT]{};
    os::kernel::KernelHeap heap{};
    os::kernel::KernelObjectManager object_manager{};
    os::kernel::FileDescriptionManager description_manager{};
    os::kernel::FileTable table{};
    if (heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                        OS_TEST_FILE_TABLE_RANDOM_HEAP_SIZE_BYTES) !=
            os::kernel::KernelHeapStatus::Succeeded ||
        object_manager.Initialize(heap) != os::kernel::KernelObjectStatus::Succeeded ||
        description_manager.Initialize(object_manager) !=
            os::kernel::FileDescriptionStatus::Succeeded ||
        table.Initialize(heap, object_manager, OS_TEST_FILE_TABLE_RANDOM_INITIAL_SOFT_LIMIT,
                         OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT) !=
            os::kernel::FileTableStatus::Succeeded) {
        test_context.Expect(false, OS_TEST_FILE_TABLE_RANDOM_OPERATION);
        return test_context.ExitCode();
    }

    uint64_t random_state = OS_TEST_FILE_TABLE_RANDOM_SEED;
    uint64_t active_descriptor_count = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
    uint64_t soft_limit = OS_TEST_FILE_TABLE_RANDOM_INITIAL_SOFT_LIMIT;
    uint64_t open_operation_count = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
    uint64_t duplicate_operation_count = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
    uint64_t close_operation_count = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
    uint64_t limit_operation_count = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;

    for (uint64_t iteration = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
         iteration < OS_TEST_FILE_TABLE_RANDOM_ITERATION_COUNT; ++iteration) {
        RandomOperation operation = static_cast<RandomOperation>(
            NextRandom(random_state) % OS_TEST_FILE_TABLE_RANDOM_OPERATION_COUNT);
        if (active_descriptor_count == OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE &&
            (operation == RandomOperation::Duplicate || operation == RandomOperation::Close)) {
            operation = RandomOperation::Open;
        }
        bool operation_valid = true;
        if (operation == RandomOperation::Open) {
            ++open_operation_count;
            const uint64_t minimum_descriptor =
                NextRandom(random_state) % OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT;
            const uint64_t expected_descriptor =
                FindLowestAvailable(records, minimum_descriptor, soft_limit);
            os::kernel::KernelObjectReference reference{};
            os::kernel::KernelObjectIdentity identity{};
            uint64_t descriptor = OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR;
            operation_valid =
                CreateOutputDescription(description_manager, reference) ==
                    os::kernel::FileDescriptionStatus::Succeeded &&
                reference.ReadIdentity(identity) == os::kernel::KernelObjectStatus::Succeeded;
            const os::kernel::FileTableStatus install_status =
                operation_valid ? table.Install(reference, minimum_descriptor,
                                                OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE, descriptor)
                                : os::kernel::FileTableStatus::ObjectFailure;
            if (expected_descriptor != OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR) {
                operation_valid = operation_valid &&
                                  install_status == os::kernel::FileTableStatus::Succeeded &&
                                  descriptor == expected_descriptor && !reference.IsActive();
                if (operation_valid) {
                    records[descriptor] = DescriptorRecord{
                        .generation = identity.generation,
                        .active = true,
                    };
                    ++active_descriptor_count;
                }
            } else {
                operation_valid =
                    operation_valid &&
                    install_status == os::kernel::FileTableStatus::SoftLimitExceeded &&
                    descriptor == OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR &&
                    reference.IsActive() &&
                    reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
            }
        } else if (operation == RandomOperation::Duplicate) {
            ++duplicate_operation_count;
            const uint64_t source_ordinal = NextRandom(random_state) % active_descriptor_count;
            const uint64_t source_descriptor = FindActiveDescriptor(records, source_ordinal);
            const uint64_t minimum_descriptor =
                NextRandom(random_state) % OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT;
            const uint64_t expected_descriptor =
                FindLowestAvailable(records, minimum_descriptor, soft_limit);
            const uint64_t descriptor_flags =
                (NextRandom(random_state) & OS_TEST_FILE_TABLE_RANDOM_CLOSE_ON_EXEC_BIT) !=
                        OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE
                    ? os::kernel::OS_KERNEL_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG
                    : OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
            uint64_t destination_descriptor = OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR;
            const os::kernel::KernelObjectManagerStatistics before_duplicate =
                object_manager.Statistics();
            const os::kernel::FileTableStatus duplicate_status = table.Duplicate(
                source_descriptor, minimum_descriptor, descriptor_flags, destination_descriptor);
            if (expected_descriptor != OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR) {
                operation_valid =
                    source_descriptor != OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR &&
                    duplicate_status == os::kernel::FileTableStatus::Succeeded &&
                    destination_descriptor == expected_descriptor;
                if (operation_valid) {
                    records[destination_descriptor] = DescriptorRecord{
                        .generation = records[source_descriptor].generation,
                        .active = true,
                    };
                    ++active_descriptor_count;
                }
            } else {
                operation_valid =
                    duplicate_status == os::kernel::FileTableStatus::SoftLimitExceeded &&
                    destination_descriptor == OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR &&
                    object_manager.Statistics().active_strong_reference_count ==
                        before_duplicate.active_strong_reference_count;
            }
        } else if (operation == RandomOperation::Close) {
            ++close_operation_count;
            const uint64_t active_ordinal = NextRandom(random_state) % active_descriptor_count;
            const uint64_t descriptor = FindActiveDescriptor(records, active_ordinal);
            os::kernel::KernelObjectReleaseResult release_result{};
            operation_valid =
                descriptor != OS_TEST_FILE_TABLE_RANDOM_INVALID_DESCRIPTOR &&
                table.Close(descriptor, release_result) == os::kernel::FileTableStatus::Succeeded;
            if (operation_valid) {
                records[descriptor] = DescriptorRecord{};
                --active_descriptor_count;
                os::kernel::KernelObjectReference stale_lookup{};
                operation_valid = table.Lookup(descriptor, stale_lookup) ==
                                      os::kernel::FileTableStatus::InvalidDescriptor &&
                                  !stale_lookup.IsActive();
            }
        } else {
            ++limit_operation_count;
            const uint64_t new_soft_limit =
                NextRandom(random_state) % OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT +
                OS_TEST_FILE_TABLE_RANDOM_FIRST_VALUE;
            operation_valid =
                table.SetSoftLimit(new_soft_limit) == os::kernel::FileTableStatus::Succeeded;
            if (operation_valid) {
                soft_limit = new_soft_limit;
            }
        }

        const os::kernel::FileTableStatistics table_statistics = table.Statistics();
        const os::kernel::KernelObjectManagerStatistics object_statistics =
            object_manager.Statistics();
        const bool validation_due = iteration % OS_TEST_FILE_TABLE_RANDOM_VALIDATION_INTERVAL ==
                                    OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
        operation_valid =
            operation_valid && table_statistics.soft_limit == soft_limit &&
            table_statistics.hard_limit == OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT &&
            table_statistics.active_descriptor_count == active_descriptor_count &&
            object_statistics.active_strong_reference_count == active_descriptor_count &&
            object_statistics.active_object_count <= active_descriptor_count &&
            (!validation_due ||
             (table.Validate() == os::kernel::FileTableStatus::Succeeded &&
              object_manager.Validate() == os::kernel::KernelObjectStatus::Succeeded &&
              heap.Validate() == os::kernel::KernelHeapStatus::Succeeded));
        test_context.ExpectRandom(operation_valid, OS_TEST_FILE_TABLE_RANDOM_OPERATION,
                                  OS_TEST_FILE_TABLE_RANDOM_SEED, iteration);
    }

    bool closed = true;
    for (uint64_t descriptor = OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
         descriptor < OS_TEST_FILE_TABLE_RANDOM_HARD_LIMIT; ++descriptor) {
        if (!records[descriptor].active) {
            continue;
        }
        os::kernel::KernelObjectReleaseResult release_result{};
        closed = closed &&
                 table.Close(descriptor, release_result) == os::kernel::FileTableStatus::Succeeded;
        records[descriptor] = DescriptorRecord{};
    }
    const bool drained =
        closed && open_operation_count > OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE &&
        duplicate_operation_count > OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE &&
        close_operation_count > OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE &&
        limit_operation_count > OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE &&
        table.Statistics().active_descriptor_count == OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE &&
        object_manager.Statistics().active_object_count == OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE &&
        table.Destroy() == os::kernel::FileTableStatus::Succeeded &&
        object_manager.Validate() == os::kernel::KernelObjectStatus::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_FILE_TABLE_RANDOM_EMPTY_VALUE;
    test_context.Expect(drained, OS_TEST_FILE_TABLE_RANDOM_DRAIN);
    return test_context.ExitCode();
}
