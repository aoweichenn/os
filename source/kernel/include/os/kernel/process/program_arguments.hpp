#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ARGUMENT_COUNT = 256ULL;
inline constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ENVIRONMENT_COUNT = 256ULL;
inline constexpr uint64_t OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_STRING_BYTES = 128ULL * 1024ULL;

enum class ProgramArgumentStatus : uint64_t {
    Succeeded,
    InvalidStackRange,
    TooManyArguments,
    TooManyEnvironmentEntries,
    StringTooLarge,
    TotalSizeTooLarge,
    StackCapacityExceeded,
    AlreadyFinalized,
    NotFinalized,
    InvalidIndex,
    CorruptedState,
};

struct ProgramArgumentLayout final {
    uint64_t stack_pointer;
    uint64_t argument_count;
    uint64_t argument_vector_address;
    uint64_t environment_vector_address;
    uint64_t string_region_begin_address;
    uint64_t string_region_end_address;
    uint64_t total_string_bytes;
};

class ProgramArgumentPlan final {
  public:
    void Reset() noexcept;
    [[nodiscard]] ProgramArgumentStatus AddArgument(uint64_t string_length_bytes) noexcept;
    [[nodiscard]] ProgramArgumentStatus AddEnvironment(uint64_t string_length_bytes) noexcept;
    [[nodiscard]] ProgramArgumentStatus Finalize(uint64_t stack_bottom_address,
                                                 uint64_t stack_top_address) noexcept;
    [[nodiscard]] ProgramArgumentStatus ReadArgument(uint64_t argument_index,
                                                     uint64_t &string_length_bytes,
                                                     uint64_t &string_address) const noexcept;
    [[nodiscard]] ProgramArgumentStatus ReadEnvironment(uint64_t environment_index,
                                                        uint64_t &string_length_bytes,
                                                        uint64_t &string_address) const noexcept;
    [[nodiscard]] ProgramArgumentStatus Validate() const noexcept;
    [[nodiscard]] const ProgramArgumentLayout &Layout() const noexcept;
    [[nodiscard]] uint64_t ArgumentCount() const noexcept;
    [[nodiscard]] uint64_t EnvironmentCount() const noexcept;
    [[nodiscard]] bool IsFinalized() const noexcept;

  private:
    uint64_t argument_lengths_[OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ARGUMENT_COUNT]{};
    uint64_t argument_addresses_[OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ARGUMENT_COUNT]{};
    uint64_t environment_lengths_[OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ENVIRONMENT_COUNT]{};
    uint64_t environment_addresses_[OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ENVIRONMENT_COUNT]{};
    ProgramArgumentLayout layout_{};
    uint64_t argument_count_{};
    uint64_t environment_count_{};
    uint64_t total_string_bytes_{};
    bool finalized_{};
};

}
