#include "os/kernel/descriptor_tables.hpp"

#include "os/kernel/descriptor_layout.hpp"
#include "os/kernel/exception_frame.hpp"

namespace os::kernel {

const uint16_t OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR = 0x0008U;
const uint16_t OS_KERNEL_DESCRIPTOR_KERNEL_DATA_SELECTOR = 0x0010U;
const uint16_t OS_KERNEL_DESCRIPTOR_TASK_STATE_SELECTOR = 0x0018U;

namespace {

constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_ENTRY_COUNT = 5ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_NULL_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_CODE_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_DATA_INDEX = 2ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_TSS_LOW_INDEX = 3ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_TSS_HIGH_INDEX = 4ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_NULL_SEGMENT = 0x0000000000000000ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_LONG_MODE_CODE_SEGMENT = 0x00AF9A000000FFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_LONG_MODE_DATA_SEGMENT = 0x00CF92000000FFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_INCLUSIVE_LIMIT_ADJUSTMENT = 1ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES = 16ULL * 1024ULL;
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

alignas(16) uint64_t kernelGlobalDescriptorTable[OS_KERNEL_DESCRIPTOR_GDT_ENTRY_COUNT];
alignas(16) InterruptGateDescriptor
    kernelInterruptDescriptorTable[OS_KERNEL_DESCRIPTOR_INTERRUPT_GATE_COUNT];
alignas(16) TaskStateSegment kernelTaskStateSegment;
alignas(16) uint8_t kernelDoubleFaultStack[OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES];
alignas(16) uint8_t
    kernelNonMaskableInterruptStack[OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES];
alignas(16) uint8_t kernelMachineCheckStack[OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES];

extern "C" const uint64_t osKernelExceptionStubTable[OS_KERNEL_EXCEPTION_ARCHITECTED_VECTOR_COUNT];

extern "C" void osKernelLoadGdtAndTss(const DescriptorTablePointer *descriptorTable,
                                      uint64_t codeSelector, uint64_t dataSelector,
                                      uint64_t taskStateSelector) noexcept;
extern "C" void osKernelLoadIdt(const DescriptorTablePointer *descriptorTable) noexcept;
extern "C" void osKernelReadGdtr(DescriptorTablePointer *descriptorTable) noexcept;
extern "C" void osKernelReadIdtr(DescriptorTablePointer *descriptorTable) noexcept;
extern "C" uint64_t osKernelReadCodeSegment() noexcept;
extern "C" uint64_t osKernelReadStackSegment() noexcept;
extern "C" uint64_t osKernelReadTaskRegister() noexcept;

[[nodiscard]] uint64_t stackTopAddress(uint8_t *stack, const uint64_t sizeBytes) noexcept {
    return reinterpret_cast<uint64_t>(stack + sizeBytes);
}

[[nodiscard]] uint8_t interruptStackTableForVector(const uint64_t vector) noexcept {
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

[[nodiscard]] uint8_t typeAttributesForVector(const uint64_t vector) noexcept {
    if (vector == OS_KERNEL_DESCRIPTOR_BREAKPOINT_VECTOR ||
        vector == OS_KERNEL_DESCRIPTOR_OVERFLOW_VECTOR) {
        return OS_KERNEL_DESCRIPTOR_RING3_INTERRUPT_GATE;
    }
    return OS_KERNEL_DESCRIPTOR_RING0_INTERRUPT_GATE;
}

}

void initializeGlobalDescriptorTable(const uint64_t privilegeStackTop) noexcept {
    kernelTaskStateSegment.reserved0 = 0U;
    kernelTaskStateSegment.privilegeStackPointer0 = privilegeStackTop;
    kernelTaskStateSegment.privilegeStackPointer1 = 0ULL;
    kernelTaskStateSegment.privilegeStackPointer2 = 0ULL;
    kernelTaskStateSegment.reserved1 = 0ULL;
    kernelTaskStateSegment.interruptStackPointer1 =
        stackTopAddress(kernelDoubleFaultStack, OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES);
    kernelTaskStateSegment.interruptStackPointer2 = stackTopAddress(
        kernelNonMaskableInterruptStack, OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES);
    kernelTaskStateSegment.interruptStackPointer3 =
        stackTopAddress(kernelMachineCheckStack, OS_KERNEL_DESCRIPTOR_PRIVILEGED_STACK_SIZE_BYTES);
    kernelTaskStateSegment.interruptStackPointer4 = 0ULL;
    kernelTaskStateSegment.interruptStackPointer5 = 0ULL;
    kernelTaskStateSegment.interruptStackPointer6 = 0ULL;
    kernelTaskStateSegment.interruptStackPointer7 = 0ULL;
    kernelTaskStateSegment.reserved2 = 0ULL;
    kernelTaskStateSegment.reserved3 = 0U;
    kernelTaskStateSegment.ioPermissionBitmapOffset = OS_KERNEL_DESCRIPTOR_TSS_IO_MAP_OFFSET;

    const SystemSegmentDescriptor taskStateDescriptor =
        createTaskStateSegmentDescriptor(reinterpret_cast<uint64_t>(&kernelTaskStateSegment),
                                         OS_KERNEL_DESCRIPTOR_TSS_INCLUSIVE_LIMIT);
    kernelGlobalDescriptorTable[OS_KERNEL_DESCRIPTOR_GDT_NULL_INDEX] =
        OS_KERNEL_DESCRIPTOR_NULL_SEGMENT;
    kernelGlobalDescriptorTable[OS_KERNEL_DESCRIPTOR_GDT_CODE_INDEX] =
        OS_KERNEL_DESCRIPTOR_LONG_MODE_CODE_SEGMENT;
    kernelGlobalDescriptorTable[OS_KERNEL_DESCRIPTOR_GDT_DATA_INDEX] =
        OS_KERNEL_DESCRIPTOR_LONG_MODE_DATA_SEGMENT;
    kernelGlobalDescriptorTable[OS_KERNEL_DESCRIPTOR_GDT_TSS_LOW_INDEX] = taskStateDescriptor.low;
    kernelGlobalDescriptorTable[OS_KERNEL_DESCRIPTOR_GDT_TSS_HIGH_INDEX] = taskStateDescriptor.high;

    const DescriptorTablePointer globalDescriptorTablePointer{
        .limit = OS_KERNEL_DESCRIPTOR_GDT_INCLUSIVE_LIMIT,
        .baseAddress = reinterpret_cast<uint64_t>(kernelGlobalDescriptorTable),
    };
    osKernelLoadGdtAndTss(&globalDescriptorTablePointer,
                          static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR),
                          static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_KERNEL_DATA_SELECTOR),
                          static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_TASK_STATE_SELECTOR));
}

