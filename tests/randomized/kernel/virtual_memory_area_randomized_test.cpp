#include "os/kernel/memory/physical_frame_allocator.hpp"
#include "os/kernel/memory/virtual_memory_area.hpp"
#include "test_context.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view OS_TEST_VMA_RANDOM_SUITE_NAME = "kernel/virtual_memory_area/randomized";
constexpr std::string_view OS_TEST_VMA_RANDOM_REFERENCE =
    "十万步映射、切分、合并与解除映射必须和独立参考模型一致";
constexpr std::string_view OS_TEST_VMA_RANDOM_DRAIN = "随机模型销毁后共享描述符池必须完整恢复";

constexpr uint64_t OS_TEST_VMA_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_PAGE_SIZE_BYTES =
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_VMA_RANDOM_WINDOW_BEGIN_ADDRESS = 0x50000000ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_WINDOW_PAGE_COUNT = 1024ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_DESCRIPTOR_CAPACITY = 2048ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_HARD_AREA_LIMIT = 1024ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_MAXIMUM_LENGTH_PAGE_COUNT = 32ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_INSERT_PERCENT = 58ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_PERCENT_SCALE = 100ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_VALIDATION_INTERVAL = 257ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_SEED = 0x564D413130303030ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_KIND_COUNT = 6ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_WRITABLE_PERMISSION_MASK = 1ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_EXECUTABLE_PERMISSION_MASK = 2ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_BACKING_DESCRIPTOR_COUNT = 31ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_FIRST_BACKING_DESCRIPTOR = 1ULL;
constexpr uint64_t OS_TEST_VMA_RANDOM_BACKING_GENERATION = 3ULL;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_VMA_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_VMA_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_VMA_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_VMA_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint64_t PageAddress(const uint64_t page_index) noexcept {
    return OS_TEST_VMA_RANDOM_WINDOW_BEGIN_ADDRESS +
           page_index * OS_TEST_VMA_RANDOM_PAGE_SIZE_BYTES;
}

[[nodiscard]] bool AttributesEqual(const os::kernel::VirtualMemoryArea &left,
                                   const os::kernel::VirtualMemoryArea &right) noexcept {
    if (os::kernel::IsFileBackedVirtualMemoryAreaKind(left.kind) ||
        os::kernel::IsFileBackedVirtualMemoryAreaKind(right.kind)) {
        return false;
    }
    return left.permissions.readable == right.permissions.readable &&
           left.permissions.writable == right.permissions.writable &&
           left.permissions.executable == right.permissions.executable && left.kind == right.kind;
}

[[nodiscard]] bool AreasEqual(const os::kernel::VirtualMemoryArea &left,
                              const os::kernel::VirtualMemoryArea &right) noexcept {
    return left.begin_address == right.begin_address && left.end_address == right.end_address &&
           left.permissions.readable == right.permissions.readable &&
           left.permissions.writable == right.permissions.writable &&
           left.permissions.executable == right.permissions.executable &&
           left.kind == right.kind &&
           left.backing_descriptor_index == right.backing_descriptor_index &&
           left.backing_generation == right.backing_generation &&
           left.backing_file_offset_bytes == right.backing_file_offset_bytes &&
           left.backing_data_length_bytes == right.backing_data_length_bytes;
}

void RestrictModelArea(os::kernel::VirtualMemoryArea &area,
                       const uint64_t new_begin_address,
                       const uint64_t new_end_address) noexcept {
    const uint64_t begin_delta_bytes =
        new_begin_address - area.begin_address;
    const uint64_t new_length_bytes =
        new_end_address - new_begin_address;
    if (os::kernel::IsFileBackedVirtualMemoryAreaKind(area.kind)) {
        area.backing_file_offset_bytes += begin_delta_bytes;
        area.backing_data_length_bytes =
            area.backing_data_length_bytes > begin_delta_bytes
                ? area.backing_data_length_bytes - begin_delta_bytes
                : OS_TEST_VMA_RANDOM_EMPTY_VALUE;
        if (area.backing_data_length_bytes > new_length_bytes) {
            area.backing_data_length_bytes = new_length_bytes;
        }
    }
    area.begin_address = new_begin_address;
    area.end_address = new_end_address;
}

