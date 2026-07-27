#pragma once

#include <stdint.h>

namespace os::user {

using ThreadFunction = uint64_t (*)(void *argument) noexcept;

struct alignas(16) ThreadRuntimeState final {
    ThreadRuntimeState *self;
    ThreadFunction function;
    void *argument;
    uint64_t thread_id;
    uint64_t validation_tag;
};

class Thread final {
  public:
    Thread() noexcept = default;
    Thread(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;

    [[nodiscard]] bool Create(ThreadFunction function, void *argument) noexcept;
    [[nodiscard]] bool Join(uint64_t &exit_value) noexcept;
    [[nodiscard]] bool IsJoinable() const noexcept;
    [[nodiscard]] uint64_t Id() const noexcept;

  private:
    uint64_t thread_id_{};
    uint64_t stack_base_address_{};
    uint64_t stack_size_bytes_{};
    uint64_t thread_local_storage_base_{};
    bool joinable_{};
};

[[nodiscard]] bool InitializeMainThreadRuntime(ThreadRuntimeState &runtime_state) noexcept;
[[nodiscard]] ThreadRuntimeState *CurrentThreadRuntimeState() noexcept;

extern "C" [[noreturn]] void OsUserThreadEntry(uint64_t runtime_state_address) noexcept;

}
