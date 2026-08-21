#pragma once

#include <os/kernel/device/block_device.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES = 4096ULL;
inline constexpr uint64_t OS_KERNEL_NVME_SUBMISSION_ENTRY_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_KERNEL_NVME_COMPLETION_ENTRY_SIZE_BYTES = 16ULL;
inline constexpr uint64_t OS_KERNEL_NVME_ADMIN_QUEUE_DEPTH = 2ULL;
inline constexpr uint64_t OS_KERNEL_NVME_MAXIMUM_IO_QUEUE_DEPTH = 64ULL;
inline constexpr uint64_t OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT = 16ULL;
inline constexpr uint64_t OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT = 4ULL;
inline constexpr uint64_t OS_KERNEL_NVME_IDENTIFY_DATA_SIZE_BYTES = 4096ULL;
inline constexpr uint16_t OS_KERNEL_NVME_FIRST_IO_COMMAND_IDENTIFIER = 1U;
inline constexpr uint8_t OS_KERNEL_NVME_ADMIN_IDENTIFY_OPCODE = 0x06U;
inline constexpr uint8_t OS_KERNEL_NVME_IDENTIFY_NAMESPACE_CNS = 0x00U;
inline constexpr uint8_t OS_KERNEL_NVME_IDENTIFY_CONTROLLER_CNS = 0x01U;

struct NvmeSubmissionEntry final {
    uint32_t dwords[16];
};

struct NvmeCompletionEntry final {
    uint32_t dwords[4];
};

static_assert(sizeof(NvmeSubmissionEntry) == OS_KERNEL_NVME_SUBMISSION_ENTRY_SIZE_BYTES);
static_assert(sizeof(NvmeCompletionEntry) == OS_KERNEL_NVME_COMPLETION_ENTRY_SIZE_BYTES);

struct NvmeControllerCapabilities final {
    uint64_t maximum_queue_entry_count;
    uint64_t ready_timeout_milliseconds;
    uint64_t doorbell_stride_bytes;
    uint64_t minimum_memory_page_size_bytes;
    uint64_t maximum_memory_page_size_bytes;
    bool nvm_command_set_supported;
};

enum class NvmeModelStatus : uint64_t {
    Succeeded,
    InvalidCapabilities,
    UnsupportedMemoryPageSize,
    UnsupportedCommandSet,
    InvalidQueueDepth,
    InvalidQueueCount,
    InvalidQueueIdentifier,
    InvalidCommandIdentifier,
    InvalidPhysicalAddress,
    InvalidIdentifySelector,
    CompletionNotReady,
    CompletionMismatch,
    CompletionFailed,
    InvalidIdentifyData,
    InvalidQueueState,
    InvalidTransfer,
    InvalidPrpList,
};

[[nodiscard]] NvmeModelStatus DecodeNvmeControllerCapabilities(
    uint64_t capabilities_register, NvmeControllerCapabilities &capabilities) noexcept;
[[nodiscard]] NvmeModelStatus
BuildNvmeControllerConfiguration(const NvmeControllerCapabilities &capabilities,
                                 uint32_t &configuration) noexcept;
[[nodiscard]] NvmeModelStatus BuildNvmeAdminQueueAttributes(uint64_t queue_depth,
                                                            uint32_t &attributes) noexcept;
[[nodiscard]] NvmeModelStatus BuildNvmeIdentifyCommand(uint16_t command_identifier,
                                                       uint32_t namespace_identifier,
                                                       uint8_t controller_namespace_selector,
                                                       uint64_t data_physical_address,
                                                       NvmeSubmissionEntry &entry) noexcept;
[[nodiscard]] NvmeModelStatus BuildNvmeSetQueueCountCommand(
    uint16_t command_identifier, uint64_t submission_queue_count,
    uint64_t completion_queue_count, NvmeSubmissionEntry &entry) noexcept;
[[nodiscard]] NvmeModelStatus BuildNvmeCreateIoCompletionQueueCommand(
    uint16_t command_identifier, uint16_t queue_identifier, uint64_t queue_depth,
    uint64_t queue_physical_address, bool interrupts_enabled,
    uint16_t interrupt_vector, NvmeSubmissionEntry &entry) noexcept;
