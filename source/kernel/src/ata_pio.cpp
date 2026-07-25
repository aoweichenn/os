#include "os/kernel/ata_pio.hpp"

#include "os/kernel/device_model.hpp"
#include "os/kernel/port_io.hpp"

namespace os::kernel {

namespace {

constexpr uint16_t OS_KERNEL_ATA_DATA_PORT = 0x01F0U;
constexpr uint16_t OS_KERNEL_ATA_SECTOR_COUNT_PORT = 0x01F2U;
constexpr uint16_t OS_KERNEL_ATA_LBA_LOW_PORT = 0x01F3U;
constexpr uint16_t OS_KERNEL_ATA_LBA_MID_PORT = 0x01F4U;
constexpr uint16_t OS_KERNEL_ATA_LBA_HIGH_PORT = 0x01F5U;
constexpr uint16_t OS_KERNEL_ATA_DRIVE_HEAD_PORT = 0x01F6U;
constexpr uint16_t OS_KERNEL_ATA_COMMAND_STATUS_PORT = 0x01F7U;
constexpr uint16_t OS_KERNEL_ATA_DEVICE_CONTROL_PORT = 0x03F6U;
constexpr uint8_t OS_KERNEL_ATA_PRIMARY_MASTER_LBA_BASE = 0xE0U;
constexpr uint8_t OS_KERNEL_ATA_DRIVE_HEAD_LBA_NIBBLE_MASK = 0x0FU;
constexpr uint64_t OS_KERNEL_ATA_LBA_HEAD_SHIFT_BITS = 24ULL;
constexpr uint64_t OS_KERNEL_ATA_LBA_MID_SHIFT_BITS = 8ULL;
constexpr uint64_t OS_KERNEL_ATA_LBA_HIGH_SHIFT_BITS = 16ULL;
constexpr uint8_t OS_KERNEL_ATA_READ_SECTORS_COMMAND = 0x20U;
constexpr uint8_t OS_KERNEL_ATA_SINGLE_SECTOR_COUNT = 0x01U;
constexpr uint8_t OS_KERNEL_ATA_STATUS_ERROR_BIT = 0x01U;
constexpr uint8_t OS_KERNEL_ATA_STATUS_DATA_REQUEST_BIT = 0x08U;
constexpr uint8_t OS_KERNEL_ATA_STATUS_DEVICE_FAULT_BIT = 0x20U;
constexpr uint8_t OS_KERNEL_ATA_STATUS_BUSY_BIT = 0x80U;
constexpr uint8_t OS_KERNEL_ATA_ERROR_STATUS_MASK =
    OS_KERNEL_ATA_STATUS_ERROR_BIT | OS_KERNEL_ATA_STATUS_DEVICE_FAULT_BIT;
constexpr uint8_t OS_KERNEL_ATA_DISABLE_DEVICE_INTERRUPTS = 0x02U;
constexpr uint64_t OS_KERNEL_ATA_STATUS_POLL_LIMIT = 0x0000FFFFULL;
constexpr uint64_t OS_KERNEL_ATA_WORDS_PER_SECTOR = 256ULL;
constexpr uint64_t OS_KERNEL_ATA_BYTES_PER_WORD = 2ULL;
constexpr uint64_t OS_KERNEL_ATA_HIGH_BYTE_OFFSET_BYTES = 1ULL;
constexpr uint16_t OS_KERNEL_ATA_LOW_BYTE_MASK = 0x00FFU;
constexpr uint64_t OS_KERNEL_ATA_HIGH_BYTE_SHIFT_BITS = 8ULL;
constexpr uint64_t OS_KERNEL_ATA_DEVICE_SELECT_DELAY_READ_COUNT = 4ULL;

}

AtaPioStatus AtaPioDevice::ReadSector(const uint64_t logicalBlockAddress, uint8_t *buffer,
                                      const uint64_t bufferSizeBytes) const noexcept {
    const AtaReadRequestStatus requestStatus =
        ValidateAtaReadRequest(logicalBlockAddress, buffer, bufferSizeBytes);
    if (requestStatus == AtaReadRequestStatus::NullBuffer) {
        return AtaPioStatus::NullBuffer;
    }
    if (requestStatus == AtaReadRequestStatus::InvalidBufferSize) {
        return AtaPioStatus::InvalidBufferSize;
    }
    if (requestStatus == AtaReadRequestStatus::InvalidLogicalBlockAddress) {
        return AtaPioStatus::InvalidLogicalBlockAddress;
    }

    // 本阶段以轮询完成单扇区请求，显式关闭设备 IRQ，避免未开放的 IRQ14
    // 与同步状态机同时拥有完成事件。
    WritePort8(OS_KERNEL_ATA_DEVICE_CONTROL_PORT, OS_KERNEL_ATA_DISABLE_DEVICE_INTERRUPTS);
    AtaPioStatus status = this->WaitUntilNotBusy();
    if (status != AtaPioStatus::Succeeded) {
        return status;
    }

    const uint8_t driveHead = static_cast<uint8_t>(
        OS_KERNEL_ATA_PRIMARY_MASTER_LBA_BASE |
        static_cast<uint8_t>((logicalBlockAddress >> OS_KERNEL_ATA_LBA_HEAD_SHIFT_BITS) &
                             OS_KERNEL_ATA_DRIVE_HEAD_LBA_NIBBLE_MASK));
    WritePort8(OS_KERNEL_ATA_DRIVE_HEAD_PORT, driveHead);
    this->ApplyDeviceSelectDelay();
    WritePort8(OS_KERNEL_ATA_SECTOR_COUNT_PORT, OS_KERNEL_ATA_SINGLE_SECTOR_COUNT);
    WritePort8(OS_KERNEL_ATA_LBA_LOW_PORT, static_cast<uint8_t>(logicalBlockAddress));
    WritePort8(OS_KERNEL_ATA_LBA_MID_PORT,
               static_cast<uint8_t>(logicalBlockAddress >> OS_KERNEL_ATA_LBA_MID_SHIFT_BITS));
    WritePort8(OS_KERNEL_ATA_LBA_HIGH_PORT,
               static_cast<uint8_t>(logicalBlockAddress >> OS_KERNEL_ATA_LBA_HIGH_SHIFT_BITS));
    WritePort8(OS_KERNEL_ATA_COMMAND_STATUS_PORT, OS_KERNEL_ATA_READ_SECTORS_COMMAND);

    status = this->WaitForDataRequest();
    if (status != AtaPioStatus::Succeeded) {
        return status;
    }
    for (uint64_t wordIndex = 0ULL; wordIndex < OS_KERNEL_ATA_WORDS_PER_SECTOR; ++wordIndex) {
        const uint16_t word = ReadPort16(OS_KERNEL_ATA_DATA_PORT);
        const uint64_t byteIndex = wordIndex * OS_KERNEL_ATA_BYTES_PER_WORD;
        buffer[byteIndex] = static_cast<uint8_t>(word & OS_KERNEL_ATA_LOW_BYTE_MASK);
        buffer[byteIndex + OS_KERNEL_ATA_HIGH_BYTE_OFFSET_BYTES] =
            static_cast<uint8_t>(word >> OS_KERNEL_ATA_HIGH_BYTE_SHIFT_BITS);
    }
    return AtaPioStatus::Succeeded;
}

AtaPioStatus AtaPioDevice::WaitUntilNotBusy() const noexcept {
    uint64_t remainingPollCount = OS_KERNEL_ATA_STATUS_POLL_LIMIT;
    while (remainingPollCount > 0ULL) {
        const uint8_t status = ReadPort8(OS_KERNEL_ATA_COMMAND_STATUS_PORT);
        if ((status & OS_KERNEL_ATA_STATUS_BUSY_BIT) == 0U) {
            if ((status & OS_KERNEL_ATA_ERROR_STATUS_MASK) != 0U) {
                return AtaPioStatus::DeviceError;
            }
            return AtaPioStatus::Succeeded;
        }
        --remainingPollCount;
    }
    return AtaPioStatus::BusyTimeout;
}

AtaPioStatus AtaPioDevice::WaitForDataRequest() const noexcept {
    uint64_t remainingPollCount = OS_KERNEL_ATA_STATUS_POLL_LIMIT;
    while (remainingPollCount > 0ULL) {
        const uint8_t status = ReadPort8(OS_KERNEL_ATA_COMMAND_STATUS_PORT);
        if ((status & OS_KERNEL_ATA_STATUS_BUSY_BIT) == 0U) {
            if ((status & OS_KERNEL_ATA_ERROR_STATUS_MASK) != 0U) {
                return AtaPioStatus::DeviceError;
            }
            if ((status & OS_KERNEL_ATA_STATUS_DATA_REQUEST_BIT) != 0U) {
                return AtaPioStatus::Succeeded;
            }
        }
        --remainingPollCount;
    }
    return AtaPioStatus::DataRequestTimeout;
}

void AtaPioDevice::ApplyDeviceSelectDelay() const noexcept {
    for (uint64_t readCount = 0ULL; readCount < OS_KERNEL_ATA_DEVICE_SELECT_DELAY_READ_COUNT;
         ++readCount) {
        static_cast<void>(ReadPort8(OS_KERNEL_ATA_DEVICE_CONTROL_PORT));
    }
}

}
