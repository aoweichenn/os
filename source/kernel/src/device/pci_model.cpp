#include <os/kernel/device/pci_model.hpp>

namespace os::kernel {

namespace {

constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_ENABLE_BIT = 1U << 31U;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_RESERVED_HIGH_MASK = 0x7F000000U;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_BUS_SHIFT_BITS = 16U;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_DEVICE_SHIFT_BITS = 11U;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_FUNCTION_SHIFT_BITS = 8U;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_BUS_MASK = 0x00FF0000U;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_DEVICE_MASK = 0x0000F800U;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_FUNCTION_MASK = 0x00000700U;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_REGISTER_MASK = 0x000000FCU;
constexpr uint32_t OS_KERNEL_PCI_CONFIGURATION_RESERVED_LOW_MASK = 0x00000003U;
constexpr uint32_t OS_KERNEL_PCI_VENDOR_IDENTIFIER_MASK = 0x0000FFFFU;
constexpr uint32_t OS_KERNEL_PCI_DEVICE_IDENTIFIER_SHIFT_BITS = 16U;
constexpr uint32_t OS_KERNEL_PCI_REVISION_IDENTIFIER_MASK = 0x000000FFU;
constexpr uint32_t OS_KERNEL_PCI_PROGRAMMING_INTERFACE_SHIFT_BITS = 8U;
constexpr uint32_t OS_KERNEL_PCI_SUBCLASS_SHIFT_BITS = 16U;
constexpr uint32_t OS_KERNEL_PCI_BASE_CLASS_SHIFT_BITS = 24U;
constexpr uint32_t OS_KERNEL_PCI_BYTE_MASK = 0xFFU;
constexpr uint32_t OS_KERNEL_PCI_BAR_IO_SPACE_BIT = 1U << 0U;
constexpr uint32_t OS_KERNEL_PCI_BAR_MEMORY_TYPE_SHIFT_BITS = 1U;
constexpr uint32_t OS_KERNEL_PCI_BAR_MEMORY_TYPE_MASK = 0x3U;
constexpr uint32_t OS_KERNEL_PCI_BAR_MEMORY_TYPE_32 = 0x0U;
constexpr uint32_t OS_KERNEL_PCI_BAR_MEMORY_TYPE_64 = 0x2U;
constexpr uint32_t OS_KERNEL_PCI_BAR_PREFETCHABLE_BIT = 1U << 3U;
constexpr uint32_t OS_KERNEL_PCI_BAR_ADDRESS_MASK = 0xFFFFFFF0U;
constexpr uint64_t OS_KERNEL_PCI_DWORD_BIT_COUNT = 32ULL;
constexpr uint64_t OS_KERNEL_PCI_BAR_MINIMUM_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_PCI_UPPER_DWORD_ADDRESS_MASK = 0xFFFFFFFF00000000ULL;
constexpr uint16_t OS_KERNEL_PCI_CAPABILITY_IDENTIFIER_MASK = 0x00FFU;
constexpr uint8_t OS_KERNEL_PCI_CAPABILITY_INVALID_IDENTIFIER = 0xFFU;
constexpr uint16_t OS_KERNEL_PCI_CAPABILITY_NEXT_SHIFT_BITS = 8U;
constexpr uint64_t OS_KERNEL_PCI_CAPABILITY_MINIMUM_OFFSET_BYTES = 0x40ULL;
constexpr uint64_t OS_KERNEL_PCI_CAPABILITY_MAXIMUM_OFFSET_BYTES = 0xFCULL;
constexpr uint64_t OS_KERNEL_PCI_CAPABILITY_ALIGNMENT_BYTES = 4ULL;
constexpr uint16_t OS_KERNEL_PCI_MSIX_TABLE_SIZE_MASK = 0x07FFU;
constexpr uint16_t OS_KERNEL_PCI_MSIX_FUNCTION_MASK_BIT = 1U << 14U;
constexpr uint16_t OS_KERNEL_PCI_MSIX_ENABLE_BIT = 1U << 15U;
constexpr uint32_t OS_KERNEL_PCI_MSIX_BAR_INDEX_MASK = 0x00000007U;
constexpr uint32_t OS_KERNEL_PCI_MSIX_TABLE_OFFSET_MASK = 0xFFFFFFF8U;
constexpr uint64_t OS_KERNEL_PCI_MAXIMUM_BAR_INDEX = 5ULL;
constexpr uint64_t OS_KERNEL_PCI_MSIX_MESSAGE_ADDRESS_BASE = 0x00000000FEE00000ULL;
constexpr uint64_t OS_KERNEL_PCI_MSIX_DESTINATION_SHIFT_BITS = 12ULL;
constexpr uint64_t OS_KERNEL_PCI_MSIX_MAXIMUM_LOCAL_APIC_IDENTIFIER = 0xFFULL;
constexpr uint64_t OS_KERNEL_PCI_MSIX_MINIMUM_INTERRUPT_VECTOR = 0x20ULL;
constexpr uint64_t OS_KERNEL_PCI_MSIX_MAXIMUM_INTERRUPT_VECTOR = 0xFEULL;
constexpr uint32_t OS_KERNEL_PCI_MSIX_VECTOR_MASK_BIT = 1U << 0U;

[[nodiscard]] bool IsPowerOfTwo(const uint64_t value) noexcept {
    return value != 0ULL && (value & (value - 1ULL)) == 0ULL;
}

[[nodiscard]] uint32_t MemoryType(const uint32_t value) noexcept {
    return (value >> OS_KERNEL_PCI_BAR_MEMORY_TYPE_SHIFT_BITS) &
           OS_KERNEL_PCI_BAR_MEMORY_TYPE_MASK;
}

}

PciConfigurationAddressStatus
EncodePciConfigurationAddress(const PciDeviceAddress &device_address,
                              const uint64_t register_offset_bytes,
                              uint32_t &configuration_address) noexcept {
    if (device_address.bus_number >= OS_KERNEL_PCI_BUS_COUNT) {
        return PciConfigurationAddressStatus::InvalidBus;
    }
    if (device_address.device_number >= OS_KERNEL_PCI_DEVICE_COUNT_PER_BUS) {
        return PciConfigurationAddressStatus::InvalidDevice;
    }
    if (device_address.function_number >= OS_KERNEL_PCI_FUNCTION_COUNT_PER_DEVICE) {
        return PciConfigurationAddressStatus::InvalidFunction;
    }
    if (register_offset_bytes >= OS_KERNEL_PCI_CONFIGURATION_SPACE_SIZE_BYTES ||
        register_offset_bytes % OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES != 0ULL) {
        return PciConfigurationAddressStatus::InvalidRegisterOffset;
    }
    configuration_address =
        OS_KERNEL_PCI_CONFIGURATION_ENABLE_BIT |
        static_cast<uint32_t>(device_address.bus_number
                              << OS_KERNEL_PCI_CONFIGURATION_BUS_SHIFT_BITS) |
        static_cast<uint32_t>(device_address.device_number
                              << OS_KERNEL_PCI_CONFIGURATION_DEVICE_SHIFT_BITS) |
        static_cast<uint32_t>(device_address.function_number
                              << OS_KERNEL_PCI_CONFIGURATION_FUNCTION_SHIFT_BITS) |
        static_cast<uint32_t>(register_offset_bytes);
    return PciConfigurationAddressStatus::Succeeded;
}

PciConfigurationAddressStatus
DecodePciConfigurationAddress(const uint32_t configuration_address,
                              PciDeviceAddress &device_address,
                              uint64_t &register_offset_bytes) noexcept {
    if ((configuration_address & OS_KERNEL_PCI_CONFIGURATION_ENABLE_BIT) == 0U ||
        (configuration_address & OS_KERNEL_PCI_CONFIGURATION_RESERVED_HIGH_MASK) != 0U ||
        (configuration_address & OS_KERNEL_PCI_CONFIGURATION_RESERVED_LOW_MASK) != 0U) {
        return PciConfigurationAddressStatus::InvalidEncodedAddress;
    }
    device_address = PciDeviceAddress{
        .bus_number = static_cast<uint64_t>(
            (configuration_address & OS_KERNEL_PCI_CONFIGURATION_BUS_MASK) >>
            OS_KERNEL_PCI_CONFIGURATION_BUS_SHIFT_BITS),
        .device_number = static_cast<uint64_t>(
            (configuration_address & OS_KERNEL_PCI_CONFIGURATION_DEVICE_MASK) >>
            OS_KERNEL_PCI_CONFIGURATION_DEVICE_SHIFT_BITS),
        .function_number = static_cast<uint64_t>(
            (configuration_address & OS_KERNEL_PCI_CONFIGURATION_FUNCTION_MASK) >>
            OS_KERNEL_PCI_CONFIGURATION_FUNCTION_SHIFT_BITS),
    };
    register_offset_bytes =
        static_cast<uint64_t>(configuration_address & OS_KERNEL_PCI_CONFIGURATION_REGISTER_MASK);
    return PciConfigurationAddressStatus::Succeeded;
}

PciDeviceIdentity DecodePciDeviceIdentity(const uint32_t vendor_device_value,
                                          const uint32_t class_revision_value) noexcept {
    return PciDeviceIdentity{
        .vendor_identifier =
            static_cast<uint16_t>(vendor_device_value & OS_KERNEL_PCI_VENDOR_IDENTIFIER_MASK),
        .device_identifier = static_cast<uint16_t>(
            vendor_device_value >> OS_KERNEL_PCI_DEVICE_IDENTIFIER_SHIFT_BITS),
        .revision_identifier =
            static_cast<uint8_t>(class_revision_value & OS_KERNEL_PCI_REVISION_IDENTIFIER_MASK),
        .programming_interface = static_cast<uint8_t>(
            (class_revision_value >> OS_KERNEL_PCI_PROGRAMMING_INTERFACE_SHIFT_BITS) &
            OS_KERNEL_PCI_BYTE_MASK),
        .subclass_code = static_cast<uint8_t>(
            (class_revision_value >> OS_KERNEL_PCI_SUBCLASS_SHIFT_BITS) &
            OS_KERNEL_PCI_BYTE_MASK),
        .base_class_code = static_cast<uint8_t>(
            (class_revision_value >> OS_KERNEL_PCI_BASE_CLASS_SHIFT_BITS) &
            OS_KERNEL_PCI_BYTE_MASK),
    };
}

bool PciDeviceIsPresent(const PciDeviceIdentity &identity) noexcept {
    return identity.vendor_identifier != OS_KERNEL_PCI_INVALID_VENDOR_IDENTIFIER;
}

bool PciDeviceIsNvmeController(const PciDeviceIdentity &identity) noexcept {
    return PciDeviceIsPresent(identity) &&
           identity.base_class_code == OS_KERNEL_PCI_NVME_BASE_CLASS_CODE &&
           identity.subclass_code == OS_KERNEL_PCI_NVME_SUBCLASS_CODE &&
           identity.programming_interface == OS_KERNEL_PCI_NVME_PROGRAMMING_INTERFACE;
}

PciMemoryBaseAddressStatus
DecodePciMemoryBaseAddress(const uint32_t low_value, const uint32_t high_value,
                           PciMemoryBaseAddress &base_address) noexcept {
    if ((low_value & OS_KERNEL_PCI_BAR_IO_SPACE_BIT) != 0U) {
        return PciMemoryBaseAddressStatus::IoSpaceUnsupported;
    }
    const uint32_t memory_type = MemoryType(low_value);
    if (memory_type != OS_KERNEL_PCI_BAR_MEMORY_TYPE_32 &&
        memory_type != OS_KERNEL_PCI_BAR_MEMORY_TYPE_64) {
        return PciMemoryBaseAddressStatus::MemoryTypeUnsupported;
    }
    const uint64_t low_address = static_cast<uint64_t>(low_value & OS_KERNEL_PCI_BAR_ADDRESS_MASK);
    const uint64_t physical_address =
        memory_type == OS_KERNEL_PCI_BAR_MEMORY_TYPE_64
            ? (static_cast<uint64_t>(high_value) << OS_KERNEL_PCI_DWORD_BIT_COUNT) | low_address
            : low_address;
    if (physical_address == 0ULL) {
        return PciMemoryBaseAddressStatus::Unassigned;
    }
    base_address = PciMemoryBaseAddress{
        .physical_address = physical_address,
        .kind = memory_type == OS_KERNEL_PCI_BAR_MEMORY_TYPE_64
                    ? PciMemoryBaseAddressKind::Memory64
                    : PciMemoryBaseAddressKind::Memory32,
        .prefetchable = (low_value & OS_KERNEL_PCI_BAR_PREFETCHABLE_BIT) != 0U,
    };
    return PciMemoryBaseAddressStatus::Succeeded;
}

PciMemoryBaseAddressStatus
CalculatePciMemoryBaseAddressSize(const uint32_t low_mask, const uint32_t high_mask,
                                  uint64_t &size_bytes) noexcept {
    size_bytes = 0ULL;
    if ((low_mask & OS_KERNEL_PCI_BAR_IO_SPACE_BIT) != 0U) {
        return PciMemoryBaseAddressStatus::IoSpaceUnsupported;
    }
    const uint32_t memory_type = MemoryType(low_mask);
    if (memory_type != OS_KERNEL_PCI_BAR_MEMORY_TYPE_32 &&
        memory_type != OS_KERNEL_PCI_BAR_MEMORY_TYPE_64) {
        return PciMemoryBaseAddressStatus::MemoryTypeUnsupported;
    }
    if (memory_type == OS_KERNEL_PCI_BAR_MEMORY_TYPE_32 &&
        (low_mask & OS_KERNEL_PCI_BAR_ADDRESS_MASK) == 0U) {
        return PciMemoryBaseAddressStatus::InvalidSizeMask;
    }
    const uint64_t address_mask =
        memory_type == OS_KERNEL_PCI_BAR_MEMORY_TYPE_64
            ? (static_cast<uint64_t>(high_mask) << OS_KERNEL_PCI_DWORD_BIT_COUNT) |
                  static_cast<uint64_t>(low_mask & OS_KERNEL_PCI_BAR_ADDRESS_MASK)
            : static_cast<uint64_t>(low_mask & OS_KERNEL_PCI_BAR_ADDRESS_MASK) |
                  OS_KERNEL_PCI_UPPER_DWORD_ADDRESS_MASK;
    const uint64_t calculated_size = ~address_mask + 1ULL;
    if (calculated_size < OS_KERNEL_PCI_BAR_MINIMUM_ALIGNMENT_BYTES ||
        !IsPowerOfTwo(calculated_size)) {
        return PciMemoryBaseAddressStatus::InvalidSizeMask;
    }
    size_bytes = calculated_size;
    return PciMemoryBaseAddressStatus::Succeeded;
}

PciMemoryResourceStatus AllocatePciMemoryResource(const uint64_t window_begin_address,
                                                  const uint64_t window_end_address,
                                                  const uint64_t size_bytes,
                                                  uint64_t &physical_address) noexcept {
    physical_address = 0ULL;
    if (window_begin_address >= window_end_address) {
        return PciMemoryResourceStatus::InvalidWindow;
    }
    if (size_bytes < OS_KERNEL_PCI_BAR_MINIMUM_ALIGNMENT_BYTES || !IsPowerOfTwo(size_bytes)) {
        return PciMemoryResourceStatus::InvalidSize;
    }
    const uint64_t alignment_mask = size_bytes - 1ULL;
    if (window_begin_address > UINT64_MAX - alignment_mask) {
        return PciMemoryResourceStatus::OutOfSpace;
    }
    const uint64_t aligned_address = (window_begin_address + alignment_mask) & ~alignment_mask;
    if (aligned_address >= window_end_address ||
        size_bytes > window_end_address - aligned_address) {
        return PciMemoryResourceStatus::OutOfSpace;
    }
    physical_address = aligned_address;
    return PciMemoryResourceStatus::Succeeded;
}

PciCapabilityStatus DecodePciCapabilityHeader(const uint16_t header_value,
                                              PciCapabilityHeader &header) noexcept {
    const uint64_t next_offset_bytes =
        static_cast<uint64_t>(header_value >> OS_KERNEL_PCI_CAPABILITY_NEXT_SHIFT_BITS);
    if (next_offset_bytes != 0ULL &&
        (next_offset_bytes < OS_KERNEL_PCI_CAPABILITY_MINIMUM_OFFSET_BYTES ||
         next_offset_bytes > OS_KERNEL_PCI_CAPABILITY_MAXIMUM_OFFSET_BYTES ||
         next_offset_bytes % OS_KERNEL_PCI_CAPABILITY_ALIGNMENT_BYTES != 0ULL)) {
        return PciCapabilityStatus::InvalidOffset;
    }
    header = PciCapabilityHeader{
        .identifier =
            static_cast<uint8_t>(header_value & OS_KERNEL_PCI_CAPABILITY_IDENTIFIER_MASK),
        .next_offset_bytes = next_offset_bytes,
    };
    return header.identifier == 0U ||
                   header.identifier == OS_KERNEL_PCI_CAPABILITY_INVALID_IDENTIFIER
               ? PciCapabilityStatus::InvalidCapability
               : PciCapabilityStatus::Succeeded;
}

PciCapabilityStatus DecodePciMsixCapability(
    const uint16_t message_control, const uint32_t table_value,
    const uint32_t pending_value, PciMsixCapability &capability) noexcept {
    const uint64_t table_bar_index =
        static_cast<uint64_t>(table_value & OS_KERNEL_PCI_MSIX_BAR_INDEX_MASK);
    const uint64_t pending_bar_index =
        static_cast<uint64_t>(pending_value & OS_KERNEL_PCI_MSIX_BAR_INDEX_MASK);
    if (table_bar_index > OS_KERNEL_PCI_MAXIMUM_BAR_INDEX ||
        pending_bar_index > OS_KERNEL_PCI_MAXIMUM_BAR_INDEX) {
        return PciCapabilityStatus::InvalidCapability;
    }
    capability = PciMsixCapability{
        .table_entry_count =
            static_cast<uint64_t>(message_control & OS_KERNEL_PCI_MSIX_TABLE_SIZE_MASK) + 1ULL,
        .table_bar_index = table_bar_index,
        .table_offset_bytes =
            static_cast<uint64_t>(table_value & OS_KERNEL_PCI_MSIX_TABLE_OFFSET_MASK),
        .pending_bar_index = pending_bar_index,
        .pending_offset_bytes =
            static_cast<uint64_t>(pending_value & OS_KERNEL_PCI_MSIX_TABLE_OFFSET_MASK),
        .enabled = (message_control & OS_KERNEL_PCI_MSIX_ENABLE_BIT) != 0U,
        .function_masked = (message_control & OS_KERNEL_PCI_MSIX_FUNCTION_MASK_BIT) != 0U,
    };
    return PciCapabilityStatus::Succeeded;
}

PciCapabilityStatus BuildPciMsixTableEntry(
    const uint64_t local_apic_identifier, const uint64_t interrupt_vector,
    const bool masked, PciMsixTableEntry &entry) noexcept {
    if (local_apic_identifier > OS_KERNEL_PCI_MSIX_MAXIMUM_LOCAL_APIC_IDENTIFIER ||
        interrupt_vector < OS_KERNEL_PCI_MSIX_MINIMUM_INTERRUPT_VECTOR ||
        interrupt_vector > OS_KERNEL_PCI_MSIX_MAXIMUM_INTERRUPT_VECTOR) {
        return PciCapabilityStatus::InvalidCapability;
    }
    const uint64_t message_address =
        OS_KERNEL_PCI_MSIX_MESSAGE_ADDRESS_BASE |
        local_apic_identifier << OS_KERNEL_PCI_MSIX_DESTINATION_SHIFT_BITS;
    entry = PciMsixTableEntry{
        .message_address_low = static_cast<uint32_t>(message_address),
        .message_address_high =
            static_cast<uint32_t>(message_address >> OS_KERNEL_PCI_DWORD_BIT_COUNT),
        .message_data = static_cast<uint32_t>(interrupt_vector),
        .vector_control = masked ? OS_KERNEL_PCI_MSIX_VECTOR_MASK_BIT : 0U,
    };
    return PciCapabilityStatus::Succeeded;
}

}
