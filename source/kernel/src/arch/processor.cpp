#include <os/kernel/arch/processor.hpp>

namespace os::kernel {

const uint64_t OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS = 0x0000000004000000ULL;

namespace {

constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF = 0x80000000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF = 0x80000001U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_ADDRESS_WIDTHS_LEAF = 0x80000008U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_STANDARD_MAXIMUM_LEAF = 0x00000000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_STANDARD_FEATURES_LEAF = 0x00000001U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_STRUCTURED_FEATURES_LEAF = 0x00000007U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_NO_EXECUTE_BIT = 0x00100000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_LOCAL_APIC_BIT = 0x00000200U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_FIVE_LEVEL_PAGING_BIT = 0x00010000U;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_PHYSICAL_ADDRESS_WIDTH_MASK = 0x000000FFU;
constexpr uint32_t OS_KERNEL_PROCESSOR_CPUID_VIRTUAL_ADDRESS_WIDTH_MASK = 0x0000FF00U;
constexpr uint64_t OS_KERNEL_PROCESSOR_CPUID_VIRTUAL_ADDRESS_WIDTH_SHIFT = 8ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_MAXIMUM_PAGE_TABLE_ADDRESS_WIDTH_BITS = 52ULL;
constexpr uint32_t OS_KERNEL_PROCESSOR_IA32_EFER_MSR = 0xC0000080U;
constexpr uint32_t OS_KERNEL_PROCESSOR_IA32_APIC_BASE_MSR = 0x0000001BU;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT = 0x0000000000000800ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_APIC_GLOBAL_ENABLE_BIT = 0x0000000000000800ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_APIC_X2_ENABLE_BIT = 0x0000000000000400ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_IA32_APIC_BASE_ADDRESS_MASK = 0x0000000FFFFFF000ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT = 0x0000000000010000ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS = 32ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_RFLAGS_INTERRUPT_ENABLE_BIT = 0x0000000000000200ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_REGISTER_OFFSET = 0x00000000000000F0ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_LOCAL_APIC_IDENTIFIER_REGISTER_OFFSET =
    0x0000000000000020ULL;
constexpr uint64_t OS_KERNEL_PROCESSOR_LOCAL_APIC_EOI_REGISTER_OFFSET = 0x00000000000000B0ULL;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_IDENTIFIER_SHIFT_BITS = 24U;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_IDENTIFIER_MASK = 0xFFU;
constexpr uint64_t OS_KERNEL_PROCESSOR_LOCAL_APIC_LINT0_REGISTER_OFFSET = 0x0000000000000350ULL;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR_MASK = 0x000000FFU;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR = 0x000000FFU;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_SOFTWARE_ENABLE_BIT = 0x00000100U;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_DELIVERY_MODE_MASK = 0x00000700U;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_EXTINT_DELIVERY_MODE = 0x00000700U;
constexpr uint32_t OS_KERNEL_PROCESSOR_LOCAL_APIC_MASK_BIT = 0x00010000U;

struct CpuIdResult final {
    uint32_t accumulator;
    uint32_t base;
    uint32_t counter;
    uint32_t data;
};

[[nodiscard]] CpuIdResult ReadCpuId(const uint32_t leaf) noexcept {
    CpuIdResult result{};
    asm volatile("cpuid"
                 : "=a"(result.accumulator), "=b"(result.base), "=c"(result.counter),
                   "=d"(result.data)
                 : "a"(leaf), "c"(0U));
    return result;
}

[[nodiscard]] uint64_t
ReadModelSpecificRegisterValue(const uint32_t register_index) noexcept {
    uint32_t low_value = 0U;
    uint32_t high_value = 0U;
    asm volatile("rdmsr" : "=a"(low_value), "=d"(high_value) : "c"(register_index));
    return static_cast<uint64_t>(low_value) |
           (static_cast<uint64_t>(high_value) << OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS);
}

void WriteModelSpecificRegisterValue(const uint32_t register_index,
                                     const uint64_t value) noexcept {
    const uint32_t low_value = static_cast<uint32_t>(value);
    const uint32_t high_value =
        static_cast<uint32_t>(value >> OS_KERNEL_PROCESSOR_REGISTER_HALF_WIDTH_BITS);
    asm volatile("wrmsr" : : "c"(register_index), "a"(low_value), "d"(high_value));
}

[[nodiscard]] volatile uint32_t *LocalApicRegister(const uint64_t register_offset) noexcept {
    return reinterpret_cast<volatile uint32_t *>(LocalApicPhysicalAddress() + register_offset);
}
}

[[noreturn]] void HaltProcessor() noexcept {
    asm volatile("cli");
    while (true) {
        asm volatile("hlt");
    }
}

bool DisableInterrupts() noexcept {
    uint64_t flags = 0ULL;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return (flags & OS_KERNEL_PROCESSOR_RFLAGS_INTERRUPT_ENABLE_BIT) != 0ULL;
}

void RestoreInterrupts(const bool interrupts_were_enabled) noexcept {
    if (interrupts_were_enabled) {
        asm volatile("sti" : : : "memory");
    }
}

void EnableInterrupts() noexcept { asm volatile("sti" : : : "memory"); }

void WaitForInterrupt() noexcept { asm volatile("hlt" : : : "memory"); }

void EnableInterruptsWaitAndDisable() noexcept {
    // 三条指令必须相邻：STI 的中断影子覆盖 HLT，唤醒返回后立即恢复内核临界区。
    asm volatile("sti; hlt; cli" : : : "memory");
}

uint64_t ReadPageTableRoot() noexcept {
    uint64_t page_table_root = 0ULL;
    asm volatile("mov %0, cr3" : "=r"(page_table_root));
    return page_table_root;
}

uint64_t ReadPageFaultLinearAddress() noexcept {
    uint64_t page_fault_linear_address = 0ULL;
    asm volatile("mov %0, cr2" : "=r"(page_fault_linear_address));
    return page_fault_linear_address;
}

uint64_t ReadStackPointer() noexcept {
    uint64_t stack_pointer = 0ULL;
    asm volatile("mov %0, rsp" : "=r"(stack_pointer));
    return stack_pointer;
}

uint32_t ProcessorStandardFeatureBits() noexcept {
    return ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_STANDARD_FEATURES_LEAF).data;
}

