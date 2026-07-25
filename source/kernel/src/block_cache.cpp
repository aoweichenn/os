#include "os/kernel/block_cache.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_BLOCK_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_BLOCK_CACHE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX = 0ULL;
constexpr uint8_t OS_KERNEL_BLOCK_CACHE_ZERO_BYTE = 0U;

void CopyBlock(uint8_t *destination, const uint8_t *source) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_BLOCK_CACHE_EMPTY_VALUE;
         byteIndex < OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES; ++byteIndex) {
        destination[byteIndex] = source[byteIndex];
    }
}

void ClearBlock(uint8_t *block) noexcept {
    for (uint64_t byteIndex = OS_KERNEL_BLOCK_CACHE_EMPTY_VALUE;
         byteIndex < OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES; ++byteIndex) {
        block[byteIndex] = OS_KERNEL_BLOCK_CACHE_ZERO_BYTE;
    }
}

}

FileSystemBlockDeviceStatus FileSystemBlockDevice::ReadBlock(
    const uint64_t logicalBlockAddress, uint8_t *block,
    const uint64_t blockSizeBytes) noexcept {
    static_cast<void>(logicalBlockAddress);
    static_cast<void>(block);
    static_cast<void>(blockSizeBytes);
    return FileSystemBlockDeviceStatus::ReadFailed;
}

FileSystemBlockDeviceStatus FileSystemBlockDevice::WriteBlock(
    const uint64_t logicalBlockAddress, const uint8_t *block,
    const uint64_t blockSizeBytes) noexcept {
    static_cast<void>(logicalBlockAddress);
    static_cast<void>(block);
    static_cast<void>(blockSizeBytes);
    return FileSystemBlockDeviceStatus::WriteFailed;
}

FileSystemBlockDeviceStatus FileSystemBlockDevice::Flush() noexcept {
    return FileSystemBlockDeviceStatus::FlushFailed;
}

void BlockCache::Initialize(FileSystemBlockDevice &device) noexcept {
    SpinLockGuard guard{this->lock_};
    this->device_ = &device;
    this->accessGeneration_ = OS_KERNEL_BLOCK_CACHE_EMPTY_VALUE;
    this->statistics_ = BlockCacheStatistics{};
    for (uint64_t entryIndex = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entryIndex < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entryIndex) {
        this->entries_[entryIndex] = Entry{};
    }
    this->initialized_ = true;
}

BlockCacheStatus BlockCache::AcquireEntry(const uint64_t logicalBlockAddress,
                                          const bool loadFromDevice,
                                          Entry *&entry) noexcept {
    for (uint64_t entryIndex = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entryIndex < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entryIndex) {
        Entry &candidate = this->entries_[entryIndex];
        if (candidate.valid && candidate.logicalBlockAddress == logicalBlockAddress) {
            candidate.accessGeneration =
                this->accessGeneration_ + OS_KERNEL_BLOCK_CACHE_COUNTER_INCREMENT;
            this->accessGeneration_ = candidate.accessGeneration;
            ++this->statistics_.hitCount;
            entry = &candidate;
            return BlockCacheStatus::Succeeded;
        }
    }

    ++this->statistics_.missCount;
    uint64_t victimIndex = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
    bool foundInvalidEntry = false;
    for (uint64_t entryIndex = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entryIndex < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entryIndex) {
        const Entry &candidate = this->entries_[entryIndex];
        if (!candidate.valid) {
            victimIndex = entryIndex;
            foundInvalidEntry = true;
            break;
        }
        if (candidate.accessGeneration < this->entries_[victimIndex].accessGeneration) {
            victimIndex = entryIndex;
        }
    }

    Entry &victim = this->entries_[victimIndex];
    if (!foundInvalidEntry) {
        ++this->statistics_.evictionCount;
    }
    const BlockCacheStatus flushStatus = this->FlushEntry(victim);
    if (flushStatus != BlockCacheStatus::Succeeded) {
        return flushStatus;
    }

    victim = Entry{};
    victim.logicalBlockAddress = logicalBlockAddress;
    victim.accessGeneration =
        this->accessGeneration_ + OS_KERNEL_BLOCK_CACHE_COUNTER_INCREMENT;
    this->accessGeneration_ = victim.accessGeneration;
    if (loadFromDevice) {
        if (this->device_->ReadBlock(logicalBlockAddress, victim.bytes,
                                     OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
            FileSystemBlockDeviceStatus::Succeeded) {
            return BlockCacheStatus::DeviceReadFailed;
        }
        ++this->statistics_.deviceReadCount;
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
    if (this->device_->WriteBlock(entry.logicalBlockAddress, entry.bytes,
                                  OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) !=
        FileSystemBlockDeviceStatus::Succeeded) {
        return BlockCacheStatus::DeviceWriteFailed;
    }
    entry.dirty = false;
    ++this->statistics_.deviceWriteCount;
    return BlockCacheStatus::Succeeded;
}

BlockCacheStatus BlockCache::ReadBlock(const uint64_t logicalBlockAddress, uint8_t *block,
                                       const uint64_t blockSizeBytes) noexcept {
    if (block == nullptr) {
        return BlockCacheStatus::NullBuffer;
    }
    if (blockSizeBytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return BlockCacheStatus::InvalidBufferSize;
    }
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->device_ == nullptr) {
        return BlockCacheStatus::NotInitialized;
    }
    Entry *entry = nullptr;
    const BlockCacheStatus status = this->AcquireEntry(logicalBlockAddress, true, entry);
    if (status != BlockCacheStatus::Succeeded) {
        return status;
    }
    CopyBlock(block, entry->bytes);
    return BlockCacheStatus::Succeeded;
}

BlockCacheStatus BlockCache::WriteBlock(const uint64_t logicalBlockAddress, const uint8_t *block,
                                        const uint64_t blockSizeBytes) noexcept {
    if (block == nullptr) {
        return BlockCacheStatus::NullBuffer;
    }
    if (blockSizeBytes != OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES) {
        return BlockCacheStatus::InvalidBufferSize;
    }
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->device_ == nullptr) {
        return BlockCacheStatus::NotInitialized;
    }
    Entry *entry = nullptr;
    const BlockCacheStatus status = this->AcquireEntry(logicalBlockAddress, false, entry);
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
    for (uint64_t entryIndex = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entryIndex < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entryIndex) {
        const BlockCacheStatus status = this->FlushEntry(this->entries_[entryIndex]);
        if (status != BlockCacheStatus::Succeeded) {
            return status;
        }
    }
    if (this->device_->Flush() != FileSystemBlockDeviceStatus::Succeeded) {
        return BlockCacheStatus::DeviceFlushFailed;
    }
    ++this->statistics_.flushCount;
    return BlockCacheStatus::Succeeded;
}

void BlockCache::Invalidate() noexcept {
    SpinLockGuard guard{this->lock_};
    for (uint64_t entryIndex = OS_KERNEL_BLOCK_CACHE_FIRST_ENTRY_INDEX;
         entryIndex < OS_KERNEL_BLOCK_CACHE_ENTRY_COUNT; ++entryIndex) {
        this->entries_[entryIndex] = Entry{};
    }
}

BlockCacheStatistics BlockCache::Statistics() const noexcept {
    const SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

}
