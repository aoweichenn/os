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
    [[nodiscard]] AtaPioStatus readSector(uint64_t logicalBlockAddress, uint8_t *buffer,
                                          uint64_t bufferSizeBytes) const noexcept;

  private:
    [[nodiscard]] AtaPioStatus waitUntilNotBusy() const noexcept;
    [[nodiscard]] AtaPioStatus waitForDataRequest() const noexcept;
    void applyDeviceSelectDelay() const noexcept;
};

}
