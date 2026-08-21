#include <os/kernel/arch/descriptor_tables.hpp>

#include <os/abi/system_call.hpp>
#include <os/kernel/arch/descriptor_layout.hpp>
#include <os/kernel/arch/interrupt_runtime.hpp>
#include <os/kernel/arch/exception_frame.hpp>
#include <os/kernel/device/device_model.hpp>

namespace os::kernel {

const uint16_t OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR = 0x0008U;
const uint16_t OS_KERNEL_DESCRIPTOR_KERNEL_DATA_SELECTOR = 0x0010U;
const uint16_t OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR = 0x001BU;
const uint16_t OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR = 0x0023U;
const uint16_t OS_KERNEL_DESCRIPTOR_TASK_STATE_SELECTOR = 0x0028U;
const uint64_t OS_KERNEL_DESCRIPTOR_INTERRUPT_STACK_GUARD_PAGE_COUNT = 4ULL;

namespace {

constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_ENTRY_COUNT = 7ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_NULL_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_CODE_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_DATA_INDEX = 2ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_USER_DATA_INDEX = 3ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_USER_CODE_INDEX = 4ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_TSS_LOW_INDEX = 5ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_TSS_HIGH_INDEX = 6ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_NULL_SEGMENT = 0x0000000000000000ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_LONG_MODE_CODE_SEGMENT = 0x00AF9A000000FFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_LONG_MODE_DATA_SEGMENT = 0x00CF92000000FFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_LONG_MODE_USER_DATA_SEGMENT = 0x00CFF2000000FFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_LONG_MODE_USER_CODE_SEGMENT = 0x00AFFA000000FFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_INCLUSIVE_LIMIT_ADJUSTMENT = 1ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES = 16ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GUARD_PAGE_SIZE_BYTES = 4ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_STACK_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES =
    OS_KERNEL_DESCRIPTOR_GUARD_PAGE_SIZE_BYTES + OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_DOUBLE_FAULT_GUARD_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_NMI_GUARD_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_MACHINE_CHECK_GUARD_INDEX = 2ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_PRIVILEGE_TRANSITION_GUARD_INDEX = 3ULL;
constexpr uint8_t OS_KERNEL_DESCRIPTOR_NO_IST = 0U;
constexpr uint8_t OS_KERNEL_DESCRIPTOR_DOUBLE_FAULT_IST = 1U;
constexpr uint8_t OS_KERNEL_DESCRIPTOR_NMI_IST = 2U;
constexpr uint8_t OS_KERNEL_DESCRIPTOR_MACHINE_CHECK_IST = 3U;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_NMI_VECTOR = 2ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_BREAKPOINT_VECTOR = 3ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_OVERFLOW_VECTOR = 4ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_DOUBLE_FAULT_VECTOR = 8ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_MACHINE_CHECK_VECTOR = 18ULL;
constexpr uint8_t OS_KERNEL_DESCRIPTOR_RING0_INTERRUPT_GATE = 0x8EU;
constexpr uint8_t OS_KERNEL_DESCRIPTOR_RING3_INTERRUPT_GATE = 0xEEU;
constexpr uint16_t OS_KERNEL_DESCRIPTOR_TSS_IO_MAP_OFFSET =
    static_cast<uint16_t>(OS_KERNEL_DESCRIPTOR_TASK_STATE_SEGMENT_SIZE_BYTES);
constexpr uint32_t OS_KERNEL_DESCRIPTOR_TSS_INCLUSIVE_LIMIT =
    static_cast<uint32_t>(OS_KERNEL_DESCRIPTOR_TASK_STATE_SEGMENT_SIZE_BYTES -
                          OS_KERNEL_DESCRIPTOR_INCLUSIVE_LIMIT_ADJUSTMENT);
constexpr uint16_t OS_KERNEL_DESCRIPTOR_GDT_INCLUSIVE_LIMIT = static_cast<uint16_t>(
    OS_KERNEL_DESCRIPTOR_GDT_ENTRY_COUNT * OS_KERNEL_DESCRIPTOR_GDT_ENTRY_SIZE_BYTES -
    OS_KERNEL_DESCRIPTOR_INCLUSIVE_LIMIT_ADJUSTMENT);
constexpr uint16_t OS_KERNEL_DESCRIPTOR_IDT_INCLUSIVE_LIMIT = static_cast<uint16_t>(
    OS_KERNEL_DESCRIPTOR_INTERRUPT_GATE_COUNT * OS_KERNEL_DESCRIPTOR_IDT_ENTRY_SIZE_BYTES -
    OS_KERNEL_DESCRIPTOR_INCLUSIVE_LIMIT_ADJUSTMENT);

alignas(16) uint64_t kernel_global_descriptor_table[OS_KERNEL_DESCRIPTOR_GDT_ENTRY_COUNT];
alignas(16) InterruptGateDescriptor
    kernel_interrupt_descriptor_table[OS_KERNEL_DESCRIPTOR_INTERRUPT_GATE_COUNT];
alignas(16) TaskStateSegment kernel_task_state_segment;
alignas(OS_KERNEL_DESCRIPTOR_GUARD_PAGE_SIZE_BYTES) uint8_t
    kernel_double_fault_stack[OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES];
alignas(OS_KERNEL_DESCRIPTOR_GUARD_PAGE_SIZE_BYTES) uint8_t
    kernel_non_maskable_interrupt_stack[OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES];
alignas(OS_KERNEL_DESCRIPTOR_GUARD_PAGE_SIZE_BYTES) uint8_t
    kernel_machine_check_stack[OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES];
alignas(OS_KERNEL_DESCRIPTOR_GUARD_PAGE_SIZE_BYTES) uint8_t
    kernel_privilege_transition_stack[OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES];

extern "C" const uint64_t
    os_kernel_exception_stub_table[OS_KERNEL_EXCEPTION_ARCHITECTED_VECTOR_COUNT];
extern "C" const uint64_t
    os_kernel_hardware_interrupt_stub_table[OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT];
extern "C" void OsKernelNvmeMsixInterruptEntry() noexcept;
extern "C" void OsKernelSystemCallEntry() noexcept;

extern "C" void OsKernelLoadGdtAndTss(const DescriptorTablePointer *descriptor_table,
                                      uint64_t code_selector, uint64_t data_selector,
                                      uint64_t task_state_selector) noexcept;
extern "C" void OsKernelLoadIdt(const DescriptorTablePointer *descriptor_table) noexcept;
extern "C" void OsKernelReadGdtr(DescriptorTablePointer *descriptor_table) noexcept;
extern "C" void OsKernelReadIdtr(DescriptorTablePointer *descriptor_table) noexcept;
extern "C" uint64_t OsKernelReadCodeSegment() noexcept;
extern "C" uint64_t OsKernelReadStackSegment() noexcept;
extern "C" uint64_t OsKernelReadTaskRegister() noexcept;

[[nodiscard]] uint64_t StackTopAddress(uint8_t *stack, const uint64_t size_bytes) noexcept {
    return reinterpret_cast<uint64_t>(stack + size_bytes);
}

[[nodiscard]] uint8_t InterruptStackTableForVector(const uint64_t vector) noexcept {
    if (vector == OS_KERNEL_DESCRIPTOR_DOUBLE_FAULT_VECTOR) {
        return OS_KERNEL_DESCRIPTOR_DOUBLE_FAULT_IST;
    }
    if (vector == OS_KERNEL_DESCRIPTOR_NMI_VECTOR) {
        return OS_KERNEL_DESCRIPTOR_NMI_IST;
    }
    if (vector == OS_KERNEL_DESCRIPTOR_MACHINE_CHECK_VECTOR) {
        return OS_KERNEL_DESCRIPTOR_MACHINE_CHECK_IST;
    }
    return OS_KERNEL_DESCRIPTOR_NO_IST;
}

[[nodiscard]] uint8_t TypeAttributesForVector(const uint64_t vector) noexcept {
    if (vector == OS_KERNEL_DESCRIPTOR_BREAKPOINT_VECTOR ||
        vector == OS_KERNEL_DESCRIPTOR_OVERFLOW_VECTOR) {
        return OS_KERNEL_DESCRIPTOR_RING3_INTERRUPT_GATE;
    }
    return OS_KERNEL_DESCRIPTOR_RING0_INTERRUPT_GATE;
}

}

void InitializeGlobalDescriptorTable() noexcept {
    kernel_task_state_segment.reserved0 = 0U;
    kernel_task_state_segment.privilege_stack_pointer0 = StackTopAddress(
        kernel_privilege_transition_stack, OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES);
    kernel_task_state_segment.privilege_stack_pointer1 = 0ULL;
    kernel_task_state_segment.privilege_stack_pointer2 = 0ULL;
    kernel_task_state_segment.reserved1 = 0ULL;
    kernel_task_state_segment.interrupt_stack_pointer1 =
        StackTopAddress(kernel_double_fault_stack, OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES);
    kernel_task_state_segment.interrupt_stack_pointer2 = StackTopAddress(
        kernel_non_maskable_interrupt_stack, OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES);
    kernel_task_state_segment.interrupt_stack_pointer3 =
        StackTopAddress(kernel_machine_check_stack, OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES);
    kernel_task_state_segment.interrupt_stack_pointer4 = 0ULL;
    kernel_task_state_segment.interrupt_stack_pointer5 = 0ULL;
    kernel_task_state_segment.interrupt_stack_pointer6 = 0ULL;
    kernel_task_state_segment.interrupt_stack_pointer7 = 0ULL;
    kernel_task_state_segment.reserved2 = 0ULL;
    kernel_task_state_segment.reserved3 = 0U;
    kernel_task_state_segment.io_permission_bitmap_offset = OS_KERNEL_DESCRIPTOR_TSS_IO_MAP_OFFSET;

    const SystemSegmentDescriptor task_state_descriptor =
        CreateTaskStateSegmentDescriptor(reinterpret_cast<uint64_t>(&kernel_task_state_segment),
                                         OS_KERNEL_DESCRIPTOR_TSS_INCLUSIVE_LIMIT);
    kernel_global_descriptor_table[OS_KERNEL_DESCRIPTOR_GDT_NULL_INDEX] =
        OS_KERNEL_DESCRIPTOR_NULL_SEGMENT;
    kernel_global_descriptor_table[OS_KERNEL_DESCRIPTOR_GDT_CODE_INDEX] =
        OS_KERNEL_DESCRIPTOR_LONG_MODE_CODE_SEGMENT;
    kernel_global_descriptor_table[OS_KERNEL_DESCRIPTOR_GDT_DATA_INDEX] =
        OS_KERNEL_DESCRIPTOR_LONG_MODE_DATA_SEGMENT;
    kernel_global_descriptor_table[OS_KERNEL_DESCRIPTOR_GDT_USER_DATA_INDEX] =
        OS_KERNEL_DESCRIPTOR_LONG_MODE_USER_DATA_SEGMENT;
    kernel_global_descriptor_table[OS_KERNEL_DESCRIPTOR_GDT_USER_CODE_INDEX] =
        OS_KERNEL_DESCRIPTOR_LONG_MODE_USER_CODE_SEGMENT;
    kernel_global_descriptor_table[OS_KERNEL_DESCRIPTOR_GDT_TSS_LOW_INDEX] =
        task_state_descriptor.low;
    kernel_global_descriptor_table[OS_KERNEL_DESCRIPTOR_GDT_TSS_HIGH_INDEX] =
        task_state_descriptor.high;

    const DescriptorTablePointer global_descriptor_table_pointer{
        .limit = OS_KERNEL_DESCRIPTOR_GDT_INCLUSIVE_LIMIT,
        .base_address = reinterpret_cast<uint64_t>(kernel_global_descriptor_table),
    };
    OsKernelLoadGdtAndTss(&global_descriptor_table_pointer,
                          static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR),
                          static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_KERNEL_DATA_SELECTOR),
                          static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_TASK_STATE_SELECTOR));
}

