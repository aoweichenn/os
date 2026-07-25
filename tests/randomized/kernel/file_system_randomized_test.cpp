#include "memory_block_device.hpp"
#include "os/kernel/file_system.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_SYSTEM_RANDOMIZED_SUITE_NAME =
    "kernel/file_system/randomized";
constexpr std::string_view OS_TEST_FILE_SYSTEM_RANDOMIZED_RESTART_MODEL =
    "固定种子随机长度与内容必须在每次重挂载后和参考模型一致";
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_SEED =
    0xF17E5A57C0DEC0DEULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_MULTIPLIER =
    6364136223846793005ULL;
constexpr uint64_t OS_TEST_FILE_SYSTEM_RANDOMIZED_INCREMENT =
    1442695040888963407ULL;
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
    os::test::TestContext testContext{
        OS_TEST_FILE_SYSTEM_RANDOMIZED_SUITE_NAME};
    static os::test::MemoryBlockDevice device{};
    uint8_t expected[os::kernel::OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES]{};
    uint64_t expectedSize = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
    uint64_t randomState = OS_TEST_FILE_SYSTEM_RANDOMIZED_SEED;
    bool modelConsistent = true;

    for (uint64_t iteration = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
         iteration < OS_TEST_FILE_SYSTEM_RANDOMIZED_ITERATION_COUNT;
         ++iteration) {
        os::kernel::FileSystem fileSystem{};
        bool formatted = false;
        modelConsistent =
            modelConsistent &&
            fileSystem.MountOrFormat(device, formatted) ==
                os::kernel::FileSystemStatus::Succeeded;
        if (iteration == OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE) {
            modelConsistent =
                modelConsistent && formatted &&
                fileSystem.CreateDirectory(
                    OS_TEST_FILE_SYSTEM_RANDOMIZED_DIRECTORY_PATH,
                    sizeof(OS_TEST_FILE_SYSTEM_RANDOMIZED_DIRECTORY_PATH)) ==
                    os::kernel::FileSystemStatus::Succeeded;
        } else {
            modelConsistent = modelConsistent && !formatted;
            os::kernel::FileSystemHandle readHandle{};
            const os::kernel::FileSystemOpenOptions readOptions{
                .readable = true,
                .writable = false,
                .create = false,
                .truncate = false,
            };
            uint8_t actual[
                os::kernel::OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES]{};
            uint64_t readBytes = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
            modelConsistent =
                modelConsistent &&
                fileSystem.Open(
                    OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH,
                    sizeof(OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH),
                    readOptions, readHandle) ==
                    os::kernel::FileSystemStatus::Succeeded &&
                fileSystem.Read(
                    readHandle, actual,
                    os::kernel::OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES,
                    readBytes) == os::kernel::FileSystemStatus::Succeeded &&
                readBytes == expectedSize &&
                fileSystem.Close(readHandle) ==
                    os::kernel::FileSystemStatus::Succeeded;
            for (uint64_t byteIndex =
                     OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
                 byteIndex < expectedSize; ++byteIndex) {
                modelConsistent =
                    modelConsistent &&
                    actual[byteIndex] == expected[byteIndex];
            }
        }

        expectedSize =
            NextRandom(randomState) %
                os::kernel::OS_KERNEL_FILE_SYSTEM_MAXIMUM_FILE_SIZE_BYTES +
            OS_TEST_FILE_SYSTEM_RANDOMIZED_MINIMUM_PAYLOAD_SIZE_BYTES;
        for (uint64_t byteIndex = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
             byteIndex < expectedSize; ++byteIndex) {
            expected[byteIndex] = static_cast<uint8_t>(
                NextRandom(randomState) &
                OS_TEST_FILE_SYSTEM_RANDOMIZED_BYTE_MASK);
        }
        os::kernel::FileSystemHandle writeHandle{};
        const os::kernel::FileSystemOpenOptions writeOptions{
            .readable = false,
            .writable = true,
            .create = true,
            .truncate = true,
        };
        uint64_t writtenBytes = OS_TEST_FILE_SYSTEM_RANDOMIZED_EMPTY_VALUE;
        modelConsistent =
            modelConsistent &&
            fileSystem.Open(
                OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH,
                sizeof(OS_TEST_FILE_SYSTEM_RANDOMIZED_FILE_PATH), writeOptions,
                writeHandle) == os::kernel::FileSystemStatus::Succeeded &&
            fileSystem.Write(writeHandle, expected, expectedSize,
                             writtenBytes) ==
                os::kernel::FileSystemStatus::Succeeded &&
            writtenBytes == expectedSize &&
            fileSystem.Close(writeHandle) ==
                os::kernel::FileSystemStatus::Succeeded &&
            fileSystem.CheckConsistency() ==
                os::kernel::FileSystemStatus::Succeeded;
        testContext.ExpectRandom(
            modelConsistent, OS_TEST_FILE_SYSTEM_RANDOMIZED_RESTART_MODEL,
            OS_TEST_FILE_SYSTEM_RANDOMIZED_SEED, iteration);
        if (!modelConsistent) {
            break;
        }
    }

    return testContext.ExitCode();
}
