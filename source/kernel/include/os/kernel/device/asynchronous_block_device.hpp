#pragma once

#include <os/kernel/device/block_request.hpp>

#include <stdint.h>

namespace os::kernel {

enum class AsynchronousBlockDeviceStatus : uint64_t {
    Succeeded,
    NotReady,
    InvalidRequest,
    CapacityExhausted,
    RequestNotFound,
    RequestInProgress,
    RequestAlreadyResolved,
    DeviceFailure,
    Corrupt,
};

class AsynchronousBlockDevice;

using AsynchronousBlockDeviceSubmitOperation = AsynchronousBlockDeviceStatus (*)(
    AsynchronousBlockDevice *device, BlockOperation operation, uint64_t logical_block_address,
    uint8_t *buffer, uint64_t buffer_size_bytes, uint64_t owner_thread_index,
    uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept;
using AsynchronousBlockDeviceCancelOperation = AsynchronousBlockDeviceStatus (*)(
    AsynchronousBlockDevice *device, uint64_t request_identifier) noexcept;
using AsynchronousBlockDeviceResolveTimeoutOperation = AsynchronousBlockDeviceStatus (*)(
    AsynchronousBlockDevice *device, uint64_t now_nanoseconds) noexcept;
using AsynchronousBlockDeviceTakeCompletionOperation = AsynchronousBlockDeviceStatus (*)(
    AsynchronousBlockDevice *device, BlockCompletion &completion, bool &available) noexcept;
using AsynchronousBlockDeviceGeometryOperation =
    BlockDeviceGeometry (*)(const AsynchronousBlockDevice *device) noexcept;

struct AsynchronousBlockDeviceOperations final {
    AsynchronousBlockDeviceSubmitOperation submit;
    AsynchronousBlockDeviceCancelOperation cancel;
    AsynchronousBlockDeviceResolveTimeoutOperation resolve_timeout;
    AsynchronousBlockDeviceTakeCompletionOperation take_completion;
    AsynchronousBlockDeviceGeometryOperation geometry;
};

class AsynchronousBlockDevice {
  public:
    AsynchronousBlockDevice(const AsynchronousBlockDevice &) = delete;
    AsynchronousBlockDevice &operator=(const AsynchronousBlockDevice &) = delete;

    [[nodiscard]] AsynchronousBlockDeviceStatus
    Submit(BlockOperation operation, uint64_t logical_block_address, uint8_t *buffer,
           uint64_t buffer_size_bytes, uint64_t owner_thread_index, uint64_t deadline_nanoseconds,
           uint64_t &request_identifier) noexcept;
    [[nodiscard]] AsynchronousBlockDeviceStatus Cancel(uint64_t request_identifier) noexcept;
    [[nodiscard]] AsynchronousBlockDeviceStatus ResolveTimeouts(uint64_t now_nanoseconds) noexcept;
    [[nodiscard]] AsynchronousBlockDeviceStatus TakeCompletion(BlockCompletion &completion,
                                                               bool &available) noexcept;
    [[nodiscard]] BlockDeviceGeometry Geometry() const noexcept;

  protected:
    // constinit 驱动对象不能依赖启动运行时；该构造只复制静态函数表。
    constexpr explicit AsynchronousBlockDevice(
        const AsynchronousBlockDeviceOperations &operations) noexcept
        : operations_(operations) {}
    ~AsynchronousBlockDevice() noexcept = default;

  private:
    AsynchronousBlockDeviceOperations operations_;
};

template <typename DriverType>
class AsynchronousBlockDeviceAdapter : public AsynchronousBlockDevice {
  protected:
    constexpr AsynchronousBlockDeviceAdapter() noexcept;
    ~AsynchronousBlockDeviceAdapter() noexcept = default;

  private:
    [[nodiscard]] static AsynchronousBlockDeviceStatus
    SubmitRequest(AsynchronousBlockDevice *device, BlockOperation operation,
                  uint64_t logical_block_address, uint8_t *buffer, uint64_t buffer_size_bytes,
                  uint64_t owner_thread_index, uint64_t deadline_nanoseconds,
                  uint64_t &request_identifier) noexcept;
    [[nodiscard]] static AsynchronousBlockDeviceStatus
    CancelRequest(AsynchronousBlockDevice *device, uint64_t request_identifier) noexcept;
    [[nodiscard]] static AsynchronousBlockDeviceStatus
    ResolveRequestTimeouts(AsynchronousBlockDevice *device, uint64_t now_nanoseconds) noexcept;
    [[nodiscard]] static AsynchronousBlockDeviceStatus
    TakeRequestCompletion(AsynchronousBlockDevice *device, BlockCompletion &completion,
                          bool &available) noexcept;
    [[nodiscard]] static BlockDeviceGeometry
    ReadGeometry(const AsynchronousBlockDevice *device) noexcept;
};

}

#include <os/kernel/device/asynchronous_block_device.tpp>