ProcessorFeatureProfile ReadProcessorFeatureProfile() noexcept {
    const CpuIdResult standard_maximum =
        ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_STANDARD_MAXIMUM_LEAF);
    const CpuIdResult extended_maximum =
        ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF);
    const CpuIdResult standard_features =
        standard_maximum.accumulator >=
                OS_KERNEL_PROCESSOR_CPUID_STANDARD_FEATURES_LEAF
            ? ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_STANDARD_FEATURES_LEAF)
            : CpuIdResult{};
    const CpuIdResult extended_features =
        extended_maximum.accumulator >=
                OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF
            ? ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF)
            : CpuIdResult{};
    const CpuIdResult address_widths =
        extended_maximum.accumulator >=
                OS_KERNEL_PROCESSOR_CPUID_ADDRESS_WIDTHS_LEAF
            ? ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_ADDRESS_WIDTHS_LEAF)
            : CpuIdResult{};
    return DecodeProcessorFeatureProfile(ProcessorFeatureLeaves{
        .standard_maximum_leaf = standard_maximum.accumulator,
        .standard_feature_data = standard_features.data,
        .extended_maximum_leaf = extended_maximum.accumulator,
        .extended_feature_data = extended_features.data,
        .address_widths_accumulator = address_widths.accumulator,
    });
}

uint64_t ReadModelSpecificRegister(const uint32_t register_index) noexcept {
    return ReadModelSpecificRegisterValue(register_index);
}

void WriteModelSpecificRegister(const uint32_t register_index,
                                const uint64_t value) noexcept {
    WriteModelSpecificRegisterValue(register_index, value);
}

uint64_t ReadControlRegister0() noexcept {
    uint64_t value = 0ULL;
    asm volatile("mov %0, cr0" : "=r"(value));
    return value;
}

void WriteControlRegister0(const uint64_t value) noexcept {
    asm volatile("mov cr0, %0" : : "r"(value) : "memory");
}

uint64_t ReadControlRegister4() noexcept {
    uint64_t value = 0ULL;
    asm volatile("mov %0, cr4" : "=r"(value));
    return value;
}

void WriteControlRegister4(const uint64_t value) noexcept {
    asm volatile("mov cr4, %0" : : "r"(value) : "memory");
}

