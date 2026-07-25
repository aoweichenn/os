#pragma once

#include "os/kernel/device_model.hpp"

namespace os::kernel {

class ProgrammableIntervalTimer final {
  public:
    [[nodiscard]] PitConfigurationStatus Initialize(uint64_t requested_frequency_hz,
                                                    PitConfiguration &configuration) const noexcept;
};

}
