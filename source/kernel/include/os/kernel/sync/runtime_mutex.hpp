#pragma once

#include <os/kernel/sync/mutex.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

class RuntimeMutex;

using RuntimeMutexAvailableOperation = bool (*)() noexcept;
using RuntimeMutexLockOperation = bool (*)(RuntimeMutex &mutex) noexcept;
using RuntimeMutexUnlockOperation = bool (*)(RuntimeMutex &mutex) noexcept;
using RuntimeMutexFailureOperation = void (*)() noexcept;

struct RuntimeMutexOperations final {
    RuntimeMutexAvailableOperation available;
    RuntimeMutexLockOperation lock;
    RuntimeMutexUnlockOperation unlock;
    RuntimeMutexFailureOperation failure;
};

enum class RuntimeMutexStatus : uint64_t {
    Succeeded,
    AlreadyConfigured,
    InvalidOperations,
    AlreadyInitialized,
    InvalidWaitQueue,
};

inline RuntimeMutexOperations runtime_mutex_operations{};
inline bool runtime_mutex_operations_configured{};

[[nodiscard]] inline RuntimeMutexStatus
ConfigureRuntimeMutexOperations(const RuntimeMutexOperations &operations) noexcept {
    if (runtime_mutex_operations_configured) {
        return RuntimeMutexStatus::AlreadyConfigured;
    }
    if (operations.available == nullptr || operations.lock == nullptr ||
        operations.unlock == nullptr || operations.failure == nullptr) {
        return RuntimeMutexStatus::InvalidOperations;
    }
    runtime_mutex_operations = operations;
    runtime_mutex_operations_configured = true;
    return RuntimeMutexStatus::Succeeded;
}

class RuntimeMutex final {
  public:
    [[nodiscard]] RuntimeMutexStatus Initialize(const WaitQueueId wait_queue_id) noexcept {
        if (this->initialized_) {
            return RuntimeMutexStatus::AlreadyInitialized;
        }
        if (wait_queue_id.value == 0ULL) {
            return RuntimeMutexStatus::InvalidWaitQueue;
        }
        this->wait_queue_id_ = wait_queue_id;
        this->runtime_primitive_initialized_ = false;
        this->runtime_mode_ = false;
        this->initialized_ = true;
        return RuntimeMutexStatus::Succeeded;
    }

    void Lock() noexcept {
        if (!this->initialized_) {
            this->Fail();
        }
        const bool runtime_available = runtime_mutex_operations_configured &&
                                       runtime_mutex_operations.available();
        if (runtime_available) {
            if (!runtime_mutex_operations.lock(*this)) {
                this->Fail();
            }
            this->runtime_mode_ = true;
        } else {
            this->fallback_lock_.Lock();
            this->runtime_mode_ = false;
        }
    }

    void Unlock() noexcept {
        if (!this->initialized_) {
            this->Fail();
        }
        const bool runtime_mode = this->runtime_mode_;
        this->runtime_mode_ = false;
        if (runtime_mode) {
            if (!runtime_mutex_operations_configured ||
                !runtime_mutex_operations.unlock(*this)) {
                this->Fail();
            }
        } else {
            this->fallback_lock_.Unlock();
        }
    }

    [[nodiscard]] bool IsInitialized() const noexcept { return this->initialized_; }

    [[nodiscard]] bool RuntimePrimitiveInitialized() const noexcept {
        return this->runtime_primitive_initialized_;
    }

    void MarkRuntimePrimitiveInitialized() noexcept {
        this->runtime_primitive_initialized_ = true;
    }

    [[nodiscard]] WaitQueueId WaitQueueIdentifier() const noexcept {
        return this->wait_queue_id_;
    }

    [[nodiscard]] Mutex &Primitive() noexcept { return this->primitive_; }

    [[nodiscard]] const Mutex &Primitive() const noexcept { return this->primitive_; }

  private:
    [[noreturn]] void Fail() const noexcept {
        if (runtime_mutex_operations_configured && runtime_mutex_operations.failure != nullptr) {
            runtime_mutex_operations.failure();
        }
        while (true) {
        }
    }

    Mutex primitive_{};
    SpinLock fallback_lock_{};
    WaitQueueId wait_queue_id_{};
    bool runtime_primitive_initialized_{};
    bool runtime_mode_{};
    bool initialized_{};
};

class RuntimeMutexGuard final {
  public:
    explicit RuntimeMutexGuard(RuntimeMutex &mutex) noexcept : mutex_{mutex} {
        this->mutex_.Lock();
    }

    ~RuntimeMutexGuard() noexcept { this->mutex_.Unlock(); }

    RuntimeMutexGuard(const RuntimeMutexGuard &) = delete;
    RuntimeMutexGuard &operator=(const RuntimeMutexGuard &) = delete;

  private:
    RuntimeMutex &mutex_;
};

}