uint64_t ProcessorPhysicalAddressWidthBits() noexcept {
    const CpuIdResult maximum_leaf = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF);
    if (maximum_leaf.accumulator < OS_KERNEL_PROCESSOR_CPUID_ADDRESS_WIDTHS_LEAF) {
        return 0ULL;
    }
    const CpuIdResult address_widths = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_ADDRESS_WIDTHS_LEAF);
    const uint64_t physical_address_width_bits =
        address_widths.accumulator & OS_KERNEL_PROCESSOR_CPUID_PHYSICAL_ADDRESS_WIDTH_MASK;
    if (physical_address_width_bits <
            OS_KERNEL_PROCESSOR_MINIMUM_PHYSICAL_ADDRESS_WIDTH_BITS ||
        physical_address_width_bits > OS_KERNEL_PROCESSOR_MAXIMUM_PAGE_TABLE_ADDRESS_WIDTH_BITS) {
        return 0ULL;
    }
    return physical_address_width_bits;
}

uint64_t ProcessorVirtualAddressWidthBits() noexcept {
    const CpuIdResult maximum_leaf = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF);
    if (maximum_leaf.accumulator < OS_KERNEL_PROCESSOR_CPUID_ADDRESS_WIDTHS_LEAF) {
        return 0ULL;
    }
    const CpuIdResult address_widths = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_ADDRESS_WIDTHS_LEAF);
    return (address_widths.accumulator & OS_KERNEL_PROCESSOR_CPUID_VIRTUAL_ADDRESS_WIDTH_MASK) >>
           OS_KERNEL_PROCESSOR_CPUID_VIRTUAL_ADDRESS_WIDTH_SHIFT;
}

uint64_t ProcessorMaximumPhysicalAddressExclusive() noexcept {
    const uint64_t physical_address_width_bits = ProcessorPhysicalAddressWidthBits();
    if (physical_address_width_bits == 0ULL) {
        return 0ULL;
    }
    return 1ULL << physical_address_width_bits;
}

bool ProcessorSupportsNoExecute() noexcept {
    const CpuIdResult maximum_leaf = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_MAXIMUM_LEAF);
    if (maximum_leaf.accumulator < OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF) {
        return false;
    }
    const CpuIdResult features = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_EXTENDED_FEATURES_LEAF);
    return (features.data & OS_KERNEL_PROCESSOR_CPUID_NO_EXECUTE_BIT) != 0U;
}

bool ProcessorSupportsLocalApic() noexcept {
    const CpuIdResult features = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_STANDARD_FEATURES_LEAF);
    return (features.data & OS_KERNEL_PROCESSOR_CPUID_LOCAL_APIC_BIT) != 0U;
}

bool ProcessorSupportsFiveLevelPaging() noexcept {
    const CpuIdResult maximum_leaf = ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_STANDARD_MAXIMUM_LEAF);
    if (maximum_leaf.accumulator < OS_KERNEL_PROCESSOR_CPUID_STRUCTURED_FEATURES_LEAF) {
        return false;
    }
    const CpuIdResult structured_features =
        ReadCpuId(OS_KERNEL_PROCESSOR_CPUID_STRUCTURED_FEATURES_LEAF);
    return (structured_features.counter & OS_KERNEL_PROCESSOR_CPUID_FIVE_LEVEL_PAGING_BIT) != 0U;
}

uint64_t LocalApicPhysicalAddress() noexcept {
    return ReadModelSpecificRegisterValue(OS_KERNEL_PROCESSOR_IA32_APIC_BASE_MSR) &
           OS_KERNEL_PROCESSOR_IA32_APIC_BASE_ADDRESS_MASK;
}

uint64_t LocalApicIdentifier() noexcept {
    if (!ProcessorSupportsLocalApic()) {
        return 0ULL;
    }
    const uint32_t identifier_register =
        *LocalApicRegister(OS_KERNEL_PROCESSOR_LOCAL_APIC_IDENTIFIER_REGISTER_OFFSET);
    return static_cast<uint64_t>(
        (identifier_register >> OS_KERNEL_PROCESSOR_LOCAL_APIC_IDENTIFIER_SHIFT_BITS) &
        OS_KERNEL_PROCESSOR_LOCAL_APIC_IDENTIFIER_MASK);
}

void AcknowledgeLocalApicInterrupt() noexcept {
    if (ProcessorSupportsLocalApic()) {
        *LocalApicRegister(OS_KERNEL_PROCESSOR_LOCAL_APIC_EOI_REGISTER_OFFSET) = 0U;
        asm volatile("" : : : "memory");
    }
}

