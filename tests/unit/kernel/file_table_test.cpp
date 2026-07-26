#include "os/kernel/io/file_description.hpp"
#include "os/kernel/io/file_table.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "os/kernel/object/kernel_object.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_TABLE_SUITE_NAME = "kernel/file_table/unit";
constexpr std::string_view OS_TEST_FILE_TABLE_DEPENDENCIES =
    "对象管理器和文件表必须拒绝未初始化依赖及非法限额";
constexpr std::string_view OS_TEST_FILE_TABLE_OWNERSHIP =
    "安装必须转移强引用且查找临时引用必须成对释放";
constexpr std::string_view OS_TEST_FILE_TABLE_FOREIGN_REFERENCE =
    "文件表必须拒绝其他对象管理器的引用且不能留下候选分块";
constexpr std::string_view OS_TEST_FILE_TABLE_DUPLICATE =
    "复制描述符必须共享同一文件描述且保持独立描述符标志";
constexpr std::string_view OS_TEST_FILE_TABLE_REUSE =
    "关闭后的最小描述符必须复用且对象代次不能混淆";
constexpr std::string_view OS_TEST_FILE_TABLE_LIMIT = "软限额必须只限制新安装并保持失败原子性";
constexpr std::string_view OS_TEST_FILE_TABLE_CLOSE_ON_EXEC =
    "执行时关闭只能回收设置了 close-on-exec 的描述符";
constexpr std::string_view OS_TEST_FILE_TABLE_DRAIN =
    "销毁文件表必须释放全部分块、描述符和内核对象";

constexpr uint64_t OS_TEST_FILE_TABLE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_HEAP_SIZE_BYTES = 256ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_SOFT_LIMIT = 130ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_HARD_LIMIT =
    os::kernel::OS_KERNEL_FILE_TABLE_FUNCTIONAL_HARD_LIMIT;
constexpr uint64_t OS_TEST_FILE_TABLE_DUPLICATE_MINIMUM = 64ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_RESTRICTED_SOFT_LIMIT = 64ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_FOREIGN_REFERENCE_MINIMUM = 128ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_INVALID_FLAGS = 1ULL << 7ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_EXPECTED_CHUNK_COUNT = 2ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_EXPECTED_LOOKUP_REFERENCE_COUNT = 2ULL;
constexpr uint64_t OS_TEST_FILE_TABLE_EXPECTED_CLOSE_ON_EXEC_COUNT = 1ULL;

