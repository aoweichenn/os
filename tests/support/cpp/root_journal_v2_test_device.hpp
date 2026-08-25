#pragma once

#include <os/kernel/device/block_device.hpp>
#include <os/kernel/fs/root_file_system_v5_format.hpp>

#include <array>
#include <stdint.h>
#include <unordered_map>

namespace os::test {

inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_START_LBA = 8ULL;
inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_BLOCK_COUNT = 4096ULL;
inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_INODE_COUNT = 4096ULL;
inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_JOURNAL_START_RELATIVE_BLOCK = 16ULL;
inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_DEVICE_SECTOR_COUNT =
    OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_START_LBA +
    OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_BLOCK_COUNT *
        os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK;
inline constexpr uint64_t OS_TEST_ROOT_JOURNAL_V2_FAILURE_DISABLED = 0ULL;

class RootJournalV2TestDevice final
    : public os::kernel::BlockDeviceAdapter<RootJournalV2TestDevice> {
  public:
    [[nodiscard]] os::kernel::BlockDeviceStatus
    ReadBlock(const uint64_t logical_block_address, uint8_t *const block,
              const uint64_t block_size_bytes) noexcept {
        if (block == nullptr) {
            return os::kernel::BlockDeviceStatus::InvalidBuffer;
        }
        if (block_size_bytes == 0ULL ||
            block_size_bytes % os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES != 0ULL) {
            return os::kernel::BlockDeviceStatus::InvalidBlock;
        }
        const uint64_t sector_count =
            block_size_bytes / os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES;
        if (logical_block_address >= OS_TEST_ROOT_JOURNAL_V2_DEVICE_SECTOR_COUNT ||
            sector_count > OS_TEST_ROOT_JOURNAL_V2_DEVICE_SECTOR_COUNT - logical_block_address) {
            return os::kernel::BlockDeviceStatus::InvalidBlock;
        }
        for (uint64_t sector_index = 0ULL; sector_index < sector_count; ++sector_index) {
            const uint64_t current_address = logical_block_address + sector_index;
            const auto volatile_iterator = this->volatile_sectors_.find(current_address);
            const auto durable_iterator = this->durable_sectors_.find(current_address);
            for (uint64_t byte_index = 0ULL;
                 byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES; ++byte_index) {
                block[sector_index * os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES +
                      byte_index] = volatile_iterator != this->volatile_sectors_.end()
                                        ? volatile_iterator->second[byte_index]
                                    : durable_iterator != this->durable_sectors_.end()
                                        ? durable_iterator->second[byte_index]
                                        : 0U;
            }
        }
        return os::kernel::BlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::BlockDeviceStatus
    WriteBlock(const uint64_t logical_block_address, const uint8_t *const block,
               const uint64_t block_size_bytes) noexcept {
        if (block == nullptr) {
            return os::kernel::BlockDeviceStatus::InvalidBuffer;
        }
        if (block_size_bytes == 0ULL ||
            block_size_bytes % os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES != 0ULL) {
            return os::kernel::BlockDeviceStatus::InvalidBlock;
        }
        const uint64_t sector_count =
            block_size_bytes / os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES;
        if (logical_block_address >= OS_TEST_ROOT_JOURNAL_V2_DEVICE_SECTOR_COUNT ||
            sector_count > OS_TEST_ROOT_JOURNAL_V2_DEVICE_SECTOR_COUNT - logical_block_address) {
            return os::kernel::BlockDeviceStatus::InvalidBlock;
        }
        for (uint64_t sector_index = 0ULL; sector_index < sector_count; ++sector_index) {
            ++this->persistence_attempt_count_;
            if (this->persistence_attempt_count_ == this->failure_ordinal_) {
                return os::kernel::BlockDeviceStatus::WriteFailed;
            }
            StoredSector &sector =
                this->volatile_sectors_[logical_block_address + sector_index];
            for (uint64_t byte_index = 0ULL;
                 byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES; ++byte_index) {
                sector[byte_index] =
                    block[sector_index * os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES +
                          byte_index];
            }
            ++this->write_count_;
        }
        return os::kernel::BlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::BlockDeviceStatus Flush() noexcept {
        ++this->persistence_attempt_count_;
        if (this->persistence_attempt_count_ == this->failure_ordinal_) {
            return os::kernel::BlockDeviceStatus::FlushFailed;
        }
        for (const auto &[logical_block_address, sector] : this->volatile_sectors_) {
            this->durable_sectors_[logical_block_address] = sector;
        }
        this->volatile_sectors_.clear();
        ++this->flush_count_;
        return os::kernel::BlockDeviceStatus::Succeeded;
    }

    void SetFailureOrdinal(const uint64_t failure_ordinal) noexcept {
        this->failure_ordinal_ = failure_ordinal;
    }

    void ResetOperationCounts() noexcept {
        this->persistence_attempt_count_ = 0ULL;
        this->write_count_ = 0ULL;
        this->flush_count_ = 0ULL;
    }

    void Crash() noexcept {
        this->volatile_sectors_.clear();
        this->failure_ordinal_ = OS_TEST_ROOT_JOURNAL_V2_FAILURE_DISABLED;
        this->ResetOperationCounts();
    }

    void WriteDurableFileSystemBlock(const uint64_t relative_block,
                                     const uint8_t *const block) noexcept {
        if (block == nullptr || relative_block >= OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_BLOCK_COUNT) {
            return;
        }
        const uint64_t first_sector =
            OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_START_LBA +
            relative_block * os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK;
        for (uint64_t sector_index = 0ULL;
             sector_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK; ++sector_index) {
            StoredSector &sector = this->durable_sectors_[first_sector + sector_index];
            for (uint64_t byte_index = 0ULL;
                 byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES; ++byte_index) {
                sector[byte_index] =
                    block[sector_index * os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES +
                          byte_index];
            }
        }
    }

    void ReadDurableFileSystemBlock(const uint64_t relative_block,
                                    uint8_t *const block) const noexcept {
        if (block == nullptr || relative_block >= OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_BLOCK_COUNT) {
            return;
        }
        const uint64_t first_sector =
            OS_TEST_ROOT_JOURNAL_V2_FILE_SYSTEM_START_LBA +
            relative_block * os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK;
        for (uint64_t sector_index = 0ULL;
             sector_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTORS_PER_BLOCK; ++sector_index) {
            const auto iterator = this->durable_sectors_.find(first_sector + sector_index);
            for (uint64_t byte_index = 0ULL;
                 byte_index < os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES; ++byte_index) {
                block[sector_index * os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES +
                      byte_index] =
                    iterator == this->durable_sectors_.end() ? 0U : iterator->second[byte_index];
            }
        }
    }

    void XorDurableSectorByte(const uint64_t logical_block_address, const uint64_t byte_offset,
                              const uint8_t mask) noexcept {
        if (logical_block_address >= OS_TEST_ROOT_JOURNAL_V2_DEVICE_SECTOR_COUNT ||
            byte_offset >= os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES) {
            return;
        }
        StoredSector &sector = this->durable_sectors_[logical_block_address];
        sector[byte_offset] = static_cast<uint8_t>(sector[byte_offset] ^ mask);
    }

    [[nodiscard]] uint64_t PersistenceAttemptCount() const noexcept {
        return this->persistence_attempt_count_;
    }

    [[nodiscard]] uint64_t WriteCount() const noexcept { return this->write_count_; }

    [[nodiscard]] uint64_t FlushCount() const noexcept { return this->flush_count_; }

  private:
    using StoredSector = std::array<uint8_t, os::kernel::fs::OS_KERNEL_ROOTFS_V5_SECTOR_SIZE_BYTES>;

    std::unordered_map<uint64_t, StoredSector> durable_sectors_{};
    std::unordered_map<uint64_t, StoredSector> volatile_sectors_{};
    uint64_t failure_ordinal_{};
    uint64_t persistence_attempt_count_{};
    uint64_t write_count_{};
    uint64_t flush_count_{};
};

}
