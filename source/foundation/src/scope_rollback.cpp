#include "os/foundation/scope_rollback.hpp"

namespace os::foundation {

namespace {

constexpr uint64_t OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT = 0ULL;

}

ScopeRollback::ScopeRollback() noexcept
    : actions_{nullptr}, action_capacity_{OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT},
      action_count_{OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT},
      failure_count_{OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT},
      state_{ScopeRollbackState::Uninitialized} {}

ScopeRollback::~ScopeRollback() noexcept {
    if (this->state_ == ScopeRollbackState::Armed) {
        static_cast<void>(this->TryRollback());
    }
}

ScopeRollbackStatus
ScopeRollback::Initialize(ScopeRollbackAction *const action_storage,
                          const uint64_t action_capacity) noexcept {
    if (this->state_ != ScopeRollbackState::Uninitialized) {
        return ScopeRollbackStatus::AlreadyInitialized;
    }
    if (action_storage == nullptr) {
        return ScopeRollbackStatus::NullActionStorage;
    }
    if (action_capacity == OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT) {
        return ScopeRollbackStatus::EmptyActionCapacity;
    }
    for (uint64_t action_index = OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT;
         action_index < action_capacity; ++action_index) {
        action_storage[action_index] = ScopeRollbackAction{};
    }
    this->actions_ = action_storage;
    this->action_capacity_ = action_capacity;
    this->action_count_ = OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT;
    this->failure_count_ = OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT;
    this->state_ = ScopeRollbackState::Armed;
    return ScopeRollbackStatus::Succeeded;
}

ScopeRollbackStatus
ScopeRollback::TryPush(const ScopeRollbackOperation operation, void *const context) noexcept {
    if (this->state_ == ScopeRollbackState::Uninitialized) {
        return ScopeRollbackStatus::NotInitialized;
    }
    if (this->state_ != ScopeRollbackState::Armed) {
        return ScopeRollbackStatus::AlreadyFinalized;
    }
    if (operation == nullptr) {
        return ScopeRollbackStatus::NullOperation;
    }
    if (this->action_count_ >= this->action_capacity_) {
        return ScopeRollbackStatus::ActionCapacityExhausted;
    }
    this->actions_[this->action_count_] = ScopeRollbackAction{
        .operation = operation,
        .context = context,
    };
    ++this->action_count_;
    return ScopeRollbackStatus::Succeeded;
}

ScopeRollbackStatus ScopeRollback::Commit() noexcept {
    if (this->state_ == ScopeRollbackState::Uninitialized) {
        return ScopeRollbackStatus::NotInitialized;
    }
    if (this->state_ != ScopeRollbackState::Armed) {
        return ScopeRollbackStatus::AlreadyFinalized;
    }
    while (this->action_count_ > OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT) {
        --this->action_count_;
        this->actions_[this->action_count_] = ScopeRollbackAction{};
    }
    this->state_ = ScopeRollbackState::Committed;
    return ScopeRollbackStatus::Succeeded;
}

ScopeRollbackStatus ScopeRollback::TryRollback() noexcept {
    if (this->state_ == ScopeRollbackState::Uninitialized) {
        return ScopeRollbackStatus::NotInitialized;
    }
    if (this->state_ != ScopeRollbackState::Armed) {
        return ScopeRollbackStatus::AlreadyFinalized;
    }

    this->failure_count_ = OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT;
    while (this->action_count_ > OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT) {
        --this->action_count_;
        const ScopeRollbackAction action = this->actions_[this->action_count_];
        // 调用前清除槽位，保证回调即使失败也不会被析构路径重复执行。
        this->actions_[this->action_count_] = ScopeRollbackAction{};
        if (!action.operation(action.context)) {
            ++this->failure_count_;
        }
    }
    if (this->failure_count_ != OS_FOUNDATION_SCOPE_ROLLBACK_EMPTY_COUNT) {
        this->state_ = ScopeRollbackState::RollbackFailed;
        return ScopeRollbackStatus::ActionFailed;
    }
    this->state_ = ScopeRollbackState::RolledBack;
    return ScopeRollbackStatus::Succeeded;
}

ScopeRollbackState ScopeRollback::State() const noexcept { return this->state_; }

uint64_t ScopeRollback::ActionCount() const noexcept { return this->action_count_; }

uint64_t ScopeRollback::FailureCount() const noexcept { return this->failure_count_; }

}