[[nodiscard]] bool DiscardWrite(void *const context, const uint8_t *const source,
                                const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    static_cast<void>(context);
    written_bytes = OS_TEST_FILE_TABLE_EMPTY_VALUE;
    if (source == nullptr && length_bytes != OS_TEST_FILE_TABLE_EMPTY_VALUE) {
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

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_TABLE_SUITE_NAME};
    alignas(64) static uint8_t heap_buffer[OS_TEST_FILE_TABLE_HEAP_SIZE_BYTES]{};

    os::kernel::KernelHeap uninitialized_heap{};
    os::kernel::KernelObjectManager invalid_object_manager{};
    os::kernel::FileTable invalid_table{};
    test_context.Expect(invalid_object_manager.Initialize(uninitialized_heap) ==
                                os::kernel::KernelObjectStatus::InvalidDependency &&
                            invalid_table.Initialize(uninitialized_heap, invalid_object_manager,
                                                     OS_TEST_FILE_TABLE_SOFT_LIMIT,
                                                     OS_TEST_FILE_TABLE_HARD_LIMIT) ==
                                os::kernel::FileTableStatus::InvalidDependency,
                        OS_TEST_FILE_TABLE_DEPENDENCIES);

    os::kernel::KernelHeap heap{};
    os::kernel::KernelObjectManager object_manager{};
    os::kernel::FileDescriptionManager description_manager{};
    os::kernel::FileTable table{};
    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                        OS_TEST_FILE_TABLE_HEAP_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        object_manager.Initialize(heap) == os::kernel::KernelObjectStatus::Succeeded &&
        description_manager.Initialize(object_manager) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.Initialize(heap, object_manager, OS_TEST_FILE_TABLE_EMPTY_VALUE,
                         OS_TEST_FILE_TABLE_HARD_LIMIT) ==
            os::kernel::FileTableStatus::InvalidLimit &&
        table.Initialize(heap, object_manager, OS_TEST_FILE_TABLE_SOFT_LIMIT,
                         OS_TEST_FILE_TABLE_HARD_LIMIT) == os::kernel::FileTableStatus::Succeeded;
    test_context.Expect(initialized, OS_TEST_FILE_TABLE_DEPENDENCIES);

    os::kernel::KernelObjectReference root_reference{};
    os::kernel::KernelObjectIdentity root_identity{};
    const bool installed =
        CreateOutputDescription(description_manager, root_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        root_reference.ReadIdentity(root_identity) == os::kernel::KernelObjectStatus::Succeeded &&
        table.InstallExact(
            root_reference, os::kernel::OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR,
            OS_TEST_FILE_TABLE_EMPTY_VALUE) == os::kernel::FileTableStatus::Succeeded &&
        !root_reference.IsActive();
    os::kernel::KernelObjectReference lookup_reference{};
    os::kernel::KernelObjectIdentity lookup_identity{};
    const bool lookup_valid =
        table.Lookup(os::kernel::OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR,
                     lookup_reference) == os::kernel::FileTableStatus::Succeeded &&
        lookup_reference.ReadIdentity(lookup_identity) ==
            os::kernel::KernelObjectStatus::Succeeded &&
        lookup_identity.generation == root_identity.generation &&
        lookup_identity.strong_reference_count >=
            OS_TEST_FILE_TABLE_EXPECTED_LOOKUP_REFERENCE_COUNT &&
        lookup_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    test_context.Expect(installed && lookup_valid, OS_TEST_FILE_TABLE_OWNERSHIP);

    os::kernel::KernelObjectManager foreign_object_manager{};
    os::kernel::FileDescriptionManager foreign_description_manager{};
    os::kernel::KernelObjectReference foreign_reference{};
    uint64_t foreign_descriptor = UINT64_MAX;
    const uint64_t chunks_before_foreign_install = table.Statistics().allocated_chunk_count;
    const bool foreign_reference_rejected =
        foreign_object_manager.Initialize(heap) == os::kernel::KernelObjectStatus::Succeeded &&
        foreign_description_manager.Initialize(foreign_object_manager) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        CreateOutputDescription(foreign_description_manager, foreign_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.Install(foreign_reference, OS_TEST_FILE_TABLE_FOREIGN_REFERENCE_MINIMUM,
                      OS_TEST_FILE_TABLE_EMPTY_VALUE,
                      foreign_descriptor) == os::kernel::FileTableStatus::ObjectFailure &&
        foreign_reference.IsActive() && foreign_descriptor == UINT64_MAX &&
        table.Statistics().allocated_chunk_count == chunks_before_foreign_install &&
        foreign_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        foreign_object_manager.Validate() == os::kernel::KernelObjectStatus::Succeeded;
    test_context.Expect(foreign_reference_rejected, OS_TEST_FILE_TABLE_FOREIGN_REFERENCE);

    uint64_t duplicate_descriptor = UINT64_MAX;
    const bool duplicated =
        table.Duplicate(os::kernel::OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR,
                        OS_TEST_FILE_TABLE_DUPLICATE_MINIMUM,
                        os::kernel::OS_KERNEL_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG,
                        duplicate_descriptor) == os::kernel::FileTableStatus::Succeeded &&
        duplicate_descriptor == OS_TEST_FILE_TABLE_DUPLICATE_MINIMUM;
    uint64_t source_flags = UINT64_MAX;
    uint64_t duplicate_flags = OS_TEST_FILE_TABLE_EMPTY_VALUE;
    os::kernel::KernelObjectReference duplicate_reference{};
    os::kernel::KernelObjectIdentity duplicate_identity{};
    const bool duplicate_shared =
        table.GetDescriptorFlags(os::kernel::OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR,
                                 source_flags) == os::kernel::FileTableStatus::Succeeded &&
        source_flags == OS_TEST_FILE_TABLE_EMPTY_VALUE &&
        table.GetDescriptorFlags(duplicate_descriptor, duplicate_flags) ==
            os::kernel::FileTableStatus::Succeeded &&
        duplicate_flags == os::kernel::OS_KERNEL_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG &&
        table.Lookup(duplicate_descriptor, duplicate_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        duplicate_reference.ReadIdentity(duplicate_identity) ==
            os::kernel::KernelObjectStatus::Succeeded &&
        duplicate_identity.generation == root_identity.generation &&
        duplicate_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    test_context.Expect(duplicated && duplicate_shared, OS_TEST_FILE_TABLE_DUPLICATE);

    os::kernel::KernelObjectReference occupied_reference{};
    const bool occupied_install_atomic =
        CreateOutputDescription(description_manager, occupied_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.InstallExact(
            occupied_reference, os::kernel::OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR,
            OS_TEST_FILE_TABLE_EMPTY_VALUE) == os::kernel::FileTableStatus::DescriptorOccupied &&
        occupied_reference.IsActive() &&
        occupied_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;

    os::kernel::KernelObjectReleaseResult release_result{};
    const bool source_closed =
        table.Close(os::kernel::OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR, release_result) ==
            os::kernel::FileTableStatus::Succeeded &&
        !release_result.released_last_reference;
    os::kernel::KernelObjectReference replacement_reference{};
    os::kernel::KernelObjectIdentity replacement_identity{};
    uint64_t replacement_descriptor = UINT64_MAX;
    const bool descriptor_reused =
        CreateOutputDescription(description_manager, replacement_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        replacement_reference.ReadIdentity(replacement_identity) ==
            os::kernel::KernelObjectStatus::Succeeded &&
        table.Install(replacement_reference, OS_TEST_FILE_TABLE_EMPTY_VALUE,
                      OS_TEST_FILE_TABLE_EMPTY_VALUE,
                      replacement_descriptor) == os::kernel::FileTableStatus::Succeeded &&
        replacement_descriptor == os::kernel::OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR &&
        replacement_identity.generation != root_identity.generation &&
        table.Close(replacement_descriptor, release_result) ==
            os::kernel::FileTableStatus::Succeeded &&
        release_result.released_last_reference;
    test_context.Expect(occupied_install_atomic && source_closed && descriptor_reused,
                        OS_TEST_FILE_TABLE_REUSE);

    os::kernel::KernelObjectReference limited_reference{};
    uint64_t limited_descriptor = UINT64_MAX;
    const bool limit_atomic =
        CreateOutputDescription(description_manager, limited_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.SetSoftLimit(OS_TEST_FILE_TABLE_RESTRICTED_SOFT_LIMIT) ==
            os::kernel::FileTableStatus::Succeeded &&
        table.Install(limited_reference, OS_TEST_FILE_TABLE_DUPLICATE_MINIMUM,
                      OS_TEST_FILE_TABLE_EMPTY_VALUE,
                      limited_descriptor) == os::kernel::FileTableStatus::SoftLimitExceeded &&
        limited_reference.IsActive() && limited_descriptor == UINT64_MAX &&
        table.SetDescriptorFlags(duplicate_descriptor, OS_TEST_FILE_TABLE_INVALID_FLAGS) ==
            os::kernel::FileTableStatus::InvalidFlags &&
        table.SetSoftLimit(OS_TEST_FILE_TABLE_SOFT_LIMIT) ==
            os::kernel::FileTableStatus::Succeeded &&
        limited_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    test_context.Expect(limit_atomic, OS_TEST_FILE_TABLE_LIMIT);

    uint64_t closed_descriptor_count = OS_TEST_FILE_TABLE_EMPTY_VALUE;
    const bool closed_on_exec =
        table.CloseOnExec(closed_descriptor_count) == os::kernel::FileTableStatus::Succeeded &&
        closed_descriptor_count == OS_TEST_FILE_TABLE_EXPECTED_CLOSE_ON_EXEC_COUNT &&
        table.Lookup(duplicate_descriptor, duplicate_reference) ==
            os::kernel::FileTableStatus::InvalidDescriptor &&
        table.Validate() == os::kernel::FileTableStatus::Succeeded;
    test_context.Expect(closed_on_exec, OS_TEST_FILE_TABLE_CLOSE_ON_EXEC);

    const os::kernel::FileTableStatistics before_destroy = table.Statistics();
    const bool drained =
        before_destroy.active_descriptor_count == OS_TEST_FILE_TABLE_EMPTY_VALUE &&
        before_destroy.allocated_chunk_count == OS_TEST_FILE_TABLE_EXPECTED_CHUNK_COUNT &&
        table.Destroy() == os::kernel::FileTableStatus::Succeeded &&
        object_manager.Validate() == os::kernel::KernelObjectStatus::Succeeded &&
        object_manager.Statistics().active_object_count == OS_TEST_FILE_TABLE_EMPTY_VALUE &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_FILE_TABLE_EMPTY_VALUE &&
        table.Statistics().chunk_release_count == OS_TEST_FILE_TABLE_EXPECTED_CHUNK_COUNT;
    test_context.Expect(drained, OS_TEST_FILE_TABLE_DRAIN);
    return test_context.ExitCode();
}
