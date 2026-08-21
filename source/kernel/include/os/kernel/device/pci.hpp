#pragma once

#include <os/kernel/arch/processor.hpp>
#include <os/kernel/device/pci_model.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

enum class PciConfigurationAccessStatus : uint64_t {
    Succeeded,
    InvalidAddress,
};

class PciConfigurationSpace final {
  public:
    constexpr PciConfigurationSpace() noexcept = default;
    [[nodiscard]] PciConfigurationAccessStatus
    ReadWord(const PciDeviceAddress &device_address, uint64_t register_offset_bytes,
             uint16_t &value) noexcept;
    [[nodiscard]] PciConfigurationAccessStatus
    WriteWord(const PciDeviceAddress &device_address, uint64_t register_offset_bytes,
              uint16_t value) noexcept;
    [[nodiscard]] PciConfigurationAccessStatus
    ReadDword(const PciDeviceAddress &device_address, uint64_t register_offset_bytes,
              uint32_t &value) noexcept;
    [[nodiscard]] PciConfigurationAccessStatus
    WriteDword(const PciDeviceAddress &device_address, uint64_t register_offset_bytes,
               uint32_t value) noexcept;

  private:
    IrqSaveSpinLock lock_{DisableInterrupts, RestoreInterrupts};
};

struct PciNvmeController final {
    PciDeviceAddress address;
    PciDeviceIdentity identity;
};

enum class PciNvmeScanStatus : uint64_t {
    Succeeded,
    NotFound,
    MultipleControllers,
    AccessFailed,
};

[[nodiscard]] PciNvmeScanStatus FindPciNvmeController(PciConfigurationSpace &configuration,
                                                      PciNvmeController &controller) noexcept;

struct PciMemoryBarAssignment final {
    PciDeviceAddress device_address;
    uint64_t bar_index;
    uint64_t physical_address;
    uint64_t size_bytes;
    uint16_t original_command;
    uint32_t original_low_value;
    uint32_t original_high_value;
    PciMemoryBaseAddressKind kind;
    bool active;
};

enum class PciMemoryBarAssignmentStatus : uint64_t {
    Succeeded,
    InvalidArgument,
    AccessFailed,
    UnsupportedBar,
    SizeProbeFailed,
    ResourceUnavailable,
    ProgrammingFailed,
    RestoreFailed,
};

[[nodiscard]] PciMemoryBarAssignmentStatus AssignPciMemoryBar(
    PciConfigurationSpace &configuration, const PciDeviceAddress &device_address,
    uint64_t bar_index, uint64_t window_begin_address, uint64_t window_end_address,
    PciMemoryBarAssignment &assignment) noexcept;
[[nodiscard]] PciMemoryBarAssignmentStatus
RestorePciMemoryBar(PciConfigurationSpace &configuration,
                    PciMemoryBarAssignment &assignment) noexcept;

struct PciMsixCapabilityLocation final {
    uint64_t capability_offset_bytes;
    uint16_t original_message_control;
    PciMsixCapability capability;
};

enum class PciMsixAccessStatus : uint64_t {
    Succeeded,
    NotSupported,
    AccessFailed,
    CorruptCapabilityList,
    InvalidCapability,
};

[[nodiscard]] PciMsixAccessStatus FindPciMsixCapability(
    PciConfigurationSpace &configuration, const PciDeviceAddress &device_address,
    PciMsixCapabilityLocation &location) noexcept;
[[nodiscard]] PciMsixAccessStatus WritePciMsixMessageControl(
    PciConfigurationSpace &configuration, const PciDeviceAddress &device_address,
    uint64_t capability_offset_bytes, uint16_t message_control) noexcept;

}
