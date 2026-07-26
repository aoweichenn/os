#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES = 256ULL;
inline constexpr uint64_t OS_KERNEL_CONSOLE_INPUT_INITIAL_INDEX = 0ULL;

enum class ConsoleInputStatus : uint64_t {
    Succeeded,
    Empty,
    Full,
    InvalidArgument,
};

struct ConsoleInputStatistics final {
    uint64_t submitted_byte_count;
    uint64_t read_byte_count;
    uint64_t dropped_byte_count;
    uint64_t buffered_byte_count;
};

class ConsoleInput final {
  public:
    constexpr ConsoleInput() noexcept = default;

    void Initialize() noexcept;
    [[nodiscard]] ConsoleInputStatus Submit(uint8_t character) noexcept;
    [[nodiscard]] ConsoleInputStatus TryRead(uint8_t *destination, uint64_t capacity_bytes,
                                             uint64_t &read_bytes) noexcept;
    [[nodiscard]] bool ReadCanProgress() const noexcept;
    [[nodiscard]] ConsoleInputStatistics Statistics() const noexcept;

  private:
    uint8_t bytes_[OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES]{};
    uint64_t read_index_{OS_KERNEL_CONSOLE_INPUT_INITIAL_INDEX};
    uint64_t write_index_{OS_KERNEL_CONSOLE_INPUT_INITIAL_INDEX};
    ConsoleInputStatistics statistics_{};
};

}
