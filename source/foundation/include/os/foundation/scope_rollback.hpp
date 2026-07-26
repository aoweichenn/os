#pragma once

#include <stdint.h>

namespace os::foundation {

using ScopeRollbackOperation = bool (*)(void *context) noexcept;

struct ScopeRollbackAction final {
    ScopeRollbackOperation operation;
    void *context;
};

enum class ScopeRollbackState : uint64_t {
    Uninitialized,
    Armed,
    Committed,
    RolledBack,
    RollbackFailed,
};

enum class ScopeRollbackStatus : uint64_t {
    Succeeded,
    NullActionStorage,
    EmptyActionCapacity,
    AlreadyInitialized,
    NotInitialized,
    NullOperation,
    ActionCapacityExhausted,
    AlreadyFinalized,
    ActionFailed,
};

// 动作存储由调用方持有，回滚严格按照注册顺序的逆序执行。
// 析构仅作为遗漏路径的安全网；需要传播失败的调用方必须显式调用 TryRollback。
class ScopeRollback final {
  public:
    ScopeRollback() noexcept;
    ~ScopeRollback() noexcept;
    ScopeRollback(const ScopeRollback &) = delete;
    ScopeRollback &operator=(const ScopeRollback &) = delete;
    ScopeRollback(ScopeRollback &&) = delete;
    ScopeRollback &operator=(ScopeRollback &&) = delete;

    [[nodiscard]] ScopeRollbackStatus Initialize(ScopeRollbackAction *action_storage,
                                                 uint64_t action_capacity) noexcept;
    [[nodiscard]] ScopeRollbackStatus TryPush(ScopeRollbackOperation operation,
                                              void *context) noexcept;
    [[nodiscard]] ScopeRollbackStatus Commit() noexcept;
    [[nodiscard]] ScopeRollbackStatus TryRollback() noexcept;
    [[nodiscard]] ScopeRollbackState State() const noexcept;
    [[nodiscard]] uint64_t ActionCount() const noexcept;
    [[nodiscard]] uint64_t FailureCount() const noexcept;

  private:
    ScopeRollbackAction *actions_;
    uint64_t action_capacity_;
    uint64_t action_count_;
    uint64_t failure_count_;
    ScopeRollbackState state_;
};

}
