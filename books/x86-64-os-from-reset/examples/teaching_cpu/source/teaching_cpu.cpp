#include "os/book/cpu/teaching_cpu.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>

namespace os::book::cpu {
namespace {

constexpr std::uint8_t OS_BOOK_CPU_LOW_BYTE_MASK = 0xFFU;
constexpr std::uint64_t OS_BOOK_CPU_BYTE_BIT_COUNT = 8U;
constexpr std::uint16_t OS_BOOK_CPU_STACK_WORD_BYTE_COUNT = 2U;
constexpr std::uint16_t OS_BOOK_CPU_STACK_HIGH_BYTE_OFFSET = 1U;
constexpr std::uint64_t OS_BOOK_CPU_REGISTER_R0_INDEX = 0U;
constexpr std::uint64_t OS_BOOK_CPU_REGISTER_R1_INDEX = 1U;
constexpr std::uint64_t OS_BOOK_CPU_REGISTER_R2_INDEX = 2U;
constexpr std::uint64_t OS_BOOK_CPU_REGISTER_R3_INDEX = 3U;
constexpr std::int32_t OS_BOOK_CPU_TRACE_CYCLE_WIDTH = 3;
constexpr std::int32_t OS_BOOK_CPU_TRACE_PHASE_WIDTH = 11;
constexpr std::int32_t OS_BOOK_CPU_TRACE_ADDRESS_WIDTH = 4;
constexpr std::int32_t OS_BOOK_CPU_TRACE_DATA_WIDTH = 2;
constexpr std::string_view OS_BOOK_CPU_PHASE_FETCH = "fetch";
constexpr std::string_view OS_BOOK_CPU_PHASE_DECODE = "decode";
constexpr std::string_view OS_BOOK_CPU_PHASE_EXECUTE = "execute";
constexpr std::string_view OS_BOOK_CPU_PHASE_WRITEBACK = "writeback";
constexpr std::string_view OS_BOOK_CPU_PHASE_STACK_READ = "stack-read";
constexpr std::string_view OS_BOOK_CPU_PHASE_STACK_WRITE = "stack-write";
constexpr std::string_view OS_BOOK_CPU_TRACE_INTERNAL_BUS =
    " addr=---- data=-- rd=0 wr=0 ready=1\n";
constexpr std::string_view OS_BOOK_CPU_TRACE_ADDRESS_LABEL = " addr=";
constexpr std::string_view OS_BOOK_CPU_TRACE_DATA_LABEL = " data=";
constexpr std::string_view OS_BOOK_CPU_TRACE_READ_LABEL = " rd=";
constexpr std::string_view OS_BOOK_CPU_TRACE_WRITE_LABEL = " wr=";
constexpr std::string_view OS_BOOK_CPU_TRACE_READY_LABEL = " ready=";
constexpr std::string_view OS_BOOK_CPU_TRACE_CYCLE_LABEL = "cycle=";
constexpr std::string_view OS_BOOK_CPU_TRACE_PHASE_LABEL = " phase=";
constexpr std::string_view OS_BOOK_CPU_TRACE_PC_LABEL = " pc=";
constexpr std::string_view OS_BOOK_CPU_TRACE_SP_LABEL = " sp=";
constexpr std::string_view OS_BOOK_CPU_TRACE_R0_LABEL = " r0=";
constexpr std::string_view OS_BOOK_CPU_TRACE_R1_LABEL = " r1=";
constexpr std::string_view OS_BOOK_CPU_TRACE_R2_LABEL = " r2=";
constexpr std::string_view OS_BOOK_CPU_TRACE_R3_LABEL = " r3=";
constexpr std::string_view OS_BOOK_CPU_TRACE_ZERO_LABEL = " z=";
constexpr std::string_view OS_BOOK_CPU_STATUS_RUNNING = "running";
constexpr std::string_view OS_BOOK_CPU_STATUS_HALTED = "halted";
constexpr std::string_view OS_BOOK_CPU_STATUS_ILLEGAL_OPCODE =
    "illegal-opcode";
constexpr std::string_view OS_BOOK_CPU_STATUS_INVALID_REGISTER =
    "invalid-register";
constexpr std::string_view OS_BOOK_CPU_STATUS_BUS_NO_RESPONSE =
    "bus-no-response";
constexpr std::string_view OS_BOOK_CPU_STATUS_BUS_TIMEOUT =
    "bus-timeout";
constexpr std::string_view OS_BOOK_CPU_STATUS_STACK_OVERFLOW =
    "stack-overflow";
constexpr std::string_view OS_BOOK_CPU_STATUS_STACK_UNDERFLOW =
    "stack-underflow";
constexpr std::string_view OS_BOOK_CPU_STATUS_INSTRUCTION_LIMIT =
    "instruction-limit";
constexpr std::string_view OS_BOOK_CPU_STATUS_UNKNOWN = "unknown";

}  // namespace

MemoryBus::MemoryBus() noexcept
    : memory_{},
      ready_delay_cycles_(0U) {
}

bool MemoryBus::LoadRom(
    const std::uint16_t begin_address,
    const std::span<const std::uint8_t> bytes) noexcept {
    const std::uint64_t byte_count =
        static_cast<std::uint64_t>(bytes.size());
    const std::uint64_t begin =
        static_cast<std::uint64_t>(begin_address);
    const std::uint64_t rom_end =
        static_cast<std::uint64_t>(OS_BOOK_CPU_ROM_END);

    if (byte_count == 0U || begin > rom_end) {
        return false;
    }
    if ((byte_count - 1U) > (rom_end - begin)) {
        return false;
    }

    for (std::uint64_t byte_index = 0U; byte_index < byte_count; ++byte_index) {
        this->memory_[begin + byte_index] = bytes[byte_index];
    }
    return true;
}

void MemoryBus::SetReadyDelay(
    const std::uint64_t ready_delay_cycles) noexcept {
    this->ready_delay_cycles_ = ready_delay_cycles;
}

BusAccess MemoryBus::Read(const std::uint16_t address) const noexcept {
    if (!this->IsReadable(address)) {
        return BusAccess{false, 0U, this->ready_delay_cycles_};
    }
    return BusAccess{
        true,
        this->memory_[static_cast<std::uint64_t>(address)],
        this->ready_delay_cycles_};
}

BusAccess MemoryBus::Write(
    const std::uint16_t address,
    const std::uint8_t value) noexcept {
    if (!this->IsWritable(address)) {
        return BusAccess{false, value, this->ready_delay_cycles_};
    }
    this->memory_[static_cast<std::uint64_t>(address)] = value;
    return BusAccess{true, value, this->ready_delay_cycles_};
}

std::uint8_t MemoryBus::Inspect(const std::uint16_t address) const noexcept {
    return this->memory_[static_cast<std::uint64_t>(address)];
}

bool MemoryBus::IsReadable(const std::uint16_t address) const noexcept {
    return address <= OS_BOOK_CPU_RAM_END;
}

bool MemoryBus::IsWritable(const std::uint16_t address) const noexcept {
    return address >= OS_BOOK_CPU_RAM_BEGIN && address <= OS_BOOK_CPU_RAM_END;
}

TeachingCpu::TeachingCpu(
    MemoryBus& bus,
    std::ostream& trace_output) noexcept
    : bus_(bus),
      trace_output_(trace_output),
      registers_{},
      program_counter_(0U),
      stack_pointer_(OS_BOOK_CPU_STACK_INITIAL),
      zero_flag_(false),
      status_(CpuStatus::Running),
      cycle_count_(0U),
      instruction_count_(0U),
      bus_timeout_cycles_(OS_BOOK_CPU_DEFAULT_BUS_TIMEOUT_CYCLE_COUNT) {
}

void TeachingCpu::Reset(const std::uint16_t reset_vector) noexcept {
    std::fill(this->registers_.begin(), this->registers_.end(), 0U);
    this->program_counter_ = reset_vector;
    this->stack_pointer_ = OS_BOOK_CPU_STACK_INITIAL;
    this->zero_flag_ = false;
    this->status_ = CpuStatus::Running;
    this->cycle_count_ = 0U;
    this->instruction_count_ = 0U;
}

void TeachingCpu::SetBusTimeoutCycles(
    const std::uint64_t bus_timeout_cycles) noexcept {
    this->bus_timeout_cycles_ = bus_timeout_cycles;
}

CpuStatus TeachingCpu::Run() noexcept {
    while (
        this->status_ == CpuStatus::Running
        && this->instruction_count_ < OS_BOOK_CPU_MAXIMUM_INSTRUCTION_COUNT) {
        static_cast<void>(this->Step());
    }

    if (this->status_ == CpuStatus::Running) {
        this->SetStatus(CpuStatus::InstructionLimit);
    }
    return this->status_;
}

CpuStatus TeachingCpu::Step() noexcept {
    if (this->status_ != CpuStatus::Running) {
        return this->status_;
    }

    std::uint8_t opcode_byte = 0U;
    if (!this->FetchByte(opcode_byte)) {
        return this->status_;
    }
    ++this->instruction_count_;
    this->EmitInternalCycle(OS_BOOK_CPU_PHASE_DECODE);

    const Opcode opcode = static_cast<Opcode>(opcode_byte);
    switch (opcode) {
        case Opcode::LoadImmediate: {
            std::uint8_t register_index = 0U;
            std::uint16_t immediate = 0U;
            if (!this->FetchByte(register_index) || !this->FetchWord(immediate)) {
                return this->status_;
            }
            if (!this->IsRegisterIndexValid(register_index)) {
                this->SetStatus(CpuStatus::InvalidRegister);
                return this->status_;
            }
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_EXECUTE);
            this->registers_[register_index] = immediate;
            this->zero_flag_ = immediate == 0U;
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_WRITEBACK);
            break;
        }
        case Opcode::Add: {
            std::uint8_t destination_index = 0U;
            std::uint8_t source_index = 0U;
            if (
                !this->FetchByte(destination_index)
                || !this->FetchByte(source_index)) {
                return this->status_;
            }
            if (
                !this->IsRegisterIndexValid(destination_index)
                || !this->IsRegisterIndexValid(source_index)) {
                this->SetStatus(CpuStatus::InvalidRegister);
                return this->status_;
            }
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_EXECUTE);
            this->registers_[destination_index] = static_cast<std::uint16_t>(
                this->registers_[destination_index]
                + this->registers_[source_index]);
            this->zero_flag_ = this->registers_[destination_index] == 0U;
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_WRITEBACK);
            break;
        }
        case Opcode::Store: {
            std::uint8_t register_index = 0U;
            std::uint16_t address = 0U;
            if (!this->FetchByte(register_index) || !this->FetchWord(address)) {
                return this->status_;
            }
            if (!this->IsRegisterIndexValid(register_index)) {
                this->SetStatus(CpuStatus::InvalidRegister);
                return this->status_;
            }
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_EXECUTE);
            const std::uint8_t value = static_cast<std::uint8_t>(
                this->registers_[register_index] & OS_BOOK_CPU_LOW_BYTE_MASK);
            if (!this->WriteByte(address, value, OS_BOOK_CPU_PHASE_WRITEBACK)) {
                return this->status_;
            }
            break;
        }
        case Opcode::Jump: {
            std::uint16_t target = 0U;
            if (!this->FetchWord(target)) {
                return this->status_;
            }
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_EXECUTE);
            this->program_counter_ = target;
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_WRITEBACK);
            break;
        }
        case Opcode::JumpIfZero: {
            std::uint16_t target = 0U;
            if (!this->FetchWord(target)) {
                return this->status_;
            }
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_EXECUTE);
            if (this->zero_flag_) {
                this->program_counter_ = target;
            }
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_WRITEBACK);
            break;
        }
        case Opcode::Call: {
            std::uint16_t target = 0U;
            if (!this->FetchWord(target)) {
                return this->status_;
            }
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_EXECUTE);
            if (!this->PushWord(this->program_counter_)) {
                return this->status_;
            }
            this->program_counter_ = target;
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_WRITEBACK);
            break;
        }
        case Opcode::Return: {
            std::uint16_t target = 0U;
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_EXECUTE);
            if (!this->PopWord(target)) {
                return this->status_;
            }
            this->program_counter_ = target;
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_WRITEBACK);
            break;
        }
        case Opcode::Halt:
            this->EmitInternalCycle(OS_BOOK_CPU_PHASE_EXECUTE);
            this->SetStatus(CpuStatus::Halted);
            break;
        default:
            this->SetStatus(CpuStatus::IllegalOpcode);
            break;
    }
    return this->status_;
}

