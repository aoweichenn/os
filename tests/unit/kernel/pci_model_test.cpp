#include <os/kernel/device/pci_model.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PCI_MODEL_SUITE_NAME = "kernel/pci_model/unit";
constexpr std::string_view OS_TEST_PCI_MODEL_CONFIGURATION_ADDRESS =
    "PCI mechanism 1 地址必须精确编码并往返 BDF 与寄存器偏移";
constexpr std::string_view OS_TEST_PCI_MODEL_CONFIGURATION_REJECTION =
    "PCI mechanism 1 必须拒绝越界 BDF、非 DWORD 偏移和非法编码";
constexpr std::string_view OS_TEST_PCI_MODEL_NVME_IDENTITY =
    "PCI class 01/08/02 必须识别为存在的 NVMe 控制器";
constexpr std::string_view OS_TEST_PCI_MODEL_MEMORY_BAR =
    "PCI memory BAR 必须区分 32/64 位、地址、预取属性与未分配状态";
constexpr std::string_view OS_TEST_PCI_MODEL_BAR_SIZE =
    "PCI memory BAR 探测掩码必须还原精确的二次幂 aperture";
constexpr std::string_view OS_TEST_PCI_MODEL_RESOURCE_ALLOCATION =
    "PCI memory window 必须按 BAR aperture 对齐并拒绝越界";
constexpr std::string_view OS_TEST_PCI_MODEL_MSIX_CAPABILITY =
    "PCI MSI-X capability 必须解析链表、table/PBA BIR、表长和 enable/mask";
constexpr std::string_view OS_TEST_PCI_MODEL_MSIX_TABLE_ENTRY =
    "PCI MSI-X table entry 必须编码 LAPIC 目标、向量和逐向量 mask";

