#pragma once

namespace os::kernel {

template <typename DriverType>
constexpr BlockDeviceAdapter<DriverType>::BlockDeviceAdapter() noexcept
    : BlockDevice(BlockDeviceOperations{
                      .read = &BlockDeviceAdapter<DriverType>::Read,
                      .write = &BlockDeviceAdapter<DriverType>::Write,
                      .flush = &BlockDeviceAdapter<DriverType>::FlushDevice,
                  }) {}

template <typename DriverType>
BlockDeviceStatus BlockDeviceAdapter<DriverType>::Read(
    BlockDevice *const device, const uint64_t logical_block_address, uint8_t *const block,
    const uint64_t block_size_bytes) noexcept {
    BlockDeviceAdapter<DriverType> *const adapter =
        static_cast<BlockDeviceAdapter<DriverType> *>(device);
    return static_cast<DriverType *>(adapter)->ReadBlock(logical_block_address, block,
                                                         block_size_bytes);
}

template <typename DriverType>
BlockDeviceStatus BlockDeviceAdapter<DriverType>::Write(
    BlockDevice *const device, const uint64_t logical_block_address, const uint8_t *const block,
    const uint64_t block_size_bytes) noexcept {
    BlockDeviceAdapter<DriverType> *const adapter =
        static_cast<BlockDeviceAdapter<DriverType> *>(device);
    return static_cast<DriverType *>(adapter)->WriteBlock(logical_block_address, block,
                                                          block_size_bytes);
}

template <typename DriverType>
BlockDeviceStatus
BlockDeviceAdapter<DriverType>::FlushDevice(BlockDevice *const device) noexcept {
    BlockDeviceAdapter<DriverType> *const adapter =
        static_cast<BlockDeviceAdapter<DriverType> *>(device);
    return static_cast<DriverType *>(adapter)->Flush();
}

}
