#include <os/kernel/device/ata_pio.hpp>

#include <os/kernel/device/device_model.hpp>
#include <os/kernel/device/port_io.hpp>

namespace os::kernel {

namespace {

constexpr uint16_t OS_KERNEL_ATA_DATA_PORT_OFFSET = 0x0000U;
constexpr uint16_t OS_KERNEL_ATA_SECTOR_COUNT_PORT_OFFSET = 0x0002U;
constexpr uint16_t OS_KERNEL_ATA_LBA_LOW_PORT_OFFSET = 0x0003U;
constexpr uint16_t OS_KERNEL_ATA_LBA_MID_PORT_OFFSET = 0x0004U;
constexpr uint16_t OS_KERNEL_ATA_LBA_HIGH_PORT_OFFSET = 0x0005U;
constexpr uint16_t OS_KERNEL_ATA_DRIVE_HEAD_PORT_OFFSET = 0x0006U;
constexpr uint16_t OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET = 0x0007U;
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
constexpr uint8_t OS_KERNEL_ATA_ENABLE_DEVICE_INTERRUPTS = 0x00U;
constexpr uint8_t OS_KERNEL_ATA_SOFTWARE_RESET = 0x04U;
constexpr uint64_t OS_KERNEL_ATA_STATUS_POLL_LIMIT = 0x0000FFFFULL;
constexpr uint64_t OS_KERNEL_ATA_WORDS_PER_SECTOR = 256ULL;
constexpr uint64_t OS_KERNEL_ATA_BYTES_PER_WORD = 2ULL;
constexpr uint64_t OS_KERNEL_ATA_HIGH_BYTE_OFFSET_BYTES = 1ULL;
constexpr uint16_t OS_KERNEL_ATA_LOW_BYTE_MASK = 0x00FFU;
constexpr uint64_t OS_KERNEL_ATA_HIGH_BYTE_SHIFT_BITS = 8ULL;
constexpr uint64_t OS_KERNEL_ATA_DEVICE_SELECT_DELAY_READ_COUNT = 4ULL;
constexpr uint64_t OS_KERNEL_ATA_FIRST_WORD_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_ATA_EMPTY_POLL_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_ATA_SINGLE_TRANSFER_BLOCK_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_ATA_SINGLE_OUTSTANDING_REQUEST_COUNT = 1ULL;
constexpr uint8_t OS_KERNEL_ATA_EMPTY_STATUS = 0U;
constexpr BlockDeviceGeometry OS_KERNEL_ATA_BLOCK_GEOMETRY{
    .logical_block_size_bytes = OS_KERNEL_DEVICE_ATA_SECTOR_SIZE_BYTES,
    .logical_block_count = OS_KERNEL_DEVICE_ATA_MAXIMUM_LBA28 +
                           OS_KERNEL_ATA_SINGLE_TRANSFER_BLOCK_COUNT,
    .maximum_transfer_block_count = OS_KERNEL_ATA_SINGLE_TRANSFER_BLOCK_COUNT,
    .maximum_outstanding_request_count = OS_KERNEL_ATA_SINGLE_OUTSTANDING_REQUEST_COUNT,
    .write_supported = true,
    .flush_supported = true,
};

}

AtaPioStatus AtaPioDevice::ReadSector(const uint64_t logical_block_address, uint8_t *buffer,
                                      const uint64_t buffer_size_bytes) noexcept {
    const AtaReadRequestStatus request_status =
        ValidateAtaReadRequest(logical_block_address, buffer, buffer_size_bytes);
    if (request_status == AtaReadRequestStatus::NullBuffer) {
        return AtaPioStatus::NullBuffer;
    }
    if (request_status == AtaReadRequestStatus::InvalidBufferSize) {
        return AtaPioStatus::InvalidBufferSize;
    }
    if (request_status == AtaReadRequestStatus::InvalidLogicalBlockAddress) {
        return AtaPioStatus::InvalidLogicalBlockAddress;
    }

    if (!this->controller_available_) {
        return AtaPioStatus::DeviceError;
    }
    if (this->HasOutstandingRequest()) {
        return AtaPioStatus::RequestInProgress;
    }
    this->EnterPollingMode();
    AtaPioStatus status =
        this->PrepareSectorRequest(logical_block_address, OS_KERNEL_ATA_READ_SECTORS_COMMAND);
    if (status != AtaPioStatus::Succeeded) {
        this->RestoreInterruptMode();
        return status;
    }
    status = this->WaitForDataRequest();
    if (status != AtaPioStatus::Succeeded) {
        this->RestoreInterruptMode();
        return status;
    }
    this->TransferReadSector(buffer);
    this->RestoreInterruptMode();
    return AtaPioStatus::Succeeded;
}

AtaPioStatus AtaPioDevice::WriteSector(const uint64_t logical_block_address, const uint8_t *buffer,
                                       const uint64_t buffer_size_bytes) noexcept {
    const AtaReadRequestStatus request_status =
        ValidateAtaReadRequest(logical_block_address, buffer, buffer_size_bytes);
    if (request_status == AtaReadRequestStatus::NullBuffer) {
        return AtaPioStatus::NullBuffer;
    }
    if (request_status == AtaReadRequestStatus::InvalidBufferSize) {
        return AtaPioStatus::InvalidBufferSize;
    }
    if (request_status == AtaReadRequestStatus::InvalidLogicalBlockAddress) {
        return AtaPioStatus::InvalidLogicalBlockAddress;
    }

    if (!this->controller_available_) {
        return AtaPioStatus::DeviceError;
    }
    if (this->HasOutstandingRequest()) {
        return AtaPioStatus::RequestInProgress;
    }
    this->EnterPollingMode();
    AtaPioStatus status =
        this->PrepareSectorRequest(logical_block_address, OS_KERNEL_ATA_WRITE_SECTORS_COMMAND);
    if (status != AtaPioStatus::Succeeded) {
        this->RestoreInterruptMode();
        return status;
    }
    status = this->WaitForDataRequest();
    if (status != AtaPioStatus::Succeeded) {
        this->RestoreInterruptMode();
        return status;
    }
    this->TransferWriteSector(buffer);
    status = this->WaitUntilNotBusy();
    this->RestoreInterruptMode();
    return status;
}

AtaPioStatus AtaPioDevice::FlushCache() noexcept {
    if (!this->controller_available_) {
        return AtaPioStatus::DeviceError;
    }
    if (this->HasOutstandingRequest()) {
        return AtaPioStatus::RequestInProgress;
    }
    this->EnterPollingMode();
    AtaPioStatus status = this->WaitUntilNotBusy();
    if (status != AtaPioStatus::Succeeded) {
        this->RestoreInterruptMode();
        return status;
    }
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_DRIVE_HEAD_PORT_OFFSET,
               OS_KERNEL_ATA_MASTER_LBA_BASE);
    this->ApplyDeviceSelectDelay();
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET,
               OS_KERNEL_ATA_FLUSH_CACHE_COMMAND);
    status = this->WaitUntilNotBusy();
    this->RestoreInterruptMode();
    return status;
}

