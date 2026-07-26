#pragma once

#include "os/kernel/process/wait_queue.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PROCESS_BOOTSTRAP_CAPACITY = 4ULL;
inline constexpr uint64_t OS_KERNEL_THREAD_BOOTSTRAP_CAPACITY = 4ULL;
inline constexpr uint64_t OS_KERNEL_BOOTSTRAP_THREADS_PER_PROCESS = 1ULL;
inline constexpr uint64_t OS_KERNEL_PROCESS_FUNCTIONAL_CAPACITY = 64ULL;
inline constexpr uint64_t OS_KERNEL_THREAD_FUNCTIONAL_CAPACITY = 128ULL;
inline constexpr uint64_t OS_KERNEL_FUNCTIONAL_THREADS_PER_PROCESS = 32ULL;
inline constexpr uint64_t OS_KERNEL_PROCESS_CAPACITY_LIMIT = 256ULL;
inline constexpr uint64_t OS_KERNEL_THREAD_CAPACITY_LIMIT = 512ULL;
inline constexpr uint64_t OS_KERNEL_CAPACITY_THREADS_PER_PROCESS = 64ULL;
inline constexpr uint64_t OS_KERNEL_THREAD_DEFAULT_QUANTUM_TICKS = 4ULL;
inline constexpr uint64_t OS_KERNEL_PROCESS_INVALID_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_THREAD_INVALID_INDEX = UINT64_MAX;

struct ProcessId final {
    uint64_t value;
};

struct ThreadId final {
    uint64_t value;
};

enum class ProcessState : uint64_t {
    Unused,
    Alive,
    Zombie,
};

enum class ThreadState : uint64_t {
    Unused,
    Ready,
    Running,
    Blocked,
    Exited,
};

enum class ThreadSchedulerStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    NullProcessStorage,
    NullThreadStorage,
    InvalidProcessCapacity,
    InvalidThreadCapacity,
    InvalidThreadsPerProcess,
    InvalidQuantum,
    ProcessCapacityExhausted,
    ThreadCapacityExhausted,
    ProcessThreadLimitReached,
    IdentifierExhausted,
    InvalidAddressSpace,
    InvalidKernelStack,
    InvalidProcessIndex,
    InvalidThreadIndex,
    InvalidProcessState,
    InvalidThreadState,
    ProcessThreadsRemain,
    ProcessNotZombie,
    ThreadNotExited,
    NoReadyThread,
    AlreadyRunning,
    InvalidCurrentThread,
    InvalidWaitCondition,
    InvalidWakeReason,
    InvalidWakeCount,
    WaitQueueNotInitialized,
    WaitQueueClosed,
    WakeAlreadyResolved,
    CorruptedState,
};

struct ProcessEntry final {
    ProcessId process_id;
    ProcessState state;
    uint64_t address_space_root_physical_address;
    uint64_t first_thread_index;
    uint64_t thread_count;
    uint64_t live_thread_count;
    uint64_t exited_thread_count;
};

struct ThreadEntry final {
    ThreadId thread_id;
    uint64_t process_index;
    ThreadState state;
    uint64_t kernel_stack_slot_index;
    uint64_t user_stack_pointer;
    uint64_t thread_local_storage_base;
    uint64_t signal_mask;
    uint64_t run_tick_count;
    uint64_t dispatch_count;
    uint64_t block_count;
    uint64_t wake_count;
    WaitCondition wait_condition;
    WakeReason wake_reason;
    uint64_t next_process_thread_index;
    uint64_t previous_run_thread_index;
    uint64_t next_run_thread_index;
    uint64_t next_wait_thread_index;
    WaitQueue *wait_queue;
};

struct ThreadSchedulingDecision final {
    uint64_t previous_thread_index;
    uint64_t current_thread_index;
    bool switched;
    bool completed;
    bool idle;
};

struct ThreadSchedulerStatistics final {
    uint64_t process_capacity;
    uint64_t thread_capacity;
    uint64_t maximum_threads_per_process;
    uint64_t owned_process_count;
    uint64_t alive_process_count;
    uint64_t zombie_process_count;
    uint64_t owned_thread_count;
    uint64_t ready_thread_count;
    uint64_t running_thread_count;
    uint64_t blocked_thread_count;
    uint64_t exited_thread_count;
    uint64_t created_process_count;
    uint64_t discarded_process_count;
    uint64_t reaped_process_count;
    uint64_t created_thread_count;
    uint64_t discarded_thread_count;
    uint64_t reaped_thread_count;
    uint64_t timer_tick_count;
    uint64_t preemption_count;
    uint64_t yield_count;
    uint64_t dispatch_count;
    uint64_t block_count;
    uint64_t wake_count;
    uint64_t zombie_transition_count;
};