[[nodiscard]] os::kernel::VirtualMemoryAreaStatus
InsertModel(std::vector<os::kernel::VirtualMemoryArea> &areas,
            const os::kernel::VirtualMemoryArea &area) {
    uint64_t insertion_index = OS_TEST_VMA_RANDOM_EMPTY_VALUE;
    while (insertion_index < static_cast<uint64_t>(areas.size()) &&
           areas[insertion_index].begin_address < area.begin_address) {
        ++insertion_index;
    }
    if (insertion_index > OS_TEST_VMA_RANDOM_EMPTY_VALUE &&
        areas[insertion_index - OS_TEST_VMA_RANDOM_SINGLE_UNIT].end_address > area.begin_address) {
        return os::kernel::VirtualMemoryAreaStatus::Overlap;
    }
    if (insertion_index < static_cast<uint64_t>(areas.size()) &&
        area.end_address > areas[insertion_index].begin_address) {
        return os::kernel::VirtualMemoryAreaStatus::Overlap;
    }

    const bool merge_previous =
        insertion_index > OS_TEST_VMA_RANDOM_EMPTY_VALUE &&
        areas[insertion_index - OS_TEST_VMA_RANDOM_SINGLE_UNIT].end_address == area.begin_address &&
        AttributesEqual(areas[insertion_index - OS_TEST_VMA_RANDOM_SINGLE_UNIT], area);
    const bool merge_next = insertion_index < static_cast<uint64_t>(areas.size()) &&
                            area.end_address == areas[insertion_index].begin_address &&
                            AttributesEqual(area, areas[insertion_index]);
    if (merge_previous && merge_next) {
        areas[insertion_index - OS_TEST_VMA_RANDOM_SINGLE_UNIT].end_address =
            areas[insertion_index].end_address;
        areas.erase(areas.begin() + static_cast<int64_t>(insertion_index));
    } else if (merge_previous) {
        areas[insertion_index - OS_TEST_VMA_RANDOM_SINGLE_UNIT].end_address = area.end_address;
    } else if (merge_next) {
        areas[insertion_index].begin_address = area.begin_address;
    } else {
        areas.insert(areas.begin() + static_cast<int64_t>(insertion_index), area);
    }
    return os::kernel::VirtualMemoryAreaStatus::Succeeded;
}

[[nodiscard]] os::kernel::VirtualMemoryAreaStatus
RemoveModel(std::vector<os::kernel::VirtualMemoryArea> &areas, const uint64_t begin_address,
            const uint64_t end_address, const os::kernel::VirtualMemoryAreaKind required_kind) {
    bool overlap_found = false;
    for (const os::kernel::VirtualMemoryArea &area : areas) {
        if (area.begin_address >= end_address) {
            break;
        }
        if (area.end_address > begin_address) {
            overlap_found = true;
            if (area.kind != required_kind) {
                return os::kernel::VirtualMemoryAreaStatus::KindMismatch;
            }
        }
    }
    if (!overlap_found) {
        return os::kernel::VirtualMemoryAreaStatus::NotMapped;
    }

    std::vector<os::kernel::VirtualMemoryArea> updated_areas;
    updated_areas.reserve(areas.size() + OS_TEST_VMA_RANDOM_SINGLE_UNIT);
    for (const os::kernel::VirtualMemoryArea &area : areas) {
        if (area.end_address <= begin_address || area.begin_address >= end_address) {
            updated_areas.push_back(area);
            continue;
        }
        if (area.begin_address < begin_address) {
            os::kernel::VirtualMemoryArea left_area = area;
            RestrictModelArea(left_area, left_area.begin_address,
                              begin_address);
            updated_areas.push_back(left_area);
        }
        if (end_address < area.end_address) {
            os::kernel::VirtualMemoryArea right_area = area;
            RestrictModelArea(right_area, end_address,
                              right_area.end_address);
            updated_areas.push_back(right_area);
        }
    }
    areas = updated_areas;
    return os::kernel::VirtualMemoryAreaStatus::Succeeded;
}

