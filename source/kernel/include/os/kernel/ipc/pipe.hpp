#pragma once

#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PIPE_CAPACITY_BYTES = 64ULL;
inline constexpr uint64_t OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES = 64ULL * 1024ULL;
inline constexpr uint64_t OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES = 4096ULL;
inline constexpr uint64_t OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT =
    OS_KERNEL_PIPE_DYNAMIC_CAPACITY_BYTES / OS_KERNEL_PIPE_BUFFER_PAGE_SIZE_BYTES;

using PipeAllocatePageOperation = bool (*)(void *context, uint64_t &physical_address,
                                           uint8_t *&virtual_address) noexcept;
using PipeReleasePageOperation = bool (*)(void *context, uint64_t physical_address,
                                          uint8_t *virtual_address) noexcept;

struct PipePageAllocator final {
    PipeAllocatePageOperation allocate_page;
    PipeReleasePageOperation release_page;
    void *context;
};

enum class PipeStatus : uint64_t {
    Succeeded,
    NotInitialized,
    InvalidArgument,
    WouldBlock,
    EndOfFile,
    BrokenPipe,
    AlreadyClosed,
    OutOfMemory,
    ReleaseFailed,
};

struct PipeStatistics final {
    uint64_t bytes_written;
    uint64_t bytes_read;
    uint64_t write_operation_count;
    uint64_t read_operation_count;
    uint64_t buffered_byte_count;
    uint64_t capacity_bytes;
    uint64_t allocated_page_count;
    uint64_t peak_allocated_page_count;
    uint64_t page_allocation_count;
    uint64_t page_release_count;
    bool reader_closed;
    bool writer_closed;
};

class Pipe final {
  public:
    void Initialize() noexcept;
    [[nodiscard]] PipeStatus Initialize(const PipePageAllocator &page_allocator,
                                        uint64_t capacity_bytes) noexcept;
    [[nodiscard]] PipeStatus TryWrite(const uint8_t *source, uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept;
    [[nodiscard]] PipeStatus TryRead(uint8_t *destination, uint64_t capacity_bytes,
                                     uint64_t &read_bytes) noexcept;
    [[nodiscard]] PipeStatus CloseReader() noexcept;
    [[nodiscard]] PipeStatus CloseWriter() noexcept;
    [[nodiscard]] bool ReadCanProgress() noexcept;
    [[nodiscard]] bool WriteCanProgress() noexcept;
    [[nodiscard]] bool IsFullyClosed() noexcept;
    [[nodiscard]] PipeStatistics Statistics() noexcept;

  private:
    [[nodiscard]] uint64_t WritableByteCount() const noexcept;
    [[nodiscard]] uint64_t Minimum(uint64_t left, uint64_t right) const noexcept;
    [[nodiscard]] PipeStatus EnsurePagesForWrite(uint64_t length_bytes) noexcept;
    [[nodiscard]] uint8_t *ByteAddress(uint64_t byte_index) noexcept;
    [[nodiscard]] PipeStatus ReleaseDynamicPages() noexcept;

    SpinLock lock_;
    uint8_t bootstrap_buffer_[OS_KERNEL_PIPE_CAPACITY_BYTES];
    uint64_t page_physical_addresses_[OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT];
    uint8_t *page_virtual_addresses_[OS_KERNEL_PIPE_MAXIMUM_BUFFER_PAGE_COUNT];
    PipePageAllocator page_allocator_;
    uint64_t capacity_bytes_;
    uint64_t read_index_;
    uint64_t write_index_;
    uint64_t buffered_byte_count_;
    uint64_t bytes_written_;
    uint64_t bytes_read_;
    uint64_t write_operation_count_;
    uint64_t read_operation_count_;
    uint64_t allocated_page_count_;
    uint64_t peak_allocated_page_count_;
    uint64_t page_allocation_count_;
    uint64_t page_release_count_;
    bool reader_closed_;
    bool writer_closed_;
    bool dynamic_storage_;
    bool initialized_;
};

}
