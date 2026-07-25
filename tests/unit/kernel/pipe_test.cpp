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

[[nodiscard]] uint8_t ExpectedByte(const uint64_t byteIndex) noexcept {
    return static_cast<uint8_t>(byteIndex + static_cast<uint64_t>(OS_TEST_PIPE_UNIT_BYTE_OFFSET));
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_PIPE_UNIT_SUITE_NAME};

    os::kernel::Pipe uninitializedPipe{};
    uint8_t uninitializedByte = OS_TEST_PIPE_UNIT_ZERO_BYTE;
    uint64_t uninitializedByteCount = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
    testContext.Expect(
        uninitializedPipe.TryRead(&uninitializedByte, OS_TEST_PIPE_UNIT_COUNTER_INCREMENT,
                                  uninitializedByteCount) == os::kernel::PipeStatus::NotInitialized,
        OS_TEST_PIPE_UNIT_REQUIRES_INITIALIZATION);

    uint8_t firstInput[OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES]{};
    uint8_t secondInput[OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES]{};
    for (uint64_t byteIndex = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
         byteIndex < OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES; ++byteIndex) {
        firstInput[byteIndex] = ExpectedByte(byteIndex);
    }
    for (uint64_t byteIndex = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
         byteIndex < OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES; ++byteIndex) {
        secondInput[byteIndex] = ExpectedByte(OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES + byteIndex);
    }

    os::kernel::Pipe pipe{};
    pipe.Initialize();
    uint64_t writtenBytes = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
    uint64_t readBytes = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
    uint8_t firstOutput[OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES]{};
    bool streamOperationsSucceeded =
        pipe.TryWrite(firstInput, OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES, writtenBytes) ==
            os::kernel::PipeStatus::Succeeded &&
        writtenBytes == OS_TEST_PIPE_UNIT_FIRST_WRITE_SIZE_BYTES &&
        pipe.TryRead(firstOutput, OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES, readBytes) ==
            os::kernel::PipeStatus::Succeeded &&
        readBytes == OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES &&
        pipe.TryWrite(secondInput, OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES, writtenBytes) ==
            os::kernel::PipeStatus::Succeeded &&
        writtenBytes == OS_TEST_PIPE_UNIT_SECOND_WRITE_SIZE_BYTES;
    for (uint64_t byteIndex = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
         byteIndex < OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES; ++byteIndex) {
        streamOperationsSucceeded =
            streamOperationsSucceeded && firstOutput[byteIndex] == ExpectedByte(byteIndex);
    }

    uint8_t finalOutput[OS_TEST_PIPE_UNIT_FINAL_READ_CAPACITY_BYTES]{};
    streamOperationsSucceeded =
        streamOperationsSucceeded &&
        pipe.TryRead(finalOutput, OS_TEST_PIPE_UNIT_FINAL_READ_CAPACITY_BYTES, readBytes) ==
            os::kernel::PipeStatus::Succeeded &&
        readBytes == OS_TEST_PIPE_UNIT_EXPECTED_FINAL_READ_SIZE_BYTES;
    for (uint64_t byteIndex = OS_TEST_PIPE_UNIT_EMPTY_VALUE;
         byteIndex < OS_TEST_PIPE_UNIT_EXPECTED_FINAL_READ_SIZE_BYTES; ++byteIndex) {
        streamOperationsSucceeded =
            streamOperationsSucceeded &&
            finalOutput[byteIndex] ==
                ExpectedByte(OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES + byteIndex);
    }
    testContext.Expect(streamOperationsSucceeded, OS_TEST_PIPE_UNIT_PRESERVES_BYTE_STREAM);

    os::kernel::Pipe fullPipe{};
    fullPipe.Initialize();
    uint8_t fullInput[os::kernel::OS_KERNEL_PIPE_CAPACITY_BYTES]{};
    const bool backpressureReported =
        fullPipe.TryWrite(fullInput, os::kernel::OS_KERNEL_PIPE_CAPACITY_BYTES, writtenBytes) ==
            os::kernel::PipeStatus::Succeeded &&
        fullPipe.TryWrite(fullInput, OS_TEST_PIPE_UNIT_COUNTER_INCREMENT, writtenBytes) ==
            os::kernel::PipeStatus::WouldBlock &&
        writtenBytes == OS_TEST_PIPE_UNIT_EMPTY_VALUE;
    testContext.Expect(backpressureReported, OS_TEST_PIPE_UNIT_REPORTS_BACKPRESSURE);

    os::kernel::Pipe endOfFilePipe{};
    endOfFilePipe.Initialize();
    const bool endOfFileReported =
        endOfFilePipe.TryWrite(firstInput, OS_TEST_PIPE_UNIT_COUNTER_INCREMENT, writtenBytes) ==
            os::kernel::PipeStatus::Succeeded &&
        endOfFilePipe.CloseWriter() == os::kernel::PipeStatus::Succeeded &&
        endOfFilePipe.TryRead(firstOutput, OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES, readBytes) ==
            os::kernel::PipeStatus::Succeeded &&
        readBytes == OS_TEST_PIPE_UNIT_COUNTER_INCREMENT &&
        endOfFilePipe.TryRead(firstOutput, OS_TEST_PIPE_UNIT_FIRST_READ_SIZE_BYTES, readBytes) ==
            os::kernel::PipeStatus::EndOfFile &&
        endOfFilePipe.CloseWriter() == os::kernel::PipeStatus::AlreadyClosed;
    testContext.Expect(endOfFileReported, OS_TEST_PIPE_UNIT_REPORTS_END_OF_FILE);

    os::kernel::Pipe brokenPipe{};
    brokenPipe.Initialize();
    const bool brokenPipeReported =
        brokenPipe.CloseReader() == os::kernel::PipeStatus::Succeeded &&
        brokenPipe.TryWrite(firstInput, OS_TEST_PIPE_UNIT_COUNTER_INCREMENT, writtenBytes) ==
            os::kernel::PipeStatus::BrokenPipe &&
        brokenPipe.CloseReader() == os::kernel::PipeStatus::AlreadyClosed;
    testContext.Expect(brokenPipeReported, OS_TEST_PIPE_UNIT_REPORTS_BROKEN_PIPE);
    return testContext.ExitCode();
}
