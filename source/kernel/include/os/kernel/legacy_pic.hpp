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

    void Initialize() noexcept;
    [[nodiscard]] LegacyPicStatus EnableInterruptRequest(uint64_t interruptRequest) noexcept;
    [[nodiscard]] LegacyPicStatus Acknowledge(uint64_t interruptRequest) noexcept;
    [[nodiscard]] uint16_t Mask() const noexcept;

  private:
    [[nodiscard]] uint8_t ReadInServiceRegister(bool slave) const noexcept;
    void WriteMasks() const noexcept;

    uint8_t masterMask_;
    uint8_t slaveMask_;
};

}
