#include "os/kernel/pipe.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PIPE_RANDOMIZED_SUITE_NAME = "kernel/pipe/randomized";
constexpr std::string_view OS_TEST_PIPE_RANDOMIZED_MODEL_CONSISTENCY =
    "固定种子随机交错读写必须与参考字节流保持一致";
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_SEED = 0x91A7B10C5EED2047ULL;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_MULTIPLIER = 6364136223846793005ULL;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_INCREMENT = 1442695040888963407ULL;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_OPERATION_COUNT = 32768ULL;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_REFERENCE_CAPACITY_MULTIPLIER = 4ULL;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_REFERENCE_CAPACITY_BYTES =
    OS_TEST_PIPE_RANDOMIZED_OPERATION_COUNT *
    OS_TEST_PIPE_RANDOMIZED_REFERENCE_CAPACITY_MULTIPLIER;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES = 17ULL;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PIPE_RANDOMIZED_WRITE_SELECTION_MASK = 1ULL;
constexpr uint8_t OS_TEST_PIPE_RANDOMIZED_BYTE_MASK = 0xFFU;

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state = state * OS_TEST_PIPE_RANDOMIZED_MULTIPLIER + OS_TEST_PIPE_RANDOMIZED_INCREMENT;
    return state;
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_PIPE_RANDOMIZED_SUITE_NAME};
    os::kernel::Pipe pipe{};
    pipe.Initialize();

    uint8_t reference[OS_TEST_PIPE_RANDOMIZED_REFERENCE_CAPACITY_BYTES]{};
    uint64_t referenceReadIndex = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
    uint64_t referenceWriteIndex = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
    uint64_t randomState = OS_TEST_PIPE_RANDOMIZED_SEED;
    bool modelConsistent = true;

    for (uint64_t operationIndex = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
         operationIndex < OS_TEST_PIPE_RANDOMIZED_OPERATION_COUNT; ++operationIndex) {
        const bool chooseWrite =
            (NextRandom(randomState) & OS_TEST_PIPE_RANDOMIZED_WRITE_SELECTION_MASK) !=
            OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
        const uint64_t transferBytes =
            NextRandom(randomState) % OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES +
            OS_TEST_PIPE_RANDOMIZED_COUNTER_INCREMENT;

        if (chooseWrite && referenceWriteIndex + transferBytes <=
                               OS_TEST_PIPE_RANDOMIZED_REFERENCE_CAPACITY_BYTES) {
            uint8_t input[OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES]{};
            for (uint64_t byteIndex = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
                 byteIndex < transferBytes; ++byteIndex) {
                input[byteIndex] =
                    static_cast<uint8_t>(NextRandom(randomState) &
                                         static_cast<uint64_t>(OS_TEST_PIPE_RANDOMIZED_BYTE_MASK));
            }
            uint64_t writtenBytes = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
            const os::kernel::PipeStatus status = pipe.TryWrite(input, transferBytes, writtenBytes);
            modelConsistent = modelConsistent && (status == os::kernel::PipeStatus::Succeeded ||
                                                  status == os::kernel::PipeStatus::WouldBlock);
            if (status == os::kernel::PipeStatus::Succeeded) {
                for (uint64_t byteIndex = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
                     byteIndex < writtenBytes; ++byteIndex) {
                    reference[referenceWriteIndex + byteIndex] = input[byteIndex];
                }
                referenceWriteIndex += writtenBytes;
            }
        } else {
            uint8_t output[OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES]{};
            uint64_t readBytes = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
            const os::kernel::PipeStatus status = pipe.TryRead(output, transferBytes, readBytes);
            modelConsistent = modelConsistent && (status == os::kernel::PipeStatus::Succeeded ||
                                                  status == os::kernel::PipeStatus::WouldBlock);
            if (status == os::kernel::PipeStatus::Succeeded) {
                modelConsistent =
                    modelConsistent && referenceReadIndex + readBytes <= referenceWriteIndex;
                for (uint64_t byteIndex = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
                     byteIndex < readBytes; ++byteIndex) {
                    modelConsistent =
                        modelConsistent &&
                        output[byteIndex] == reference[referenceReadIndex + byteIndex];
                }
                referenceReadIndex += readBytes;
            }
        }
        const os::kernel::PipeStatistics statistics = pipe.Statistics();
        modelConsistent =
            modelConsistent && statistics.bytesWritten == referenceWriteIndex &&
            statistics.bytesRead == referenceReadIndex &&
            statistics.bufferedByteCount == referenceWriteIndex - referenceReadIndex &&
            statistics.bufferedByteCount <= os::kernel::OS_KERNEL_PIPE_CAPACITY_BYTES;
    }

    modelConsistent = modelConsistent && pipe.CloseWriter() == os::kernel::PipeStatus::Succeeded;
    uint8_t drainBuffer[OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES]{};
    while (true) {
        uint64_t readBytes = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
        const os::kernel::PipeStatus status =
            pipe.TryRead(drainBuffer, OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES, readBytes);
        if (status == os::kernel::PipeStatus::EndOfFile) {
            break;
        }
        modelConsistent = modelConsistent && status == os::kernel::PipeStatus::Succeeded;
        for (uint64_t byteIndex = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE; byteIndex < readBytes;
             ++byteIndex) {
            modelConsistent = modelConsistent &&
                              drainBuffer[byteIndex] == reference[referenceReadIndex + byteIndex];
        }
        referenceReadIndex += readBytes;
    }
    modelConsistent = modelConsistent && referenceReadIndex == referenceWriteIndex;
    testContext.Expect(modelConsistent, OS_TEST_PIPE_RANDOMIZED_MODEL_CONSISTENCY);
    return testContext.ExitCode();
}
