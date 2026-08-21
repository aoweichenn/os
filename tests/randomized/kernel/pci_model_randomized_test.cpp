#include <os/kernel/device/pci_model.hpp>
#include <test_context.hpp>

#include <random>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_PCI_RANDOM_SUITE_NAME = "kernel/pci_model/randomized";
constexpr std::string_view OS_TEST_PCI_RANDOM_CONFIGURATION_ROUND_TRIP =
    "随机 PCI BDF 与 DWORD 偏移必须精确往返";
constexpr std::string_view OS_TEST_PCI_RANDOM_BAR_SIZE =
    "随机 32/64 位 BAR 二次幂掩码必须还原精确 aperture";
constexpr std::string_view OS_TEST_PCI_RANDOM_MSIX_ENTRY =
    "随机 MSI-X LAPIC 目标与向量必须精确编码 message address/data";
constexpr os::test::RandomSeed OS_TEST_PCI_RANDOM_SEED = 0x5043494E564D4531ULL;
constexpr os::test::TestCount OS_TEST_PCI_RANDOM_ITERATION_COUNT = 4096ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MINIMUM_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MAXIMUM_BUS = os::kernel::OS_KERNEL_PCI_BUS_COUNT - 1ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MAXIMUM_DEVICE =
    os::kernel::OS_KERNEL_PCI_DEVICE_COUNT_PER_BUS - 1ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MAXIMUM_FUNCTION =
    os::kernel::OS_KERNEL_PCI_FUNCTION_COUNT_PER_DEVICE - 1ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_CONFIGURATION_DWORD_COUNT =
    os::kernel::OS_KERNEL_PCI_CONFIGURATION_SPACE_SIZE_BYTES /
    os::kernel::OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES;
