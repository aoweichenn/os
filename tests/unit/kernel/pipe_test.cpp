#include "os/kernel/pipe.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PIPE_UNIT_SUITE_NAME = "kernel/pipe/unit";
constexpr std::string_view OS_TEST_PIPE_UNIT_REQUIRES_INITIALIZATION =
    "初始化前的管道操作必须返回明确错误";
constexpr std::string_view OS_TEST_PIPE_UNIT_PRESERVES_BYTE_STREAM =
    "跨越环形缓冲区边界的部分读写必须保持字节顺序";
constexpr std::string_view OS_TEST_PIPE_UNIT_REPORTS_BACKPRESSURE =
    "满管道必须报告 WouldBlock 且不能覆盖未读数据";
constexpr std::string_view OS_TEST_PIPE_UNIT_REPORTS_END_OF_FILE =
    "写端关闭后必须先排空数据再报告 EOF";
constexpr std::string_view OS_TEST_PIPE_UNIT_REPORTS_BROKEN_PIPE =
    "读端关闭后写入必须报告 BrokenPipe";
constexpr uint64_t OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES = 48ULL;
constexpr uint64_t OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES = 32ULL;
constexpr uint64_t OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES = 40ULL;
constexpr uint64_t OS_TEST_PIPE_UNIT_FINAL_READ_CAPACITY_BYTES = 64ULL;
constexpr uint64_t OS_TEST_PIPE_UNIT_EXPECTED_FINAL_READ_SIZE_BYTES =
    OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES - OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES +
    OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES;
constexpr uint64_t OS_TEST_PIPE_UNIT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PIPE_UNIT_COUNTER_INCREMENT = 1ULL;
constexpr uint8_t OS_TEST_PIPE_UNIT_BYTE_OFFSET = 17U;
constexpr uint8_t OS_TEST_PIPE_UNIT_ZERO_BYTE = 0U;

[[nodiscard]] uint8_t ExpectedByte(const uint64_t byte_index) noexcept {
    return static_cast<uint8_t>(byte_index + static_cast<uint64_t>(OS_TEST_PIPE_UNIT_BYTE_OFFSET));
}
}

int main() {
    os::test::TestContext test_context{OS_TEST_PIPE_UNIT_SUITE_NAME};

    os::kernel::Pipe uninitialized_pipe{};
    uint8_t uninitialized_byte = OS_TEST_PIPE_UNIT_ZERO_BYTE;
    uint64_t uninitialized_byte_count = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
    test_context.Expect(uninitialized_pipe.TryRead(
                            &uninitialized_byte, OS_TEST_PIPE_UNIT_COUNTER_INCREMENT,
                            uninitialized_byte_count) == os::kernel::PipeStatus::NotInitialized,
                        OS_TEST_PIPE_UNIT_REQUIRES_INITIALIZATION);

    uint8_t first_input[OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES]{};
    uint8_t second_input[OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES]{};
    for (uint64_t byte_index = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
         byte_index < OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES; ++byte_index) {
        first_input[byte_index] = ExpectedByte(byte_index);
    }
    for (uint64_t byte_index = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
         byte_index < OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES; ++byte_index) {
        second_input[byte_index] =
            ExpectedByte(OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES + byte_index);
    }

    os::kernel::Pipe pipe{};
    pipe.Initialize();
    uint64_t written_bytes = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
    uint64_t read_bytes = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
    uint8_t first_output[OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES]{};
    bool stream_operations_succeeded =
        pipe.TryWrite(first_input, OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES, written_bytes) ==
            os::kernel::PipeStatus::Succeeded &&
        written_bytes == OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES &&
        pipe.TryRead(first_output, OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES, read_bytes) ==
            os::kernel::PipeStatus::Succeeded &&
        read_bytes == OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES &&
        pipe.TryWrite(second_input, OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES, written_bytes) ==
            os::kernel::PipeStatus::Succeeded &&
        written_bytes == OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES;
    for (uint64_t byte_index = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
         byte_index < OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES; ++byte_index) {
        stream_operations_succeeded =
            stream_operations_succeeded && first_output[byte_index] == ExpectedByte(byte_index);
    }

    uint8_t final_output[OS_TEST_PIPE_UNIT_FINAL_READ_CAPACITY_BYTES]{};
    stream_operations_succeeded =
        stream_operations_succeeded &&
        pipe.TryRead(final_output, OS_TEST_PIPE_UNIT_FINAL_READ_CAPACITY_BYTES, read_bytes) ==
            os::kernel::PipeStatus::Succeeded &&
        read_bytes == OS_TEST_PIPE_UNIT_EXPECTED_FINAL_READ_SIZE_BYTES;
    for (uint64_t byte_index = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
         byte_index < OS_TEST_PIPE_UNIT_EXPECTED_FINAL_READ_SIZE_BYTES; ++byte_index) {
        stream_operations_succeeded =
            stream_operations_succeeded &&
            final_output[byte_index] ==
                ExpectedByte(OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES + byte_index);
    }
    test_context.Expect(stream_operations_succeeded, OS_TEST_PIPE_UNIT_PRESERVES_BYTE_STREAM);

    os::kernel::Pipe full_pipe{};
    full_pipe.Initialize();
    uint8_t full_input[os::kernel::OS_KERNEL_PIPE_CAPACITY_BYTES]{};
    const bool backpressure_reported =
        full_pipe.TryWrite(full_input, os::kernel::OS_KERNEL_PIPE_CAPACITY_BYTES, written_bytes) ==
            os::kernel::PipeStatus::Succeeded &&
        full_pipe.TryWrite(full_input, OS_TEST_PIPE_UNIT_COUNTER_INCREMENT, written_bytes) ==
            os::kernel::PipeStatus::WouldBlock &&
        written_bytes == OS_TEST_PIPE_UNIT_EMPTY_VALUE;
    test_context.Expect(backpressure_reported, OS_TEST_PIPE_UNIT_REPORTS_BACKPRESSURE);

    os::kernel::Pipe end_of_file_pipe{};
    end_of_file_pipe.Initialize();
    const bool end_of_file_reported =
        end_of_file_pipe.TryWrite(first_input, OS_TEST_PIPE_UNIT_COUNTER_INCREMENT,
                                  written_bytes) == os::kernel::PipeStatus::Succeeded &&
        end_of_file_pipe.CloseWriter() == os::kernel::PipeStatus::Succeeded &&
        end_of_file_pipe.TryRead(first_output, OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES,
                                 read_bytes) == os::kernel::PipeStatus::Succeeded &&
        read_bytes == OS_TEST_PIPE_UNIT_COUNTER_INCREMENT &&
        end_of_file_pipe.TryRead(first_output, OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES,
                                 read_bytes) == os::kernel::PipeStatus::EndOfFile &&
        end_of_file_pipe.CloseWriter() == os::kernel::PipeStatus::AlreadyClosed;
    test_context.Expect(end_of_file_reported, OS_TEST_PIPE_UNIT_REPORTS_END_OF_FILE);

    os::kernel::Pipe broken_pipe{};
    broken_pipe.Initialize();
    const bool broken_pipe_reported =
        broken_pipe.CloseReader() == os::kernel::PipeStatus::Succeeded &&
        broken_pipe.TryWrite(first_input, OS_TEST_PIPE_UNIT_COUNTER_INCREMENT, written_bytes) ==
            os::kernel::PipeStatus::BrokenPipe &&
        broken_pipe.CloseReader() == os::kernel::PipeStatus::AlreadyClosed;
    test_context.Expect(broken_pipe_reported, OS_TEST_PIPE_UNIT_REPORTS_BROKEN_PIPE);
    return test_context.ExitCode();
}
