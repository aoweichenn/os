#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PCI_BUS_COUNT = 256ULL;
inline constexpr uint64_t OS_KERNEL_PCI_DEVICE_COUNT_PER_BUS = 32ULL;
inline constexpr uint64_t OS_KERNEL_PCI_FUNCTION_COUNT_PER_DEVICE = 8ULL;
inline constexpr uint64_t OS_KERNEL_PCI_CONFIGURATION_SPACE_SIZE_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES = 4ULL;
inline constexpr uint16_t OS_KERNEL_PCI_INVALID_VENDOR_IDENTIFIER = 0xFFFFU;
inline constexpr uint8_t OS_KERNEL_PCI_NVME_BASE_CLASS_CODE = 0x01U;
inline constexpr uint8_t OS_KERNEL_PCI_NVME_SUBCLASS_CODE = 0x08U;
inline constexpr uint8_t OS_KERNEL_PCI_NVME_PROGRAMMING_INTERFACE = 0x02U;
inline constexpr uint8_t OS_KERNEL_PCI_MSIX_CAPABILITY_IDENTIFIER = 0x11U;

struct PciDeviceAddress final {
    uint64_t bus_number;
    uint64_t device_number;
    uint64_t function_number;
};

enum class PciConfigurationAddressStatus : uint64_t {
    Succeeded,
    InvalidBus,
    InvalidDevice,
    InvalidFunction,
    InvalidRegisterOffset,
    InvalidEncodedAddress,
};

[[nodiscard]] PciConfigurationAddressStatus
EncodePciConfigurationAddress(const PciDeviceAddress &device_address,
                              uint64_t register_offset_bytes,
                              uint32_t &configuration_address) noexcept;
[[nodiscard]] PciConfigurationAddressStatus
DecodePciConfigurationAddress(uint32_t configuration_address,
                              PciDeviceAddress &device_address,
                              uint64_t &register_offset_bytes) noexcept;

struct PciDeviceIdentity final {
    uint16_t vendor_identifier;
    uint16_t device_identifier;
    uint8_t revision_identifier;
    uint8_t programming_interface;
    uint8_t subclass_code;
    uint8_t base_class_code;
};

[[nodiscard]] PciDeviceIdentity DecodePciDeviceIdentity(uint32_t vendor_device_value,
                                                        uint32_t class_revision_value) noexcept;
[[nodiscard]] bool PciDeviceIsPresent(const PciDeviceIdentity &identity) noexcept;
[[nodiscard]] bool PciDeviceIsNvmeController(const PciDeviceIdentity &identity) noexcept;

enum class PciMemoryBaseAddressKind : uint64_t {
    Memory32,
    Memory64,
};

struct PciMemoryBaseAddress final {
    uint64_t physical_address;
    PciMemoryBaseAddressKind kind;
    bool prefetchable;
};

enum class PciMemoryBaseAddressStatus : uint64_t {
    Succeeded,
    IoSpaceUnsupported,
    MemoryTypeUnsupported,
    Unassigned,
    InvalidSizeMask,
};

enum class PciMemoryResourceStatus : uint64_t {
    Succeeded,
    InvalidWindow,
    InvalidSize,
    OutOfSpace,
};

[[nodiscard]] PciMemoryBaseAddressStatus
DecodePciMemoryBaseAddress(uint32_t low_value, uint32_t high_value,
                           PciMemoryBaseAddress &base_address) noexcept;
[[nodiscard]] PciMemoryBaseAddressStatus
CalculatePciMemoryBaseAddressSize(uint32_t low_mask, uint32_t high_mask,
                                  uint64_t &size_bytes) noexcept;
[[nodiscard]] PciMemoryResourceStatus
AllocatePciMemoryResource(uint64_t window_begin_address, uint64_t window_end_address,
                          uint64_t size_bytes, uint64_t &physical_address) noexcept;

struct PciCapabilityHeader final {
    uint8_t identifier;
    uint64_t next_offset_bytes;
};

enum class PciCapabilityStatus : uint64_t {
    Succeeded,
    InvalidOffset,
    InvalidCapability,
};

[[nodiscard]] PciCapabilityStatus DecodePciCapabilityHeader(
    uint16_t header_value, PciCapabilityHeader &header) noexcept;

struct PciMsixCapability final {
    uint64_t table_entry_count;
    uint64_t table_bar_index;
    uint64_t table_offset_bytes;
    uint64_t pending_bar_index;
    uint64_t pending_offset_bytes;
    bool enabled;
    bool function_masked;
};

[[nodiscard]] PciCapabilityStatus DecodePciMsixCapability(
    uint16_t message_control, uint32_t table_value, uint32_t pending_value,
    PciMsixCapability &capability) noexcept;

struct PciMsixTableEntry final {
    uint32_t message_address_low;
    uint32_t message_address_high;
    uint32_t message_data;
    uint32_t vector_control;
};

static_assert(sizeof(PciMsixTableEntry) == 16ULL);

[[nodiscard]] PciCapabilityStatus BuildPciMsixTableEntry(
    uint64_t local_apic_identifier, uint64_t interrupt_vector, bool masked,
    PciMsixTableEntry &entry) noexcept;

}
