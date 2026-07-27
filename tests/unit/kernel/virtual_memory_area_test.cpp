#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "os/kernel/memory/virtual_memory_area.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VMA_SUITE_NAME = "kernel/virtual_memory_area/unit";
constexpr std::string_view OS_TEST_VMA_INITIALIZATION =
    "初始化必须拒绝无效页大小、空容量和非法硬限制";
constexpr std::string_view OS_TEST_VMA_ORDERING = "VMA 必须按地址排序并拒绝重叠和未对齐区间";
constexpr std::string_view OS_TEST_VMA_MERGE = "属性相同的相邻 VMA 必须双向合并并回收描述符";
constexpr std::string_view OS_TEST_VMA_SPLIT = "区间中部解除映射必须切分且保留两侧属性";
constexpr std::string_view OS_TEST_VMA_KIND_GUARD = "解除映射跨越不同类型时必须失败且不修改原映射";
constexpr std::string_view OS_TEST_VMA_GAP = "首个空洞查找必须同时满足窗口、长度和对齐约束";
constexpr std::string_view OS_TEST_VMA_FAILURE_ATOMIC =
    "切分需要的新描述符不可用时必须保持映射完全不变";
constexpr std::string_view OS_TEST_VMA_FILE_BACKING =
    "文件 VMA 必须校验后端身份并在切分时精确重定位文件区间";
constexpr std::string_view OS_TEST_VMA_DESTROY = "销毁地址空间必须把全部共享池描述符归还到基线";

constexpr uint64_t OS_TEST_VMA_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VMA_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_VMA_HALF_DIVISOR = 2ULL;
constexpr uint64_t OS_TEST_VMA_PAGE_SIZE_BYTES = os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_VMA_POOL_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_VMA_MAP_HARD_LIMIT = 12ULL;
constexpr uint64_t OS_TEST_VMA_WINDOW_BEGIN_ADDRESS = 0x40000000ULL;
constexpr uint64_t OS_TEST_VMA_WINDOW_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_TEST_VMA_FIRST_BEGIN_PAGE_INDEX = 4ULL;
constexpr uint64_t OS_TEST_VMA_FIRST_END_PAGE_INDEX = 8ULL;
constexpr uint64_t OS_TEST_VMA_SECOND_BEGIN_PAGE_INDEX = 12ULL;
constexpr uint64_t OS_TEST_VMA_SECOND_END_PAGE_INDEX = 16ULL;
constexpr uint64_t OS_TEST_VMA_MERGE_BEGIN_PAGE_INDEX = 8ULL;
constexpr uint64_t OS_TEST_VMA_MERGE_END_PAGE_INDEX = 12ULL;
constexpr uint64_t OS_TEST_VMA_SPLIT_BEGIN_PAGE_INDEX = 6ULL;
constexpr uint64_t OS_TEST_VMA_SPLIT_END_PAGE_INDEX = 10ULL;
constexpr uint64_t OS_TEST_VMA_EXPECTED_SPLIT_AREA_COUNT = 3ULL;
constexpr uint64_t OS_TEST_VMA_EXPECTED_MERGED_AREA_COUNT = OS_TEST_VMA_SINGLE_UNIT;
constexpr uint64_t OS_TEST_VMA_EXPECTED_REMOVED_MIDDLE_AREA_COUNT = 2ULL;
constexpr uint64_t OS_TEST_VMA_GAP_LENGTH_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_TEST_VMA_GAP_ALIGNMENT_PAGE_COUNT = 4ULL;
constexpr uint64_t OS_TEST_VMA_EXPECTED_GAP_PAGE_INDEX = OS_TEST_VMA_EMPTY_VALUE;
constexpr uint64_t OS_TEST_VMA_TINY_POOL_CAPACITY = 1ULL;
constexpr uint64_t OS_TEST_VMA_FILE_POOL_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_VMA_FILE_DESCRIPTOR_INDEX = 7ULL;
constexpr uint64_t OS_TEST_VMA_FILE_BACKING_GENERATION = 11ULL;
constexpr uint64_t OS_TEST_VMA_FILE_OFFSET_PAGE_INDEX = 19ULL;