void InitializeInterruptDescriptorTable() noexcept {
    for (uint64_t vector = 0ULL; vector < OS_KERNEL_EXCEPTION_ARCHITECTED_VECTOR_COUNT; ++vector) {
        kernel_interrupt_descriptor_table[vector] = CreateInterruptGateDescriptor(
            os_kernel_exception_stub_table[vector], OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR,
            InterruptStackTableForVector(vector), TypeAttributesForVector(vector));
    }

    const InterruptGateDescriptor not_present_gate{};
    for (uint64_t vector = OS_KERNEL_EXCEPTION_ARCHITECTED_VECTOR_COUNT;
         vector < OS_KERNEL_DESCRIPTOR_INTERRUPT_GATE_COUNT; ++vector) {
        kernel_interrupt_descriptor_table[vector] = not_present_gate;
    }
    for (uint64_t interrupt_request = 0ULL; interrupt_request < OS_KERNEL_DEVICE_LEGACY_IRQ_COUNT;
         ++interrupt_request) {
        uint64_t vector = 0ULL;
        if (CalculateLegacyPicVector(interrupt_request, vector) !=
            LegacyPicModelStatus::Succeeded) {
            continue;
        }
        kernel_interrupt_descriptor_table[vector] = CreateInterruptGateDescriptor(
            os_kernel_hardware_interrupt_stub_table[interrupt_request],
            OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR, OS_KERNEL_DESCRIPTOR_NO_IST,
            OS_KERNEL_DESCRIPTOR_RING0_INTERRUPT_GATE);
    }
    kernel_interrupt_descriptor_table[OS_KERNEL_INTERRUPT_NVME_MSIX_VECTOR] =
        CreateInterruptGateDescriptor(
            reinterpret_cast<uint64_t>(&OsKernelNvmeMsixInterruptEntry),
            OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR, OS_KERNEL_DESCRIPTOR_NO_IST,
            OS_KERNEL_DESCRIPTOR_RING0_INTERRUPT_GATE);
    kernel_interrupt_descriptor_table[os::abi::OS_ABI_SYSTEM_CALL_VECTOR] =
        CreateInterruptGateDescriptor(reinterpret_cast<uint64_t>(&OsKernelSystemCallEntry),
                                      OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR,
                                      OS_KERNEL_DESCRIPTOR_NO_IST,
                                      OS_KERNEL_DESCRIPTOR_RING3_INTERRUPT_GATE);

    const DescriptorTablePointer interrupt_descriptor_table_pointer{
        .limit = OS_KERNEL_DESCRIPTOR_IDT_INCLUSIVE_LIMIT,
        .base_address = reinterpret_cast<uint64_t>(kernel_interrupt_descriptor_table),
    };
    OsKernelLoadIdt(&interrupt_descriptor_table_pointer);
}

