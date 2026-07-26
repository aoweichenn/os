#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PROCESS_CAPACITY = 4ULL;
inline constexpr uint64_t OS_KERNEL_PROCESS_DEFAULT_QUANTUM_TICKS = 4ULL;
inline constexpr uint64_t OS_KERNEL_PROCESS_INVALID_INDEX = UINT64_MAX;

enum class ProcessState : uint64_t {
    Unused,
    Ready,
    Running,
    Blocked,
    Terminated,
};

enum class ProcessWaitReason : uint64_t {
    None,
    PipeReadable,
    PipeWritable,
    DescriptorReadable,
    DescriptorWritable,
};

enum class ProcessSchedulerStatus : uint64_t {
    Succeeded,
    NotInitialized,
    InvalidQuantum,
    CapacityExhausted,
    NoReadyProcess,
    AlreadyRunning,
    InvalidCurrentProcess,
    InvalidProcessIndex,
    InvalidWaitReason,
    InvalidWakeCount,
};

struct ProcessSchedulingDecision final {
    uint64_t previous_process_index;
    uint64_t current_process_index;
    bool switched;
    bool completed;
};

struct ProcessSchedulerEntry final {
    uint64_t process_id;
    ProcessState state;
    uint64_t run_tick_count;
    uint64_t dispatch_count;
    uint64_t block_count;
    uint64_t wakeup_count;
    ProcessWaitReason wait_reason;
};

struct ProcessSchedulerStatistics final {
    uint64_t created_process_count;
    uint64_t terminated_process_count;
    uint64_t timer_tick_count;
    uint64_t preemption_count;
    uint64_t dispatch_count;
    uint64_t block_count;
    uint64_t wakeup_count;
};

class ProcessScheduler final {
  public:
    [[nodiscard]] ProcessSchedulerStatus Initialize(uint64_t quantum_ticks) noexcept;
    [[nodiscard]] ProcessSchedulerStatus CreateProcess(uint64_t &process_index,
                                                       uint64_t &process_id) noexcept;
    [[nodiscard]] ProcessSchedulerStatus DiscardReadyProcess(uint64_t process_index) noexcept;
    [[nodiscard]] ProcessSchedulerStatus Start(ProcessSchedulingDecision &decision) noexcept;
    [[nodiscard]] ProcessSchedulerStatus
    HandleTimerTick(ProcessSchedulingDecision &decision) noexcept;
    [[nodiscard]] ProcessSchedulerStatus
    TerminateCurrentProcess(ProcessSchedulingDecision &decision) noexcept;
    [[nodiscard]] ProcessSchedulerStatus
    BlockCurrentProcess(ProcessWaitReason wait_reason,
                        ProcessSchedulingDecision &decision) noexcept;
    [[nodiscard]] ProcessSchedulerStatus
    WakeBlockedProcesses(ProcessWaitReason wait_reason, uint64_t maximum_wake_count,
                         uint64_t &woken_process_count) noexcept;
    [[nodiscard]] ProcessSchedulerStatus ReadEntry(uint64_t process_index,
                                                   ProcessSchedulerEntry &entry) const noexcept;
    [[nodiscard]] ProcessSchedulerStatistics Statistics() const noexcept;
    [[nodiscard]] uint64_t CurrentProcessIndex() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;

  private:
    [[nodiscard]] bool FindFreeProcess(uint64_t &process_index) const noexcept;
    [[nodiscard]] bool FindNextReadyProcess(uint64_t first_process_index,
                                            uint64_t &process_index) const noexcept;
    [[nodiscard]] bool HasBlockedProcess() const noexcept;
    void ActivateProcess(uint64_t process_index, uint64_t previous_process_index, bool switched,
                         ProcessSchedulingDecision &decision) noexcept;
    void ResetDecision(ProcessSchedulingDecision &decision) const noexcept;

    ProcessSchedulerEntry entries_[OS_KERNEL_PROCESS_CAPACITY];
    ProcessSchedulerStatistics statistics_;
    uint64_t quantum_ticks_;
    uint64_t elapsed_quantum_ticks_;
    uint64_t next_process_id_;
    uint64_t current_process_index_;
    bool initialized_;
};

}
