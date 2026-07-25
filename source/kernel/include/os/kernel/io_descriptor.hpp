#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_IO_STANDARD_INPUT_DESCRIPTOR = 0ULL;
inline constexpr uint64_t OS_KERNEL_IO_STANDARD_OUTPUT_DESCRIPTOR = 1ULL;
inline constexpr uint64_t OS_KERNEL_IO_STANDARD_ERROR_DESCRIPTOR = 2ULL;
inline constexpr uint64_t OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR = 3ULL;
inline constexpr uint64_t OS_KERNEL_IO_DESCRIPTOR_CAPACITY = 8ULL;

enum class IoDescriptorKind : uint64_t {
    Closed,
    ConsoleInput,
    ConsoleOutput,
    ConsoleError,
    RegularFile,
    Directory,
    PipeReader,
    PipeWriter,
};

enum class IoDescriptorStatus : uint64_t {
    Succeeded,
    InvalidDescriptor,
    CapacityExhausted,
    PermissionDenied,
    InvalidKind,
};

class IoDescriptorTable final {
  public:
    void Initialize(bool attachPipeReader, bool attachPipeWriter) noexcept;
    [[nodiscard]] IoDescriptorStatus Allocate(IoDescriptorKind kind,
                                              uint64_t &descriptor) noexcept;
    [[nodiscard]] IoDescriptorStatus Lookup(uint64_t descriptor,
                                            IoDescriptorKind &kind) const noexcept;
    [[nodiscard]] IoDescriptorStatus Close(uint64_t descriptor,
                                           IoDescriptorKind &closedKind) noexcept;

  private:
    // Initialize 会在任何读取前完整覆盖表项；去掉类内初始化可保持该类型
    // 平凡构造，避免 freestanding 内核产生隐藏的 .init_array。
    IoDescriptorKind descriptors_[OS_KERNEL_IO_DESCRIPTOR_CAPACITY];
};

}
