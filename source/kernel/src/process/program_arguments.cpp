#include "os/kernel/process/program_arguments.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_POINTER_SIZE_BYTES = sizeof(uint64_t);
constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_STACK_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_STACK_ALIGNMENT_MASK =
    OS_KERNEL_PROGRAM_ARGUMENT_STACK_ALIGNMENT_BYTES - OS_KERNEL_PROGRAM_ARGUMENT_COUNTER_INCREMENT;
constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_METADATA_FIXED_POINTER_COUNT = 3ULL;

[[nodiscard]] bool CheckedAdd(const uint64_t left, const uint64_t right,
                              uint64_t &result) noexcept {
    if (left > UINT64_MAX - right) {
        return false;
    }
    result = left + right;
    return true;
}

}

void ProgramArgumentPlan::Reset() noexcept {
    for (uint64_t argument_index = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
         argument_index < OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ARGUMENT_COUNT; ++argument_index) {
        this->argument_lengths_[argument_index] = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
        this->argument_addresses_[argument_index] = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    }
    for (uint64_t environment_index = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
         environment_index < OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ENVIRONMENT_COUNT;
         ++environment_index) {
        this->environment_lengths_[environment_index] = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
        this->environment_addresses_[environment_index] = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    }
    this->layout_ = ProgramArgumentLayout{};
    this->argument_count_ = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    this->environment_count_ = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    this->total_string_bytes_ = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    this->finalized_ = false;
}

ProgramArgumentStatus
ProgramArgumentPlan::AddArgument(const uint64_t string_length_bytes) noexcept {
    if (this->finalized_) {
        return ProgramArgumentStatus::AlreadyFinalized;
    }
    if (this->argument_count_ >= OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ARGUMENT_COUNT) {
        return ProgramArgumentStatus::TooManyArguments;
    }
    uint64_t stored_string_bytes = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    if (!CheckedAdd(string_length_bytes, OS_KERNEL_PROGRAM_ARGUMENT_STRING_TERMINATOR_SIZE_BYTES,
                    stored_string_bytes)) {
        return ProgramArgumentStatus::StringTooLarge;
    }
    uint64_t candidate_total = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    if (!CheckedAdd(this->total_string_bytes_, stored_string_bytes, candidate_total) ||
        candidate_total > OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_STRING_BYTES) {
        return ProgramArgumentStatus::TotalSizeTooLarge;
    }
    this->argument_lengths_[this->argument_count_] = string_length_bytes;
    this->argument_count_ += OS_KERNEL_PROGRAM_ARGUMENT_COUNTER_INCREMENT;
    this->total_string_bytes_ = candidate_total;
    return ProgramArgumentStatus::Succeeded;
}

ProgramArgumentStatus
ProgramArgumentPlan::AddEnvironment(const uint64_t string_length_bytes) noexcept {
    if (this->finalized_) {
        return ProgramArgumentStatus::AlreadyFinalized;
    }
    if (this->environment_count_ >= OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ENVIRONMENT_COUNT) {
        return ProgramArgumentStatus::TooManyEnvironmentEntries;
    }
    uint64_t stored_string_bytes = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    if (!CheckedAdd(string_length_bytes, OS_KERNEL_PROGRAM_ARGUMENT_STRING_TERMINATOR_SIZE_BYTES,
                    stored_string_bytes)) {
        return ProgramArgumentStatus::StringTooLarge;
    }
    uint64_t candidate_total = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    if (!CheckedAdd(this->total_string_bytes_, stored_string_bytes, candidate_total) ||
        candidate_total > OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_STRING_BYTES) {
        return ProgramArgumentStatus::TotalSizeTooLarge;
    }
    this->environment_lengths_[this->environment_count_] = string_length_bytes;
    this->environment_count_ += OS_KERNEL_PROGRAM_ARGUMENT_COUNTER_INCREMENT;
    this->total_string_bytes_ = candidate_total;
    return ProgramArgumentStatus::Succeeded;
}