AtaPioStatus
AtaPioDevice::InitializeAsynchronousRequests(BlockRequest *const request_storage,
                                             const uint64_t request_capacity) noexcept {
    if (this->asynchronous_initialized_) {
        return AtaPioStatus::AlreadyInitialized;
    }
    if (this->request_queue_.Initialize(request_storage, request_capacity,
                                        OS_KERNEL_ATA_BLOCK_GEOMETRY) !=
        BlockRequestQueueStatus::Succeeded) {
        return AtaPioStatus::RequestQueueFailure;
    }
    this->active_request_ = BlockRequest{};
    this->controller_available_ = true;
    this->write_data_transferred_ = false;
    this->interrupt_count_ = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
    this->spurious_interrupt_count_ = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
    this->timeout_recovery_count_ = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
    this->timeout_recovery_failure_count_ = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
    this->software_reset_count_ = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
    this->read_completion_count_ = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
    this->write_completion_count_ = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
    this->flush_completion_count_ = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
    this->asynchronous_initialized_ = true;
    WritePort8(this->device_control_port_, OS_KERNEL_ATA_ENABLE_DEVICE_INTERRUPTS);
    return AtaPioStatus::Succeeded;
}

AtaPioStatus AtaPioDevice::SubmitAsynchronous(
    const BlockOperation operation, const uint64_t logical_block_address, uint8_t *const buffer,
    const uint64_t buffer_size_bytes, const uint64_t owner_thread_index,
    const uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept {
    if (!this->asynchronous_initialized_) {
        request_identifier = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
        return AtaPioStatus::NotInitialized;
    }
    if (!this->controller_available_) {
        request_identifier = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
        return AtaPioStatus::DeviceError;
    }
    return this->request_queue_.Submit(operation, logical_block_address, buffer, buffer_size_bytes,
                                       owner_thread_index, deadline_nanoseconds,
                                       request_identifier) == BlockRequestQueueStatus::Succeeded
               ? AtaPioStatus::Succeeded
               : AtaPioStatus::RequestQueueFailure;
}

AtaPioStatus AtaPioDevice::StartNextAsynchronous(AtaPioCompletion &completion,
                                                 bool &request_started) noexcept {
    completion = AtaPioCompletion{};
    request_started = false;
    if (!this->asynchronous_initialized_) {
        return AtaPioStatus::NotInitialized;
    }
    BlockRequest request{};
    bool issued = false;
    if (this->request_queue_.IssueNext(request, issued) != BlockRequestQueueStatus::Succeeded) {
        return AtaPioStatus::RequestQueueFailure;
    }
    if (!issued) {
        return AtaPioStatus::Succeeded;
    }
    this->active_request_ = request;
    this->write_data_transferred_ = false;
    if (!this->controller_available_) {
        return this->ResolveIssuedRequest(BlockRequestResult::DeviceError, completion);
    }
    const AtaPioStatus issue_status = this->PrepareAsynchronousRequest(request);
    if (issue_status != AtaPioStatus::Succeeded) {
        return this->ResolveIssuedRequest(BlockRequestResult::DeviceError, completion);
    }
    request_started = true;
    return AtaPioStatus::Succeeded;
}

AtaPioStatus AtaPioDevice::HandleInterrupt(AtaPioCompletion &completion) noexcept {
    completion = AtaPioCompletion{};
    if (!this->asynchronous_initialized_) {
        return AtaPioStatus::NotInitialized;
    }
    ++this->interrupt_count_;
    if (this->active_request_.state != BlockRequestState::Issued) {
        ++this->spurious_interrupt_count_;
        static_cast<void>(
            ReadPort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET));
        return AtaPioStatus::Succeeded;
    }

    const uint8_t status =
        ReadPort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET);
    if ((status & OS_KERNEL_ATA_ERROR_STATUS_MASK) != OS_KERNEL_ATA_EMPTY_STATUS) {
        return this->ResolveIssuedRequest(BlockRequestResult::DeviceError, completion);
    }
    if ((status & OS_KERNEL_ATA_STATUS_BUSY_BIT) != OS_KERNEL_ATA_EMPTY_STATUS) {
        ++this->spurious_interrupt_count_;
        return AtaPioStatus::Succeeded;
    }
    if (this->active_request_.operation == BlockOperation::Read) {
        if ((status & OS_KERNEL_ATA_STATUS_DATA_REQUEST_BIT) == OS_KERNEL_ATA_EMPTY_STATUS) {
            return this->ResolveIssuedRequest(BlockRequestResult::DeviceError, completion);
        }
        this->TransferReadSector(this->active_request_.buffer);
        ++this->read_completion_count_;
        return this->ResolveIssuedRequest(BlockRequestResult::Succeeded, completion);
    }
    if (this->active_request_.operation == BlockOperation::Write) {
        if (!this->write_data_transferred_) {
            if ((status & OS_KERNEL_ATA_STATUS_DATA_REQUEST_BIT) == OS_KERNEL_ATA_EMPTY_STATUS) {
                return this->ResolveIssuedRequest(BlockRequestResult::DeviceError, completion);
            }
            this->TransferWriteSector(this->active_request_.buffer);
            this->write_data_transferred_ = true;
            return AtaPioStatus::Succeeded;
        }
        ++this->write_completion_count_;
        return this->ResolveIssuedRequest(BlockRequestResult::Succeeded, completion);
    }
    if (this->active_request_.operation == BlockOperation::Flush) {
        ++this->flush_completion_count_;
        return this->ResolveIssuedRequest(BlockRequestResult::Succeeded, completion);
    }
    return this->ResolveIssuedRequest(BlockRequestResult::DeviceError, completion);
}

