#include <os/kernel/memory/swap_manager.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SWAP_RANDOM_SUITE_NAME = "kernel/swap_manager/randomized";
constexpr std::string_view OS_TEST_SWAP_RANDOM_MODEL =
    "十万步交换写入、换入和撤销必须逐步匹配独立槽模型与页面内容";

constexpr uint64_t OS_TEST_SWAP_RANDOM_SLOT_CAPACITY = 16ULL;
constexpr uint64_t OS_TEST_SWAP_RANDOM_IDENTITY_COUNT = 32ULL;
constexpr uint64_t OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_TEST_SWAP_RANDOM_STORAGE_SIZE_BYTES =
    OS_TEST_SWAP_RANDOM_SLOT_CAPACITY * OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_SWAP_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_SWAP_RANDOM_SEED = 0x5357415052414E44ULL;
constexpr uint64_t OS_TEST_SWAP_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_TEST_SWAP_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_TEST_SWAP_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_TEST_SWAP_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;

struct TestStorage final {
    uint8_t bytes[OS_TEST_SWAP_RANDOM_STORAGE_SIZE_BYTES];
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_SWAP_RANDOM_SHIFT_FIRST;
    state ^= state << OS_TEST_SWAP_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_TEST_SWAP_RANDOM_SHIFT_THIRD;
    state *= OS_TEST_SWAP_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] bool ReadSlot(void *const context, const uint64_t slot_index,
                            uint8_t *const destination, const uint64_t length_bytes) noexcept {
    if (context == nullptr || destination == nullptr ||
        slot_index >= OS_TEST_SWAP_RANDOM_SLOT_CAPACITY ||
        length_bytes != OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES) {
        return false;
    }
    const TestStorage &storage = *static_cast<const TestStorage *>(context);
    const uint64_t offset_bytes = slot_index * OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES;
    for (uint64_t byte_index = 0ULL; byte_index < length_bytes; ++byte_index) {
        destination[byte_index] = storage.bytes[offset_bytes + byte_index];
    }
    return true;
}

[[nodiscard]] bool WriteSlot(void *const context, const uint64_t slot_index,
                             const uint8_t *const source, const uint64_t length_bytes) noexcept {
    if (context == nullptr || source == nullptr ||
        slot_index >= OS_TEST_SWAP_RANDOM_SLOT_CAPACITY ||
        length_bytes != OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES) {
        return false;
    }
    TestStorage &storage = *static_cast<TestStorage *>(context);
    const uint64_t offset_bytes = slot_index * OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES;
    for (uint64_t byte_index = 0ULL; byte_index < length_bytes; ++byte_index) {
        storage.bytes[offset_bytes + byte_index] = source[byte_index];
    }
    return true;
}

[[nodiscard]] os::kernel::SwapPageIdentity MakeIdentity(const uint64_t identity_index) noexcept {
    return os::kernel::SwapPageIdentity{
        .address_space_identifier = identity_index + 1ULL,
        .virtual_address = identity_index * OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES,
    };
}

