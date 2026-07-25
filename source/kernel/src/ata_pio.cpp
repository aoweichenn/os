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
constexpr uint8_t OS_KERNEL_ATA_WRITE_SECTORS_COMMAND = 0x30U;
constexpr uint8_t OS_KERNEL_ATA_FLUSH_CACHE_COMMAND = 0xE7U;
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
constexpr uint64_t OS_KERNEL_ATA_FIRST_WORD_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_ATA_EMPTY_POLL_COUNT = 0ULL;
constexpr uint8_t OS_KERNEL_ATA_EMPTY_STATUS = 0U;

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

    AtaPioStatus status =
        this->PrepareSectorRequest(logicalBlockAddress, OS_KERNEL_ATA_READ_SECTORS_COMMAND);
    if (status != AtaPioStatus::Succeeded) {
        return status;
    }
    status = this->WaitForDataRequest();
    if (status != AtaPioStatus::Succeeded) {
        return status;
    }
    for (uint64_t wordIndex = OS_KERNEL_ATA_FIRST_WORD_INDEX;
         wordIndex < OS_KERNEL_ATA_WORDS_PER_SECTOR; ++wordIndex) {
        const uint16_t word = ReadPort16(OS_KERNEL_ATA_DATA_PORT);
        const uint64_t byteIndex = wordIndex * OS_KERNEL_ATA_BYTES_PER_WORD;
        buffer[byteIndex] = static_cast<uint8_t>(word & OS_KERNEL_ATA_LOW_BYTE_MASK);
        buffer[byteIndex + OS_KERNEL_ATA_HIGH_BYTE_OFFSET_BYTES] =
            static_cast<uint8_t>(word >> OS_KERNEL_ATA_HIGH_BYTE_SHIFT_BITS);
    }
    return AtaPioStatus::Succeeded;
}

AtaPioStatus AtaPioDevice::WriteSector(const uint64_t logicalBlockAddress,
                                       const uint8_t *buffer,
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

    AtaPioStatus status =
        this->PrepareSectorRequest(logicalBlockAddress, OS_KERNEL_ATA_WRITE_SECTORS_COMMAND);
    if (status != AtaPioStatus::Succeeded) {
        return status;
    }
    status = this->WaitForDataRequest();
    if (status != AtaPioStatus::Succeeded) {
        return status;
    }
    for (uint64_t wordIndex = OS_KERNEL_ATA_FIRST_WORD_INDEX;
         wordIndex < OS_KERNEL_ATA_WORDS_PER_SECTOR; ++wordIndex) {
        const uint64_t byteIndex = wordIndex * OS_KERNEL_ATA_BYTES_PER_WORD;
        const uint16_t word =
            static_cast<uint16_t>(buffer[byteIndex]) |
            static_cast<uint16_t>(
                static_cast<uint16_t>(
                    buffer[byteIndex + OS_KERNEL_ATA_HIGH_BYTE_OFFSET_BYTES])
                << OS_KERNEL_ATA_HIGH_BYTE_SHIFT_BITS);
        WritePort16(OS_KERNEL_ATA_DATA_PORT, word);
    }
    return this->WaitUntilNotBusy();
}

AtaPioStatus AtaPioDevice::FlushCache() const noexcept {
    WritePort8(OS_KERNEL_ATA_DEVICE_CONTROL_PORT, OS_KERNEL_ATA_DISABLE_DEVICE_INTERRUPTS);
    AtaPioStatus status = this->WaitUntilNotBusy();
    if (status != AtaPioStatus::Succeeded) {
        return status;
    }
    WritePort8(OS_KERNEL_ATA_DRIVE_HEAD_PORT, OS_KERNEL_ATA_PRIMARY_MASTER_LBA_BASE);
    this->ApplyDeviceSelectDelay();
    WritePort8(OS_KERNEL_ATA_COMMAND_STATUS_PORT, OS_KERNEL_ATA_FLUSH_CACHE_COMMAND);
    return this->WaitUntilNotBusy();
}

