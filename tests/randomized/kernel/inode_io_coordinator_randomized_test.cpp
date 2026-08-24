#include <os/kernel/fs/inode_io_coordinator.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_INODE_IO_RANDOMIZED_SUITE_NAME =
    "kernel/inode_io_coordinator/randomized";
constexpr std::string_view OS_TEST_INODE_IO_RANDOMIZED_MESSAGE =
    "固定种子十万步 acquire、release、LRU 复用和容量拒绝必须与活跃 identity oracle 一致";
constexpr uint64_t OS_TEST_INODE_IO_RANDOMIZED_SEED = 0x494E4F4445494F31ULL;
constexpr uint64_t OS_TEST_INODE_IO_RANDOMIZED_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_INODE_IO_RANDOMIZED_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT = 16ULL;
constexpr uint64_t OS_TEST_INODE_IO_RANDOMIZED_WAIT_QUEUE_BASE = 0x7200ULL;
constexpr uint64_t OS_TEST_INODE_IO_RANDOMIZED_LEFT_SHIFT = 13ULL;
constexpr uint64_t OS_TEST_INODE_IO_RANDOMIZED_RIGHT_SHIFT = 7ULL;
constexpr uint64_t OS_TEST_INODE_IO_RANDOMIZED_FINAL_LEFT_SHIFT = 17ULL;

struct OracleEntry final {
    os::kernel::fs::InodeIoIdentity identity;
    os::kernel::fs::InodeIoToken token;
    bool active;
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << OS_TEST_INODE_IO_RANDOMIZED_LEFT_SHIFT;
    state ^= state >> OS_TEST_INODE_IO_RANDOMIZED_RIGHT_SHIFT;
    state ^= state << OS_TEST_INODE_IO_RANDOMIZED_FINAL_LEFT_SHIFT;
    return state;
}

[[nodiscard]] os::kernel::fs::InodeIoIdentity MakeIdentity(const uint64_t identity_index) noexcept {
    return os::kernel::fs::InodeIoIdentity{
        .superblock_identifier = 17ULL,
        .superblock_generation = 9ULL,
        .node_identifier = identity_index + 1ULL,
        .node_generation = 4ULL,
    };
}

[[nodiscard]] uint64_t FindInactiveIdentity(const OracleEntry *const oracle,
                                            const uint64_t start_index) noexcept {
    for (uint64_t offset = 0ULL; offset < OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT; ++offset) {
        const uint64_t identity_index =
            (start_index + offset) % OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT;
        if (!oracle[identity_index].active) {
            return identity_index;
        }
    }
    return UINT64_MAX;
}

[[nodiscard]] uint64_t FindActiveIdentity(const OracleEntry *const oracle,
                                          const uint64_t start_index) noexcept {
    for (uint64_t offset = 0ULL; offset < OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT; ++offset) {
        const uint64_t identity_index =
            (start_index + offset) % OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT;
        if (oracle[identity_index].active) {
            return identity_index;
        }
    }
    return UINT64_MAX;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_INODE_IO_RANDOMIZED_SUITE_NAME};
    os::kernel::fs::InodeIoSlot storage[OS_TEST_INODE_IO_RANDOMIZED_CAPACITY]{};
    os::kernel::fs::InodeIoCoordinator coordinator{};
    OracleEntry oracle[OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT]{};
    for (uint64_t identity_index = 0ULL;
         identity_index < OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT; ++identity_index) {
        oracle[identity_index].identity = MakeIdentity(identity_index);
    }
    bool consistent = coordinator.Initialize(storage, OS_TEST_INODE_IO_RANDOMIZED_CAPACITY,
                                             OS_TEST_INODE_IO_RANDOMIZED_WAIT_QUEUE_BASE) ==
                      os::kernel::fs::InodeIoCoordinatorStatus::Succeeded;
    uint64_t random_state = OS_TEST_INODE_IO_RANDOMIZED_SEED;
    uint64_t active_count = 0ULL;
    uint64_t expected_acquisition_count = 0ULL;
    uint64_t expected_release_count = 0ULL;
    uint64_t expected_rejection_count = 0ULL;
    for (uint64_t iteration = 0ULL;
         consistent && iteration < OS_TEST_INODE_IO_RANDOMIZED_ITERATION_COUNT; ++iteration) {
        const uint64_t random_value = NextRandom(random_state);
        const bool acquire =
            active_count == 0ULL ||
            ((random_value & 1ULL) != 0ULL && active_count < OS_TEST_INODE_IO_RANDOMIZED_CAPACITY);
        if (acquire) {
            const uint64_t identity_index = FindInactiveIdentity(
                oracle, random_value % OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT);
            consistent = identity_index != UINT64_MAX &&
                         coordinator.Acquire(oracle[identity_index].identity,
                                             oracle[identity_index].token) ==
                             os::kernel::fs::InodeIoCoordinatorStatus::Succeeded;
            if (consistent) {
                oracle[identity_index].active = true;
                ++active_count;
                ++expected_acquisition_count;
            }
        } else if (active_count == OS_TEST_INODE_IO_RANDOMIZED_CAPACITY &&
                   (random_value & 3ULL) == 0ULL) {
            const uint64_t identity_index = FindInactiveIdentity(
                oracle, random_value % OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT);
            os::kernel::fs::InodeIoToken rejected_token{};
            consistent = identity_index != UINT64_MAX &&
                         coordinator.Acquire(oracle[identity_index].identity, rejected_token) ==
                             os::kernel::fs::InodeIoCoordinatorStatus::CapacityExhausted;
            ++expected_rejection_count;
        } else {
            const uint64_t identity_index = FindActiveIdentity(
                oracle, random_value % OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT);
            consistent = identity_index != UINT64_MAX &&
                         coordinator.Release(oracle[identity_index].token) ==
                             os::kernel::fs::InodeIoCoordinatorStatus::Succeeded;
            if (consistent) {
                oracle[identity_index].active = false;
                --active_count;
                ++expected_release_count;
            }
        }
        const os::kernel::fs::InodeIoCoordinatorStatistics statistics = coordinator.Statistics();
        consistent = consistent && statistics.active_reference_count == active_count &&
                     statistics.referenced_slot_count == active_count &&
                     coordinator.Validate() == os::kernel::fs::InodeIoCoordinatorStatus::Succeeded;
    }
    for (uint64_t identity_index = 0ULL;
         consistent && identity_index < OS_TEST_INODE_IO_RANDOMIZED_IDENTITY_COUNT;
         ++identity_index) {
        if (!oracle[identity_index].active) {
            continue;
        }
        consistent = coordinator.Release(oracle[identity_index].token) ==
                     os::kernel::fs::InodeIoCoordinatorStatus::Succeeded;
        ++expected_release_count;
    }
    const os::kernel::fs::InodeIoCoordinatorStatistics statistics = coordinator.Statistics();
    consistent =
        consistent &&
        coordinator.Validate() == os::kernel::fs::InodeIoCoordinatorStatus::Succeeded &&
        statistics.active_reference_count == 0ULL && statistics.referenced_slot_count == 0ULL &&
        statistics.acquisition_count == expected_acquisition_count &&
        statistics.release_count == expected_release_count &&
        statistics.capacity_rejection_count == expected_rejection_count &&
        statistics.slot_replacement_count != 0ULL && statistics.identity_reuse_count != 0ULL;
    test_context.ExpectRandom(consistent, OS_TEST_INODE_IO_RANDOMIZED_MESSAGE,
                              OS_TEST_INODE_IO_RANDOMIZED_SEED,
                              OS_TEST_INODE_IO_RANDOMIZED_ITERATION_COUNT);
    return test_context.ExitCode();
}
