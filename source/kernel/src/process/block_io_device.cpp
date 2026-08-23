#include <os/kernel/process/block_io_device.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_BLOCK_IO_DEVICE_EMPTY_VALUE = 0ULL;

}

RuntimeBlockIoStatus BlockIoDevice::Initialize(BlockDevice &synchronous_device,
                                               AsynchronousBlockDevice &asynchronous_device,
                                               const uint64_t timeout_nanoseconds,
                                               const bool asynchronous_wait_enabled) noexcept {
    if (this->initialized_) {
        return RuntimeBlockIoStatus::Corrupt;
    }
    if (timeout_nanoseconds == OS_KERNEL_BLOCK_IO_DEVICE_EMPTY_VALUE) {
        return RuntimeBlockIoStatus::InvalidRequest;
    }
    if (asynchronous_wait_enabled) {
        const RuntimeBlockIoStatus register_status =
            RegisterRuntimeBlockIoDevice(asynchronous_device);
        if (register_status != RuntimeBlockIoStatus::Succeeded) {
            return register_status;
        }
    }
    this->synchronous_device_ = &synchronous_device;
    this->asynchronous_device_ = &asynchronous_device;
    this->timeout_nanoseconds_ = timeout_nanoseconds;
    this->statistics_ = BlockIoDeviceStatistics{};
    this->asynchronous_wait_enabled_ = asynchronous_wait_enabled;
    this->initialized_ = true;
    return RuntimeBlockIoStatus::Succeeded;
}

BlockDeviceStatus BlockIoDevice::ReadBlock(const uint64_t logical_block_address,
                                           uint8_t *const block,
                                           const uint64_t block_size_bytes) noexcept {
    return this->Transfer(BlockOperation::Read, logical_block_address, block, block_size_bytes);
}

BlockDeviceStatus BlockIoDevice::WriteBlock(const uint64_t logical_block_address,
                                            const uint8_t *const block,
                                            const uint64_t block_size_bytes) noexcept {
    return this->Transfer(BlockOperation::Write, logical_block_address,
                          const_cast<uint8_t *>(block), block_size_bytes);
}

BlockDeviceStatus BlockIoDevice::Flush() noexcept {
    return this->Transfer(BlockOperation::Flush, OS_KERNEL_BLOCK_IO_DEVICE_EMPTY_VALUE, nullptr,
                          OS_KERNEL_BLOCK_IO_DEVICE_EMPTY_VALUE);
}

BlockIoDeviceStatistics BlockIoDevice::Statistics() const noexcept { return this->statistics_; }

BlockDeviceStatus BlockIoDevice::Transfer(const BlockOperation operation,
                                          const uint64_t logical_block_address,
                                          uint8_t *const buffer,
                                          const uint64_t buffer_size_bytes) noexcept {
    if (!this->initialized_ || this->synchronous_device_ == nullptr ||
        this->asynchronous_device_ == nullptr) {
        return operation == BlockOperation::Read    ? BlockDeviceStatus::ReadFailed
               : operation == BlockOperation::Write ? BlockDeviceStatus::WriteFailed
                                                    : BlockDeviceStatus::FlushFailed;
    }
    if (!this->asynchronous_wait_enabled_ || !RuntimeBlockIoWaitAvailable()) {
        ++this->statistics_.synchronous_fallback_count;
        return operation == BlockOperation::Read
                   ? this->synchronous_device_->ReadBlock(logical_block_address, buffer,
                                                          buffer_size_bytes)
               : operation == BlockOperation::Write
                   ? this->synchronous_device_->WriteBlock(logical_block_address, buffer,
                                                           buffer_size_bytes)
                   : this->synchronous_device_->Flush();
    }
    const RuntimeBlockIoStatus status =
        AwaitRuntimeBlockIo(*this->asynchronous_device_, operation, logical_block_address, buffer,
                            buffer_size_bytes, this->timeout_nanoseconds_);
    if (status != RuntimeBlockIoStatus::Succeeded) {
        ++this->statistics_.asynchronous_failure_count;
        return operation == BlockOperation::Read    ? BlockDeviceStatus::ReadFailed
               : operation == BlockOperation::Write ? BlockDeviceStatus::WriteFailed
                                                    : BlockDeviceStatus::FlushFailed;
    }
    if (operation == BlockOperation::Read) {
        ++this->statistics_.asynchronous_read_count;
    } else if (operation == BlockOperation::Write) {
        ++this->statistics_.asynchronous_write_count;
    } else {
        ++this->statistics_.asynchronous_flush_count;
    }
    return BlockDeviceStatus::Succeeded;
}

}
