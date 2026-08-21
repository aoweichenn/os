#pragma once

#include <stdint.h>

namespace os::kernel {

struct BlockDeviceGeometry final {
    uint64_t logical_block_size_bytes;
    uint64_t logical_block_count;
    uint64_t maximum_transfer_block_count;
    uint64_t maximum_outstanding_request_count;
    bool write_supported;
    bool flush_supported;
};

enum class BlockDeviceStatus : uint64_t {
    Succeeded,
    InvalidBlock,
    InvalidBuffer,
    ReadFailed,
    WriteFailed,
    FlushFailed,
};

class BlockDevice;

using BlockDeviceReadOperation = BlockDeviceStatus (*)(
    BlockDevice *device, uint64_t logical_block_address, uint8_t *block,
    uint64_t block_size_bytes) noexcept;
using BlockDeviceWriteOperation = BlockDeviceStatus (*)(
    BlockDevice *device, uint64_t logical_block_address, const uint8_t *block,
    uint64_t block_size_bytes) noexcept;
using BlockDeviceFlushOperation = BlockDeviceStatus (*)(BlockDevice *device) noexcept;

struct BlockDeviceOperations final {
    BlockDeviceReadOperation read;
    BlockDeviceWriteOperation write;
    BlockDeviceFlushOperation flush;
};

class BlockDevice {
  public:
    BlockDevice(const BlockDevice &) = delete;
    BlockDevice &operator=(const BlockDevice &) = delete;

    [[nodiscard]] BlockDeviceStatus ReadBlock(uint64_t logical_block_address, uint8_t *block,
                                              uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus WriteBlock(uint64_t logical_block_address,
                                               const uint8_t *block,
                                               uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus Flush() noexcept;

  protected:
    // constinit 驱动对象不能依赖启动运行时；该构造只复制静态函数表。
    constexpr explicit BlockDevice(const BlockDeviceOperations &operations) noexcept
        : operations_(operations) {}
    ~BlockDevice() noexcept = default;

  private:
    BlockDeviceOperations operations_;
};

template <typename DriverType> class BlockDeviceAdapter : public BlockDevice {
  protected:
    constexpr BlockDeviceAdapter() noexcept;
    ~BlockDeviceAdapter() noexcept = default;

  private:
    [[nodiscard]] static BlockDeviceStatus Read(
        BlockDevice *device, uint64_t logical_block_address, uint8_t *block,
        uint64_t block_size_bytes) noexcept;
    [[nodiscard]] static BlockDeviceStatus Write(
        BlockDevice *device, uint64_t logical_block_address, const uint8_t *block,
        uint64_t block_size_bytes) noexcept;
    [[nodiscard]] static BlockDeviceStatus FlushDevice(BlockDevice *device) noexcept;
};

}

#include <os/kernel/device/block_device.tpp>
