#pragma once

#include <os/kernel/device/asynchronous_block_device.hpp>
#include <os/kernel/device/block_device.hpp>
#include <os/kernel/process/block_io.hpp>

#include <stdint.h>

namespace os::kernel {

enum class RuntimeBlockIoStatus : uint64_t {
    Succeeded,
    NotAvailable,
    InvalidRequest,
    SubmitFailed,
    WaitFailed,
    DeviceFailed,
    TimedOut,
    Cancelled,
    Corrupt,
};

struct BlockIoDeviceStatistics final {
    uint64_t synchronous_fallback_count;
    uint64_t asynchronous_read_count;
    uint64_t asynchronous_write_count;
    uint64_t asynchronous_flush_count;
    uint64_t asynchronous_failure_count;
};

[[nodiscard]] bool RuntimeBlockIoWaitAvailable() noexcept;
[[nodiscard]] RuntimeBlockIoStatus
RegisterRuntimeBlockIoDevice(AsynchronousBlockDevice &device) noexcept;
[[nodiscard]] RuntimeBlockIoStatus AwaitRuntimeBlockIo(AsynchronousBlockDevice &device,
                                                       BlockOperation operation,
                                                       uint64_t logical_block_address,
                                                       uint8_t *buffer, uint64_t buffer_size_bytes,
                                                       uint64_t timeout_nanoseconds) noexcept;
void NotifyRuntimeBlockIoCompletion() noexcept;
void ServiceRuntimeBlockIoTimeouts(uint64_t now_nanoseconds) noexcept;
[[nodiscard]] BlockIoStatistics GetRuntimeBlockIoStatistics() noexcept;

class BlockIoDevice final : public BlockDeviceAdapter<BlockIoDevice> {
  public:
    [[nodiscard]] RuntimeBlockIoStatus Initialize(BlockDevice &synchronous_device,
                                                  AsynchronousBlockDevice &asynchronous_device,
                                                  uint64_t timeout_nanoseconds,
                                                  bool asynchronous_wait_enabled) noexcept;
    [[nodiscard]] BlockDeviceStatus ReadBlock(uint64_t logical_block_address, uint8_t *block,
                                              uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus WriteBlock(uint64_t logical_block_address, const uint8_t *block,
                                               uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus Flush() noexcept;
    [[nodiscard]] BlockIoDeviceStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] BlockDeviceStatus Transfer(BlockOperation operation,
                                             uint64_t logical_block_address, uint8_t *buffer,
                                             uint64_t buffer_size_bytes) noexcept;

    BlockDevice *synchronous_device_{};
    AsynchronousBlockDevice *asynchronous_device_{};
    uint64_t timeout_nanoseconds_{};
    BlockIoDeviceStatistics statistics_{};
    bool asynchronous_wait_enabled_{};
    bool initialized_{};
};

}
