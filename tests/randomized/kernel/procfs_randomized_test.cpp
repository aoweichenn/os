#include "os/kernel/fs/procfs.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "test_context.hpp"

#include <string>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PROCFS_RANDOM_SUITE_NAME =
    "kernel/procfs/randomized";
constexpr std::string_view OS_TEST_PROCFS_RANDOM_ORACLE =
    "100000 次随机快照、偏移和短读必须与纯文本 oracle 完全一致";
constexpr uint64_t OS_TEST_PROCFS_RANDOM_SEED = 0x50524F4346537632ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_ITERATION_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_MULTIPLIER =
    0x2545F4914F6CDD1DULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_OFFSET_PADDING = 5ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_CAPACITY_LIMIT = 33ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_MOUNT_CAPACITY = 1ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_FIRST_SHIFT_BITS = 12ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_SECOND_SHIFT_BITS = 25ULL;
constexpr uint64_t OS_TEST_PROCFS_RANDOM_THIRD_SHIFT_BITS = 27ULL;
constexpr uint8_t OS_TEST_PROCFS_RANDOM_UPTIME_PATH[] = {
    '/', 'u', 'p', 't', 'i', 'm', 'e',
};
constexpr char OS_TEST_PROCFS_RANDOM_PREFIX[] = "monotonic_nanoseconds ";
constexpr char OS_TEST_PROCFS_RANDOM_SUFFIX[] = "\n";

struct RandomSource final {
    os::kernel::fs::ProcfsSnapshot snapshot;
};

[[nodiscard]] bool CaptureSnapshot(
    void *const context,
    os::kernel::fs::ProcfsSnapshot &snapshot) noexcept {
    if (context == nullptr) {
        return false;
    }
    snapshot = static_cast<const RandomSource *>(context)->snapshot;
    return true;
}

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_TEST_PROCFS_RANDOM_FIRST_SHIFT_BITS;
    state ^= state << OS_TEST_PROCFS_RANDOM_SECOND_SHIFT_BITS;
    state ^= state >> OS_TEST_PROCFS_RANDOM_THIRD_SHIFT_BITS;
    return state * OS_TEST_PROCFS_RANDOM_MULTIPLIER;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_PROCFS_RANDOM_SUITE_NAME};
    RandomSource source{};
    os::kernel::fs::Procfs procfs{};
    os::kernel::fs::Mount mounts[OS_TEST_PROCFS_RANDOM_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};
    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile uptime_file{};
    bool oracle_valid =
        procfs.Initialize(OS_TEST_PROCFS_RANDOM_SUPERBLOCK_IDENTIFIER,
                          CaptureSnapshot, &source) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_PROCFS_RANDOM_MOUNT_CAPACITY,
                       procfs.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded &&
        vfs.Open(context, OS_TEST_PROCFS_RANDOM_UPTIME_PATH,
                 sizeof(OS_TEST_PROCFS_RANDOM_UPTIME_PATH), read_options,
                 uptime_file) == os::kernel::fs::Status::Succeeded;

    uint64_t random_state = OS_TEST_PROCFS_RANDOM_SEED;
    for (uint64_t iteration = OS_TEST_PROCFS_RANDOM_EMPTY_VALUE;
         iteration < OS_TEST_PROCFS_RANDOM_ITERATION_COUNT && oracle_valid;
         ++iteration) {
        source.snapshot.monotonic_nanoseconds = NextRandom(random_state);
        const std::string expected =
            std::string{OS_TEST_PROCFS_RANDOM_PREFIX} +
            std::to_string(source.snapshot.monotonic_nanoseconds) +
            OS_TEST_PROCFS_RANDOM_SUFFIX;
        const uint64_t offset_bytes =
            NextRandom(random_state) %
            (expected.size() + OS_TEST_PROCFS_RANDOM_OFFSET_PADDING);
        const uint64_t capacity_bytes =
            NextRandom(random_state) % OS_TEST_PROCFS_RANDOM_CAPACITY_LIMIT;
        uint8_t actual_bytes[OS_TEST_PROCFS_RANDOM_CAPACITY_LIMIT]{};
        uint64_t actual_size_bytes = OS_TEST_PROCFS_RANDOM_EMPTY_VALUE;
        if (vfs.ReadAt(uptime_file, offset_bytes, actual_bytes, capacity_bytes,
                       actual_size_bytes) != os::kernel::fs::Status::Succeeded) {
            oracle_valid = false;
            break;
        }
        const uint64_t expected_size_bytes =
            offset_bytes >= expected.size()
                ? OS_TEST_PROCFS_RANDOM_EMPTY_VALUE
                : capacity_bytes < expected.size() - offset_bytes
                      ? capacity_bytes
                      : expected.size() - offset_bytes;
        const std::string_view expected_slice =
            offset_bytes >= expected.size()
                ? std::string_view{}
                : std::string_view{expected}.substr(offset_bytes,
                                                    expected_size_bytes);
        oracle_valid =
            actual_size_bytes == expected_size_bytes &&
            std::string_view{
                reinterpret_cast<const char *>(actual_bytes),
                actual_size_bytes} == expected_slice;
    }

    oracle_valid =
        oracle_valid &&
        vfs.Close(uptime_file) == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        procfs.Validate() == os::kernel::fs::Status::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded;
    test_context.Expect(oracle_valid, OS_TEST_PROCFS_RANDOM_ORACLE);
    return test_context.ExitCode();
}
