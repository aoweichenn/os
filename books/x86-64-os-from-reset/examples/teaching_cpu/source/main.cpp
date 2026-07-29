#include "os/book/cpu/teaching_cpu.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

namespace os::book::cpu {
namespace {

constexpr std::uint16_t OS_BOOK_CPU_EXAMPLE_RESET_VECTOR = 0x0000U;
constexpr std::uint16_t OS_BOOK_CPU_EXAMPLE_RESULT_ADDRESS = 0x8000U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_EXPECTED_RESULT = 0x34U;
constexpr std::uint64_t OS_BOOK_CPU_EXAMPLE_DELAY_CYCLE_COUNT = 2U;
constexpr std::uint64_t OS_BOOK_CPU_EXAMPLE_NO_DELAY_CYCLE_COUNT = 0U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_R0_INDEX = 0U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_R1_INDEX = 1U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_R2_INDEX = 2U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_HIGH_BYTE_ZERO = 0x00U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_LOW_BYTE_ONE = 0x01U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_HIGH_BYTE_ONE = 0x00U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_LOW_BYTE_FFFF = 0xFFU;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_HIGH_BYTE_FFFF = 0xFFU;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_JUMP_TARGET_LOW = 0x10U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_JUMP_TARGET_HIGH = 0x00U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_CALL_TARGET_LOW = 0x18U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_CALL_TARGET_HIGH = 0x00U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_RESULT_ADDRESS_LOW = 0x00U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_RESULT_ADDRESS_HIGH = 0x80U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_RESULT_LOW = 0x34U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_RESULT_HIGH = 0x12U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_UNMAPPED_ADDRESS_LOW = 0x00U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_UNMAPPED_ADDRESS_HIGH = 0xE0U;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_NO_RESPONSE_VALUE = 0x5AU;
constexpr std::uint8_t OS_BOOK_CPU_EXAMPLE_ILLEGAL_OPCODE = 0x7EU;
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_SCENARIO_NORMAL = "normal";
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_SCENARIO_READY_DELAY =
    "ready-delay";
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_SCENARIO_ILLEGAL_OPCODE =
    "illegal-opcode";
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_SCENARIO_BUS_NO_RESPONSE =
    "bus-no-response";
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_SCENARIO_PREFIX =
    "=== scenario=";
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_SCENARIO_SUFFIX = " ===\n";
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_RESULT_STATUS =
    "result status=";
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_RESULT_CYCLES = " cycles=";
constexpr std::string_view OS_BOOK_CPU_EXAMPLE_RESULT_MEMORY =
    " memory[0x8000]=0x";

constexpr std::uint8_t EncodeOpcode(const Opcode opcode) noexcept {
    return static_cast<std::uint8_t>(opcode);
}

// 正常程序先制造零标志，再跨过失败 HALT，调用子程序并把结果写入 RAM。
constexpr auto OS_BOOK_CPU_NORMAL_PROGRAM = std::to_array<std::uint8_t>({
    EncodeOpcode(Opcode::LoadImmediate),
    OS_BOOK_CPU_EXAMPLE_R0_INDEX,
    OS_BOOK_CPU_EXAMPLE_LOW_BYTE_FFFF,
    OS_BOOK_CPU_EXAMPLE_HIGH_BYTE_FFFF,
    EncodeOpcode(Opcode::LoadImmediate),
    OS_BOOK_CPU_EXAMPLE_R1_INDEX,
    OS_BOOK_CPU_EXAMPLE_LOW_BYTE_ONE,
    OS_BOOK_CPU_EXAMPLE_HIGH_BYTE_ONE,
    EncodeOpcode(Opcode::Add),
    OS_BOOK_CPU_EXAMPLE_R0_INDEX,
    OS_BOOK_CPU_EXAMPLE_R1_INDEX,
    EncodeOpcode(Opcode::JumpIfZero),
    OS_BOOK_CPU_EXAMPLE_JUMP_TARGET_LOW,
    OS_BOOK_CPU_EXAMPLE_JUMP_TARGET_HIGH,
    EncodeOpcode(Opcode::Halt),
    EncodeOpcode(Opcode::Halt),
    EncodeOpcode(Opcode::Call),
    OS_BOOK_CPU_EXAMPLE_CALL_TARGET_LOW,
    OS_BOOK_CPU_EXAMPLE_CALL_TARGET_HIGH,
    EncodeOpcode(Opcode::Store),
    OS_BOOK_CPU_EXAMPLE_R2_INDEX,
    OS_BOOK_CPU_EXAMPLE_RESULT_ADDRESS_LOW,
    OS_BOOK_CPU_EXAMPLE_RESULT_ADDRESS_HIGH,
    EncodeOpcode(Opcode::Halt),
    EncodeOpcode(Opcode::LoadImmediate),
    OS_BOOK_CPU_EXAMPLE_R2_INDEX,
    OS_BOOK_CPU_EXAMPLE_RESULT_LOW,
    OS_BOOK_CPU_EXAMPLE_RESULT_HIGH,
    EncodeOpcode(Opcode::Return),
});

constexpr auto OS_BOOK_CPU_ILLEGAL_PROGRAM = std::to_array<std::uint8_t>({
    OS_BOOK_CPU_EXAMPLE_ILLEGAL_OPCODE,
});

// 该程序把一个 byte 写到未映射窗口，用于观察总线无响应。
constexpr auto OS_BOOK_CPU_NO_RESPONSE_PROGRAM = std::to_array<std::uint8_t>({
    EncodeOpcode(Opcode::LoadImmediate),
    OS_BOOK_CPU_EXAMPLE_R0_INDEX,
    OS_BOOK_CPU_EXAMPLE_NO_RESPONSE_VALUE,
    OS_BOOK_CPU_EXAMPLE_HIGH_BYTE_ZERO,
    EncodeOpcode(Opcode::Store),
    OS_BOOK_CPU_EXAMPLE_R0_INDEX,
    OS_BOOK_CPU_EXAMPLE_UNMAPPED_ADDRESS_LOW,
    OS_BOOK_CPU_EXAMPLE_UNMAPPED_ADDRESS_HIGH,
    EncodeOpcode(Opcode::Halt),
});

struct ScenarioResult final {
    CpuStatus status;
    std::uint8_t memory_result;
    std::uint64_t cycle_count;
};

[[nodiscard]] ScenarioResult RunScenario(
    const std::string_view scenario_name,
    const std::span<const std::uint8_t> program,
    const std::uint64_t ready_delay_cycles) {
    std::cout
        << OS_BOOK_CPU_EXAMPLE_SCENARIO_PREFIX
        << scenario_name
        << OS_BOOK_CPU_EXAMPLE_SCENARIO_SUFFIX;

    MemoryBus bus;
    bus.SetReadyDelay(ready_delay_cycles);
    if (!bus.LoadRom(OS_BOOK_CPU_EXAMPLE_RESET_VECTOR, program)) {
        return ScenarioResult{CpuStatus::BusNoResponse, 0U, 0U};
    }

    TeachingCpu cpu{bus, std::cout};
    cpu.Reset(OS_BOOK_CPU_EXAMPLE_RESET_VECTOR);
    const auto status = cpu.Run();
    const auto memory_result = bus.Inspect(OS_BOOK_CPU_EXAMPLE_RESULT_ADDRESS);
    std::cout
        << OS_BOOK_CPU_EXAMPLE_RESULT_STATUS << CpuStatusName(status)
        << OS_BOOK_CPU_EXAMPLE_RESULT_CYCLES << cpu.CycleCount()
        << OS_BOOK_CPU_EXAMPLE_RESULT_MEMORY << std::hex
        << static_cast<std::uint16_t>(memory_result)
        << std::dec << "\n";
    return ScenarioResult{status, memory_result, cpu.CycleCount()};
}

}  // namespace
}  // namespace os::book::cpu

