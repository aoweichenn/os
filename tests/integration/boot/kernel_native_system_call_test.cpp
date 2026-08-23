#include "os/kernel/arch/cpu_local.hpp"
#include "os/kernel/arch/native_system_call_layout.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_SUITE_NAME =
    "kernel/native_system_call/integration";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_INITIALIZATION_MESSAGE =
    "CpuLocal 必须拒绝未初始化访问和无效自举栈";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_OWNERSHIP_MESSAGE =
    "线程所有权与可信内核入口栈必须成对更新";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_DEPTH_MESSAGE =
    "IRQ 与抢占深度必须平衡并记录峰值";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_ENTRY_MESSAGE =
    "系统调用入口状态必须区分原生与兼容路径并拒绝嵌套";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_SUSPEND_MESSAGE =
    "阻塞式内核续体必须挂起并恢复原系统调用入口方法";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_RETURN_MESSAGE =
    "返回、延迟调度和可信栈证据必须形成一致统计";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_CLEAR_MESSAGE =
    "非局部返回清理线程时必须同时清除原生入口和用户 RSP 暂存";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_REGISTER_MESSAGE =
    "EFER、STAR、LSTAR、FMASK 与双 GS 基址必须形成可回读布局";
constexpr std::string_view OS_TEST_NATIVE_SYSTEM_CALL_REJECTION_MESSAGE =
    "非法段、地址和回读差异必须在写 MSR 前被拒绝";

constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_BOOTSTRAP_STACK = 0x1000ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_THREAD_STACK = 0x2000ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_MISALIGNED_STACK = 0x2001ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_THREAD_INDEX = 7ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_PREEMPTION_DEPTH = 2ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_INTERRUPT_DEPTH = 2ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_NATIVE_ENTRIES = 2ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_LEGACY_ENTRIES = 1ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_INTERRUPT_COUNT = 2ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_INTERRUPT_RETURNS = 2ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_SINGLE_COUNT = 1ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_USER_STACK_POINTER = 0x00007FFFFFFEFFC0ULL;

constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_CURRENT_EFER = 0x500ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_KERNEL_CODE_SEGMENT = 0x08ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_USER_STACK_SEGMENT = 0x1BULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_USER_CODE_SEGMENT = 0x23ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_ENTRY_ADDRESS = 0xFFFF800000001000ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_CPU_LOCAL_ADDRESS = 0xFFFF888000002000ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_VIRTUAL_WIDTH_BITS = 48ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_INVALID_VIRTUAL_WIDTH_BITS = 47ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_EFER = 0x501ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_STAR = 0x0010000800000000ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS = 0ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_INVALID_USER_CODE_SEGMENT = 0x1BULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_INVALID_USER_STACK_SEGMENT = 0x23ULL;
constexpr uint64_t OS_TEST_NATIVE_SYSTEM_CALL_REGISTER_DIFFERENCE = 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_NATIVE_SYSTEM_CALL_SUITE_NAME};
    os::kernel::CpuLocal cpu_local{};

    test_context.Expect(cpu_local.Validate() == os::kernel::CpuLocalStatus::NotInitialized &&
                            cpu_local.SetCurrentThread(OS_TEST_NATIVE_SYSTEM_CALL_THREAD_INDEX,
                                                       OS_TEST_NATIVE_SYSTEM_CALL_THREAD_STACK) ==
                                os::kernel::CpuLocalStatus::NotInitialized &&
                            cpu_local.Initialize(OS_TEST_NATIVE_SYSTEM_CALL_MISALIGNED_STACK) ==
                                os::kernel::CpuLocalStatus::InvalidStackPointer &&
                            cpu_local.Initialize(OS_TEST_NATIVE_SYSTEM_CALL_BOOTSTRAP_STACK) ==
                                os::kernel::CpuLocalStatus::Succeeded &&
                            cpu_local.Initialize(OS_TEST_NATIVE_SYSTEM_CALL_BOOTSTRAP_STACK) ==
                                os::kernel::CpuLocalStatus::AlreadyInitialized,
                        OS_TEST_NATIVE_SYSTEM_CALL_INITIALIZATION_MESSAGE);

    test_context.Expect(
        cpu_local.SetCurrentThread(os::kernel::OS_KERNEL_CPU_LOCAL_INVALID_THREAD_INDEX,
                                   OS_TEST_NATIVE_SYSTEM_CALL_THREAD_STACK) ==
                os::kernel::CpuLocalStatus::InvalidThreadIndex &&
            cpu_local.SetCurrentThread(OS_TEST_NATIVE_SYSTEM_CALL_THREAD_INDEX,
                                       OS_TEST_NATIVE_SYSTEM_CALL_MISALIGNED_STACK) ==
                os::kernel::CpuLocalStatus::InvalidStackPointer &&
            cpu_local.SetCurrentThread(OS_TEST_NATIVE_SYSTEM_CALL_THREAD_INDEX,
                                       OS_TEST_NATIVE_SYSTEM_CALL_THREAD_STACK) ==
                os::kernel::CpuLocalStatus::Succeeded &&
            cpu_local.Statistics().current_thread_index ==
                OS_TEST_NATIVE_SYSTEM_CALL_THREAD_INDEX &&
            cpu_local.Statistics().kernel_entry_stack_pointer ==
                OS_TEST_NATIVE_SYSTEM_CALL_THREAD_STACK,
        OS_TEST_NATIVE_SYSTEM_CALL_OWNERSHIP_MESSAGE);

    const bool preemption_balanced =
        cpu_local.DisablePreemption() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.DisablePreemption() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.EnablePreemption() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.EnablePreemption() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.EnablePreemption() == os::kernel::CpuLocalStatus::InvalidState;

    const bool invalid_entry_rejected =
        cpu_local.BeginSystemCall(os::kernel::UserContextEntryMethod::Initial) ==
        os::kernel::CpuLocalStatus::InvalidEntryMethod;
    const bool native_entry_started =
        cpu_local.BeginSystemCall(os::kernel::UserContextEntryMethod::NativeSystemCall) ==
            os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.NativeSystemCallActive() &&
        cpu_local.BeginSystemCall(os::kernel::UserContextEntryMethod::NativeSystemCall) ==
            os::kernel::CpuLocalStatus::InvalidState;
    test_context.Expect(invalid_entry_rejected && native_entry_started,
                        OS_TEST_NATIVE_SYSTEM_CALL_ENTRY_MESSAGE);

    const bool interrupts_balanced =
        cpu_local.EnterInterrupt() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.EnterInterrupt() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.LeaveInterrupt() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.LeaveInterrupt() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.LeaveInterrupt() == os::kernel::CpuLocalStatus::InvalidState;
    test_context.Expect(preemption_balanced && interrupts_balanced &&
                            cpu_local.Statistics().maximum_preemption_disable_depth ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_PREEMPTION_DEPTH &&
                            cpu_local.Statistics().maximum_interrupt_depth ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_INTERRUPT_DEPTH,
                        OS_TEST_NATIVE_SYSTEM_CALL_DEPTH_MESSAGE);

    os::kernel::UserContextEntryMethod suspended_entry_method =
        os::kernel::UserContextEntryMethod::Invalid;
    const bool system_call_suspended_and_resumed =
        cpu_local.SuspendSystemCall(suspended_entry_method) ==
            os::kernel::CpuLocalStatus::Succeeded &&
        suspended_entry_method == os::kernel::UserContextEntryMethod::NativeSystemCall &&
        !cpu_local.NativeSystemCallActive() &&
        cpu_local.SuspendSystemCall(suspended_entry_method) ==
            os::kernel::CpuLocalStatus::InvalidState &&
        cpu_local.ResumeSystemCall(os::kernel::UserContextEntryMethod::Initial) ==
            os::kernel::CpuLocalStatus::InvalidState &&
        cpu_local.ResumeSystemCall(os::kernel::UserContextEntryMethod::NativeSystemCall) ==
            os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.NativeSystemCallActive() &&
        cpu_local.ResumeSystemCall(os::kernel::UserContextEntryMethod::NativeSystemCall) ==
            os::kernel::CpuLocalStatus::InvalidState;
    test_context.Expect(system_call_suspended_and_resumed,
                        OS_TEST_NATIVE_SYSTEM_CALL_SUSPEND_MESSAGE);

    cpu_local.RequestReschedule();
    const bool reschedule_consumed =
        cpu_local.ConsumeRescheduleRequest() && !cpu_local.ConsumeRescheduleRequest() &&
        cpu_local.RecordReturnReschedule() == os::kernel::CpuLocalStatus::Succeeded;
    const bool return_evidence_recorded =
        cpu_local.RecordTrustedStackValidation() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.RecordUserReturn(os::kernel::UserContextEntryMethod::NativeSystemCall,
                                   os::kernel::UserReturnMethod::SystemReturn) ==
            os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.RecordUserReturn(os::kernel::UserContextEntryMethod::NativeSystemCall,
                                   os::kernel::UserReturnMethod::InterruptReturn) ==
            os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.EndSystemCall() == os::kernel::CpuLocalStatus::Succeeded &&
        !cpu_local.NativeSystemCallActive() &&
        cpu_local.BeginSystemCall(os::kernel::UserContextEntryMethod::LegacyInterrupt) ==
            os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.RecordUserReturn(os::kernel::UserContextEntryMethod::LegacyInterrupt,
                                   os::kernel::UserReturnMethod::InterruptReturn) ==
            os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.EndSystemCall() == os::kernel::CpuLocalStatus::Succeeded &&
        cpu_local.RecordUserReturn(os::kernel::UserContextEntryMethod::Invalid,
                                   os::kernel::UserReturnMethod::Rejected) ==
            os::kernel::CpuLocalStatus::Succeeded;
    test_context.Expect(reschedule_consumed && return_evidence_recorded,
                        OS_TEST_NATIVE_SYSTEM_CALL_RETURN_MESSAGE);

    const bool non_local_state_created =
        cpu_local.BeginSystemCall(os::kernel::UserContextEntryMethod::NativeSystemCall) ==
        os::kernel::CpuLocalStatus::Succeeded;
    uint64_t *const system_call_user_stack_pointer = reinterpret_cast<uint64_t *>(
        cpu_local.Address() +
        os::kernel::OS_KERNEL_CPU_LOCAL_SYSTEM_CALL_USER_STACK_POINTER_OFFSET);
    *system_call_user_stack_pointer = OS_TEST_NATIVE_SYSTEM_CALL_USER_STACK_POINTER;
    const bool non_local_state_cleared =
        cpu_local.ClearCurrentThread(OS_TEST_NATIVE_SYSTEM_CALL_BOOTSTRAP_STACK) ==
            os::kernel::CpuLocalStatus::Succeeded &&
        !cpu_local.NativeSystemCallActive() &&
        cpu_local.SystemCallUserStackPointer() == OS_TEST_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS &&
        cpu_local.Validate() == os::kernel::CpuLocalStatus::Succeeded;
    const os::kernel::CpuLocalStatistics cpu_statistics = cpu_local.Statistics();
    test_context.Expect(non_local_state_created && non_local_state_cleared &&
                            cpu_statistics.current_thread_index ==
                                os::kernel::OS_KERNEL_CPU_LOCAL_INVALID_THREAD_INDEX &&
                            cpu_statistics.native_system_call_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_NATIVE_ENTRIES &&
                            cpu_statistics.legacy_system_call_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_LEGACY_ENTRIES &&
                            cpu_statistics.interrupt_during_system_call_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_INTERRUPT_COUNT &&
                            cpu_statistics.system_return_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_SINGLE_COUNT &&
                            cpu_statistics.interrupt_return_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_INTERRUPT_RETURNS &&
                            cpu_statistics.native_interrupt_return_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_SINGLE_COUNT &&
                            cpu_statistics.rejected_return_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_SINGLE_COUNT &&
                            cpu_statistics.return_reschedule_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_SINGLE_COUNT &&
                            cpu_statistics.trusted_stack_validation_count ==
                                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_SINGLE_COUNT,
                        OS_TEST_NATIVE_SYSTEM_CALL_CLEAR_MESSAGE);

    os::kernel::NativeSystemCallRegisterValues register_values{};
    const os::kernel::NativeSystemCallLayoutStatus layout_status =
        os::kernel::BuildNativeSystemCallRegisterValues(
            OS_TEST_NATIVE_SYSTEM_CALL_CURRENT_EFER, OS_TEST_NATIVE_SYSTEM_CALL_KERNEL_CODE_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_USER_STACK_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_USER_CODE_SEGMENT, OS_TEST_NATIVE_SYSTEM_CALL_ENTRY_ADDRESS,
            OS_TEST_NATIVE_SYSTEM_CALL_CPU_LOCAL_ADDRESS,
            OS_TEST_NATIVE_SYSTEM_CALL_VIRTUAL_WIDTH_BITS, register_values);
    test_context.Expect(
        layout_status == os::kernel::NativeSystemCallLayoutStatus::Succeeded &&
            register_values.extended_feature_enable_register ==
                OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_EFER &&
            register_values.segment_selector_register == OS_TEST_NATIVE_SYSTEM_CALL_EXPECTED_STAR &&
            register_values.entry_instruction_pointer_register ==
                OS_TEST_NATIVE_SYSTEM_CALL_ENTRY_ADDRESS &&
            register_values.flags_mask_register ==
                os::kernel::OS_KERNEL_NATIVE_SYSTEM_CALL_FLAGS_MASK &&
            register_values.user_gs_base_register == OS_TEST_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS &&
            register_values.kernel_gs_base_register ==
                OS_TEST_NATIVE_SYSTEM_CALL_CPU_LOCAL_ADDRESS &&
            os::kernel::ValidateNativeSystemCallRegisterValues(register_values, register_values) ==
                os::kernel::NativeSystemCallLayoutStatus::Succeeded,
        OS_TEST_NATIVE_SYSTEM_CALL_REGISTER_MESSAGE);

    os::kernel::NativeSystemCallRegisterValues ignored_values{};
    const bool invalid_segments_rejected =
        os::kernel::BuildNativeSystemCallRegisterValues(
            OS_TEST_NATIVE_SYSTEM_CALL_CURRENT_EFER, OS_TEST_NATIVE_SYSTEM_CALL_KERNEL_CODE_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_INVALID_USER_STACK_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_INVALID_USER_CODE_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_ENTRY_ADDRESS, OS_TEST_NATIVE_SYSTEM_CALL_CPU_LOCAL_ADDRESS,
            OS_TEST_NATIVE_SYSTEM_CALL_VIRTUAL_WIDTH_BITS,
            ignored_values) == os::kernel::NativeSystemCallLayoutStatus::InvalidUserSegmentOrder;
    const bool invalid_cpu_local_rejected =
        os::kernel::BuildNativeSystemCallRegisterValues(
            OS_TEST_NATIVE_SYSTEM_CALL_CURRENT_EFER, OS_TEST_NATIVE_SYSTEM_CALL_KERNEL_CODE_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_USER_STACK_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_USER_CODE_SEGMENT, OS_TEST_NATIVE_SYSTEM_CALL_ENTRY_ADDRESS,
            OS_TEST_NATIVE_SYSTEM_CALL_EMPTY_ADDRESS, OS_TEST_NATIVE_SYSTEM_CALL_VIRTUAL_WIDTH_BITS,
            ignored_values) == os::kernel::NativeSystemCallLayoutStatus::InvalidCpuLocalAddress;
    const bool invalid_virtual_width_rejected =
        os::kernel::BuildNativeSystemCallRegisterValues(
            OS_TEST_NATIVE_SYSTEM_CALL_CURRENT_EFER, OS_TEST_NATIVE_SYSTEM_CALL_KERNEL_CODE_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_USER_STACK_SEGMENT,
            OS_TEST_NATIVE_SYSTEM_CALL_USER_CODE_SEGMENT, OS_TEST_NATIVE_SYSTEM_CALL_ENTRY_ADDRESS,
            OS_TEST_NATIVE_SYSTEM_CALL_CPU_LOCAL_ADDRESS,
            OS_TEST_NATIVE_SYSTEM_CALL_INVALID_VIRTUAL_WIDTH_BITS,
            ignored_values) == os::kernel::NativeSystemCallLayoutStatus::InvalidVirtualAddressWidth;
    os::kernel::NativeSystemCallRegisterValues changed_values = register_values;
    changed_values.flags_mask_register ^= OS_TEST_NATIVE_SYSTEM_CALL_REGISTER_DIFFERENCE;
    test_context.Expect(
        invalid_segments_rejected && invalid_cpu_local_rejected && invalid_virtual_width_rejected &&
            os::kernel::ValidateNativeSystemCallRegisterValues(register_values, changed_values) ==
                os::kernel::NativeSystemCallLayoutStatus::InvalidRegisterValues,
        OS_TEST_NATIVE_SYSTEM_CALL_REJECTION_MESSAGE);

    return test_context.ExitCode();
}
