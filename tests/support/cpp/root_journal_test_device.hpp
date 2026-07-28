#pragma once

#include "os/kernel/fs/block_cache.hpp"
#include "os/kernel/fs/root_file_system_format.hpp"

#include <array>
#include <stdint.h>
#include <unordered_map>

namespace os::test {

inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_COUNT =
    os::kernel::fs::OS_KERNEL_ROOTFS_START_LBA + os::kernel::fs::OS_KERNEL_ROOTFS_TOTAL_BLOCK_COUNT;
inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_DEVICE_DISABLED_FAILURE_ORDINAL = 0ULL;

class RootJournalTestDevice final : public os::kernel::FileSystemBlockDevice {
  public:
    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    ReadBlock(const uint64_t logical_block_address, uint8_t *const block,
              const uint64_t block_size_bytes) noexcept override {
        if (block == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        if (logical_block_address >= OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_COUNT ||
            block_size_bytes != OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBlock;
        }
        ++this->read_attempt_count_;
        const auto iterator = this->blocks_.find(logical_block_address);
        for (uint64_t byte_index = 0ULL; byte_index < block_size_bytes; ++byte_index) {
            block[byte_index] = iterator == this->blocks_.end() ? 0U : iterator->second[byte_index];
        }
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    WriteBlock(const uint64_t logical_block_address, const uint8_t *const block,
               const uint64_t block_size_bytes) noexcept override {
        if (block == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        if (logical_block_address >= OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_COUNT ||
            block_size_bytes != OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBlock;
        }
        ++this->write_attempt_count_;
        if (this->failed_write_ordinal_ != OS_TEST_ROOT_JOURNAL_DEVICE_DISABLED_FAILURE_ORDINAL &&
            this->write_attempt_count_ == this->failed_write_ordinal_) {
            return os::kernel::FileSystemBlockDeviceStatus::WriteFailed;
        }
        StoredBlock &stored_block = this->blocks_[logical_block_address];
        for (uint64_t byte_index = 0ULL; byte_index < block_size_bytes; ++byte_index) {
            stored_block[byte_index] = block[byte_index];
        }
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus Flush() noexcept override {
        ++this->flush_attempt_count_;
        if (this->failed_flush_ordinal_ != OS_TEST_ROOT_JOURNAL_DEVICE_DISABLED_FAILURE_ORDINAL &&
            this->flush_attempt_count_ == this->failed_flush_ordinal_) {
            return os::kernel::FileSystemBlockDeviceStatus::FlushFailed;
        }
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    void SetWriteFailureOrdinal(const uint64_t failure_ordinal) noexcept {
        this->failed_write_ordinal_ = failure_ordinal;
    }

    void SetFlushFailureOrdinal(const uint64_t failure_ordinal) noexcept {
        this->failed_flush_ordinal_ = failure_ordinal;
    }

    void ClearFailures() noexcept {
        this->failed_write_ordinal_ = OS_TEST_ROOT_JOURNAL_DEVICE_DISABLED_FAILURE_ORDINAL;
        this->failed_flush_ordinal_ = OS_TEST_ROOT_JOURNAL_DEVICE_DISABLED_FAILURE_ORDINAL;
    }

    void ResetOperationCounts() noexcept {
        this->read_attempt_count_ = 0ULL;
        this->write_attempt_count_ = 0ULL;
        this->flush_attempt_count_ = 0ULL;
    }

    void XorByte(const uint64_t logical_block_address, const uint64_t byte_offset,
                 const uint8_t mask) noexcept {
        if (logical_block_address >= OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_COUNT ||
            byte_offset >= OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_SIZE_BYTES) {
            return;
        }
        StoredBlock &stored_block = this->blocks_[logical_block_address];
        stored_block[byte_offset] = static_cast<uint8_t>(stored_block[byte_offset] ^ mask);
    }

    [[nodiscard]] uint64_t WriteAttemptCount() const noexcept { return this->write_attempt_count_; }

    [[nodiscard]] uint64_t FlushAttemptCount() const noexcept { return this->flush_attempt_count_; }

  private:
    using StoredBlock = std::array<uint8_t, OS_TEST_ROOT_JOURNAL_DEVICE_BLOCK_SIZE_BYTES>;

    std::unordered_map<uint64_t, StoredBlock> blocks_{};
    uint64_t read_attempt_count_{};
    uint64_t write_attempt_count_{};
    uint64_t flush_attempt_count_{};
    uint64_t failed_write_ordinal_{};
    uint64_t failed_flush_ordinal_{};
};

}
