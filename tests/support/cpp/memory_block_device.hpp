#pragma once

#include "os/kernel/block_cache.hpp"

#include <stdint.h>

namespace os::test {

inline constexpr uint64_t OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_COUNT = 4096ULL;
inline constexpr uint64_t OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES = 512ULL;

class MemoryBlockDevice final : public os::kernel::FileSystemBlockDevice {
  public:
    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    ReadBlock(const uint64_t logicalBlockAddress, uint8_t *block,
              const uint64_t blockSizeBytes) noexcept override {
        if (block == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        if (logicalBlockAddress >= OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_COUNT ||
            blockSizeBytes != OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBlock;
        }
        if (this->failReads_) {
            return os::kernel::FileSystemBlockDeviceStatus::ReadFailed;
        }
        const uint64_t blockOffset =
            logicalBlockAddress * OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES;
        for (uint64_t byteIndex = 0ULL; byteIndex < blockSizeBytes;
             ++byteIndex) {
            block[byteIndex] = this->bytes_[blockOffset + byteIndex];
        }
        ++this->readCount_;
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    WriteBlock(const uint64_t logicalBlockAddress, const uint8_t *block,
               const uint64_t blockSizeBytes) noexcept override {
        if (block == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        if (logicalBlockAddress >= OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_COUNT ||
            blockSizeBytes != OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBlock;
        }
        if (this->failWrites_) {
            return os::kernel::FileSystemBlockDeviceStatus::WriteFailed;
        }
        const uint64_t blockOffset =
            logicalBlockAddress * OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES;
        for (uint64_t byteIndex = 0ULL; byteIndex < blockSizeBytes;
             ++byteIndex) {
            this->bytes_[blockOffset + byteIndex] = block[byteIndex];
        }
        ++this->writeCount_;
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    Flush() noexcept override {
        if (this->failFlushes_) {
            return os::kernel::FileSystemBlockDeviceStatus::FlushFailed;
        }
        ++this->flushCount_;
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    void XorByte(const uint64_t logicalBlockAddress,
                 const uint64_t byteOffset, const uint8_t mask) noexcept {
        const uint64_t absoluteOffset =
            logicalBlockAddress * OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES +
            byteOffset;
        this->bytes_[absoluteOffset] =
            static_cast<uint8_t>(this->bytes_[absoluteOffset] ^ mask);
    }

    void SetFailureModes(const bool failReads, const bool failWrites,
                         const bool failFlushes) noexcept {
        this->failReads_ = failReads;
        this->failWrites_ = failWrites;
        this->failFlushes_ = failFlushes;
    }

    [[nodiscard]] uint64_t ReadCount() const noexcept {
        return this->readCount_;
    }

    [[nodiscard]] uint64_t WriteCount() const noexcept {
        return this->writeCount_;
    }

    [[nodiscard]] uint64_t FlushCount() const noexcept {
        return this->flushCount_;
    }

  private:
    uint8_t bytes_[OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_COUNT *
                   OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES]{};
    uint64_t readCount_{};
    uint64_t writeCount_{};
    uint64_t flushCount_{};
    bool failReads_{};
    bool failWrites_{};
    bool failFlushes_{};
};

}