int main() {
    using namespace os::book::cpu;

    const auto normal_result = RunScenario(
        OS_BOOK_CPU_EXAMPLE_SCENARIO_NORMAL,
        OS_BOOK_CPU_NORMAL_PROGRAM,
        OS_BOOK_CPU_EXAMPLE_NO_DELAY_CYCLE_COUNT);
    const auto delayed_result = RunScenario(
        OS_BOOK_CPU_EXAMPLE_SCENARIO_READY_DELAY,
        OS_BOOK_CPU_NORMAL_PROGRAM,
        OS_BOOK_CPU_EXAMPLE_DELAY_CYCLE_COUNT);
    const auto illegal_result = RunScenario(
        OS_BOOK_CPU_EXAMPLE_SCENARIO_ILLEGAL_OPCODE,
        OS_BOOK_CPU_ILLEGAL_PROGRAM,
        OS_BOOK_CPU_EXAMPLE_NO_DELAY_CYCLE_COUNT);
    const auto no_response_result = RunScenario(
        OS_BOOK_CPU_EXAMPLE_SCENARIO_BUS_NO_RESPONSE,
        OS_BOOK_CPU_NO_RESPONSE_PROGRAM,
        OS_BOOK_CPU_EXAMPLE_NO_DELAY_CYCLE_COUNT);

    const bool results_match =
        normal_result.status == CpuStatus::Halted
        && normal_result.memory_result == OS_BOOK_CPU_EXAMPLE_EXPECTED_RESULT
        && delayed_result.status == CpuStatus::Halted
        && delayed_result.memory_result == OS_BOOK_CPU_EXAMPLE_EXPECTED_RESULT
        && delayed_result.cycle_count > normal_result.cycle_count
        && illegal_result.status == CpuStatus::IllegalOpcode
        && no_response_result.status == CpuStatus::BusNoResponse;
    return results_match ? 0 : 1;
}
