#include <os/kernel/device/nvme.hpp>

#include <os/kernel/arch/interrupt_runtime.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_NVME_PCI_BAR_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_NVME_PCI_WINDOW_BEGIN_ADDRESS = 0x00000000F8000000ULL;
constexpr uint64_t OS_KERNEL_NVME_PCI_WINDOW_END_ADDRESS = 0x00000000F9000000ULL;
constexpr uint64_t OS_KERNEL_NVME_MSIX_WINDOW_BEGIN_ADDRESS = 0x00000000F9000000ULL;
constexpr uint64_t OS_KERNEL_NVME_MSIX_WINDOW_END_ADDRESS = 0x00000000FA000000ULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_OFFSET_BYTES = 0x00ULL;
constexpr uint64_t OS_KERNEL_NVME_VERSION_OFFSET_BYTES = 0x08ULL;
constexpr uint64_t OS_KERNEL_NVME_CONTROLLER_CONFIGURATION_OFFSET_BYTES = 0x14ULL;
constexpr uint64_t OS_KERNEL_NVME_CONTROLLER_STATUS_OFFSET_BYTES = 0x1CULL;
constexpr uint64_t OS_KERNEL_NVME_ADMIN_QUEUE_ATTRIBUTES_OFFSET_BYTES = 0x24ULL;
constexpr uint64_t OS_KERNEL_NVME_ADMIN_SUBMISSION_BASE_OFFSET_BYTES = 0x28ULL;
constexpr uint64_t OS_KERNEL_NVME_ADMIN_COMPLETION_BASE_OFFSET_BYTES = 0x30ULL;
constexpr uint64_t OS_KERNEL_NVME_DOORBELL_BASE_OFFSET_BYTES = 0x1000ULL;
constexpr uint16_t OS_KERNEL_NVME_ADMIN_QUEUE_IDENTIFIER = 0U;
constexpr uint16_t OS_KERNEL_NVME_IO_QUEUE_IDENTIFIER = 1U;
constexpr uint64_t OS_KERNEL_NVME_IO_QUEUE_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_NVME_SUBMISSION_DOORBELL_MULTIPLIER = 2ULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_DOORBELL_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_NVME_LAST_USED_DOORBELL_INDEX = 3ULL;
constexpr uint32_t OS_KERNEL_NVME_CONTROLLER_ENABLE_BIT = 1U << 0U;
constexpr uint32_t OS_KERNEL_NVME_CONTROLLER_STATUS_READY_BIT = 1U << 0U;
constexpr uint32_t OS_KERNEL_NVME_CONTROLLER_STATUS_FATAL_BIT = 1U << 1U;
constexpr uint64_t OS_KERNEL_NVME_NANOSECONDS_PER_MILLISECOND = 1000000ULL;
constexpr uint32_t OS_KERNEL_NVME_MINIMUM_MAJOR_VERSION = 1U;
constexpr uint64_t OS_KERNEL_NVME_MAJOR_VERSION_SHIFT_BITS = 16ULL;
constexpr uint16_t OS_KERNEL_NVME_IDENTIFY_CONTROLLER_COMMAND_IDENTIFIER = 1U;
constexpr uint16_t OS_KERNEL_NVME_IDENTIFY_NAMESPACE_COMMAND_IDENTIFIER = 2U;
constexpr uint16_t OS_KERNEL_NVME_SET_QUEUE_COUNT_COMMAND_IDENTIFIER = 3U;
constexpr uint16_t OS_KERNEL_NVME_CREATE_IO_COMPLETION_COMMAND_IDENTIFIER = 4U;
constexpr uint16_t OS_KERNEL_NVME_CREATE_IO_SUBMISSION_COMMAND_IDENTIFIER = 5U;
constexpr uint32_t OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER = 1U;
constexpr uint32_t OS_KERNEL_NVME_SECONDARY_NAMESPACE_IDENTIFIER = 2U;
constexpr uint64_t OS_KERNEL_NVME_DWORD_BIT_COUNT = 32ULL;
constexpr uint64_t OS_KERNEL_NVME_REGISTER_DWORD_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_DWORD_COUNT = 4ULL;
constexpr uint16_t OS_KERNEL_NVME_LAST_IO_COMMAND_IDENTIFIER = UINT16_MAX - 1U;
constexpr uint16_t OS_KERNEL_NVME_MSIX_ENABLE_BIT = 1U << 15U;
constexpr uint16_t OS_KERNEL_NVME_MSIX_FUNCTION_MASK_BIT = 1U << 14U;
constexpr uint32_t OS_KERNEL_NVME_MSIX_VECTOR_MASK_BIT = 1U << 0U;
constexpr uint64_t OS_KERNEL_NVME_MSIX_TABLE_ENTRY_SIZE_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_NVME_MSIX_VECTOR_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_NVME_SUPPORTED_MSIX_BAR_ZERO = 0ULL;
constexpr uint64_t OS_KERNEL_NVME_SUPPORTED_MSIX_BAR_FOUR = 4ULL;
constexpr uint64_t OS_KERNEL_NVME_PRP_LIST_ENTRY_CAPACITY =
    OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES / 8ULL;
constexpr uint64_t OS_KERNEL_NVME_MAXIMUM_TRANSFER_SIZE_BYTES =
    OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES * OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT;
constexpr uint64_t OS_KERNEL_NVME_TEST_REQUEST_COUNT =
    OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT;
constexpr uint64_t OS_KERNEL_NVME_TEST_PATTERN_MULTIPLIER = 37ULL;
constexpr uint64_t OS_KERNEL_NVME_TEST_PATTERN_INCREMENT = 11ULL;
constexpr uint64_t OS_KERNEL_NVME_TEST_PATTERN_BYTE_MASK = 0xFFULL;
constexpr uint64_t OS_KERNEL_NVME_TEST_CHECKSUM_INITIAL_VALUE = 1469598103934665603ULL;
constexpr uint64_t OS_KERNEL_NVME_TEST_CHECKSUM_MULTIPLIER = 1099511628211ULL;

alignas(OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES)
uint8_t nvme_io_write_test_buffers[OS_KERNEL_NVME_TEST_REQUEST_COUNT]
                                  [OS_KERNEL_NVME_MAXIMUM_TRANSFER_SIZE_BYTES];

NvmeController *volatile active_nvme_controller;
bool nvme_command_timeout_injection_armed;

struct KernelNvmeStorageRuntime final {
    PciConfigurationSpace configuration;
    NvmeController controller;
    NvmeNamespaceDevice root_device;
    NvmeNamespaceDevice swap_device;
    PhysicalFrameAllocatorStatistics frames_before;
    KernelVirtualAddressAllocatorStatistics kva_before;
    bool active;
};

constinit KernelNvmeStorageRuntime kernel_nvme_storage_runtime{};

[[nodiscard]] uint64_t DmaVirtualAddress(const PhysicalFrame frame) noexcept {
    return PhysicalMemoryDirectMapAddress(frame.physical_address);
}

[[nodiscard]] uint8_t NvmeTestPatternByte(const uint64_t byte_index,
                                          const uint64_t logical_block_address) noexcept {
    return static_cast<uint8_t>((byte_index * OS_KERNEL_NVME_TEST_PATTERN_MULTIPLIER +
                                 logical_block_address +
                                 OS_KERNEL_NVME_TEST_PATTERN_INCREMENT) &
                                OS_KERNEL_NVME_TEST_PATTERN_BYTE_MASK);
}

void DmaMemoryBarrier() noexcept { asm volatile("mfence" : : : "memory"); }

void ProcessorPause() noexcept { asm volatile("pause" : : : "memory"); }

[[nodiscard]] bool ReleaseOwnedFrame(PhysicalFrameAllocator &allocator,
                                     PhysicalFrame &frame) noexcept {
    if (frame.physical_address == 0ULL) {
        return true;
    }
    if (allocator.Release(frame) != PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    frame = PhysicalFrame{};
    return true;
}

[[nodiscard]] AsynchronousBlockDeviceStatus
MapNvmeStatusToAsynchronousStatus(const NvmeStatus status) noexcept {
    if (status == NvmeStatus::Succeeded) {
        return AsynchronousBlockDeviceStatus::Succeeded;
    }
    if (status == NvmeStatus::IoNotReady) {
        return AsynchronousBlockDeviceStatus::NotReady;
    }
    if (status == NvmeStatus::InvalidIoRequest || status == NvmeStatus::CommandBuildFailed) {
        return AsynchronousBlockDeviceStatus::InvalidRequest;
    }
    if (status == NvmeStatus::IoRequestUnavailable) {
        return AsynchronousBlockDeviceStatus::CapacityExhausted;
    }
    if (status == NvmeStatus::IoRequestNotFound) {
        return AsynchronousBlockDeviceStatus::RequestNotFound;
    }
    if (status == NvmeStatus::CommandTimedOut || status == NvmeStatus::CommandCompletionFailed ||
        status == NvmeStatus::ControllerResetFailed || status == NvmeStatus::ControllerFatal) {
        return AsynchronousBlockDeviceStatus::DeviceFailure;
    }
    return AsynchronousBlockDeviceStatus::Corrupt;
}

[[nodiscard]] bool MapBlockOperationToNvmeOperation(const BlockOperation operation,
                                                    NvmeIoOperation &nvme_operation) noexcept {
    if (operation == BlockOperation::Read) {
        nvme_operation = NvmeIoOperation::Read;
        return true;
    }
    if (operation == BlockOperation::Write) {
        nvme_operation = NvmeIoOperation::Write;
        return true;
    }
    if (operation == BlockOperation::Flush) {
        nvme_operation = NvmeIoOperation::Flush;
        return true;
    }
    return false;
}

[[nodiscard]] BlockOperation
MapNvmeOperationToBlockOperation(const NvmeIoOperation operation) noexcept {
    if (operation == NvmeIoOperation::Read) {
        return BlockOperation::Read;
    }
    if (operation == NvmeIoOperation::Write) {
        return BlockOperation::Write;
    }
    return BlockOperation::Flush;
}
}

NvmeStatus NvmeNamespaceDevice::Initialize(
    NvmeController &controller, const uint32_t namespace_identifier,
    const BlockDeviceGeometry &geometry) noexcept {
    if (this->initialized_) {
        return NvmeStatus::AlreadyInitialized;
    }
    if (namespace_identifier == 0U || geometry.logical_block_size_bytes == 0ULL ||
        geometry.logical_block_count == 0ULL ||
        geometry.maximum_transfer_block_count == 0ULL ||
        geometry.maximum_outstanding_request_count == 0ULL) {
        return NvmeStatus::NamespaceIdentifyInvalid;
    }
    this->controller_ = &controller;
    this->namespace_identifier_ = namespace_identifier;
    this->geometry_ = geometry;
    this->initialized_ = true;
    return NvmeStatus::Succeeded;
}

BlockDeviceStatus NvmeNamespaceDevice::ReadBlock(const uint64_t logical_block_address,
                                                 uint8_t *const block,
                                                 const uint64_t block_size_bytes) noexcept {
    return this->initialized_ && this->controller_ != nullptr
               ? this->controller_->ReadNamespaceBlocks(
                     this->namespace_identifier_, this->geometry_, logical_block_address,
                     block, block_size_bytes)
               : BlockDeviceStatus::ReadFailed;
}

BlockDeviceStatus NvmeNamespaceDevice::WriteBlock(const uint64_t logical_block_address,
                                                  const uint8_t *const block,
                                                  const uint64_t block_size_bytes) noexcept {
    return this->initialized_ && this->controller_ != nullptr
               ? this->controller_->WriteNamespaceBlocks(
                     this->namespace_identifier_, this->geometry_, logical_block_address,
                     block, block_size_bytes)
               : BlockDeviceStatus::WriteFailed;
}

BlockDeviceStatus NvmeNamespaceDevice::Flush() noexcept {
    return this->initialized_ && this->controller_ != nullptr
               ? this->controller_->FlushNamespace(this->namespace_identifier_)
               : BlockDeviceStatus::FlushFailed;
}

const BlockDeviceGeometry &NvmeNamespaceDevice::Geometry() const noexcept {
    return this->geometry_;
}