void initializeInterruptDescriptorTable() noexcept {
    for (uint64_t vector = 0ULL; vector < OS_KERNEL_EXCEPTION_ARCHITECTED_VECTOR_COUNT; ++vector) {
        kernelInterruptDescriptorTable[vector] = createInterruptGateDescriptor(
            osKernelExceptionStubTable[vector], OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR,
            interruptStackTableForVector(vector), typeAttributesForVector(vector));
    }

    const InterruptGateDescriptor notPresentGate{};
    for (uint64_t vector = OS_KERNEL_EXCEPTION_ARCHITECTED_VECTOR_COUNT;
         vector < OS_KERNEL_DESCRIPTOR_INTERRUPT_GATE_COUNT; ++vector) {
        kernelInterruptDescriptorTable[vector] = notPresentGate;
    }

    const DescriptorTablePointer interruptDescriptorTablePointer{
        .limit = OS_KERNEL_DESCRIPTOR_IDT_INCLUSIVE_LIMIT,
        .baseAddress = reinterpret_cast<uint64_t>(kernelInterruptDescriptorTable),
    };
    osKernelLoadIdt(&interruptDescriptorTablePointer);
}

DescriptorTableValidationStatus
validateDescriptorTables(const uint64_t expectedPrivilegeStackTop) noexcept {
    DescriptorTablePointer currentGlobalDescriptorTable{};
    osKernelReadGdtr(&currentGlobalDescriptorTable);
    if (currentGlobalDescriptorTable.limit != OS_KERNEL_DESCRIPTOR_GDT_INCLUSIVE_LIMIT ||
        currentGlobalDescriptorTable.baseAddress !=
            reinterpret_cast<uint64_t>(kernelGlobalDescriptorTable)) {
        return DescriptorTableValidationStatus::InvalidGlobalDescriptorTable;
    }

    DescriptorTablePointer currentInterruptDescriptorTable{};
    osKernelReadIdtr(&currentInterruptDescriptorTable);
    if (currentInterruptDescriptorTable.limit != OS_KERNEL_DESCRIPTOR_IDT_INCLUSIVE_LIMIT ||
        currentInterruptDescriptorTable.baseAddress !=
            reinterpret_cast<uint64_t>(kernelInterruptDescriptorTable)) {
        return DescriptorTableValidationStatus::InvalidInterruptDescriptorTable;
    }
    if (osKernelReadCodeSegment() !=
        static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_KERNEL_CODE_SELECTOR)) {
        return DescriptorTableValidationStatus::InvalidCodeSegment;
    }
    if (osKernelReadStackSegment() !=
        static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_KERNEL_DATA_SELECTOR)) {
        return DescriptorTableValidationStatus::InvalidStackSegment;
    }
    if (osKernelReadTaskRegister() !=
        static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_TASK_STATE_SELECTOR)) {
        return DescriptorTableValidationStatus::InvalidTaskRegister;
    }
    if (kernelTaskStateSegment.privilegeStackPointer0 != expectedPrivilegeStackTop ||
        kernelTaskStateSegment.interruptStackPointer1 == 0ULL ||
        kernelTaskStateSegment.interruptStackPointer2 == 0ULL ||
        kernelTaskStateSegment.interruptStackPointer3 == 0ULL ||
        kernelTaskStateSegment.ioPermissionBitmapOffset != OS_KERNEL_DESCRIPTOR_TSS_IO_MAP_OFFSET) {
        return DescriptorTableValidationStatus::InvalidTaskStateSegment;
    }
    return DescriptorTableValidationStatus::Succeeded;
}

}