AtaPioStatus AtaPioDevice::ResolveTimeout(const uint64_t now_nanoseconds,
                                          AtaPioCompletion &completion) noexcept {
    completion = AtaPioCompletion{};
    if (!this->asynchronous_initialized_) {
        return AtaPioStatus::NotInitialized;
    }
    BlockRequest request{};
    bool resolved = false;
    const BlockRequestQueueStatus timeout_status =
        this->request_queue_.ResolveTimeout(now_nanoseconds, request, resolved);
    if (timeout_status != BlockRequestQueueStatus::Succeeded) {
        return AtaPioStatus::RequestQueueFailure;
    }
    if (!resolved) {
        return AtaPioStatus::Succeeded;
    }
    const AtaPioStatus reset_status = this->ResetController();
    if (reset_status == AtaPioStatus::Succeeded) {
        ++this->timeout_recovery_count_;
    } else {
        ++this->timeout_recovery_failure_count_;
        this->controller_available_ = false;
    }
    completion = AtaPioCompletion{
        .request_identifier = request.identifier,
        .owner_thread_index = request.owner_thread_index,
        .operation = request.operation,
        .result = BlockRequestResult::TimedOut,
        .ready = true,
    };
    if (this->request_queue_.Reap(request.identifier) != BlockRequestQueueStatus::Succeeded) {
        return AtaPioStatus::RequestQueueFailure;
    }
    this->active_request_ = BlockRequest{};
    this->write_data_transferred_ = false;
    return AtaPioStatus::Succeeded;
}

