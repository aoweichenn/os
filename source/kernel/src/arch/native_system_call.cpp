#include "os/kernel/arch/native_system_call.hpp"

#include "os/kernel/arch/descriptor_tables.hpp"
#include "os/kernel/arch/processor.hpp"

namespace os::kernel {

namespace {

constexpr uint32_t OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_EFER_MSR = 0xC0000080U;
constexpr uint32_t OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_STAR_MSR = 0xC0000081U;
constexpr uint32_t OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_LSTAR_MSR = 0xC0000082U;
constexpr uint32_t OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_FMASK_MSR = 0xC0000084U;
constexpr uint32_t OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_GS_BASE_MSR = 0xC0000101U;
constexpr uint32_t OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_KERNEL_GS_BASE_MSR = 0xC0000102U;
constexpr uint64_t OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_CPU_LOCAL_ADDRESS = 0ULL;

NativeSystemCallConfiguration native_system_call_configuration;

extern "C" void OsKernelNativeSystemCallEntry() noexcept;

[[nodiscard]] NativeSystemCallRegisterValues ReadNativeSystemCallRegisterValues() noexcept {
    return NativeSystemCallRegisterValues{
        .extended_feature_enable_register =
            ReadModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_EFER_MSR),
        .segment_selector_register =
            ReadModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_STAR_MSR),
        .entry_instruction_pointer_register =
            ReadModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_LSTAR_MSR),
        .flags_mask_register =
            ReadModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_FMASK_MSR),
        .user_gs_base_register =
            ReadModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_GS_BASE_MSR),
        .kernel_gs_base_register =
            ReadModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_KERNEL_GS_BASE_MSR),
    };
}

}

NativeSystemCallStatus InitializeNativeSystemCalls(const ProcessorFeatureProfile &profile,
                                                   const uint64_t cpu_local_address) noexcept {
    if (native_system_call_configuration.initialized) {
        return NativeSystemCallStatus::AlreadyInitialized;
    }
    if (ValidateProcessorFeatureProfile(profile) != ProcessorFeatureStatus::Succeeded ||
        !profile.native_system_call) {
        return NativeSystemCallStatus::UnsupportedProcessor;
    }
    if (cpu_local_address == OS_KERNEL_NATIVE_SYSTEM_CALL_EMPTY_CPU_LOCAL_ADDRESS) {
        return NativeSystemCallStatus::InvalidCpuLocal;
    }

    NativeSystemCallRegisterValues expected{};
    if (BuildNativeSystemCallRegisterValues(
            ReadModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_EFER_MSR),
            OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR, OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR,
            OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR,
            reinterpret_cast<uint64_t>(&OsKernelNativeSystemCallEntry), cpu_local_address,
            profile.virtual_address_width_bits,
            expected) != NativeSystemCallLayoutStatus::Succeeded) {
        return NativeSystemCallStatus::InvalidRegisterLayout;
    }

    WriteModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_STAR_MSR,
                               expected.segment_selector_register);
    WriteModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_LSTAR_MSR,
                               expected.entry_instruction_pointer_register);
    WriteModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_FMASK_MSR,
                               expected.flags_mask_register);
    WriteModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_GS_BASE_MSR,
                               expected.user_gs_base_register);
    WriteModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_KERNEL_GS_BASE_MSR,
                               expected.kernel_gs_base_register);
    WriteModelSpecificRegister(OS_KERNEL_NATIVE_SYSTEM_CALL_IA32_EFER_MSR,
                               expected.extended_feature_enable_register);

    const NativeSystemCallRegisterValues observed = ReadNativeSystemCallRegisterValues();
    if (ValidateNativeSystemCallRegisterValues(expected, observed) !=
        NativeSystemCallLayoutStatus::Succeeded) {
        return NativeSystemCallStatus::RegisterVerificationFailed;
    }
    native_system_call_configuration = NativeSystemCallConfiguration{
        .registers = observed,
        .virtual_address_width_bits = profile.virtual_address_width_bits,
        .initialized = true,
    };
    return NativeSystemCallStatus::Succeeded;
}

NativeSystemCallConfiguration GetNativeSystemCallConfiguration() noexcept {
    return native_system_call_configuration;
}

}
