#include "memory_block_device.hpp"
#include "os/kernel/fs/file_system.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_SYSTEM_RANDOMIZED_SUITE_NAME =
    "kernel/file_system/randomized";
constexpr std::string_view OS_TEST_FILE_SYSTEM_RANDOMIZED_RESTART_MODEL =
    "固定种子随机长度与内容必须在每次重挂载后和参考模型一致";
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_SEED = 0xF17E5A57C0DEC0DEULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_ITERATION_COUNT = 128ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_MINIMUM_PAYLOAD_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_BYTE_MASK = 0xFFULL;
constexpr uint8_t OS_TEST_FILE_SYSTEM_RANDOMIZED_DIRECTORY_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('r'), static_cast<uint8_t>('a'),
    static_cast<uint8_t>('n'), static_cast<uint8_t>('d'), static_cast<uint8_t>('o'),
    static_cast<uint8_t>('m'),
};
constexpr uint8_t OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('r'), static_cast<uint8_t>('a'),
    static_cast<uint8_t>('n'), static_cast<uint8_t>('d'), static_cast<uint8_t>('o'),
    static_cast<uint8_t>('m'), static_cast<uint8_t>('/'), static_cast<uint8_t>('s'),
    static_cast<uint8_t>('t'), static_cast<uint8_t>('a'), static_cast<uint8_t>('t'),
    static_cast<uint8_t>('e'), static_cast<uint8_t>('.'), static_cast<uint8_t>('b'),
    static_cast<uint8_t>('i'), static_cast<uint8_t>('n'),
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_FILE_SYSTEM_RANDOMIZED_MULTIPLIER +
            OS_TEST_FILE_SYSTEM_RANDOMIZED_INCREMENT;
    return state;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_SYSTEM_RANDOMIZED_SUITE_NAME};
    static os::test::MemoryBlockDevice device{};
    uint8_t expected[os::kernel::OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES]{};
    uint64_t expected_size = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
    uint64_t random_state = OS_TEST_FILE_SYSTEM_RANDOMIZED_SEED;
    bool model_consistent = true;

    for (uint64_t iteration = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
         iteration < OS_TEST_FILE_SYSTEM_RANDOMIZED_ITERATION_COUNT; ++iteration) {
        os::kernel::FileSystem file_system{};
        bool formatted = false;
        model_consistent = model_consistent && file_system.MountOrFormat(device, formatted) ==
                                                   os::kernel::FileSystemStatus::Succeeded;
        if (iteration == OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE) {
            model_consistent = model_consistent && formatted &&
                               file_system.CreateDirectory(
                                   OS_TEST_FILE_SYSTEM_RANDOMIZED_DIRECTORY_PATH,
                                   sizeof(OS_TEST_FILE_SYSTEM_RANDOMIZED_DIRECTORY_PATH)) ==
                                   os::kernel::FileSystemStatus::Succeeded;
        } else {
            model_consistent = model_consistent && !formatted;
            os::kernel::FileSystemHandle read_handle{};
            const os::kernel::FileSystemOpenOptions read_options{
                .readable = true,
                .writable = false,
                .create = false,
                .truncate = false,
            };
            uint8_t actual[os::kernel::OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES]{};
            uint64_t read_bytes = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
            model_consistent =
                model_consistent &&
                file_system.Open(OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH,
                                 sizeof(OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH), read_options,
                                 read_handle) == os::kernel::FileSystemStatus::Succeeded &&
                file_system.Read(read_handle, actual,
                                 os::kernel::OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES,
                                 read_bytes) == os::kernel::FileSystemStatus::Succeeded &&
                read_bytes == expected_size &&
                file_system.Close(read_handle) == os::kernel::FileSystemStatus::Succeeded;
            for (uint64_t byte_index = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
                 byte_index < expected_size; ++byte_index) {
                model_consistent = model_consistent && actual[byte_index] == expected[byte_index];
            }
        }

        expected_size =
            NextRandom(random_state) % os::kernel::OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES +
            OS_TEST_FILE_SYSTEM_RANDOMIZED_MINIMUM_PAYLOAD_SIZE_BYTES;
        for (uint64_t byte_index = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
             byte_index < expected_size; ++byte_index) {
            expected[byte_index] = static_cast<uint8_t>(NextRandom(random_state) &
                                                        OS_TEST_FILE_SYSTEM_RANDOMIZED_BYTE_MASK);
        }
        os::kernel::FileSystemHandle write_handle{};
        const os::kernel::FileSystemOpenOptions write_options{
            .readable = false,
            .writable = true,
            .create = true,
            .truncate = true,
        };
        uint64_t written_bytes = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
        model_consistent =
            model_consistent &&
            file_system.Open(OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH,
                             sizeof(OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH), write_options,
                             write_handle) == os::kernel::FileSystemStatus::Succeeded &&
            file_system.Write(write_handle, expected, expected_size, written_bytes) ==
                os::kernel::FileSystemStatus::Succeeded &&
            written_bytes == expected_size &&
            file_system.Close(write_handle) == os::kernel::FileSystemStatus::Succeeded &&
            file_system.CheckConsistency() == os::kernel::FileSystemStatus::Succeeded;
        test_context.ExpectRandom(model_consistent, OS_TEST_FILE_SYSTEM_RANDOMIZED_RESTART_MODEL,
                                  OS_TEST_FILE_SYSTEM_RANDOMIZED_SEED, iteration);
        if (!model_consistent) {
            break;
        }
    }

    return test_context.ExitCode();
}