ProgramArgumentStatus ProgramArgumentPlan::Finalize(const uint64_t stack_bottom_address,
                                                    const uint64_t stack_top_address) noexcept {
    if (this->finalized_) {
        return ProgramArgumentStatus::AlreadyFinalized;
    }
    if (stack_bottom_address >= stack_top_address) {
        return ProgramArgumentStatus::InvalidStackRange;
    }
    const uint64_t stack_capacity_bytes = stack_top_address - stack_bottom_address;
    if (this->total_string_bytes_ > stack_capacity_bytes) {
        return ProgramArgumentStatus::StackCapacityExceeded;
    }
    const uint64_t string_region_begin_address = stack_top_address - this->total_string_bytes_;
    uint64_t pointer_count = this->argument_count_ + this->environment_count_ +
                             OS_KERNEL_PROGRAM_ARGUMENT_METADATA_FIXED_POINTER_COUNT;
    if (pointer_count > UINT64_MAX / OS_KERNEL_PROGRAM_ARGUMENT_POINTER_SIZE_BYTES) {
        return ProgramArgumentStatus::StackCapacityExceeded;
    }
    const uint64_t metadata_size_bytes =
        pointer_count * OS_KERNEL_PROGRAM_ARGUMENT_POINTER_SIZE_BYTES;
    if (metadata_size_bytes > string_region_begin_address - stack_bottom_address) {
        return ProgramArgumentStatus::StackCapacityExceeded;
    }
    const uint64_t unaligned_stack_pointer = string_region_begin_address - metadata_size_bytes;
    const uint64_t stack_pointer =
        unaligned_stack_pointer & ~OS_KERNEL_PROGRAM_ARGUMENT_STACK_ALIGNMENT_MASK;
    if (stack_pointer < stack_bottom_address) {
        return ProgramArgumentStatus::StackCapacityExceeded;
    }

    uint64_t next_string_address = string_region_begin_address;
    for (uint64_t argument_index = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
         argument_index < this->argument_count_; ++argument_index) {
        this->argument_addresses_[argument_index] = next_string_address;
        next_string_address += this->argument_lengths_[argument_index] +
                               OS_KERNEL_PROGRAM_ARGUMENT_STRING_TERMINATOR_SIZE_BYTES;
    }
    for (uint64_t environment_index = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
         environment_index < this->environment_count_; ++environment_index) {
        this->environment_addresses_[environment_index] = next_string_address;
        next_string_address += this->environment_lengths_[environment_index] +
                               OS_KERNEL_PROGRAM_ARGUMENT_STRING_TERMINATOR_SIZE_BYTES;
    }
    if (next_string_address != stack_top_address) {
        return ProgramArgumentStatus::CorruptedState;
    }

    const uint64_t argument_vector_address =
        stack_pointer + OS_KERNEL_PROGRAM_ARGUMENT_POINTER_SIZE_BYTES;
    const uint64_t environment_vector_address =
        argument_vector_address +
        (this->argument_count_ + OS_KERNEL_PROGRAM_ARGUMENT_COUNTER_INCREMENT) *
            OS_KERNEL_PROGRAM_ARGUMENT_POINTER_SIZE_BYTES;
    this->layout_ = ProgramArgumentLayout{
        .stack_pointer = stack_pointer,
        .argument_count = this->argument_count_,
        .argument_vector_address = argument_vector_address,
        .environment_vector_address = environment_vector_address,
        .string_region_begin_address = string_region_begin_address,
        .string_region_end_address = stack_top_address,
        .total_string_bytes = this->total_string_bytes_,
    };
    this->finalized_ = true;
    return this->Validate();
}

ProgramArgumentStatus ProgramArgumentPlan::ReadArgument(const uint64_t argument_index,
                                                        uint64_t &string_length_bytes,
                                                        uint64_t &string_address) const noexcept {
    string_length_bytes = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    string_address = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    if (!this->finalized_) {
        return ProgramArgumentStatus::NotFinalized;
    }
    if (argument_index >= this->argument_count_) {
        return ProgramArgumentStatus::InvalidIndex;
    }
    string_length_bytes = this->argument_lengths_[argument_index];
    string_address = this->argument_addresses_[argument_index];
    return ProgramArgumentStatus::Succeeded;
}

ProgramArgumentStatus
ProgramArgumentPlan::ReadEnvironment(const uint64_t environment_index,
                                     uint64_t &string_length_bytes,
                                     uint64_t &string_address) const noexcept {
    string_length_bytes = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    string_address = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
    if (!this->finalized_) {
        return ProgramArgumentStatus::NotFinalized;
    }
    if (environment_index >= this->environment_count_) {
        return ProgramArgumentStatus::InvalidIndex;
    }
    string_length_bytes = this->environment_lengths_[environment_index];
    string_address = this->environment_addresses_[environment_index];
    return ProgramArgumentStatus::Succeeded;
}

ProgramArgumentStatus ProgramArgumentPlan::Validate() const noexcept {
    if (!this->finalized_) {
        return ProgramArgumentStatus::NotFinalized;
    }
    if ((this->layout_.stack_pointer & OS_KERNEL_PROGRAM_ARGUMENT_STACK_ALIGNMENT_MASK) !=
            OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE ||
        this->layout_.argument_count != this->argument_count_ ||
        this->layout_.total_string_bytes != this->total_string_bytes_ ||
        this->layout_.string_region_begin_address > this->layout_.string_region_end_address ||
        this->layout_.string_region_end_address - this->layout_.string_region_begin_address !=
            this->total_string_bytes_) {
        return ProgramArgumentStatus::CorruptedState;
    }
    uint64_t expected_string_address = this->layout_.string_region_begin_address;
    for (uint64_t argument_index = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
         argument_index < this->argument_count_; ++argument_index) {
        if (this->argument_addresses_[argument_index] != expected_string_address) {
            return ProgramArgumentStatus::CorruptedState;
        }
        expected_string_address += this->argument_lengths_[argument_index] +
                                   OS_KERNEL_PROGRAM_ARGUMENT_STRING_TERMINATOR_SIZE_BYTES;
    }
    for (uint64_t environment_index = OS_KERNEL_PROGRAM_ARGUMENT_EMPTY_VALUE;
         environment_index < this->environment_count_; ++environment_index) {
        if (this->environment_addresses_[environment_index] != expected_string_address) {
            return ProgramArgumentStatus::CorruptedState;
        }
        expected_string_address += this->environment_lengths_[environment_index] +
                                   OS_KERNEL_PROGRAM_ARGUMENT_STRING_TERMINATOR_SIZE_BYTES;
    }
    return expected_string_address == this->layout_.string_region_end_address
               ? ProgramArgumentStatus::Succeeded
               : ProgramArgumentStatus::CorruptedState;
}

const ProgramArgumentLayout &ProgramArgumentPlan::Layout() const noexcept { return this->layout_; }

uint64_t ProgramArgumentPlan::ArgumentCount() const noexcept { return this->argument_count_; }

uint64_t ProgramArgumentPlan::EnvironmentCount() const noexcept { return this->environment_count_; }

bool ProgramArgumentPlan::IsFinalized() const noexcept { return this->finalized_; }

}
