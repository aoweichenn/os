#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_DESCRIPTOR_GDT_ENTRY_SIZE_BYTES = 8ULL;
inline constexpr uint64_t OS_KERNEL_DESCRIPTOR_SYSTEM_ENTRY_SIZE_BYTES = 16ULL;
inline constexpr uint64_t OS_KERNEL_DESCRIPTOR_IDT_ENTRY_SIZE_BYTES = 16ULL;
inline constexpr uint64_t OS_KERNEL_DESCRIPTOR_TABLE_POINTER_SIZE_BYTES = 10ULL;
inline constexpr uint64_t OS_KERNEL_DESCRIPTOR_TASK_STATE_SEGMENT_SIZE_BYTES = 104ULL;
inline constexpr uint64_t OS_KERNEL_DESCRIPTOR_INTERRUPT_GATE_COUNT = 256ULL;

struct [[gnu::packed]] DescriptorTablePointer final {
    uint16_t limit;
    uint64_t baseAddress;
};

struct [[gnu::packed]] SystemSegmentDescriptor final {
    uint64_t low;
    uint64_t high;
};

struct [[gnu::packed]] InterruptGateDescriptor final {
    uint16_t offsetLow;
    uint16_t segmentSelector;
    uint8_t interruptStackTable;
    uint8_t typeAttributes;
    uint16_t offsetMiddle;
    uint32_t offsetHigh;
    uint32_t reserved;
};

struct [[gnu::packed]] TaskStateSegment final {
    uint32_t reserved0;
    uint64_t privilegeStackPointer0;
    uint64_t privilegeStackPointer1;
    uint64_t privilegeStackPointer2;
    uint64_t reserved1;
    uint64_t interruptStackPointer1;
    uint64_t interruptStackPointer2;
    uint64_t interruptStackPointer3;
    uint64_t interruptStackPointer4;
    uint64_t interruptStackPointer5;
    uint64_t interruptStackPointer6;
    uint64_t interruptStackPointer7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t ioPermissionBitmapOffset;
};

[[nodiscard]] SystemSegmentDescriptor
CreateTaskStateSegmentDescriptor(uint64_t baseAddress, uint32_t inclusiveLimit) noexcept;

[[nodiscard]] InterruptGateDescriptor
CreateInterruptGateDescriptor(uint64_t handlerAddress, uint16_t segmentSelector,
                              uint8_t interruptStackTable, uint8_t typeAttributes) noexcept;

static_assert(sizeof(DescriptorTablePointer) == OS_KERNEL_DESCRIPTOR_TABLE_POINTER_SIZE_BYTES);
static_assert(sizeof(SystemSegmentDescriptor) == OS_KERNEL_DESCRIPTOR_SYSTEM_ENTRY_SIZE_BYTES);
static_assert(sizeof(InterruptGateDescriptor) == OS_KERNEL_DESCRIPTOR_IDT_ENTRY_SIZE_BYTES);
static_assert(sizeof(TaskStateSegment) == OS_KERNEL_DESCRIPTOR_TASK_STATE_SEGMENT_SIZE_BYTES);

}
