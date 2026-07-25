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
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t write_operation_count;
    uint64_t read_operation_count;
    uint64_t buffered_byte_count;
    bool reader_closed;
    bool writer_closed;
};

class Pipe final {
  public:
    void Initialize() noexcept;
    [[nodiscard]] PipeStatus TryWrite(const uint8_t *source, uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept;
    [[nodiscard]] PipeStatus TryRead(uint8_t *destination, uint64_t capacity_bytes,
                                     uint64_t &read_bytes) noexcept;
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
    uint64_t read_index_;
    uint64_t write_index_;
    uint64_t buffered_byte_count_;
    uint64_t bytes_written_;
    uint64_t bytes_read_;
    uint64_t write_operation_count_;
    uint64_t read_operation_count_;
    bool reader_closed_;
    bool writer_closed_;
    bool initialized_;
};

}
