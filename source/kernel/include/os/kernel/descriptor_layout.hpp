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
    uint64_t base_address;
};

struct [[gnu::packed]] SystemSegmentDescriptor final {
    uint64_t low;
    uint64_t high;
};

struct [[gnu::packed]] InterruptGateDescriptor final {
    uint16_t offset_low;
    uint16_t segment_selector;
    uint8_t interrupt_stack_table;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct [[gnu::packed]] TaskStateSegment final {
    uint32_t reserved0;
    uint64_t privilege_stack_pointer0;
    uint64_t privilege_stack_pointer1;
    uint64_t privilege_stack_pointer2;
    uint64_t reserved1;
    uint64_t interrupt_stack_pointer1;
    uint64_t interrupt_stack_pointer2;
    uint64_t interrupt_stack_pointer3;
    uint64_t interrupt_stack_pointer4;
    uint64_t interrupt_stack_pointer5;
    uint64_t interrupt_stack_pointer6;
    uint64_t interrupt_stack_pointer7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_permission_bitmap_offset;
};

[[nodiscard]] SystemSegmentDescriptor
CreateTaskStateSegmentDescriptor(uint64_t base_address, uint32_t inclusive_limit) noexcept;

[[nodiscard]] InterruptGateDescriptor
CreateInterruptGateDescriptor(uint64_t handler_address, uint16_t segment_selector,
                              uint8_t interrupt_stack_table, uint8_t type_attributes) noexcept;

static_assert(sizeof(DescriptorTablePointer) == OS_KERNEL_DESCRIPTOR_TABLE_POINTER_SIZE_BYTES);
static_assert(sizeof(SystemSegmentDescriptor) == OS_KERNEL_DESCRIPTOR_SYSTEM_ENTRY_SIZE_BYTES);
static_assert(sizeof(InterruptGateDescriptor) == OS_KERNEL_DESCRIPTOR_IDT_ENTRY_SIZE_BYTES);
static_assert(sizeof(TaskStateSegment) == OS_KERNEL_DESCRIPTOR_TASK_STATE_SEGMENT_SIZE_BYTES);

}
