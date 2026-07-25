#include "os/kernel/descriptor_layout.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_LIMIT_LOW_MASK = 0x000000000000FFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_BASE_LOW_MASK = 0x0000000000FFFFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_BASE_LOW_SHIFT = 16ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_TYPE_PRESENT = 0x0000000000000089ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_TYPE_SHIFT = 40ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_MASK = 0x00000000000F0000ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_SOURCE_SHIFT = 16ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_TARGET_SHIFT = 48ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_MASK = 0x00000000FF000000ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_SOURCE_SHIFT = 24ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_TARGET_SHIFT = 56ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_SHIFT = 32ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_OFFSET_LOW_MASK = 0x000000000000FFFFULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_OFFSET_MIDDLE_MASK = 0x00000000FFFF0000ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_OFFSET_MIDDLE_SHIFT = 16ULL;
constexpr uint64_t OS_KERNEL_DESCRIPTOR_OFFSET_HIGH_SHIFT = 32ULL;
constexpr uint8_t OS_KERNEL_DESCRIPTOR_IST_INDEX_MASK = 0x07U;
constexpr uint32_t OS_KERNEL_DESCRIPTOR_RESERVED_ZERO = 0U;

}

SystemSegmentDescriptor CreateTaskStateSegmentDescriptor(const uint64_t base_address,
                                                         const uint32_t inclusive_limit) noexcept {
    const uint64_t widened_limit = static_cast<uint64_t>(inclusive_limit);
    const uint64_t low =
        (widened_limit & OS_KERNEL_DESCRIPTOR_TSS_LIMIT_LOW_MASK) |
        ((base_address & OS_KERNEL_DESCRIPTOR_TSS_BASE_LOW_MASK)
         << OS_KERNEL_DESCRIPTOR_TSS_BASE_LOW_SHIFT) |
        (OS_KERNEL_DESCRIPTOR_TSS_TYPE_PRESENT << OS_KERNEL_DESCRIPTOR_TSS_TYPE_SHIFT) |
        (((widened_limit & OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_MASK) >>
          OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_SOURCE_SHIFT)
         << OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_TARGET_SHIFT) |
        (((base_address & OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_MASK) >>
          OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_SOURCE_SHIFT)
         << OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_TARGET_SHIFT);
    return SystemSegmentDescriptor{
        .low = low,
        .high = base_address >> OS_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_SHIFT,
    };
}

InterruptGateDescriptor CreateInterruptGateDescriptor(const uint64_t handler_address,
                                                      const uint16_t segment_selector,
                                                      const uint8_t interrupt_stack_table,
                                                      const uint8_t type_attributes) noexcept {
    return InterruptGateDescriptor{
        .offset_low = static_cast<uint16_t>(handler_address & OS_KERNEL_DESCRIPTOR_OFFSET_LOW_MASK),
        .segment_selector = segment_selector,
        .interrupt_stack_table =
            static_cast<uint8_t>(interrupt_stack_table & OS_KERNEL_DESCRIPTOR_IST_INDEX_MASK),
        .type_attributes = type_attributes,
        .offset_middle =
            static_cast<uint16_t>((handler_address & OS_KERNEL_DESCRIPTOR_OFFSET_MIDDLE_MASK) >>
                                  OS_KERNEL_DESCRIPTOR_OFFSET_MIDDLE_SHIFT),
        .offset_high =
            static_cast<uint32_t>(handler_address >> OS_KERNEL_DESCRIPTOR_OFFSET_HIGH_SHIFT),
        .reserved = OS_KERNEL_DESCRIPTOR_RESERVED_ZERO,
    };
}
}
