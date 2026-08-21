#pragma once

#include <os/kernel/device/nvme_model.hpp>
#include <os/kernel/device/pci.hpp>
#include <os/kernel/memory/memory_manager.hpp>

#include <stdint.h>

namespace os::kernel {

using NvmeTimeOperation = uint64_t (*)() noexcept;

enum class NvmeStatus : uint64_t {
    Succeeded,
    NotFound,
    MultipleControllers,
    AlreadyInitialized,
    SecondaryNamespaceUnavailable,
    PciAccessFailed,
    BarAssignmentFailed,
    MmioMappingFailed,
    InvalidCapabilities,
    UnsupportedVersion,
    DmaAllocationFailed,
    ControllerFatal,
    ControllerDisableTimedOut,
    ControllerEnableTimedOut,
    AdminQueueConfigurationFailed,
    CommandBuildFailed,
    CommandTimedOut,
    CommandCompletionFailed,
    ControllerIdentifyInvalid,
    NamespaceIdentifyInvalid,
    MsixCapabilityInvalid,
    MsixBarAssignmentFailed,
    MsixMappingFailed,
    MsixConfigurationFailed,
    IoQueueConfigurationFailed,
    IoNotReady,
    InvalidIoRequest,
    IoRequestUnavailable,
    IoRequestNotFound,
    IoSelfTestFailed,
    DataVerificationFailed,
    ControllerResetFailed,
    ResourceReleaseFailed,
    ResourceLeak,
};

struct NvmeProbeResult final {
    PciNvmeController pci_controller;
    uint64_t bar_physical_address;
    uint64_t bar_size_bytes;
    uint32_t version;
    NvmeControllerCapabilities capabilities;
    NvmeControllerIdentity controller_identity;
    NvmeNamespaceIdentity namespace_identity;
    BlockDeviceGeometry geometry;
    uint64_t io_queue_depth;
    uint64_t io_test_logical_block_address;
    uint64_t io_test_logical_block_count;
    uint64_t io_test_checksum;
    uint64_t msix_table_entry_count;
    uint64_t msix_table_bar_index;
    uint64_t io_test_request_count;
    uint64_t io_test_transfer_page_count;
    uint64_t peak_outstanding_request_count;
    uint64_t msix_interrupt_count;
    uint64_t controller_reset_count;
    uint64_t error_completion_count;
    uint64_t command_timeout_count;
    bool resources_reclaimed;
};

enum class NvmeIoRequestState : uint64_t {
    Free,
    Preparing,
    Submitted,
    Completed,
};

struct NvmeIoRequestSlot final {
    PhysicalFrame data_frames[OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT];
    uint8_t *data_pages[OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT];
    PhysicalFrame prp_list_frame;
    uint64_t *prp_list_entries;
    uint64_t request_identifier;
    uint64_t logical_block_address;
    uint64_t logical_block_count;
    uint64_t transfer_size_bytes;
    uint64_t deadline_nanoseconds;
    uint32_t namespace_identifier;
    NvmeIoOperation operation;
    NvmeStatus completion_status;
    NvmeIoRequestState state;
};

class NvmeController;

class NvmeNamespaceDevice final : public BlockDeviceAdapter<NvmeNamespaceDevice> {
  public:
    constexpr NvmeNamespaceDevice() noexcept = default;
    NvmeNamespaceDevice(const NvmeNamespaceDevice &) = delete;
    NvmeNamespaceDevice &operator=(const NvmeNamespaceDevice &) = delete;

