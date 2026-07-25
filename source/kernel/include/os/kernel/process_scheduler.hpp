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
    Terminated,
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
};

struct ProcessSchedulingDecision final {
    uint64_t previousProcessIndex;
    uint64_t currentProcessIndex;
    bool switched;
    bool completed;
};

struct ProcessSchedulerEntry final {
    uint64_t processId;
    ProcessState state;
    uint64_t runTickCount;
    uint64_t dispatchCount;
};

struct ProcessSchedulerStatistics final {
    uint64_t createdProcessCount;
    uint64_t terminatedProcessCount;
    uint64_t timerTickCount;
    uint64_t preemptionCount;
    uint64_t dispatchCount;
};

class ProcessScheduler final {
  public:
    [[nodiscard]] ProcessSchedulerStatus Initialize(uint64_t quantumTicks) noexcept;
    [[nodiscard]] ProcessSchedulerStatus CreateProcess(uint64_t &processIndex,
                                                       uint64_t &processId) noexcept;
    [[nodiscard]] ProcessSchedulerStatus DiscardReadyProcess(uint64_t processIndex) noexcept;
    [[nodiscard]] ProcessSchedulerStatus Start(ProcessSchedulingDecision &decision) noexcept;
    [[nodiscard]] ProcessSchedulerStatus
    HandleTimerTick(ProcessSchedulingDecision &decision) noexcept;
    [[nodiscard]] ProcessSchedulerStatus
    TerminateCurrentProcess(ProcessSchedulingDecision &decision) noexcept;
    [[nodiscard]] ProcessSchedulerStatus
    ReadEntry(uint64_t processIndex, ProcessSchedulerEntry &entry) const noexcept;
    [[nodiscard]] ProcessSchedulerStatistics Statistics() const noexcept;
    [[nodiscard]] uint64_t CurrentProcessIndex() const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;

  private:
    [[nodiscard]] bool FindFreeProcess(uint64_t &processIndex) const noexcept;
    [[nodiscard]] bool FindNextReadyProcess(uint64_t firstProcessIndex,
                                            uint64_t &processIndex) const noexcept;
    void ActivateProcess(uint64_t processIndex, uint64_t previousProcessIndex,
                         bool switched, ProcessSchedulingDecision &decision) noexcept;
    void ResetDecision(ProcessSchedulingDecision &decision) const noexcept;

    ProcessSchedulerEntry entries_[OS_KERNEL_PROCESS_CAPACITY];
    ProcessSchedulerStatistics statistics_;
    uint64_t quantumTicks_;
    uint64_t elapsedQuantumTicks_;
    uint64_t nextProcessId_;
    uint64_t currentProcessIndex_;
    bool initialized_;
};

}
