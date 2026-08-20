#include <os/kernel/memory/swap_manager.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SWAP_MANAGER_SUITE_NAME = "kernel/swap_manager/unit";
constexpr std::string_view OS_TEST_SWAP_MANAGER_ROUND_TRIP =
    "写入成功后必须按地址空间和虚拟页找回并在校验后释放槽";
constexpr std::string_view OS_TEST_SWAP_MANAGER_CAPACITY = "重复映射和槽耗尽必须保持已有交换页不变";
constexpr std::string_view OS_TEST_SWAP_MANAGER_CLONE =
    "fork clone 必须保留源槽并建立内容一致的独立目标槽";
constexpr std::string_view OS_TEST_SWAP_MANAGER_WRITE_FAILURE = "交换写失败不得提交槽或映射";
constexpr std::string_view OS_TEST_SWAP_MANAGER_READ_FAILURE = "交换读失败必须保留槽供后续重试";
constexpr std::string_view OS_TEST_SWAP_MANAGER_CORRUPTION =
    "交换页校验失败必须显式报告且不得静默释放唯一副本";
constexpr std::string_view OS_TEST_SWAP_MANAGER_RELEASE =
    "未换入的映射销毁必须只释放所属槽并维持统计守恒";

constexpr uint64_t OS_TEST_SWAP_MANAGER_SLOT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES = 64ULL;
constexpr uint64_t OS_TEST_SWAP_MANAGER_STORAGE_SIZE_BYTES =
    OS_TEST_SWAP_MANAGER_SLOT_CAPACITY * OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_SWAP_MANAGER_INVALID_SLOT = UINT64_MAX;

struct TestStorage final {
    uint8_t bytes[OS_TEST_SWAP_MANAGER_STORAGE_SIZE_BYTES];
    os::kernel::SwapSlotEntry entries[OS_TEST_SWAP_MANAGER_SLOT_CAPACITY];
    bool fail_next_read;
    bool fail_next_write;
};

[[nodiscard]] bool ReadEntry(void *const context, const uint64_t slot_index,
                             os::kernel::SwapSlotEntry &entry) noexcept {
    if (context == nullptr || slot_index >= OS_TEST_SWAP_MANAGER_SLOT_CAPACITY) {
        return false;
    }
    entry = static_cast<TestStorage *>(context)->entries[slot_index];
    return true;
}

[[nodiscard]] bool WriteEntry(void *const context, const uint64_t slot_index,
                              const os::kernel::SwapSlotEntry &entry) noexcept {
    if (context == nullptr || slot_index >= OS_TEST_SWAP_MANAGER_SLOT_CAPACITY) {
        return false;
    }
    static_cast<TestStorage *>(context)->entries[slot_index] = entry;
    return true;
}

[[nodiscard]] bool ReadSlot(void *const context, const uint64_t slot_index,
                            uint8_t *const destination, const uint64_t length_bytes) noexcept {
    if (context == nullptr || destination == nullptr ||
        slot_index >= OS_TEST_SWAP_MANAGER_SLOT_CAPACITY ||
        length_bytes != OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES) {
        return false;
    }
    TestStorage &storage = *static_cast<TestStorage *>(context);
    if (storage.fail_next_read) {
        storage.fail_next_read = false;
        return false;
    }
    const uint64_t offset_bytes = slot_index * OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES;
    for (uint64_t byte_index = 0ULL; byte_index < length_bytes; ++byte_index) {
        destination[byte_index] = storage.bytes[offset_bytes + byte_index];
    }
    return true;
}

[[nodiscard]] bool WriteSlot(void *const context, const uint64_t slot_index,
                             const uint8_t *const source, const uint64_t length_bytes) noexcept {
    if (context == nullptr || source == nullptr ||
        slot_index >= OS_TEST_SWAP_MANAGER_SLOT_CAPACITY ||
        length_bytes != OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES) {
        return false;
    }
    TestStorage &storage = *static_cast<TestStorage *>(context);
    if (storage.fail_next_write) {
        storage.fail_next_write = false;
        return false;
    }
    const uint64_t offset_bytes = slot_index * OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES;
    for (uint64_t byte_index = 0ULL; byte_index < length_bytes; ++byte_index) {
        storage.bytes[offset_bytes + byte_index] = source[byte_index];
    }
    return true;
}