AsynchronousBlockDeviceStatus NvmeNamespaceDevice::SubmitBlockRequest(
    const BlockOperation operation, const uint64_t logical_block_address, uint8_t *const buffer,
    const uint64_t buffer_size_bytes, const uint64_t owner_thread_index,
    const uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept {
    request_identifier = 0ULL;
    return this->initialized_ && this->controller_ != nullptr
               ? this->controller_->SubmitNamespaceBlockRequest(
                     this->namespace_identifier_, this->geometry_, operation, logical_block_address,
                     buffer, buffer_size_bytes, owner_thread_index, deadline_nanoseconds,
                     request_identifier)
               : AsynchronousBlockDeviceStatus::NotReady;
}

AsynchronousBlockDeviceStatus
NvmeNamespaceDevice::CancelBlockRequest(const uint64_t request_identifier) noexcept {
    return this->initialized_ && this->controller_ != nullptr
               ? this->controller_->CancelNamespaceBlockRequest(this->namespace_identifier_,
                                                                request_identifier)
               : AsynchronousBlockDeviceStatus::NotReady;
}

AsynchronousBlockDeviceStatus
NvmeNamespaceDevice::ResolveBlockTimeouts(const uint64_t now_nanoseconds) noexcept {
    return this->initialized_ && this->controller_ != nullptr
               ? this->controller_->ResolveNamespaceBlockTimeouts(now_nanoseconds)
               : AsynchronousBlockDeviceStatus::NotReady;
}

AsynchronousBlockDeviceStatus NvmeNamespaceDevice::TakeBlockCompletion(BlockCompletion &completion,
                                                                       bool &available) noexcept {
    completion = BlockCompletion{};
    available = false;
    return this->initialized_ && this->controller_ != nullptr
               ? this->controller_->TakeNamespaceBlockCompletion(this->namespace_identifier_,
                                                                 completion, available)
               : AsynchronousBlockDeviceStatus::NotReady;
}

BlockDeviceGeometry NvmeNamespaceDevice::AsynchronousGeometry() const noexcept {
    return this->geometry_;
}

NvmeStatus NvmeController::Initialize(PciConfigurationSpace &configuration,
                                      const PciNvmeController &pci_controller,
                                      const NvmeTimeOperation time_operation) noexcept {
    if (this->initialized_ || time_operation == nullptr) {
        return NvmeStatus::InvalidCapabilities;
    }
    this->configuration_ = &configuration;
    this->pci_controller_ = pci_controller;
    this->time_operation_ = time_operation;
    if (AssignPciMemoryBar(configuration, pci_controller.address, OS_KERNEL_NVME_PCI_BAR_INDEX,
                           OS_KERNEL_NVME_PCI_WINDOW_BEGIN_ADDRESS,
                           OS_KERNEL_NVME_PCI_WINDOW_END_ADDRESS, this->bar_assignment_) !=
        PciMemoryBarAssignmentStatus::Succeeded) {
        return NvmeStatus::BarAssignmentFailed;
    }
    if (this->bar_assignment_.size_bytes < OS_KERNEL_NVME_DOORBELL_BASE_OFFSET_BYTES +
                                                OS_KERNEL_NVME_COMPLETION_ENTRY_SIZE_BYTES ||
        this->bar_assignment_.size_bytes % OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES != 0ULL ||
        MapKernelMmio(this->bar_assignment_.physical_address, this->bar_assignment_.size_bytes,
                      this->mmio_mapping_) != KernelMmioStatus::Succeeded) {
        static_cast<void>(RestorePciMemoryBar(configuration, this->bar_assignment_));
        return NvmeStatus::MmioMappingFailed;
    }
    const uint64_t capabilities_register = this->ReadRegister64(OS_KERNEL_NVME_CAP_OFFSET_BYTES);
    if (DecodeNvmeControllerCapabilities(capabilities_register, this->capabilities_) !=
        NvmeModelStatus::Succeeded) {
        static_cast<void>(this->Shutdown());
        return NvmeStatus::InvalidCapabilities;
    }
    const uint64_t last_doorbell_offset =
        OS_KERNEL_NVME_DOORBELL_BASE_OFFSET_BYTES +
        OS_KERNEL_NVME_LAST_USED_DOORBELL_INDEX * this->capabilities_.doorbell_stride_bytes;
    if (last_doorbell_offset >= this->bar_assignment_.size_bytes ||
        OS_KERNEL_NVME_REGISTER_DWORD_SIZE_BYTES >
            this->bar_assignment_.size_bytes - last_doorbell_offset) {
        static_cast<void>(this->Shutdown());
        return NvmeStatus::InvalidCapabilities;
    }
    this->version_ = this->ReadRegister32(OS_KERNEL_NVME_VERSION_OFFSET_BYTES);
    if ((this->version_ >> OS_KERNEL_NVME_MAJOR_VERSION_SHIFT_BITS) <
        OS_KERNEL_NVME_MINIMUM_MAJOR_VERSION) {
        static_cast<void>(this->Shutdown());
        return NvmeStatus::UnsupportedVersion;
    }
    uint32_t controller_configuration =
        this->ReadRegister32(OS_KERNEL_NVME_CONTROLLER_CONFIGURATION_OFFSET_BYTES);
    if ((controller_configuration & OS_KERNEL_NVME_CONTROLLER_ENABLE_BIT) != 0U) {
        controller_configuration &= ~OS_KERNEL_NVME_CONTROLLER_ENABLE_BIT;
        this->WriteRegister32(OS_KERNEL_NVME_CONTROLLER_CONFIGURATION_OFFSET_BYTES,
                              controller_configuration);
        const NvmeStatus disable_status = this->WaitForReady(false);
        if (disable_status != NvmeStatus::Succeeded) {
            static_cast<void>(this->Shutdown());
            return disable_status;
        }
    }
    NvmeProbeResult ignored_result{};
    const NvmeStatus msix_status = this->ConfigureMsix(ignored_result);
    if (msix_status != NvmeStatus::Succeeded) {
        static_cast<void>(this->Shutdown());
        return msix_status;
    }
    const NvmeStatus dma_status = this->AllocateDmaPages();
    if (dma_status != NvmeStatus::Succeeded) {
        static_cast<void>(this->Shutdown());
        return dma_status;
    }
    const NvmeStatus queue_status = this->ConfigureAdminQueues();
    if (queue_status != NvmeStatus::Succeeded) {
        static_cast<void>(this->Shutdown());
        return queue_status;
    }
    this->initialized_ = true;
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::Identify(NvmeProbeResult &result) noexcept {
    if (!this->initialized_) {
        return NvmeStatus::InvalidCapabilities;
    }
    NvmeStatus status = this->SubmitIdentify(
        OS_KERNEL_NVME_IDENTIFY_CONTROLLER_COMMAND_IDENTIFIER, 0U,
        OS_KERNEL_NVME_IDENTIFY_CONTROLLER_CNS);
    if (status != NvmeStatus::Succeeded ||
        ParseNvmeControllerIdentity(this->identify_data_, OS_KERNEL_NVME_IDENTIFY_DATA_SIZE_BYTES,
                                    this->capabilities_, result.controller_identity) !=
            NvmeModelStatus::Succeeded ||
        result.controller_identity.vendor_identifier !=
            this->pci_controller_.identity.vendor_identifier) {
        return status == NvmeStatus::Succeeded ? NvmeStatus::ControllerIdentifyInvalid : status;
    }
    status = this->IdentifyNamespace(OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER,
                                     result.controller_identity, result.namespace_identity,
                                     this->geometry_);
    if (status != NvmeStatus::Succeeded) {
        return status;
    }
    result.pci_controller = this->pci_controller_;
    result.bar_physical_address = this->bar_assignment_.physical_address;
    result.bar_size_bytes = this->bar_assignment_.size_bytes;
    result.version = this->version_;
    result.capabilities = this->capabilities_;
    result.geometry = this->geometry_;
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::IdentifyNamespace(
    const uint32_t namespace_identifier,
    const NvmeControllerIdentity &controller_identity,
    NvmeNamespaceIdentity &namespace_identity,
    BlockDeviceGeometry &geometry) noexcept {
    if (!this->initialized_ || namespace_identifier == 0U) {
        return NvmeStatus::NamespaceIdentifyInvalid;
    }
    const uint16_t command_identifier =
        namespace_identifier == OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER
            ? OS_KERNEL_NVME_IDENTIFY_NAMESPACE_COMMAND_IDENTIFIER
            : static_cast<uint16_t>(OS_KERNEL_NVME_IDENTIFY_NAMESPACE_COMMAND_IDENTIFIER +
                                    namespace_identifier -
                                    OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER);
    const NvmeStatus status = this->SubmitIdentify(
        command_identifier, namespace_identifier, OS_KERNEL_NVME_IDENTIFY_NAMESPACE_CNS);
    if (status != NvmeStatus::Succeeded) {
        return status;
    }
    return ParseNvmeNamespaceIdentity(this->identify_data_,
                                      OS_KERNEL_NVME_IDENTIFY_DATA_SIZE_BYTES,
                                      namespace_identity) == NvmeModelStatus::Succeeded &&
                   CalculateNvmeBlockDeviceGeometry(controller_identity, namespace_identity,
                                                    geometry) == NvmeModelStatus::Succeeded
               ? NvmeStatus::Succeeded
               : NvmeStatus::NamespaceIdentifyInvalid;
}

NvmeStatus NvmeController::ConfigureMsix(NvmeProbeResult &result) noexcept {
    if (this->configuration_ == nullptr || this->msix_ready_) {
        return NvmeStatus::MsixConfigurationFailed;
    }
    if (FindPciMsixCapability(*this->configuration_, this->pci_controller_.address,
                              this->msix_location_) != PciMsixAccessStatus::Succeeded ||
        this->msix_location_.capability.table_entry_count == 0ULL) {
        return NvmeStatus::MsixCapabilityInvalid;
    }
    uint64_t table_mapping_base = 0ULL;
    uint64_t table_mapping_size_bytes = 0ULL;
    const uint64_t table_bar_index = this->msix_location_.capability.table_bar_index;
    if (table_bar_index == OS_KERNEL_NVME_SUPPORTED_MSIX_BAR_ZERO) {
        table_mapping_base = this->mmio_mapping_.virtual_address;
        table_mapping_size_bytes = this->bar_assignment_.size_bytes;
    } else if (table_bar_index == OS_KERNEL_NVME_SUPPORTED_MSIX_BAR_FOUR) {
        if (AssignPciMemoryBar(*this->configuration_, this->pci_controller_.address,
                               table_bar_index, OS_KERNEL_NVME_MSIX_WINDOW_BEGIN_ADDRESS,
                               OS_KERNEL_NVME_MSIX_WINDOW_END_ADDRESS,
                               this->msix_bar_assignment_) !=
            PciMemoryBarAssignmentStatus::Succeeded) {
            return NvmeStatus::MsixBarAssignmentFailed;
        }
        if (MapKernelMmio(this->msix_bar_assignment_.physical_address,
                          this->msix_bar_assignment_.size_bytes,
                          this->msix_mmio_mapping_) != KernelMmioStatus::Succeeded) {
            static_cast<void>(RestorePciMemoryBar(*this->configuration_,
                                                  this->msix_bar_assignment_));
            return NvmeStatus::MsixMappingFailed;
        }
        table_mapping_base = this->msix_mmio_mapping_.virtual_address;
        table_mapping_size_bytes = this->msix_bar_assignment_.size_bytes;
    } else {
        return NvmeStatus::MsixCapabilityInvalid;
    }
    const uint64_t table_offset_bytes = this->msix_location_.capability.table_offset_bytes;
    if (table_offset_bytes >= table_mapping_size_bytes ||
        OS_KERNEL_NVME_MSIX_TABLE_ENTRY_SIZE_BYTES >
            table_mapping_size_bytes - table_offset_bytes) {
        if (this->msix_mmio_mapping_.active) {
            static_cast<void>(UnmapKernelMmio(this->msix_mmio_mapping_));
        }
        if (this->msix_bar_assignment_.active) {
            static_cast<void>(RestorePciMemoryBar(*this->configuration_,
                                                  this->msix_bar_assignment_));
        }
        return NvmeStatus::MsixCapabilityInvalid;
    }
    this->msix_table_entry_ = reinterpret_cast<volatile PciMsixTableEntry *>(
        table_mapping_base + table_offset_bytes);
    this->msix_ready_ = true;
    PciMsixTableEntry entry{};
    if (BuildPciMsixTableEntry(LocalApicIdentifier(), OS_KERNEL_INTERRUPT_NVME_MSIX_VECTOR,
                               true, entry) != PciCapabilityStatus::Succeeded) {
        return NvmeStatus::MsixConfigurationFailed;
    }
    const uint16_t masked_control = static_cast<uint16_t>(
        this->msix_location_.original_message_control | OS_KERNEL_NVME_MSIX_ENABLE_BIT |
        OS_KERNEL_NVME_MSIX_FUNCTION_MASK_BIT);
    if (WritePciMsixMessageControl(*this->configuration_, this->pci_controller_.address,
                                   this->msix_location_.capability_offset_bytes,
                                   masked_control) != PciMsixAccessStatus::Succeeded) {
        return NvmeStatus::MsixConfigurationFailed;
    }
    this->msix_table_entry_->vector_control = OS_KERNEL_NVME_MSIX_VECTOR_MASK_BIT;
    DmaMemoryBarrier();
    this->msix_table_entry_->message_address_low = entry.message_address_low;
    this->msix_table_entry_->message_address_high = entry.message_address_high;
    this->msix_table_entry_->message_data = entry.message_data;
    DmaMemoryBarrier();
    active_nvme_controller = this;
    result.msix_interrupt_count = this->msix_interrupt_count_;
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::ConfigureIoQueues(NvmeProbeResult &result) noexcept {
    if (!this->initialized_ || !this->msix_ready_ || this->io_ready_ ||
        this->geometry_.logical_block_size_bytes == 0ULL ||
        this->geometry_.logical_block_count == 0ULL ||
        this->geometry_.maximum_transfer_block_count == 0ULL) {
        return NvmeStatus::IoQueueConfigurationFailed;
    }
    if (SelectNvmeIoQueueDepth(this->capabilities_, this->io_queue_depth_) !=
        NvmeModelStatus::Succeeded) {
        return NvmeStatus::IoQueueConfigurationFailed;
    }
    NvmeSubmissionEntry command{};
    NvmeCompletionResult completion{};
    if (BuildNvmeSetQueueCountCommand(OS_KERNEL_NVME_SET_QUEUE_COUNT_COMMAND_IDENTIFIER,
                                      OS_KERNEL_NVME_IO_QUEUE_COUNT,
                                      OS_KERNEL_NVME_IO_QUEUE_COUNT,
                                      command) != NvmeModelStatus::Succeeded ||
        this->SubmitAdminCommand(command, OS_KERNEL_NVME_SET_QUEUE_COUNT_COMMAND_IDENTIFIER,
                                 completion) != NvmeStatus::Succeeded) {
        return NvmeStatus::IoQueueConfigurationFailed;
    }
    NvmeAllocatedQueueCounts allocated_queue_counts{};
    if (DecodeNvmeAllocatedQueueCounts(completion, allocated_queue_counts) !=
            NvmeModelStatus::Succeeded ||
        allocated_queue_counts.submission_queue_count < OS_KERNEL_NVME_IO_QUEUE_COUNT ||
        allocated_queue_counts.completion_queue_count < OS_KERNEL_NVME_IO_QUEUE_COUNT) {
        return NvmeStatus::IoQueueConfigurationFailed;
    }
    if (BuildNvmeCreateIoCompletionQueueCommand(
            OS_KERNEL_NVME_CREATE_IO_COMPLETION_COMMAND_IDENTIFIER,
            OS_KERNEL_NVME_IO_QUEUE_IDENTIFIER, this->io_queue_depth_,
            this->io_completion_frame_.physical_address, true,
            static_cast<uint16_t>(OS_KERNEL_NVME_MSIX_VECTOR_INDEX),
            command) != NvmeModelStatus::Succeeded ||
        this->SubmitAdminCommand(command,
                                 OS_KERNEL_NVME_CREATE_IO_COMPLETION_COMMAND_IDENTIFIER,
                                 completion) != NvmeStatus::Succeeded) {
        return NvmeStatus::IoQueueConfigurationFailed;
    }
    if (BuildNvmeCreateIoSubmissionQueueCommand(
            OS_KERNEL_NVME_CREATE_IO_SUBMISSION_COMMAND_IDENTIFIER,
            OS_KERNEL_NVME_IO_QUEUE_IDENTIFIER, OS_KERNEL_NVME_IO_QUEUE_IDENTIFIER,
            this->io_queue_depth_, this->io_submission_frame_.physical_address,
            command) != NvmeModelStatus::Succeeded ||
        this->SubmitAdminCommand(command,
                                 OS_KERNEL_NVME_CREATE_IO_SUBMISSION_COMMAND_IDENTIFIER,
                                 completion) != NvmeStatus::Succeeded) {
        return NvmeStatus::IoQueueConfigurationFailed;
    }
    this->io_submission_tail_ = 0ULL;
    this->io_completion_head_ = 0ULL;
    this->io_completion_phase_ = true;
    this->next_io_command_identifier_ = OS_KERNEL_NVME_FIRST_IO_COMMAND_IDENTIFIER;
    this->outstanding_request_count_ = 0ULL;
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT; ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        if (!slot.asynchronous || slot.state != NvmeIoRequestState::Completed) {
            this->ReleaseIoRequestSlot(slot);
        }
    }
    this->io_ready_ = true;
    if (this->UnmaskMsix() != NvmeStatus::Succeeded) {
        this->io_ready_ = false;
        return NvmeStatus::MsixConfigurationFailed;
    }
    result.io_queue_depth = this->io_queue_depth_;
    result.geometry = this->geometry_;
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::RunIoSelfTest(NvmeProbeResult &result) noexcept {
    if (!this->io_ready_ ||
        this->geometry_.maximum_outstanding_request_count < OS_KERNEL_NVME_TEST_REQUEST_COUNT) {
        return NvmeStatus::IoNotReady;
    }
    const uint64_t transfer_block_count = this->geometry_.maximum_transfer_block_count;
    const uint64_t transfer_size_bytes =
        transfer_block_count * this->geometry_.logical_block_size_bytes;
    const uint64_t total_test_block_count =
        transfer_block_count * OS_KERNEL_NVME_TEST_REQUEST_COUNT;
    const uint64_t first_test_logical_block_address =
        this->geometry_.logical_block_count - total_test_block_count;
    const uint64_t start_nanoseconds = this->time_operation_();
    const uint64_t timeout_nanoseconds =
        this->capabilities_.ready_timeout_milliseconds * OS_KERNEL_NVME_NANOSECONDS_PER_MILLISECOND;
    const uint64_t deadline_nanoseconds = timeout_nanoseconds > UINT64_MAX - start_nanoseconds
                                              ? UINT64_MAX
                                              : start_nanoseconds + timeout_nanoseconds;
    uint64_t request_identifiers[OS_KERNEL_NVME_TEST_REQUEST_COUNT]{};
    for (uint64_t request_index = 0ULL; request_index < OS_KERNEL_NVME_TEST_REQUEST_COUNT;
         ++request_index) {
        const uint64_t logical_block_address =
            first_test_logical_block_address + request_index * transfer_block_count;
        for (uint64_t byte_index = 0ULL; byte_index < transfer_size_bytes; ++byte_index) {
            nvme_io_write_test_buffers[request_index][byte_index] =
                NvmeTestPatternByte(byte_index, logical_block_address);
        }
    }
    NvmeNamespaceDevice namespace_device{};
    if (namespace_device.Initialize(*this, OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER,
                                    this->geometry_) != NvmeStatus::Succeeded) {
        return NvmeStatus::IoSelfTestFailed;
    }
    AsynchronousBlockDevice &asynchronous_device = namespace_device;
    const bool interrupts_were_enabled = DisableInterrupts();
    AsynchronousBlockDeviceStatus asynchronous_status = AsynchronousBlockDeviceStatus::Succeeded;
    for (uint64_t request_index = 0ULL;
         request_index < OS_KERNEL_NVME_TEST_REQUEST_COUNT &&
         asynchronous_status == AsynchronousBlockDeviceStatus::Succeeded;
         ++request_index) {
        const uint64_t logical_block_address =
            first_test_logical_block_address + request_index * transfer_block_count;
        asynchronous_status = asynchronous_device.Submit(
            BlockOperation::Write, logical_block_address, nvme_io_write_test_buffers[request_index],
            transfer_size_bytes, OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX,
            deadline_nanoseconds, request_identifiers[request_index]);
    }
    RestoreInterrupts(interrupts_were_enabled);
    if (asynchronous_status != AsynchronousBlockDeviceStatus::Succeeded ||
        this->WaitForAsynchronousCompletions(asynchronous_device, request_identifiers,
                                             OS_KERNEL_NVME_TEST_REQUEST_COUNT,
                                             BlockOperation::Write) != NvmeStatus::Succeeded) {
        return NvmeStatus::IoSelfTestFailed;
    }
    if (asynchronous_device.Submit(BlockOperation::Flush, 0ULL, nullptr, 0ULL,
                                   OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX,
                                   deadline_nanoseconds, request_identifiers[0]) !=
            AsynchronousBlockDeviceStatus::Succeeded ||
        this->WaitForAsynchronousCompletions(asynchronous_device, request_identifiers, 1ULL,
                                             BlockOperation::Flush) != NvmeStatus::Succeeded) {
        return NvmeStatus::IoSelfTestFailed;
    }
    const bool read_interrupts_were_enabled = DisableInterrupts();
    for (uint64_t request_index = 0ULL;
         request_index < OS_KERNEL_NVME_TEST_REQUEST_COUNT &&
         asynchronous_status == AsynchronousBlockDeviceStatus::Succeeded;
         ++request_index) {
        const uint64_t logical_block_address =
            first_test_logical_block_address + request_index * transfer_block_count;
        asynchronous_status = asynchronous_device.Submit(
            BlockOperation::Read, logical_block_address, nvme_io_write_test_buffers[request_index],
            transfer_size_bytes, OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX,
            deadline_nanoseconds, request_identifiers[request_index]);
    }
    RestoreInterrupts(read_interrupts_were_enabled);
    if (asynchronous_status != AsynchronousBlockDeviceStatus::Succeeded ||
        this->WaitForAsynchronousCompletions(asynchronous_device, request_identifiers,
                                             OS_KERNEL_NVME_TEST_REQUEST_COUNT,
                                             BlockOperation::Read) != NvmeStatus::Succeeded) {
        return NvmeStatus::IoSelfTestFailed;
    }
    uint64_t checksum = OS_KERNEL_NVME_TEST_CHECKSUM_INITIAL_VALUE;
    for (uint64_t request_index = 0ULL; request_index < OS_KERNEL_NVME_TEST_REQUEST_COUNT;
         ++request_index) {
        const uint64_t logical_block_address =
            first_test_logical_block_address + request_index * transfer_block_count;
        for (uint64_t byte_index = 0ULL; byte_index < transfer_size_bytes; ++byte_index) {
            if (nvme_io_write_test_buffers[request_index][byte_index] !=
                NvmeTestPatternByte(byte_index, logical_block_address)) {
                return NvmeStatus::DataVerificationFailed;
            }
            checksum = (checksum ^ static_cast<uint64_t>(
                                       nvme_io_write_test_buffers[request_index][byte_index])) *
                       OS_KERNEL_NVME_TEST_CHECKSUM_MULTIPLIER;
        }
    }
    result.io_test_logical_block_address = first_test_logical_block_address;
    result.io_test_logical_block_count = transfer_block_count;
    result.io_test_checksum = checksum;
    result.io_test_request_count = OS_KERNEL_NVME_TEST_REQUEST_COUNT;
    result.io_test_transfer_page_count = OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT;
    this->PopulateRuntimeStatistics(result);
    return this->msix_interrupt_count_ == 0ULL ||
                   this->peak_outstanding_request_count_ < OS_KERNEL_NVME_TEST_REQUEST_COUNT
               ? NvmeStatus::IoSelfTestFailed
               : NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::HandleIoInterrupt() noexcept {
    this->msix_interrupt_count_ = this->msix_interrupt_count_ + 1ULL;
    return this->DrainIoCompletions();
}

void NvmeController::PopulateRuntimeStatistics(NvmeProbeResult &result) const noexcept {
    result.msix_table_entry_count = this->msix_location_.capability.table_entry_count;
    result.msix_table_bar_index = this->msix_location_.capability.table_bar_index;
    result.peak_outstanding_request_count = this->peak_outstanding_request_count_;
    result.msix_interrupt_count = this->msix_interrupt_count_;
    result.controller_reset_count = this->controller_reset_count_;
    result.error_completion_count = this->error_completion_count_;
    result.command_timeout_count = this->command_timeout_count_;
}

NvmeStatus NvmeController::Shutdown() noexcept {
    bool released = true;
    if (this->msix_ready_ && this->MaskMsix() != NvmeStatus::Succeeded) {
        released = false;
    }
    if (this->mmio_mapping_.active) {
        uint32_t configuration =
            this->ReadRegister32(OS_KERNEL_NVME_CONTROLLER_CONFIGURATION_OFFSET_BYTES);
        if ((configuration & OS_KERNEL_NVME_CONTROLLER_ENABLE_BIT) != 0U) {
            configuration &= ~OS_KERNEL_NVME_CONTROLLER_ENABLE_BIT;
            this->WriteRegister32(OS_KERNEL_NVME_CONTROLLER_CONFIGURATION_OFFSET_BYTES,
                                  configuration);
        }
        if (this->WaitForReady(false) != NvmeStatus::Succeeded) {
            this->initialized_ = false;
            this->io_ready_ = false;
            return NvmeStatus::ResourceReleaseFailed;
        }
    }
    if (this->msix_ready_ && this->RestoreMsix() != NvmeStatus::Succeeded) {
        released = false;
    }
    if (this->mmio_mapping_.active) {
        released = UnmapKernelMmio(this->mmio_mapping_) == KernelMmioStatus::Succeeded && released;
    }
    released = this->ReleaseDmaPages() == NvmeStatus::Succeeded && released;
    if (this->bar_assignment_.active && this->configuration_ != nullptr) {
        released = RestorePciMemoryBar(*this->configuration_, this->bar_assignment_) ==
                       PciMemoryBarAssignmentStatus::Succeeded &&
                   released;
    }
    this->initialized_ = false;
    this->io_ready_ = false;
    this->reset_in_progress_ = false;
    if (!released) {
        return NvmeStatus::ResourceReleaseFailed;
    }
    this->configuration_ = nullptr;
    this->time_operation_ = nullptr;
    this->geometry_ = BlockDeviceGeometry{};
    this->io_queue_depth_ = 0ULL;
    this->io_completion_list_head_index_ = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    this->io_completion_list_tail_index_ = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    return NvmeStatus::Succeeded;
}

BlockDeviceStatus NvmeController::ReadBlock(const uint64_t logical_block_address,
                                            uint8_t *const block,
                                            const uint64_t block_size_bytes) noexcept {
    return this->ReadNamespaceBlocks(OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER,
                                     this->geometry_, logical_block_address, block,
                                     block_size_bytes);
}

BlockDeviceStatus NvmeController::WriteBlock(const uint64_t logical_block_address,
                                             const uint8_t *const block,
                                             const uint64_t block_size_bytes) noexcept {
    return this->WriteNamespaceBlocks(OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER,
                                      this->geometry_, logical_block_address, block,
                                      block_size_bytes);
}

BlockDeviceStatus NvmeController::Flush() noexcept {
    return this->FlushNamespace(OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER);
}

const BlockDeviceGeometry &NvmeController::Geometry() const noexcept { return this->geometry_; }

BlockDeviceStatus NvmeController::ReadNamespaceBlocks(
    const uint32_t namespace_identifier, const BlockDeviceGeometry &geometry,
    const uint64_t logical_block_address, uint8_t *const block,
    const uint64_t block_size_bytes) noexcept {
    uint64_t logical_block_count = 0ULL;
    const BlockDeviceStatus validation_status = this->ValidateIoTransfer(
        geometry, logical_block_address, block, block_size_bytes, logical_block_count);
    if (validation_status != BlockDeviceStatus::Succeeded) {
        return validation_status;
    }
    uint64_t request_identifier = 0ULL;
    return this->SubmitIoRequest(NvmeIoOperation::Read, logical_block_address, logical_block_count,
                                 namespace_identifier, geometry, nullptr, nullptr, block_size_bytes,
                                 OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX, 0ULL, false,
                                 request_identifier) == NvmeStatus::Succeeded &&
                   this->WaitForIoRequest(request_identifier, block, block_size_bytes) ==
                       NvmeStatus::Succeeded
               ? BlockDeviceStatus::Succeeded
               : BlockDeviceStatus::ReadFailed;
}

BlockDeviceStatus NvmeController::WriteNamespaceBlocks(
    const uint32_t namespace_identifier, const BlockDeviceGeometry &geometry,
    const uint64_t logical_block_address, const uint8_t *const block,
    const uint64_t block_size_bytes) noexcept {
    uint64_t logical_block_count = 0ULL;
    const BlockDeviceStatus validation_status = this->ValidateIoTransfer(
        geometry, logical_block_address, block, block_size_bytes, logical_block_count);
    if (validation_status != BlockDeviceStatus::Succeeded) {
        return validation_status == BlockDeviceStatus::ReadFailed
                   ? BlockDeviceStatus::WriteFailed
                   : validation_status;
    }
    uint64_t request_identifier = 0ULL;
    return this->SubmitIoRequest(NvmeIoOperation::Write, logical_block_address, logical_block_count,
                                 namespace_identifier, geometry, block, nullptr, block_size_bytes,
                                 OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX, 0ULL, false,
                                 request_identifier) == NvmeStatus::Succeeded &&
                   this->WaitForIoRequest(request_identifier, nullptr, block_size_bytes) ==
                       NvmeStatus::Succeeded
               ? BlockDeviceStatus::Succeeded
               : BlockDeviceStatus::WriteFailed;
}

BlockDeviceStatus NvmeController::FlushNamespace(
    const uint32_t namespace_identifier) noexcept {
    if (!this->initialized_ || !this->io_ready_ || namespace_identifier == 0U) {
        return BlockDeviceStatus::FlushFailed;
    }
    uint64_t request_identifier = 0ULL;
    return this->SubmitIoRequest(NvmeIoOperation::Flush, 0ULL, 0ULL, namespace_identifier,
                                 this->geometry_, nullptr, nullptr, 0ULL,
                                 OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX, 0ULL, false,
                                 request_identifier) == NvmeStatus::Succeeded &&
                   this->WaitForIoRequest(request_identifier, nullptr, 0ULL) ==
                       NvmeStatus::Succeeded
               ? BlockDeviceStatus::Succeeded
               : BlockDeviceStatus::FlushFailed;
}

AsynchronousBlockDeviceStatus NvmeController::SubmitNamespaceBlockRequest(
    const uint32_t namespace_identifier, const BlockDeviceGeometry &geometry,
    const BlockOperation operation, const uint64_t logical_block_address, uint8_t *const buffer,
    const uint64_t buffer_size_bytes, const uint64_t owner_thread_index,
    const uint64_t deadline_nanoseconds, uint64_t &request_identifier) noexcept {
    request_identifier = 0ULL;
    NvmeIoOperation nvme_operation = NvmeIoOperation::Flush;
    if (!MapBlockOperationToNvmeOperation(operation, nvme_operation) ||
        deadline_nanoseconds == 0ULL || namespace_identifier == 0U) {
        return AsynchronousBlockDeviceStatus::InvalidRequest;
    }
    uint64_t logical_block_count = 0ULL;
    if (operation == BlockOperation::Flush) {
        if (!geometry.flush_supported || logical_block_address != 0ULL || buffer != nullptr ||
            buffer_size_bytes != 0ULL) {
            return AsynchronousBlockDeviceStatus::InvalidRequest;
        }
    } else {
        if (buffer == nullptr || buffer_size_bytes == 0ULL ||
            geometry.logical_block_size_bytes == 0ULL ||
            buffer_size_bytes % geometry.logical_block_size_bytes != 0ULL ||
            (operation == BlockOperation::Write && !geometry.write_supported)) {
            return AsynchronousBlockDeviceStatus::InvalidRequest;
        }
        logical_block_count = buffer_size_bytes / geometry.logical_block_size_bytes;
        if (logical_block_count == 0ULL ||
            logical_block_count > geometry.maximum_transfer_block_count ||
            logical_block_address >= geometry.logical_block_count ||
            logical_block_count > geometry.logical_block_count - logical_block_address) {
            return AsynchronousBlockDeviceStatus::InvalidRequest;
        }
    }
    const NvmeStatus submit_status = this->SubmitIoRequest(
        nvme_operation, logical_block_address, logical_block_count, namespace_identifier, geometry,
        operation == BlockOperation::Write ? buffer : nullptr,
        operation == BlockOperation::Read ? buffer : nullptr, buffer_size_bytes, owner_thread_index,
        deadline_nanoseconds, true, request_identifier);
    return MapNvmeStatusToAsynchronousStatus(submit_status);
}

AsynchronousBlockDeviceStatus
NvmeController::CancelNamespaceBlockRequest(const uint32_t namespace_identifier,
                                            const uint64_t request_identifier) noexcept {
    const bool interrupts_were_enabled = DisableInterrupts();
    NvmeIoRequestSlot *const slot = this->FindIoRequestSlot(request_identifier);
    if (slot == nullptr || !slot->asynchronous ||
        slot->namespace_identifier != namespace_identifier) {
        RestoreInterrupts(interrupts_were_enabled);
        return AsynchronousBlockDeviceStatus::RequestNotFound;
    }
    const AsynchronousBlockDeviceStatus status =
        slot->state == NvmeIoRequestState::Completed
            ? AsynchronousBlockDeviceStatus::RequestAlreadyResolved
            : AsynchronousBlockDeviceStatus::RequestInProgress;
    RestoreInterrupts(interrupts_were_enabled);
    return status;
}

AsynchronousBlockDeviceStatus
NvmeController::ResolveNamespaceBlockTimeouts(const uint64_t now_nanoseconds) noexcept {
    if (this->DrainIoCompletions() != NvmeStatus::Succeeded) {
        return AsynchronousBlockDeviceStatus::DeviceFailure;
    }
    if (!this->io_ready_) {
        this->ResolveAsynchronousResetFallout(0ULL);
        return this->ResetController(true) == NvmeStatus::Succeeded
                   ? AsynchronousBlockDeviceStatus::Succeeded
                   : AsynchronousBlockDeviceStatus::DeviceFailure;
    }
    NvmeIoRequestSlot *expired_slot = nullptr;
    for (uint64_t slot_index = 0ULL; slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT;
         ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        if (!slot.asynchronous || slot.state != NvmeIoRequestState::Submitted ||
            now_nanoseconds < slot.deadline_nanoseconds) {
            continue;
        }
        if (expired_slot == nullptr ||
            slot.deadline_nanoseconds < expired_slot->deadline_nanoseconds ||
            (slot.deadline_nanoseconds == expired_slot->deadline_nanoseconds &&
             slot.request_identifier < expired_slot->request_identifier)) {
            expired_slot = &slot;
        }
    }
    if (expired_slot == nullptr) {
        return AsynchronousBlockDeviceStatus::Succeeded;
    }
    const uint64_t timed_out_request_identifier = expired_slot->request_identifier;
    this->command_timeout_count_ = this->command_timeout_count_ + 1ULL;
    this->ResolveAsynchronousResetFallout(timed_out_request_identifier);
    return this->ResetController(true) == NvmeStatus::Succeeded
               ? AsynchronousBlockDeviceStatus::Succeeded
               : AsynchronousBlockDeviceStatus::DeviceFailure;
}

AsynchronousBlockDeviceStatus NvmeController::TakeNamespaceBlockCompletion(
    const uint32_t namespace_identifier, BlockCompletion &completion, bool &available) noexcept {
    completion = BlockCompletion{};
    available = false;
    if (namespace_identifier == 0U) {
        return AsynchronousBlockDeviceStatus::InvalidRequest;
    }
    if (!this->io_ready_) {
        this->ResolveAsynchronousResetFallout(0ULL);
        static_cast<void>(this->ResetController(true));
    }
    const bool interrupts_were_enabled = DisableInterrupts();
    uint64_t slot_index = this->io_completion_list_head_index_;
    while (slot_index != OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
        if (slot_index >= OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT) {
            RestoreInterrupts(interrupts_were_enabled);
            return AsynchronousBlockDeviceStatus::Corrupt;
        }
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        if (slot.asynchronous && slot.state == NvmeIoRequestState::Completed &&
            slot.namespace_identifier == namespace_identifier) {
            if (slot.block_result == BlockRequestResult::Succeeded &&
                slot.operation == NvmeIoOperation::Read &&
                (slot.completion_buffer == nullptr || slot.transfer_size_bytes == 0ULL)) {
                RestoreInterrupts(interrupts_were_enabled);
                return AsynchronousBlockDeviceStatus::Corrupt;
            }
            if (!this->RemoveIoCompletionIndex(slot_index)) {
                RestoreInterrupts(interrupts_were_enabled);
                return AsynchronousBlockDeviceStatus::Corrupt;
            }
            RestoreInterrupts(interrupts_were_enabled);
            if (slot.block_result == BlockRequestResult::Succeeded &&
                slot.operation == NvmeIoOperation::Read) {
                // NVMe 使用驱动自有 DMA 页；回拷在非 IRQ 的完成消费阶段执行。
                this->CopyFromIoRequest(slot, slot.completion_buffer, slot.transfer_size_bytes);
            }
            completion = BlockCompletion{
                .request_identifier = slot.request_identifier,
                .operation = MapNvmeOperationToBlockOperation(slot.operation),
                .logical_block_address = slot.logical_block_address,
                .logical_block_count = slot.logical_block_count,
                .owner_thread_index = slot.owner_thread_index,
                .result = slot.block_result,
            };
            const bool release_interrupts_were_enabled = DisableInterrupts();
            this->ReleaseIoRequestSlot(slot);
            available = true;
            RestoreInterrupts(release_interrupts_were_enabled);
            return AsynchronousBlockDeviceStatus::Succeeded;
        }
        slot_index = slot.next_completion_index;
    }
    RestoreInterrupts(interrupts_were_enabled);
    return AsynchronousBlockDeviceStatus::Succeeded;
}

NvmeStatus NvmeController::AllocateDmaPages() noexcept {
    PhysicalFrameAllocator &allocator = GetKernelPhysicalFrameAllocator();
    if (allocator.Allocate(this->admin_submission_frame_) !=
            PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.Allocate(this->admin_completion_frame_) !=
            PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.Allocate(this->identify_data_frame_) !=
            PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.Allocate(this->io_submission_frame_) !=
            PhysicalFrameAllocatorStatus::Succeeded ||
        allocator.Allocate(this->io_completion_frame_) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
        return NvmeStatus::DmaAllocationFailed;
    }
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT; ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        for (uint64_t page_index = 0ULL;
             page_index < OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT; ++page_index) {
            if (allocator.Allocate(slot.data_frames[page_index]) !=
                PhysicalFrameAllocatorStatus::Succeeded) {
                return NvmeStatus::DmaAllocationFailed;
            }
        }
        if (allocator.Allocate(slot.prp_list_frame) != PhysicalFrameAllocatorStatus::Succeeded) {
            return NvmeStatus::DmaAllocationFailed;
        }
    }
    const uint64_t admin_submission_address = DmaVirtualAddress(this->admin_submission_frame_);
    const uint64_t admin_completion_address = DmaVirtualAddress(this->admin_completion_frame_);
    const uint64_t identify_data_address = DmaVirtualAddress(this->identify_data_frame_);
    const uint64_t io_submission_address = DmaVirtualAddress(this->io_submission_frame_);
    const uint64_t io_completion_address = DmaVirtualAddress(this->io_completion_frame_);
    if (admin_submission_address == 0ULL || admin_completion_address == 0ULL ||
        identify_data_address == 0ULL || io_submission_address == 0ULL ||
        io_completion_address == 0ULL) {
        return NvmeStatus::DmaAllocationFailed;
    }
    this->admin_submission_queue_ =
        reinterpret_cast<NvmeSubmissionEntry *>(admin_submission_address);
    this->admin_completion_queue_ =
        reinterpret_cast<volatile NvmeCompletionEntry *>(admin_completion_address);
    this->identify_data_ = reinterpret_cast<uint8_t *>(identify_data_address);
    this->io_submission_queue_ = reinterpret_cast<NvmeSubmissionEntry *>(io_submission_address);
    this->io_completion_queue_ =
        reinterpret_cast<volatile NvmeCompletionEntry *>(io_completion_address);
    this->ClearPage(reinterpret_cast<uint8_t *>(admin_submission_address));
    this->ClearPage(reinterpret_cast<uint8_t *>(admin_completion_address));
    this->ClearPage(this->identify_data_);
    this->ClearPage(reinterpret_cast<uint8_t *>(io_submission_address));
    this->ClearPage(reinterpret_cast<uint8_t *>(io_completion_address));
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT; ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        for (uint64_t page_index = 0ULL;
             page_index < OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT; ++page_index) {
            const uint64_t data_address = DmaVirtualAddress(slot.data_frames[page_index]);
            if (data_address == 0ULL) {
                return NvmeStatus::DmaAllocationFailed;
            }
            slot.data_pages[page_index] = reinterpret_cast<uint8_t *>(data_address);
            this->ClearPage(slot.data_pages[page_index]);
        }
        const uint64_t prp_list_address = DmaVirtualAddress(slot.prp_list_frame);
        if (prp_list_address == 0ULL) {
            return NvmeStatus::DmaAllocationFailed;
        }
        slot.prp_list_entries = reinterpret_cast<uint64_t *>(prp_list_address);
        this->ClearPage(reinterpret_cast<uint8_t *>(prp_list_address));
        slot.state = NvmeIoRequestState::Free;
    }
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::ConfigureAdminQueues() noexcept {
    uint32_t admin_queue_attributes = 0U;
    uint32_t controller_configuration = 0U;
    if (BuildNvmeAdminQueueAttributes(OS_KERNEL_NVME_ADMIN_QUEUE_DEPTH,
                                      admin_queue_attributes) != NvmeModelStatus::Succeeded ||
        BuildNvmeControllerConfiguration(this->capabilities_, controller_configuration) !=
            NvmeModelStatus::Succeeded) {
        return NvmeStatus::AdminQueueConfigurationFailed;
    }
    this->WriteRegister32(OS_KERNEL_NVME_ADMIN_QUEUE_ATTRIBUTES_OFFSET_BYTES,
                          admin_queue_attributes);
    this->WriteRegister64(OS_KERNEL_NVME_ADMIN_SUBMISSION_BASE_OFFSET_BYTES,
                          this->admin_submission_frame_.physical_address);
    this->WriteRegister64(OS_KERNEL_NVME_ADMIN_COMPLETION_BASE_OFFSET_BYTES,
                          this->admin_completion_frame_.physical_address);
    this->submission_tail_ = 0ULL;
    this->completion_head_ = 0ULL;
    this->completion_phase_ = true;
    DmaMemoryBarrier();
    this->WriteRegister32(OS_KERNEL_NVME_CONTROLLER_CONFIGURATION_OFFSET_BYTES,
                          controller_configuration);
    return this->WaitForReady(true);
}

NvmeStatus NvmeController::WaitForReady(const bool expected_ready) noexcept {
    if (this->time_operation_ == nullptr) {
        return NvmeStatus::InvalidCapabilities;
    }
    const uint64_t start_nanoseconds = this->time_operation_();
    const uint64_t timeout_nanoseconds =
        this->capabilities_.ready_timeout_milliseconds *
        OS_KERNEL_NVME_NANOSECONDS_PER_MILLISECOND;
    const uint64_t deadline_nanoseconds =
        timeout_nanoseconds > UINT64_MAX - start_nanoseconds
            ? UINT64_MAX
            : start_nanoseconds + timeout_nanoseconds;
    while (true) {
        const uint32_t status =
            this->ReadRegister32(OS_KERNEL_NVME_CONTROLLER_STATUS_OFFSET_BYTES);
        if ((status & OS_KERNEL_NVME_CONTROLLER_STATUS_FATAL_BIT) != 0U) {
            return NvmeStatus::ControllerFatal;
        }
        const bool ready = (status & OS_KERNEL_NVME_CONTROLLER_STATUS_READY_BIT) != 0U;
        if (ready == expected_ready) {
            return NvmeStatus::Succeeded;
        }
        if (this->time_operation_() >= deadline_nanoseconds) {
            return expected_ready ? NvmeStatus::ControllerEnableTimedOut
                                  : NvmeStatus::ControllerDisableTimedOut;
        }
        ProcessorPause();
    }
}

NvmeStatus NvmeController::SubmitIdentify(const uint16_t command_identifier,
                                          const uint32_t namespace_identifier,
                                          const uint8_t selector) noexcept {
    this->ClearPage(this->identify_data_);
    NvmeSubmissionEntry command{};
    NvmeCompletionResult completion{};
    if (BuildNvmeIdentifyCommand(command_identifier, namespace_identifier, selector,
                                 this->identify_data_frame_.physical_address,
                                 command) != NvmeModelStatus::Succeeded) {
        return NvmeStatus::CommandBuildFailed;
    }
    return this->SubmitAdminCommand(command, command_identifier, completion);
}

NvmeStatus NvmeController::SubmitAdminCommand(
    const NvmeSubmissionEntry &entry, const uint16_t command_identifier,
    NvmeCompletionResult &completion_result) noexcept {
    return this->SubmitQueueCommand(
        this->admin_submission_queue_, this->admin_completion_queue_,
        OS_KERNEL_NVME_ADMIN_QUEUE_DEPTH, OS_KERNEL_NVME_ADMIN_QUEUE_IDENTIFIER,
        this->submission_tail_, this->completion_head_, this->completion_phase_, entry,
        command_identifier, completion_result);
}

NvmeStatus NvmeController::SubmitIoRequest(
    const NvmeIoOperation operation, const uint64_t logical_block_address,
    const uint64_t logical_block_count, const uint32_t namespace_identifier,
    const BlockDeviceGeometry &geometry, const uint8_t *const write_buffer,
    uint8_t *const completion_buffer, const uint64_t transfer_size_bytes,
    const uint64_t owner_thread_index, const uint64_t deadline_nanoseconds, const bool asynchronous,
    uint64_t &request_identifier) noexcept {
    request_identifier = 0ULL;
    if (!this->io_ready_ || this->reset_in_progress_) {
        return NvmeStatus::IoNotReady;
    }
    if (namespace_identifier == 0U ||
        (operation == NvmeIoOperation::Flush &&
         (logical_block_address != 0ULL || logical_block_count != 0ULL ||
          transfer_size_bytes != 0ULL || write_buffer != nullptr ||
          completion_buffer != nullptr)) ||
        (operation != NvmeIoOperation::Flush &&
         (logical_block_count == 0ULL || logical_block_address >= geometry.logical_block_count ||
          logical_block_count > geometry.logical_block_count - logical_block_address ||
          logical_block_count > geometry.maximum_transfer_block_count ||
          transfer_size_bytes != logical_block_count * geometry.logical_block_size_bytes)) ||
        (operation == NvmeIoOperation::Write && write_buffer == nullptr) ||
        (asynchronous && operation == NvmeIoOperation::Read && completion_buffer == nullptr) ||
        (asynchronous && deadline_nanoseconds == 0ULL)) {
        return NvmeStatus::InvalidIoRequest;
    }
    const bool interrupts_were_enabled = DisableInterrupts();
    for (uint64_t slot_index = 0ULL; slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT;
         ++slot_index) {
        const NvmeIoRequestSlot &active_slot = this->io_request_slots_[slot_index];
        if (active_slot.state != NvmeIoRequestState::Free &&
            active_slot.asynchronous != asynchronous) {
            RestoreInterrupts(interrupts_were_enabled);
            return NvmeStatus::IoRequestUnavailable;
        }
    }
    NvmeIoRequestSlot *const slot = this->FindFreeIoRequestSlot();
    if (slot == nullptr || this->next_io_request_identifier_ == 0ULL ||
        this->next_io_request_identifier_ == UINT64_MAX) {
        RestoreInterrupts(interrupts_were_enabled);
        return NvmeStatus::IoRequestUnavailable;
    }
    const uint16_t command_identifier = this->TakeIoCommandIdentifier();
    if (command_identifier == 0U) {
        RestoreInterrupts(interrupts_were_enabled);
        return NvmeStatus::IoRequestUnavailable;
    }
    slot->state = NvmeIoRequestState::Preparing;
    slot->request_identifier = this->next_io_request_identifier_;
    ++this->next_io_request_identifier_;
    slot->completion_buffer = completion_buffer;
    slot->owner_thread_index = owner_thread_index;
    slot->logical_block_address = logical_block_address;
    slot->logical_block_count = logical_block_count;
    slot->transfer_size_bytes = transfer_size_bytes;
    slot->deadline_nanoseconds = deadline_nanoseconds;
    slot->next_completion_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    slot->namespace_identifier = namespace_identifier;
    slot->command_identifier = command_identifier;
    slot->operation = operation;
    slot->completion_status = NvmeStatus::IoNotReady;
    slot->block_result = BlockRequestResult::None;
    slot->asynchronous = asynchronous;
    RestoreInterrupts(interrupts_were_enabled);

    if (operation == NvmeIoOperation::Write) {
        if (write_buffer == nullptr || transfer_size_bytes == 0ULL) {
            this->ReleaseIoRequestSlot(*slot);
            return NvmeStatus::InvalidIoRequest;
        }
        this->CopyToIoRequest(*slot, write_buffer, transfer_size_bytes);
    }
    NvmePrpMapping mapping{};
    if (this->PrepareIoRequestPrps(*slot, transfer_size_bytes, mapping) !=
        NvmeStatus::Succeeded) {
        this->ReleaseIoRequestSlot(*slot);
        return NvmeStatus::CommandBuildFailed;
    }
    NvmeSubmissionEntry command{};
    if (BuildNvmeIoCommand(operation, command_identifier,
                           slot->namespace_identifier,
                           logical_block_address, logical_block_count,
                           mapping.first_data_pointer, mapping.second_data_pointer,
                           command) != NvmeModelStatus::Succeeded) {
        this->ReleaseIoRequestSlot(*slot);
        return NvmeStatus::CommandBuildFailed;
    }
    const bool submission_interrupts_were_enabled = DisableInterrupts();
    if (!this->io_ready_ || this->io_submission_tail_ >= this->io_queue_depth_) {
        this->ReleaseIoRequestSlot(*slot);
        RestoreInterrupts(submission_interrupts_were_enabled);
        return NvmeStatus::IoNotReady;
    }
    this->io_submission_queue_[this->io_submission_tail_] = command;
    if (AdvanceNvmeSubmissionTail(this->io_queue_depth_, this->io_submission_tail_) !=
        NvmeModelStatus::Succeeded) {
        this->ReleaseIoRequestSlot(*slot);
        RestoreInterrupts(submission_interrupts_were_enabled);
        return NvmeStatus::CommandBuildFailed;
    }
    const uint64_t start_nanoseconds = this->time_operation_();
    const uint64_t timeout_nanoseconds =
        this->capabilities_.ready_timeout_milliseconds *
        OS_KERNEL_NVME_NANOSECONDS_PER_MILLISECOND;
    if (!asynchronous) {
        slot->deadline_nanoseconds = timeout_nanoseconds > UINT64_MAX - start_nanoseconds
                                         ? UINT64_MAX
                                         : start_nanoseconds + timeout_nanoseconds;
    }
    slot->state = NvmeIoRequestState::Submitted;
    this->outstanding_request_count_ = this->outstanding_request_count_ + 1ULL;
    if (this->outstanding_request_count_ > this->peak_outstanding_request_count_) {
        this->peak_outstanding_request_count_ = this->outstanding_request_count_;
    }
    DmaMemoryBarrier();
    const uint64_t submission_doorbell_offset =
        OS_KERNEL_NVME_DOORBELL_BASE_OFFSET_BYTES +
        (static_cast<uint64_t>(OS_KERNEL_NVME_IO_QUEUE_IDENTIFIER) *
         OS_KERNEL_NVME_SUBMISSION_DOORBELL_MULTIPLIER) *
            this->capabilities_.doorbell_stride_bytes;
    const bool suppress_submission_doorbell =
        nvme_command_timeout_injection_armed && operation == NvmeIoOperation::Write;
    if (!suppress_submission_doorbell) {
        this->WriteRegister32(submission_doorbell_offset,
                              static_cast<uint32_t>(this->io_submission_tail_));
    }
    request_identifier = slot->request_identifier;
    RestoreInterrupts(submission_interrupts_were_enabled);
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::WaitForIoRequest(const uint64_t request_identifier,
                                            uint8_t *const read_buffer,
                                            const uint64_t transfer_size_bytes) noexcept {
    while (true) {
        const bool interrupts_were_enabled = DisableInterrupts();
        NvmeIoRequestSlot *const slot = this->FindIoRequestSlot(request_identifier);
        if (slot == nullptr) {
            RestoreInterrupts(interrupts_were_enabled);
            return NvmeStatus::IoRequestNotFound;
        }
        const NvmeIoRequestState state = slot->state;
        const NvmeStatus completion_status = slot->completion_status;
        const uint64_t deadline_nanoseconds = slot->deadline_nanoseconds;
        RestoreInterrupts(interrupts_were_enabled);
        if (state == NvmeIoRequestState::Completed) {
            if (completion_status == NvmeStatus::Succeeded &&
                slot->operation == NvmeIoOperation::Read) {
                if (read_buffer == nullptr || transfer_size_bytes != slot->transfer_size_bytes) {
                    const bool invalid_release_interrupts_were_enabled = DisableInterrupts();
                    this->ReleaseIoRequestSlot(*slot);
                    RestoreInterrupts(invalid_release_interrupts_were_enabled);
                    return NvmeStatus::InvalidIoRequest;
                }
                this->CopyFromIoRequest(*slot, read_buffer, transfer_size_bytes);
            }
            const bool release_interrupts_were_enabled = DisableInterrupts();
            this->ReleaseIoRequestSlot(*slot);
            RestoreInterrupts(release_interrupts_were_enabled);
            if (completion_status != NvmeStatus::Succeeded) {
                return this->ResetController(false) == NvmeStatus::Succeeded
                           ? completion_status
                           : NvmeStatus::ControllerResetFailed;
            }
            return NvmeStatus::Succeeded;
        }
        if (this->time_operation_() >= deadline_nanoseconds) {
            this->command_timeout_count_ = this->command_timeout_count_ + 1ULL;
            return this->ResetController(false) == NvmeStatus::Succeeded
                       ? NvmeStatus::CommandTimedOut
                       : NvmeStatus::ControllerResetFailed;
        }
        if (interrupts_were_enabled) {
            WaitForInterrupt();
        } else {
            ProcessorPause();
        }
        const NvmeStatus drain_status = this->DrainIoCompletions();
        if (drain_status != NvmeStatus::Succeeded) {
            return this->ResetController(false) == NvmeStatus::Succeeded
                       ? NvmeStatus::CommandCompletionFailed
                       : NvmeStatus::ControllerResetFailed;
        }
    }
}

NvmeStatus NvmeController::WaitForAsynchronousCompletions(
    AsynchronousBlockDevice &device, const uint64_t *const request_identifiers,
    const uint64_t request_count, const BlockOperation expected_operation) noexcept {
    if (request_identifiers == nullptr || request_count == 0ULL ||
        request_count > OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT) {
        return NvmeStatus::InvalidIoRequest;
    }
    bool completion_observed[OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT]{};
    uint64_t completion_count = 0ULL;
    while (completion_count < request_count) {
        if (this->DrainIoCompletions() != NvmeStatus::Succeeded) {
            return NvmeStatus::CommandCompletionFailed;
        }
        while (true) {
            BlockCompletion completion{};
            bool available = false;
            if (device.TakeCompletion(completion, available) !=
                AsynchronousBlockDeviceStatus::Succeeded) {
                return NvmeStatus::IoSelfTestFailed;
            }
            if (!available) {
                break;
            }
            uint64_t matched_index = request_count;
            for (uint64_t request_index = 0ULL; request_index < request_count; ++request_index) {
                if (request_identifiers[request_index] == completion.request_identifier) {
                    matched_index = request_index;
                    break;
                }
            }
            if (matched_index == request_count || completion_observed[matched_index] ||
                completion.operation != expected_operation ||
                completion.owner_thread_index !=
                    OS_KERNEL_BLOCK_REQUEST_KERNEL_OWNER_THREAD_INDEX ||
                completion.result != BlockRequestResult::Succeeded) {
                return NvmeStatus::IoSelfTestFailed;
            }
            completion_observed[matched_index] = true;
            ++completion_count;
        }
        if (completion_count == request_count) {
            return NvmeStatus::Succeeded;
        }
        if (device.ResolveTimeouts(this->time_operation_()) !=
            AsynchronousBlockDeviceStatus::Succeeded) {
            return NvmeStatus::CommandTimedOut;
        }
        const bool interrupts_were_enabled = DisableInterrupts();
        RestoreInterrupts(interrupts_were_enabled);
        if (interrupts_were_enabled) {
            WaitForInterrupt();
        } else {
            ProcessorPause();
        }
    }
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::DrainIoCompletions() noexcept {
    if (this->io_completion_queue_ == nullptr || this->io_queue_depth_ < 2ULL) {
        return NvmeStatus::IoNotReady;
    }
    const bool interrupts_were_enabled = DisableInterrupts();
    bool completion_processed = false;
    while (true) {
        DmaMemoryBarrier();
        NvmeCompletionEntry completion_entry{};
        for (uint64_t dword_index = 0ULL;
             dword_index < OS_KERNEL_NVME_COMPLETION_DWORD_COUNT; ++dword_index) {
            completion_entry.dwords[dword_index] =
                this->io_completion_queue_[this->io_completion_head_].dwords[dword_index];
        }
        NvmeCompletionResult completion_result{};
        const NvmeModelStatus completion_status = DecodeNvmeCompletionEntry(
            completion_entry, this->io_completion_phase_, completion_result);
        if (completion_status == NvmeModelStatus::CompletionNotReady) {
            break;
        }
        NvmeIoRequestSlot *const slot =
            this->FindIoCommandSlot(completion_result.command_identifier);
        if ((completion_status != NvmeModelStatus::Succeeded &&
             completion_status != NvmeModelStatus::CompletionFailed) ||
            completion_result.submission_queue_identifier !=
                OS_KERNEL_NVME_IO_QUEUE_IDENTIFIER ||
            slot == nullptr || slot->state != NvmeIoRequestState::Submitted ||
            this->outstanding_request_count_ == 0ULL ||
            AdvanceNvmeCompletionHead(this->io_queue_depth_, this->io_completion_head_,
                                      this->io_completion_phase_) !=
                NvmeModelStatus::Succeeded) {
            this->io_ready_ = false;
            RestoreInterrupts(interrupts_were_enabled);
            return NvmeStatus::CommandCompletionFailed;
        }
        slot->completion_status = completion_status == NvmeModelStatus::Succeeded
                                      ? NvmeStatus::Succeeded
                                      : NvmeStatus::CommandCompletionFailed;
        slot->block_result = completion_status == NvmeModelStatus::Succeeded
                                 ? BlockRequestResult::Succeeded
                                 : BlockRequestResult::DeviceError;
        slot->state = NvmeIoRequestState::Completed;
        this->outstanding_request_count_ = this->outstanding_request_count_ - 1ULL;
        if (slot->asynchronous) {
            const uint64_t slot_index = this->IndexOfIoRequestSlot(*slot);
            if (slot_index == OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
                this->io_ready_ = false;
                RestoreInterrupts(interrupts_were_enabled);
                return NvmeStatus::CommandCompletionFailed;
            }
            this->AppendIoCompletionIndex(slot_index);
        }
        if (completion_status == NvmeModelStatus::CompletionFailed) {
            this->error_completion_count_ = this->error_completion_count_ + 1ULL;
            this->io_ready_ = false;
        }
        completion_processed = true;
    }
    if (completion_processed) {
        DmaMemoryBarrier();
        const uint64_t completion_doorbell_offset =
            OS_KERNEL_NVME_DOORBELL_BASE_OFFSET_BYTES +
            (static_cast<uint64_t>(OS_KERNEL_NVME_IO_QUEUE_IDENTIFIER) *
                 OS_KERNEL_NVME_SUBMISSION_DOORBELL_MULTIPLIER +
             OS_KERNEL_NVME_COMPLETION_DOORBELL_INCREMENT) *
                this->capabilities_.doorbell_stride_bytes;
        this->WriteRegister32(completion_doorbell_offset,
                              static_cast<uint32_t>(this->io_completion_head_));
    }
    RestoreInterrupts(interrupts_were_enabled);
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::ResetController(const bool preserve_asynchronous_completions) noexcept {
    if (this->reset_in_progress_ || !this->mmio_mapping_.active) {
        return NvmeStatus::ControllerResetFailed;
    }
    this->reset_in_progress_ = true;
    this->io_ready_ = false;
    if (this->MaskMsix() != NvmeStatus::Succeeded) {
        this->reset_in_progress_ = false;
        return NvmeStatus::ControllerResetFailed;
    }
    uint32_t configuration =
        this->ReadRegister32(OS_KERNEL_NVME_CONTROLLER_CONFIGURATION_OFFSET_BYTES);
    configuration &= ~OS_KERNEL_NVME_CONTROLLER_ENABLE_BIT;
    this->WriteRegister32(OS_KERNEL_NVME_CONTROLLER_CONFIGURATION_OFFSET_BYTES, configuration);
    if (this->WaitForReady(false) != NvmeStatus::Succeeded) {
        this->reset_in_progress_ = false;
        return NvmeStatus::ControllerResetFailed;
    }
    nvme_command_timeout_injection_armed = false;
    this->ClearPage(reinterpret_cast<uint8_t *>(this->admin_submission_queue_));
    this->ClearPage(reinterpret_cast<uint8_t *>(
        const_cast<NvmeCompletionEntry *>(this->admin_completion_queue_)));
    this->ClearPage(reinterpret_cast<uint8_t *>(this->io_submission_queue_));
    this->ClearPage(reinterpret_cast<uint8_t *>(
        const_cast<NvmeCompletionEntry *>(this->io_completion_queue_)));
    this->outstanding_request_count_ = 0ULL;
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT; ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        if (!preserve_asynchronous_completions || !slot.asynchronous ||
            slot.state != NvmeIoRequestState::Completed) {
            this->ReleaseIoRequestSlot(slot);
        }
    }
    if (!preserve_asynchronous_completions) {
        this->io_completion_list_head_index_ = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
        this->io_completion_list_tail_index_ = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    }
    if (this->ConfigureAdminQueues() != NvmeStatus::Succeeded) {
        this->reset_in_progress_ = false;
        return NvmeStatus::ControllerResetFailed;
    }
    NvmeProbeResult ignored_result{};
    if (this->ConfigureIoQueues(ignored_result) != NvmeStatus::Succeeded) {
        this->reset_in_progress_ = false;
        return NvmeStatus::ControllerResetFailed;
    }
    this->controller_reset_count_ = this->controller_reset_count_ + 1ULL;
    this->reset_in_progress_ = false;
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::SubmitQueueCommand(
    NvmeSubmissionEntry *const submission_queue,
    volatile NvmeCompletionEntry *const completion_queue, const uint64_t queue_depth,
    const uint16_t queue_identifier, uint64_t &submission_tail,
    uint64_t &completion_head, bool &completion_phase,
    const NvmeSubmissionEntry &entry, const uint16_t command_identifier,
    NvmeCompletionResult &completion_result) noexcept {
    if (submission_queue == nullptr || completion_queue == nullptr || queue_depth < 2ULL ||
        submission_tail >= queue_depth || completion_head >= queue_depth ||
        this->time_operation_ == nullptr) {
        return NvmeStatus::CommandBuildFailed;
    }
    submission_queue[submission_tail] = entry;
    if (AdvanceNvmeSubmissionTail(queue_depth, submission_tail) !=
        NvmeModelStatus::Succeeded) {
        return NvmeStatus::CommandBuildFailed;
    }
    DmaMemoryBarrier();
    const uint64_t submission_doorbell_index =
        static_cast<uint64_t>(queue_identifier) *
        OS_KERNEL_NVME_SUBMISSION_DOORBELL_MULTIPLIER;
    const uint64_t submission_doorbell_offset =
        OS_KERNEL_NVME_DOORBELL_BASE_OFFSET_BYTES +
        submission_doorbell_index * this->capabilities_.doorbell_stride_bytes;
    this->WriteRegister32(submission_doorbell_offset, static_cast<uint32_t>(submission_tail));
    const uint64_t start_nanoseconds = this->time_operation_();
    const uint64_t timeout_nanoseconds =
        this->capabilities_.ready_timeout_milliseconds *
        OS_KERNEL_NVME_NANOSECONDS_PER_MILLISECOND;
    const uint64_t deadline_nanoseconds =
        timeout_nanoseconds > UINT64_MAX - start_nanoseconds
            ? UINT64_MAX
            : start_nanoseconds + timeout_nanoseconds;
    NvmeModelStatus completion_status = NvmeModelStatus::CompletionNotReady;
    while (true) {
        DmaMemoryBarrier();
        NvmeCompletionEntry completion_entry{};
        for (uint64_t dword_index = 0ULL;
             dword_index < OS_KERNEL_NVME_COMPLETION_DWORD_COUNT; ++dword_index) {
            completion_entry.dwords[dword_index] =
                completion_queue[completion_head].dwords[dword_index];
        }
        completion_status = DecodeNvmeCompletion(
            completion_entry, completion_phase, command_identifier, completion_result);
        if (completion_status != NvmeModelStatus::CompletionNotReady) {
            break;
        }
        if ((this->ReadRegister32(OS_KERNEL_NVME_CONTROLLER_STATUS_OFFSET_BYTES) &
             OS_KERNEL_NVME_CONTROLLER_STATUS_FATAL_BIT) != 0U) {
            return NvmeStatus::ControllerFatal;
        }
        if (this->time_operation_() >= deadline_nanoseconds) {
            return NvmeStatus::CommandTimedOut;
        }
        ProcessorPause();
    }
    if ((completion_status != NvmeModelStatus::Succeeded &&
         completion_status != NvmeModelStatus::CompletionFailed) ||
        completion_result.submission_queue_identifier != queue_identifier ||
        AdvanceNvmeCompletionHead(queue_depth, completion_head, completion_phase) !=
            NvmeModelStatus::Succeeded) {
        return NvmeStatus::CommandCompletionFailed;
    }
    DmaMemoryBarrier();
    const uint64_t completion_doorbell_index =
        static_cast<uint64_t>(queue_identifier) *
            OS_KERNEL_NVME_SUBMISSION_DOORBELL_MULTIPLIER +
        OS_KERNEL_NVME_COMPLETION_DOORBELL_INCREMENT;
    const uint64_t completion_doorbell_offset =
        OS_KERNEL_NVME_DOORBELL_BASE_OFFSET_BYTES +
        completion_doorbell_index * this->capabilities_.doorbell_stride_bytes;
    this->WriteRegister32(completion_doorbell_offset, static_cast<uint32_t>(completion_head));
    return completion_status == NvmeModelStatus::Succeeded
               ? NvmeStatus::Succeeded
               : NvmeStatus::CommandCompletionFailed;
}

BlockDeviceStatus NvmeController::ValidateIoTransfer(
    const BlockDeviceGeometry &geometry, const uint64_t logical_block_address,
    const uint8_t *const block,
    const uint64_t block_size_bytes, uint64_t &logical_block_count) const noexcept {
    logical_block_count = 0ULL;
    if (!this->initialized_ || !this->io_ready_) {
        return BlockDeviceStatus::ReadFailed;
    }
    if (block == nullptr || block_size_bytes == 0ULL ||
        geometry.logical_block_size_bytes == 0ULL ||
        block_size_bytes % geometry.logical_block_size_bytes != 0ULL) {
        return BlockDeviceStatus::InvalidBuffer;
    }
    logical_block_count = block_size_bytes / geometry.logical_block_size_bytes;
    if (logical_block_count > geometry.maximum_transfer_block_count ||
        logical_block_address >= geometry.logical_block_count ||
        logical_block_count > geometry.logical_block_count - logical_block_address) {
        logical_block_count = 0ULL;
        return BlockDeviceStatus::InvalidBlock;
    }
    return BlockDeviceStatus::Succeeded;
}

uint16_t NvmeController::TakeIoCommandIdentifier() noexcept {
    for (uint64_t attempt_count = 0ULL;
         attempt_count < static_cast<uint64_t>(OS_KERNEL_NVME_LAST_IO_COMMAND_IDENTIFIER);
         ++attempt_count) {
        const uint16_t command_identifier = this->next_io_command_identifier_;
        this->next_io_command_identifier_ =
            this->next_io_command_identifier_ >= OS_KERNEL_NVME_LAST_IO_COMMAND_IDENTIFIER
                ? OS_KERNEL_NVME_FIRST_IO_COMMAND_IDENTIFIER
                : static_cast<uint16_t>(this->next_io_command_identifier_ + 1U);
        if (this->FindIoCommandSlot(command_identifier) == nullptr) {
            return command_identifier;
        }
    }
    return 0U;
}

NvmeIoRequestSlot *NvmeController::FindIoCommandSlot(const uint16_t command_identifier) noexcept {
    if (command_identifier == 0U) {
        return nullptr;
    }
    for (uint64_t slot_index = 0ULL; slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT;
         ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        if (slot.state != NvmeIoRequestState::Free &&
            slot.command_identifier == command_identifier) {
            return &slot;
        }
    }
    return nullptr;
}

NvmeIoRequestSlot *NvmeController::FindIoRequestSlot(
    const uint64_t request_identifier) noexcept {
    if (request_identifier == 0ULL) {
        return nullptr;
    }
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT; ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        if (slot.state != NvmeIoRequestState::Free &&
            slot.request_identifier == request_identifier) {
            return &slot;
        }
    }
    return nullptr;
}

NvmeIoRequestSlot *NvmeController::FindFreeIoRequestSlot() noexcept {
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT; ++slot_index) {
        if (this->io_request_slots_[slot_index].state == NvmeIoRequestState::Free) {
            return &this->io_request_slots_[slot_index];
        }
    }
    return nullptr;
}

uint64_t NvmeController::IndexOfIoRequestSlot(const NvmeIoRequestSlot &slot) const noexcept {
    const uint64_t slot_address = reinterpret_cast<uint64_t>(&slot);
    const uint64_t storage_address = reinterpret_cast<uint64_t>(this->io_request_slots_);
    if (slot_address < storage_address) {
        return OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    }
    const uint64_t byte_offset = slot_address - storage_address;
    if (byte_offset % sizeof(NvmeIoRequestSlot) != 0ULL) {
        return OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    }
    const uint64_t slot_index = byte_offset / sizeof(NvmeIoRequestSlot);
    return slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT
               ? slot_index
               : OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
}

void NvmeController::AppendIoCompletionIndex(const uint64_t slot_index) noexcept {
    this->io_request_slots_[slot_index].next_completion_index =
        OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    if (this->io_completion_list_tail_index_ == OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
        this->io_completion_list_head_index_ = slot_index;
        this->io_completion_list_tail_index_ = slot_index;
        return;
    }
    this->io_request_slots_[this->io_completion_list_tail_index_].next_completion_index =
        slot_index;
    this->io_completion_list_tail_index_ = slot_index;
}

bool NvmeController::RemoveIoCompletionIndex(const uint64_t slot_index) noexcept {
    uint64_t previous_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    uint64_t current_index = this->io_completion_list_head_index_;
    while (current_index != OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
        if (current_index >= OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT) {
            return false;
        }
        if (current_index == slot_index) {
            const uint64_t next_index =
                this->io_request_slots_[current_index].next_completion_index;
            if (previous_index == OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX) {
                this->io_completion_list_head_index_ = next_index;
            } else {
                this->io_request_slots_[previous_index].next_completion_index = next_index;
            }
            if (this->io_completion_list_tail_index_ == current_index) {
                this->io_completion_list_tail_index_ = previous_index;
            }
            this->io_request_slots_[current_index].next_completion_index =
                OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
            return true;
        }
        previous_index = current_index;
        current_index = this->io_request_slots_[current_index].next_completion_index;
    }
    return false;
}

void NvmeController::ReleaseIoRequestSlot(NvmeIoRequestSlot &slot) noexcept {
    slot.request_identifier = 0ULL;
    slot.completion_buffer = nullptr;
    slot.owner_thread_index = 0ULL;
    slot.logical_block_address = 0ULL;
    slot.logical_block_count = 0ULL;
    slot.transfer_size_bytes = 0ULL;
    slot.deadline_nanoseconds = 0ULL;
    slot.next_completion_index = OS_KERNEL_BLOCK_REQUEST_INVALID_INDEX;
    slot.namespace_identifier = 0U;
    slot.command_identifier = 0U;
    slot.operation = NvmeIoOperation::Read;
    slot.completion_status = NvmeStatus::IoNotReady;
    slot.block_result = BlockRequestResult::None;
    slot.state = NvmeIoRequestState::Free;
    slot.asynchronous = false;
}

void NvmeController::ResolveAsynchronousResetFallout(
    const uint64_t timed_out_request_identifier) noexcept {
    const bool interrupts_were_enabled = DisableInterrupts();
    for (uint64_t slot_index = 0ULL; slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT;
         ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        if (!slot.asynchronous || (slot.state != NvmeIoRequestState::Preparing &&
                                   slot.state != NvmeIoRequestState::Submitted)) {
            continue;
        }
        const bool timed_out = timed_out_request_identifier != 0ULL &&
                               slot.request_identifier == timed_out_request_identifier;
        if (slot.state == NvmeIoRequestState::Submitted &&
            this->outstanding_request_count_ != 0ULL) {
            this->outstanding_request_count_ = this->outstanding_request_count_ - 1ULL;
        }
        slot.completion_status =
            timed_out ? NvmeStatus::CommandTimedOut : NvmeStatus::CommandCompletionFailed;
        slot.block_result =
            timed_out ? BlockRequestResult::TimedOut : BlockRequestResult::DeviceError;
        slot.state = NvmeIoRequestState::Completed;
        this->AppendIoCompletionIndex(slot_index);
    }
    RestoreInterrupts(interrupts_were_enabled);
}

NvmeStatus NvmeController::PrepareIoRequestPrps(NvmeIoRequestSlot &slot,
                                                const uint64_t transfer_size_bytes,
                                                NvmePrpMapping &mapping) noexcept {
    if (slot.operation == NvmeIoOperation::Flush) {
        mapping = NvmePrpMapping{};
        return transfer_size_bytes == 0ULL ? NvmeStatus::Succeeded
                                          : NvmeStatus::InvalidIoRequest;
    }
    if (transfer_size_bytes == 0ULL ||
        transfer_size_bytes > OS_KERNEL_NVME_MAXIMUM_TRANSFER_SIZE_BYTES) {
        return NvmeStatus::InvalidIoRequest;
    }
    const uint64_t page_count =
        (transfer_size_bytes + OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES - 1ULL) /
        OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES;
    uint64_t data_page_physical_addresses[OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT]{};
    for (uint64_t page_index = 0ULL; page_index < page_count; ++page_index) {
        data_page_physical_addresses[page_index] = slot.data_frames[page_index].physical_address;
    }
    return BuildNvmePrpMapping(
               data_page_physical_addresses, page_count, slot.prp_list_frame.physical_address,
               slot.prp_list_entries, OS_KERNEL_NVME_PRP_LIST_ENTRY_CAPACITY,
               mapping) == NvmeModelStatus::Succeeded
               ? NvmeStatus::Succeeded
               : NvmeStatus::CommandBuildFailed;
}

void NvmeController::CopyToIoRequest(NvmeIoRequestSlot &slot, const uint8_t *const source,
                                     const uint64_t byte_count) noexcept {
    uint64_t copied_byte_count = 0ULL;
    for (uint64_t page_index = 0ULL;
         page_index < OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT && copied_byte_count < byte_count;
         ++page_index) {
        const uint64_t remaining_byte_count = byte_count - copied_byte_count;
        const uint64_t page_byte_count =
            remaining_byte_count < OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES
                ? remaining_byte_count
                : OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES;
        for (uint64_t byte_index = 0ULL; byte_index < page_byte_count; ++byte_index) {
            slot.data_pages[page_index][byte_index] = source[copied_byte_count + byte_index];
        }
        copied_byte_count += page_byte_count;
    }
}

void NvmeController::CopyFromIoRequest(const NvmeIoRequestSlot &slot,
                                       uint8_t *const destination,
                                       const uint64_t byte_count) noexcept {
    uint64_t copied_byte_count = 0ULL;
    for (uint64_t page_index = 0ULL;
         page_index < OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT && copied_byte_count < byte_count;
         ++page_index) {
        const uint64_t remaining_byte_count = byte_count - copied_byte_count;
        const uint64_t page_byte_count =
            remaining_byte_count < OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES
                ? remaining_byte_count
                : OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES;
        for (uint64_t byte_index = 0ULL; byte_index < page_byte_count; ++byte_index) {
            destination[copied_byte_count + byte_index] = slot.data_pages[page_index][byte_index];
        }
        copied_byte_count += page_byte_count;
    }
}

NvmeStatus NvmeController::MaskMsix() noexcept {
    if (!this->msix_ready_ || this->configuration_ == nullptr ||
        this->msix_table_entry_ == nullptr) {
        return NvmeStatus::MsixConfigurationFailed;
    }
    const uint16_t masked_control = static_cast<uint16_t>(
        this->msix_location_.original_message_control | OS_KERNEL_NVME_MSIX_ENABLE_BIT |
        OS_KERNEL_NVME_MSIX_FUNCTION_MASK_BIT);
    if (WritePciMsixMessageControl(*this->configuration_, this->pci_controller_.address,
                                   this->msix_location_.capability_offset_bytes,
                                   masked_control) != PciMsixAccessStatus::Succeeded) {
        return NvmeStatus::MsixConfigurationFailed;
    }
    this->msix_table_entry_->vector_control = OS_KERNEL_NVME_MSIX_VECTOR_MASK_BIT;
    DmaMemoryBarrier();
    return NvmeStatus::Succeeded;
}

NvmeStatus NvmeController::UnmaskMsix() noexcept {
    if (!this->msix_ready_ || this->configuration_ == nullptr ||
        this->msix_table_entry_ == nullptr) {
        return NvmeStatus::MsixConfigurationFailed;
    }
    this->msix_table_entry_->vector_control = 0U;
    DmaMemoryBarrier();
    const uint16_t enabled_control = static_cast<uint16_t>(
        (this->msix_location_.original_message_control | OS_KERNEL_NVME_MSIX_ENABLE_BIT) &
        ~OS_KERNEL_NVME_MSIX_FUNCTION_MASK_BIT);
    return WritePciMsixMessageControl(*this->configuration_, this->pci_controller_.address,
                                      this->msix_location_.capability_offset_bytes,
                                      enabled_control) == PciMsixAccessStatus::Succeeded
               ? NvmeStatus::Succeeded
               : NvmeStatus::MsixConfigurationFailed;
}

NvmeStatus NvmeController::RestoreMsix() noexcept {
    if (!this->msix_ready_ || this->configuration_ == nullptr) {
        return NvmeStatus::MsixConfigurationFailed;
    }
    bool restored = this->MaskMsix() == NvmeStatus::Succeeded;
    active_nvme_controller = nullptr;
    restored = WritePciMsixMessageControl(
                   *this->configuration_, this->pci_controller_.address,
                   this->msix_location_.capability_offset_bytes,
                   this->msix_location_.original_message_control) ==
                   PciMsixAccessStatus::Succeeded &&
               restored;
    if (this->msix_mmio_mapping_.active) {
        restored = UnmapKernelMmio(this->msix_mmio_mapping_) == KernelMmioStatus::Succeeded &&
                   restored;
    }
    if (this->msix_bar_assignment_.active) {
        restored = RestorePciMemoryBar(*this->configuration_, this->msix_bar_assignment_) ==
                       PciMemoryBarAssignmentStatus::Succeeded &&
                   restored;
    }
    this->msix_table_entry_ = nullptr;
    this->msix_ready_ = false;
    return restored ? NvmeStatus::Succeeded : NvmeStatus::ResourceReleaseFailed;
}

NvmeStatus NvmeController::ReleaseDmaPages() noexcept {
    bool released = true;
    PhysicalFrameAllocator &allocator = GetKernelPhysicalFrameAllocator();
    for (uint64_t slot_index = 0ULL;
         slot_index < OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT; ++slot_index) {
        NvmeIoRequestSlot &slot = this->io_request_slots_[slot_index];
        released = ReleaseOwnedFrame(allocator, slot.prp_list_frame) && released;
        for (uint64_t page_index = 0ULL;
             page_index < OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT; ++page_index) {
            released = ReleaseOwnedFrame(allocator, slot.data_frames[page_index]) && released;
            slot.data_pages[page_index] = nullptr;
        }
        slot.prp_list_entries = nullptr;
        this->ReleaseIoRequestSlot(slot);
    }
    released = ReleaseOwnedFrame(allocator, this->io_completion_frame_) && released;
    released = ReleaseOwnedFrame(allocator, this->io_submission_frame_) && released;
    released = ReleaseOwnedFrame(allocator, this->identify_data_frame_) && released;
    released = ReleaseOwnedFrame(allocator, this->admin_completion_frame_) && released;
    released = ReleaseOwnedFrame(allocator, this->admin_submission_frame_) && released;
    this->io_completion_queue_ = nullptr;
    this->io_submission_queue_ = nullptr;
    this->identify_data_ = nullptr;
    this->admin_completion_queue_ = nullptr;
    this->admin_submission_queue_ = nullptr;
    return released ? NvmeStatus::Succeeded : NvmeStatus::ResourceReleaseFailed;
}

uint32_t NvmeController::ReadRegister32(const uint64_t offset_bytes) const noexcept {
    volatile const uint32_t *const register_address = reinterpret_cast<volatile const uint32_t *>(
        this->mmio_mapping_.virtual_address + offset_bytes);
    return *register_address;
}

uint64_t NvmeController::ReadRegister64(const uint64_t offset_bytes) const noexcept {
    const uint64_t low = static_cast<uint64_t>(this->ReadRegister32(offset_bytes));
    const uint64_t high = static_cast<uint64_t>(
        this->ReadRegister32(offset_bytes + OS_KERNEL_NVME_REGISTER_DWORD_SIZE_BYTES));
    return low | high << OS_KERNEL_NVME_DWORD_BIT_COUNT;
}

void NvmeController::WriteRegister32(const uint64_t offset_bytes,
                                     const uint32_t value) noexcept {
    volatile uint32_t *const register_address = reinterpret_cast<volatile uint32_t *>(
        this->mmio_mapping_.virtual_address + offset_bytes);
    *register_address = value;
}

void NvmeController::WriteRegister64(const uint64_t offset_bytes,
                                     const uint64_t value) noexcept {
    this->WriteRegister32(offset_bytes, static_cast<uint32_t>(value));
    this->WriteRegister32(offset_bytes + OS_KERNEL_NVME_REGISTER_DWORD_SIZE_BYTES,
                          static_cast<uint32_t>(value >> OS_KERNEL_NVME_DWORD_BIT_COUNT));
}

void NvmeController::ClearPage(uint8_t *const page) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES;
         ++byte_index) {
        page[byte_index] = 0U;
    }
}

NvmeStatus ProbeNvmeController(const NvmeTimeOperation time_operation,
                               NvmeProbeResult &result) noexcept {
    const PhysicalFrameAllocatorStatistics frames_before = GetPhysicalFrameAllocatorStatistics();
    const KernelVirtualAddressAllocatorStatistics kva_before =
        GetKernelVirtualAddressAllocator().Statistics();
    PciConfigurationSpace configuration{};
    PciNvmeController pci_controller{};
    const PciNvmeScanStatus scan_status = FindPciNvmeController(configuration, pci_controller);
    if (scan_status == PciNvmeScanStatus::NotFound) {
        return NvmeStatus::NotFound;
    }
    if (scan_status == PciNvmeScanStatus::MultipleControllers) {
        return NvmeStatus::MultipleControllers;
    }
    if (scan_status != PciNvmeScanStatus::Succeeded) {
        return NvmeStatus::PciAccessFailed;
    }
    NvmeController controller{};
    NvmeStatus status = controller.Initialize(configuration, pci_controller, time_operation);
    if (status == NvmeStatus::Succeeded) {
        status = controller.Identify(result);
    }
    if (status == NvmeStatus::Succeeded) {
        status = controller.ConfigureIoQueues(result);
    }
    if (status == NvmeStatus::Succeeded) {
        status = controller.RunIoSelfTest(result);
    }
    controller.PopulateRuntimeStatistics(result);
    const NvmeStatus shutdown_status = controller.Shutdown();
    if (status == NvmeStatus::Succeeded && shutdown_status != NvmeStatus::Succeeded) {
        status = shutdown_status;
    }
    const PhysicalFrameAllocatorStatistics frames_after = GetPhysicalFrameAllocatorStatistics();
    const KernelVirtualAddressAllocatorStatistics kva_after =
        GetKernelVirtualAddressAllocator().Statistics();
    result.resources_reclaimed =
        frames_after.allocated_frame_count == frames_before.allocated_frame_count &&
        kva_after.active_allocation_count == kva_before.active_allocation_count &&
        kva_after.allocated_page_count == kva_before.allocated_page_count;
    if (!result.resources_reclaimed) {
        return NvmeStatus::ResourceLeak;
    }
    return status;
}

NvmeStatus
InitializeNvmeStorageRuntime(const NvmeTimeOperation time_operation,
                             NvmeStorageRuntimeResult &result, BlockDevice *&root_device,
                             BlockDevice *&swap_device,
                             AsynchronousBlockDevice *&root_asynchronous_device,
                             AsynchronousBlockDevice *&swap_asynchronous_device) noexcept {
    result = NvmeStorageRuntimeResult{};
    root_device = nullptr;
    swap_device = nullptr;
    root_asynchronous_device = nullptr;
    swap_asynchronous_device = nullptr;
    if (kernel_nvme_storage_runtime.active) {
        return NvmeStatus::AlreadyInitialized;
    }
    kernel_nvme_storage_runtime.frames_before = GetPhysicalFrameAllocatorStatistics();
    kernel_nvme_storage_runtime.kva_before =
        GetKernelVirtualAddressAllocator().Statistics();
    PciNvmeController pci_controller{};
    const PciNvmeScanStatus scan_status = FindPciNvmeController(
        kernel_nvme_storage_runtime.configuration, pci_controller);
    if (scan_status == PciNvmeScanStatus::NotFound) {
        return NvmeStatus::NotFound;
    }
    if (scan_status == PciNvmeScanStatus::MultipleControllers) {
        return NvmeStatus::MultipleControllers;
    }
    if (scan_status != PciNvmeScanStatus::Succeeded) {
        return NvmeStatus::PciAccessFailed;
    }
    NvmeStatus status = kernel_nvme_storage_runtime.controller.Initialize(
        kernel_nvme_storage_runtime.configuration, pci_controller, time_operation);
    if (status == NvmeStatus::Succeeded) {
        status = kernel_nvme_storage_runtime.controller.Identify(result.controller);
    }
    if (status == NvmeStatus::Succeeded) {
        result.root_namespace_identity = result.controller.namespace_identity;
        result.root_geometry = result.controller.geometry;
        status = kernel_nvme_storage_runtime.controller.IdentifyNamespace(
            OS_KERNEL_NVME_SECONDARY_NAMESPACE_IDENTIFIER,
            result.controller.controller_identity, result.swap_namespace_identity,
            result.swap_geometry);
        if (status != NvmeStatus::Succeeded) {
            status = NvmeStatus::SecondaryNamespaceUnavailable;
        }
    }
    if (status == NvmeStatus::Succeeded) {
        status = kernel_nvme_storage_runtime.controller.ConfigureIoQueues(result.controller);
    }
    if (status == NvmeStatus::Succeeded) {
        status = kernel_nvme_storage_runtime.root_device.Initialize(
            kernel_nvme_storage_runtime.controller,
            OS_KERNEL_NVME_PRIMARY_NAMESPACE_IDENTIFIER, result.root_geometry);
    }
    if (status == NvmeStatus::Succeeded) {
        status = kernel_nvme_storage_runtime.swap_device.Initialize(
            kernel_nvme_storage_runtime.controller,
            OS_KERNEL_NVME_SECONDARY_NAMESPACE_IDENTIFIER, result.swap_geometry);
    }
    if (status != NvmeStatus::Succeeded) {
        kernel_nvme_storage_runtime.controller.PopulateRuntimeStatistics(result.controller);
        const NvmeStatus shutdown_status = kernel_nvme_storage_runtime.controller.Shutdown();
        const PhysicalFrameAllocatorStatistics frames_after =
            GetPhysicalFrameAllocatorStatistics();
        const KernelVirtualAddressAllocatorStatistics kva_after =
            GetKernelVirtualAddressAllocator().Statistics();
        result.resources_reclaimed =
            shutdown_status == NvmeStatus::Succeeded &&
            frames_after.allocated_frame_count ==
                kernel_nvme_storage_runtime.frames_before.allocated_frame_count &&
            kva_after.active_allocation_count ==
                kernel_nvme_storage_runtime.kva_before.active_allocation_count &&
            kva_after.allocated_page_count ==
                kernel_nvme_storage_runtime.kva_before.allocated_page_count;
        return result.resources_reclaimed ? status : NvmeStatus::ResourceLeak;
    }
    kernel_nvme_storage_runtime.active = true;
    result.active = true;
    root_device = &kernel_nvme_storage_runtime.root_device;
    swap_device = &kernel_nvme_storage_runtime.swap_device;
    root_asynchronous_device = &kernel_nvme_storage_runtime.root_device;
    swap_asynchronous_device = &kernel_nvme_storage_runtime.swap_device;
    return NvmeStatus::Succeeded;
}

NvmeStatus ShutdownNvmeStorageRuntime(NvmeStorageRuntimeResult &result) noexcept {
    if (!kernel_nvme_storage_runtime.active) {
        return NvmeStatus::IoNotReady;
    }
    kernel_nvme_storage_runtime.controller.PopulateRuntimeStatistics(result.controller);
    const NvmeStatus shutdown_status = kernel_nvme_storage_runtime.controller.Shutdown();
    const PhysicalFrameAllocatorStatistics frames_after = GetPhysicalFrameAllocatorStatistics();
    const KernelVirtualAddressAllocatorStatistics kva_after =
        GetKernelVirtualAddressAllocator().Statistics();
    result.resources_reclaimed =
        shutdown_status == NvmeStatus::Succeeded &&
        frames_after.allocated_frame_count ==
            kernel_nvme_storage_runtime.frames_before.allocated_frame_count &&
        kva_after.active_allocation_count ==
            kernel_nvme_storage_runtime.kva_before.active_allocation_count &&
        kva_after.allocated_page_count ==
            kernel_nvme_storage_runtime.kva_before.allocated_page_count;
    result.active = false;
    kernel_nvme_storage_runtime.active = false;
    return result.resources_reclaimed ? NvmeStatus::Succeeded
                                      : NvmeStatus::ResourceReleaseFailed;
}

bool DispatchNvmeMsixInterrupt() noexcept {
    NvmeController *const controller = active_nvme_controller;
    return controller != nullptr && controller->HandleIoInterrupt() == NvmeStatus::Succeeded;
}

void ArmNvmeCommandTimeoutInjection() noexcept {
    nvme_command_timeout_injection_armed = true;
}

}
