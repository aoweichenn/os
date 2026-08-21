#include <os/kernel/memory/file_cache_address_space.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <test_context.hpp>

#include <map>
#include <stdint.h>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_CACHE_RANDOM_SUITE_NAME =
    "kernel/file_cache_address_space/randomized";
constexpr std::string_view OS_TEST_FILE_CACHE_RANDOM_MODEL =
    "十万步动态页面、引用、状态和标记查找必须逐步匹配有序参考模型";
constexpr std::string_view OS_TEST_FILE_CACHE_RANDOM_DRAIN =
    "随机模型排空后必须归还全部页面与 radix 节点元数据";

constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_HEAP_SIZE_BYTES = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_CANDIDATE_COUNT = 1024ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_VALIDATION_INTERVAL = 256ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_INSERT_LIMIT = 25ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_RETAIN_LIMIT = 40ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_RELEASE_LIMIT = 55ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_TRANSITION_LIMIT = 70ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_REMOVE_LIMIT = 85ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_LOOKUP_LIMIT = 93ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_PERCENT_SCALE = 100ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SEED = 0x5632385041474543ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_INDEX_MULTIPLIER = 0x9E3779B97F4A7C15ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SUPERBLOCK_IDENTIFIER = 131ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_SUPERBLOCK_GENERATION = 7ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_NODE_IDENTIFIER = 8191ULL;
constexpr uint64_t OS_TEST_FILE_CACHE_RANDOM_NODE_GENERATION = 19ULL;

alignas(OS_TEST_FILE_CACHE_RANDOM_ALIGNMENT_BYTES) uint8_t
    heap_storage[OS_TEST_FILE_CACHE_RANDOM_HEAP_SIZE_BYTES]{};

struct ModelEntry final {
    uint64_t physical_address;
    uint64_t mapping_reference_count;
    os::kernel::FileCachePageState state;
};

using Model = std::map<uint64_t, ModelEntry>;

[[nodiscard]] uint64_t AddressOf(void *const pointer) noexcept {
    return reinterpret_cast<uint64_t>(pointer);
}

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_FILE_CACHE_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_FILE_CACHE_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_FILE_CACHE_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_FILE_CACHE_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] uint64_t PageIndexForCandidate(const uint64_t candidate) noexcept {
    return candidate * OS_TEST_FILE_CACHE_RANDOM_INDEX_MULTIPLIER;
}

[[nodiscard]] uint64_t PhysicalAddressForCandidate(const uint64_t candidate) noexcept {
    return (candidate + 1ULL) * os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
}

[[nodiscard]] bool TransitionIsValid(const os::kernel::FileCachePageState current_state,
                                     const os::kernel::FileCachePageState new_state) noexcept {
    return (current_state == os::kernel::FileCachePageState::Clean &&
            new_state == os::kernel::FileCachePageState::Dirty) ||
           (current_state == os::kernel::FileCachePageState::Dirty &&
            new_state == os::kernel::FileCachePageState::Writeback) ||
           (current_state == os::kernel::FileCachePageState::Error &&
            (new_state == os::kernel::FileCachePageState::Dirty ||
             new_state == os::kernel::FileCachePageState::Writeback)) ||
           (current_state == os::kernel::FileCachePageState::Writeback &&
            (new_state == os::kernel::FileCachePageState::Clean ||
             new_state == os::kernel::FileCachePageState::Error));
}

[[nodiscard]] bool
StatisticsMatch(const Model &model,
                const os::kernel::FileCacheAddressSpace &address_space) noexcept {
    uint64_t referenced_page_count = 0ULL;
    uint64_t active_mapping_reference_count = 0ULL;
    uint64_t clean_page_count = 0ULL;
    uint64_t dirty_page_count = 0ULL;
    uint64_t writeback_page_count = 0ULL;
    uint64_t error_page_count = 0ULL;
    for (Model::const_iterator iterator = model.begin(); iterator != model.end(); ++iterator) {
        const ModelEntry &entry = iterator->second;
        active_mapping_reference_count += entry.mapping_reference_count;
        referenced_page_count += entry.mapping_reference_count == 0ULL ? 0ULL : 1ULL;
        clean_page_count += entry.state == os::kernel::FileCachePageState::Clean ? 1ULL : 0ULL;
        dirty_page_count += entry.state == os::kernel::FileCachePageState::Dirty ? 1ULL : 0ULL;
        writeback_page_count +=
            entry.state == os::kernel::FileCachePageState::Writeback ? 1ULL : 0ULL;
        error_page_count += entry.state == os::kernel::FileCachePageState::Error ? 1ULL : 0ULL;
    }
    const os::kernel::FileCacheAddressSpaceStatistics statistics = address_space.Statistics();
    return statistics.resident_page_count == static_cast<uint64_t>(model.size()) &&
           statistics.referenced_page_count == referenced_page_count &&
           statistics.active_mapping_reference_count == active_mapping_reference_count &&
           statistics.clean_page_count == clean_page_count &&
           statistics.dirty_page_count == dirty_page_count &&
           statistics.writeback_page_count == writeback_page_count &&
           statistics.error_page_count == error_page_count &&
           statistics.index.entry_count == static_cast<uint64_t>(model.size()) &&
           statistics.index.dirty_entry_count == dirty_page_count &&
           statistics.index.writeback_entry_count == writeback_page_count &&
           statistics.index.error_entry_count == error_page_count;
}