void FillPage(uint8_t *const page, const uint8_t seed) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES;
         ++byte_index) {
        page[byte_index] = static_cast<uint8_t>(seed + static_cast<uint8_t>(byte_index));
    }
}

[[nodiscard]] bool PagesEqual(const uint8_t *const left, const uint8_t *const right) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] os::kernel::SwapPageIdentity MakeIdentity(const uint64_t index) noexcept {
    return os::kernel::SwapPageIdentity{
        .address_space_identifier = index + 1ULL,
        .virtual_address = index * OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SWAP_MANAGER_SUITE_NAME};
    TestStorage storage{};
    os::kernel::SwapManager manager{};
    uint8_t source[OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES]{};
    uint8_t destination[OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES]{};
    FillPage(source, 0x31U);
    const os::kernel::SwapPageIdentity first_identity = MakeIdentity(0ULL);
    uint64_t first_slot = OS_TEST_SWAP_MANAGER_INVALID_SLOT;
    const bool round_trip_valid =
        manager.Initialize(OS_TEST_SWAP_MANAGER_SLOT_CAPACITY, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
                           &storage, ReadEntry, WriteEntry, ReadSlot,
                           WriteSlot) == os::kernel::SwapManagerStatus::Succeeded &&
        manager.Store(first_identity, source, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES, first_slot) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        first_slot < OS_TEST_SWAP_MANAGER_SLOT_CAPACITY &&
        manager.Statistics().active_slot_count == 1ULL &&
        manager.LoadAndRelease(first_identity, destination, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        PagesEqual(source, destination) && manager.Statistics().active_slot_count == 0ULL &&
        manager.Statistics().successful_load_count == 1ULL &&
        manager.Validate() == os::kernel::SwapManagerStatus::Succeeded;
    test_context.Expect(round_trip_valid, OS_TEST_SWAP_MANAGER_ROUND_TRIP);

    const os::kernel::SwapPageIdentity clone_identity = MakeIdentity(7ULL);
    uint64_t clone_source_slot = OS_TEST_SWAP_MANAGER_INVALID_SLOT;
    uint64_t clone_destination_slot = OS_TEST_SWAP_MANAGER_INVALID_SLOT;
    const bool clone_valid =
        manager.Store(first_identity, source, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
                      clone_source_slot) == os::kernel::SwapManagerStatus::Succeeded &&
        manager.Clone(first_identity, clone_identity, destination,
                      OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
                      clone_destination_slot) == os::kernel::SwapManagerStatus::Succeeded &&
        clone_source_slot != clone_destination_slot &&
        manager.Statistics().active_slot_count == 2ULL &&
        manager.LoadAndRelease(first_identity, destination, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        PagesEqual(source, destination) &&
        manager.LoadAndRelease(clone_identity, destination, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        PagesEqual(source, destination) && manager.Statistics().successful_clone_count == 1ULL &&
        manager.Statistics().active_slot_count == 0ULL;
    test_context.Expect(clone_valid, OS_TEST_SWAP_MANAGER_CLONE);

    uint64_t occupied_slots[OS_TEST_SWAP_MANAGER_SLOT_CAPACITY]{};
    bool capacity_valid = true;
    for (uint64_t slot_index = 0ULL; slot_index < OS_TEST_SWAP_MANAGER_SLOT_CAPACITY;
         ++slot_index) {
        FillPage(source, static_cast<uint8_t>(0x40U + slot_index));
        capacity_valid =
            capacity_valid &&
            manager.Store(MakeIdentity(slot_index), source, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
                          occupied_slots[slot_index]) == os::kernel::SwapManagerStatus::Succeeded &&
            occupied_slots[slot_index] < OS_TEST_SWAP_MANAGER_SLOT_CAPACITY;
    }
    uint64_t rejected_slot = 0ULL;
    capacity_valid =
        capacity_valid &&
        manager.Store(MakeIdentity(0ULL), source, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
                      rejected_slot) == os::kernel::SwapManagerStatus::MappingAlreadyStored &&
        manager.Store(MakeIdentity(OS_TEST_SWAP_MANAGER_SLOT_CAPACITY), source,
                      OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
                      rejected_slot) == os::kernel::SwapManagerStatus::CapacityExhausted &&
        manager.Statistics().active_slot_count == OS_TEST_SWAP_MANAGER_SLOT_CAPACITY &&
        manager.Validate() == os::kernel::SwapManagerStatus::Succeeded;
    test_context.Expect(capacity_valid, OS_TEST_SWAP_MANAGER_CAPACITY);

    const os::kernel::SwapPageIdentity released_identity = MakeIdentity(1ULL);
    const bool release_valid =
        manager.Release(released_identity) == os::kernel::SwapManagerStatus::Succeeded &&
        manager.FindSlot(released_identity, rejected_slot) ==
            os::kernel::SwapManagerStatus::MappingNotFound &&
        manager.Statistics().release_count == 1ULL &&
        manager.Statistics().active_slot_count == OS_TEST_SWAP_MANAGER_SLOT_CAPACITY - 1ULL;
    test_context.Expect(release_valid, OS_TEST_SWAP_MANAGER_RELEASE);

    storage.fail_next_write = true;
    uint64_t failed_write_slot = 0ULL;
    const bool write_failure_valid =
        manager.Store(released_identity, source, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
                      failed_write_slot) == os::kernel::SwapManagerStatus::WriteFailed &&
        failed_write_slot == OS_TEST_SWAP_MANAGER_INVALID_SLOT &&
        manager.FindSlot(released_identity, rejected_slot) ==
            os::kernel::SwapManagerStatus::MappingNotFound &&
        manager.Statistics().failed_store_count == 1ULL;
    test_context.Expect(write_failure_valid, OS_TEST_SWAP_MANAGER_WRITE_FAILURE);

    uint64_t retry_slot = OS_TEST_SWAP_MANAGER_INVALID_SLOT;
    FillPage(source, 0x75U);
    const bool stored_for_read_failure =
        manager.Store(released_identity, source, OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES,
                      retry_slot) == os::kernel::SwapManagerStatus::Succeeded;
    storage.fail_next_read = true;
    const bool read_failure_valid = stored_for_read_failure &&
                                    manager.LoadAndRelease(released_identity, destination,
                                                           OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES) ==
                                        os::kernel::SwapManagerStatus::ReadFailed &&
                                    manager.FindSlot(released_identity, rejected_slot) ==
                                        os::kernel::SwapManagerStatus::Succeeded &&
                                    manager.Statistics().failed_load_count == 1ULL;
    test_context.Expect(read_failure_valid, OS_TEST_SWAP_MANAGER_READ_FAILURE);

    storage.bytes[retry_slot * OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES] ^= 0xFFU;
    const bool corruption_valid =
        manager.LoadAndRelease(released_identity, destination,
                               OS_TEST_SWAP_MANAGER_PAGE_SIZE_BYTES) ==
            os::kernel::SwapManagerStatus::ChecksumMismatch &&
        manager.FindSlot(released_identity, rejected_slot) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        manager.Statistics().checksum_failure_count == 1ULL &&
        manager.Release(released_identity) == os::kernel::SwapManagerStatus::Succeeded &&
        manager.Validate() == os::kernel::SwapManagerStatus::Succeeded;
    test_context.Expect(corruption_valid, OS_TEST_SWAP_MANAGER_CORRUPTION);

    return test_context.ExitCode();
}
