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
    OS_TEST_PIPE_RANDOMIZED_OPERATION_COUNT * OS_TEST_PIPE_RANDOMIZED_REFERENCE_CAPACITY_MULTIPLIER;
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
    os::test::TestContext test_context{OS_TEST_PIPE_RANDOMIZED_SUITE_NAME};
    os::kernel::Pipe pipe{};
    pipe.Initialize();

    uint8_t reference[OS_TEST_PIPE_RANDOMIZED_REFERENCE_CAPACITY_BYTES]{};
    uint64_t reference_read_index = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
    uint64_t reference_write_index = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
    uint64_t random_state = OS_TEST_PIPE_RANDOMIZED_SEED;
    bool model_consistent = true;

    for (uint64_t operation_index = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
         operation_index < OS_TEST_PIPE_RANDOMIZED_OPERATION_COUNT; ++operation_index) {
        const bool choose_write =
            (NextRandom(random_state) & OS_TEST_PIPE_RANDOMIZED_WRITE_SELECTION_MASK) !=
            OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
        const uint64_t transfer_bytes =
            NextRandom(random_state) % OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES +
            OS_TEST_PIPE_RANDOMIZED_COUNTER_INCREMENT;

        if (choose_write && reference_write_index + transfer_bytes <=
                                OS_TEST_PIPE_RANDOMIZED_REFERENCE_CAPACITY_BYTES) {
            uint8_t input[OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES]{};
            for (uint64_t byte_index = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
                 byte_index < transfer_bytes; ++byte_index) {
                input[byte_index] =
                    static_cast<uint8_t>(NextRandom(random_state) &
                                         static_cast<uint64_t>(OS_TEST_PIPE_RANDOMIZED_BYTE_MASK));
            }
            uint64_t written_bytes = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
            const os::kernel::PipeStatus status =
                pipe.TryWrite(input, transfer_bytes, written_bytes);
            model_consistent = model_consistent && (status == os::kernel::PipeStatus::Succeeded ||
                                                    status == os::kernel::PipeStatus::WouldBlock);
            if (status == os::kernel::PipeStatus::Succeeded) {
                for (uint64_t byte_index = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
                     byte_index < written_bytes; ++byte_index) {
                    reference[reference_write_index + byte_index] = input[byte_index];
                }
                reference_write_index += written_bytes;
            }
        } else {
            uint8_t output[OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES]{};
            uint64_t read_bytes = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
            const os::kernel::PipeStatus status = pipe.TryRead(output, transfer_bytes, read_bytes);
            model_consistent = model_consistent && (status == os::kernel::PipeStatus::Succeeded ||
                                                    status == os::kernel::PipeStatus::WouldBlock);
            if (status == os::kernel::PipeStatus::Succeeded) {
                model_consistent =
                    model_consistent && reference_read_index + read_bytes <= reference_write_index;
                for (uint64_t byte_index = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
                     byte_index < read_bytes; ++byte_index) {
                    model_consistent =
                        model_consistent &&
                        output[byte_index] == reference[reference_read_index + byte_index];
                }
                reference_read_index += read_bytes;
            }
        }
        const os::kernel::PipeStatistics statistics = pipe.Statistics();
        model_consistent =
            model_consistent && statistics.bytes_written == reference_write_index &&
            statistics.bytes_read == reference_read_index &&
            statistics.buffered_byte_count == reference_write_index - reference_read_index &&
            statistics.buffered_byte_count <= os::kernel::OS_KERNEL_PIPE_CAPACITY_BYTES;
    }

    model_consistent = model_consistent && pipe.CloseWriter() == os::kernel::PipeStatus::Succeeded;
    uint8_t drain_buffer[OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES]{};
    while (true) {
        uint64_t read_bytes = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE;
        const os::kernel::PipeStatus status =
            pipe.TryRead(drain_buffer, OS_TEST_PIPE_RANDOMIZED_MAXIMUM_TRANSFER_BYTES, read_bytes);
        if (status == os::kernel::PipeStatus::EndOfFile) {
            break;
        }
        model_consistent = model_consistent && status == os::kernel::PipeStatus::Succeeded;
        for (uint64_t byte_index = OS_TEST_PIPE_RANDOMIZED_EMPTY_VALUE; byte_index < read_bytes;
             ++byte_index) {
            model_consistent = model_consistent && drain_buffer[byte_index] ==
                                                       reference[reference_read_index + byte_index];
        }
        reference_read_index += read_bytes;
    }
    model_consistent = model_consistent && reference_read_index == reference_write_index;
    test_context.Expect(model_consistent, OS_TEST_PIPE_RANDOMIZED_MODEL_CONSISTENCY);
    return test_context.ExitCode();
}