std::uint16_t TeachingCpu::ProgramCounter() const noexcept {
    return this->program_counter_;
}

std::uint16_t TeachingCpu::StackPointer() const noexcept {
    return this->stack_pointer_;
}

std::uint16_t TeachingCpu::Register(
    const std::uint8_t register_index) const noexcept {
    if (!this->IsRegisterIndexValid(register_index)) {
        return 0U;
    }
    return this->registers_[register_index];
}

bool TeachingCpu::ZeroFlag() const noexcept {
    return this->zero_flag_;
}

std::uint64_t TeachingCpu::CycleCount() const noexcept {
    return this->cycle_count_;
}

bool TeachingCpu::FetchByte(std::uint8_t& value) noexcept {
    const std::uint16_t fetch_address = this->program_counter_;
    if (!this->ReadByte(fetch_address, value, OS_BOOK_CPU_PHASE_FETCH)) {
        return false;
    }
    ++this->program_counter_;
    return true;
}

bool TeachingCpu::FetchWord(std::uint16_t& value) noexcept {
    std::uint8_t low_byte = 0U;
    std::uint8_t high_byte = 0U;
    if (!this->FetchByte(low_byte) || !this->FetchByte(high_byte)) {
        return false;
    }
    value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(low_byte)
        | (static_cast<std::uint16_t>(high_byte)
           << OS_BOOK_CPU_BYTE_BIT_COUNT));
    return true;
}

