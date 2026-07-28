#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_JOB_CONTROL_INVALID_INDEX = UINT64_MAX;

enum class JobControlStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidProcessIndex,
    InvalidProcessId,
    InvalidProcessGroup,
    InvalidSession,
    ProcessAlreadyRegistered,
    ProcessNotFound,
    PermissionDenied,
    SessionLeader,
    ProcessGroupLeader,
    ProcessThreadsRemain,
    CorruptedState,
};

struct JobControlProcessState final {
    uint64_t process_id;
    uint64_t process_group_id;
    uint64_t session_id;
    bool active;
    bool session_leader;
};

struct JobControlStatistics final {
    uint64_t capacity;
    uint64_t active_process_count;
    uint64_t active_session_count;
    uint64_t active_process_group_count;
    uint64_t session_create_count;
    uint64_t process_group_change_count;
};

class JobControlManager final {
  public:
    [[nodiscard]] JobControlStatus Initialize(JobControlProcessState *process_storage,
                                              uint64_t capacity) noexcept;
    [[nodiscard]] JobControlStatus RegisterInit(uint64_t process_index,
                                                uint64_t process_id) noexcept;
    [[nodiscard]] JobControlStatus ForkProcess(uint64_t parent_process_index,
                                               uint64_t child_process_index,
                                               uint64_t child_process_id) noexcept;
    [[nodiscard]] JobControlStatus RemoveProcess(uint64_t process_index) noexcept;
    [[nodiscard]] JobControlStatus CreateSession(uint64_t process_index,
                                                 uint64_t &session_id) noexcept;
    [[nodiscard]] JobControlStatus SetProcessGroup(uint64_t caller_process_index,
                                                   uint64_t target_process_index,
                                                   uint64_t process_group_id) noexcept;
    [[nodiscard]] JobControlStatus ReadProcess(uint64_t process_index,
                                              JobControlProcessState &state) const noexcept;
    [[nodiscard]] JobControlStatus FindProcess(uint64_t process_id,
                                              uint64_t &process_index) const noexcept;
    [[nodiscard]] bool GroupBelongsToSession(uint64_t process_group_id,
                                             uint64_t session_id) const noexcept;
    [[nodiscard]] JobControlStatus Validate() const noexcept;
    [[nodiscard]] JobControlStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] bool ProcessGroupExists(uint64_t process_group_id,
                                          uint64_t session_id) const noexcept;

    JobControlProcessState *processes_{};
    uint64_t capacity_{};
    JobControlStatistics statistics_{};
    bool initialized_{};
};

}
