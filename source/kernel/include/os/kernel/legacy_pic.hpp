#pragma once

#include <stdint.h>

namespace os::kernel {

enum class LegacyPicStatus : uint64_t {
    Succeeded,
    InvalidInterruptRequest,
    SpuriousInterrupt,
};

class LegacyPic final {
  public:
    LegacyPic() noexcept;

    void initialize() noexcept;
    [[nodiscard]] LegacyPicStatus enableInterruptRequest(uint64_t interruptRequest) noexcept;
    [[nodiscard]] LegacyPicStatus acknowledge(uint64_t interruptRequest) noexcept;
    [[nodiscard]] uint16_t mask() const noexcept;

  private:
    [[nodiscard]] uint8_t readInServiceRegister(bool slave) const noexcept;
    void writeMasks() const noexcept;

    uint8_t masterMask_;
    uint8_t slaveMask_;
};

}