bool EnableKernelMemoryProtection() noexcept {
    if (!ProcessorSupportsNoExecute()) {
        return false;
    }
    const uint64_t extended_feature_register =
        ReadModelSpecificRegisterValue(OS_KERNEL_PROCESSOR_IA32_EFER_MSR) |
        OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT;
    WriteModelSpecificRegisterValue(OS_KERNEL_PROCESSOR_IA32_EFER_MSR,
                                    extended_feature_register);
    WriteControlRegister0(ReadControlRegister0() | OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT);
    return KernelMemoryProtectionEnabled();
}

bool KernelMemoryProtectionEnabled() noexcept {
    return (ReadModelSpecificRegisterValue(OS_KERNEL_PROCESSOR_IA32_EFER_MSR) &
            OS_KERNEL_PROCESSOR_IA32_EFER_NO_EXECUTE_ENABLE_BIT) != 0ULL &&
           (ReadControlRegister0() & OS_KERNEL_PROCESSOR_CR0_WRITE_PROTECT_BIT) != 0ULL;
}

bool ConfigureLegacyInterruptRouting() noexcept {
    if (!ProcessorSupportsLocalApic()) {
        return true;
    }
    const uint64_t local_apic_base =
        ReadModelSpecificRegisterValue(OS_KERNEL_PROCESSOR_IA32_APIC_BASE_MSR);
    if ((local_apic_base & OS_KERNEL_PROCESSOR_IA32_APIC_GLOBAL_ENABLE_BIT) == 0ULL ||
        (local_apic_base & OS_KERNEL_PROCESSOR_IA32_APIC_X2_ENABLE_BIT) != 0ULL) {
        return false;
    }

    volatile uint32_t *const spurious_register =
        LocalApicRegister(OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_REGISTER_OFFSET);
    const uint32_t spurious_value =
        (*spurious_register & ~OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR_MASK) |
        OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR |
        OS_KERNEL_PROCESSOR_LOCAL_APIC_SOFTWARE_ENABLE_BIT;
    *spurious_register = spurious_value;

    volatile uint32_t *const lint0_register =
        LocalApicRegister(OS_KERNEL_PROCESSOR_LOCAL_APIC_LINT0_REGISTER_OFFSET);
    const uint32_t lint0_value =
        (*lint0_register & ~(OS_KERNEL_PROCESSOR_LOCAL_APIC_DELIVERY_MODE_MASK |
                             OS_KERNEL_PROCESSOR_LOCAL_APIC_MASK_BIT)) |
        OS_KERNEL_PROCESSOR_LOCAL_APIC_EXTINT_DELIVERY_MODE;
    *lint0_register = lint0_value;
    asm volatile("" : : : "memory");

    return (*spurious_register & OS_KERNEL_PROCESSOR_LOCAL_APIC_SOFTWARE_ENABLE_BIT) != 0U &&
           (*spurious_register & OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR_MASK) ==
               OS_KERNEL_PROCESSOR_LOCAL_APIC_SPURIOUS_VECTOR &&
           (*lint0_register & OS_KERNEL_PROCESSOR_LOCAL_APIC_DELIVERY_MODE_MASK) ==
               OS_KERNEL_PROCESSOR_LOCAL_APIC_EXTINT_DELIVERY_MODE &&
           (*lint0_register & OS_KERNEL_PROCESSOR_LOCAL_APIC_MASK_BIT) == 0U;
}

void ActivatePageTable(const uint64_t root_physical_address) noexcept {
    asm volatile("mov cr3, %0" : : "r"(root_physical_address) : "memory");
}

void InvalidatePage(const uint64_t virtual_address) noexcept {
    asm volatile("invlpg [%0]" : : "r"(virtual_address) : "memory");
}

void TriggerBreakpoint() noexcept { asm volatile("int3"); }

void TriggerLegacyPicSpuriousInterrupt() noexcept { asm volatile("int 0x27"); }

[[noreturn]] void TriggerInvalidOpcode() noexcept {
    asm volatile("ud2");
    HaltProcessor();
}

[[noreturn]] void TriggerPageFault() noexcept {
    const volatile uint64_t *const unmapped_address =
        reinterpret_cast<const volatile uint64_t *>(OS_KERNEL_PROCESSOR_UNMAPPED_TEST_ADDRESS);
    static_cast<void>(*unmapped_address);
    HaltProcessor();
}

[[noreturn]] void TriggerWriteProtectionFault(const uint64_t protected_address) noexcept {
    volatile uint64_t *const write_protected_address =
        reinterpret_cast<volatile uint64_t *>(protected_address);
    *write_protected_address = protected_address;
    HaltProcessor();
}
}