[[nodiscard]] NvmeModelStatus BuildNvmeCreateIoSubmissionQueueCommand(
    uint16_t command_identifier, uint16_t queue_identifier,
    uint16_t completion_queue_identifier, uint64_t queue_depth,
    uint64_t queue_physical_address, NvmeSubmissionEntry &entry) noexcept;

enum class NvmeIoOperation : uint64_t {
    Read,
    Write,
    Flush,
};

[[nodiscard]] NvmeModelStatus BuildNvmeIoCommand(
    NvmeIoOperation operation, uint16_t command_identifier,
    uint32_t namespace_identifier, uint64_t logical_block_address,
    uint64_t logical_block_count, uint64_t first_data_pointer,
    uint64_t second_data_pointer,
    NvmeSubmissionEntry &entry) noexcept;

struct NvmePrpMapping final {
    uint64_t first_data_pointer;
    uint64_t second_data_pointer;
    uint64_t list_entry_count;
};

[[nodiscard]] NvmeModelStatus BuildNvmePrpMapping(
    const uint64_t *data_page_physical_addresses, uint64_t data_page_count,
    uint64_t prp_list_physical_address, uint64_t *prp_list_entries,
    uint64_t prp_list_capacity, NvmePrpMapping &mapping) noexcept;

struct NvmeCompletionResult final {
    uint32_t command_specific;
    uint16_t command_identifier;
    uint16_t submission_queue_identifier;
    uint16_t submission_queue_head;
    uint16_t status_code;
    uint8_t status_code_type;
};

struct NvmeAllocatedQueueCounts final {
    uint64_t submission_queue_count;
    uint64_t completion_queue_count;
};

[[nodiscard]] NvmeModelStatus DecodeNvmeAllocatedQueueCounts(
    const NvmeCompletionResult &completion,
    NvmeAllocatedQueueCounts &queue_counts) noexcept;

[[nodiscard]] NvmeModelStatus DecodeNvmeCompletion(const NvmeCompletionEntry &entry,
                                                   bool expected_phase,
                                                   uint16_t expected_command_identifier,
                                                   NvmeCompletionResult &result) noexcept;
[[nodiscard]] NvmeModelStatus DecodeNvmeCompletionEntry(
    const NvmeCompletionEntry &entry, bool expected_phase,
    NvmeCompletionResult &result) noexcept;

struct NvmeControllerIdentity final {
    uint16_t vendor_identifier;
    uint64_t namespace_count;
    uint64_t maximum_transfer_size_bytes;
    uint64_t minimum_submission_entry_size_bytes;
    uint64_t maximum_submission_entry_size_bytes;
    uint64_t minimum_completion_entry_size_bytes;
    uint64_t maximum_completion_entry_size_bytes;
};

struct NvmeNamespaceIdentity final {
    uint64_t logical_block_count;
    uint64_t capacity_block_count;
    uint64_t logical_block_size_bytes;
    uint64_t metadata_size_bytes;
    uint64_t formatted_lba_index;
    bool extended_metadata;
};

[[nodiscard]] NvmeModelStatus ParseNvmeControllerIdentity(
    const uint8_t *data, uint64_t length_bytes,
    const NvmeControllerCapabilities &capabilities, NvmeControllerIdentity &identity) noexcept;
[[nodiscard]] NvmeModelStatus ParseNvmeNamespaceIdentity(const uint8_t *data,
                                                         uint64_t length_bytes,
                                                         NvmeNamespaceIdentity &identity) noexcept;
[[nodiscard]] NvmeModelStatus CalculateNvmeBlockDeviceGeometry(
    const NvmeControllerIdentity &controller_identity,
    const NvmeNamespaceIdentity &namespace_identity,
    BlockDeviceGeometry &geometry) noexcept;
[[nodiscard]] NvmeModelStatus SelectNvmeIoQueueDepth(
    const NvmeControllerCapabilities &capabilities, uint64_t &queue_depth) noexcept;

[[nodiscard]] NvmeModelStatus AdvanceNvmeSubmissionTail(uint64_t queue_depth,
                                                        uint64_t &tail_index) noexcept;
[[nodiscard]] NvmeModelStatus AdvanceNvmeCompletionHead(uint64_t queue_depth,
                                                        uint64_t &head_index,
                                                        bool &expected_phase) noexcept;

}
