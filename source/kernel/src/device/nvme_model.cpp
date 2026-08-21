#include <os/kernel/device/nvme_model.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_NVME_CAP_MQES_MASK = 0xFFFFULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_TIMEOUT_SHIFT_BITS = 24ULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_TIMEOUT_MASK = 0xFFULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_DOORBELL_STRIDE_SHIFT_BITS = 32ULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_DOORBELL_STRIDE_MASK = 0xFULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_NVM_COMMAND_SET_BIT = 1ULL << 37ULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_MPSMIN_SHIFT_BITS = 48ULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_MPSMAX_SHIFT_BITS = 52ULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_MEMORY_PAGE_MASK = 0xFULL;
constexpr uint64_t OS_KERNEL_NVME_CAP_TIMEOUT_UNIT_MILLISECONDS = 500ULL;
constexpr uint64_t OS_KERNEL_NVME_MINIMUM_TIMEOUT_UNIT_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_NVME_BASE_MEMORY_PAGE_SHIFT_BITS = 12ULL;
constexpr uint64_t OS_KERNEL_NVME_DOORBELL_BASE_SHIFT_BITS = 2ULL;
constexpr uint64_t OS_KERNEL_NVME_MAXIMUM_SHIFT_BITS = 63ULL;
constexpr uint32_t OS_KERNEL_NVME_CC_ENABLE_BIT = 1U << 0U;
constexpr uint32_t OS_KERNEL_NVME_CC_IO_SUBMISSION_ENTRY_SHIFT_BITS = 16U;
constexpr uint32_t OS_KERNEL_NVME_CC_IO_COMPLETION_ENTRY_SHIFT_BITS = 20U;
constexpr uint32_t OS_KERNEL_NVME_SUBMISSION_ENTRY_EXPONENT = 6U;
constexpr uint32_t OS_KERNEL_NVME_COMPLETION_ENTRY_EXPONENT = 4U;
constexpr uint64_t OS_KERNEL_NVME_MAXIMUM_ADMIN_QUEUE_DEPTH = 4096ULL;
constexpr uint32_t OS_KERNEL_NVME_AQA_COMPLETION_SHIFT_BITS = 16U;
constexpr uint64_t OS_KERNEL_NVME_COMMAND_DWORD_COUNT = 16ULL;
constexpr uint64_t OS_KERNEL_NVME_COMMAND_IDENTIFIER_SHIFT_BITS = 16ULL;
constexpr uint16_t OS_KERNEL_NVME_INVALID_COMMAND_IDENTIFIER = UINT16_MAX;
constexpr uint8_t OS_KERNEL_NVME_ADMIN_CREATE_IO_SUBMISSION_QUEUE_OPCODE = 0x01U;
constexpr uint8_t OS_KERNEL_NVME_ADMIN_CREATE_IO_COMPLETION_QUEUE_OPCODE = 0x05U;
constexpr uint8_t OS_KERNEL_NVME_ADMIN_SET_FEATURES_OPCODE = 0x09U;
constexpr uint8_t OS_KERNEL_NVME_NUMBER_OF_QUEUES_FEATURE_IDENTIFIER = 0x07U;
constexpr uint8_t OS_KERNEL_NVME_IO_FLUSH_OPCODE = 0x00U;
constexpr uint8_t OS_KERNEL_NVME_IO_WRITE_OPCODE = 0x01U;
constexpr uint8_t OS_KERNEL_NVME_IO_READ_OPCODE = 0x02U;
constexpr uint64_t OS_KERNEL_NVME_MAXIMUM_QUEUE_COUNT = 65535ULL;
constexpr uint64_t OS_KERNEL_NVME_MAXIMUM_LOGICAL_BLOCK_COUNT_PER_COMMAND = 65536ULL;
constexpr uint32_t OS_KERNEL_NVME_QUEUE_SIZE_SHIFT_BITS = 16U;
constexpr uint32_t OS_KERNEL_NVME_QUEUE_PHYSICALLY_CONTIGUOUS_BIT = 1U << 0U;
constexpr uint32_t OS_KERNEL_NVME_COMPLETION_QUEUE_INTERRUPTS_ENABLED_BIT = 1U << 1U;
constexpr uint32_t OS_KERNEL_NVME_COMPLETION_QUEUE_INTERRUPT_VECTOR_SHIFT_BITS = 16U;
constexpr uint16_t OS_KERNEL_NVME_MAXIMUM_MSIX_INTERRUPT_VECTOR = 2047U;
constexpr uint32_t OS_KERNEL_NVME_CREATE_SQ_COMPLETION_QUEUE_IDENTIFIER_SHIFT_BITS = 16U;
constexpr uint32_t OS_KERNEL_NVME_ALLOCATED_COMPLETION_QUEUE_COUNT_SHIFT_BITS = 16U;
constexpr uint32_t OS_KERNEL_NVME_ALLOCATED_QUEUE_COUNT_MASK = 0xFFFFU;
constexpr uint64_t OS_KERNEL_NVME_IO_SUBMISSION_QUEUE_PAGE_CAPACITY =
    OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES / OS_KERNEL_NVME_SUBMISSION_ENTRY_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_NVME_IO_COMPLETION_QUEUE_PAGE_CAPACITY =
    OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES / OS_KERNEL_NVME_COMPLETION_ENTRY_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_NVME_DWORD_BIT_COUNT = 32ULL;
