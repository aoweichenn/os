#pragma once

#include "os/kernel/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PIPE_CAPACITY_BYTES = 64ULL;

enum class PipeStatus : uint64_t {
    Succeeded,
    NotInitialized,
    InvalidArgument,
    WouldBlock,
    EndOfFile,
    BrokenPipe,
    AlreadyClosed,
};

struct PipeStatistics final {
    uint64_t bytesWritten;
    uint64_t bytesRead;
    uint64_t writeOperationCount;
    uint64_t readOperationCount;
    uint64_t bufferedByteCount;
    bool readerClosed;
    bool writerClosed;
};

class Pipe final {
  public:
    void Initialize() noexcept;
    [[nodiscard]] PipeStatus TryWrite(const uint8_t *source, uint64_t lengthBytes,
                                      uint64_t &writtenBytes) noexcept;
    [[nodiscard]] PipeStatus TryRead(uint8_t *destination, uint64_t capacityBytes,
                                     uint64_t &readBytes) noexcept;
    [[nodiscard]] PipeStatus CloseReader() noexcept;
    [[nodiscard]] PipeStatus CloseWriter() noexcept;
    [[nodiscard]] bool ReadCanProgress() noexcept;
    [[nodiscard]] bool WriteCanProgress() noexcept;
    [[nodiscard]] PipeStatistics Statistics() noexcept;

  private:
    [[nodiscard]] uint64_t WritableByteCount() const noexcept;
    [[nodiscard]] uint64_t Minimum(uint64_t left, uint64_t right) const noexcept;

    SpinLock lock_;
    uint8_t buffer_[OS_KERNEL_PIPE_CAPACITY_BYTES];
    uint64_t readIndex_;
    uint64_t writeIndex_;
    uint64_t bufferedByteCount_;
    uint64_t bytesWritten_;
    uint64_t bytesRead_;
    uint64_t writeOperationCount_;
    uint64_t readOperationCount_;
    bool readerClosed_;
    bool writerClosed_;
    bool initialized_;
};

}
