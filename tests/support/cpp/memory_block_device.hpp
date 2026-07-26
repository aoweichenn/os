#pragma once

#include "os/kernel/fs/block_cache.hpp"

#include <stdint.h>

namespace os::test {

inline constexpr uint64_t OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_COUNT = 4096ULL;
inline constexpr uint64_t OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES = 512ULL;

class MemoryBlockDevice final : public os::kernel::FileSystemBlockDevice {
  public:
    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    ReadBlock(const uint64_t logical_block_address, uint8_t *block,
              const uint64_t block_size_bytes) noexcept override {
        if (block == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        if (logical_block_address >= OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_COUNT ||
            block_size_bytes != OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBlock;
        }
        if (this->fail_reads_) {
            return os::kernel::FileSystemBlockDeviceStatus::ReadFailed;
        }
        const uint64_t block_offset =
            logical_block_address * OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES;
        for (uint64_t byte_index = 0ULL; byte_index < block_size_bytes; ++byte_index) {
            block[byte_index] = this->bytes_[block_offset + byte_index];
        }
        ++this->read_count_;
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    WriteBlock(const uint64_t logical_block_address, const uint8_t *block,
               const uint64_t block_size_bytes) noexcept override {
        if (block == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        if (logical_block_address >= OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_COUNT ||
            block_size_bytes != OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBlock;
        }
        if (this->fail_writes_) {
            return os::kernel::FileSystemBlockDeviceStatus::WriteFailed;
        }
        const uint64_t block_offset =
            logical_block_address * OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES;
        for (uint64_t byte_index = 0ULL; byte_index < block_size_bytes; ++byte_index) {
            this->bytes_[block_offset + byte_index] = block[byte_index];
        }
        ++this->write_count_;
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus Flush() noexcept override {
        if (this->fail_flushes_) {
            return os::kernel::FileSystemBlockDeviceStatus::FlushFailed;
        }
        ++this->flush_count_;
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    void XorByte(const uint64_t logical_block_address, const uint64_t byte_offset,
                 const uint8_t mask) noexcept {
        const uint64_t absolute_offset =
            logical_block_address * OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES + byte_offset;
        this->bytes_[absolute_offset] = static_cast<uint8_t>(this->bytes_[absolute_offset] ^ mask);
    }

    void SetFailureModes(const bool fail_reads, const bool fail_writes,
                         const bool fail_flushes) noexcept {
        this->fail_reads_ = fail_reads;
        this->fail_writes_ = fail_writes;
        this->fail_flushes_ = fail_flushes;
    }

    [[nodiscard]] uint64_t ReadCount() const noexcept { return this->read_count_; }

    [[nodiscard]] uint64_t WriteCount() const noexcept { return this->write_count_; }

    [[nodiscard]] uint64_t FlushCount() const noexcept { return this->flush_count_; }

  private:
    uint8_t bytes_[OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_COUNT *
                   OS_TEST_MEMORY_BLOCK_DEVICE_BLOCK_SIZE_BYTES]{};
    uint64_t read_count_{};
    uint64_t write_count_{};
    uint64_t flush_count_{};
    bool fail_reads_{};
    bool fail_writes_{};
    bool fail_flushes_{};
};

}