FileSystemBlockDeviceStatus AtaPioDevice::ReadBlock(const uint64_t logicalBlockAddress,
                                                    uint8_t *block,
                                                    const uint64_t blockSizeBytes) noexcept {
    return this->ReadSector(logicalBlockAddress, block, blockSizeBytes) ==
                   AtaPioStatus::Succeeded
               ? FileSystemBlockDeviceStatus::Succeeded
               : FileSystemBlockDeviceStatus::ReadFailed;
}

FileSystemBlockDeviceStatus AtaPioDevice::WriteBlock(const uint64_t logicalBlockAddress,
                                                     const uint8_t *block,
                                                     const uint64_t blockSizeBytes) noexcept {
    return this->WriteSector(logicalBlockAddress, block, blockSizeBytes) ==
                   AtaPioStatus::Succeeded
               ? FileSystemBlockDeviceStatus::Succeeded
               : FileSystemBlockDeviceStatus::WriteFailed;
}

FileSystemBlockDeviceStatus AtaPioDevice::Flush() noexcept {
    return this->FlushCache() == AtaPioStatus::Succeeded
               ? FileSystemBlockDeviceStatus::Succeeded
               : FileSystemBlockDeviceStatus::FlushFailed;
}

AtaPioStatus AtaPioDevice::PrepareSectorRequest(const uint64_t logicalBlockAddress,
                                                const uint8_t command) const noexcept {
    // PIO 请求同步轮询完成，显式关闭 IRQ14，确保完成事件只有当前状态机拥有。
    WritePort8(OS_KERNEL_ATA_DEVICE_CONTROL_PORT, OS_KERNEL_ATA_DISABLE_DEVICE_INTERRUPTS);
    const AtaPioStatus status = this->WaitUntilNotBusy();
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
    WritePort8(OS_KERNEL_ATA_COMMAND_STATUS_PORT, command);
    return AtaPioStatus::Succeeded;
}

AtaPioStatus AtaPioDevice::WaitUntilNotBusy() const noexcept {
    uint64_t remainingPollCount = OS_KERNEL_ATA_STATUS_POLL_LIMIT;
    while (remainingPollCount > OS_KERNEL_ATA_EMPTY_POLL_COUNT) {
        const uint8_t status = ReadPort8(OS_KERNEL_ATA_COMMAND_STATUS_PORT);
        if ((status & OS_KERNEL_ATA_STATUS_BUSY_BIT) == OS_KERNEL_ATA_EMPTY_STATUS) {
            if ((status & OS_KERNEL_ATA_ERROR_STATUS_MASK) != OS_KERNEL_ATA_EMPTY_STATUS) {
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
    while (remainingPollCount > OS_KERNEL_ATA_EMPTY_POLL_COUNT) {
        const uint8_t status = ReadPort8(OS_KERNEL_ATA_COMMAND_STATUS_PORT);
        if ((status & OS_KERNEL_ATA_STATUS_BUSY_BIT) == OS_KERNEL_ATA_EMPTY_STATUS) {
            if ((status & OS_KERNEL_ATA_ERROR_STATUS_MASK) != OS_KERNEL_ATA_EMPTY_STATUS) {
                return AtaPioStatus::DeviceError;
            }
            if ((status & OS_KERNEL_ATA_STATUS_DATA_REQUEST_BIT) !=
                OS_KERNEL_ATA_EMPTY_STATUS) {
                return AtaPioStatus::Succeeded;
            }
        }
        --remainingPollCount;
    }
    return AtaPioStatus::DataRequestTimeout;
}

void AtaPioDevice::ApplyDeviceSelectDelay() const noexcept {
    for (uint64_t readCount = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
         readCount < OS_KERNEL_ATA_DEVICE_SELECT_DELAY_READ_COUNT; ++readCount) {
        static_cast<void>(ReadPort8(OS_KERNEL_ATA_DEVICE_CONTROL_PORT));
    }
}

}
