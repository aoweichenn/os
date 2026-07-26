#include "os/kernel/fs/block_cache.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_BLOCK_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_BLOCK_CACHE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX = 0ULL;
constexpr uint8_t OS_KERNEL_BLOCK_CACHE_ZERO_BYTE = 0U;

void CopyBlock(uint8_t *destination, const uint8_t *source) noexcept {
    for (uint64_t byte_index = OS_KERNEL_BLOCK_CACHE_EMPTY_VALUE;
         byte_index < OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES; ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

void ClearBlock(uint8_t *block) noexcept {
    for (uint64_t byte_index = OS_KERNEL_BLOCK_CACHE_EMPTY_VALUE;
         byte_index < OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES; ++byte_index) {
        block[byte_index] = OS_KERNEL_BLOCK_CACHE_ZERO_BYTE;
    }
}

}

FileSystemBlockDeviceStatus
FileSystemBlockDevice::ReadBlock(const uint64_t logical_block_address, uint8_t *block,
                                 const uint64_t block_size_bytes) noexcept {
    static_cast<void>(logical_block_address);
    static_cast<void>(block);
    static_cast<void>(block_size_bytes);
    return FileSystemBlockDeviceStatus::ReadFailed;
}

FileSystemBlockDeviceStatus
FileSystemBlockDevice::WriteBlock(const uint64_t logical_block_address, const uint8_t *block,
                                  const uint64_t block_size_bytes) noexcept {
    static_cast<void>(logical_block_address);
    static_cast<void>(block);
    static_cast<void>(block_size_bytes);
    return FileSystemBlockDeviceStatus::WriteFailed;
}

FileSystemBlockDeviceStatus FileSystemBlockDevice::Flush() noexcept {
    return FileSystemBlockDeviceStatus::FlushFailed;
}

void BlockCache::Initialize(FileSystemBlockDevice &device) noexcept {
    SpinLockGuard guard{this->lock_};
    this->device_ = &device;
    this->access_generation_ = OS_KERNEL_BLOCK_CACHE_EMPTY_VALUE;
    this->statistics_ = BlockCacheStatistics{};
    for (uint64_t entry_index = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entry_index < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entry_index) {
        this->entries_[entry_index] = Entry{};
    }
    this->initialized_ = true;
}

BlockCacheStatus BlockCache::AcquireEntry(const uint64_t logical_block_address,
                                          const bool load_from_device, Entry *&entry) noexcept {
    for (uint64_t entry_index = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entry_index < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entry_index) {
        Entry &candidate = this->entries_[entry_index];
        if (candidate.valid && candidate.logical_block_address == logical_block_address) {
            candidate.access_generation =
                this->access_generation_ + OS_KERNEL_BLOCK_CACHE_COUNTER_INCREMENT;
            this->access_generation_ = candidate.access_generation;
            ++this->statistics_.hit_count;
            entry = &candidate;
            return BlockCacheStatus::Succeeded;
        }
    }

    ++this->statistics_.miss_count;
    uint64_t victim_index = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
    bool found_invalid_entry = false;
    for (uint64_t entry_index = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entry_index < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entry_index) {
        const Entry &candidate = this->entries_[entry_index];
        if (!candidate.valid) {
            victim_index = entry_index;
            found_invalid_entry = true;
            break;
        }
        if (candidate.access_generation < this->entries_[victim_index].access_generation) {
            victim_index = entry_index;
        }
    }

    Entry &victim = this->entries_[victim_index];
    if (!found_invalid_entry) {
        ++this->statistics_.eviction_count;
    }
    const BlockCacheStatus flush_status = this->FlushEntry(victim);
    if (flush_status != BlockCacheStatus::Succeeded) {
        return flush_status;
    }

    victim = Entry{};
    victim.logical_block_address = logical_block_address;
    victim.access_generation = this->access_generation_ + OS_KERNEL_BLOCK_CACHE_COUNTER_INCREMENT;
    this->access_generation_ = victim.access_generation;
    if (load_from_device) {
        if (this->device_->ReadBlock(logical_block_address, victim.bytes,
                                     OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
            FileSystemBlockDeviceStatus::Succeeded) {
            return BlockCacheStatus::DeviceReadFailed;
        }
        ++this->statistics_.device_read_count;
    } else {
        ClearBlock(victim.bytes);
    }
    victim.valid = true;
    entry = &victim;
    return BlockCacheStatus::Succeeded;
}

BlockCacheStatus BlockCache::FlushEntry(Entry &entry) noexcept {
    if (!entry.valid || !entry.dirty) {
        return BlockCacheStatus::Succeeded;
    }
    if (this->device_->WriteBlock(entry.logical_block_address, entry.bytes,
                                  OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
        FileSystemBlockDeviceStatus::Succeeded) {
        return BlockCacheStatus::DeviceWriteFailed;
    }
    entry.dirty = false;
    ++this->statistics_.device_write_count;
    return BlockCacheStatus::Succeeded;
}

BlockCacheStatus BlockCache::ReadBlock(const uint64_t logical_block_address, uint8_t *block,
                                       const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return BlockCacheStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return BlockCacheStatus::InvalidBufferSize;
    }
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->device_ == nullptr) {
        return BlockCacheStatus::NotInitialized;
    }
    Entry *entry = nullptr;
    const BlockCacheStatus status = this->AcquireEntry(logical_block_address, true, entry);
    if (status != BlockCacheStatus::Succeeded) {
        return status;
    }
    CopyBlock(block, entry->bytes);
    return BlockCacheStatus::Succeeded;
}

BlockCacheStatus BlockCache::WriteBlock(const uint64_t logical_block_address, const uint8_t *block,
                                        const uint64_t block_size_bytes) noexcept {
    if (block == nullptr) {
        return BlockCacheStatus::NullBuffer;
    }
    if (block_size_bytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return BlockCacheStatus::InvalidBufferSize;
    }
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->device_ == nullptr) {
        return BlockCacheStatus::NotInitialized;
    }
    Entry *entry = nullptr;
    const BlockCacheStatus status = this->AcquireEntry(logical_block_address, false, entry);
    if (status != BlockCacheStatus::Succeeded) {
        return status;
    }
    CopyBlock(entry->bytes, block);
    entry->dirty = true;
    return BlockCacheStatus::Succeeded;
}

BlockCacheStatus BlockCache::Sync() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->device_ == nullptr) {
        return BlockCacheStatus::NotInitialized;
    }
    for (uint64_t entry_index = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entry_index < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entry_index) {
        const BlockCacheStatus status = this->FlushEntry(this->entries_[entry_index]);
        if (status != BlockCacheStatus::Succeeded) {
            return status;
        }
    }
    if (this->device_->Flush() != FileSystemBlockDeviceStatus::Succeeded) {
        return BlockCacheStatus::DeviceFlushFailed;
    }
    ++this->statistics_.flush_count;
    return BlockCacheStatus::Succeeded;
}

void BlockCache::Invalidate() noexcept {
    SpinLockGuard guard{this->lock_};
    for (uint64_t entry_index = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entry_index < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entry_index) {
        this->entries_[entry_index] = Entry{};
    }
}

BlockCacheStatistics BlockCache::Statistics() const noexcept {
    const SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

}
