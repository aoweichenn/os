#pragma once

#include <stdint.h>

namespace os::kernel {

enum class AtaPioStatus : uint64_t {
    Succeeded,
    NullBuffer,
    InvalidBufferSize,
    InvalidLogicalBlockAddress,
    BusyTimeout,
    DataRequestTimeout,
    DeviceError,
};

class AtaPioDevice final {
  public:
    [[nodiscard]] AtaPioStatus ReadSector(uint64_t logicalBlockAddress, uint8_t *buffer,
                                          uint64_t bufferSizeBytes) const noexcept;

  private:
    [[nodiscard]] AtaPioStatus WaitUntilNotBusy() const noexcept;
    [[nodiscard]] AtaPioStatus WaitForDataRequest() const noexcept;
    void ApplyDeviceSelectDelay() const noexcept;
};

}
