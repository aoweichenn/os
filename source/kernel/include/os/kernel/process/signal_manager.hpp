#pragma once

#include "os/abi/signal.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_SIGNAL_INVALID_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_SIGNAL_ACTION_CAPACITY = os::abi::OS_ABI_SIGNAL_MAXIMUM_NUMBER;

enum class SignalManagerStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidProcessIndex,
    InvalidThreadIndex,
    InvalidProcessId,
    InvalidThreadId,
    InvalidProcessGroup,
    InvalidSignal,
    InvalidAction,
    ProcessAlreadyRegistered,
    ThreadAlreadyRegistered,
    ProcessNotFound,
    ThreadNotFound,
    ProcessThreadsRemain,
    SignalFrameNotActive,
    SignalFrameMismatch,
    CapacityExhausted,
    CorruptedState,
};

enum class SignalDeliveryKind : uint64_t {
    None,
    DefaultTerminate,
    UserHandler,
};

struct SignalProcessState final {
    uint64_t process_id;
    uint64_t process_group_id;
    uint64_t pending_set;
    uint64_t next_thread_index;
    os::abi::SignalAction actions[OS_KERNEL_SIGNAL_ACTION_CAPACITY];
    bool active;
};

struct SignalThreadState final {
    uint64_t thread_id;
    uint64_t process_index;
    uint64_t signal_mask;
    uint64_t pending_set;
    uint64_t active_frame_address;
    uint64_t active_frame_cookie;
    uint64_t active_signal_number;
    uint64_t active_restorer_address;
    uint64_t active_previous_mask;
    bool active;
    bool frame_active;
};

struct SignalDelivery final {
    SignalDeliveryKind kind;
    uint64_t signal_number;
    uint64_t previous_mask;
    os::abi::SignalAction action;
};

struct SignalManagerStatistics final {
    uint64_t active_process_count;
    uint64_t active_thread_count;
    uint64_t queued_signal_count;
    uint64_t coalesced_signal_count;
    uint64_t ignored_signal_count;
    uint64_t handler_delivery_count;
    uint64_t default_termination_count;
    uint64_t process_group_send_count;
    uint64_t interrupted_wait_count;
    uint64_t restarted_wait_count;
    uint64_t rejected_frame_count;
};

class SignalManager final {
  public:
    [[nodiscard]] SignalManagerStatus Initialize(SignalProcessState *process_storage,
                                                 uint64_t process_capacity,
                                                 SignalThreadState *thread_storage,
                                                 uint64_t thread_capacity) noexcept;
    [[nodiscard]] SignalManagerStatus RegisterProcess(uint64_t process_index, uint64_t process_id,
                                                      uint64_t process_group_id) noexcept;
    [[nodiscard]] SignalManagerStatus ForkProcess(uint64_t parent_process_index,
                                                  uint64_t child_process_index,
                                                  uint64_t child_process_id) noexcept;
    [[nodiscard]] SignalManagerStatus ExecProcess(uint64_t process_index,
                                                  uint64_t surviving_thread_index) noexcept;
    [[nodiscard]] SignalManagerStatus RemoveProcess(uint64_t process_index) noexcept;
    [[nodiscard]] SignalManagerStatus RegisterThread(uint64_t thread_index, uint64_t process_index,
                                                     uint64_t thread_id,
                                                     uint64_t signal_mask) noexcept;
    [[nodiscard]] SignalManagerStatus RemoveThread(uint64_t thread_index) noexcept;
    [[nodiscard]] SignalManagerStatus SetAction(uint64_t process_index, uint64_t signal_number,
                                                const os::abi::SignalAction &action,
                                                os::abi::SignalAction &previous_action) noexcept;
    [[nodiscard]] SignalManagerStatus SetThreadMask(uint64_t thread_index, uint64_t signal_mask,
                                                    uint64_t &previous_signal_mask) noexcept;
    [[nodiscard]] SignalManagerStatus SendToProcess(uint64_t process_id, uint64_t signal_number,
                                                    uint64_t &selected_thread_index) noexcept;
    [[nodiscard]] SignalManagerStatus
    SendToProcessGroup(uint64_t process_group_id, uint64_t signal_number,
                       uint64_t *selected_thread_storage, uint64_t selected_thread_capacity,
                       uint64_t &selected_thread_count, uint64_t &target_process_count) noexcept;
    [[nodiscard]] SignalManagerStatus BeginThreadDelivery(uint64_t thread_index,
                                                          SignalDelivery &delivery) noexcept;
    [[nodiscard]] SignalManagerStatus CommitHandlerFrame(uint64_t thread_index,
                                                         uint64_t frame_address,
                                                         uint64_t frame_cookie) noexcept;
    [[nodiscard]] SignalManagerStatus
    CompleteHandlerFrame(uint64_t thread_index, uint64_t frame_address, uint64_t frame_cookie,
                         uint64_t signal_number, uint64_t restorer_address,
                         uint64_t restored_mask) noexcept;
    [[nodiscard]] SignalManagerStatus
    ValidateHandlerFrame(uint64_t thread_index, uint64_t frame_address, uint64_t frame_cookie,
                         uint64_t signal_number, uint64_t restorer_address,
                         uint64_t restored_mask) const noexcept;
    [[nodiscard]] SignalManagerStatus GetProcessGroup(uint64_t process_index,
                                                      uint64_t &process_group_id) const noexcept;
    [[nodiscard]] SignalManagerStatus SetProcessGroup(uint64_t process_index,
                                                      uint64_t process_group_id) noexcept;
    [[nodiscard]] SignalManagerStatus FindProcess(uint64_t process_id,
                                                  uint64_t &process_index) const noexcept;
    [[nodiscard]] SignalManagerStatus ReadProcess(uint64_t process_index,
                                                  SignalProcessState &state) const noexcept;
    [[nodiscard]] SignalManagerStatus ReadThread(uint64_t thread_index,
                                                 SignalThreadState &state) const noexcept;
    void RecordInterruptedWait(bool restarted) noexcept;
    void RecordRejectedFrame() noexcept;
    [[nodiscard]] SignalManagerStatus Validate() const noexcept;
    [[nodiscard]] SignalManagerStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool SignalNumberIsValid(uint64_t signal_number) const noexcept;
    [[nodiscard]] bool ActionIsValid(uint64_t signal_number,
                                     const os::abi::SignalAction &action) const noexcept;
    [[nodiscard]] bool SignalAlreadyPending(uint64_t process_index,
                                            uint64_t signal_number) const noexcept;
    [[nodiscard]] bool DefaultDispositionIgnores(uint64_t signal_number) const noexcept;
    [[nodiscard]] SignalManagerStatus QueueForProcess(uint64_t process_index,
                                                      uint64_t signal_number,
                                                      uint64_t &selected_thread_index) noexcept;
    [[nodiscard]] SignalManagerStatus AssignPendingSignal(uint64_t process_index,
                                                          uint64_t signal_number,
                                                          uint64_t &selected_thread_index) noexcept;
    void AssignAllEligiblePending(uint64_t process_index) noexcept;

    SignalProcessState *processes_{};
    SignalThreadState *threads_{};
    uint64_t process_capacity_{};
    uint64_t thread_capacity_{};
    uint64_t next_frame_cookie_{};
    SignalManagerStatistics statistics_{};
    bool initialized_{};
};

}
