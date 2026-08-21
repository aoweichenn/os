#include <os/kernel/device/pci.hpp>

#include <os/kernel/device/port_io.hpp>

namespace os::kernel {

namespace {

constexpr uint16_t OS_KERNEL_PCI_CONFIGURATION_ADDRESS_PORT = 0x0CF8U;
constexpr uint16_t OS_KERNEL_PCI_CONFIGURATION_DATA_PORT = 0x0CFCU;
constexpr uint64_t OS_KERNEL_PCI_CONFIGURATION_WORD_SIZE_BYTES = 2ULL;
constexpr uint64_t OS_KERNEL_PCI_VENDOR_DEVICE_OFFSET_BYTES = 0x00ULL;
constexpr uint64_t OS_KERNEL_PCI_COMMAND_OFFSET_BYTES = 0x04ULL;
constexpr uint64_t OS_KERNEL_PCI_STATUS_OFFSET_BYTES = 0x06ULL;
constexpr uint64_t OS_KERNEL_PCI_CLASS_REVISION_OFFSET_BYTES = 0x08ULL;
constexpr uint64_t OS_KERNEL_PCI_HEADER_TYPE_OFFSET_BYTES = 0x0EULL;
constexpr uint64_t OS_KERNEL_PCI_FIRST_BAR_OFFSET_BYTES = 0x10ULL;
constexpr uint64_t OS_KERNEL_PCI_CAPABILITY_POINTER_OFFSET_BYTES = 0x34ULL;
constexpr uint64_t OS_KERNEL_PCI_CAPABILITY_MINIMUM_OFFSET_BYTES = 0x40ULL;
constexpr uint64_t OS_KERNEL_PCI_CAPABILITY_MAXIMUM_HEADER_OFFSET_BYTES = 0xF4ULL;
constexpr uint64_t OS_KERNEL_PCI_CAPABILITY_ALIGNMENT_BYTES = 4ULL;
constexpr uint64_t OS_KERNEL_PCI_CAPABILITY_MAXIMUM_TRAVERSAL_COUNT = 48ULL;
constexpr uint16_t OS_KERNEL_PCI_STATUS_CAPABILITY_LIST_BIT = 1U << 4U;
constexpr uint16_t OS_KERNEL_PCI_CAPABILITY_POINTER_MASK = 0x00FFU;
constexpr uint64_t OS_KERNEL_PCI_MSIX_MESSAGE_CONTROL_OFFSET_BYTES = 0x02ULL;
constexpr uint64_t OS_KERNEL_PCI_MSIX_TABLE_OFFSET_BYTES = 0x04ULL;
constexpr uint64_t OS_KERNEL_PCI_MSIX_PENDING_OFFSET_BYTES = 0x08ULL;
constexpr uint64_t OS_KERNEL_PCI_BAR_COUNT = 6ULL;
constexpr uint64_t OS_KERNEL_PCI_DWORD_BIT_COUNT = 32ULL;
constexpr uint16_t OS_KERNEL_PCI_COMMAND_MEMORY_SPACE_ENABLE = 1U << 1U;
constexpr uint16_t OS_KERNEL_PCI_COMMAND_BUS_MASTER_ENABLE = 1U << 2U;
constexpr uint16_t OS_KERNEL_PCI_COMMAND_DEVICE_ENABLE_MASK =
    OS_KERNEL_PCI_COMMAND_MEMORY_SPACE_ENABLE | OS_KERNEL_PCI_COMMAND_BUS_MASTER_ENABLE;
constexpr uint8_t OS_KERNEL_PCI_HEADER_MULTIFUNCTION_BIT = 1U << 7U;
constexpr uint32_t OS_KERNEL_PCI_BAR_IO_SPACE_BIT = 1U << 0U;
constexpr uint32_t OS_KERNEL_PCI_BAR_MEMORY_TYPE_SHIFT_BITS = 1U;
constexpr uint32_t OS_KERNEL_PCI_BAR_MEMORY_TYPE_MASK = 0x3U;
constexpr uint32_t OS_KERNEL_PCI_BAR_MEMORY_TYPE_32 = 0x0U;
constexpr uint32_t OS_KERNEL_PCI_BAR_MEMORY_TYPE_64 = 0x2U;
constexpr uint32_t OS_KERNEL_PCI_BAR_FLAG_MASK = 0x0000000FU;
constexpr uint32_t OS_KERNEL_PCI_BAR_ADDRESS_MASK = 0xFFFFFFF0U;
constexpr uint32_t OS_KERNEL_PCI_BAR_SIZE_PROBE_VALUE = UINT32_MAX;

[[nodiscard]] uint64_t BarOffset(const uint64_t bar_index) noexcept {
    return OS_KERNEL_PCI_FIRST_BAR_OFFSET_BYTES +
           bar_index * OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES;
}

[[nodiscard]] uint32_t BarMemoryType(const uint32_t value) noexcept {
    return (value >> OS_KERNEL_PCI_BAR_MEMORY_TYPE_SHIFT_BITS) &
           OS_KERNEL_PCI_BAR_MEMORY_TYPE_MASK;
}

[[nodiscard]] bool RestoreBarConfiguration(PciConfigurationSpace &configuration,
                                           const PciMemoryBarAssignment &assignment) noexcept {
    const uint64_t low_offset = BarOffset(assignment.bar_index);
    if (configuration.WriteDword(assignment.device_address, low_offset,
                                 assignment.original_low_value) !=
        PciConfigurationAccessStatus::Succeeded) {
        return false;
    }
    if (assignment.kind == PciMemoryBaseAddressKind::Memory64 &&
        configuration.WriteDword(assignment.device_address,
                                 low_offset + OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES,
                                 assignment.original_high_value) !=
            PciConfigurationAccessStatus::Succeeded) {
        return false;
    }
    return configuration.WriteWord(assignment.device_address, OS_KERNEL_PCI_COMMAND_OFFSET_BYTES,
                                   assignment.original_command) ==
           PciConfigurationAccessStatus::Succeeded;
}

}

PciConfigurationAccessStatus PciConfigurationSpace::ReadWord(
    const PciDeviceAddress &device_address, const uint64_t register_offset_bytes,
    uint16_t &value) noexcept {
    if (register_offset_bytes >= OS_KERNEL_PCI_CONFIGURATION_SPACE_SIZE_BYTES ||
        register_offset_bytes % OS_KERNEL_PCI_CONFIGURATION_WORD_SIZE_BYTES != 0ULL) {
        return PciConfigurationAccessStatus::InvalidAddress;
    }
    const uint64_t aligned_offset =
        register_offset_bytes & ~(OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES - 1ULL);
    uint32_t configuration_address = 0U;
    if (EncodePciConfigurationAddress(device_address, aligned_offset, configuration_address) !=
        PciConfigurationAddressStatus::Succeeded) {
        return PciConfigurationAccessStatus::InvalidAddress;
    }
    const uint16_t data_port = static_cast<uint16_t>(
        OS_KERNEL_PCI_CONFIGURATION_DATA_PORT + register_offset_bytes - aligned_offset);
    IrqSaveSpinLockGuard guard{this->lock_};
    WritePort32(OS_KERNEL_PCI_CONFIGURATION_ADDRESS_PORT, configuration_address);
    value = ReadPort16(data_port);
    return PciConfigurationAccessStatus::Succeeded;
}

PciConfigurationAccessStatus PciConfigurationSpace::WriteWord(
    const PciDeviceAddress &device_address, const uint64_t register_offset_bytes,
    const uint16_t value) noexcept {
    if (register_offset_bytes >= OS_KERNEL_PCI_CONFIGURATION_SPACE_SIZE_BYTES ||
        register_offset_bytes % OS_KERNEL_PCI_CONFIGURATION_WORD_SIZE_BYTES != 0ULL) {
        return PciConfigurationAccessStatus::InvalidAddress;
    }
    const uint64_t aligned_offset =
        register_offset_bytes & ~(OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES - 1ULL);
    uint32_t configuration_address = 0U;
    if (EncodePciConfigurationAddress(device_address, aligned_offset, configuration_address) !=
        PciConfigurationAddressStatus::Succeeded) {
        return PciConfigurationAccessStatus::InvalidAddress;
    }
    const uint16_t data_port = static_cast<uint16_t>(
        OS_KERNEL_PCI_CONFIGURATION_DATA_PORT + register_offset_bytes - aligned_offset);
    IrqSaveSpinLockGuard guard{this->lock_};
    WritePort32(OS_KERNEL_PCI_CONFIGURATION_ADDRESS_PORT, configuration_address);
    WritePort16(data_port, value);
    return PciConfigurationAccessStatus::Succeeded;
}

PciConfigurationAccessStatus PciConfigurationSpace::ReadDword(
    const PciDeviceAddress &device_address, const uint64_t register_offset_bytes,
    uint32_t &value) noexcept {
    uint32_t configuration_address = 0U;
    if (EncodePciConfigurationAddress(device_address, register_offset_bytes,
                                      configuration_address) !=
        PciConfigurationAddressStatus::Succeeded) {
        return PciConfigurationAccessStatus::InvalidAddress;
    }
    IrqSaveSpinLockGuard guard{this->lock_};
    WritePort32(OS_KERNEL_PCI_CONFIGURATION_ADDRESS_PORT, configuration_address);
    value = ReadPort32(OS_KERNEL_PCI_CONFIGURATION_DATA_PORT);
    return PciConfigurationAccessStatus::Succeeded;
}

PciConfigurationAccessStatus PciConfigurationSpace::WriteDword(
    const PciDeviceAddress &device_address, const uint64_t register_offset_bytes,
    const uint32_t value) noexcept {
    uint32_t configuration_address = 0U;
    if (EncodePciConfigurationAddress(device_address, register_offset_bytes,
                                      configuration_address) !=
        PciConfigurationAddressStatus::Succeeded) {
        return PciConfigurationAccessStatus::InvalidAddress;
    }
    IrqSaveSpinLockGuard guard{this->lock_};
    WritePort32(OS_KERNEL_PCI_CONFIGURATION_ADDRESS_PORT, configuration_address);
    WritePort32(OS_KERNEL_PCI_CONFIGURATION_DATA_PORT, value);
    return PciConfigurationAccessStatus::Succeeded;
}

PciNvmeScanStatus FindPciNvmeController(PciConfigurationSpace &configuration,
                                        PciNvmeController &controller) noexcept {
    bool found = false;
    for (uint64_t bus_number = 0ULL; bus_number < OS_KERNEL_PCI_BUS_COUNT; ++bus_number) {
        for (uint64_t device_number = 0ULL;
             device_number < OS_KERNEL_PCI_DEVICE_COUNT_PER_BUS; ++device_number) {
            const PciDeviceAddress function_zero{
                .bus_number = bus_number,
                .device_number = device_number,
                .function_number = 0ULL,
            };
            uint32_t vendor_device_value = 0U;
            if (configuration.ReadDword(function_zero, OS_KERNEL_PCI_VENDOR_DEVICE_OFFSET_BYTES,
                                        vendor_device_value) !=
                PciConfigurationAccessStatus::Succeeded) {
                return PciNvmeScanStatus::AccessFailed;
            }
            const PciDeviceIdentity function_zero_identity =
                DecodePciDeviceIdentity(vendor_device_value, 0U);
            if (!PciDeviceIsPresent(function_zero_identity)) {
                continue;
            }
            uint16_t header_type = 0U;
            if (configuration.ReadWord(function_zero, OS_KERNEL_PCI_HEADER_TYPE_OFFSET_BYTES,
                                       header_type) != PciConfigurationAccessStatus::Succeeded) {
                return PciNvmeScanStatus::AccessFailed;
            }
            const uint64_t function_count =
                (static_cast<uint8_t>(header_type) & OS_KERNEL_PCI_HEADER_MULTIFUNCTION_BIT) != 0U
                    ? OS_KERNEL_PCI_FUNCTION_COUNT_PER_DEVICE
                    : 1ULL;
            for (uint64_t function_number = 0ULL; function_number < function_count;
                 ++function_number) {
                const PciDeviceAddress address{
                    .bus_number = bus_number,
                    .device_number = device_number,
                    .function_number = function_number,
                };
                if (function_number != 0ULL &&
                    configuration.ReadDword(address, OS_KERNEL_PCI_VENDOR_DEVICE_OFFSET_BYTES,
                                            vendor_device_value) !=
                        PciConfigurationAccessStatus::Succeeded) {
                    return PciNvmeScanStatus::AccessFailed;
                }
                if (!PciDeviceIsPresent(DecodePciDeviceIdentity(vendor_device_value, 0U))) {
                    continue;
                }
                uint32_t class_revision_value = 0U;
                if (configuration.ReadDword(address, OS_KERNEL_PCI_CLASS_REVISION_OFFSET_BYTES,
                                            class_revision_value) !=
                    PciConfigurationAccessStatus::Succeeded) {
                    return PciNvmeScanStatus::AccessFailed;
                }
                const PciDeviceIdentity identity =
                    DecodePciDeviceIdentity(vendor_device_value, class_revision_value);
                if (!PciDeviceIsNvmeController(identity)) {
                    continue;
                }
                if (found) {
                    return PciNvmeScanStatus::MultipleControllers;
                }
                controller = PciNvmeController{.address = address, .identity = identity};
                found = true;
            }
        }
    }
    return found ? PciNvmeScanStatus::Succeeded : PciNvmeScanStatus::NotFound;
}

PciMemoryBarAssignmentStatus AssignPciMemoryBar(
    PciConfigurationSpace &configuration, const PciDeviceAddress &device_address,
    const uint64_t bar_index, const uint64_t window_begin_address,
    const uint64_t window_end_address, PciMemoryBarAssignment &assignment) noexcept {
    if (assignment.active || bar_index >= OS_KERNEL_PCI_BAR_COUNT) {
        return PciMemoryBarAssignmentStatus::InvalidArgument;
    }
    uint16_t original_command = 0U;
    uint32_t original_low_value = 0U;
    const uint64_t low_offset = BarOffset(bar_index);
    if (configuration.ReadWord(device_address, OS_KERNEL_PCI_COMMAND_OFFSET_BYTES,
                               original_command) != PciConfigurationAccessStatus::Succeeded ||
        configuration.ReadDword(device_address, low_offset, original_low_value) !=
            PciConfigurationAccessStatus::Succeeded) {
        return PciMemoryBarAssignmentStatus::AccessFailed;
    }
    if ((original_low_value & OS_KERNEL_PCI_BAR_IO_SPACE_BIT) != 0U) {
        return PciMemoryBarAssignmentStatus::UnsupportedBar;
    }
    const uint32_t memory_type = BarMemoryType(original_low_value);
    if (memory_type != OS_KERNEL_PCI_BAR_MEMORY_TYPE_32 &&
        memory_type != OS_KERNEL_PCI_BAR_MEMORY_TYPE_64) {
        return PciMemoryBarAssignmentStatus::UnsupportedBar;
    }
    const PciMemoryBaseAddressKind kind = memory_type == OS_KERNEL_PCI_BAR_MEMORY_TYPE_64
                                               ? PciMemoryBaseAddressKind::Memory64
                                               : PciMemoryBaseAddressKind::Memory32;
    if (kind == PciMemoryBaseAddressKind::Memory64 &&
        bar_index + 1ULL >= OS_KERNEL_PCI_BAR_COUNT) {
        return PciMemoryBarAssignmentStatus::UnsupportedBar;
    }
    uint32_t original_high_value = 0U;
    if (kind == PciMemoryBaseAddressKind::Memory64 &&
        configuration.ReadDword(device_address,
                                low_offset + OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES,
                                original_high_value) != PciConfigurationAccessStatus::Succeeded) {
        return PciMemoryBarAssignmentStatus::AccessFailed;
    }
    assignment = PciMemoryBarAssignment{
        .device_address = device_address,
        .bar_index = bar_index,
        .physical_address = 0ULL,
        .size_bytes = 0ULL,
        .original_command = original_command,
        .original_low_value = original_low_value,
        .original_high_value = original_high_value,
        .kind = kind,
        .active = false,
    };
    const uint16_t disabled_command =
        static_cast<uint16_t>(original_command & ~OS_KERNEL_PCI_COMMAND_DEVICE_ENABLE_MASK);
    if (configuration.WriteWord(device_address, OS_KERNEL_PCI_COMMAND_OFFSET_BYTES,
                                disabled_command) != PciConfigurationAccessStatus::Succeeded ||
        configuration.WriteDword(device_address, low_offset,
                                 OS_KERNEL_PCI_BAR_SIZE_PROBE_VALUE) !=
            PciConfigurationAccessStatus::Succeeded ||
        (kind == PciMemoryBaseAddressKind::Memory64 &&
         configuration.WriteDword(device_address,
                                  low_offset + OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES,
                                  OS_KERNEL_PCI_BAR_SIZE_PROBE_VALUE) !=
             PciConfigurationAccessStatus::Succeeded)) {
        static_cast<void>(RestoreBarConfiguration(configuration, assignment));
        return PciMemoryBarAssignmentStatus::AccessFailed;
    }
    uint32_t low_mask = 0U;
    uint32_t high_mask = 0U;
    const bool mask_read =
        configuration.ReadDword(device_address, low_offset, low_mask) ==
            PciConfigurationAccessStatus::Succeeded &&
        (kind == PciMemoryBaseAddressKind::Memory32 ||
         configuration.ReadDword(device_address,
                                 low_offset + OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES,
                                 high_mask) == PciConfigurationAccessStatus::Succeeded);
    const bool probe_restored = RestoreBarConfiguration(configuration, assignment);
    if (!mask_read || !probe_restored) {
        return probe_restored ? PciMemoryBarAssignmentStatus::AccessFailed
                              : PciMemoryBarAssignmentStatus::RestoreFailed;
    }
    uint64_t size_bytes = 0ULL;
    if (CalculatePciMemoryBaseAddressSize(low_mask, high_mask, size_bytes) !=
        PciMemoryBaseAddressStatus::Succeeded) {
        return PciMemoryBarAssignmentStatus::SizeProbeFailed;
    }
    uint64_t physical_address = 0ULL;
    if (AllocatePciMemoryResource(window_begin_address, window_end_address, size_bytes,
                                  physical_address) != PciMemoryResourceStatus::Succeeded ||
        (kind == PciMemoryBaseAddressKind::Memory32 && physical_address > UINT32_MAX)) {
        return PciMemoryBarAssignmentStatus::ResourceUnavailable;
    }
    const uint32_t programmed_low_value =
        (static_cast<uint32_t>(physical_address) & OS_KERNEL_PCI_BAR_ADDRESS_MASK) |
        (original_low_value & OS_KERNEL_PCI_BAR_FLAG_MASK);
    const uint32_t programmed_high_value =
        static_cast<uint32_t>(physical_address >> OS_KERNEL_PCI_DWORD_BIT_COUNT);
    if (configuration.WriteWord(device_address, OS_KERNEL_PCI_COMMAND_OFFSET_BYTES,
                                disabled_command) != PciConfigurationAccessStatus::Succeeded ||
        configuration.WriteDword(device_address, low_offset, programmed_low_value) !=
            PciConfigurationAccessStatus::Succeeded ||
        (kind == PciMemoryBaseAddressKind::Memory64 &&
         configuration.WriteDword(device_address,
                                  low_offset + OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES,
                                  programmed_high_value) !=
             PciConfigurationAccessStatus::Succeeded)) {
        static_cast<void>(RestoreBarConfiguration(configuration, assignment));
        return PciMemoryBarAssignmentStatus::ProgrammingFailed;
    }
    const uint16_t enabled_command =
        static_cast<uint16_t>(original_command | OS_KERNEL_PCI_COMMAND_DEVICE_ENABLE_MASK);
    if (configuration.WriteWord(device_address, OS_KERNEL_PCI_COMMAND_OFFSET_BYTES,
                                enabled_command) != PciConfigurationAccessStatus::Succeeded) {
        static_cast<void>(RestoreBarConfiguration(configuration, assignment));
        return PciMemoryBarAssignmentStatus::ProgrammingFailed;
    }
    uint32_t verified_low_value = 0U;
    uint32_t verified_high_value = 0U;
    PciMemoryBaseAddress verified_address{};
    if (configuration.ReadDword(device_address, low_offset, verified_low_value) !=
            PciConfigurationAccessStatus::Succeeded ||
        (kind == PciMemoryBaseAddressKind::Memory64 &&
         configuration.ReadDword(device_address,
                                 low_offset + OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES,
                                 verified_high_value) != PciConfigurationAccessStatus::Succeeded) ||
        DecodePciMemoryBaseAddress(verified_low_value, verified_high_value, verified_address) !=
            PciMemoryBaseAddressStatus::Succeeded ||
        verified_address.physical_address != physical_address) {
        static_cast<void>(RestoreBarConfiguration(configuration, assignment));
        return PciMemoryBarAssignmentStatus::ProgrammingFailed;
    }
    assignment.physical_address = physical_address;
    assignment.size_bytes = size_bytes;
    assignment.active = true;
    return PciMemoryBarAssignmentStatus::Succeeded;
}

PciMemoryBarAssignmentStatus
RestorePciMemoryBar(PciConfigurationSpace &configuration,
                    PciMemoryBarAssignment &assignment) noexcept {
    if (!assignment.active) {
        return PciMemoryBarAssignmentStatus::InvalidArgument;
    }
    if (!RestoreBarConfiguration(configuration, assignment)) {
        return PciMemoryBarAssignmentStatus::RestoreFailed;
    }
    assignment = PciMemoryBarAssignment{};
    return PciMemoryBarAssignmentStatus::Succeeded;
}

PciMsixAccessStatus FindPciMsixCapability(
    PciConfigurationSpace &configuration, const PciDeviceAddress &device_address,
    PciMsixCapabilityLocation &location) noexcept {
    location = PciMsixCapabilityLocation{};
    uint16_t status = 0U;
    uint16_t capability_pointer_value = 0U;
    if (configuration.ReadWord(device_address, OS_KERNEL_PCI_STATUS_OFFSET_BYTES, status) !=
            PciConfigurationAccessStatus::Succeeded ||
        configuration.ReadWord(device_address, OS_KERNEL_PCI_CAPABILITY_POINTER_OFFSET_BYTES,
                               capability_pointer_value) !=
            PciConfigurationAccessStatus::Succeeded) {
        return PciMsixAccessStatus::AccessFailed;
    }
    if ((status & OS_KERNEL_PCI_STATUS_CAPABILITY_LIST_BIT) == 0U) {
        return PciMsixAccessStatus::NotSupported;
    }
    uint64_t capability_offset =
        static_cast<uint64_t>(capability_pointer_value & OS_KERNEL_PCI_CAPABILITY_POINTER_MASK);
    for (uint64_t traversal_count = 0ULL;
         traversal_count < OS_KERNEL_PCI_CAPABILITY_MAXIMUM_TRAVERSAL_COUNT;
         ++traversal_count) {
        if (capability_offset < OS_KERNEL_PCI_CAPABILITY_MINIMUM_OFFSET_BYTES ||
            capability_offset > OS_KERNEL_PCI_CAPABILITY_MAXIMUM_HEADER_OFFSET_BYTES ||
            capability_offset % OS_KERNEL_PCI_CAPABILITY_ALIGNMENT_BYTES != 0ULL) {
            return PciMsixAccessStatus::CorruptCapabilityList;
        }
        uint16_t header_value = 0U;
        if (configuration.ReadWord(device_address, capability_offset, header_value) !=
            PciConfigurationAccessStatus::Succeeded) {
            return PciMsixAccessStatus::AccessFailed;
        }
        PciCapabilityHeader header{};
        if (DecodePciCapabilityHeader(header_value, header) !=
            PciCapabilityStatus::Succeeded) {
            return PciMsixAccessStatus::CorruptCapabilityList;
        }
        if (header.identifier == OS_KERNEL_PCI_MSIX_CAPABILITY_IDENTIFIER) {
            uint16_t message_control = 0U;
            uint32_t table_value = 0U;
            uint32_t pending_value = 0U;
            if (configuration.ReadWord(
                    device_address,
                    capability_offset + OS_KERNEL_PCI_MSIX_MESSAGE_CONTROL_OFFSET_BYTES,
                    message_control) != PciConfigurationAccessStatus::Succeeded ||
                configuration.ReadDword(
                    device_address, capability_offset + OS_KERNEL_PCI_MSIX_TABLE_OFFSET_BYTES,
                    table_value) != PciConfigurationAccessStatus::Succeeded ||
                configuration.ReadDword(
                    device_address, capability_offset + OS_KERNEL_PCI_MSIX_PENDING_OFFSET_BYTES,
                    pending_value) != PciConfigurationAccessStatus::Succeeded) {
                return PciMsixAccessStatus::AccessFailed;
            }
            PciMsixCapability capability{};
            if (DecodePciMsixCapability(message_control, table_value, pending_value,
                                        capability) != PciCapabilityStatus::Succeeded) {
                return PciMsixAccessStatus::InvalidCapability;
            }
            location = PciMsixCapabilityLocation{
                .capability_offset_bytes = capability_offset,
                .original_message_control = message_control,
                .capability = capability,
            };
            return PciMsixAccessStatus::Succeeded;
        }
        if (header.next_offset_bytes == 0ULL) {
            return PciMsixAccessStatus::NotSupported;
        }
        if (header.next_offset_bytes == capability_offset) {
            return PciMsixAccessStatus::CorruptCapabilityList;
        }
        capability_offset = header.next_offset_bytes;
    }
    return PciMsixAccessStatus::CorruptCapabilityList;
}

PciMsixAccessStatus WritePciMsixMessageControl(
    PciConfigurationSpace &configuration, const PciDeviceAddress &device_address,
    const uint64_t capability_offset_bytes, const uint16_t message_control) noexcept {
    if (capability_offset_bytes < OS_KERNEL_PCI_CAPABILITY_MINIMUM_OFFSET_BYTES ||
        capability_offset_bytes > OS_KERNEL_PCI_CAPABILITY_MAXIMUM_HEADER_OFFSET_BYTES ||
        capability_offset_bytes % OS_KERNEL_PCI_CAPABILITY_ALIGNMENT_BYTES != 0ULL) {
        return PciMsixAccessStatus::InvalidCapability;
    }
    return configuration.WriteWord(
               device_address,
               capability_offset_bytes + OS_KERNEL_PCI_MSIX_MESSAGE_CONTROL_OFFSET_BYTES,
               message_control) == PciConfigurationAccessStatus::Succeeded
               ? PciMsixAccessStatus::Succeeded
               : PciMsixAccessStatus::AccessFailed;
}

}
