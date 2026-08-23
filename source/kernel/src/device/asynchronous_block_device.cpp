#include <os/kernel/device/asynchronous_block_device.hpp>

namespace os::kernel {

AsynchronousBlockDeviceStatus AsynchronousBlockDevice::Submit(
    const BlockOperation operation, const uint64_t logical_block_address, uint8_t *const buffer,
    const uint64_t buffer_size_bytes, const uint64_t owner_thread_index,
    const uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept {
    request_identifier = 0ULL;
    if (this->operations_.submit == nullptr) {
        return AsynchronousBlockDeviceStatus::NotReady;
    }
    return this->operations_.submit(this, operation, logical_block_address, buffer,
                                    buffer_size_bytes, owner_thread_index, deadline_nanoseconds,
                                    request_identifier);
}

AsynchronousBlockDeviceStatus
AsynchronousBlockDevice::Cancel(const uint64_t request_identifier) noexcept {
    return this->operations_.cancel == nullptr ? AsynchronousBlockDeviceStatus::NotReady
                                               : this->operations_.cancel(this, request_identifier);
}

AsynchronousBlockDeviceStatus
AsynchronousBlockDevice::ResolveTimeouts(const uint64_t now_nanoseconds) noexcept {
    return this->operations_.resolve_timeout == nullptr
               ? AsynchronousBlockDeviceStatus::NotReady
               : this->operations_.resolve_timeout(this, now_nanoseconds);
}

AsynchronousBlockDeviceStatus AsynchronousBlockDevice::TakeCompletion(BlockCompletion &completion,
                                                                      bool &available) noexcept {
    completion = BlockCompletion{};
    available = false;
    return this->operations_.take_completion == nullptr
               ? AsynchronousBlockDeviceStatus::NotReady
               : this->operations_.take_completion(this, completion, available);
}

BlockDeviceGeometry AsynchronousBlockDevice::Geometry() const noexcept {
    return this->operations_.geometry == nullptr ? BlockDeviceGeometry{}
                                                 : this->operations_.geometry(this);
}

}
