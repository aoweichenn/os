#include <os/kernel/device/block_device.hpp>

namespace os::kernel {

BlockDeviceStatus BlockDevice::ReadBlock(const uint64_t logical_block_address, uint8_t *const block,
                                         const uint64_t block_size_bytes) noexcept {
    if (this->operations_.read == nullptr) {
        return BlockDeviceStatus::ReadFailed;
    }
    return this->operations_.read(this, logical_block_address, block, block_size_bytes);
}

BlockDeviceStatus BlockDevice::WriteBlock(const uint64_t logical_block_address,
                                          const uint8_t *const block,
                                          const uint64_t block_size_bytes) noexcept {
    if (this->operations_.write == nullptr) {
        return BlockDeviceStatus::WriteFailed;
    }
    return this->operations_.write(this, logical_block_address, block, block_size_bytes);
}

BlockDeviceStatus BlockDevice::Flush() noexcept {
    if (this->operations_.flush == nullptr) {
        return BlockDeviceStatus::FlushFailed;
    }
    return this->operations_.flush(this);
}

}
