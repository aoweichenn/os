#include "os/kernel/descriptor_layout.hpp"
#include "os/kernel/exception_frame.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_SUITE_NAME = "kernel/descriptor_layout/unit";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_MESSAGE =
    "TSS 描述符必须保留完整 64 位基址";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_MESSAGE =
    "TSS 描述符必须保留 20 位 inclusive limit";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_TSS_TYPE_MESSAGE =
    "TSS 描述符必须为 present 的 available 64-bit TSS";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_GATE_ADDRESS_MESSAGE =
    "IDT gate 必须保留完整 64 位处理器地址";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_GATE_SELECTOR_MESSAGE =
    "IDT gate 必须保留代码段选择子";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_GATE_IST_MESSAGE =
    "IDT gate 必须把 IST 限制到硬件的三位字段";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_GATE_ATTRIBUTES_MESSAGE =
    "IDT gate 必须保留类型与权限属性";
constexpr std::string_view OS_TEST_KERNEL_DESCRIPTOR_GATE_RESERVED_MESSAGE =
    "IDT gate 保留字段必须为零";
constexpr std::string_view OS_TEST_KERNEL_EXCEPTION_ERROR_CODE_MESSAGE =
    "架构错误码向量集合必须与 x86-64 规范一致";
constexpr std::string_view OS_TEST_KERNEL_EXCEPTION_BREAKPOINT_MESSAGE =
    "只有受控 breakpoint 可以从内核分发器返回";
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE = 0x0000000012345678ULL;
constexpr uint32_t OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT = 0x000ABCDEU;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_LOW_MASK = 0x0000000000FFFFFFULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_LOW_SHIFT = 16ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_BYTE_MASK = 0x00000000000000FFULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_BYTE_SHIFT = 56ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_TARGET_SHIFT = 24ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_UPPER_SHIFT = 32ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_LOW_MASK = 0x000000000000FFFFULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_MASK = 0x000000000000000FULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_SOURCE_SHIFT = 48ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_TARGET_SHIFT = 16ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_TYPE_MASK = 0x00000000000000FFULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_TYPE_SHIFT = 40ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_TSS_EXPECTED_TYPE = 0x89ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_GATE_HANDLER = 0xFFFF800012345678ULL;
constexpr uint16_t OS_TEST_KERNEL_DESCRIPTOR_GATE_SELECTOR = 0x0008U;
constexpr uint8_t OS_TEST_KERNEL_DESCRIPTOR_GATE_IST_INPUT = 0xFDU;
constexpr uint8_t OS_TEST_KERNEL_DESCRIPTOR_GATE_IST_MASK = 0x07U;
constexpr uint8_t OS_TEST_KERNEL_DESCRIPTOR_GATE_ATTRIBUTES = 0x8EU;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_GATE_MIDDLE_SHIFT = 16ULL;
constexpr uint64_t OS_TEST_KERNEL_DESCRIPTOR_GATE_HIGH_SHIFT = 32ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_VECTOR_COUNT = 32ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_BREAKPOINT_VECTOR = 3ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_DOUBLE_FAULT_VECTOR = 8ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_INVALID_TSS_VECTOR = 10ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_SEGMENT_NOT_PRESENT_VECTOR = 11ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_STACK_SEGMENT_VECTOR = 12ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_GENERAL_PROTECTION_VECTOR = 13ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_PAGE_FAULT_VECTOR = 14ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_ALIGNMENT_CHECK_VECTOR = 17ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_CONTROL_PROTECTION_VECTOR = 21ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_VMM_COMMUNICATION_VECTOR = 29ULL;
constexpr uint64_t OS_TEST_KERNEL_EXCEPTION_SECURITY_VECTOR = 30ULL;
constexpr uint32_t OS_TEST_KERNEL_DESCRIPTOR_RESERVED_ZERO = 0U;

[[nodiscard]] uint64_t
DecodeTaskStateSegmentBase(const os::kernel::SystemSegmentDescriptor &descriptor) noexcept {
    return ((descriptor.low >> OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_LOW_SHIFT) &
            OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_LOW_MASK) |
           (((descriptor.low >> OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_BYTE_SHIFT) &
             OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_BYTE_MASK)
            << OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_HIGH_TARGET_SHIFT) |
           (descriptor.high << OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_UPPER_SHIFT);
}

[[nodiscard]] uint64_t
DecodeTaskStateSegmentLimit(const os::kernel::SystemSegmentDescriptor &descriptor) noexcept {
    return (descriptor.low & OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_LOW_MASK) |
           (((descriptor.low >> OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_SOURCE_SHIFT) &
             OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_MASK)
            << OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_HIGH_TARGET_SHIFT);
}