bool TeachingCpu::ReadByte(
    const std::uint16_t address,
    std::uint8_t& value,
    const std::string_view phase) noexcept {
    const BusAccess access = this->bus_.Read(address);
    if (!access.response) {
        this->EmitBusCycle(phase, address, 0U, true, false, false);
        this->SetStatus(CpuStatus::BusNoResponse);
        return false;
    }
    if (!this->WaitForReady(
            access.ready_delay_cycles,
            address,
            0U,
            true,
            false,
            phase)) {
        return false;
    }
    this->EmitBusCycle(
        phase,
        address,
        access.value,
        true,
        false,
        access.response);
    value = access.value;
    return true;
}

bool TeachingCpu::WriteByte(
    const std::uint16_t address,
    const std::uint8_t value,
    const std::string_view phase) noexcept {
    const BusAccess access = this->bus_.Write(address, value);
    if (!access.response) {
        this->EmitBusCycle(phase, address, value, false, true, false);
        this->SetStatus(CpuStatus::BusNoResponse);
        return false;
    }
    if (!this->WaitForReady(
            access.ready_delay_cycles,
            address,
            value,
            false,
            true,
            phase)) {
        return false;
    }
    this->EmitBusCycle(
        phase,
        address,
        value,
        false,
        true,
        access.response);
    return true;
}