AtaPioStatistics AtaPioDevice::Statistics() const noexcept {
    return AtaPioStatistics{
        .request_queue = this->request_queue_.Statistics(),
        .interrupt_count = this->interrupt_count_,
        .spurious_interrupt_count = this->spurious_interrupt_count_,
        .timeout_recovery_count = this->timeout_recovery_count_,
        .timeout_recovery_failure_count = this->timeout_recovery_failure_count_,
        .software_reset_count = this->software_reset_count_,
        .read_completion_count = this->read_completion_count_,
        .write_completion_count = this->write_completion_count_,
        .flush_completion_count = this->flush_completion_count_,
    };
}

FileSystemBlockDeviceStatus AtaPioDevice::ReadBlock(const uint64_t logical_block_address,
                                                    uint8_t *block,
                                                    const uint64_t block_size_bytes) noexcept {
    return this->ReadSector(logical_block_address, block, block_size_bytes) ==
                   AtaPioStatus::Succeeded
               ? FileSystemBlockDeviceStatus::Succeeded
               : FileSystemBlockDeviceStatus::ReadFailed;
}

FileSystemBlockDeviceStatus AtaPioDevice::WriteBlock(const uint64_t logical_block_address,
                                                     const uint8_t *block,
                                                     const uint64_t block_size_bytes) noexcept {
    return this->WriteSector(logical_block_address, block, block_size_bytes) ==
                   AtaPioStatus::Succeeded
               ? FileSystemBlockDeviceStatus::Succeeded
               : FileSystemBlockDeviceStatus::WriteFailed;
}

FileSystemBlockDeviceStatus AtaPioDevice::Flush() noexcept {
    return this->FlushCache() == AtaPioStatus::Succeeded ? FileSystemBlockDeviceStatus::Succeeded
                                                         : FileSystemBlockDeviceStatus::FlushFailed;
}

AtaPioStatus AtaPioDevice::PrepareSectorRequest(const uint64_t logical_block_address,
                                                const uint8_t command) noexcept {
    const AtaPioStatus status = this->WaitUntilNotBusy();
    if (status != AtaPioStatus::Succeeded) {
        return status;
    }
    const uint8_t drive_head = static_cast<uint8_t>(
        OS_KERNEL_ATA_MASTER_LBA_BASE |
        static_cast<uint8_t>((logical_block_address >> OS_KERNEL_ATA_LBA_HEAD_SHIFT_BITS) &
                             OS_KERNEL_ATA_DRIVE_HEAD_LBA_NIBBLE_MASK));
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_DRIVE_HEAD_PORT_OFFSET, drive_head);
    this->ApplyDeviceSelectDelay();
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_SECTOR_COUNT_PORT_OFFSET,
               OS_KERNEL_ATA_SINGLE_SECTOR_COUNT);
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_LBA_LOW_PORT_OFFSET,
               static_cast<uint8_t>(logical_block_address));
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_LBA_MID_PORT_OFFSET,
               static_cast<uint8_t>(logical_block_address >> OS_KERNEL_ATA_LBA_MID_SHIFT_BITS));
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_LBA_HIGH_PORT_OFFSET,
               static_cast<uint8_t>(logical_block_address >> OS_KERNEL_ATA_LBA_HIGH_SHIFT_BITS));
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET, command);
    return AtaPioStatus::Succeeded;
}