[[nodiscard]] uint64_t PageAddress(const uint64_t page_index) noexcept {
    return OS_TEST_VMA_WINDOW_BEGIN_ADDRESS + page_index * OS_TEST_VMA_PAGE_SIZE_BYTES;
}

[[nodiscard]] os::kernel::VirtualMemoryArea
MakeArea(const uint64_t begin_page_index, const uint64_t end_page_index,
         const os::kernel::VirtualMemoryAreaKind kind,
         const os::kernel::VirtualMemoryAreaPermissions permissions) noexcept {
    os::kernel::VirtualMemoryArea area{
        .begin_address = PageAddress(begin_page_index),
        .end_address = PageAddress(end_page_index),
        .permissions = permissions,
        .kind = kind,
    };
    if (os::kernel::IsFileBackedVirtualMemoryAreaKind(kind)) {
        area.backing_descriptor_index = OS_TEST_VMA_FILE_DESCRIPTOR_INDEX;
        area.backing_generation = OS_TEST_VMA_FILE_BACKING_GENERATION;
        area.backing_file_offset_bytes =
            OS_TEST_VMA_FILE_OFFSET_PAGE_INDEX * OS_TEST_VMA_PAGE_SIZE_BYTES;
        area.backing_data_length_bytes =
            area.end_address - area.begin_address;
    }
    return area;
}