constexpr uint64_t OS_KERNEL_NVME_DWORD_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_KERNEL_NVME_QWORD_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_NVME_BYTE_BIT_COUNT = 8ULL;
constexpr uint8_t OS_KERNEL_NVME_NIBBLE_MASK = 0x0FU;
constexpr uint8_t OS_KERNEL_NVME_HIGH_NIBBLE_SHIFT_BITS = 4U;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_PHASE_BIT = 1ULL << 16ULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_COMMAND_IDENTIFIER_MASK = 0xFFFFULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_QUEUE_HEAD_MASK = 0xFFFFULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_QUEUE_IDENTIFIER_SHIFT_BITS = 16ULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_STATUS_SHIFT_BITS = 17ULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_STATUS_CODE_MASK = 0xFFULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_STATUS_TYPE_SHIFT_BITS = 8ULL;
constexpr uint64_t OS_KERNEL_NVME_COMPLETION_STATUS_TYPE_MASK = 0x7ULL;
constexpr uint64_t OS_KERNEL_NVME_CONTROLLER_VENDOR_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_NVME_CONTROLLER_MDTS_OFFSET_BYTES = 77ULL;
constexpr uint64_t OS_KERNEL_NVME_CONTROLLER_SQES_OFFSET_BYTES = 512ULL;
constexpr uint64_t OS_KERNEL_NVME_CONTROLLER_CQES_OFFSET_BYTES = 513ULL;
constexpr uint64_t OS_KERNEL_NVME_CONTROLLER_NAMESPACE_COUNT_OFFSET_BYTES = 516ULL;
constexpr uint64_t OS_KERNEL_NVME_NAMESPACE_SIZE_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_NVME_NAMESPACE_CAPACITY_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_NVME_NAMESPACE_LBA_FORMAT_COUNT_OFFSET_BYTES = 25ULL;
constexpr uint64_t OS_KERNEL_NVME_NAMESPACE_FORMATTED_LBA_OFFSET_BYTES = 26ULL;
constexpr uint64_t OS_KERNEL_NVME_NAMESPACE_LBA_FORMATS_OFFSET_BYTES = 128ULL;
constexpr uint64_t OS_KERNEL_NVME_NAMESPACE_LBA_FORMAT_SIZE_BYTES = 4ULL;
constexpr uint8_t OS_KERNEL_NVME_NAMESPACE_FORMAT_INDEX_MASK = 0x0FU;
constexpr uint8_t OS_KERNEL_NVME_NAMESPACE_EXTENDED_METADATA_BIT = 1U << 4U;
constexpr uint64_t OS_KERNEL_NVME_LBA_METADATA_MASK = 0xFFFFULL;
constexpr uint64_t OS_KERNEL_NVME_LBA_DATA_SIZE_SHIFT_BITS = 16ULL;
constexpr uint64_t OS_KERNEL_NVME_LBA_DATA_SIZE_MASK = 0xFFULL;

[[nodiscard]] uint16_t LoadLittleEndian16(const uint8_t *const data) noexcept {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 static_cast<uint16_t>(data[1]) << 8U);
}

[[nodiscard]] uint32_t LoadLittleEndian32(const uint8_t *const data) noexcept {
    uint32_t value = 0U;
    for (uint64_t byte_index = 0ULL; byte_index < OS_KERNEL_NVME_DWORD_SIZE_BYTES;
         ++byte_index) {
        value |= static_cast<uint32_t>(data[byte_index]) <<
                 (byte_index * OS_KERNEL_NVME_BYTE_BIT_COUNT);
    }
    return value;
}

[[nodiscard]] uint64_t LoadLittleEndian64(const uint8_t *const data) noexcept {
    uint64_t value = 0ULL;
    for (uint64_t byte_index = 0ULL; byte_index < OS_KERNEL_NVME_QWORD_SIZE_BYTES;
         ++byte_index) {
        value |= static_cast<uint64_t>(data[byte_index]) <<
                 (byte_index * OS_KERNEL_NVME_BYTE_BIT_COUNT);
    }
    return value;
}

[[nodiscard]] bool PowerOfTwo(const uint64_t exponent, uint64_t &value) noexcept {
    if (exponent >= OS_KERNEL_NVME_MAXIMUM_SHIFT_BITS) {
        return false;
    }
    value = 1ULL << exponent;
    return true;
}

[[nodiscard]] bool CommandIdentifierIsValid(const uint16_t command_identifier) noexcept {
    return command_identifier != 0U &&
           command_identifier != OS_KERNEL_NVME_INVALID_COMMAND_IDENTIFIER;
}

[[nodiscard]] bool PagePhysicalAddressIsValid(const uint64_t physical_address) noexcept {
    return physical_address != 0ULL &&
           physical_address % OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES == 0ULL;
}

void ClearSubmissionEntry(NvmeSubmissionEntry &entry) noexcept {
    for (uint64_t dword_index = 0ULL; dword_index < OS_KERNEL_NVME_COMMAND_DWORD_COUNT;
         ++dword_index) {
        entry.dwords[dword_index] = 0U;
    }
}

