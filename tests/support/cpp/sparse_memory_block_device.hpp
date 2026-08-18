#pragma once

#include "os/kernel/fs/block_cache.hpp"
#include "os/kernel/fs/root_file_system_format.hpp"

#include <array>
#include <stdint.h>
#include <unordered_map>

namespace os::test {

inline constexpr uint64_t OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_COUNT =
    os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA +
    os::kernel::fs::OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT;
inline constexpr uint64_t OS_TEST_SPARSE_BLOCK_DEVICE_EMPTY_VALUE = 0ULL;
inline constexpr uint8_t OS_TEST_SPARSE_BLOCK_DEVICE_ZERO_BYTE = 0U;

// 逻辑容量按真实 rootfs v4 规格提供，未写入块隐式读取为全零，避免物化 128 GiB。
class SparseMemoryBlockDevice final : public os::kernel::FileSystemBlockDevice {
  public:
    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    ReadBlock(const uint64_t logical_block_address, uint8_t *const block,
              const uint64_t block_size_bytes) noexcept override {
        if (block == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        if (logical_block_address >= OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_COUNT ||
            block_size_bytes != OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBlock;
        }
        if (this->fail_reads_) {
            return os::kernel::FileSystemBlockDeviceStatus::ReadFailed;
        }
        const auto block_iterator = this->blocks_.find(logical_block_address);
        for (uint64_t byte_index = OS_TEST_SPARSE_BLOCK_DEVICE_EMPTY_VALUE;
             byte_index < block_size_bytes; ++byte_index) {
            block[byte_index] = block_iterator == this->blocks_.end()
                                    ? OS_TEST_SPARSE_BLOCK_DEVICE_ZERO_BYTE
                                    : block_iterator->second[byte_index];
        }
        ++this->read_count_;
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    WriteBlock(const uint64_t logical_block_address, const uint8_t *const block,
               const uint64_t block_size_bytes) noexcept override {
        if (block == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        if (logical_block_address >= OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_COUNT ||
            block_size_bytes != OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBlock;
        }
        if (this->fail_writes_) {
            return os::kernel::FileSystemBlockDeviceStatus::WriteFailed;
        }
        if (this->fail_targeted_write_ &&
            logical_block_address == this->failed_write_logical_block_address_) {
            return os::kernel::FileSystemBlockDeviceStatus::WriteFailed;
        }
        bool all_zero = true;
        for (uint64_t byte_index = OS_TEST_SPARSE_BLOCK_DEVICE_EMPTY_VALUE;
             byte_index < block_size_bytes; ++byte_index) {
            all_zero = all_zero && block[byte_index] == OS_TEST_SPARSE_BLOCK_DEVICE_ZERO_BYTE;
        }
        if (all_zero) {
            this->blocks_.erase(logical_block_address);
        } else {
            auto &stored_block = this->blocks_[logical_block_address];
            for (uint64_t byte_index = OS_TEST_SPARSE_BLOCK_DEVICE_EMPTY_VALUE;
                 byte_index < block_size_bytes; ++byte_index) {
                stored_block[byte_index] = block[byte_index];
            }
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

    void SetFailureModes(const bool fail_reads, const bool fail_writes,
                         const bool fail_flushes) noexcept {
        this->fail_reads_ = fail_reads;
        this->fail_writes_ = fail_writes;
        this->fail_flushes_ = fail_flushes;
    }

    void SetTargetedWriteFailure(const uint64_t logical_block_address,
                                 const bool enabled) noexcept {
        this->failed_write_logical_block_address_ = logical_block_address;
        this->fail_targeted_write_ = enabled;
    }

    void XorByte(const uint64_t logical_block_address, const uint64_t byte_offset,
                 const uint8_t mask) noexcept {
        if (logical_block_address >= OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_COUNT ||
            byte_offset >= OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_SIZE_BYTES) {
            return;
        }
        auto &stored_block = this->blocks_[logical_block_address];
        stored_block[byte_offset] = static_cast<uint8_t>(stored_block[byte_offset] ^ mask);
    }

    [[nodiscard]] uint64_t StoredBlockCount() const noexcept {
        return static_cast<uint64_t>(this->blocks_.size());
    }

    [[nodiscard]] uint64_t ReadCount() const noexcept { return this->read_count_; }

    [[nodiscard]] uint64_t WriteCount() const noexcept { return this->write_count_; }

    [[nodiscard]] uint64_t FlushCount() const noexcept { return this->flush_count_; }

  private:
    using StoredBlock = std::array<uint8_t, OS_TEST_SPARSE_BLOCK_DEVICE_BLOCK_SIZE_BYTES>;

    std::unordered_map<uint64_t, StoredBlock> blocks_{};
    uint64_t read_count_{};
    uint64_t write_count_{};
    uint64_t flush_count_{};
    bool fail_reads_{};
    bool fail_writes_{};
    bool fail_flushes_{};
    uint64_t failed_write_logical_block_address_{};
    bool fail_targeted_write_{};
};

}
