#pragma once

namespace os::kernel {

template <typename DriverType>
constexpr AsynchronousBlockDeviceAdapter<DriverType>::AsynchronousBlockDeviceAdapter() noexcept
    : AsynchronousBlockDevice(AsynchronousBlockDeviceOperations{
          .submit = &AsynchronousBlockDeviceAdapter<DriverType>::SubmitRequest,
          .cancel = &AsynchronousBlockDeviceAdapter<DriverType>::CancelRequest,
          .resolve_timeout = &AsynchronousBlockDeviceAdapter<DriverType>::ResolveRequestTimeouts,
          .take_completion = &AsynchronousBlockDeviceAdapter<DriverType>::TakeRequestCompletion,
          .geometry = &AsynchronousBlockDeviceAdapter<DriverType>::ReadGeometry,
      }) {}

template <typename DriverType>
AsynchronousBlockDeviceStatus AsynchronousBlockDeviceAdapter<DriverType>::SubmitRequest(
    AsynchronousBlockDevice *const device, const BlockOperation operation,
    const uint64_t logical_block_address, uint8_t *const buffer, const uint64_t buffer_size_bytes,
    const uint64_t owner_thread_index, const uint64_t deadline_nanoseconds,
    uint64_t &request_identifier) noexcept {
    AsynchronousBlockDeviceAdapter<DriverType> *const adapter =
        static_cast<AsynchronousBlockDeviceAdapter<DriverType> *>(device);
    return static_cast<DriverType *>(adapter)->SubmitBlockRequest(
        operation, logical_block_address, buffer, buffer_size_bytes, owner_thread_index,
        deadline_nanoseconds, request_identifier);
}

template <typename DriverType>
AsynchronousBlockDeviceStatus AsynchronousBlockDeviceAdapter<DriverType>::CancelRequest(
    AsynchronousBlockDevice *const device, const uint64_t request_identifier) noexcept {
    AsynchronousBlockDeviceAdapter<DriverType> *const adapter =
        static_cast<AsynchronousBlockDeviceAdapter<DriverType> *>(device);
    return static_cast<DriverType *>(adapter)->CancelBlockRequest(request_identifier);
}

template <typename DriverType>
AsynchronousBlockDeviceStatus AsynchronousBlockDeviceAdapter<DriverType>::ResolveRequestTimeouts(
    AsynchronousBlockDevice *const device, const uint64_t now_nanoseconds) noexcept {
    AsynchronousBlockDeviceAdapter<DriverType> *const adapter =
        static_cast<AsynchronousBlockDeviceAdapter<DriverType> *>(device);
    return static_cast<DriverType *>(adapter)->ResolveBlockTimeouts(now_nanoseconds);
}

template <typename DriverType>
AsynchronousBlockDeviceStatus AsynchronousBlockDeviceAdapter<DriverType>::TakeRequestCompletion(
    AsynchronousBlockDevice *const device, BlockCompletion &completion, bool &available) noexcept {
    AsynchronousBlockDeviceAdapter<DriverType> *const adapter =
        static_cast<AsynchronousBlockDeviceAdapter<DriverType> *>(device);
    return static_cast<DriverType *>(adapter)->TakeBlockCompletion(completion, available);
}

template <typename DriverType>
BlockDeviceGeometry AsynchronousBlockDeviceAdapter<DriverType>::ReadGeometry(
    const AsynchronousBlockDevice *const device) noexcept {
    const AsynchronousBlockDeviceAdapter<DriverType> *const adapter =
        static_cast<const AsynchronousBlockDeviceAdapter<DriverType> *>(device);
    return static_cast<const DriverType *>(adapter)->AsynchronousGeometry();
}

}