void SetCommandHeader(const uint8_t opcode, const uint16_t command_identifier,
                      NvmeSubmissionEntry &entry) noexcept {
    entry.dwords[0] = static_cast<uint32_t>(opcode) |
                      static_cast<uint32_t>(command_identifier)
                          << OS_KERNEL_NVME_COMMAND_IDENTIFIER_SHIFT_BITS;
}

void SetPhysicalRegionPage(const uint64_t physical_address,
                           NvmeSubmissionEntry &entry) noexcept {
    entry.dwords[6] = static_cast<uint32_t>(physical_address);
    entry.dwords[7] =
        static_cast<uint32_t>(physical_address >> OS_KERNEL_NVME_DWORD_BIT_COUNT);
}

}

NvmeModelStatus DecodeNvmeControllerCapabilities(
    const uint64_t capabilities_register, NvmeControllerCapabilities &capabilities) noexcept {
    const uint64_t maximum_queue_entry_count =
        (capabilities_register & OS_KERNEL_NVME_CAP_MQES_MASK) + 1ULL;
    const uint64_t timeout_units =
        (capabilities_register >> OS_KERNEL_NVME_CAP_TIMEOUT_SHIFT_BITS) &
        OS_KERNEL_NVME_CAP_TIMEOUT_MASK;
    const uint64_t doorbell_stride_exponent =
        OS_KERNEL_NVME_DOORBELL_BASE_SHIFT_BITS +
        ((capabilities_register >> OS_KERNEL_NVME_CAP_DOORBELL_STRIDE_SHIFT_BITS) &
         OS_KERNEL_NVME_CAP_DOORBELL_STRIDE_MASK);
    const uint64_t minimum_page_exponent =
        OS_KERNEL_NVME_BASE_MEMORY_PAGE_SHIFT_BITS +
        ((capabilities_register >> OS_KERNEL_NVME_CAP_MPSMIN_SHIFT_BITS) &
         OS_KERNEL_NVME_CAP_MEMORY_PAGE_MASK);
    const uint64_t maximum_page_exponent =
        OS_KERNEL_NVME_BASE_MEMORY_PAGE_SHIFT_BITS +
        ((capabilities_register >> OS_KERNEL_NVME_CAP_MPSMAX_SHIFT_BITS) &
         OS_KERNEL_NVME_CAP_MEMORY_PAGE_MASK);
    uint64_t doorbell_stride_bytes = 0ULL;
    uint64_t minimum_memory_page_size_bytes = 0ULL;
    uint64_t maximum_memory_page_size_bytes = 0ULL;
    if (maximum_queue_entry_count < 2ULL ||
        !PowerOfTwo(doorbell_stride_exponent, doorbell_stride_bytes) ||
        !PowerOfTwo(minimum_page_exponent, minimum_memory_page_size_bytes) ||
        !PowerOfTwo(maximum_page_exponent, maximum_memory_page_size_bytes) ||
        minimum_memory_page_size_bytes > maximum_memory_page_size_bytes) {
        return NvmeModelStatus::InvalidCapabilities;
    }
    const uint64_t effective_timeout_units =
        timeout_units == 0ULL ? OS_KERNEL_NVME_MINIMUM_TIMEOUT_UNIT_COUNT : timeout_units;
    capabilities = NvmeControllerCapabilities{
        .maximum_queue_entry_count = maximum_queue_entry_count,
        .ready_timeout_milliseconds =
            effective_timeout_units * OS_KERNEL_NVME_CAP_TIMEOUT_UNIT_MILLISECONDS,
        .doorbell_stride_bytes = doorbell_stride_bytes,
        .minimum_memory_page_size_bytes = minimum_memory_page_size_bytes,
        .maximum_memory_page_size_bytes = maximum_memory_page_size_bytes,
        .nvm_command_set_supported =
            (capabilities_register & OS_KERNEL_NVME_CAP_NVM_COMMAND_SET_BIT) != 0ULL,
    };
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus
BuildNvmeControllerConfiguration(const NvmeControllerCapabilities &capabilities,
                                 uint32_t &configuration) noexcept {
    if (capabilities.minimum_memory_page_size_bytes > OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES ||
        capabilities.maximum_memory_page_size_bytes < OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES) {
        return NvmeModelStatus::UnsupportedMemoryPageSize;
    }
    if (!capabilities.nvm_command_set_supported) {
        return NvmeModelStatus::UnsupportedCommandSet;
    }
    configuration = OS_KERNEL_NVME_CC_ENABLE_BIT |
                    (OS_KERNEL_NVME_SUBMISSION_ENTRY_EXPONENT
                     << OS_KERNEL_NVME_CC_IO_SUBMISSION_ENTRY_SHIFT_BITS) |
                    (OS_KERNEL_NVME_COMPLETION_ENTRY_EXPONENT
                     << OS_KERNEL_NVME_CC_IO_COMPLETION_ENTRY_SHIFT_BITS);
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus BuildNvmeAdminQueueAttributes(const uint64_t queue_depth,
                                              uint32_t &attributes) noexcept {
    if (queue_depth < 2ULL || queue_depth > OS_KERNEL_NVME_MAXIMUM_ADMIN_QUEUE_DEPTH) {
        return NvmeModelStatus::InvalidQueueDepth;
    }
    const uint32_t zero_based_depth = static_cast<uint32_t>(queue_depth - 1ULL);
    attributes = zero_based_depth |
                 zero_based_depth << OS_KERNEL_NVME_AQA_COMPLETION_SHIFT_BITS;
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus BuildNvmeIdentifyCommand(const uint16_t command_identifier,
                                         const uint32_t namespace_identifier,
                                         const uint8_t controller_namespace_selector,
                                         const uint64_t data_physical_address,
                                         NvmeSubmissionEntry &entry) noexcept {
    if (!CommandIdentifierIsValid(command_identifier)) {
        return NvmeModelStatus::InvalidCommandIdentifier;
    }
    if (!PagePhysicalAddressIsValid(data_physical_address)) {
        return NvmeModelStatus::InvalidPhysicalAddress;
    }
    if (controller_namespace_selector != OS_KERNEL_NVME_IDENTIFY_NAMESPACE_CNS &&
        controller_namespace_selector != OS_KERNEL_NVME_IDENTIFY_CONTROLLER_CNS) {
        return NvmeModelStatus::InvalidIdentifySelector;
    }
    if ((controller_namespace_selector == OS_KERNEL_NVME_IDENTIFY_CONTROLLER_CNS &&
         namespace_identifier != 0U) ||
        (controller_namespace_selector == OS_KERNEL_NVME_IDENTIFY_NAMESPACE_CNS &&
         namespace_identifier == 0U)) {
        return NvmeModelStatus::InvalidIdentifySelector;
    }
    ClearSubmissionEntry(entry);
    SetCommandHeader(OS_KERNEL_NVME_ADMIN_IDENTIFY_OPCODE, command_identifier, entry);
    entry.dwords[1] = namespace_identifier;
    SetPhysicalRegionPage(data_physical_address, entry);
    entry.dwords[10] = static_cast<uint32_t>(controller_namespace_selector);
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus BuildNvmeSetQueueCountCommand(
    const uint16_t command_identifier, const uint64_t submission_queue_count,
    const uint64_t completion_queue_count, NvmeSubmissionEntry &entry) noexcept {
    if (!CommandIdentifierIsValid(command_identifier)) {
        return NvmeModelStatus::InvalidCommandIdentifier;
    }
    if (submission_queue_count == 0ULL ||
        submission_queue_count > OS_KERNEL_NVME_MAXIMUM_QUEUE_COUNT ||
        completion_queue_count == 0ULL ||
        completion_queue_count > OS_KERNEL_NVME_MAXIMUM_QUEUE_COUNT) {
        return NvmeModelStatus::InvalidQueueCount;
    }
    ClearSubmissionEntry(entry);
    SetCommandHeader(OS_KERNEL_NVME_ADMIN_SET_FEATURES_OPCODE, command_identifier, entry);
    entry.dwords[10] = OS_KERNEL_NVME_NUMBER_OF_QUEUES_FEATURE_IDENTIFIER;
    entry.dwords[11] =
        static_cast<uint32_t>(submission_queue_count - 1ULL) |
        static_cast<uint32_t>(completion_queue_count - 1ULL)
            << OS_KERNEL_NVME_ALLOCATED_COMPLETION_QUEUE_COUNT_SHIFT_BITS;
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus BuildNvmeCreateIoCompletionQueueCommand(
    const uint16_t command_identifier, const uint16_t queue_identifier,
    const uint64_t queue_depth, const uint64_t queue_physical_address,
    const bool interrupts_enabled, const uint16_t interrupt_vector,
    NvmeSubmissionEntry &entry) noexcept {
    if (!CommandIdentifierIsValid(command_identifier)) {
        return NvmeModelStatus::InvalidCommandIdentifier;
    }
    if (queue_identifier == 0U) {
        return NvmeModelStatus::InvalidQueueIdentifier;
    }
    if (queue_depth < 2ULL || queue_depth > OS_KERNEL_NVME_IO_COMPLETION_QUEUE_PAGE_CAPACITY) {
        return NvmeModelStatus::InvalidQueueDepth;
    }
    if (!PagePhysicalAddressIsValid(queue_physical_address)) {
        return NvmeModelStatus::InvalidPhysicalAddress;
    }
    if (interrupts_enabled &&
        interrupt_vector > OS_KERNEL_NVME_MAXIMUM_MSIX_INTERRUPT_VECTOR) {
        return NvmeModelStatus::InvalidQueueIdentifier;
    }
    ClearSubmissionEntry(entry);
    SetCommandHeader(OS_KERNEL_NVME_ADMIN_CREATE_IO_COMPLETION_QUEUE_OPCODE,
                     command_identifier, entry);
    SetPhysicalRegionPage(queue_physical_address, entry);
    entry.dwords[10] = static_cast<uint32_t>(queue_identifier) |
                       static_cast<uint32_t>(queue_depth - 1ULL)
                           << OS_KERNEL_NVME_QUEUE_SIZE_SHIFT_BITS;
    entry.dwords[11] =
        OS_KERNEL_NVME_QUEUE_PHYSICALLY_CONTIGUOUS_BIT |
        (interrupts_enabled ? OS_KERNEL_NVME_COMPLETION_QUEUE_INTERRUPTS_ENABLED_BIT : 0U) |
        static_cast<uint32_t>(interrupt_vector)
            << OS_KERNEL_NVME_COMPLETION_QUEUE_INTERRUPT_VECTOR_SHIFT_BITS;
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus BuildNvmeCreateIoSubmissionQueueCommand(
    const uint16_t command_identifier, const uint16_t queue_identifier,
    const uint16_t completion_queue_identifier, const uint64_t queue_depth,
    const uint64_t queue_physical_address, NvmeSubmissionEntry &entry) noexcept {
    if (!CommandIdentifierIsValid(command_identifier)) {
        return NvmeModelStatus::InvalidCommandIdentifier;
    }
    if (queue_identifier == 0U || completion_queue_identifier == 0U) {
        return NvmeModelStatus::InvalidQueueIdentifier;
    }
    if (queue_depth < 2ULL || queue_depth > OS_KERNEL_NVME_IO_SUBMISSION_QUEUE_PAGE_CAPACITY) {
        return NvmeModelStatus::InvalidQueueDepth;
    }
    if (!PagePhysicalAddressIsValid(queue_physical_address)) {
        return NvmeModelStatus::InvalidPhysicalAddress;
    }
    ClearSubmissionEntry(entry);
    SetCommandHeader(OS_KERNEL_NVME_ADMIN_CREATE_IO_SUBMISSION_QUEUE_OPCODE,
                     command_identifier, entry);
    SetPhysicalRegionPage(queue_physical_address, entry);
    entry.dwords[10] = static_cast<uint32_t>(queue_identifier) |
                       static_cast<uint32_t>(queue_depth - 1ULL)
                           << OS_KERNEL_NVME_QUEUE_SIZE_SHIFT_BITS;
    entry.dwords[11] = OS_KERNEL_NVME_QUEUE_PHYSICALLY_CONTIGUOUS_BIT |
                       static_cast<uint32_t>(completion_queue_identifier)
                           << OS_KERNEL_NVME_CREATE_SQ_COMPLETION_QUEUE_IDENTIFIER_SHIFT_BITS;
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus BuildNvmeIoCommand(
    const NvmeIoOperation operation, const uint16_t command_identifier,
    const uint32_t namespace_identifier, const uint64_t logical_block_address,
    const uint64_t logical_block_count, const uint64_t first_data_pointer,
    const uint64_t second_data_pointer,
    NvmeSubmissionEntry &entry) noexcept {
    if (!CommandIdentifierIsValid(command_identifier)) {
        return NvmeModelStatus::InvalidCommandIdentifier;
    }
    if (namespace_identifier == 0U) {
        return NvmeModelStatus::InvalidTransfer;
    }
    if (operation != NvmeIoOperation::Read && operation != NvmeIoOperation::Write &&
        operation != NvmeIoOperation::Flush) {
        return NvmeModelStatus::InvalidTransfer;
    }
    uint8_t opcode = OS_KERNEL_NVME_IO_FLUSH_OPCODE;
    if (operation == NvmeIoOperation::Flush) {
        if (logical_block_address != 0ULL || logical_block_count != 0ULL ||
            first_data_pointer != 0ULL || second_data_pointer != 0ULL) {
            return NvmeModelStatus::InvalidTransfer;
        }
    } else {
        if (logical_block_count == 0ULL ||
            logical_block_count > OS_KERNEL_NVME_MAXIMUM_LOGICAL_BLOCK_COUNT_PER_COMMAND ||
            !PagePhysicalAddressIsValid(first_data_pointer) ||
            (second_data_pointer != 0ULL &&
             !PagePhysicalAddressIsValid(second_data_pointer))) {
            return NvmeModelStatus::InvalidTransfer;
        }
        opcode = operation == NvmeIoOperation::Read ? OS_KERNEL_NVME_IO_READ_OPCODE
                                                    : OS_KERNEL_NVME_IO_WRITE_OPCODE;
    }
    ClearSubmissionEntry(entry);
    SetCommandHeader(opcode, command_identifier, entry);
    entry.dwords[1] = namespace_identifier;
    if (operation != NvmeIoOperation::Flush) {
        SetPhysicalRegionPage(first_data_pointer, entry);
        entry.dwords[8] = static_cast<uint32_t>(second_data_pointer);
        entry.dwords[9] =
            static_cast<uint32_t>(second_data_pointer >> OS_KERNEL_NVME_DWORD_BIT_COUNT);
        entry.dwords[10] = static_cast<uint32_t>(logical_block_address);
        entry.dwords[11] =
            static_cast<uint32_t>(logical_block_address >> OS_KERNEL_NVME_DWORD_BIT_COUNT);
        entry.dwords[12] = static_cast<uint32_t>(logical_block_count - 1ULL);
    }
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus BuildNvmePrpMapping(
    const uint64_t *const data_page_physical_addresses,
    const uint64_t data_page_count, const uint64_t prp_list_physical_address,
    uint64_t *const prp_list_entries, const uint64_t prp_list_capacity,
    NvmePrpMapping &mapping) noexcept {
    mapping = NvmePrpMapping{};
    if (data_page_physical_addresses == nullptr || data_page_count == 0ULL ||
        data_page_count > OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT) {
        return NvmeModelStatus::InvalidPrpList;
    }
    for (uint64_t page_index = 0ULL; page_index < data_page_count; ++page_index) {
        if (!PagePhysicalAddressIsValid(data_page_physical_addresses[page_index])) {
            return NvmeModelStatus::InvalidPhysicalAddress;
        }
    }
    mapping.first_data_pointer = data_page_physical_addresses[0];
    if (data_page_count == 1ULL) {
        return NvmeModelStatus::Succeeded;
    }
    if (data_page_count == 2ULL) {
        mapping.second_data_pointer = data_page_physical_addresses[1];
        return NvmeModelStatus::Succeeded;
    }
    const uint64_t required_list_entry_count = data_page_count - 1ULL;
    if (!PagePhysicalAddressIsValid(prp_list_physical_address) ||
        prp_list_entries == nullptr || prp_list_capacity < required_list_entry_count) {
        mapping = NvmePrpMapping{};
        return NvmeModelStatus::InvalidPrpList;
    }
    for (uint64_t list_index = 0ULL; list_index < required_list_entry_count; ++list_index) {
        prp_list_entries[list_index] = data_page_physical_addresses[list_index + 1ULL];
    }
    mapping.second_data_pointer = prp_list_physical_address;
    mapping.list_entry_count = required_list_entry_count;
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus DecodeNvmeCompletion(const NvmeCompletionEntry &entry,
                                     const bool expected_phase,
                                     const uint16_t expected_command_identifier,
                                     NvmeCompletionResult &result) noexcept {
    const NvmeModelStatus status =
        DecodeNvmeCompletionEntry(entry, expected_phase, result);
    if (status == NvmeModelStatus::CompletionNotReady) {
        return status;
    }
    if (result.command_identifier != expected_command_identifier) {
        return NvmeModelStatus::CompletionMismatch;
    }
    return status;
}

NvmeModelStatus DecodeNvmeCompletionEntry(const NvmeCompletionEntry &entry,
                                          const bool expected_phase,
                                          NvmeCompletionResult &result) noexcept {
    const bool phase = (static_cast<uint64_t>(entry.dwords[3]) &
                        OS_KERNEL_NVME_COMPLETION_PHASE_BIT) != 0ULL;
    if (phase != expected_phase) {
        return NvmeModelStatus::CompletionNotReady;
    }
    const uint16_t command_identifier = static_cast<uint16_t>(
        static_cast<uint64_t>(entry.dwords[3]) &
        OS_KERNEL_NVME_COMPLETION_COMMAND_IDENTIFIER_MASK);
    const uint64_t status_field =
        static_cast<uint64_t>(entry.dwords[3]) >> OS_KERNEL_NVME_COMPLETION_STATUS_SHIFT_BITS;
    result = NvmeCompletionResult{
        .command_specific = entry.dwords[0],
        .command_identifier = command_identifier,
        .submission_queue_identifier = static_cast<uint16_t>(
            static_cast<uint64_t>(entry.dwords[2]) >>
            OS_KERNEL_NVME_COMPLETION_QUEUE_IDENTIFIER_SHIFT_BITS),
        .submission_queue_head = static_cast<uint16_t>(
            static_cast<uint64_t>(entry.dwords[2]) &
            OS_KERNEL_NVME_COMPLETION_QUEUE_HEAD_MASK),
        .status_code = static_cast<uint16_t>(status_field &
                                            OS_KERNEL_NVME_COMPLETION_STATUS_CODE_MASK),
        .status_code_type = static_cast<uint8_t>(
            (status_field >> OS_KERNEL_NVME_COMPLETION_STATUS_TYPE_SHIFT_BITS) &
            OS_KERNEL_NVME_COMPLETION_STATUS_TYPE_MASK),
    };
    return status_field == 0ULL ? NvmeModelStatus::Succeeded
                                : NvmeModelStatus::CompletionFailed;
}

NvmeModelStatus DecodeNvmeAllocatedQueueCounts(
    const NvmeCompletionResult &completion,
    NvmeAllocatedQueueCounts &queue_counts) noexcept {
    const uint32_t zero_based_submission_queue_count =
        completion.command_specific & OS_KERNEL_NVME_ALLOCATED_QUEUE_COUNT_MASK;
    const uint32_t zero_based_completion_queue_count =
        completion.command_specific >>
        OS_KERNEL_NVME_ALLOCATED_COMPLETION_QUEUE_COUNT_SHIFT_BITS;
    if (zero_based_submission_queue_count == OS_KERNEL_NVME_ALLOCATED_QUEUE_COUNT_MASK ||
        zero_based_completion_queue_count == OS_KERNEL_NVME_ALLOCATED_QUEUE_COUNT_MASK) {
        queue_counts = NvmeAllocatedQueueCounts{};
        return NvmeModelStatus::InvalidQueueCount;
    }
    queue_counts = NvmeAllocatedQueueCounts{
        .submission_queue_count =
            static_cast<uint64_t>(zero_based_submission_queue_count) +
            1ULL,
        .completion_queue_count =
            static_cast<uint64_t>(zero_based_completion_queue_count) +
            1ULL,
    };
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus ParseNvmeControllerIdentity(
    const uint8_t *const data, const uint64_t length_bytes,
    const NvmeControllerCapabilities &capabilities, NvmeControllerIdentity &identity) noexcept {
    if (data == nullptr || length_bytes != OS_KERNEL_NVME_IDENTIFY_DATA_SIZE_BYTES) {
        return NvmeModelStatus::InvalidIdentifyData;
    }
    const uint8_t submission_entry_sizes = data[OS_KERNEL_NVME_CONTROLLER_SQES_OFFSET_BYTES];
    const uint8_t completion_entry_sizes = data[OS_KERNEL_NVME_CONTROLLER_CQES_OFFSET_BYTES];
    const uint64_t minimum_submission_exponent =
        submission_entry_sizes & OS_KERNEL_NVME_NIBBLE_MASK;
    const uint64_t maximum_submission_exponent =
        submission_entry_sizes >> OS_KERNEL_NVME_HIGH_NIBBLE_SHIFT_BITS;
    const uint64_t minimum_completion_exponent =
        completion_entry_sizes & OS_KERNEL_NVME_NIBBLE_MASK;
    const uint64_t maximum_completion_exponent =
        completion_entry_sizes >> OS_KERNEL_NVME_HIGH_NIBBLE_SHIFT_BITS;
    uint64_t minimum_submission_size = 0ULL;
    uint64_t maximum_submission_size = 0ULL;
    uint64_t minimum_completion_size = 0ULL;
    uint64_t maximum_completion_size = 0ULL;
    if (!PowerOfTwo(minimum_submission_exponent, minimum_submission_size) ||
        !PowerOfTwo(maximum_submission_exponent, maximum_submission_size) ||
        !PowerOfTwo(minimum_completion_exponent, minimum_completion_size) ||
        !PowerOfTwo(maximum_completion_exponent, maximum_completion_size) ||
        minimum_submission_size > OS_KERNEL_NVME_SUBMISSION_ENTRY_SIZE_BYTES ||
        maximum_submission_size < OS_KERNEL_NVME_SUBMISSION_ENTRY_SIZE_BYTES ||
        minimum_completion_size > OS_KERNEL_NVME_COMPLETION_ENTRY_SIZE_BYTES ||
        maximum_completion_size < OS_KERNEL_NVME_COMPLETION_ENTRY_SIZE_BYTES) {
        return NvmeModelStatus::InvalidIdentifyData;
    }
    const uint8_t maximum_transfer_exponent = data[OS_KERNEL_NVME_CONTROLLER_MDTS_OFFSET_BYTES];
    uint64_t maximum_transfer_size_bytes = UINT64_MAX;
    if (maximum_transfer_exponent != 0U) {
        if (maximum_transfer_exponent >= OS_KERNEL_NVME_MAXIMUM_SHIFT_BITS ||
            capabilities.minimum_memory_page_size_bytes >
                (UINT64_MAX >> maximum_transfer_exponent)) {
            return NvmeModelStatus::InvalidIdentifyData;
        }
        maximum_transfer_size_bytes =
            capabilities.minimum_memory_page_size_bytes << maximum_transfer_exponent;
    }
    const uint64_t namespace_count = static_cast<uint64_t>(LoadLittleEndian32(
        data + OS_KERNEL_NVME_CONTROLLER_NAMESPACE_COUNT_OFFSET_BYTES));
    if (namespace_count == 0ULL) {
        return NvmeModelStatus::InvalidIdentifyData;
    }
    identity = NvmeControllerIdentity{
        .vendor_identifier =
            LoadLittleEndian16(data + OS_KERNEL_NVME_CONTROLLER_VENDOR_OFFSET_BYTES),
        .namespace_count = namespace_count,
        .maximum_transfer_size_bytes = maximum_transfer_size_bytes,
        .minimum_submission_entry_size_bytes = minimum_submission_size,
        .maximum_submission_entry_size_bytes = maximum_submission_size,
        .minimum_completion_entry_size_bytes = minimum_completion_size,
        .maximum_completion_entry_size_bytes = maximum_completion_size,
    };
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus ParseNvmeNamespaceIdentity(const uint8_t *const data,
                                           const uint64_t length_bytes,
                                           NvmeNamespaceIdentity &identity) noexcept {
    if (data == nullptr || length_bytes != OS_KERNEL_NVME_IDENTIFY_DATA_SIZE_BYTES) {
        return NvmeModelStatus::InvalidIdentifyData;
    }
    const uint64_t logical_block_count =
        LoadLittleEndian64(data + OS_KERNEL_NVME_NAMESPACE_SIZE_OFFSET_BYTES);
    const uint64_t capacity_block_count =
        LoadLittleEndian64(data + OS_KERNEL_NVME_NAMESPACE_CAPACITY_OFFSET_BYTES);
    const uint64_t lba_format_count =
        static_cast<uint64_t>(data[OS_KERNEL_NVME_NAMESPACE_LBA_FORMAT_COUNT_OFFSET_BYTES]) + 1ULL;
    const uint8_t formatted_lba = data[OS_KERNEL_NVME_NAMESPACE_FORMATTED_LBA_OFFSET_BYTES];
    const uint64_t formatted_lba_index =
        static_cast<uint64_t>(formatted_lba & OS_KERNEL_NVME_NAMESPACE_FORMAT_INDEX_MASK);
    if (logical_block_count == 0ULL || capacity_block_count == 0ULL ||
        capacity_block_count > logical_block_count || formatted_lba_index >= lba_format_count) {
        return NvmeModelStatus::InvalidIdentifyData;
    }
    const uint64_t format_offset = OS_KERNEL_NVME_NAMESPACE_LBA_FORMATS_OFFSET_BYTES +
                                   formatted_lba_index *
                                       OS_KERNEL_NVME_NAMESPACE_LBA_FORMAT_SIZE_BYTES;
    if (format_offset + OS_KERNEL_NVME_NAMESPACE_LBA_FORMAT_SIZE_BYTES > length_bytes) {
        return NvmeModelStatus::InvalidIdentifyData;
    }
    const uint32_t format = LoadLittleEndian32(data + format_offset);
    const uint64_t data_size_exponent =
        (static_cast<uint64_t>(format) >> OS_KERNEL_NVME_LBA_DATA_SIZE_SHIFT_BITS) &
        OS_KERNEL_NVME_LBA_DATA_SIZE_MASK;
    uint64_t logical_block_size_bytes = 0ULL;
    if (!PowerOfTwo(data_size_exponent, logical_block_size_bytes)) {
        return NvmeModelStatus::InvalidIdentifyData;
    }
    identity = NvmeNamespaceIdentity{
        .logical_block_count = logical_block_count,
        .capacity_block_count = capacity_block_count,
        .logical_block_size_bytes = logical_block_size_bytes,
        .metadata_size_bytes = static_cast<uint64_t>(format) & OS_KERNEL_NVME_LBA_METADATA_MASK,
        .formatted_lba_index = formatted_lba_index,
        .extended_metadata =
            (formatted_lba & OS_KERNEL_NVME_NAMESPACE_EXTENDED_METADATA_BIT) != 0U,
    };
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus CalculateNvmeBlockDeviceGeometry(
    const NvmeControllerIdentity &controller_identity,
    const NvmeNamespaceIdentity &namespace_identity,
    BlockDeviceGeometry &geometry) noexcept {
    geometry = BlockDeviceGeometry{};
    if (namespace_identity.logical_block_count == 0ULL ||
        namespace_identity.logical_block_size_bytes == 0ULL ||
        namespace_identity.logical_block_size_bytes > OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES ||
        OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES %
                namespace_identity.logical_block_size_bytes !=
            0ULL ||
        namespace_identity.metadata_size_bytes != 0ULL ||
        namespace_identity.extended_metadata) {
        return NvmeModelStatus::InvalidTransfer;
    }
    const uint64_t bounce_buffer_block_count =
        (OS_KERNEL_NVME_MEMORY_PAGE_SIZE_BYTES * OS_KERNEL_NVME_MAXIMUM_DATA_PAGE_COUNT) /
        namespace_identity.logical_block_size_bytes;
    const uint64_t controller_transfer_block_count =
        controller_identity.maximum_transfer_size_bytes == UINT64_MAX
            ? bounce_buffer_block_count
            : controller_identity.maximum_transfer_size_bytes /
                  namespace_identity.logical_block_size_bytes;
    uint64_t maximum_transfer_block_count =
        bounce_buffer_block_count < controller_transfer_block_count
            ? bounce_buffer_block_count
            : controller_transfer_block_count;
    if (maximum_transfer_block_count > namespace_identity.logical_block_count) {
        maximum_transfer_block_count = namespace_identity.logical_block_count;
    }
    if (maximum_transfer_block_count == 0ULL) {
        return NvmeModelStatus::InvalidTransfer;
    }
    geometry = BlockDeviceGeometry{
        .logical_block_size_bytes = namespace_identity.logical_block_size_bytes,
        .logical_block_count = namespace_identity.logical_block_count,
        .maximum_transfer_block_count = maximum_transfer_block_count,
        .maximum_outstanding_request_count =
            OS_KERNEL_NVME_MAXIMUM_OUTSTANDING_REQUEST_COUNT,
        .write_supported = true,
        .flush_supported = true,
    };
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus SelectNvmeIoQueueDepth(
    const NvmeControllerCapabilities &capabilities, uint64_t &queue_depth) noexcept {
    queue_depth = capabilities.maximum_queue_entry_count < OS_KERNEL_NVME_MAXIMUM_IO_QUEUE_DEPTH
                      ? capabilities.maximum_queue_entry_count
                      : OS_KERNEL_NVME_MAXIMUM_IO_QUEUE_DEPTH;
    if (queue_depth < 2ULL || queue_depth > OS_KERNEL_NVME_IO_SUBMISSION_QUEUE_PAGE_CAPACITY ||
        queue_depth > OS_KERNEL_NVME_IO_COMPLETION_QUEUE_PAGE_CAPACITY) {
        queue_depth = 0ULL;
        return NvmeModelStatus::InvalidQueueDepth;
    }
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus AdvanceNvmeSubmissionTail(const uint64_t queue_depth,
                                          uint64_t &tail_index) noexcept {
    if (queue_depth < 2ULL || tail_index >= queue_depth) {
        return NvmeModelStatus::InvalidQueueState;
    }
    tail_index = tail_index + 1ULL == queue_depth ? 0ULL : tail_index + 1ULL;
    return NvmeModelStatus::Succeeded;
}

NvmeModelStatus AdvanceNvmeCompletionHead(const uint64_t queue_depth, uint64_t &head_index,
                                          bool &expected_phase) noexcept {
    if (queue_depth < 2ULL || head_index >= queue_depth) {
        return NvmeModelStatus::InvalidQueueState;
    }
    if (head_index + 1ULL == queue_depth) {
        head_index = 0ULL;
        expected_phase = !expected_phase;
    } else {
        ++head_index;
    }
    return NvmeModelStatus::Succeeded;
}

}
