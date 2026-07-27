#include "os/kernel/ipc/pipe.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PIPE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PIPE_COUNTER_INCREMENT = 1ULL;
constexpr uint8_t OS_KERNEL_PIPE_CLEARED_BYTE = 0U;

[[nodiscard]] uint64_t Maximum(const uint64_t left, const uint64_t right) noexcept {
    return left > right ? left : right;
}

}

void Pipe::Initialize() noexcept {
    SpinLockGuard guard{this->lock_};
    for (uint64_t byte_index = OS_KERNEL_PIPE_EMPTY_VALUE;
         byte_index < OS_KERNEL_PIPE_CAPACITY_BYTES; ++byte_index) {
        this->bootstrap_buffer_[byte_index] = OS_KERNEL_PIPE_CLEARED_BYTE;
    }
    for (uint64_t page_index = OS_KERNEL_PIPE_EMPTY_VALUE;
         page_index < OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT; ++page_index) {
        this->page_physical_addresses_[page_index] = OS_KERNEL_PIPE_EMPTY_VALUE;
        this->page_virtual_addresses_[page_index] = nullptr;
    }
    this->page_allocator_ = PipePageAllocator{};
    this->capacity_bytes_ = OS_KERNEL_PIPE_CAPACITY_BYTES;
    this->read_index_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->write_index_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->buffered_byte_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bytes_written_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bytes_read_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->write_operation_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->read_operation_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->allocated_page_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->peak_allocated_page_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->page_allocation_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->page_release_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->reader_closed_ = false;
    this->writer_closed_ = false;
    this->dynamic_storage_ = false;
    this->initialized_ = true;
}

PipeStatus Pipe::Initialize(const PipePageAllocator &page_allocator,
                            const uint64_t capacity_bytes) noexcept {
    if (page_allocator.allocate_page == nullptr || page_allocator.release_page == nullptr ||
        capacity_bytes == OS_KERNEL_PIPE_EMPTY_VALUE ||
        capacity_bytes > OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES ||
        capacity_bytes % OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES != OS_KERNEL_PIPE_EMPTY_VALUE) {
        return PipeStatus::InvalidArgument;
    }
    SpinLockGuard guard{this->lock_};
    if (this->initialized_ &&
        (!this->reader_closed_ || !this->writer_closed_ ||
         this->allocated_page_count_ != OS_KERNEL_PIPE_EMPTY_VALUE)) {
        return PipeStatus::InvalidArgument;
    }
    for (uint64_t page_index = OS_KERNEL_PIPE_EMPTY_VALUE;
         page_index < OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT; ++page_index) {
        this->page_physical_addresses_[page_index] = OS_KERNEL_PIPE_EMPTY_VALUE;
        this->page_virtual_addresses_[page_index] = nullptr;
    }
    this->page_allocator_ = page_allocator;
    this->capacity_bytes_ = capacity_bytes;
    this->read_index_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->write_index_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->buffered_byte_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bytes_written_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->bytes_read_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->write_operation_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->read_operation_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->allocated_page_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->peak_allocated_page_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->page_allocation_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->page_release_count_ = OS_KERNEL_PIPE_EMPTY_VALUE;
    this->reader_closed_ = false;
    this->writer_closed_ = false;
    this->dynamic_storage_ = true;
    this->initialized_ = true;
    return PipeStatus::Succeeded;
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
    const PipeStatus page_status = this->EnsurePagesForWrite(written_bytes);
    if (page_status != PipeStatus::Succeeded) {
        written_bytes = OS_KERNEL_PIPE_EMPTY_VALUE;
        return page_status;
    }
    for (uint64_t byte_index = OS_KERNEL_PIPE_EMPTY_VALUE; byte_index < written_bytes;
         ++byte_index) {
        uint8_t *const destination = this->ByteAddress(this->write_index_);
        if (destination == nullptr) {
            written_bytes = OS_KERNEL_PIPE_EMPTY_VALUE;
            return PipeStatus::InvalidArgument;
        }
        *destination = source[byte_index];
        this->write_index_ =
            (this->write_index_ + OS_KERNEL_PIPE_COUNTER_INCREMENT) % this->capacity_bytes_;
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
        uint8_t *const source = this->ByteAddress(this->read_index_);
        if (source == nullptr) {
            read_bytes = OS_KERNEL_PIPE_EMPTY_VALUE;
            return PipeStatus::InvalidArgument;
        }
        destination[byte_index] = *source;
        this->read_index_ =
            (this->read_index_ + OS_KERNEL_PIPE_COUNTER_INCREMENT) % this->capacity_bytes_;
    }
    this->buffered_byte_count_ -= read_bytes;
    this->bytes_read_ += read_bytes;
    this->read_operation_count_ += OS_KERNEL_PIPE_COUNTER_INCREMENT;
    return PipeStatus::Succeeded;
}

PipeStatus Pipe::CloseReader() noexcept {
    this->lock_.Lock();
    if (!this->initialized_) {
        this->lock_.Unlock();
        return PipeStatus::NotInitialized;
    }
    if (this->reader_closed_) {
        this->lock_.Unlock();
        return PipeStatus::AlreadyClosed;
    }
    this->reader_closed_ = true;
    const bool release_pages = this->writer_closed_ && this->dynamic_storage_;
    this->lock_.Unlock();
    return release_pages ? this->ReleaseDynamicPages() : PipeStatus::Succeeded;
}

