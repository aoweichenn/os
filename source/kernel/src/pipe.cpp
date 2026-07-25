#include "os/kernel/pipe.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PIPE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PIPE_COUNTER_INCREMENT = 1ULL;
constexpr uint8_t OS_KERNEL_PIPE_CLEARED_BYTE = 0U;

}

void Pipe::Initialize() noexcept {
    SpinLockGuard guard{this->lock_};
    for (uint64_t byteIndex = OS_KERNEL_PIPE_EMPTY_VALUE; byteIndex < OS_KERNEL_PIPE_CAPACITY_BYTES;
         ++byteIndex) {
        this->buffer_[byteIndex] = OS_KERNEL_PIPE_CLEARED_BYTE;
    }
    this->readIndex_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->writeIndex_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bufferedByteCount_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bytesWritten_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bytesRead_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->writeOperationCount_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->readOperationCount_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->readerClosed_ = false;
    this->writerClosed_ = false;
    this->initialized_ = true;
}

PipeStatus Pipe::TryWrite(const uint8_t *source, const uint64_t lengthBytes,
                          uint64_t &writtenBytes) noexcept {
    writtenBytes = OS_KERNEL_PIPE_EMPTY_VALUE;
    if (source == nullptr || lengthBytes == OS_KERNEL_PIPE_EMPTY_VALUE) {
        return PipeStatus::InvalidArgument;
    }

    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return PipeStatus::NotInitialized;
    }
    if (this->readerClosed_) {
        return PipeStatus::BrokenPipe;
    }
    const uint64_t writableByteCount = this->WritableByteCount();
    if (writableByteCount == OS_KERNEL_PIPE_EMPTY_VALUE) {
        return PipeStatus::WouldBlock;
    }

    writtenBytes = this->Minimum(lengthBytes, writableByteCount);
    for (uint64_t byteIndex = OS_KERNEL_PIPE_EMPTY_VALUE; byteIndex < writtenBytes; ++byteIndex) {
        this->buffer_[this->writeIndex_] = source[byteIndex];
        this->writeIndex_ =
            (this->writeIndex_ + OS_KERNEL_PIPE_COUNTER_INCREMENT) % OS_KERNEL_PIPE_CAPACITY_BYTES;
    }
    this->bufferedByteCount_ += writtenBytes;
    this->bytesWritten_ += writtenBytes;
    this->writeOperationCount_ += OS_KERNEL_PIPE_COUNTER_INCREMENT;
    return PipeStatus::Succeeded;
}

PipeStatus Pipe::TryRead(uint8_t *destination, const uint64_t capacityBytes,
                         uint64_t &readBytes) noexcept {
    readBytes = OS_KERNEL_PIPE_EMPTY_VALUE;
    if (destination == nullptr || capacityBytes == OS_KERNEL_PIPE_EMPTY_VALUE) {
        return PipeStatus::InvalidArgument;
    }

    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return PipeStatus::NotInitialized;
    }
    if (this->bufferedByteCount_ == OS_KERNEL_PIPE_EMPTY_VALUE) {
        return this->writerClosed_ ? PipeStatus::EndOfFile : PipeStatus::WouldBlock;
    }

    readBytes = this->Minimum(capacityBytes, this->bufferedByteCount_);
    for (uint64_t byteIndex = OS_KERNEL_PIPE_EMPTY_VALUE; byteIndex < readBytes; ++byteIndex) {
        destination[byteIndex] = this->buffer_[this->readIndex_];
        this->readIndex_ =
            (this->readIndex_ + OS_KERNEL_PIPE_COUNTER_INCREMENT) % OS_KERNEL_PIPE_CAPACITY_BYTES;
    }
    this->bufferedByteCount_ -= readBytes;
    this->bytesRead_ += readBytes;
    this->readOperationCount_ += OS_KERNEL_PIPE_COUNTER_INCREMENT;
    return PipeStatus::Succeeded;
}

PipeStatus Pipe::CloseReader() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return PipeStatus::NotInitialized;
    }
    if (this->readerClosed_) {
        return PipeStatus::AlreadyClosed;
    }
    this->readerClosed_ = true;
    return PipeStatus::Succeeded;
}

PipeStatus Pipe::CloseWriter() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return PipeStatus::NotInitialized;
    }
    if (this->writerClosed_) {
        return PipeStatus::AlreadyClosed;
    }
    this->writerClosed_ = true;
    return PipeStatus::Succeeded;
}

bool Pipe::ReadCanProgress() noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ &&
           (this->bufferedByteCount_ != OS_KERNEL_PIPE_EMPTY_VALUE || this->writerClosed_);
}

bool Pipe::WriteCanProgress() noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ &&
           (this->readerClosed_ || this->WritableByteCount() != OS_KERNEL_PIPE_EMPTY_VALUE);
}

PipeStatistics Pipe::Statistics() noexcept {
    SpinLockGuard guard{this->lock_};
    return PipeStatistics{
        .bytesWritten = this->bytesWritten_,
        .bytesRead = this->bytesRead_,
        .writeOperationCount = this->writeOperationCount_,
        .readOperationCount = this->readOperationCount_,
        .bufferedByteCount = this->bufferedByteCount_,
        .readerClosed = this->readerClosed_,
        .writerClosed = this->writerClosed_,
    };
}

uint64_t Pipe::WritableByteCount() const noexcept {
    return OS_KERNEL_PIPE_CAPACITY_BYTES - this->bufferedByteCount_;
}

uint64_t Pipe::Minimum(const uint64_t left, const uint64_t right) const noexcept {
    return left < right ? left : right;
}

}