AtaPioStatus AtaPioDevice::PrepareAsynchronousRequest(const BlockRequest &request) noexcept {
    WritePort8(this->device_control_port_, OS_KERNEL_ATA_ENABLE_DEVICE_INTERRUPTS);
    const uint8_t status =
        ReadPort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET);
    if ((status & OS_KERNEL_ATA_STATUS_BUSY_BIT) != OS_KERNEL_ATA_EMPTY_STATUS) {
        return AtaPioStatus::BusyTimeout;
    }
    if ((status & OS_KERNEL_ATA_ERROR_STATUS_MASK) != OS_KERNEL_ATA_EMPTY_STATUS) {
        return AtaPioStatus::DeviceError;
    }
    if (request.operation == BlockOperation::Flush) {
        WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_DRIVE_HEAD_PORT_OFFSET,
                   OS_KERNEL_ATA_MASTER_LBA_BASE);
        this->ApplyDeviceSelectDelay();
        WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET,
                   OS_KERNEL_ATA_FLUSH_CACHE_COMMAND);
        return AtaPioStatus::Succeeded;
    }

    const uint8_t drive_head = static_cast<uint8_t>(
        OS_KERNEL_ATA_MASTER_LBA_BASE |
        static_cast<uint8_t>((request.logical_block_address >> OS_KERNEL_ATA_LBA_HEAD_SHIFT_BITS) &
                             OS_KERNEL_ATA_DRIVE_HEAD_LBA_NIBBLE_MASK));
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_DRIVE_HEAD_PORT_OFFSET, drive_head);
    this->ApplyDeviceSelectDelay();
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_SECTOR_COUNT_PORT_OFFSET,
               OS_KERNEL_ATA_SINGLE_SECTOR_COUNT);
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_LBA_LOW_PORT_OFFSET,
               static_cast<uint8_t>(request.logical_block_address));
    WritePort8(
        this->command_block_base_port_ + OS_KERNEL_ATA_LBA_MID_PORT_OFFSET,
        static_cast<uint8_t>(request.logical_block_address >> OS_KERNEL_ATA_LBA_MID_SHIFT_BITS));
    WritePort8(
        this->command_block_base_port_ + OS_KERNEL_ATA_LBA_HIGH_PORT_OFFSET,
        static_cast<uint8_t>(request.logical_block_address >> OS_KERNEL_ATA_LBA_HIGH_SHIFT_BITS));
    WritePort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET,
               request.operation == BlockOperation::Read ? OS_KERNEL_ATA_READ_SECTORS_COMMAND
                                                         : OS_KERNEL_ATA_WRITE_SECTORS_COMMAND);
    return AtaPioStatus::Succeeded;
}

AtaPioStatus AtaPioDevice::ResolveIssuedRequest(const BlockRequestResult result,
                                                AtaPioCompletion &completion) noexcept {
    completion = AtaPioCompletion{};
    if (this->active_request_.state != BlockRequestState::Issued ||
        this->request_queue_.Complete(this->active_request_.identifier, result) !=
            BlockRequestQueueStatus::Succeeded) {
        return AtaPioStatus::RequestQueueFailure;
    }
    BlockRequest completed_request{};
    if (this->request_queue_.Read(this->active_request_.identifier, completed_request) !=
            BlockRequestQueueStatus::Succeeded ||
        this->request_queue_.Reap(this->active_request_.identifier) !=
            BlockRequestQueueStatus::Succeeded) {
        return AtaPioStatus::RequestQueueFailure;
    }
    completion = AtaPioCompletion{
        .request_identifier = completed_request.identifier,
        .owner_thread_index = completed_request.owner_thread_index,
        .operation = completed_request.operation,
        .result = completed_request.result,
        .ready = true,
    };
    this->active_request_ = BlockRequest{};
    this->write_data_transferred_ = false;
    return AtaPioStatus::Succeeded;
}

bool AtaPioDevice::HasOutstandingRequest() const noexcept {
    return this->asynchronous_initialized_ &&
           this->request_queue_.Statistics().active_request_count != OS_KERNEL_ATA_EMPTY_POLL_COUNT;
}

void AtaPioDevice::EnterPollingMode() noexcept {
    WritePort8(this->device_control_port_, OS_KERNEL_ATA_DISABLE_DEVICE_INTERRUPTS);
}

void AtaPioDevice::RestoreInterruptMode() noexcept {
    if (this->asynchronous_initialized_) {
        WritePort8(this->device_control_port_, OS_KERNEL_ATA_ENABLE_DEVICE_INTERRUPTS);
    }
}

