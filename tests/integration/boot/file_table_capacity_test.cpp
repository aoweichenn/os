#include "os/kernel/io/file_description.hpp"
#include "os/kernel/io/file_table.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "os/kernel/object/kernel_object.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_TABLE_CAPACITY_SUITE_NAME =
    "kernel/file_table/capacity/integration";
constexpr std::string_view OS_TEST_FILE_TABLE_CAPACITY_FILL =
    "文件表必须按 64 项分块懒增长到 4096 个描述符";
constexpr std::string_view OS_TEST_FILE_TABLE_CAPACITY_EXHAUSTION =
    "硬上限耗尽必须返回稳定错误且不能产生半安装引用";
constexpr std::string_view OS_TEST_FILE_TABLE_CAPACITY_DRAIN =
    "关闭 4096 个描述符后必须完整回收对象和全部分块";

constexpr uint64_t OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_CAPACITY_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT =
    os::kernel::OS_KERNEL_FILE_TABLE_MAXIMUM_HARD_LIMIT;
constexpr uint64_t OS_TEST_FILE_TABLE_CAPACITY_FIRST_DUPLICATE_DESCRIPTOR = 1ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_CAPACITY_EXPECTED_CHUNK_COUNT =
    OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT /
    os::kernel::OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT;

[[nodiscard]] bool DiscardWrite(void *const context, const uint8_t *const source,
                                const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    static_cast<void>(context);
    written_bytes = OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE;
    if (source == nullptr && length_bytes != OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE) {
        return false;
    }
    written_bytes = length_bytes;
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_TABLE_CAPACITY_SUITE_NAME};
    alignas(64) static uint8_t heap_buffer[OS_TEST_FILE_TABLE_CAPACITY_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::KernelObjectManager object_manager{};
    os::kernel::FileDescriptionManager description_manager{};
    os::kernel::FileTable table{};
    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                        OS_TEST_FILE_TABLE_CAPACITY_HEAP_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        object_manager.Initialize(heap) == os::kernel::KernelObjectStatus::Succeeded &&
        description_manager.Initialize(object_manager) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.Initialize(heap, object_manager, OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT,
                         OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT) ==
            os::kernel::FileTableStatus::Succeeded;

    const os::kernel::FileDescriptionCreateRequest request{
        .kind = os::kernel::FileDescriptionKind::ConsoleOutput,
        .file_status_flags = os::kernel::OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
        .console_input = nullptr,
        .device_write_operation = DiscardWrite,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .file_system = nullptr,
        .file_system_handle = {},
    };
    os::kernel::KernelObjectReference root_reference{};
    bool filled = initialized &&
                  description_manager.Create(request, root_reference) ==
                      os::kernel::FileDescriptionStatus::Succeeded &&
                  table.InstallExact(root_reference, OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE,
                                     OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE) ==
                      os::kernel::FileTableStatus::Succeeded;
    for (uint64_t expected_descriptor = OS_TEST_FILE_TABLE_CAPACITY_FIRST_DUPLICATE_DESCRIPTOR;
         expected_descriptor < OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT; ++expected_descriptor) {
        uint64_t descriptor = UINT64_MAX;
        filled = filled &&
                 table.Duplicate(OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE,
                                 OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE,
                                 OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE,
                                 descriptor) == os::kernel::FileTableStatus::Succeeded &&
                 descriptor == expected_descriptor;
        if (!filled) {
            break;
        }
    }
    const os::kernel::FileTableStatistics filled_statistics = table.Statistics();
    test_context.Expect(filled && table.Validate() == os::kernel::FileTableStatus::Succeeded &&
                            filled_statistics.active_descriptor_count ==
                                OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT &&
                            filled_statistics.allocated_chunk_count ==
                                OS_TEST_FILE_TABLE_CAPACITY_EXPECTED_CHUNK_COUNT &&
                            object_manager.Statistics().active_strong_reference_count ==
                                OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT,
                        OS_TEST_FILE_TABLE_CAPACITY_FILL);

    uint64_t unavailable_descriptor = UINT64_MAX;
    const os::kernel::KernelObjectManagerStatistics before_exhaustion = object_manager.Statistics();
    test_context.Expect(
        table.Duplicate(OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE,
                        OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE,
                        OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE,
                        unavailable_descriptor) == os::kernel::FileTableStatus::SoftLimitExceeded &&
            unavailable_descriptor == UINT64_MAX &&
            object_manager.Statistics().active_strong_reference_count ==
                before_exhaustion.active_strong_reference_count &&
            table.Statistics().active_descriptor_count == OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT,
        OS_TEST_FILE_TABLE_CAPACITY_EXHAUSTION);

    bool closed = true;
    for (uint64_t descriptor = OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE;
         descriptor < OS_TEST_FILE_TABLE_CAPACITY_HARD_LIMIT; ++descriptor) {
        os::kernel::KernelObjectReleaseResult release_result{};
        closed = closed &&
                 table.Close(descriptor, release_result) == os::kernel::FileTableStatus::Succeeded;
    }
    const bool drained =
        closed &&
        table.Statistics().active_descriptor_count == OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE &&
        object_manager.Statistics().active_object_count ==
            OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE &&
        table.Destroy() == os::kernel::FileTableStatus::Succeeded &&
        table.Statistics().chunk_release_count ==
            OS_TEST_FILE_TABLE_CAPACITY_EXPECTED_CHUNK_COUNT &&
        object_manager.Validate() == os::kernel::KernelObjectStatus::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_FILE_TABLE_CAPACITY_EMPTY_VALUE;
    test_context.Expect(drained, OS_TEST_FILE_TABLE_CAPACITY_DRAIN);
    return test_context.ExitCode();
}
