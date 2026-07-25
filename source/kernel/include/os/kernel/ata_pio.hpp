#pragma once

#include "os/kernel/block_cache.hpp"

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

class AtaPioDevice final : public FileSystemBlockDevice {
  public:
    [[nodiscard]] AtaPioStatus ReadSector(uint64_t logicalBlockAddress, uint8_t *buffer,
                                          uint64_t bufferSizeBytes) const noexcept;
    [[nodiscard]] AtaPioStatus WriteSector(uint64_t logicalBlockAddress, const uint8_t *buffer,
                                           uint64_t bufferSizeBytes) const noexcept;
    [[nodiscard]] AtaPioStatus FlushCache() const noexcept;

    [[nodiscard]] FileSystemBlockDeviceStatus
    ReadBlock(uint64_t logicalBlockAddress, uint8_t *block,
              uint64_t blockSizeBytes) noexcept override;
    [[nodiscard]] FileSystemBlockDeviceStatus
    WriteBlock(uint64_t logicalBlockAddress, const uint8_t *block,
               uint64_t blockSizeBytes) noexcept override;
    [[nodiscard]] FileSystemBlockDeviceStatus Flush() noexcept override;

  private:
    [[nodiscard]] AtaPioStatus PrepareSectorRequest(uint64_t logicalBlockAddress,
                                                    uint8_t command) const noexcept;
    [[nodiscard]] AtaPioStatus WaitUntilNotBusy() const noexcept;
    [[nodiscard]] AtaPioStatus WaitForDataRequest() const noexcept;
    void ApplyDeviceSelectDelay() const noexcept;
};

}