constexpr uint64_t OS_TEST_PCI_RANDOM_MINIMUM_BAR_ORDER = 4ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MAXIMUM_32_BIT_BAR_ORDER = 31ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MAXIMUM_64_BIT_BAR_ORDER = 48ULL;
constexpr uint32_t OS_TEST_PCI_RANDOM_BAR_ADDRESS_MASK = 0xFFFFFFF0U;
constexpr uint32_t OS_TEST_PCI_RANDOM_64_BIT_BAR_TYPE = 0x00000004U;
constexpr uint64_t OS_TEST_PCI_RANDOM_DWORD_BIT_COUNT = 32ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MAXIMUM_LOCAL_APIC_IDENTIFIER = 0xFFULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MINIMUM_INTERRUPT_VECTOR = 0x20ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MAXIMUM_INTERRUPT_VECTOR = 0xFEULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MSIX_MESSAGE_ADDRESS_BASE = 0xFEE00000ULL;
constexpr uint64_t OS_TEST_PCI_RANDOM_MSIX_DESTINATION_SHIFT_BITS = 12ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_PCI_RANDOM_SUITE_NAME};
    std::mt19937_64 generator{OS_TEST_PCI_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> bus_distribution{
        OS_TEST_PCI_RANDOM_MINIMUM_VALUE, OS_TEST_PCI_RANDOM_MAXIMUM_BUS};
    std::uniform_int_distribution<uint64_t> device_distribution{
        OS_TEST_PCI_RANDOM_MINIMUM_VALUE, OS_TEST_PCI_RANDOM_MAXIMUM_DEVICE};
    std::uniform_int_distribution<uint64_t> function_distribution{
        OS_TEST_PCI_RANDOM_MINIMUM_VALUE, OS_TEST_PCI_RANDOM_MAXIMUM_FUNCTION};
    std::uniform_int_distribution<uint64_t> register_distribution{
        OS_TEST_PCI_RANDOM_MINIMUM_VALUE,
        OS_TEST_PCI_RANDOM_CONFIGURATION_DWORD_COUNT - 1ULL};
    std::uniform_int_distribution<uint64_t> bar_kind_distribution{0ULL, 1ULL};
    std::uniform_int_distribution<uint64_t> bar32_order_distribution{
        OS_TEST_PCI_RANDOM_MINIMUM_BAR_ORDER, OS_TEST_PCI_RANDOM_MAXIMUM_32_BIT_BAR_ORDER};
    std::uniform_int_distribution<uint64_t> bar64_order_distribution{
        OS_TEST_PCI_RANDOM_MINIMUM_BAR_ORDER, OS_TEST_PCI_RANDOM_MAXIMUM_64_BIT_BAR_ORDER};
    std::uniform_int_distribution<uint64_t> local_apic_identifier_distribution{
        OS_TEST_PCI_RANDOM_MINIMUM_VALUE,
        OS_TEST_PCI_RANDOM_MAXIMUM_LOCAL_APIC_IDENTIFIER};
    std::uniform_int_distribution<uint64_t> interrupt_vector_distribution{
        OS_TEST_PCI_RANDOM_MINIMUM_INTERRUPT_VECTOR,
        OS_TEST_PCI_RANDOM_MAXIMUM_INTERRUPT_VECTOR};

    for (os::test::TestCount iteration = 0ULL;
         iteration < OS_TEST_PCI_RANDOM_ITERATION_COUNT; ++iteration) {
        const os::kernel::PciDeviceAddress expected_address{
            .bus_number = bus_distribution(generator),
            .device_number = device_distribution(generator),
            .function_number = function_distribution(generator),
        };
        const uint64_t expected_register_offset_bytes =
            register_distribution(generator) *
            os::kernel::OS_KERNEL_PCI_CONFIGURATION_DWORD_SIZE_BYTES;
        uint32_t encoded_address = 0U;
        os::kernel::PciDeviceAddress decoded_address{};
        uint64_t decoded_register_offset_bytes = 0ULL;
        test_context.ExpectRandom(
            os::kernel::EncodePciConfigurationAddress(
                expected_address, expected_register_offset_bytes, encoded_address) ==
                    os::kernel::PciConfigurationAddressStatus::Succeeded &&
                os::kernel::DecodePciConfigurationAddress(
                    encoded_address, decoded_address, decoded_register_offset_bytes) ==
                    os::kernel::PciConfigurationAddressStatus::Succeeded &&
                decoded_address.bus_number == expected_address.bus_number &&
                decoded_address.device_number == expected_address.device_number &&
                decoded_address.function_number == expected_address.function_number &&
                decoded_register_offset_bytes == expected_register_offset_bytes,
            OS_TEST_PCI_RANDOM_CONFIGURATION_ROUND_TRIP, OS_TEST_PCI_RANDOM_SEED, iteration);

        const bool use_64_bit_bar = bar_kind_distribution(generator) != 0ULL;
        const uint64_t bar_order = use_64_bit_bar ? bar64_order_distribution(generator)
                                                  : bar32_order_distribution(generator);
        const uint64_t expected_size_bytes = 1ULL << bar_order;
        const uint64_t full_mask = ~(expected_size_bytes - 1ULL);
        const uint32_t low_mask =
            (static_cast<uint32_t>(full_mask) & OS_TEST_PCI_RANDOM_BAR_ADDRESS_MASK) |
            (use_64_bit_bar ? OS_TEST_PCI_RANDOM_64_BIT_BAR_TYPE : 0U);
        const uint32_t high_mask =
            use_64_bit_bar
                ? static_cast<uint32_t>(full_mask >> OS_TEST_PCI_RANDOM_DWORD_BIT_COUNT)
                : 0U;
        uint64_t actual_size_bytes = 0ULL;
        test_context.ExpectRandom(
            os::kernel::CalculatePciMemoryBaseAddressSize(low_mask, high_mask,
                                                          actual_size_bytes) ==
                    os::kernel::PciMemoryBaseAddressStatus::Succeeded &&
                actual_size_bytes == expected_size_bytes,
            OS_TEST_PCI_RANDOM_BAR_SIZE, OS_TEST_PCI_RANDOM_SEED, iteration);

        const uint64_t local_apic_identifier =
            local_apic_identifier_distribution(generator);
        const uint64_t interrupt_vector = interrupt_vector_distribution(generator);
        os::kernel::PciMsixTableEntry msix_entry{};
        test_context.ExpectRandom(
            os::kernel::BuildPciMsixTableEntry(local_apic_identifier, interrupt_vector,
                                               false, msix_entry) ==
                    os::kernel::PciCapabilityStatus::Succeeded &&
                msix_entry.message_address_low ==
                    static_cast<uint32_t>(OS_TEST_PCI_RANDOM_MSIX_MESSAGE_ADDRESS_BASE |
                                          local_apic_identifier
                                              << OS_TEST_PCI_RANDOM_MSIX_DESTINATION_SHIFT_BITS) &&
                msix_entry.message_address_high == 0U &&
                msix_entry.message_data == interrupt_vector &&
                msix_entry.vector_control == 0U,
            OS_TEST_PCI_RANDOM_MSIX_ENTRY, OS_TEST_PCI_RANDOM_SEED, iteration);
    }
    return test_context.ExitCode();
}