void AtaPioDevice::TransferReadSector(uint8_t *const buffer) noexcept {
    for (uint64_t word_index = OS_KERNEL_ATA_FIRST_WORD_INDEX;
         word_index < OS_KERNEL_ATA_WORDS_PER_SECTOR; ++word_index) {
        const uint16_t word =
            ReadPort16(this->command_block_base_port_ + OS_KERNEL_ATA_DATA_PORT_OFFSET);
        const uint64_t byte_index = word_index * OS_KERNEL_ATA_BYTES_PER_WORD;
        buffer[byte_index] = static_cast<uint8_t>(word & OS_KERNEL_ATA_LOW_BYTE_MASK);
        buffer[byte_index + OS_KERNEL_ATA_HIGH_BYTE_OFFSET_BYTES] =
            static_cast<uint8_t>(word >> OS_KERNEL_ATA_HIGH_BYTE_SHIFT_BITS);
    }
}

void AtaPioDevice::TransferWriteSector(const uint8_t *const buffer) noexcept {
    for (uint64_t word_index = OS_KERNEL_ATA_FIRST_WORD_INDEX;
         word_index < OS_KERNEL_ATA_WORDS_PER_SECTOR; ++word_index) {
        const uint64_t byte_index = word_index * OS_KERNEL_ATA_BYTES_PER_WORD;
        const uint16_t word =
            static_cast<uint16_t>(buffer[byte_index]) |
            static_cast<uint16_t>(
                static_cast<uint16_t>(buffer[byte_index + OS_KERNEL_ATA_HIGH_BYTE_OFFSET_BYTES])
                << OS_KERNEL_ATA_HIGH_BYTE_SHIFT_BITS);
        WritePort16(this->command_block_base_port_ + OS_KERNEL_ATA_DATA_PORT_OFFSET, word);
    }
}

AtaPioStatus AtaPioDevice::ResetController() noexcept {
    WritePort8(this->device_control_port_, OS_KERNEL_ATA_SOFTWARE_RESET);
    this->ApplyDeviceSelectDelay();
    WritePort8(this->device_control_port_, OS_KERNEL_ATA_ENABLE_DEVICE_INTERRUPTS);
    this->ApplyDeviceSelectDelay();
    ++this->software_reset_count_;
    return this->WaitUntilNotBusy();
}

AtaPioStatus AtaPioDevice::WaitUntilNotBusy() const noexcept {
    uint64_t remaining_poll_count = OS_KERNEL_ATA_STATUS_POLL_LIMIT;
    while (remaining_poll_count > OS_KERNEL_ATA_EMPTY_POLL_COUNT) {
        const uint8_t status =
            ReadPort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET);
        if ((status & OS_KERNEL_ATA_STATUS_BUSY_BIT) == OS_KERNEL_ATA_EMPTY_STATUS) {
            if ((status & OS_KERNEL_ATA_ERROR_STATUS_MASK) != OS_KERNEL_ATA_EMPTY_STATUS) {
                return AtaPioStatus::DeviceError;
            }
            return AtaPioStatus::Succeeded;
        }
        --remaining_poll_count;
    }
    return AtaPioStatus::BusyTimeout;
}

AtaPioStatus AtaPioDevice::WaitForDataRequest() const noexcept {
    uint64_t remaining_poll_count = OS_KERNEL_ATA_STATUS_POLL_LIMIT;
    while (remaining_poll_count > OS_KERNEL_ATA_EMPTY_POLL_COUNT) {
        const uint8_t status =
            ReadPort8(this->command_block_base_port_ + OS_KERNEL_ATA_COMMAND_STATUS_PORT_OFFSET);
        if ((status & OS_KERNEL_ATA_STATUS_BUSY_BIT) == OS_KERNEL_ATA_EMPTY_STATUS) {
            if ((status & OS_KERNEL_ATA_ERROR_STATUS_MASK) != OS_KERNEL_ATA_EMPTY_STATUS) {
                return AtaPioStatus::DeviceError;
            }
            if ((status & OS_KERNEL_ATA_STATUS_DATA_REQUEST_BIT) != OS_KERNEL_ATA_EMPTY_STATUS) {
                return AtaPioStatus::Succeeded;
            }
        }
        --remaining_poll_count;
    }
    return AtaPioStatus::DataRequestTimeout;
}

void AtaPioDevice::ApplyDeviceSelectDelay() const noexcept {
    for (uint64_t read_count = OS_KERNEL_ATA_EMPTY_POLL_COUNT;
         read_count < OS_KERNEL_ATA_DEVICE_SELECT_DELAY_READ_COUNT; ++read_count) {
        static_cast<void>(ReadPort8(this->device_control_port_));
    }
}

}
