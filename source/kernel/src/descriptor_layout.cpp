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

SystemSegmentDescriptor CreateTaskStateSegmentDescriptor(const uint64_t baseAddress,
                                                         const uint32_t inclusiveLimit) noexcept {
    const uint64_t widenedLimit = static_cast<uint64_t>(inclusiveLimit);
    const uint64_t low =
        (widenedLimit & OS_KERNEL_DESCRIPTOR_TSS_LIMIT_LOW_MASK) |
        ((baseAddress & OS_KERNEL_DESCRIPTOR_TSS_BASE_LOW_MASK)
         << OS_KERNEL_DESCRIPTOR_TSS_BASE_LOW_SHIFT) |
        (OS_KERNEL_DESCRIPTOR_TSS_TYPE_PRESENT << OS_KERNEL_DESCRIPTOR_TSS_TYPE_SHIFT) |
        (((widenedLimit & OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_MASK) >>
          OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_SOURCE_SHIFT)
         << OS_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_TARGET_SHIFT) |
        (((baseAddress & OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_MASK) >>
          OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_SOURCE_SHIFT)
         << OS_KERNEL_DESCRIPTOR_TSS_BASE_MIDDLE_TARGET_SHIFT);
    return SystemSegmentDescriptor{
        .low = low,
        .high = baseAddress >> OS_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_SHIFT,
    };
}

InterruptGateDescriptor CreateInterruptGateDescriptor(const uint64_t handlerAddress,
                                                      const uint16_t segmentSelector,
                                                      const uint8_t interruptStackTable,
                                                      const uint8_t typeAttributes) noexcept {
    return InterruptGateDescriptor{
        .offsetLow = static_cast<uint16_t>(handlerAddress & OS_KERNEL_DESCRIPTOR_OFFSET_LOW_MASK),
        .segmentSelector = segmentSelector,
        .interruptStackTable =
            static_cast<uint8_t>(interruptStackTable & OS_KERNEL_DESCRIPTOR_IST_INDEX_MASK),
        .typeAttributes = typeAttributes,
        .offsetMiddle =
            static_cast<uint16_t>((handlerAddress & OS_KERNEL_DESCRIPTOR_OFFSET_MIDDLE_MASK) >>
                                  OS_KERNEL_DESCRIPTOR_OFFSET_MIDDLE_SHIFT),
        .offsetHigh =
            static_cast<uint32_t>(handlerAddress >> OS_KERNEL_DESCRIPTOR_OFFSET_HIGH_SHIFT),
        .reserved = OS_KERNEL_DESCRIPTOR_RESERVED_ZERO,
    };
}

}