bool TeachingCpu::PushWord(const std::uint16_t value) noexcept {
    if (
        this->stack_pointer_
        < (OS_BOOK_CPU_RAM_BEGIN + OS_BOOK_CPU_STACK_WORD_BYTE_COUNT)) {
        this->SetStatus(CpuStatus::StackOverflow);
        return false;
    }
    this->stack_pointer_ = static_cast<std::uint16_t>(
        this->stack_pointer_ - OS_BOOK_CPU_STACK_WORD_BYTE_COUNT);
    const std::uint8_t low_byte = static_cast<std::uint8_t>(
        value & OS_BOOK_CPU_LOW_BYTE_MASK);
    const std::uint8_t high_byte = static_cast<std::uint8_t>(
        value >> OS_BOOK_CPU_BYTE_BIT_COUNT);
    if (
        !this->WriteByte(
            this->stack_pointer_,
            low_byte,
            OS_BOOK_CPU_PHASE_STACK_WRITE)
        || !this->WriteByte(
            static_cast<std::uint16_t>(
                this->stack_pointer_ + OS_BOOK_CPU_STACK_HIGH_BYTE_OFFSET),
            high_byte,
            OS_BOOK_CPU_PHASE_STACK_WRITE)) {
        return false;
    }
    return true;
}

bool TeachingCpu::PopWord(std::uint16_t& value) noexcept {
    if (
        this->stack_pointer_
        > (OS_BOOK_CPU_STACK_INITIAL - OS_BOOK_CPU_STACK_WORD_BYTE_COUNT)) {
        this->SetStatus(CpuStatus::StackUnderflow);
        return false;
    }
    std::uint8_t low_byte = 0U;
    std::uint8_t high_byte = 0U;
    if (
        !this->ReadByte(
            this->stack_pointer_,
            low_byte,
            OS_BOOK_CPU_PHASE_STACK_READ)
        || !this->ReadByte(
            static_cast<std::uint16_t>(
                this->stack_pointer_ + OS_BOOK_CPU_STACK_HIGH_BYTE_OFFSET),
            high_byte,
            OS_BOOK_CPU_PHASE_STACK_READ)) {
        return false;
    }
    value = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(low_byte)
        | (static_cast<std::uint16_t>(high_byte)
           << OS_BOOK_CPU_BYTE_BIT_COUNT));
    this->stack_pointer_ = static_cast<std::uint16_t>(
        this->stack_pointer_ + OS_BOOK_CPU_STACK_WORD_BYTE_COUNT);
    return true;
}

