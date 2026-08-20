#pragma once

#include <os/kernel/device/block_request.hpp>
#include <os/kernel/fs/block_cache.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint16_t OS_KERNEL_ATA_PRIMARY_COMMAND_BLOCK_BASE_PORT = 0x01F0U;
inline constexpr uint16_t OS_KERNEL_ATA_PRIMARY_DEVICE_CONTROL_PORT = 0x03F6U;
inline constexpr uint16_t OS_KERNEL_ATA_SECONDARY_COMMAND_BLOCK_BASE_PORT = 0x0170U;
inline constexpr uint16_t OS_KERNEL_ATA_SECONDARY_DEVICE_CONTROL_PORT = 0x0376U;
inline constexpr uint8_t OS_KERNEL_ATA_MASTER_LBA_BASE = 0xE0U;

enum class AtaPioChannel : uint64_t {
    Primary,
    Secondary,
};

enum class AtaPioStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidLogicalBlockAddress,
    BusyTimeout,
    DataRequestTimeout,
    DeviceError,
    NotInitialized,
    AlreadyInitialized,
    RequestQueueFailure,
    RequestInProgress,
};

struct AtaPioCompletion final {
    uint64_t request_identifier;
    uint64_t owner_thread_index;
    BlockOperation operation;
    BlockRequestResult result;
    bool ready;
};

struct AtaPioStatistics final {
    BlockRequestQueueStatistics request_queue;
    uint64_t interrupt_count;
    uint64_t spurious_interrupt_count;
    uint64_t timeout_recovery_count;
    uint64_t timeout_recovery_failure_count;
    uint64_t software_reset_count;
    uint64_t read_completion_count;
    uint64_t write_completion_count;
    uint64_t flush_completion_count;
};

class AtaPioDevice final : public FileSystemBlockDevice {
  public:
    constexpr explicit AtaPioDevice(const AtaPioChannel channel = AtaPioChannel::Primary) noexcept
        : command_block_base_port_(channel == AtaPioChannel::Primary
                                       ? OS_KERNEL_ATA_PRIMARY_COMMAND_BLOCK_BASE_PORT
                                       : OS_KERNEL_ATA_SECONDARY_COMMAND_BLOCK_BASE_PORT),
          device_control_port_(channel == AtaPioChannel::Primary
                                   ? OS_KERNEL_ATA_PRIMARY_DEVICE_CONTROL_PORT
                                   : OS_KERNEL_ATA_SECONDARY_DEVICE_CONTROL_PORT) {}

    [[nodiscard]] AtaPioStatus ReadSector(uint64_t logical_block_address, uint8_t *buffer,
                                          uint64_t buffer_size_bytes) noexcept;
    [[nodiscard]] AtaPioStatus WriteSector(uint64_t logical_block_address, const uint8_t *buffer,
                                           uint64_t buffer_size_bytes) noexcept;
    [[nodiscard]] AtaPioStatus FlushCache() noexcept;
    [[nodiscard]] AtaPioStatus InitializeAsynchronousRequests(BlockRequest *request_storage,
                                                              uint64_t request_capacity) noexcept;
    [[nodiscard]] AtaPioStatus
    SubmitAsynchronous(BlockOperation operation, uint64_t logical_block_address, uint8_t *buffer,
                       uint64_t buffer_size_bytes, uint64_t owner_thread_index,
                       uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept;
    [[nodiscard]] AtaPioStatus StartNextAsynchronous(AtaPioCompletion &completion,
                                                     bool &request_started) noexcept;
    [[nodiscard]] AtaPioStatus HandleInterrupt(AtaPioCompletion &completion) noexcept;
    [[nodiscard]] AtaPioStatus ResolveTimeout(uint64_t now_nanoseconds,
                                              AtaPioCompletion &completion) noexcept;
    [[nodiscard]] AtaPioStatistics Statistics() const noexcept;

    [[nodiscard]] FileSystemBlockDeviceStatus
    ReadBlock(uint64_t logical_block_address, uint8_t *block,
              uint64_t block_size_bytes) noexcept override;
    [[nodiscard]] FileSystemBlockDeviceStatus
    WriteBlock(uint64_t logical_block_address, const uint8_t *block,
               uint64_t block_size_bytes) noexcept override;
    [[nodiscard]] FileSystemBlockDeviceStatus Flush() noexcept override;

  private:
    [[nodiscard]] AtaPioStatus PrepareSectorRequest(uint64_t logical_block_address,
                                                    uint8_t command) noexcept;
    [[nodiscard]] AtaPioStatus PrepareAsynchronousRequest(const BlockRequest &request) noexcept;
    [[nodiscard]] AtaPioStatus ResolveIssuedRequest(BlockRequestResult result,
                                                    AtaPioCompletion &completion) noexcept;
    [[nodiscard]] bool HasOutstandingRequest() const noexcept;
    void EnterPollingMode() noexcept;
    void RestoreInterruptMode() noexcept;
    void TransferReadSector(uint8_t *buffer) noexcept;
    void TransferWriteSector(const uint8_t *buffer) noexcept;
    [[nodiscard]] AtaPioStatus ResetController() noexcept;
    [[nodiscard]] AtaPioStatus WaitUntilNotBusy() const noexcept;
    [[nodiscard]] AtaPioStatus WaitForDataRequest() const noexcept;
    void ApplyDeviceSelectDelay() const noexcept;

    BlockRequestQueue request_queue_{};
    BlockRequest active_request_{};
    bool asynchronous_initialized_{};
    bool controller_available_{true};
    bool write_data_transferred_{};
    uint64_t interrupt_count_{};
    uint64_t spurious_interrupt_count_{};
    uint64_t timeout_recovery_count_{};
    uint64_t timeout_recovery_failure_count_{};
    uint64_t software_reset_count_{};
    uint64_t read_completion_count_{};
    uint64_t write_completion_count_{};
    uint64_t flush_completion_count_{};
    uint16_t command_block_base_port_{};
    uint16_t device_control_port_{};
};

}