[[nodiscard]] bool ApplyInsert(os::kernel::FileCacheAddressSpace &address_space, Model &model,
                               const uint64_t candidate, const uint64_t random_value) {
    const uint64_t page_index = PageIndexForCandidate(candidate);
    const uint64_t physical_address = PhysicalAddressForCandidate(candidate);
    Model::iterator iterator = model.find(page_index);
    const os::kernel::FileCachePageState state = (random_value & 1ULL) == 0ULL
                                                     ? os::kernel::FileCachePageState::Clean
                                                     : os::kernel::FileCachePageState::Dirty;
    const os::kernel::FileCacheAddressSpaceStatus status =
        address_space.Insert(page_index, physical_address, state);
    if (iterator != model.end()) {
        return status == os::kernel::FileCacheAddressSpaceStatus::AlreadyExists;
    }
    if (status != os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
        return false;
    }
    model.emplace(page_index, ModelEntry{
                                  .physical_address = physical_address,
                                  .mapping_reference_count = 0ULL,
                                  .state = state,
                              });
    return true;
}

[[nodiscard]] bool ApplyRetain(os::kernel::FileCacheAddressSpace &address_space, Model &model,
                               const uint64_t candidate) noexcept {
    const uint64_t page_index = PageIndexForCandidate(candidate);
    Model::iterator iterator = model.find(page_index);
    const os::kernel::FileCacheAddressSpaceStatus status =
        address_space.Retain(page_index, PhysicalAddressForCandidate(candidate));
    if (iterator == model.end()) {
        return status == os::kernel::FileCacheAddressSpaceStatus::NotFound;
    }
    if (status != os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
        return false;
    }
    ++iterator->second.mapping_reference_count;
    return true;
}

[[nodiscard]] bool ApplyRelease(os::kernel::FileCacheAddressSpace &address_space, Model &model,
                                const uint64_t candidate) noexcept {
    const uint64_t page_index = PageIndexForCandidate(candidate);
    Model::iterator iterator = model.find(page_index);
    const os::kernel::FileCacheAddressSpaceStatus status =
        address_space.Release(page_index, PhysicalAddressForCandidate(candidate));
    if (iterator == model.end()) {
        return status == os::kernel::FileCacheAddressSpaceStatus::NotFound;
    }
    if (iterator->second.mapping_reference_count == 0ULL) {
        return status == os::kernel::FileCacheAddressSpaceStatus::MappingReferenceUnderflow;
    }
    if (status != os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
        return false;
    }
    --iterator->second.mapping_reference_count;
    return true;
}

[[nodiscard]] bool ApplyTransition(os::kernel::FileCacheAddressSpace &address_space, Model &model,
                                   const uint64_t candidate, const uint64_t random_value) noexcept {
    const uint64_t page_index = PageIndexForCandidate(candidate);
    Model::iterator iterator = model.find(page_index);
    const os::kernel::FileCachePageState requested_state =
        static_cast<os::kernel::FileCachePageState>((random_value >> 8ULL) & 3ULL);
    os::kernel::FileCachePageState expected_state = os::kernel::FileCachePageState::Clean;
    if (iterator != model.end()) {
        expected_state = iterator->second.state;
        if ((random_value & 7ULL) == 0ULL) {
            expected_state = static_cast<os::kernel::FileCachePageState>(
                (static_cast<uint64_t>(expected_state) + 1ULL) & 3ULL);
        }
    }
    const os::kernel::FileCacheAddressSpaceStatus status = address_space.Transition(
        page_index, PhysicalAddressForCandidate(candidate), expected_state, requested_state);
    if (iterator == model.end()) {
        return status == os::kernel::FileCacheAddressSpaceStatus::NotFound;
    }
    if (expected_state != iterator->second.state) {
        return status == os::kernel::FileCacheAddressSpaceStatus::InvalidState;
    }
    if (expected_state == requested_state) {
        return status == os::kernel::FileCacheAddressSpaceStatus::Succeeded;
    }
    if (!TransitionIsValid(expected_state, requested_state)) {
        return status == os::kernel::FileCacheAddressSpaceStatus::InvalidState;
    }
    if (status != os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
        return false;
    }
    iterator->second.state = requested_state;
    return true;
}

