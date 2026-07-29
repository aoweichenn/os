#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string_view>

namespace os::book::cpu {

inline constexpr std::uint64_t OS_BOOK_CPU_MEMORY_BYTE_COUNT = 65'536U;
inline constexpr std::uint16_t OS_BOOK_CPU_ROM_BEGIN = 0x0000U;
inline constexpr std::uint16_t OS_BOOK_CPU_ROM_END = 0x7FFFU;
inline constexpr std::uint16_t OS_BOOK_CPU_RAM_BEGIN = 0x8000U;
inline constexpr std::uint16_t OS_BOOK_CPU_RAM_END = 0xDFFFU;
inline constexpr std::uint16_t OS_BOOK_CPU_UNMAPPED_BEGIN = 0xE000U;
inline constexpr std::uint16_t OS_BOOK_CPU_STACK_INITIAL = 0xDFFEU;
inline constexpr std::uint64_t OS_BOOK_CPU_REGISTER_COUNT = 4U;
inline constexpr std::uint64_t OS_BOOK_CPU_MAXIMUM_INSTRUCTION_COUNT = 64U;
inline constexpr std::uint64_t OS_BOOK_CPU_DEFAULT_BUS_TIMEOUT_CYCLE_COUNT =
    8U;

enum class Opcode : std::uint8_t {
    LoadImmediate = 0x10U,
    Add = 0x20U,
    Store = 0x30U,
    Jump = 0x40U,
    JumpIfZero = 0x41U,
    Call = 0x50U,
    Return = 0x51U,
    Halt = 0xFFU,
};

enum class CpuStatus : std::uint8_t {
    Running,
    Halted,
    IllegalOpcode,
    InvalidRegister,
    BusNoResponse,
    BusTimeout,
    StackOverflow,
    StackUnderflow,
    InstructionLimit,
};

struct BusAccess final {
    bool response;
    std::uint8_t value;
    std::uint64_t ready_delay_cycles;
};

class MemoryBus final {
public:
    MemoryBus() noexcept;

    [[nodiscard]] bool LoadRom(
        std::uint16_t begin_address,
        std::span<const std::uint8_t> bytes) noexcept;
    void SetReadyDelay(std::uint64_t ready_delay_cycles) noexcept;

    [[nodiscard]] BusAccess Read(std::uint16_t address) const noexcept;
    [[nodiscard]] BusAccess Write(
        std::uint16_t address,
        std::uint8_t value) noexcept;
    [[nodiscard]] std::uint8_t Inspect(std::uint16_t address) const noexcept;

private:
    [[nodiscard]] bool IsReadable(std::uint16_t address) const noexcept;
    [[nodiscard]] bool IsWritable(std::uint16_t address) const noexcept;

    std::array<std::uint8_t, OS_BOOK_CPU_MEMORY_BYTE_COUNT> memory_;
    std::uint64_t ready_delay_cycles_;
};

class TeachingCpu final {
public:
    TeachingCpu(MemoryBus& bus, std::ostream& trace_output) noexcept;

    void Reset(std::uint16_t reset_vector) noexcept;
    void SetBusTimeoutCycles(std::uint64_t bus_timeout_cycles) noexcept;
    [[nodiscard]] CpuStatus Run() noexcept;
    [[nodiscard]] CpuStatus Step() noexcept;

    [[nodiscard]] std::uint16_t ProgramCounter() const noexcept;
    [[nodiscard]] std::uint16_t StackPointer() const noexcept;
    [[nodiscard]] std::uint16_t Register(std::uint8_t register_index) const noexcept;
    [[nodiscard]] bool ZeroFlag() const noexcept;
    [[nodiscard]] std::uint64_t CycleCount() const noexcept;

private:
    [[nodiscard]] bool FetchByte(std::uint8_t& value) noexcept;
    [[nodiscard]] bool FetchWord(std::uint16_t& value) noexcept;
    [[nodiscard]] bool ReadByte(
        std::uint16_t address,
        std::uint8_t& value,
        std::string_view phase) noexcept;
    [[nodiscard]] bool WriteByte(
        std::uint16_t address,
        std::uint8_t value,
        std::string_view phase) noexcept;
    [[nodiscard]] bool PushWord(std::uint16_t value) noexcept;
    [[nodiscard]] bool PopWord(std::uint16_t& value) noexcept;
    [[nodiscard]] bool WaitForReady(
        std::uint64_t ready_delay_cycles,
        std::uint16_t address,
        std::uint8_t value,
        bool read,
        bool write,
        std::string_view phase) noexcept;
    [[nodiscard]] bool IsRegisterIndexValid(
        std::uint8_t register_index) const noexcept;
    void EmitInternalCycle(std::string_view phase) noexcept;
    void EmitBusCycle(
        std::string_view phase,
        std::uint16_t address,
        std::uint8_t value,
        bool read,
        bool write,
        bool ready) noexcept;
    void EmitStatePrefix(std::string_view phase) noexcept;
    void SetStatus(CpuStatus status) noexcept;

    MemoryBus& bus_;
    std::ostream& trace_output_;
    std::array<std::uint16_t, OS_BOOK_CPU_REGISTER_COUNT> registers_;
    std::uint16_t program_counter_;
    std::uint16_t stack_pointer_;
    bool zero_flag_;
    CpuStatus status_;
    std::uint64_t cycle_count_;
    std::uint64_t instruction_count_;
    std::uint64_t bus_timeout_cycles_;
};

[[nodiscard]] std::string_view CpuStatusName(CpuStatus status) noexcept;

}  // namespace os::book::cpu