class ThreadScheduler final {
  public:
    [[nodiscard]] ThreadSchedulerStatus
    Initialize(ProcessEntry *process_storage, uint64_t process_capacity,
               ThreadEntry *thread_storage, uint64_t thread_capacity,
               uint64_t maximum_threads_per_process, uint64_t quantum_ticks) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    CreateProcess(uint64_t address_space_root_physical_address, uint64_t &process_index,
                  ProcessId &process_id) noexcept;
    [[nodiscard]] ThreadSchedulerStatus DiscardProcess(uint64_t process_index) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    CreateThread(uint64_t process_index, uint64_t kernel_stack_slot_index,
                 uint64_t user_stack_pointer, uint64_t thread_local_storage_base,
                 uint64_t signal_mask, uint64_t &thread_index, ThreadId &thread_id) noexcept;
    [[nodiscard]] ThreadSchedulerStatus DiscardReadyThread(uint64_t thread_index) noexcept;
    [[nodiscard]] ThreadSchedulerStatus Start(ThreadSchedulingDecision &decision) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    HandleTimerTick(ThreadSchedulingDecision &decision) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    YieldCurrentThread(ThreadSchedulingDecision &decision) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    BlockCurrentThread(WaitQueue &wait_queue, WaitCondition wait_condition,
                       ThreadSchedulingDecision &decision) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    WakeOne(WaitQueue &wait_queue, WakeReason wake_reason, uint64_t &woken_thread_index,
            bool &wake_won) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    WakeThread(WaitQueue &wait_queue, uint64_t thread_index, WakeReason wake_reason,
               bool &wake_won) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    WakeMany(WaitQueue &wait_queue, WakeReason wake_reason, uint64_t maximum_wake_count,
             uint64_t &woken_thread_count) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    CloseWaitQueue(WaitQueue &wait_queue, uint64_t &woken_thread_count) noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    TerminateCurrentThread(ThreadSchedulingDecision &decision) noexcept;
    [[nodiscard]] ThreadSchedulerStatus ReapExitedThread(uint64_t thread_index) noexcept;
    [[nodiscard]] ThreadSchedulerStatus ReapZombieProcess(uint64_t process_index) noexcept;
    [[nodiscard]] ThreadSchedulerStatus ReadProcess(uint64_t process_index,
                                                    ProcessEntry &entry) const noexcept;
    [[nodiscard]] ThreadSchedulerStatus ReadThread(uint64_t thread_index,
                                                   ThreadEntry &entry) const noexcept;
    [[nodiscard]] ThreadSchedulerStatus Validate() const noexcept;
    [[nodiscard]] ThreadSchedulerStatus
    ValidateWaitQueue(const WaitQueue &wait_queue) const noexcept;
    [[nodiscard]] ThreadSchedulerStatistics Statistics() const noexcept;
    [[nodiscard]] uint64_t CurrentThreadIndex() const noexcept;
    [[nodiscard]] ThreadSchedulerStatus CurrentThreadId(ThreadId &thread_id) const noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

  private:
    [[nodiscard]] bool FindFreeProcess(uint64_t &process_index) const noexcept;
    [[nodiscard]] bool FindFreeThread(uint64_t &thread_index) const noexcept;
    void AppendReadyThread(uint64_t thread_index) noexcept;
    [[nodiscard]] bool PopReadyThread(uint64_t &thread_index) noexcept;
    void RemoveReadyThread(uint64_t thread_index) noexcept;
    void AppendProcessThread(uint64_t process_index, uint64_t thread_index) noexcept;
    void RemoveProcessThread(uint64_t process_index, uint64_t thread_index) noexcept;
    void AppendWaitingThread(WaitQueue &wait_queue, uint64_t thread_index) noexcept;
    void RemoveWaitingThread(WaitQueue &wait_queue, uint64_t thread_index) noexcept;
    void ActivateThread(uint64_t thread_index, uint64_t previous_thread_index, bool switched,
                        ThreadSchedulingDecision &decision) noexcept;
    void SelectAfterCurrentStops(uint64_t previous_thread_index,
                                 ThreadSchedulingDecision &decision) noexcept;
    void ResetDecision(ThreadSchedulingDecision &decision) const noexcept;
    [[nodiscard]] bool HasBlockedThread() const noexcept;
    [[nodiscard]] bool HasLiveThread() const noexcept;
    [[nodiscard]] bool ProcessContainsThread(uint64_t process_index,
                                             uint64_t thread_index) const noexcept;

    ProcessEntry *processes_{};
    ThreadEntry *threads_{};
    uint64_t process_capacity_{};
    uint64_t thread_capacity_{};
    uint64_t maximum_threads_per_process_{};
    uint64_t quantum_ticks_{};
    uint64_t elapsed_quantum_ticks_{};
    uint64_t next_process_id_{};
    uint64_t next_thread_id_{};
    uint64_t ready_head_thread_index_{OS_KERNEL_THREAD_INVALID_INDEX};
    uint64_t ready_tail_thread_index_{OS_KERNEL_THREAD_INVALID_INDEX};
    uint64_t current_thread_index_{OS_KERNEL_THREAD_INVALID_INDEX};
    ThreadSchedulerStatistics cumulative_statistics_{};
    bool initialized_{};
};

}
