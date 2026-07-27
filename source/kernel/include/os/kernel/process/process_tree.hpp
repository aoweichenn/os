#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PROCESS_TREE_INVALID_INDEX = UINT64_MAX;
inline constexpr uint64_t OS_KERNEL_PROCESS_TREE_WAIT_ANY_PROCESS_ID = UINT64_MAX;

enum class ProcessTreeState : uint64_t {
    Unused,
    Alive,
    Zombie,
};

enum class ProcessTreeTerminationReason : uint64_t {
    None,
    Exited,
    Exception,
};

enum class ProcessTreeStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidProcessIndex,
    InvalidProcessId,
    InvalidParent,
    InvalidState,
    InitAlreadyRegistered,
    InitRequired,
    ProcessAlreadyRegistered,
    ProcessNotRegistered,
    ProcessHasChildren,
    NoMatchingChild,
    ChildStillRunning,
    CorruptedState,
};

struct ProcessTreeExitStatus final {
    ProcessTreeTerminationReason termination_reason;
    int64_t exit_code;
    uint64_t exception_vector;
};

struct ProcessTreeEntry final {
    uint64_t process_id;
    uint64_t parent_process_index;
    ProcessTreeState state;
    ProcessTreeExitStatus exit_status;
};

struct ProcessTreeWaitResult final {
    uint64_t process_id;
    uint64_t process_index;
    uint64_t parent_process_id;
    ProcessTreeExitStatus exit_status;
};

struct ProcessTreeStatistics final {
    uint64_t capacity;
    uint64_t active_process_count;
    uint64_t alive_process_count;
    uint64_t zombie_process_count;
    uint64_t registered_process_count;
    uint64_t exited_process_count;
    uint64_t collected_process_count;
    uint64_t reparented_process_count;
    uint64_t wait_attempt_count;
    uint64_t wait_success_count;
    uint64_t wait_block_count;
    uint64_t wait_no_child_count;
};

class ProcessTree final {
  public:
    [[nodiscard]] ProcessTreeStatus Initialize(ProcessTreeEntry *entry_storage,
                                               uint64_t capacity) noexcept;
    [[nodiscard]] ProcessTreeStatus RegisterInit(uint64_t process_index,
                                                 uint64_t process_id) noexcept;
    [[nodiscard]] ProcessTreeStatus RegisterChild(uint64_t process_index, uint64_t process_id,
                                                  uint64_t parent_process_index) noexcept;
    [[nodiscard]] ProcessTreeStatus MarkExited(uint64_t process_index,
                                               const ProcessTreeExitStatus &exit_status,
                                               uint64_t &reparented_process_count) noexcept;
    [[nodiscard]] ProcessTreeStatus TryWait(uint64_t parent_process_index,
                                            uint64_t requested_process_id,
                                            ProcessTreeWaitResult &wait_result) noexcept;
    [[nodiscard]] ProcessTreeStatus CollectInit(ProcessTreeWaitResult &wait_result) noexcept;
    [[nodiscard]] ProcessTreeStatus Read(uint64_t process_index,
                                         ProcessTreeEntry &entry) const noexcept;
    [[nodiscard]] ProcessTreeStatus Validate() const noexcept;
    [[nodiscard]] ProcessTreeStatistics Statistics() const noexcept;
    [[nodiscard]] uint64_t InitProcessIndex() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

  private:
    [[nodiscard]] bool IsRegistered(uint64_t process_index) const noexcept;
    [[nodiscard]] bool HasChild(uint64_t parent_process_index) const noexcept;
    [[nodiscard]] bool MatchesRequestedProcess(const ProcessTreeEntry &entry,
                                               uint64_t requested_process_id) const noexcept;
    [[nodiscard]] ProcessTreeStatus Collect(uint64_t process_index,
                                            ProcessTreeWaitResult &wait_result,
                                            bool record_wait_success) noexcept;

    ProcessTreeEntry *entries_{};
    uint64_t capacity_{};
    uint64_t init_process_index_{OS_KERNEL_PROCESS_TREE_INVALID_INDEX};
    ProcessTreeStatistics statistics_{};
    bool initialized_{};
};

}