bool TeachingCpu::WaitForReady(
    const std::uint64_t ready_delay_cycles,
    const std::uint16_t address,
    const std::uint8_t value,
    const bool read,
    const bool write,
    const std::string_view phase) noexcept {
    const std::uint64_t emitted_wait_cycles = std::min(
        ready_delay_cycles,
        this->bus_timeout_cycles_);
    for (
        std::uint64_t wait_cycle = 0U;
        wait_cycle < emitted_wait_cycles;
        ++wait_cycle) {
        this->EmitBusCycle(
            phase,
            address,
            value,
            read,
            write,
            false);
    }
    if (ready_delay_cycles > this->bus_timeout_cycles_) {
        this->SetStatus(CpuStatus::BusTimeout);
        return false;
    }
    return true;
}

bool TeachingCpu::IsRegisterIndexValid(
    const std::uint8_t register_index) const noexcept {
    return register_index < OS_BOOK_CPU_REGISTER_COUNT;
}

void TeachingCpu::EmitInternalCycle(const std::string_view phase) noexcept {
    this->EmitStatePrefix(phase);
    this->trace_output_ << OS_BOOK_CPU_TRACE_INTERNAL_BUS;
    ++this->cycle_count_;
}

void TeachingCpu::EmitBusCycle(
    const std::string_view phase,
    const std::uint16_t address,
    const std::uint8_t value,
    const bool read,
    const bool write,
    const bool ready) noexcept {
    this->EmitStatePrefix(phase);
    this->trace_output_
        << OS_BOOK_CPU_TRACE_ADDRESS_LABEL
        << std::setw(OS_BOOK_CPU_TRACE_ADDRESS_WIDTH) << address
        << OS_BOOK_CPU_TRACE_DATA_LABEL
        << std::setw(OS_BOOK_CPU_TRACE_DATA_WIDTH)
        << static_cast<std::uint16_t>(value)
        << std::dec
        << OS_BOOK_CPU_TRACE_READ_LABEL << (read ? 1U : 0U)
        << OS_BOOK_CPU_TRACE_WRITE_LABEL << (write ? 1U : 0U)
        << OS_BOOK_CPU_TRACE_READY_LABEL << (ready ? 1U : 0U)
        << '\n';
    ++this->cycle_count_;
}

