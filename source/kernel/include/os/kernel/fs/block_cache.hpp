#pragma once

#include "os/kernel/fs/file_system_format.hpp"
#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT = 8ULL;

enum class FileSystemBlockDeviceStatus : uint64_t {
    Succeeded,
    InvalidBlock,
    InvalidBuffer,
    ReadFailed,
    WriteFailed,
    FlushFailed,
};

class FileSystemBlockDevice {
  public:
    FileSystemBlockDevice() noexcept = default;
    FileSystemBlockDevice(const FileSystemBlockDevice &) = delete;
    FileSystemBlockDevice &operator=(const FileSystemBlockDevice &) = delete;

    [[nodiscard]] virtual FileSystemBlockDeviceStatus
    ReadBlock(uint64_t logical_block_address, uint8_t *block, uint64_t block_size_bytes) noexcept;
    [[nodiscard]] virtual FileSystemBlockDeviceStatus
    WriteBlock(uint64_t logical_block_address, const uint8_t *block,
               uint64_t block_size_bytes) noexcept;
    [[nodiscard]] virtual FileSystemBlockDeviceStatus Flush() noexcept;

  protected:
    ~FileSystemBlockDevice() noexcept = default;
};

enum class BlockCacheStatus : uint64_t {
    Succeeded,
    NotInitialized,
    NullBuffer,
    InvalidBufferSize,
    DeviceReadFailed,
    DeviceWriteFailed,
    DeviceFlushFailed,
};

struct BlockCacheStatistics final {
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t eviction_count;
    uint64_t device_read_count;
    uint64_t device_write_count;
    uint64_t flush_count;
};

class BlockCache final {
  public:
    BlockCache() noexcept = default;

    void Initialize(FileSystemBlockDevice &device) noexcept;
    [[nodiscard]] BlockCacheStatus ReadBlock(uint64_t logical_block_address, uint8_t *block,
                                             uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockCacheStatus WriteBlock(uint64_t logical_block_address, const uint8_t *block,
                                              uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockCacheStatus Sync() noexcept;
    void Invalidate() noexcept;
    [[nodiscard]] BlockCacheStatistics Statistics() const noexcept;

  private:
    struct Entry final {
        uint8_t bytes[OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES];
        uint64_t logical_block_address;
        uint64_t access_generation;
        bool valid;
        bool dirty;
    };

    [[nodiscard]] BlockCacheStatus AcquireEntry(uint64_t logical_block_address,
                                                bool load_from_device, Entry *&entry) noexcept;
    [[nodiscard]] BlockCacheStatus FlushEntry(Entry &entry) noexcept;

    FileSystemBlockDevice *device_{nullptr};
    Entry entries_[OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT]{};
    uint64_t access_generation_{};
    BlockCacheStatistics statistics_{};
    mutable SpinLock lock_{};
    bool initialized_{false};
};

}