[[nodiscard]] bool MapMatchesModel(const os::kernel::VirtualMemoryMap &map,
                                   const std::vector<os::kernel::VirtualMemoryArea> &areas) {
    if (map.AreaCount() != static_cast<uint64_t>(areas.size()) ||
        map.Validate() != os::kernel::VirtualMemoryAreaStatus::Succeeded) {
        return false;
    }
    for (uint64_t area_index = OS_TEST_VMA_RANDOM_EMPTY_VALUE;
         area_index < static_cast<uint64_t>(areas.size()); ++area_index) {
        os::kernel::VirtualMemoryArea observed_area{};
        if (map.ReadAt(area_index, observed_area) !=
                os::kernel::VirtualMemoryAreaStatus::Succeeded ||
            !AreasEqual(observed_area, areas[area_index])) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VMA_RANDOM_SUITE_NAME};
    os::kernel::VirtualMemoryAreaDescriptor descriptors[OS_TEST_VMA_RANDOM_DESCRIPTOR_CAPACITY]{};
    os::kernel::VirtualMemoryAreaPool pool{};
    os::kernel::VirtualMemoryMap map{};
    std::vector<os::kernel::VirtualMemoryArea> model_areas;
    model_areas.reserve(OS_TEST_VMA_RANDOM_HARD_AREA_LIMIT);

    bool reference_valid = pool.Initialize(descriptors, OS_TEST_VMA_RANDOM_DESCRIPTOR_CAPACITY) ==
                               os::kernel::VirtualMemoryAreaStatus::Succeeded &&
                           map.Initialize(pool, OS_TEST_VMA_RANDOM_PAGE_SIZE_BYTES,
                                          OS_TEST_VMA_RANDOM_HARD_AREA_LIMIT) ==
                               os::kernel::VirtualMemoryAreaStatus::Succeeded;
    uint64_t random_state = OS_TEST_VMA_RANDOM_SEED;
    for (uint64_t iteration = OS_TEST_VMA_RANDOM_EMPTY_VALUE;
         reference_valid && iteration < OS_TEST_VMA_RANDOM_ITERATION_COUNT; ++iteration) {
        const uint64_t operation = NextRandom(random_state) % OS_TEST_VMA_RANDOM_PERCENT_SCALE;
        const uint64_t begin_page_index =
            NextRandom(random_state) % OS_TEST_VMA_RANDOM_WINDOW_PAGE_COUNT;
        const uint64_t maximum_length_page_count =
            std::min(OS_TEST_VMA_RANDOM_MAXIMUM_LENGTH_PAGE_COUNT,
                     OS_TEST_VMA_RANDOM_WINDOW_PAGE_COUNT - begin_page_index);
        const uint64_t length_page_count =
            NextRandom(random_state) % maximum_length_page_count + OS_TEST_VMA_RANDOM_SINGLE_UNIT;
        const uint64_t end_page_index = begin_page_index + length_page_count;
        const os::kernel::VirtualMemoryAreaKind kind =
            static_cast<os::kernel::VirtualMemoryAreaKind>(NextRandom(random_state) %
                                                           OS_TEST_VMA_RANDOM_KIND_COUNT);
        const uint64_t permission_bits = NextRandom(random_state);
        os::kernel::VirtualMemoryArea area{
            .begin_address = PageAddress(begin_page_index),
            .end_address = PageAddress(end_page_index),
            .permissions =
                {
                    .readable = true,
                    .writable = (permission_bits & OS_TEST_VMA_RANDOM_WRITABLE_PERMISSION_MASK) !=
                                OS_TEST_VMA_RANDOM_EMPTY_VALUE,
                    .executable =
                        (permission_bits & OS_TEST_VMA_RANDOM_EXECUTABLE_PERMISSION_MASK) !=
                        OS_TEST_VMA_RANDOM_EMPTY_VALUE,
                },
            .kind = kind,
        };
        if (os::kernel::IsFileBackedVirtualMemoryAreaKind(kind)) {
            area.backing_descriptor_index =
                NextRandom(random_state) %
                    OS_TEST_VMA_RANDOM_BACKING_DESCRIPTOR_COUNT +
                OS_TEST_VMA_RANDOM_FIRST_BACKING_DESCRIPTOR;
            area.backing_generation =
                OS_TEST_VMA_RANDOM_BACKING_GENERATION;
            area.backing_file_offset_bytes =
                begin_page_index * OS_TEST_VMA_RANDOM_PAGE_SIZE_BYTES;
            area.backing_data_length_bytes =
                length_page_count * OS_TEST_VMA_RANDOM_PAGE_SIZE_BYTES;
        }

        os::kernel::VirtualMemoryAreaStatus expected_status =
            os::kernel::VirtualMemoryAreaStatus::Corrupt;
        os::kernel::VirtualMemoryAreaStatus observed_status =
            os::kernel::VirtualMemoryAreaStatus::Corrupt;
        if (operation < OS_TEST_VMA_RANDOM_INSERT_PERCENT) {
            expected_status = InsertModel(model_areas, area);
            observed_status = map.Insert(area);
        } else {
            expected_status = RemoveModel(model_areas, area.begin_address, area.end_address, kind);
            observed_status = map.Remove(area.begin_address, area.end_address, kind);
        }
        reference_valid = expected_status == observed_status;
        if (reference_valid &&
            iteration % OS_TEST_VMA_RANDOM_VALIDATION_INTERVAL == OS_TEST_VMA_RANDOM_EMPTY_VALUE) {
            reference_valid = MapMatchesModel(map, model_areas) &&
                              pool.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded;
        }
    }
    reference_valid = reference_valid && MapMatchesModel(map, model_areas);
    test_context.Expect(reference_valid, OS_TEST_VMA_RANDOM_REFERENCE);

    const bool drain_valid =
        map.Destroy() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        pool.Validate() == os::kernel::VirtualMemoryAreaStatus::Succeeded &&
        pool.Statistics().active_descriptor_count == OS_TEST_VMA_RANDOM_EMPTY_VALUE &&
        pool.Statistics().free_descriptor_count == OS_TEST_VMA_RANDOM_DESCRIPTOR_CAPACITY &&
        pool.Statistics().successful_acquire_count == pool.Statistics().release_count;
    test_context.Expect(drain_valid, OS_TEST_VMA_RANDOM_DRAIN);
    return test_context.ExitCode();
}