constexpr os::kernel::PciDeviceAddress OS_TEST_PCI_MODEL_DEVICE_ADDRESS{
    .bus_number = 0xABULL,
    .device_number = 0x1DULL,
    .function_number = 0x05ULL,
};
constexpr uint64_t OS_TEST_PCI_MODEL_REGISTER_OFFSET_BYTES = 0xFCULL;
constexpr uint32_t OS_TEST_PCI_MODEL_EXPECTED_CONFIGURATION_ADDRESS = 0x80ABEDFCU;
constexpr uint32_t OS_TEST_PCI_MODEL_UNCHANGED_CONFIGURATION_ADDRESS = 0xA5A5A5A5U;
constexpr uint64_t OS_TEST_PCI_MODEL_INVALID_BUS = 0x100ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_INVALID_DEVICE = 0x20ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_INVALID_FUNCTION = 0x08ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_INVALID_REGISTER_OFFSET_BYTES = 0x100ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_MISALIGNED_REGISTER_OFFSET_BYTES = 0x03ULL;
constexpr uint32_t OS_TEST_PCI_MODEL_DISABLED_CONFIGURATION_ADDRESS = 0x00ABEDFCU;
constexpr uint32_t OS_TEST_PCI_MODEL_RESERVED_CONFIGURATION_ADDRESS = 0x81ABEDFCU;
constexpr uint32_t OS_TEST_PCI_MODEL_NVME_VENDOR_DEVICE = 0x00101B36U;
constexpr uint32_t OS_TEST_PCI_MODEL_NVME_CLASS_REVISION = 0x01080203U;
constexpr uint32_t OS_TEST_PCI_MODEL_ABSENT_VENDOR_DEVICE = 0xFFFFFFFFU;
constexpr uint16_t OS_TEST_PCI_MODEL_EXPECTED_VENDOR_IDENTIFIER = 0x1B36U;
constexpr uint16_t OS_TEST_PCI_MODEL_EXPECTED_DEVICE_IDENTIFIER = 0x0010U;
constexpr uint8_t OS_TEST_PCI_MODEL_EXPECTED_REVISION_IDENTIFIER = 0x03U;
constexpr uint32_t OS_TEST_PCI_MODEL_64_BIT_BAR_LOW = 0xFEBF0004U;
constexpr uint32_t OS_TEST_PCI_MODEL_64_BIT_BAR_HIGH = 0x00000010U;
constexpr uint64_t OS_TEST_PCI_MODEL_EXPECTED_64_BIT_ADDRESS = 0x00000010FEBF0000ULL;
constexpr uint32_t OS_TEST_PCI_MODEL_32_BIT_BAR_LOW = 0xFEBF0008U;
constexpr uint64_t OS_TEST_PCI_MODEL_EXPECTED_32_BIT_ADDRESS = 0x00000000FEBF0000ULL;
constexpr uint32_t OS_TEST_PCI_MODEL_IO_BAR_LOW = 0x0000C001U;
constexpr uint32_t OS_TEST_PCI_MODEL_UNSUPPORTED_BAR_LOW = 0x0000C002U;
constexpr uint32_t OS_TEST_PCI_MODEL_UNASSIGNED_64_BIT_BAR_LOW = 0x00000004U;
constexpr uint32_t OS_TEST_PCI_MODEL_16_KIB_BAR_MASK_LOW = 0xFFFFC004U;
constexpr uint32_t OS_TEST_PCI_MODEL_16_KIB_BAR_MASK_HIGH = 0xFFFFFFFFU;
constexpr uint64_t OS_TEST_PCI_MODEL_EXPECTED_16_KIB_SIZE_BYTES = 16384ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_WINDOW_BEGIN_ADDRESS = 0x00000000F8001000ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_WINDOW_END_ADDRESS = 0x00000000F8010000ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_EXPECTED_RESOURCE_ADDRESS = 0x00000000F8004000ULL;
constexpr uint16_t OS_TEST_PCI_MODEL_MSIX_CAPABILITY_HEADER = 0x9011U;
constexpr uint16_t OS_TEST_PCI_MODEL_INVALID_CAPABILITY_HEADER = 0x4211U;
constexpr uint64_t OS_TEST_PCI_MODEL_NEXT_CAPABILITY_OFFSET_BYTES = 0x90ULL;
constexpr uint16_t OS_TEST_PCI_MODEL_MSIX_MESSAGE_CONTROL = 0xC040U;
constexpr uint32_t OS_TEST_PCI_MODEL_MSIX_TABLE_VALUE = 0x00000004U;
constexpr uint32_t OS_TEST_PCI_MODEL_MSIX_PENDING_VALUE = 0x00000804U;
constexpr uint64_t OS_TEST_PCI_MODEL_EXPECTED_MSIX_TABLE_ENTRY_COUNT = 65ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_MSIX_BAR_INDEX = 4ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_MSIX_PENDING_OFFSET_BYTES = 0x800ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_LOCAL_APIC_IDENTIFIER = 0ULL;
constexpr uint64_t OS_TEST_PCI_MODEL_MSIX_INTERRUPT_VECTOR = 0x50ULL;
constexpr uint32_t OS_TEST_PCI_MODEL_EXPECTED_MSIX_MESSAGE_ADDRESS = 0xFEE00000U;
constexpr uint32_t OS_TEST_PCI_MODEL_MSIX_VECTOR_MASK = 1U;

}