void TeachingCpu::EmitStatePrefix(const std::string_view phase) noexcept {
    this->trace_output_
        << std::setfill('0')
        << OS_BOOK_CPU_TRACE_CYCLE_LABEL
        << std::dec << std::setw(OS_BOOK_CPU_TRACE_CYCLE_WIDTH)
        << this->cycle_count_
        << OS_BOOK_CPU_TRACE_PHASE_LABEL
        << std::left << std::setw(OS_BOOK_CPU_TRACE_PHASE_WIDTH)
        << std::setfill(' ') << phase
        << std::right << std::setfill('0') << std::hex
        << OS_BOOK_CPU_TRACE_PC_LABEL
        << std::setw(OS_BOOK_CPU_TRACE_ADDRESS_WIDTH) << this->program_counter_
        << OS_BOOK_CPU_TRACE_SP_LABEL
        << std::setw(OS_BOOK_CPU_TRACE_ADDRESS_WIDTH) << this->stack_pointer_
        << OS_BOOK_CPU_TRACE_R0_LABEL
        << std::setw(OS_BOOK_CPU_TRACE_ADDRESS_WIDTH)
        << this->registers_[OS_BOOK_CPU_REGISTER_R0_INDEX]
        << OS_BOOK_CPU_TRACE_R1_LABEL
        << std::setw(OS_BOOK_CPU_TRACE_ADDRESS_WIDTH)
        << this->registers_[OS_BOOK_CPU_REGISTER_R1_INDEX]
        << OS_BOOK_CPU_TRACE_R2_LABEL
        << std::setw(OS_BOOK_CPU_TRACE_ADDRESS_WIDTH)
        << this->registers_[OS_BOOK_CPU_REGISTER_R2_INDEX]
        << OS_BOOK_CPU_TRACE_R3_LABEL
        << std::setw(OS_BOOK_CPU_TRACE_ADDRESS_WIDTH)
        << this->registers_[OS_BOOK_CPU_REGISTER_R3_INDEX]
        << std::dec
        << OS_BOOK_CPU_TRACE_ZERO_LABEL << (this->zero_flag_ ? 1U : 0U)
        << std::hex;
}

void TeachingCpu::SetStatus(const CpuStatus status) noexcept {
    this->status_ = status;
}

std::string_view CpuStatusName(const CpuStatus status) noexcept {
    switch (status) {
        case CpuStatus::Running:
            return OS_BOOK_CPU_STATUS_RUNNING;
        case CpuStatus::Halted:
            return OS_BOOK_CPU_STATUS_HALTED;
        case CpuStatus::IllegalOpcode:
            return OS_BOOK_CPU_STATUS_ILLEGAL_OPCODE;
        case CpuStatus::InvalidRegister:
            return OS_BOOK_CPU_STATUS_INVALID_REGISTER;
        case CpuStatus::BusNoResponse:
            return OS_BOOK_CPU_STATUS_BUS_NO_RESPONSE;
        case CpuStatus::BusTimeout:
            return OS_BOOK_CPU_STATUS_BUS_TIMEOUT;
        case CpuStatus::StackOverflow:
            return OS_BOOK_CPU_STATUS_STACK_OVERFLOW;
        case CpuStatus::StackUnderflow:
            return OS_BOOK_CPU_STATUS_STACK_UNDERFLOW;
        case CpuStatus::InstructionLimit:
            return OS_BOOK_CPU_STATUS_INSTRUCTION_LIMIT;
    }
    return OS_BOOK_CPU_STATUS_UNKNOWN;
}

}  // namespace os::book::cpu