DescriptorTableValidationStatus ValidateDescriptorTables() noexcept {
    DescriptorTablePointer current_global_descriptor_table{};
    OsKernelReadGdtr(&current_global_descriptor_table);
    if (current_global_descriptor_table.limit != OS_KERNEL_DESCRIPTOR_GDT_INCLUSIVE_LIMIT ||
        current_global_descriptor_table.base_address !=
            reinterpret_cast<uint64_t>(kernel_global_descriptor_table)) {
        return DescriptorTableValidationStatus::InvalidGlobalDescriptorTable;
    }

    DescriptorTablePointer current_interrupt_descriptor_table{};
    OsKernelReadIdtr(&current_interrupt_descriptor_table);
    if (current_interrupt_descriptor_table.limit != OS_KERNEL_DESCRIPTOR_IDT_INCLUSIVE_LIMIT ||
        current_interrupt_descriptor_table.base_address !=
            reinterpret_cast<uint64_t>(kernel_interrupt_descriptor_table)) {
        return DescriptorTableValidationStatus::InvalidInterruptDescriptorTable;
    }
    if (OsKernelReadCodeSegment() !=
        static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR)) {
        return DescriptorTableValidationStatus::InvalidCodeSegment;
    }
    if (OsKernelReadStackSegment() !=
        static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_KERNEL_DATA_SELECTOR)) {
        return DescriptorTableValidationStatus::InvalidStackSegment;
    }
    if (OsKernelReadTaskRegister() !=
        static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_TASK_STATE_SELECTOR)) {
        return DescriptorTableValidationStatus::InvalidTaskRegister;
    }
    if (kernel_task_state_segment.privilege_stack_pointer0 !=
            StackTopAddress(kernel_privilege_transition_stack,
                            OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES) ||
        kernel_task_state_segment.interrupt_stack_pointer1 == 0ULL ||
        kernel_task_state_segment.interrupt_stack_pointer2 == 0ULL ||
        kernel_task_state_segment.interrupt_stack_pointer3 == 0ULL ||
        kernel_task_state_segment.io_permission_bitmap_offset !=
            OS_KERNEL_DESCRIPTOR_TSS_IO_MAP_OFFSET) {
        return DescriptorTableValidationStatus::InvalidTaskStateSegment;
    }
    return DescriptorTableValidationStatus::Succeeded;
}

