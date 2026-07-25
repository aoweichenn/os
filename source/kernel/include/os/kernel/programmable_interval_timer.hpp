#pragma once

#include "os/kernel/device_model.hpp"

namespace os::kernel {

class ProgrammableIntervalTimer final {
  public:
    [[nodiscard]] PitConfigurationStatus initialize(uint64_t requestedFrequencyHz,
                                                    PitConfiguration &configuration) const noexcept;
};

}