PipeStatus Pipe::CloseWriter() noexcept {
    this->lock_.Lock();
    if (!this->initialized_) {
        this->lock_.Unlock();
        return PipeStatus::NotInitialized;
    }
    if (this->writer_closed_) {
        this->lock_.Unlock();
        return PipeStatus::AlreadyClosed;
    }
    this->writer_closed_ = true;
    const bool release_pages = this->reader_closed_ && this->dynamic_storage_;
    this->lock_.Unlock();
    return release_pages ? this->ReleaseDynamicPages() : PipeStatus::Succeeded;
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

bool Pipe::IsFullyClosed() noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ && this->reader_closed_ && this->writer_closed_;
}

PipeStatistics Pipe::Statistics() noexcept {
    SpinLockGuard guard{this->lock_};
    return PipeStatistics{
        .bytes_written = this->bytes_written_,
        .bytes_read = this->bytes_read_,
        .write_operation_count = this->write_operation_count_,
        .read_operation_count = this->read_operation_count_,
        .buffered_byte_count = this->buffered_byte_count_,
        .capacity_bytes = this->capacity_bytes_,
        .allocated_page_count = this->allocated_page_count_,
        .peak_allocated_page_count = this->peak_allocated_page_count_,
        .page_allocation_count = this->page_allocation_count_,
        .page_release_count = this->page_release_count_,
        .reader_closed = this->reader_closed_,
        .writer_closed = this->writer_closed_,
    };
}

uint64_t Pipe::WritableByteCount() const noexcept {
    return this->capacity_bytes_ - this->buffered_byte_count_;
}

uint64_t Pipe::Minimum(const uint64_t left, const uint64_t right) const noexcept {
    return left < right ? left : right;
}

PipeStatus Pipe::EnsurePagesForWrite(const uint64_t length_bytes) noexcept {
    if (!this->dynamic_storage_) {
        return PipeStatus::Succeeded;
    }
    bool allocated_during_operation[OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT]{};
    for (uint64_t byte_offset = OS_KERNEL_PIPE_EMPTY_VALUE; byte_offset < length_bytes;
         ++byte_offset) {
        const uint64_t buffer_index = (this->write_index_ + byte_offset) % this->capacity_bytes_;
        const uint64_t page_index = buffer_index / OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES;
        if (this->page_virtual_addresses_[page_index] != nullptr) {
            continue;
        }
        uint64_t physical_address = OS_KERNEL_PIPE_EMPTY_VALUE;
        uint8_t *virtual_address = nullptr;
        if (!this->page_allocator_.allocate_page(this->page_allocator_.context, physical_address,
                                                 virtual_address) ||
            physical_address == OS_KERNEL_PIPE_EMPTY_VALUE || virtual_address == nullptr) {
            for (uint64_t rollback_page_index = OS_KERNEL_PIPE_EMPTY_VALUE;
                 rollback_page_index < OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT;
                 ++rollback_page_index) {
                if (!allocated_during_operation[rollback_page_index]) {
                    continue;
                }
                static_cast<void>(this->page_allocator_.release_page(
                    this->page_allocator_.context,
                    this->page_physical_addresses_[rollback_page_index],
                    this->page_virtual_addresses_[rollback_page_index]));
                this->page_physical_addresses_[rollback_page_index] =
                    OS_KERNEL_PIPE_EMPTY_VALUE;
                this->page_virtual_addresses_[rollback_page_index] = nullptr;
                --this->allocated_page_count_;
                ++this->page_release_count_;
            }
            return PipeStatus::OutOfMemory;
        }
        for (uint64_t page_byte_index = OS_KERNEL_PIPE_EMPTY_VALUE;
             page_byte_index < OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES; ++page_byte_index) {
            virtual_address[page_byte_index] = OS_KERNEL_PIPE_CLEARED_BYTE;
        }
        this->page_physical_addresses_[page_index] = physical_address;
        this->page_virtual_addresses_[page_index] = virtual_address;
        allocated_during_operation[page_index] = true;
        ++this->allocated_page_count_;
        ++this->page_allocation_count_;
        this->peak_allocated_page_count_ =
            Maximum(this->peak_allocated_page_count_, this->allocated_page_count_);
    }
    return PipeStatus::Succeeded;
}

uint8_t *Pipe::ByteAddress(const uint64_t byte_index) noexcept {
    if (!this->dynamic_storage_) {
        return byte_index < OS_KERNEL_PIPE_CAPACITY_BYTES
                   ? &this->bootstrap_buffer_[byte_index]
                   : nullptr;
    }
    const uint64_t page_index = byte_index / OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES;
    const uint64_t page_offset = byte_index % OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES;
    return page_index < OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT &&
                   this->page_virtual_addresses_[page_index] != nullptr
               ? this->page_virtual_addresses_[page_index] + page_offset
               : nullptr;
}

PipeStatus Pipe::ReleaseDynamicPages() noexcept {
    bool succeeded = true;
    this->lock_.Lock();
    for (uint64_t page_index = OS_KERNEL_PIPE_EMPTY_VALUE;
         page_index < OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT; ++page_index) {
        if (this->page_virtual_addresses_[page_index] == nullptr) {
            continue;
        }
        const uint64_t physical_address = this->page_physical_addresses_[page_index];
        uint8_t *const virtual_address = this->page_virtual_addresses_[page_index];
        if (!this->page_allocator_.release_page(this->page_allocator_.context, physical_address,
                                                virtual_address)) {
            succeeded = false;
        } else {
            this->page_physical_addresses_[page_index] = OS_KERNEL_PIPE_EMPTY_VALUE;
            this->page_virtual_addresses_[page_index] = nullptr;
            --this->allocated_page_count_;
            ++this->page_release_count_;
        }
    }
    this->lock_.Unlock();
    return succeeded ? PipeStatus::Succeeded : PipeStatus::ReleaseFailed;
}

}