uint64_t InterruptStackGuardPageAddress(const uint64_t guard_page_index) noexcept {
    if (guard_page_index == OS_KERNEL_DESCRIPTOR_DOUBLE_FAULT_GUARD_INDEX) {
        return reinterpret_cast<uint64_t>(kernel_double_fault_stack);
    }
    if (guard_page_index == OS_KERNEL_DESCRIPTOR_NMI_GUARD_INDEX) {
        return reinterpret_cast<uint64_t>(kernel_non_maskable_interrupt_stack);
    }
    if (guard_page_index == OS_KERNEL_DESCRIPTOR_MACHINE_CHECK_GUARD_INDEX) {
        return reinterpret_cast<uint64_t>(kernel_machine_check_stack);
    }
    if (guard_page_index == OS_KERNEL_DESCRIPTOR_PRIVILEGE_TRANSITION_GUARD_INDEX) {
        return reinterpret_cast<uint64_t>(kernel_privilege_transition_stack);
    }
    return 0ULL;
}

uint64_t DefaultPrivilegeStackPointer0() noexcept {
    return StackTopAddress(kernel_privilege_transition_stack,
                           OS_KERNEL_DESCRIPTOR_STACK_STORAGE_SIZE_BYTES);
}

uint64_t CurrentPrivilegeStackPointer0() noexcept {
    return kernel_task_state_segment.privilege_stack_pointer0;
}

bool SetPrivilegeStackPointer0(const uint64_t stack_pointer) noexcept {
    if (stack_pointer == 0ULL ||
        stack_pointer % OS_KERNEL_DESCRIPTOR_STACK_ALIGNMENT_BYTES != 0ULL) {
        return false;
    }
    kernel_task_state_segment.privilege_stack_pointer0 = stack_pointer;
    return kernel_task_state_segment.privilege_stack_pointer0 == stack_pointer;
}
}
