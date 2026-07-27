#include "os/user/thread.hpp"

#include "os/abi/system_call.hpp"
#include "os/abi/virtual_memory.hpp"
#include "os/user/system_call.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_THREAD_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_THREAD_STACK_RESERVATION_SIZE_BYTES =
    os::abi::OS_ABI_THREAD_STACK_DEFAULT_SIZE_BYTES + os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_USER_THREAD_RUNTIME_VALIDATION_TAG = 0x4F53544852454144ULL;
constexpr uint64_t OS_USER_THREAD_READ_WRITE_PROTECTION =
    os::abi::OS_ABI_MEMORY_PROTECTION_READ | os::abi::OS_ABI_MEMORY_PROTECTION_WRITE;
constexpr int64_t OS_USER_THREAD_FIRST_ERROR_RESULT = 0LL;

[[nodiscard]] bool ReleaseMapping(const uint64_t address, const uint64_t size_bytes) noexcept {
    return address == OS_USER_THREAD_EMPTY_VALUE ||
           UnmapMemory(address, size_bytes) == static_cast<int64_t>(OS_USER_THREAD_EMPTY_VALUE);
}

}

bool Thread::Create(ThreadFunction const function, void *const argument) noexcept {
    if (function == nullptr || this->joinable_) {
        return false;
    }
    const int64_t stack_reservation_result = MapAnonymousMemory(
        os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS, OS_USER_THREAD_STACK_RESERVATION_SIZE_BYTES,
        OS_USER_THREAD_READ_WRITE_PROTECTION, os::abi::OS_ABI_MEMORY_MAP_NO_FLAGS);
    if (stack_reservation_result < OS_USER_THREAD_FIRST_ERROR_RESULT) {
        return false;
    }
    const uint64_t stack_reservation_address = static_cast<uint64_t>(stack_reservation_result);
    const uint64_t stack_base_address =
        stack_reservation_address + os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
    const int64_t thread_local_storage_result = MapAnonymousMemory(
        os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES,
        OS_USER_THREAD_READ_WRITE_PROTECTION, os::abi::OS_ABI_MEMORY_MAP_NO_FLAGS);
    if (thread_local_storage_result < OS_USER_THREAD_FIRST_ERROR_RESULT) {
        static_cast<void>(
            ReleaseMapping(stack_reservation_address, OS_USER_THREAD_STACK_RESERVATION_SIZE_BYTES));
        return false;
    }
    const uint64_t thread_local_storage_base = static_cast<uint64_t>(thread_local_storage_result);
    if (UnmapMemory(stack_reservation_address, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES) !=
        static_cast<int64_t>(OS_USER_THREAD_EMPTY_VALUE)) {
        static_cast<void>(
            ReleaseMapping(thread_local_storage_base, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES));
        static_cast<void>(
            ReleaseMapping(stack_reservation_address, OS_USER_THREAD_STACK_RESERVATION_SIZE_BYTES));
        return false;
    }
    auto *const runtime_state = reinterpret_cast<ThreadRuntimeState *>(thread_local_storage_base);
    *runtime_state = ThreadRuntimeState{
        .self = runtime_state,
        .function = function,
        .argument = argument,
        .thread_id = OS_USER_THREAD_EMPTY_VALUE,
        .validation_tag = OS_USER_THREAD_RUNTIME_VALIDATION_TAG,
    };
    const uint64_t stack_pointer = stack_base_address +
                                   os::abi::OS_ABI_THREAD_STACK_DEFAULT_SIZE_BYTES -
                                   os::abi::OS_ABI_THREAD_ENTRY_STACK_REMAINDER_BYTES;
    *reinterpret_cast<uint64_t *>(stack_pointer) = OS_USER_THREAD_EMPTY_VALUE;
    const os::abi::ThreadCreateRequest request{
        .entry_address = reinterpret_cast<uint64_t>(&OsUserThreadEntry),
        .argument = thread_local_storage_base,
        .stack_base_address = stack_base_address,
        .stack_size_bytes = os::abi::OS_ABI_THREAD_STACK_DEFAULT_SIZE_BYTES,
        .stack_pointer = stack_pointer,
        .thread_local_storage_base = thread_local_storage_base,
    };
    const int64_t create_result = os::user::CreateThread(request);
    if (create_result <= OS_USER_THREAD_FIRST_ERROR_RESULT) {
        static_cast<void>(
            ReleaseMapping(thread_local_storage_base, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES));
        static_cast<void>(
            ReleaseMapping(stack_base_address, os::abi::OS_ABI_THREAD_STACK_DEFAULT_SIZE_BYTES));
        return false;
    }
    this->thread_id_ = static_cast<uint64_t>(create_result);
    this->stack_base_address_ = stack_base_address;
    this->stack_size_bytes_ = os::abi::OS_ABI_THREAD_STACK_DEFAULT_SIZE_BYTES;
    this->thread_local_storage_base_ = thread_local_storage_base;
    this->joinable_ = true;
    return true;
}