[[nodiscard]] uint64_t
DecodeInterruptGateAddress(const os::kernel::InterruptGateDescriptor &descriptor) noexcept {
    return static_cast<uint64_t>(descriptor.offsetLow) |
           (static_cast<uint64_t>(descriptor.offsetMiddle)
            << OS_TEST_KERNEL_DESCRIPTOR_GATE_MIDDLE_SHIFT) |
           (static_cast<uint64_t>(descriptor.offsetHigh)
            << OS_TEST_KERNEL_DESCRIPTOR_GATE_HIGH_SHIFT);
}

}

int main() {
    os::test::TestContext testContext{OS_TEST_KERNEL_DESCRIPTOR_SUITE_NAME};

    const os::kernel::SystemSegmentDescriptor taskStateDescriptor =
        os::kernel::CreateTaskStateSegmentDescriptor(OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE,
                                                     OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT);
    testContext.Expect(DecodeTaskStateSegmentBase(taskStateDescriptor) ==
                           OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE,
                       OS_TEST_KERNEL_DESCRIPTOR_TSS_BASE_MESSAGE);
    testContext.Expect(DecodeTaskStateSegmentLimit(taskStateDescriptor) ==
                           static_cast<uint64_t>(OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT),
                       OS_TEST_KERNEL_DESCRIPTOR_TSS_LIMIT_MESSAGE);
    testContext.Expect(((taskStateDescriptor.low >> OS_TEST_KERNEL_DESCRIPTOR_TSS_TYPE_SHIFT) &
                        OS_TEST_KERNEL_DESCRIPTOR_TSS_TYPE_MASK) ==
                           OS_TEST_KERNEL_DESCRIPTOR_TSS_EXPECTED_TYPE,
                       OS_TEST_KERNEL_DESCRIPTOR_TSS_TYPE_MESSAGE);

    const os::kernel::InterruptGateDescriptor interruptGate =
        os::kernel::CreateInterruptGateDescriptor(
            OS_TEST_KERNEL_DESCRIPTOR_GATE_HANDLER, OS_TEST_KERNEL_DESCRIPTOR_GATE_SELECTOR,
            OS_TEST_KERNEL_DESCRIPTOR_GATE_IST_INPUT, OS_TEST_KERNEL_DESCRIPTOR_GATE_ATTRIBUTES);
    testContext.Expect(DecodeInterruptGateAddress(interruptGate) ==
                           OS_TEST_KERNEL_DESCRIPTOR_GATE_HANDLER,
                       OS_TEST_KERNEL_DESCRIPTOR_GATE_ADDRESS_MESSAGE);
    testContext.Expect(interruptGate.segmentSelector == OS_TEST_KERNEL_DESCRIPTOR_GATE_SELECTOR,
                       OS_TEST_KERNEL_DESCRIPTOR_GATE_SELECTOR_MESSAGE);
    testContext.Expect(interruptGate.interruptStackTable ==
                           static_cast<uint8_t>(OS_TEST_KERNEL_DESCRIPTOR_GATE_IST_INPUT &
                                                OS_TEST_KERNEL_DESCRIPTOR_GATE_IST_MASK),
                       OS_TEST_KERNEL_DESCRIPTOR_GATE_IST_MESSAGE);
    testContext.Expect(interruptGate.typeAttributes == OS_TEST_KERNEL_DESCRIPTOR_GATE_ATTRIBUTES,
                       OS_TEST_KERNEL_DESCRIPTOR_GATE_ATTRIBUTES_MESSAGE);
    testContext.Expect(interruptGate.reserved == OS_TEST_KERNEL_DESCRIPTOR_RESERVED_ZERO,
                       OS_TEST_KERNEL_DESCRIPTOR_GATE_RESERVED_MESSAGE);

    for (uint64_t vector = 0ULL; vector < OS_TEST_KERNEL_EXCEPTION_VECTOR_COUNT; ++vector) {
        const bool expectedErrorCode =
            vector == OS_TEST_KERNEL_EXCEPTION_DOUBLE_FAULT_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_INVALID_TSS_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_SEGMENT_NOT_PRESENT_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_STACK_SEGMENT_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_GENERAL_PROTECTION_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_PAGE_FAULT_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_ALIGNMENT_CHECK_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_CONTROL_PROTECTION_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_VMM_COMMUNICATION_VECTOR ||
            vector == OS_TEST_KERNEL_EXCEPTION_SECURITY_VECTOR;
        testContext.Expect(os::kernel::ExceptionPushesHardwareErrorCode(vector) ==
                               expectedErrorCode,
                           OS_TEST_KERNEL_EXCEPTION_ERROR_CODE_MESSAGE);
        testContext.Expect(os::kernel::IsResumableKernelException(vector) ==
                               (vector == OS_TEST_KERNEL_EXCEPTION_BREAKPOINT_VECTOR),
                           OS_TEST_KERNEL_EXCEPTION_BREAKPOINT_MESSAGE);
    }

    return testContext.ExitCode();
}