[[nodiscard]] bool AreasEqual(const os::kernel::VirtualMemoryArea &left,
                              const os::kernel::VirtualMemoryArea &right) noexcept {
    return left.begin_address == right.begin_address && left.end_address == right.end_address &&
           left.permissions.readable == right.permissions.readable &&
           left.permissions.writable == right.permissions.writable &&
           left.permissions.executable == right.permissions.executable && left.kind == right.kind &&
           left.backing_descriptor_index == right.backing_descriptor_index &&
           left.backing_generation == right.backing_generation &&
           left.backing_file_offset_bytes == right.backing_file_offset_bytes &&
           left.backing_data_length_bytes == right.backing_data_length_bytes;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VMA_SUITE_NAME};
    os::kernel::VirtualMemoryAreaDescriptor descriptors[OS_TEST_VMA_POOL_CAPACITY]{};
    os::kernel::VirtualMemoryAreaPool pool{};
    os::kernel::VirtualMemoryMap map{};

    os::kernel::VirtualMemoryAreaPool invalid_pool{};
    os::kernel::VirtualMemoryMap invalid_map{};
    const bool initialization_valid =
        invalid_pool.Initialize(nullptr, OS_TEST_VMA_POOL_CAPACITY) ==
            os::kernel::VirtualMemoryAreaStatus::InvalidStorage &&
        invalid_pool.Initialize(descriptors, OS_TEST_VMA_EMPTY_VALUE) ==
            os::kernel::VirtualMemoryAreaStatus::InvalidCapacity &&
        pool.Initialize(descriptors, OS_TEST_VMA_POOL_CAPACITY) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        invalid_map.Initialize(
            pool,
            OS_TEST_VMA_PAGE_SIZE_BYTES + OS_TEST_VMA_PAGE_SIZE_BYTES / OS_TEST_VMA_HALF_DIVISOR,
            OS_TEST_VMA_MAP_HARD_LIMIT) == os::kernel::VirtualMemoryAreaStatus::InvalidAlignment &&
        invalid_map.Initialize(pool, OS_TEST_VMA_PAGE_SIZE_BYTES, OS_TEST_VMA_EMPTY_VALUE) ==
            os::kernel::VirtualMemoryAreaStatus::InvalidHardLimit &&
        map.Initialize(pool, OS_TEST_VMA_PAGE_SIZE_BYTES, OS_TEST_VMA_MAP_HARD_LIMIT) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded;
    test_context.Expect(initialization_valid, OS_TEST_VMA_INITIALIZATION);

    constexpr os::kernel::VirtualMemoryAreaPermissions writable_permissions{
        .readable = true,
        .writable = true,
        .executable = false,
    };
    constexpr os::kernel::VirtualMemoryAreaPermissions read_only_permissions{
        .readable = true,
        .writable = false,
        .executable = false,
    };
    const os::kernel::VirtualMemoryArea first_area =
        MakeArea(OS_TEST_VMA_FIRST_BEGIN_PAGE_INDEX, OS_TEST_VMA_FIRST_END_PAGE_INDEX,
                 os::kernel::VirtualMemoryAreaKind::Anonymous, writable_permissions);
    const os::kernel::VirtualMemoryArea second_area =
        MakeArea(OS_TEST_VMA_SECOND_BEGIN_PAGE_INDEX, OS_TEST_VMA_SECOND_END_PAGE_INDEX,
                 os::kernel::VirtualMemoryAreaKind::Anonymous, writable_permissions);
    os::kernel::VirtualMemoryArea first_observed_area{};
    os::kernel::VirtualMemoryArea second_observed_area{};
    const bool ordering_valid =
        map.Insert(second_area) == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        map.Insert(first_area) == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        map.Insert(MakeArea(OS_TEST_VMA_FIRST_BEGIN_PAGE_INDEX + OS_TEST_VMA_SINGLE_UNIT,
                            OS_TEST_VMA_SECOND_BEGIN_PAGE_INDEX + OS_TEST_VMA_SINGLE_UNIT,
                            os::kernel::VirtualMemoryAreaKind::Anonymous, writable_permissions)) ==
            os::kernel::VirtualMemoryAreaStatus::Overlap &&
        map.Insert(os::kernel::VirtualMemoryArea{
            .begin_address = first_area.begin_address + OS_TEST_VMA_SINGLE_UNIT,
            .end_address = first_area.end_address,
            .permissions = writable_permissions,
            .kind = os::kernel::VirtualMemoryAreaKind::Anonymous,
        }) == os::kernel::VirtualMemoryAreaStatus::InvalidRange &&
        map.ReadAt(OS_TEST_VMA_EMPTY_VALUE, first_observed_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        map.ReadAt(OS_TEST_VMA_SINGLE_UNIT, second_observed_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        AreasEqual(first_observed_area, first_area) &&
        AreasEqual(second_observed_area, second_area) &&
        map.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded;
    test_context.Expect(ordering_valid, OS_TEST_VMA_ORDERING);

    const os::kernel::VirtualMemoryArea bridge_area =
        MakeArea(OS_TEST_VMA_MERGE_BEGIN_PAGE_INDEX, OS_TEST_VMA_MERGE_END_PAGE_INDEX,
                 os::kernel::VirtualMemoryAreaKind::Anonymous, writable_permissions);
    const bool merge_valid =
        map.Insert(bridge_area) == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        map.AreaCount() == OS_TEST_VMA_EXPECTED_MERGED_AREA_COUNT &&
        map.ReadAt(OS_TEST_VMA_EMPTY_VALUE, first_observed_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        first_observed_area.begin_address == first_area.begin_address &&
        first_observed_area.end_address == second_area.end_address &&
        pool.Statistics().active_descriptor_count == OS_TEST_VMA_EXPECTED_MERGED_AREA_COUNT &&
        map.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded;
    test_context.Expect(merge_valid, OS_TEST_VMA_MERGE);

    const bool split_valid =
        map.Remove(PageAddress(OS_TEST_VMA_SPLIT_BEGIN_PAGE_INDEX),
                   PageAddress(OS_TEST_VMA_SPLIT_END_PAGE_INDEX),
                   os::kernel::VirtualMemoryAreaKind::Anonymous) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        map.AreaCount() == OS_TEST_VMA_EXPECTED_REMOVED_MIDDLE_AREA_COUNT &&
        map.ReadAt(OS_TEST_VMA_EMPTY_VALUE, first_observed_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        map.ReadAt(OS_TEST_VMA_SINGLE_UNIT, second_observed_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        first_observed_area.begin_address == first_area.begin_address &&
        first_observed_area.end_address == PageAddress(OS_TEST_VMA_SPLIT_BEGIN_PAGE_INDEX) &&
        second_observed_area.begin_address == PageAddress(OS_TEST_VMA_SPLIT_END_PAGE_INDEX) &&
        second_observed_area.end_address == second_area.end_address &&
        map.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded;
    test_context.Expect(split_valid, OS_TEST_VMA_SPLIT);

    const os::kernel::VirtualMemoryArea protected_area =
        MakeArea(OS_TEST_VMA_SPLIT_BEGIN_PAGE_INDEX, OS_TEST_VMA_SPLIT_END_PAGE_INDEX,
                 os::kernel::VirtualMemoryAreaKind::ExecutableImage, read_only_permissions);
    const bool kind_guard_valid =
        map.Insert(protected_area) == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        map.AreaCount() == OS_TEST_VMA_EXPECTED_SPLIT_AREA_COUNT &&
        map.Remove(first_area.begin_address, second_area.end_address,
                   os::kernel::VirtualMemoryAreaKind::Anonymous) ==
            os::kernel::VirtualMemoryAreaStatus::KindMismatch &&
        map.AreaCount() == OS_TEST_VMA_EXPECTED_SPLIT_AREA_COUNT &&
        map.ReadAt(OS_TEST_VMA_SINGLE_UNIT, first_observed_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        AreasEqual(first_observed_area, protected_area) &&
        map.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded;
    test_context.Expect(kind_guard_valid, OS_TEST_VMA_KIND_GUARD);

    uint64_t gap_begin_address = UINT64_MAX;
    const bool gap_valid =
        map.FindFirstGap(OS_TEST_VMA_WINDOW_BEGIN_ADDRESS,
                         PageAddress(OS_TEST_VMA_WINDOW_PAGE_COUNT),
                         OS_TEST_VMA_GAP_LENGTH_PAGE_COUNT * OS_TEST_VMA_PAGE_SIZE_BYTES,
                         OS_TEST_VMA_GAP_ALIGNMENT_PAGE_COUNT * OS_TEST_VMA_PAGE_SIZE_BYTES,
                         gap_begin_address) == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        gap_begin_address == PageAddress(OS_TEST_VMA_EXPECTED_GAP_PAGE_INDEX);
    test_context.Expect(gap_valid, OS_TEST_VMA_GAP);

    os::kernel::VirtualMemoryAreaDescriptor tiny_descriptors[OS_TEST_VMA_TINY_POOL_CAPACITY]{};
    os::kernel::VirtualMemoryAreaPool tiny_pool{};
    os::kernel::VirtualMemoryMap tiny_map{};
    os::kernel::VirtualMemoryArea unchanged_area{};
    const os::kernel::VirtualMemoryArea whole_tiny_area =
        MakeArea(OS_TEST_VMA_FIRST_BEGIN_PAGE_INDEX, OS_TEST_VMA_SECOND_END_PAGE_INDEX,
                 os::kernel::VirtualMemoryAreaKind::Anonymous, writable_permissions);
    const bool failure_atomic_valid =
        tiny_pool.Initialize(tiny_descriptors, OS_TEST_VMA_TINY_POOL_CAPACITY) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        tiny_map.Initialize(tiny_pool, OS_TEST_VMA_PAGE_SIZE_BYTES,
                            OS_TEST_VMA_TINY_POOL_CAPACITY) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        tiny_map.Insert(whole_tiny_area) == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        tiny_map.Remove(PageAddress(OS_TEST_VMA_SPLIT_BEGIN_PAGE_INDEX),
                        PageAddress(OS_TEST_VMA_SPLIT_END_PAGE_INDEX),
                        os::kernel::VirtualMemoryAreaKind::Anonymous) ==
            os::kernel::VirtualMemoryAreaStatus::AreaLimitExceeded &&
        tiny_map.AreaCount() == OS_TEST_VMA_EXPECTED_MERGED_AREA_COUNT &&
        tiny_map.ReadAt(OS_TEST_VMA_EMPTY_VALUE, unchanged_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        AreasEqual(unchanged_area, whole_tiny_area) &&
        tiny_map.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded;
    test_context.Expect(failure_atomic_valid, OS_TEST_VMA_FAILURE_ATOMIC);

    os::kernel::VirtualMemoryAreaDescriptor
        file_descriptors[OS_TEST_VMA_FILE_POOL_CAPACITY]{};
    os::kernel::VirtualMemoryAreaPool file_pool{};
    os::kernel::VirtualMemoryMap file_map{};
    os::kernel::VirtualMemoryArea invalid_file_area =
        MakeArea(OS_TEST_VMA_FIRST_BEGIN_PAGE_INDEX,
                 OS_TEST_VMA_SECOND_END_PAGE_INDEX,
                 os::kernel::VirtualMemoryAreaKind::FilePrivate,
                 read_only_permissions);
    invalid_file_area.backing_generation = OS_TEST_VMA_EMPTY_VALUE;
    const os::kernel::VirtualMemoryArea whole_file_area =
        MakeArea(OS_TEST_VMA_FIRST_BEGIN_PAGE_INDEX,
                 OS_TEST_VMA_SECOND_END_PAGE_INDEX,
                 os::kernel::VirtualMemoryAreaKind::FilePrivate,
                 read_only_permissions);
    os::kernel::VirtualMemoryArea left_file_area{};
    os::kernel::VirtualMemoryArea right_file_area{};
    const bool file_backing_valid =
        file_pool.Initialize(file_descriptors,
                             OS_TEST_VMA_FILE_POOL_CAPACITY) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        file_map.Initialize(file_pool, OS_TEST_VMA_PAGE_SIZE_BYTES,
                            OS_TEST_VMA_FILE_POOL_CAPACITY) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        file_map.Insert(invalid_file_area) ==
            os::kernel::VirtualMemoryAreaStatus::InvalidBacking &&
        file_map.Insert(whole_file_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        file_map.Remove(PageAddress(OS_TEST_VMA_SPLIT_BEGIN_PAGE_INDEX),
                        PageAddress(OS_TEST_VMA_SPLIT_END_PAGE_INDEX),
                        os::kernel::VirtualMemoryAreaKind::FilePrivate) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        file_map.ReadAt(OS_TEST_VMA_EMPTY_VALUE, left_file_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        file_map.ReadAt(OS_TEST_VMA_SINGLE_UNIT, right_file_area) ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        left_file_area.backing_file_offset_bytes ==
            whole_file_area.backing_file_offset_bytes &&
        left_file_area.backing_data_length_bytes ==
            (OS_TEST_VMA_SPLIT_BEGIN_PAGE_INDEX -
             OS_TEST_VMA_FIRST_BEGIN_PAGE_INDEX) *
                OS_TEST_VMA_PAGE_SIZE_BYTES &&
        right_file_area.backing_file_offset_bytes ==
            whole_file_area.backing_file_offset_bytes +
                (OS_TEST_VMA_SPLIT_END_PAGE_INDEX -
                 OS_TEST_VMA_FIRST_BEGIN_PAGE_INDEX) *
                    OS_TEST_VMA_PAGE_SIZE_BYTES &&
        right_file_area.backing_data_length_bytes ==
            (OS_TEST_VMA_SECOND_END_PAGE_INDEX -
             OS_TEST_VMA_SPLIT_END_PAGE_INDEX) *
                OS_TEST_VMA_PAGE_SIZE_BYTES &&
        file_map.Validate() ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded;
    test_context.Expect(file_backing_valid, OS_TEST_VMA_FILE_BACKING);

    const bool destroy_valid =
        tiny_map.Destroy() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        map.Destroy() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        file_map.Destroy() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        pool.Statistics().active_descriptor_count == OS_TEST_VMA_EMPTY_VALUE &&
        pool.Statistics().free_descriptor_count == OS_TEST_VMA_POOL_CAPACITY &&
        pool.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        tiny_pool.Statistics().active_descriptor_count == OS_TEST_VMA_EMPTY_VALUE &&
        tiny_pool.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        file_pool.Statistics().active_descriptor_count ==
            OS_TEST_VMA_EMPTY_VALUE &&
        file_pool.Validate() ==
            os::kernel::VirtualMemoryAreaStatus::Succeeded;
    test_context.Expect(destroy_valid, OS_TEST_VMA_DESTROY);

    return test_context.ExitCode();
}
