#include "os/kernel/arch/extended_state.hpp"

#include "os/kernel/arch/processor.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_EXTENDED_STATE_CR0_MONITOR_COPROCESSOR_BIT =
    0x0000000000000002ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_CR0_EMULATION_BIT =
    0x0000000000000004ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_CR0_TASK_SWITCHED_BIT =
    0x0000000000000008ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_CR0_NUMERIC_ERROR_BIT =
    0x0000000000000020ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_CR4_OS_FX_SAVE_RESTORE_BIT =
    0x0000000000000200ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_CR4_OS_XMM_EXCEPTION_BIT =
    0x0000000000000400ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_CR4_OS_XSAVE_BIT =
    0x0000000000040000ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_EMPTY_COUNT = 0ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_EXTENDED_STATE_FIRST_BYTE_INDEX = 0ULL;

FxSaveArea initial_fx_save_area;
ExtendedStateConfiguration extended_state_configuration;

extern "C" void OsKernelInitializeFxState(FxSaveArea *area) noexcept;
extern "C" void OsKernelSaveFxState(FxSaveArea *area) noexcept;
extern "C" void OsKernelRestoreFxState(const FxSaveArea *area) noexcept;

}

ExtendedStateStatus InitializeExtendedState() noexcept {
    if (extended_state_configuration.initialized) {
        return ExtendedStateStatus::AlreadyInitialized;
    }
    const ExtendedStateFeatures features =
        DecodeExtendedStateFeatures(ProcessorStandardFeatureBits());
    if (!RequiredExtendedStateFeaturesAvailable(features)) {
        extended_state_configuration.features = features;
        return ExtendedStateStatus::UnsupportedProcessor;
    }

    const uint64_t control_register0 =
        (ReadControlRegister0() |
         OS_KERNEL_EXTENDED_STATE_CR0_MONITOR_COPROCESSOR_BIT |
         OS_KERNEL_EXTENDED_STATE_CR0_NUMERIC_ERROR_BIT) &
        ~(OS_KERNEL_EXTENDED_STATE_CR0_EMULATION_BIT |
          OS_KERNEL_EXTENDED_STATE_CR0_TASK_SWITCHED_BIT);
    const uint64_t control_register4 =
        (ReadControlRegister4() |
         OS_KERNEL_EXTENDED_STATE_CR4_OS_FX_SAVE_RESTORE_BIT |
         OS_KERNEL_EXTENDED_STATE_CR4_OS_XMM_EXCEPTION_BIT) &
        ~OS_KERNEL_EXTENDED_STATE_CR4_OS_XSAVE_BIT;
    WriteControlRegister0(control_register0);
    WriteControlRegister4(control_register4);

    const uint64_t verified_control_register0 = ReadControlRegister0();
    const uint64_t verified_control_register4 = ReadControlRegister4();
    const bool control_register0_valid =
        (verified_control_register0 &
         (OS_KERNEL_EXTENDED_STATE_CR0_MONITOR_COPROCESSOR_BIT |
          OS_KERNEL_EXTENDED_STATE_CR0_NUMERIC_ERROR_BIT)) ==
            (OS_KERNEL_EXTENDED_STATE_CR0_MONITOR_COPROCESSOR_BIT |
             OS_KERNEL_EXTENDED_STATE_CR0_NUMERIC_ERROR_BIT) &&
        (verified_control_register0 &
         (OS_KERNEL_EXTENDED_STATE_CR0_EMULATION_BIT |
          OS_KERNEL_EXTENDED_STATE_CR0_TASK_SWITCHED_BIT)) ==
            OS_KERNEL_EXTENDED_STATE_EMPTY_COUNT;
    const bool control_register4_valid =
        (verified_control_register4 &
         (OS_KERNEL_EXTENDED_STATE_CR4_OS_FX_SAVE_RESTORE_BIT |
          OS_KERNEL_EXTENDED_STATE_CR4_OS_XMM_EXCEPTION_BIT)) ==
            (OS_KERNEL_EXTENDED_STATE_CR4_OS_FX_SAVE_RESTORE_BIT |
             OS_KERNEL_EXTENDED_STATE_CR4_OS_XMM_EXCEPTION_BIT) &&
        (verified_control_register4 &
         OS_KERNEL_EXTENDED_STATE_CR4_OS_XSAVE_BIT) ==
            OS_KERNEL_EXTENDED_STATE_EMPTY_COUNT;
    if (!control_register0_valid || !control_register4_valid ||
        !IsFxSaveAreaAligned(initial_fx_save_area)) {
        return ExtendedStateStatus::InvalidControlRegisters;
    }

    OsKernelInitializeFxState(&initial_fx_save_area);
    extended_state_configuration = ExtendedStateConfiguration{
        .features = features,
        .control_register0 = verified_control_register0,
        .control_register4 = verified_control_register4,
        .save_count = OS_KERNEL_EXTENDED_STATE_EMPTY_COUNT,
        .restore_count = OS_KERNEL_EXTENDED_STATE_EMPTY_COUNT,
        .avx_disabled = true,
        .initialized = true,
    };
    return ExtendedStateStatus::Succeeded;
}

ExtendedStateStatus InitializeFxSaveArea(FxSaveArea &area) noexcept {
    if (!extended_state_configuration.initialized) {
        return ExtendedStateStatus::NotInitialized;
    }
    if (!IsFxSaveAreaAligned(area)) {
        return ExtendedStateStatus::MisalignedArea;
    }
    for (uint64_t byte_index = OS_KERNEL_EXTENDED_STATE_FIRST_BYTE_INDEX;
         byte_index < OS_KERNEL_EXTENDED_STATE_AREA_SIZE_BYTES; ++byte_index) {
        area.bytes[byte_index] = initial_fx_save_area.bytes[byte_index];
    }
    return ExtendedStateStatus::Succeeded;
}

ExtendedStateStatus SaveFxState(FxSaveArea &area) noexcept {
    if (!extended_state_configuration.initialized) {
        return ExtendedStateStatus::NotInitialized;
    }
    if (!IsFxSaveAreaAligned(area)) {
        return ExtendedStateStatus::MisalignedArea;
    }
    OsKernelSaveFxState(&area);
    extended_state_configuration.save_count +=
        OS_KERNEL_EXTENDED_STATE_COUNTER_INCREMENT;
    return ExtendedStateStatus::Succeeded;
}

ExtendedStateStatus RestoreFxState(const FxSaveArea &area) noexcept {
    if (!extended_state_configuration.initialized) {
        return ExtendedStateStatus::NotInitialized;
    }
    if (!IsFxSaveAreaAligned(area)) {
        return ExtendedStateStatus::MisalignedArea;
    }
    OsKernelRestoreFxState(&area);
    extended_state_configuration.restore_count +=
        OS_KERNEL_EXTENDED_STATE_COUNTER_INCREMENT;
    return ExtendedStateStatus::Succeeded;
}

ExtendedStateConfiguration GetExtendedStateConfiguration() noexcept {
    return extended_state_configuration;
}

}