bool Thread::Join(uint64_t &exit_value) noexcept {
    exit_value = OS_USER_THREAD_EMPTY_VALUE;
    if (!this->joinable_ || this->thread_id_ == OS_USER_THREAD_EMPTY_VALUE) {
        return false;
    }
    os::abi::ThreadJoinResult result{};
    int64_t join_result = os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK;
    while (join_result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
        join_result = JoinThread(this->thread_id_, result);
    }
    if (join_result != static_cast<int64_t>(this->thread_id_) ||
        result.thread_id != this->thread_id_) {
        return false;
    }
    const bool stack_released = ReleaseMapping(this->stack_base_address_, this->stack_size_bytes_);
    const bool thread_local_storage_released =
        ReleaseMapping(this->thread_local_storage_base_, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES);
    if (!stack_released || !thread_local_storage_released) {
        return false;
    }
    exit_value = result.exit_value;
    this->thread_id_ = OS_USER_THREAD_EMPTY_VALUE;
    this->stack_base_address_ = OS_USER_THREAD_EMPTY_VALUE;
    this->stack_size_bytes_ = OS_USER_THREAD_EMPTY_VALUE;
    this->thread_local_storage_base_ = OS_USER_THREAD_EMPTY_VALUE;
    this->joinable_ = false;
    return true;
}

bool Thread::IsJoinable() const noexcept { return this->joinable_; }

uint64_t Thread::Id() const noexcept { return this->thread_id_; }

bool InitializeMainThreadRuntime(ThreadRuntimeState &runtime_state) noexcept {
    runtime_state = ThreadRuntimeState{
        .self = &runtime_state,
        .function = nullptr,
        .argument = nullptr,
        .thread_id = GetThreadId(),
        .validation_tag = OS_USER_THREAD_RUNTIME_VALIDATION_TAG,
    };
    return runtime_state.thread_id != OS_USER_THREAD_EMPTY_VALUE &&
           reinterpret_cast<uint64_t>(&runtime_state) %
                   os::abi::OS_ABI_THREAD_LOCAL_STORAGE_ALIGNMENT_BYTES ==
               OS_USER_THREAD_EMPTY_VALUE &&
           SetThreadLocalStorage(reinterpret_cast<uint64_t>(&runtime_state)) ==
               static_cast<int64_t>(OS_USER_THREAD_EMPTY_VALUE) &&
           CurrentThreadRuntimeState() == &runtime_state;
}

ThreadRuntimeState *CurrentThreadRuntimeState() noexcept {
    ThreadRuntimeState *runtime_state = nullptr;
    asm volatile("mov %0, qword ptr fs:[0]" : "=r"(runtime_state));
    return runtime_state != nullptr && runtime_state->self == runtime_state &&
                   runtime_state->validation_tag == OS_USER_THREAD_RUNTIME_VALIDATION_TAG
               ? runtime_state
               : nullptr;
}

extern "C" [[noreturn]] void OsUserThreadEntry(const uint64_t runtime_state_address) noexcept {
    auto *const runtime_state = reinterpret_cast<ThreadRuntimeState *>(runtime_state_address);
    const uint64_t current_thread_id = GetThreadId();
    if (runtime_state == nullptr || runtime_state->self != runtime_state ||
        runtime_state->function == nullptr ||
        runtime_state->validation_tag != OS_USER_THREAD_RUNTIME_VALIDATION_TAG ||
        current_thread_id == OS_USER_THREAD_EMPTY_VALUE ||
        CurrentThreadRuntimeState() != runtime_state) {
        ExitThread(UINT64_MAX);
    }
    // TID 只能由已经开始执行的子线程写入，避免父线程返回前被抢占造成发布竞态。
    runtime_state->thread_id = current_thread_id;
    const uint64_t exit_value = runtime_state->function(runtime_state->argument);
    ExitThread(exit_value);
}

}
