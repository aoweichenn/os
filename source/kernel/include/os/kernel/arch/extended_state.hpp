#pragma once

#include "os/kernel/arch/extended_state_layout.hpp"

#include <stdint.h>

namespace os::kernel {

enum class ExtendedStateStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    NotInitialized,
    UnsupportedProcessor,
    InvalidControlRegisters,
    MisalignedArea,
};

struct ExtendedStateConfiguration final {
    ExtendedStateFeatures features;
    uint64_t control_register0;
    uint64_t control_register4;
    uint64_t save_count;
    uint64_t restore_count;
    bool avx_disabled;
    bool initialized;
};

[[nodiscard]] ExtendedStateStatus InitializeExtendedState() noexcept;
[[nodiscard]] ExtendedStateStatus InitializeFxSaveArea(FxSaveArea &area) noexcept;
[[nodiscard]] ExtendedStateStatus SaveFxState(FxSaveArea &area) noexcept;
[[nodiscard]] ExtendedStateStatus RestoreFxState(const FxSaveArea &area) noexcept;
[[nodiscard]] ExtendedStateConfiguration GetExtendedStateConfiguration() noexcept;

}
