#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint8_t OS_KERNEL_PIC_INITIAL_MASK = 0xFFU;

enum class LegacyPicStatus : uint64_t {
    Succeeded,
    InvalidInterruptRequest,
    SpuriousInterrupt,
};

class LegacyPic final {
  public:
    constexpr LegacyPic() noexcept = default;

    void Initialize() noexcept;
    [[nodiscard]] LegacyPicStatus EnableInterruptRequest(uint64_t interrupt_request) noexcept;
    [[nodiscard]] LegacyPicStatus Acknowledge(uint64_t interrupt_request) noexcept;
    [[nodiscard]] uint16_t Mask() const noexcept;

  private:
    [[nodiscard]] uint8_t ReadInServiceRegister(bool slave) const noexcept;
    void WriteMasks() const noexcept;

    uint8_t master_mask_{OS_KERNEL_PIC_INITIAL_MASK};
    uint8_t slave_mask_{OS_KERNEL_PIC_INITIAL_MASK};
};

}