void FillPage(uint8_t *const page, const uint64_t identity_index,
              const uint64_t generation) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES;
         ++byte_index) {
        page[byte_index] =
            static_cast<uint8_t>(identity_index * 17ULL + generation * 29ULL + byte_index);
    }
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SWAP_RANDOM_SUITE_NAME};
    TestStorage storage{};
    os::kernel::SwapSlotEntry entries[OS_TEST_SWAP_RANDOM_SLOT_CAPACITY]{};
    os::kernel::SwapManager manager{};
    uint64_t model_slot[OS_TEST_SWAP_RANDOM_IDENTITY_COUNT]{};
    uint64_t model_generation[OS_TEST_SWAP_RANDOM_IDENTITY_COUNT]{};
    for (uint64_t identity_index = 0ULL; identity_index < OS_TEST_SWAP_RANDOM_IDENTITY_COUNT;
         ++identity_index) {
        model_slot[identity_index] = UINT64_MAX;
    }
    bool model_valid = manager.Initialize(entries, OS_TEST_SWAP_RANDOM_SLOT_CAPACITY,
                                          OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES, &storage, ReadSlot,
                                          WriteSlot) == os::kernel::SwapManagerStatus::Succeeded;
    uint64_t active_model_count = 0ULL;
    uint64_t random_state = OS_TEST_SWAP_RANDOM_SEED;
    uint8_t page[OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES]{};
    uint8_t destination[OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES]{};
    for (uint64_t iteration = 0ULL; model_valid && iteration < OS_TEST_SWAP_RANDOM_ITERATION_COUNT;
         ++iteration) {
        const uint64_t identity_index =
            NextRandom(random_state) % OS_TEST_SWAP_RANDOM_IDENTITY_COUNT;
        const uint64_t operation = NextRandom(random_state) % 3ULL;
        const os::kernel::SwapPageIdentity identity = MakeIdentity(identity_index);
        if (operation == 0ULL) {
            FillPage(page, identity_index, model_generation[identity_index]);
            uint64_t slot_index = UINT64_MAX;
            const os::kernel::SwapManagerStatus status =
                manager.Store(identity, page, OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES, slot_index);
            if (model_slot[identity_index] != UINT64_MAX) {
                model_valid = status == os::kernel::SwapManagerStatus::MappingAlreadyStored;
            } else if (active_model_count == OS_TEST_SWAP_RANDOM_SLOT_CAPACITY) {
                model_valid = status == os::kernel::SwapManagerStatus::CapacityExhausted;
            } else {
                model_valid = status == os::kernel::SwapManagerStatus::Succeeded &&
                              slot_index < OS_TEST_SWAP_RANDOM_SLOT_CAPACITY;
                if (model_valid) {
                    model_slot[identity_index] = slot_index;
                    ++active_model_count;
                }
            }
        } else if (operation == 1ULL) {
            const os::kernel::SwapManagerStatus status =
                manager.LoadAndRelease(identity, destination, OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES);
            if (model_slot[identity_index] == UINT64_MAX) {
                model_valid = status == os::kernel::SwapManagerStatus::MappingNotFound;
            } else {
                FillPage(page, identity_index, model_generation[identity_index]);
                model_valid = status == os::kernel::SwapManagerStatus::Succeeded;
                for (uint64_t byte_index = 0ULL;
                     model_valid && byte_index < OS_TEST_SWAP_RANDOM_PAGE_SIZE_BYTES;
                     ++byte_index) {
                    model_valid = destination[byte_index] == page[byte_index];
                }
                if (model_valid) {
                    model_slot[identity_index] = UINT64_MAX;
                    ++model_generation[identity_index];
                    --active_model_count;
                }
            }
        } else {
            const os::kernel::SwapManagerStatus status = manager.Release(identity);
            if (model_slot[identity_index] == UINT64_MAX) {
                model_valid = status == os::kernel::SwapManagerStatus::MappingNotFound;
            } else {
                model_valid = status == os::kernel::SwapManagerStatus::Succeeded;
                if (model_valid) {
                    model_slot[identity_index] = UINT64_MAX;
                    ++model_generation[identity_index];
                    --active_model_count;
                }
            }
        }
        model_valid = model_valid && manager.Statistics().active_slot_count == active_model_count &&
                      manager.Statistics().free_slot_count ==
                          OS_TEST_SWAP_RANDOM_SLOT_CAPACITY - active_model_count &&
                      manager.Validate() == os::kernel::SwapManagerStatus::Succeeded;
    }
    test_context.ExpectRandom(model_valid, OS_TEST_SWAP_RANDOM_MODEL, OS_TEST_SWAP_RANDOM_SEED,
                              OS_TEST_SWAP_RANDOM_ITERATION_COUNT);
    return test_context.ExitCode();
}
