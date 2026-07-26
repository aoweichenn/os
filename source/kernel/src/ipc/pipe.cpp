#include "os/kernel/ipc/pipe.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PIPE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PIPE_COUNTER_INCREMENT = 1ULL;
constexpr uint8_t OS_KERNEL_PIPE_CLEARED_BYTE = 0U;

}

void Pipe::Initialize() noexcept {
    SpinLockGuard guard{this->lock_};
    for (uint64_t byte_index = OS_KERNEL_PIPE_EMPTY_VALUE;
         byte_index < OS_KERNEL_PIPE_CAPACITY_BYTES; ++byte_index) {
        this->buffer_[byte_index] = OS_KERNEL_PIPE_CLEARED_BYTE;
    }
    this->read_index_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->write_index_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->buffered_byte_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bytes_written_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bytes_read_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->write_operation_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->read_operation_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->reader_closed_ = false;
    this->writer_closed_ = false;
    this->initialized_ = true;
}

PipeStatus Pipe::TryWrite(const uint8_t *source, const uint64_t length_bytes,
                          uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_PIPE_EMPTY_VALUE;
    if (source == nullptr || length_bytes == OS_KERNEL_PIPE_EMPTY_VALUE) {
        return PipeStatus::InvalidArgument;
    }

    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return PipeStatus::NotInitialized;
    }
    if (this->reader_closed_) {
        return PipeStatus::BrokenPipe;
    }
    const uint64_t writable_byte_count = this->WritableByteCount();
    if (writable_byte_count == OS_KERNEL_PIPE_EMPTY_VALUE) {
        return PipeStatus::WouldBlock;
    }

    written_bytes = this->Minimum(length_bytes, writable_byte_count);
    for (uint64_t byte_index = OS_KERNEL_PIPE_EMPTY_VALUE; byte_index < written_bytes;
         ++byte_index) {
        this->buffer_[this->write_index_] = source[byte_index];
        this->write_index_ =
            (this->write_index_ + OS_KERNEL_PIPE_COUNTER_INCREMENT) % OS_KERNEL_PIPE_CAPACITY_BYTES;
    }
    this->buffered_byte_count_ += written_bytes;
    this->bytes_written_ += written_bytes;
    this->write_operation_count_ += OS_KERNEL_PIPE_COUNTER_INCREMENT;
    return PipeStatus::Succeeded;
}

PipeStatus Pipe::TryRead(uint8_t *destination, const uint64_t capacity_bytes,
                         uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_PIPE_EMPTY_VALUE;
    if (destination == nullptr || capacity_bytes == OS_KERNEL_PIPE_EMPTY_VALUE) {
        return PipeStatus::InvalidArgument;
    }

    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return PipeStatus::NotInitialized;
    }
    if (this->buffered_byte_count_ == OS_KERNEL_PIPE_EMPTY_VALUE) {
        return this->writer_closed_ ? PipeStatus::EndOfFile : PipeStatus::WouldBlock;
    }

    read_bytes = this->Minimum(capacity_bytes, this->buffered_byte_count_);
    for (uint64_t byte_index = OS_KERNEL_PIPE_EMPTY_VALUE; byte_index < read_bytes; ++byte_index) {
        destination[byte_index] = this->buffer_[this->read_index_];
        this->read_index_ =
            (this->read_index_ + OS_KERNEL_PIPE_COUNTER_INCREMENT) % OS_KERNEL_PIPE_CAPACITY_BYTES;
    }
    this->buffered_byte_count_ -= read_bytes;
    this->bytes_read_ += read_bytes;
    this->read_operation_count_ += OS_KERNEL_PIPE_COUNTER_INCREMENT;
    return PipeStatus::Succeeded;
}

PipeStatus Pipe::CloseReader() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return PipeStatus::NotInitialized;
    }
    if (this->reader_closed_) {
        return PipeStatus::AlreadyClosed;
    }
    this->reader_closed_ = true;
    return PipeStatus::Succeeded;
}

PipeStatus Pipe::CloseWriter() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return PipeStatus::NotInitialized;
    }
    if (this->writer_closed_) {
        return PipeStatus::AlreadyClosed;
    }
    this->writer_closed_ = true;
    return PipeStatus::Succeeded;
}

bool Pipe::ReadCanProgress() noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ &&
           (this->buffered_byte_count_ != OS_KERNEL_PIPE_EMPTY_VALUE || this->writer_closed_);
}

bool Pipe::WriteCanProgress() noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ &&
           (this->reader_closed_ || this->WritableByteCount() != OS_KERNEL_PIPE_EMPTY_VALUE);
}

PipeStatistics Pipe::Statistics() noexcept {
    SpinLockGuard guard{this->lock_};
    return PipeStatistics{
        .bytes_written = this->bytes_written_,
        .bytes_read = this->bytes_read_,
        .write_operation_count = this->write_operation_count_,
        .read_operation_count = this->read_operation_count_,
        .buffered_byte_count = this->buffered_byte_count_,
        .reader_closed = this->reader_closed_,
        .writer_closed = this->writer_closed_,
    };
}

uint64_t Pipe::WritableByteCount() const noexcept {
    return OS_KERNEL_PIPE_CAPACITY_BYTES - this->buffered_byte_count_;
}

uint64_t Pipe::Minimum(const uint64_t left, const uint64_t right) const noexcept {
    return left < right ? left : right;
}

}