[[nodiscard]] bool ApplyRemove(os::kernel::FileCacheAddressSpace &address_space, Model &model,
                               const uint64_t candidate) {
    const uint64_t page_index = PageIndexForCandidate(candidate);
    Model::iterator iterator = model.find(page_index);
    const os::kernel::FileCacheAddressSpaceStatus status =
        address_space.Remove(page_index, PhysicalAddressForCandidate(candidate));
    if (iterator == model.end()) {
        return status == os::kernel::FileCacheAddressSpaceStatus::NotFound;
    }
    if (iterator->second.mapping_reference_count != 0ULL ||
        iterator->second.state == os::kernel::FileCachePageState::Writeback) {
        return status == os::kernel::FileCacheAddressSpaceStatus::PageBusy;
    }
    if (iterator->second.state != os::kernel::FileCachePageState::Clean) {
        return status == os::kernel::FileCacheAddressSpaceStatus::DirtyPagesRemain;
    }
    if (status != os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
        return false;
    }
    model.erase(iterator);
    return true;
}

[[nodiscard]] bool ApplyLookup(const os::kernel::FileCacheAddressSpace &address_space,
                               const Model &model, const uint64_t candidate) noexcept {
    const uint64_t page_index = PageIndexForCandidate(candidate);
    const Model::const_iterator iterator = model.find(page_index);
    os::kernel::FileCachePageSnapshot page{};
    const os::kernel::FileCacheAddressSpaceStatus status = address_space.Lookup(page_index, page);
    if (iterator == model.end()) {
        return status == os::kernel::FileCacheAddressSpaceStatus::NotFound;
    }
    return status == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
           page.page_index == page_index &&
           page.physical_address == iterator->second.physical_address &&
           page.mapping_reference_count == iterator->second.mapping_reference_count &&
           page.state == iterator->second.state;
}

[[nodiscard]] bool ApplyFind(const os::kernel::FileCacheAddressSpace &address_space,
                             const Model &model, const uint64_t random_value) noexcept {
    const os::kernel::FileCachePageState state =
        static_cast<os::kernel::FileCachePageState>((random_value >> 16ULL) & 3ULL);
    const uint64_t first_page_index = random_value;
    Model::const_iterator expected = model.lower_bound(first_page_index);
    while (expected != model.end() && expected->second.state != state) {
        ++expected;
    }
    os::kernel::FileCachePageSnapshot page{};
    const os::kernel::FileCacheAddressSpaceStatus status =
        address_space.FindNext(first_page_index, UINT64_MAX, state, page);
    if (expected == model.end()) {
        return status == os::kernel::FileCacheAddressSpaceStatus::NotFound;
    }
    return status == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
           page.page_index == expected->first && page.state == expected->second.state &&
           page.physical_address == expected->second.physical_address;
}

