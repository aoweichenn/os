#pragma once

#include "os/kernel/ipc/pipe.hpp"
#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PIPE_MANAGER_BOOTSTRAP_CAPACITY = 8ULL;
inline constexpr uint64_t OS_KERNEL_PIPE_MANAGER_FUNCTIONAL_CAPACITY = 128ULL;
inline constexpr uint64_t OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY = 1024ULL;

enum class PipeManagerStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    NotInitialized,
    InvalidConfiguration,
    CapacityExhausted,
    InvalidPipe,
    EndpointFailure,
    CorruptedState,
};

struct PipeManagerStatistics final {
    uint64_t capacity;
    uint64_t active_pipe_count;
    uint64_t peak_active_pipe_count;
    uint64_t creation_count;
    uint64_t release_count;
    uint64_t capacity_rejection_count;
};

class PipeManager final {
  public:
    PipeManager() noexcept = default;
    PipeManager(const PipeManager &) = delete;
    PipeManager &operator=(const PipeManager &) = delete;

    [[nodiscard]] PipeManagerStatus Initialize(const PipePageAllocator &page_allocator,
                                               uint64_t capacity) noexcept;
    [[nodiscard]] PipeManagerStatus Create(Pipe *&pipe) noexcept;
    [[nodiscard]] PipeManagerStatus CloseReader(Pipe &pipe) noexcept;
    [[nodiscard]] PipeManagerStatus CloseWriter(Pipe &pipe) noexcept;
    [[nodiscard]] PipeManagerStatus Validate() noexcept;
    [[nodiscard]] PipeManagerStatistics Statistics() noexcept;

  private:
    [[nodiscard]] uint64_t FindPipeIndex(const Pipe &pipe) const noexcept;
    [[nodiscard]] PipeManagerStatus ReleaseIfClosed(Pipe &pipe) noexcept;

    Pipe pipes_[OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY];
    bool active_[OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY];
    PipePageAllocator page_allocator_;
    SpinLock lock_;
    PipeManagerStatistics statistics_;
    bool initialized_;
};

}
