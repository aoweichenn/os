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
    uint64_t submittedByteCount;
    uint64_t readByteCount;
    uint64_t droppedByteCount;
    uint64_t bufferedByteCount;
};

class ConsoleInput final {
  public:
    constexpr ConsoleInput() noexcept = default;

    void Initialize() noexcept;
    [[nodiscard]] ConsoleInputStatus Submit(uint8_t character) noexcept;
    [[nodiscard]] ConsoleInputStatus TryRead(uint8_t *destination, uint64_t capacityBytes,
                                             uint64_t &readBytes) noexcept;
    [[nodiscard]] bool ReadCanProgress() const noexcept;
    [[nodiscard]] ConsoleInputStatistics Statistics() const noexcept;

  private:
    uint8_t bytes_[OS_KERNEL_CONSOLE_INPUT_CAPACITY_BYTES]{};
    uint64_t readIndex_{OS_KERNEL_CONSOLE_INPUT_INITIAL_INDEX};
    uint64_t writeIndex_{OS_KERNEL_CONSOLE_INPUT_INITIAL_INDEX};
    ConsoleInputStatistics statistics_{};
};

}