[[nodiscard]] bool NormalizeForRemoval(os::kernel::FileCacheAddressSpace &address_space,
                                       const uint64_t page_index, ModelEntry &entry) noexcept {
    while (entry.mapping_reference_count != 0ULL) {
        if (address_space.Release(page_index, entry.physical_address) !=
            os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
            return false;
        }
        --entry.mapping_reference_count;
    }
    if (entry.state == os::kernel::FileCachePageState::Dirty) {
        if (address_space.Transition(page_index, entry.physical_address,
                                     os::kernel::FileCachePageState::Dirty,
                                     os::kernel::FileCachePageState::Writeback) !=
            os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
            return false;
        }
        entry.state = os::kernel::FileCachePageState::Writeback;
    } else if (entry.state == os::kernel::FileCachePageState::Error) {
        if (address_space.Transition(page_index, entry.physical_address,
                                     os::kernel::FileCachePageState::Error,
                                     os::kernel::FileCachePageState::Writeback) !=
            os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
            return false;
        }
        entry.state = os::kernel::FileCachePageState::Writeback;
    }
    if (entry.state == os::kernel::FileCachePageState::Writeback) {
        if (address_space.Transition(page_index, entry.physical_address,
                                     os::kernel::FileCachePageState::Writeback,
                                     os::kernel::FileCachePageState::Clean) !=
            os::kernel::FileCacheAddressSpaceStatus::Succeeded) {
            return false;
        }
        entry.state = os::kernel::FileCachePageState::Clean;
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_CACHE_RANDOM_SUITE_NAME};
    os::kernel::KernelHeap heap{};
    os::kernel::FileCacheAddressSpace address_space{};
    const os::kernel::FileCacheIdentity identity{
        .superblock_identifier = OS_TEST_FILE_CACHE_RANDOM_SUPERBLOCK_IDENTIFIER,
        .superblock_generation = OS_TEST_FILE_CACHE_RANDOM_SUPERBLOCK_GENERATION,
        .node_identifier = OS_TEST_FILE_CACHE_RANDOM_NODE_IDENTIFIER,
        .node_generation = OS_TEST_FILE_CACHE_RANDOM_NODE_GENERATION,
    };
    bool valid = heap.Initialize(AddressOf(heap_storage), sizeof(heap_storage)) ==
                     os::kernel::KernelHeapStatus::Succeeded &&
                 address_space.Initialize(identity, heap) ==
                     os::kernel::FileCacheAddressSpaceStatus::Succeeded;
    Model model{};
    uint64_t random_state = OS_TEST_FILE_CACHE_RANDOM_SEED;
    for (uint64_t iteration = 0ULL; valid && iteration < OS_TEST_FILE_CACHE_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t random_value = NextRandom(random_state);
        const uint64_t candidate =
            (random_value >> 32ULL) % OS_TEST_FILE_CACHE_RANDOM_CANDIDATE_COUNT;
        const uint64_t operation = random_value % OS_TEST_FILE_CACHE_RANDOM_PERCENT_SCALE;
        bool iteration_valid = false;
        if (operation < OS_TEST_FILE_CACHE_RANDOM_INSERT_LIMIT) {
            iteration_valid = ApplyInsert(address_space, model, candidate, random_value);
        } else if (operation < OS_TEST_FILE_CACHE_RANDOM_RETAIN_LIMIT) {
            iteration_valid = ApplyRetain(address_space, model, candidate);
        } else if (operation < OS_TEST_FILE_CACHE_RANDOM_RELEASE_LIMIT) {
            iteration_valid = ApplyRelease(address_space, model, candidate);
        } else if (operation < OS_TEST_FILE_CACHE_RANDOM_TRANSITION_LIMIT) {
            iteration_valid = ApplyTransition(address_space, model, candidate, random_value);
        } else if (operation < OS_TEST_FILE_CACHE_RANDOM_REMOVE_LIMIT) {
            iteration_valid = ApplyRemove(address_space, model, candidate);
        } else if (operation < OS_TEST_FILE_CACHE_RANDOM_LOOKUP_LIMIT) {
            iteration_valid = ApplyLookup(address_space, model, candidate);
        } else {
            iteration_valid = ApplyFind(address_space, model, random_value);
        }
        if (iteration_valid && iteration % OS_TEST_FILE_CACHE_RANDOM_VALIDATION_INTERVAL == 0ULL) {
            iteration_valid =
                address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
                StatisticsMatch(model, address_space);
        }
        test_context.ExpectRandom(iteration_valid, OS_TEST_FILE_CACHE_RANDOM_MODEL,
                                  OS_TEST_FILE_CACHE_RANDOM_SEED, iteration);
        valid = iteration_valid;
    }

    while (valid && !model.empty()) {
        Model::iterator iterator = model.begin();
        valid = NormalizeForRemoval(address_space, iterator->first, iterator->second) &&
                address_space.Remove(iterator->first, iterator->second.physical_address) ==
                    os::kernel::FileCacheAddressSpaceStatus::Succeeded;
        if (valid) {
            model.erase(iterator);
        }
    }
    const os::kernel::FileCacheAddressSpaceStatistics final_statistics = address_space.Statistics();
    const bool drained =
        valid && model.empty() &&
        address_space.Validate() == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        StatisticsMatch(model, address_space) && final_statistics.resident_page_count == 0ULL &&
        final_statistics.index.node_count == 0ULL &&
        address_space.Destroy() == os::kernel::FileCacheAddressSpaceStatus::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == 0ULL &&
        heap.Statistics().active_requested_bytes == 0ULL;
    test_context.Expect(drained, OS_TEST_FILE_CACHE_RANDOM_DRAIN);

    return test_context.ExitCode();
}