int main() {
    os::test::TestContext test_context{OS_TEST_PCI_MODEL_SUITE_NAME};

    uint32_t configuration_address = OS_TEST_PCI_MODEL_UNCHANGED_CONFIGURATION_ADDRESS;
    os::kernel::PciDeviceAddress decoded_address{};
    uint64_t decoded_register_offset_bytes = 0ULL;
    const bool configuration_address_passed =
        os::kernel::EncodePciConfigurationAddress(
            OS_TEST_PCI_MODEL_DEVICE_ADDRESS, OS_TEST_PCI_MODEL_REGISTER_OFFSET_BYTES,
            configuration_address) ==
            os::kernel::PciConfigurationAddressStatus::Succeeded &&
        configuration_address == OS_TEST_PCI_MODEL_EXPECTED_CONFIGURATION_ADDRESS &&
        os::kernel::DecodePciConfigurationAddress(
            configuration_address, decoded_address, decoded_register_offset_bytes) ==
            os::kernel::PciConfigurationAddressStatus::Succeeded &&
        decoded_address.bus_number == OS_TEST_PCI_MODEL_DEVICE_ADDRESS.bus_number &&
        decoded_address.device_number == OS_TEST_PCI_MODEL_DEVICE_ADDRESS.device_number &&
        decoded_address.function_number == OS_TEST_PCI_MODEL_DEVICE_ADDRESS.function_number &&
        decoded_register_offset_bytes == OS_TEST_PCI_MODEL_REGISTER_OFFSET_BYTES;
    test_context.Expect(configuration_address_passed,
                        OS_TEST_PCI_MODEL_CONFIGURATION_ADDRESS);

    configuration_address = OS_TEST_PCI_MODEL_UNCHANGED_CONFIGURATION_ADDRESS;
    const bool configuration_rejection_passed =
        os::kernel::EncodePciConfigurationAddress(
            os::kernel::PciDeviceAddress{
                .bus_number = OS_TEST_PCI_MODEL_INVALID_BUS,
                .device_number = 0ULL,
                .function_number = 0ULL,
            },
            0ULL, configuration_address) ==
            os::kernel::PciConfigurationAddressStatus::InvalidBus &&
        os::kernel::EncodePciConfigurationAddress(
            os::kernel::PciDeviceAddress{
                .bus_number = 0ULL,
                .device_number = OS_TEST_PCI_MODEL_INVALID_DEVICE,
                .function_number = 0ULL,
            },
            0ULL, configuration_address) ==
            os::kernel::PciConfigurationAddressStatus::InvalidDevice &&
        os::kernel::EncodePciConfigurationAddress(
            os::kernel::PciDeviceAddress{
                .bus_number = 0ULL,
                .device_number = 0ULL,
                .function_number = OS_TEST_PCI_MODEL_INVALID_FUNCTION,
            },
            0ULL, configuration_address) ==
            os::kernel::PciConfigurationAddressStatus::InvalidFunction &&
        os::kernel::EncodePciConfigurationAddress(
            os::kernel::PciDeviceAddress{}, OS_TEST_PCI_MODEL_INVALID_REGISTER_OFFSET_BYTES,
            configuration_address) ==
            os::kernel::PciConfigurationAddressStatus::InvalidRegisterOffset &&
        os::kernel::EncodePciConfigurationAddress(
            os::kernel::PciDeviceAddress{}, OS_TEST_PCI_MODEL_MISALIGNED_REGISTER_OFFSET_BYTES,
            configuration_address) ==
            os::kernel::PciConfigurationAddressStatus::InvalidRegisterOffset &&
        configuration_address == OS_TEST_PCI_MODEL_UNCHANGED_CONFIGURATION_ADDRESS &&
        os::kernel::DecodePciConfigurationAddress(
            OS_TEST_PCI_MODEL_DISABLED_CONFIGURATION_ADDRESS, decoded_address,
            decoded_register_offset_bytes) ==
            os::kernel::PciConfigurationAddressStatus::InvalidEncodedAddress &&
        os::kernel::DecodePciConfigurationAddress(
            OS_TEST_PCI_MODEL_RESERVED_CONFIGURATION_ADDRESS, decoded_address,
            decoded_register_offset_bytes) ==
            os::kernel::PciConfigurationAddressStatus::InvalidEncodedAddress;
    test_context.Expect(configuration_rejection_passed,
                        OS_TEST_PCI_MODEL_CONFIGURATION_REJECTION);

    const os::kernel::PciDeviceIdentity nvme_identity =
        os::kernel::DecodePciDeviceIdentity(OS_TEST_PCI_MODEL_NVME_VENDOR_DEVICE,
                                            OS_TEST_PCI_MODEL_NVME_CLASS_REVISION);
    const os::kernel::PciDeviceIdentity absent_identity =
        os::kernel::DecodePciDeviceIdentity(OS_TEST_PCI_MODEL_ABSENT_VENDOR_DEVICE,
                                            OS_TEST_PCI_MODEL_NVME_CLASS_REVISION);
    test_context.Expect(
        nvme_identity.vendor_identifier == OS_TEST_PCI_MODEL_EXPECTED_VENDOR_IDENTIFIER &&
            nvme_identity.device_identifier == OS_TEST_PCI_MODEL_EXPECTED_DEVICE_IDENTIFIER &&
            nvme_identity.revision_identifier == OS_TEST_PCI_MODEL_EXPECTED_REVISION_IDENTIFIER &&
            os::kernel::PciDeviceIsPresent(nvme_identity) &&
            os::kernel::PciDeviceIsNvmeController(nvme_identity) &&
            !os::kernel::PciDeviceIsPresent(absent_identity) &&
            !os::kernel::PciDeviceIsNvmeController(absent_identity),
        OS_TEST_PCI_MODEL_NVME_IDENTITY);

    os::kernel::PciMemoryBaseAddress base_address{};
    const bool memory_bar_passed =
        os::kernel::DecodePciMemoryBaseAddress(OS_TEST_PCI_MODEL_64_BIT_BAR_LOW,
                                               OS_TEST_PCI_MODEL_64_BIT_BAR_HIGH,
                                               base_address) ==
            os::kernel::PciMemoryBaseAddressStatus::Succeeded &&
        base_address.physical_address == OS_TEST_PCI_MODEL_EXPECTED_64_BIT_ADDRESS &&
        base_address.kind == os::kernel::PciMemoryBaseAddressKind::Memory64 &&
        !base_address.prefetchable &&
        os::kernel::DecodePciMemoryBaseAddress(OS_TEST_PCI_MODEL_32_BIT_BAR_LOW, 0U,
                                               base_address) ==
            os::kernel::PciMemoryBaseAddressStatus::Succeeded &&
        base_address.physical_address == OS_TEST_PCI_MODEL_EXPECTED_32_BIT_ADDRESS &&
        base_address.kind == os::kernel::PciMemoryBaseAddressKind::Memory32 &&
        base_address.prefetchable &&
        os::kernel::DecodePciMemoryBaseAddress(OS_TEST_PCI_MODEL_IO_BAR_LOW, 0U,
                                               base_address) ==
            os::kernel::PciMemoryBaseAddressStatus::IoSpaceUnsupported &&
        os::kernel::DecodePciMemoryBaseAddress(OS_TEST_PCI_MODEL_UNSUPPORTED_BAR_LOW, 0U,
                                               base_address) ==
            os::kernel::PciMemoryBaseAddressStatus::MemoryTypeUnsupported &&
        os::kernel::DecodePciMemoryBaseAddress(OS_TEST_PCI_MODEL_UNASSIGNED_64_BIT_BAR_LOW, 0U,
                                               base_address) ==
            os::kernel::PciMemoryBaseAddressStatus::Unassigned;
    test_context.Expect(memory_bar_passed, OS_TEST_PCI_MODEL_MEMORY_BAR);

    uint64_t size_bytes = 0ULL;
    test_context.Expect(
        os::kernel::CalculatePciMemoryBaseAddressSize(
            OS_TEST_PCI_MODEL_16_KIB_BAR_MASK_LOW, OS_TEST_PCI_MODEL_16_KIB_BAR_MASK_HIGH,
            size_bytes) == os::kernel::PciMemoryBaseAddressStatus::Succeeded &&
            size_bytes == OS_TEST_PCI_MODEL_EXPECTED_16_KIB_SIZE_BYTES &&
            os::kernel::CalculatePciMemoryBaseAddressSize(OS_TEST_PCI_MODEL_IO_BAR_LOW, 0U,
                                                          size_bytes) ==
                os::kernel::PciMemoryBaseAddressStatus::IoSpaceUnsupported &&
            os::kernel::CalculatePciMemoryBaseAddressSize(0U, 0U, size_bytes) ==
                os::kernel::PciMemoryBaseAddressStatus::InvalidSizeMask,
        OS_TEST_PCI_MODEL_BAR_SIZE);

    uint64_t resource_address = 0ULL;
    test_context.Expect(
        os::kernel::AllocatePciMemoryResource(
            OS_TEST_PCI_MODEL_WINDOW_BEGIN_ADDRESS, OS_TEST_PCI_MODEL_WINDOW_END_ADDRESS,
            OS_TEST_PCI_MODEL_EXPECTED_16_KIB_SIZE_BYTES, resource_address) ==
                os::kernel::PciMemoryResourceStatus::Succeeded &&
            resource_address == OS_TEST_PCI_MODEL_EXPECTED_RESOURCE_ADDRESS &&
            os::kernel::AllocatePciMemoryResource(
                OS_TEST_PCI_MODEL_WINDOW_BEGIN_ADDRESS,
                OS_TEST_PCI_MODEL_EXPECTED_RESOURCE_ADDRESS,
                OS_TEST_PCI_MODEL_EXPECTED_16_KIB_SIZE_BYTES, resource_address) ==
                os::kernel::PciMemoryResourceStatus::OutOfSpace,
        OS_TEST_PCI_MODEL_RESOURCE_ALLOCATION);

    os::kernel::PciCapabilityHeader capability_header{};
    os::kernel::PciMsixCapability msix_capability{};
    test_context.Expect(
        os::kernel::DecodePciCapabilityHeader(OS_TEST_PCI_MODEL_MSIX_CAPABILITY_HEADER,
                                              capability_header) ==
                os::kernel::PciCapabilityStatus::Succeeded &&
            capability_header.identifier ==
                os::kernel::OS_KERNEL_PCI_MSIX_CAPABILITY_IDENTIFIER &&
            capability_header.next_offset_bytes ==
                OS_TEST_PCI_MODEL_NEXT_CAPABILITY_OFFSET_BYTES &&
            os::kernel::DecodePciCapabilityHeader(OS_TEST_PCI_MODEL_INVALID_CAPABILITY_HEADER,
                                                  capability_header) ==
                os::kernel::PciCapabilityStatus::InvalidOffset &&
            os::kernel::DecodePciMsixCapability(
                OS_TEST_PCI_MODEL_MSIX_MESSAGE_CONTROL, OS_TEST_PCI_MODEL_MSIX_TABLE_VALUE,
                OS_TEST_PCI_MODEL_MSIX_PENDING_VALUE,
                msix_capability) == os::kernel::PciCapabilityStatus::Succeeded &&
            msix_capability.table_entry_count ==
                OS_TEST_PCI_MODEL_EXPECTED_MSIX_TABLE_ENTRY_COUNT &&
            msix_capability.table_bar_index == OS_TEST_PCI_MODEL_MSIX_BAR_INDEX &&
            msix_capability.table_offset_bytes == 0ULL &&
            msix_capability.pending_bar_index == OS_TEST_PCI_MODEL_MSIX_BAR_INDEX &&
            msix_capability.pending_offset_bytes ==
                OS_TEST_PCI_MODEL_MSIX_PENDING_OFFSET_BYTES &&
            msix_capability.enabled && msix_capability.function_masked,
        OS_TEST_PCI_MODEL_MSIX_CAPABILITY);

    os::kernel::PciMsixTableEntry msix_entry{};
    const bool unmasked_entry_valid =
        os::kernel::BuildPciMsixTableEntry(
            OS_TEST_PCI_MODEL_LOCAL_APIC_IDENTIFIER,
            OS_TEST_PCI_MODEL_MSIX_INTERRUPT_VECTOR, false,
            msix_entry) == os::kernel::PciCapabilityStatus::Succeeded &&
        msix_entry.message_address_low ==
            OS_TEST_PCI_MODEL_EXPECTED_MSIX_MESSAGE_ADDRESS &&
        msix_entry.message_address_high == 0U &&
        msix_entry.message_data == OS_TEST_PCI_MODEL_MSIX_INTERRUPT_VECTOR &&
        msix_entry.vector_control == 0U;
    const bool masked_entry_valid =
        os::kernel::BuildPciMsixTableEntry(
            OS_TEST_PCI_MODEL_LOCAL_APIC_IDENTIFIER,
            OS_TEST_PCI_MODEL_MSIX_INTERRUPT_VECTOR, true,
            msix_entry) == os::kernel::PciCapabilityStatus::Succeeded &&
        msix_entry.vector_control == OS_TEST_PCI_MODEL_MSIX_VECTOR_MASK;
    test_context.Expect(unmasked_entry_valid && masked_entry_valid,
                        OS_TEST_PCI_MODEL_MSIX_TABLE_ENTRY);
    return test_context.ExitCode();
}
