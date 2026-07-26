#pragma once

#include "os/kernel/arch/native_system_call_layout.hpp"
#include "os/kernel/arch/processor_features.hpp"

#include <stdint.h>

namespace os::kernel {

enum class NativeSystemCallStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    UnsupportedProcessor,
    InvalidCpuLocal,
    InvalidRegisterLayout,
    RegisterVerificationFailed,
};

struct NativeSystemCallConfiguration final {
    NativeSystemCallRegisterValues registers;
    uint64_t virtual_address_width_bits;
    bool initialized;
};

[[nodiscard]] NativeSystemCallStatus
InitializeNativeSystemCalls(const ProcessorFeatureProfile &profile,
                            uint64_t cpu_local_address) noexcept;
[[nodiscard]] NativeSystemCallConfiguration GetNativeSystemCallConfiguration() noexcept;

}