    [[nodiscard]] NvmeStatus Initialize(NvmeController &controller,
                                        uint32_t namespace_identifier,
                                        const BlockDeviceGeometry &geometry) noexcept;
    [[nodiscard]] BlockDeviceStatus ReadBlock(uint64_t logical_block_address,
                                              uint8_t *block,
                                              uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus WriteBlock(uint64_t logical_block_address,
                                               const uint8_t *block,
                                               uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus Flush() noexcept;
    [[nodiscard]] const BlockDeviceGeometry &Geometry() const noexcept;

  private:
    NvmeController *controller_{};
    uint32_t namespace_identifier_{};
    BlockDeviceGeometry geometry_{};
    bool initialized_{};
};

class NvmeController final : public BlockDeviceAdapter<NvmeController> {
  public:
    constexpr NvmeController() noexcept = default;
    NvmeController(const NvmeController &) = delete;
    NvmeController &operator=(const NvmeController &) = delete;

    [[nodiscard]] NvmeStatus Initialize(PciConfigurationSpace &configuration,
                                        const PciNvmeController &pci_controller,
                                        NvmeTimeOperation time_operation) noexcept;
    [[nodiscard]] NvmeStatus Identify(NvmeProbeResult &result) noexcept;
    [[nodiscard]] NvmeStatus IdentifyNamespace(
        uint32_t namespace_identifier,
        const NvmeControllerIdentity &controller_identity,
        NvmeNamespaceIdentity &namespace_identity,
        BlockDeviceGeometry &geometry) noexcept;
    [[nodiscard]] NvmeStatus ConfigureMsix(NvmeProbeResult &result) noexcept;
    [[nodiscard]] NvmeStatus ConfigureIoQueues(NvmeProbeResult &result) noexcept;
    [[nodiscard]] NvmeStatus RunIoSelfTest(NvmeProbeResult &result) noexcept;
    [[nodiscard]] NvmeStatus HandleIoInterrupt() noexcept;
    void PopulateRuntimeStatistics(NvmeProbeResult &result) const noexcept;
    [[nodiscard]] NvmeStatus Shutdown() noexcept;
    [[nodiscard]] BlockDeviceStatus ReadBlock(uint64_t logical_block_address,
                                              uint8_t *block,
                                              uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus WriteBlock(uint64_t logical_block_address,
                                               const uint8_t *block,
                                               uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus Flush() noexcept;
    [[nodiscard]] const BlockDeviceGeometry &Geometry() const noexcept;
    [[nodiscard]] BlockDeviceStatus ReadNamespaceBlocks(
        uint32_t namespace_identifier, const BlockDeviceGeometry &geometry,
        uint64_t logical_block_address, uint8_t *block,
        uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus WriteNamespaceBlocks(
        uint32_t namespace_identifier, const BlockDeviceGeometry &geometry,
        uint64_t logical_block_address, const uint8_t *block,
        uint64_t block_size_bytes) noexcept;
    [[nodiscard]] BlockDeviceStatus FlushNamespace(uint32_t namespace_identifier) noexcept;

  private:
    [[nodiscard]] NvmeStatus AllocateDmaPages() noexcept;
    [[nodiscard]] NvmeStatus ConfigureAdminQueues() noexcept;
    [[nodiscard]] NvmeStatus WaitForReady(bool expected_ready) noexcept;
    [[nodiscard]] NvmeStatus SubmitIdentify(uint16_t command_identifier,
                                            uint32_t namespace_identifier,
                                            uint8_t selector) noexcept;
    [[nodiscard]] NvmeStatus SubmitAdminCommand(
        const NvmeSubmissionEntry &entry, uint16_t command_identifier,
        NvmeCompletionResult &completion_result) noexcept;
    [[nodiscard]] NvmeStatus SubmitIoRequest(
        NvmeIoOperation operation, uint64_t logical_block_address,
        uint64_t logical_block_count, uint32_t namespace_identifier,
        const BlockDeviceGeometry &geometry, const uint8_t *write_buffer,
        uint64_t transfer_size_bytes, uint64_t &request_identifier) noexcept;
    [[nodiscard]] NvmeStatus WaitForIoRequest(uint64_t request_identifier,
                                              uint8_t *read_buffer,
                                              uint64_t transfer_size_bytes) noexcept;
    [[nodiscard]] NvmeStatus DrainIoCompletions() noexcept;
    [[nodiscard]] NvmeStatus ResetController() noexcept;
    [[nodiscard]] NvmeStatus SubmitQueueCommand(
        NvmeSubmissionEntry *submission_queue,
        volatile NvmeCompletionEntry *completion_queue, uint64_t queue_depth,
        uint16_t queue_identifier, uint64_t &submission_tail,
        uint64_t &completion_head, bool &completion_phase,
        const NvmeSubmissionEntry &entry, uint16_t command_identifier,
        NvmeCompletionResult &completion_result) noexcept;
    [[nodiscard]] BlockDeviceStatus ValidateIoTransfer(
        const BlockDeviceGeometry &geometry, uint64_t logical_block_address, const uint8_t *block,
        uint64_t block_size_bytes, uint64_t &logical_block_count) const noexcept;
    [[nodiscard]] uint16_t TakeIoCommandIdentifier() noexcept;
    [[nodiscard]] NvmeIoRequestSlot *FindIoRequestSlot(uint64_t request_identifier) noexcept;
    [[nodiscard]] NvmeIoRequestSlot *FindFreeIoRequestSlot() noexcept;
    [[nodiscard]] NvmeStatus PrepareIoRequestPrps(NvmeIoRequestSlot &slot,
                                                  uint64_t transfer_size_bytes,
                                                  NvmePrpMapping &mapping) noexcept;
    void CopyToIoRequest(NvmeIoRequestSlot &slot, const uint8_t *source,
                         uint64_t byte_count) noexcept;
    void CopyFromIoRequest(const NvmeIoRequestSlot &slot, uint8_t *destination,
                           uint64_t byte_count) noexcept;
    [[nodiscard]] NvmeStatus MaskMsix() noexcept;
    [[nodiscard]] NvmeStatus UnmaskMsix() noexcept;
    [[nodiscard]] NvmeStatus RestoreMsix() noexcept;
    [[nodiscard]] NvmeStatus ReleaseDmaPages() noexcept;
    [[nodiscard]] uint32_t ReadRegister32(uint64_t offset_bytes) const noexcept;
    [[nodiscard]] uint64_t ReadRegister64(uint64_t offset_bytes) const noexcept;
    void WriteRegister32(uint64_t offset_bytes, uint32_t value) noexcept;
    void WriteRegister64(uint64_t offset_bytes, uint64_t value) noexcept;
    void ClearPage(uint8_t *page) noexcept;

    PciConfigurationSpace *configuration_{};
    PciNvmeController pci_controller_{};
    PciMemoryBarAssignment bar_assignment_{};
    PciMemoryBarAssignment msix_bar_assignment_{};
    KernelMmioMapping mmio_mapping_{};
    KernelMmioMapping msix_mmio_mapping_{};
    PciMsixCapabilityLocation msix_location_{};
    NvmeControllerCapabilities capabilities_{};
    PhysicalFrame admin_submission_frame_{};
    PhysicalFrame admin_completion_frame_{};
    PhysicalFrame identify_data_frame_{};
    PhysicalFrame io_submission_frame_{};
    PhysicalFrame io_completion_frame_{};
    NvmeSubmissionEntry *admin_submission_queue_{};
    volatile NvmeCompletionEntry *admin_completion_queue_{};
    uint8_t *identify_data_{};
    NvmeSubmissionEntry *io_submission_queue_{};
    volatile NvmeCompletionEntry *io_completion_queue_{};
    NvmeIoRequestSlot io_request_slots_[OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT]{};
    volatile PciMsixTableEntry *msix_table_entry_{};
    NvmeTimeOperation time_operation_{};
    uint64_t submission_tail_{};
    uint64_t completion_head_{};
    bool completion_phase_{true};
    uint64_t io_queue_depth_{};
    uint64_t io_submission_tail_{};
    uint64_t io_completion_head_{};
    bool io_completion_phase_{true};
    uint16_t next_io_command_identifier_{OS_KERNEL_NVME_FIRST_IO_COMMAND_IDENTIFIER};
    BlockDeviceGeometry geometry_{};
    volatile uint64_t outstanding_request_count_{};
    volatile uint64_t peak_outstanding_request_count_{};
    volatile uint64_t msix_interrupt_count_{};
    volatile uint64_t controller_reset_count_{};
    volatile uint64_t error_completion_count_{};
    volatile uint64_t command_timeout_count_{};
    uint32_t version_{};
    bool initialized_{};
    bool io_ready_{};
    bool msix_ready_{};
    bool reset_in_progress_{};
};

struct NvmeStorageRuntimeResult final {
    NvmeProbeResult controller;
    NvmeNamespaceIdentity root_namespace_identity;
    NvmeNamespaceIdentity swap_namespace_identity;
    BlockDeviceGeometry root_geometry;
    BlockDeviceGeometry swap_geometry;
    bool active;
    bool resources_reclaimed;
};

[[nodiscard]] NvmeStatus InitializeNvmeStorageRuntime(
    NvmeTimeOperation time_operation, NvmeStorageRuntimeResult &result,
    BlockDevice *&root_device, BlockDevice *&swap_device) noexcept;
[[nodiscard]] NvmeStatus ShutdownNvmeStorageRuntime(
    NvmeStorageRuntimeResult &result) noexcept;

[[nodiscard]] NvmeStatus ProbeNvmeController(NvmeTimeOperation time_operation,
                                             NvmeProbeResult &result) noexcept;
[[nodiscard]] bool DispatchNvmeMsixInterrupt() noexcept;
void ArmNvmeCommandTimeoutInjection() noexcept;

}
