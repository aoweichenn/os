#include "os/kernel/process/process_runtime.hpp"

#include "os/kernel/arch/descriptor_tables.hpp"
#include "os/kernel/arch/processor.hpp"
#include "os/kernel/device/serial_port.hpp"
#include "os/kernel/memory/memory_manager.hpp"
#include "os/kernel/sync/spin_lock.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS = 0x202ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT =
    OS_KERNEL_THREAD_CAPACITY_LIMIT;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_STACK_POINTER_PROBE_SIZE_BYTES = sizeof(uint64_t);
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FUNCTIONAL_MEMORY_BYTES =
    256ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_MEMORY_BYTES =
    64ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_BASE =
    0x00007FFFFFF00000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_STRIDE_BYTES =
    0x0000000000001000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PIPE_READABLE_QUEUE_ID = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PIPE_WRITABLE_QUEUE_ID = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_READABLE_QUEUE_ID = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_WRITABLE_QUEUE_ID = 4ULL;
constexpr int64_t OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE = 0LL;

[[nodiscard]] ProcessIoStatus MapPipeIoStatus(const PipeStatus status) noexcept {
    if (status == PipeStatus::Succeeded) {
        return ProcessIoStatus::Succeeded;
    }
    if (status == PipeStatus::WouldBlock) {
        return ProcessIoStatus::WouldBlock;
    }
    if (status == PipeStatus::EndOfFile) {
        return ProcessIoStatus::EndOfFile;
    }
    if (status == PipeStatus::BrokenPipe) {
        return ProcessIoStatus::BrokenPipe;
    }
    if (status == PipeStatus::AlreadyClosed) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    return ProcessIoStatus::InvalidArgument;
}

struct ProcessRuntimeLimits final {
    uint64_t process_capacity;
    uint64_t thread_capacity;
    uint64_t maximum_threads_per_process;
};

struct ProcessRuntimeProcess final {
    UserAddressSpace address_space;
    ProcessExecutionResult result;
    IoDescriptorTable descriptors;
    FileSystemHandle file_handles[OS_KERNEL_IO_DESCRIPTOR_CAPACITY];
    bool active;
};

struct alignas(OS_KERNEL_EXTENDED_STATE_AREA_ALIGNMENT_BYTES)
    ProcessRuntimeThread final {
    ExceptionFrame *saved_frame;
    FxSaveArea extended_state;
    bool active;
};

ThreadScheduler thread_scheduler;
ProcessEntry process_entries[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ThreadEntry thread_entries[OS_KERNEL_THREAD_CAPACITY_LIMIT];
Pipe process_pipe;
ConsoleInput process_console_input;
ProcessRuntimeProcess runtime_processes[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ProcessRuntimeThread runtime_threads[OS_KERNEL_THREAD_CAPACITY_LIMIT];
WaitQueue pipe_readable_wait_queue;
WaitQueue pipe_writable_wait_queue;
WaitQueue descriptor_readable_wait_queue;
WaitQueue descriptor_writable_wait_queue;
constinit IrqSaveSpinLock scheduler_lock{DisableInterrupts, RestoreInterrupts};
FileSystem *process_file_system;
ProcessRuntimeLimits process_runtime_limits;
PhysicalFrameAllocatorStatistics frames_before_processes;
PhysicalFrameAllocatorStatistics frames_after_processes;
KernelVirtualAddressAllocatorStatistics virtual_addresses_before_processes;
KernelVirtualAddressAllocatorStatistics virtual_addresses_after_processes;
KernelStackManagerStatistics kernel_stacks_before_processes;
KernelStackManagerStatistics kernel_stacks_after_processes;
ResourceSnapshot resource_snapshot_before_processes;
ResourceSnapshot resource_snapshot_after_processes;
ResourceSnapshotDifference resource_snapshot_difference;
uint64_t pipe_reader_block_count;
uint64_t pipe_writer_block_count;
uint64_t pipe_end_of_file_observation_count;
uint64_t pipe_broken_observation_count;
uint64_t capacity_self_test_process_count;
uint64_t capacity_self_test_thread_count;
uint64_t capacity_self_test_threads_per_process;
bool process_runtime_initialized;
bool process_scheduling_active;

ProcessEntry capacity_process_entries[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ThreadEntry capacity_thread_entries[OS_KERNEL_THREAD_CAPACITY_LIMIT];
FxSaveArea capacity_thread_extended_states[OS_KERNEL_THREAD_CAPACITY_LIMIT];
uint64_t capacity_process_roots[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
bool capacity_stack_active[OS_KERNEL_THREAD_CAPACITY_LIMIT];

extern "C" void OsKernelEnterScheduledProcess(ExceptionFrame *frame) noexcept;
extern "C" [[noreturn]] void OsKernelReturnFromUserMode() noexcept;

[[nodiscard]] ProcessRuntimeLimits
SelectProcessRuntimeLimits(const uint64_t managed_memory_bytes) noexcept {
    if (managed_memory_bytes >= OS_KERNEL_PROCESS_RUNTIME_CAPACITY_MEMORY_BYTES) {
        return ProcessRuntimeLimits{
            .process_capacity = OS_KERNEL_PROCESS_CAPACITY_LIMIT,
            .thread_capacity = OS_KERNEL_THREAD_CAPACITY_LIMIT,
            .maximum_threads_per_process =
                OS_KERNEL_CAPACITY_THREADS_PER_PROCESS,
        };
    }
    if (managed_memory_bytes >= OS_KERNEL_PROCESS_RUNTIME_FUNCTIONAL_MEMORY_BYTES) {
        return ProcessRuntimeLimits{
            .process_capacity = OS_KERNEL_PROCESS_FUNCTIONAL_CAPACITY,
            .thread_capacity = OS_KERNEL_THREAD_FUNCTIONAL_CAPACITY,
            .maximum_threads_per_process =
                OS_KERNEL_FUNCTIONAL_THREADS_PER_PROCESS,
        };
    }
    return ProcessRuntimeLimits{
        .process_capacity = OS_KERNEL_PROCESS_BOOTSTRAP_CAPACITY,
        .thread_capacity = OS_KERNEL_THREAD_BOOTSTRAP_CAPACITY,
        .maximum_threads_per_process =
            OS_KERNEL_BOOTSTRAP_THREADS_PER_PROCESS,
    };
}

void ResetRuntimeStorage() noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY_LIMIT; ++process_index) {
        runtime_processes[process_index] = ProcessRuntimeProcess{};
    }
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < OS_KERNEL_THREAD_CAPACITY_LIMIT; ++thread_index) {
        runtime_threads[thread_index] = ProcessRuntimeThread{};
    }
}

[[nodiscard]] bool ReadThreadKernelStack(const uint64_t thread_index,
                                         KernelStack &stack) noexcept {
    ThreadEntry thread{};
    return thread_scheduler.ReadThread(thread_index, thread) ==
               ThreadSchedulerStatus::Succeeded &&
           thread.kernel_stack_slot_index < OS_KERNEL_THREAD_CAPACITY_LIMIT &&
           GetKernelStackManager().Read(thread.kernel_stack_slot_index, stack) ==
           KernelStackManagerStatus::Succeeded;
}

[[nodiscard]] bool BuildInitialContextFrame(
    const uint64_t kernel_stack_slot_index, const ProcessId process_id,
    const UserAddressSpace &address_space, ExceptionFrame *&saved_frame) noexcept {
    KernelStack stack{};
    if (GetKernelStackManager().Read(kernel_stack_slot_index, stack) !=
        KernelStackManagerStatus::Succeeded) {
        return false;
    }
    const uint64_t stack_top_address = KernelStackTopAddress(stack);
    if (stack_top_address < OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES) {
        return false;
    }
    const uint64_t frame_address = stack_top_address - OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES;
    if (!GetKernelStackManager().Contains(kernel_stack_slot_index, frame_address,
                                          OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES)) {
        return false;
    }

    UserPrivilegeFrame *const frame = reinterpret_cast<UserPrivilegeFrame *>(frame_address);
    *frame = UserPrivilegeFrame{};
    frame->common.register_rdi = process_id.value;
    frame->common.vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.instruction_pointer = address_space.entry_virtual_address;
    frame->common.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    frame->common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    frame->user_stack_pointer = address_space.stack_top_virtual_address;
    frame->user_stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    saved_frame = &frame->common;
    return true;
}

[[nodiscard]] bool CurrentFrameIsValid(const uint64_t thread_index,
                                       const ExceptionFrame &frame) noexcept {
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) !=
            ThreadSchedulerStatus::Succeeded ||
        thread.process_index >= process_runtime_limits.process_capacity ||
        !FrameOriginatedFromUser(frame) ||
        !GetKernelStackManager().Contains(
            thread.kernel_stack_slot_index, reinterpret_cast<uint64_t>(&frame),
            OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES)) {
        return false;
    }
    const ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
    return process.address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           ReadPageTableRoot() == process.address_space.root_physical_address;
}

[[nodiscard]] bool ActivateThread(const uint64_t thread_index) noexcept {
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) !=
            ThreadSchedulerStatus::Succeeded ||
        thread.process_index >= process_runtime_limits.process_capacity) {
        return false;
    }
    ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    KernelStack stack{};
    if (!process.active || !runtime_thread.active ||
        runtime_thread.saved_frame == nullptr ||
        !ReadThreadKernelStack(thread_index, stack) ||
        !GetKernelStackManager().Contains(
            thread.kernel_stack_slot_index,
            reinterpret_cast<uint64_t>(runtime_thread.saved_frame),
            OS_KERNEL_USER_PRIVILEGE_FRAME_SIZE_BYTES) ||
        !ActivateUserPageTable(process.address_space.root_physical_address)) {
        return false;
    }
    if (!SetPrivilegeStackPointer0(KernelStackTopAddress(stack))) {
        ActivateKernelPageTable();
        return false;
    }
    return RestoreFxState(runtime_thread.extended_state) ==
           ExtendedStateStatus::Succeeded;
}

[[nodiscard]] WaitQueue *SelectWaitQueue(const WaitCondition wait_condition) noexcept {
    if (wait_condition == WaitCondition::PipeReadable) {
        return &pipe_readable_wait_queue;
    }
    if (wait_condition == WaitCondition::PipeWritable) {
        return &pipe_writable_wait_queue;
    }
    if (wait_condition == WaitCondition::DescriptorReadable) {
        return &descriptor_readable_wait_queue;
    }
    if (wait_condition == WaitCondition::DescriptorWritable) {
        return &descriptor_writable_wait_queue;
    }
    return nullptr;
}

void WakeRequiredThreads(const WaitCondition wait_condition,
                         const WakeReason wake_reason) noexcept {
    uint64_t woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (WakeThreads(wait_condition, wake_reason,
                    OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT,
                    woken_thread_count) != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
}

[[nodiscard]] bool ReadCurrentThreadAndProcess(ThreadEntry &thread,
                                               ProcessEntry &process) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    return thread_index < process_runtime_limits.thread_capacity &&
           thread_scheduler.ReadThread(thread_index, thread) ==
               ThreadSchedulerStatus::Succeeded &&
           thread.process_index < process_runtime_limits.process_capacity &&
           thread_scheduler.ReadProcess(thread.process_index, process) ==
               ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] ProcessRuntimeProcess &CurrentRuntimeProcess() noexcept {
    ThreadEntry thread{};
    ProcessEntry process{};
    if (!ReadCurrentThreadAndProcess(thread, process)) {
        HaltProcessor();
    }
    return runtime_processes[thread.process_index];
}

void CloseProcessIoDescriptors(ProcessRuntimeProcess &process) noexcept {
    if (process_file_system == nullptr) {
        HaltProcessor();
    }
    for (uint64_t descriptor_index = OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR;
         descriptor_index < OS_KERNEL_IO_DESCRIPTOR_CAPACITY; ++descriptor_index) {
        IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
        if (process.descriptors.Lookup(descriptor_index, descriptor_kind) !=
            IoDescriptorStatus::Succeeded) {
            continue;
        }
        if ((descriptor_kind == IoDescriptorKind::RegularFile ||
             descriptor_kind == IoDescriptorKind::Directory) &&
            process_file_system->Close(process.file_handles[descriptor_index]) !=
                FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        if (descriptor_kind == IoDescriptorKind::PipeReader) {
            const PipeStatus close_status = process_pipe.CloseReader();
            if (close_status != PipeStatus::Succeeded &&
                close_status != PipeStatus::AlreadyClosed) {
                HaltProcessor();
            }
            WakeRequiredThreads(WaitCondition::DescriptorWritable,
                                WakeReason::ObjectClosed);
        }
        if (descriptor_kind == IoDescriptorKind::PipeWriter) {
            const PipeStatus close_status = process_pipe.CloseWriter();
            if (close_status != PipeStatus::Succeeded &&
                close_status != PipeStatus::AlreadyClosed) {
                HaltProcessor();
            }
            WakeRequiredThreads(WaitCondition::DescriptorReadable,
                                WakeReason::ObjectClosed);
        }
        IoDescriptorKind closed_kind = IoDescriptorKind::Closed;
        if (process.descriptors.Close(descriptor_index, closed_kind) !=
            IoDescriptorStatus::Succeeded) {
            HaltProcessor();
        }
    }
}

[[nodiscard]] bool ReapExitedThreadsAndZombieProcesses() noexcept {
    const uint64_t current_stack_pointer = ReadStackPointer();
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        ThreadEntry thread{};
        if (thread_scheduler.ReadThread(thread_index, thread) !=
                ThreadSchedulerStatus::Succeeded ||
            thread.state != ThreadState::Exited) {
            continue;
        }
        KernelStack stack{};
        const KernelStackManagerStatus read_status =
            GetKernelStackManager().Read(thread.kernel_stack_slot_index, stack);
        if (read_status == KernelStackManagerStatus::SlotNotActive) {
            return false;
        }
        if (read_status != KernelStackManagerStatus::Succeeded ||
            GetKernelStackManager().Contains(
                thread.kernel_stack_slot_index, current_stack_pointer,
                OS_KERNEL_PROCESS_RUNTIME_STACK_POINTER_PROBE_SIZE_BYTES) ||
            GetKernelStackManager().TryDestroy(thread.kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded) {
            return false;
        }
        if (thread.process_index >= process_runtime_limits.process_capacity) {
            return false;
        }
        ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
        process.result.run_tick_count = thread.run_tick_count;
        process.result.dispatch_count = thread.dispatch_count;
        runtime_threads[thread_index].saved_frame = nullptr;
        runtime_threads[thread_index].active = false;

        const bool interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus reap_thread_status =
            thread_scheduler.ReapExitedThread(thread_index);
        ProcessEntry process_entry{};
        const ThreadSchedulerStatus read_process_status =
            thread_scheduler.ReadProcess(thread.process_index, process_entry);
        ThreadSchedulerStatus reap_process_status =
            ThreadSchedulerStatus::Succeeded;
        if (read_process_status == ThreadSchedulerStatus::Succeeded &&
            process_entry.state == ProcessState::Zombie &&
            process_entry.thread_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            reap_process_status =
                thread_scheduler.ReapZombieProcess(thread.process_index);
            if (reap_process_status == ThreadSchedulerStatus::Succeeded) {
                process.active = false;
            }
        }
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (reap_thread_status != ThreadSchedulerStatus::Succeeded ||
            read_process_status != ThreadSchedulerStatus::Succeeded ||
            reap_process_status != ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CleanupCapacitySelfTestResources(
    const ProcessRuntimeLimits limits) noexcept {
    bool cleanup_succeeded = true;
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < limits.thread_capacity; ++thread_index) {
        if (!capacity_stack_active[thread_index]) {
            continue;
        }
        cleanup_succeeded =
            GetKernelStackManager().TryDestroy(thread_index) ==
                KernelStackManagerStatus::Succeeded &&
            cleanup_succeeded;
        capacity_stack_active[thread_index] = false;
    }
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < limits.process_capacity; ++process_index) {
        if (capacity_process_roots[process_index] ==
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            continue;
        }
        cleanup_succeeded =
            DestroyUserPageTable(capacity_process_roots[process_index]) ==
                KernelUserPageStatus::Succeeded &&
            cleanup_succeeded;
        capacity_process_roots[process_index] =
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }
    return cleanup_succeeded;
}

[[nodiscard]] bool RunProcessThreadCapacitySelfTest(
    const ProcessRuntimeLimits limits) noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY_LIMIT; ++process_index) {
        capacity_process_roots[process_index] =
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < OS_KERNEL_THREAD_CAPACITY_LIMIT; ++thread_index) {
        capacity_stack_active[thread_index] = false;
    }

    ResourceSnapshot before{};
    ResourceSnapshot active{};
    ResourceSnapshot after{};
    ResourceSnapshotDifference difference{};
    ThreadScheduler capacity_scheduler{};
    if (GetKernelResourceSnapshot(before) != ResourceSnapshotStatus::Succeeded ||
        capacity_scheduler.Initialize(
            capacity_process_entries, limits.process_capacity,
            capacity_thread_entries, limits.thread_capacity,
            limits.maximum_threads_per_process,
            OS_KERNEL_THREAD_DEFAULT_QUANTUM_TICKS) !=
            ThreadSchedulerStatus::Succeeded) {
        return false;
    }

    for (uint64_t process_ordinal = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_ordinal < limits.process_capacity; ++process_ordinal) {
        uint64_t root_physical_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
        ProcessId process_id{};
        if (CreateUserPageTable(root_physical_address) !=
            KernelUserPageStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        // 页表根一旦创建就先登记到回滚表；后续调度器校验失败也不能遗失本次资源。
        capacity_process_roots[process_ordinal] = root_physical_address;
        if (capacity_scheduler.CreateProcess(
                root_physical_address, process_index, process_id) !=
                ThreadSchedulerStatus::Succeeded ||
            process_index != process_ordinal ||
            process_id.value == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }

    uint64_t created_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
    while (created_thread_count < limits.thread_capacity) {
        if (created_thread_count < limits.maximum_threads_per_process) {
            process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
        } else if (created_thread_count <
                   limits.maximum_threads_per_process +
                       limits.process_capacity -
                       OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) {
            process_index =
                created_thread_count - limits.maximum_threads_per_process +
                OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
        } else {
            const uint64_t remaining_process_count =
                limits.process_capacity -
                OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
            process_index =
                OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT +
                (created_thread_count -
                 limits.maximum_threads_per_process -
                 remaining_process_count) %
                    remaining_process_count;
        }

        const uint64_t kernel_stack_slot_index = created_thread_count;
        const uint64_t user_stack_pointer =
            OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_BASE -
            created_thread_count *
                OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_STRIDE_BYTES;
        uint64_t thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
        ThreadId thread_id{};
        if (GetKernelStackManager().TryCreate(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        capacity_stack_active[kernel_stack_slot_index] = true;
        if (InitializeFxSaveArea(
                capacity_thread_extended_states[kernel_stack_slot_index]) !=
                ExtendedStateStatus::Succeeded ||
            capacity_scheduler.CreateThread(
                process_index, kernel_stack_slot_index, user_stack_pointer,
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, thread_index,
                thread_id) != ThreadSchedulerStatus::Succeeded ||
            thread_index >= limits.thread_capacity ||
            thread_id.value == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        created_thread_count += OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
    }

    const ThreadSchedulerStatistics active_statistics =
        capacity_scheduler.Statistics();
    const ResourceSnapshotSupplementalCounts active_supplemental_counts{
        .process_count = active_statistics.owned_process_count,
        .thread_count = active_statistics.owned_thread_count,
        .file_description_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .vnode_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .cache_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .block_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    if (active_statistics.owned_process_count != limits.process_capacity ||
        active_statistics.owned_thread_count != limits.thread_capacity ||
        GetKernelResourceSnapshot(active_supplemental_counts, active) !=
            ResourceSnapshotStatus::Succeeded ||
        active.process_count != limits.process_capacity ||
        active.thread_count != limits.thread_capacity ||
        active.kernel_stack_active_count != limits.thread_capacity) {
        static_cast<void>(CleanupCapacitySelfTestResources(limits));
        return false;
    }

    ThreadSchedulingDecision decision{};
    if (capacity_scheduler.Validate() != ThreadSchedulerStatus::Succeeded ||
        capacity_scheduler.Start(decision) !=
            ThreadSchedulerStatus::Succeeded) {
        static_cast<void>(CleanupCapacitySelfTestResources(limits));
        return false;
    }
    for (uint64_t terminated_thread_count =
             OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         terminated_thread_count < limits.thread_capacity;
         ++terminated_thread_count) {
        if (capacity_scheduler.TerminateCurrentThread(decision) !=
            ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }
    if (!decision.completed ||
        capacity_scheduler.Validate() != ThreadSchedulerStatus::Succeeded) {
        static_cast<void>(CleanupCapacitySelfTestResources(limits));
        return false;
    }

    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < limits.thread_capacity; ++thread_index) {
        if (GetKernelStackManager().TryDestroy(thread_index) !=
            KernelStackManagerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        capacity_stack_active[thread_index] = false;
        if (capacity_scheduler.ReapExitedThread(thread_index) !=
            ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }
    for (uint64_t capacity_process_index =
             OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         capacity_process_index < limits.process_capacity;
         ++capacity_process_index) {
        if (DestroyUserPageTable(
                capacity_process_roots[capacity_process_index]) !=
            KernelUserPageStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        capacity_process_roots[capacity_process_index] =
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (capacity_scheduler.ReapZombieProcess(capacity_process_index) !=
            ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }

    const ThreadSchedulerStatistics statistics =
        capacity_scheduler.Statistics();
    const ResourceSnapshotSupplementalCounts final_supplemental_counts{
        .process_count = statistics.owned_process_count,
        .thread_count = statistics.owned_thread_count,
        .file_description_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .vnode_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .cache_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .block_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    if (capacity_scheduler.Validate() != ThreadSchedulerStatus::Succeeded ||
        statistics.owned_process_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        statistics.owned_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        statistics.created_process_count != limits.process_capacity ||
        statistics.created_thread_count != limits.thread_capacity ||
        statistics.reaped_process_count != limits.process_capacity ||
        statistics.reaped_thread_count != limits.thread_capacity ||
        GetKernelResourceSnapshot(final_supplemental_counts, after) !=
            ResourceSnapshotStatus::Succeeded ||
        CompareResourceSnapshots(before, after, difference) !=
            ResourceSnapshotStatus::Succeeded ||
        difference.changed_fields_mask != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        difference.changed_field_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }

    capacity_self_test_process_count = limits.process_capacity;
    capacity_self_test_thread_count = limits.thread_capacity;
    capacity_self_test_threads_per_process =
        limits.maximum_threads_per_process;
    return true;
}

[[nodiscard]] ExceptionFrame *
CompleteCurrentThread(ExceptionFrame &frame,
                      const ProcessTerminationReason termination_reason,
                      const int64_t exit_code,
                      const uint64_t page_fault_address) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (!process_scheduling_active ||
        !CurrentFrameIsValid(thread_index, frame) ||
        thread_scheduler.ReadThread(thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.process_index >= process_runtime_limits.process_capacity) {
        HaltProcessor();
    }

    ProcessRuntimeProcess &process =
        runtime_processes[current_thread.process_index];
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    if (SaveFxState(runtime_thread.extended_state) !=
        ExtendedStateStatus::Succeeded) {
        HaltProcessor();
    }
    process.result.termination_reason = termination_reason;
    process.result.exit_code = exit_code;
    if (termination_reason == ProcessTerminationReason::Exception) {
        process.result.exception_vector = frame.vector;
        process.result.exception_error_code = frame.error_code;
        process.result.exception_instruction_pointer = frame.instruction_pointer;
        process.result.page_fault_address = page_fault_address;
    }
    CloseProcessIoDescriptors(process);

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status =
        thread_scheduler.TerminateCurrentThread(decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }

    ActivateKernelPageTable();
    if (DestroyUserAddressSpace(process.address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }

    if (decision.completed || !decision.switched) {
        if (decision.completed) {
            process_scheduling_active = false;
        }
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        OsKernelReturnFromUserMode();
    }
    if (!ActivateThread(decision.current_thread_index)) {
        HaltProcessor();
    }
    return runtime_threads[decision.current_thread_index].saved_frame;
}
}

ProcessRuntimeStatus InitializeProcessRuntime() noexcept {
    if (process_scheduling_active || process_runtime_initialized) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    if (!GetExtendedStateConfiguration().initialized) {
        return ProcessRuntimeStatus::ExtendedStateFailure;
    }
    process_runtime_limits = SelectProcessRuntimeLimits(
        GetKernelMemoryStatistics().managed_usable_memory_bytes);
    if (!RunProcessThreadCapacitySelfTest(process_runtime_limits)) {
        return ProcessRuntimeStatus::CapacitySelfTestFailure;
    }
    if (thread_scheduler.Initialize(
            process_entries, process_runtime_limits.process_capacity,
            thread_entries, process_runtime_limits.thread_capacity,
            process_runtime_limits.maximum_threads_per_process,
            OS_KERNEL_THREAD_DEFAULT_QUANTUM_TICKS) !=
        ThreadSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ResetRuntimeStorage();
    frames_before_processes = GetPhysicalFrameAllocatorStatistics();
    frames_after_processes = PhysicalFrameAllocatorStatistics{};
    virtual_addresses_before_processes = GetKernelVirtualAddressAllocator().Statistics();
    virtual_addresses_after_processes = KernelVirtualAddressAllocatorStatistics{};
    kernel_stacks_before_processes = GetKernelStackManager().Statistics();
    kernel_stacks_after_processes = KernelStackManagerStatistics{};
    resource_snapshot_before_processes = ResourceSnapshot{};
    resource_snapshot_after_processes = ResourceSnapshot{};
    resource_snapshot_difference = ResourceSnapshotDifference{};
    if (GetKernelStackManager().Validate() != KernelStackManagerStatus::Succeeded ||
        kernel_stacks_before_processes.active_stack_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        GetKernelResourceSnapshot(
            ResourceSnapshotSupplementalCounts{},
            resource_snapshot_before_processes) !=
            ResourceSnapshotStatus::Succeeded) {
        return ProcessRuntimeStatus::KernelStackFailure;
    }
    if (pipe_readable_wait_queue.Initialize(WaitQueueId{
            .value = OS_KERNEL_PROCESS_RUNTIME_PIPE_READABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        pipe_writable_wait_queue.Initialize(WaitQueueId{
            .value = OS_KERNEL_PROCESS_RUNTIME_PIPE_WRITABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        descriptor_readable_wait_queue.Initialize(WaitQueueId{
            .value =
                OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_READABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        descriptor_writable_wait_queue.Initialize(WaitQueueId{
            .value =
                OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_WRITABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    process_pipe.Initialize();
    process_console_input.Initialize();
    pipe_reader_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_writer_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_end_of_file_observation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_broken_observation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    process_file_system = nullptr;
    process_runtime_initialized = true;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus AttachProcessFileSystem(FileSystem &file_system) noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    process_file_system = &file_system;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus CreateProcess(const UserProgramSelection selection,
                                   ProcessCreationResult &creation_result,
                                   UserElfValidationStatus &elf_validation_status,
                                   UserAddressSpaceStatus &address_space_status) noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }

    if (thread_scheduler.Statistics().created_process_count >=
        OS_KERNEL_PROCESS_RUNTIME_RESULT_CAPACITY) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    const UserProgramImage image = SelectUserProgramImage(selection);
    UserAddressSpace address_space{};
    address_space_status = LoadUserAddressSpace(image.image, image.image_size_bytes, address_space,
                                                elf_validation_status);
    if (address_space_status != UserAddressSpaceStatus::Succeeded) {
        return address_space_status == UserAddressSpaceStatus::InvalidElf
                   ? ProcessRuntimeStatus::InvalidElf
                   : ProcessRuntimeStatus::AddressSpaceFailure;
    }

    uint64_t process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    ProcessId process_id{};
    bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus create_process_status =
        thread_scheduler.CreateProcess(address_space.root_physical_address,
                                       process_index, process_id);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (create_process_status != ThreadSchedulerStatus::Succeeded) {
        if (DestroyUserAddressSpace(address_space) !=
            UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    uint64_t kernel_stack_slot_index = OS_KERNEL_THREAD_INVALID_INDEX;
    for (uint64_t candidate_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         candidate_index < process_runtime_limits.thread_capacity;
         ++candidate_index) {
        KernelStack candidate_stack{};
        if (!runtime_threads[candidate_index].active &&
            GetKernelStackManager().Read(candidate_index, candidate_stack) ==
                KernelStackManagerStatus::SlotNotActive) {
            kernel_stack_slot_index = candidate_index;
            break;
        }
    }
    if (kernel_stack_slot_index == OS_KERNEL_THREAD_INVALID_INDEX ||
        GetKernelStackManager().TryCreate(kernel_stack_slot_index) !=
            KernelStackManagerStatus::Succeeded) {
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (discard_status != ThreadSchedulerStatus::Succeeded ||
            DestroyUserAddressSpace(address_space) !=
                UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::KernelStackFailure;
    }

    uint64_t thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    ThreadId thread_id{};
    interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus create_thread_status =
        thread_scheduler.CreateThread(
            process_index, kernel_stack_slot_index,
            address_space.stack_top_virtual_address,
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, thread_index, thread_id);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (create_thread_status != ThreadSchedulerStatus::Succeeded) {
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_process_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded ||
            discard_process_status != ThreadSchedulerStatus::Succeeded ||
            DestroyUserAddressSpace(address_space) !=
                UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    ExceptionFrame *saved_frame = nullptr;
    if (!BuildInitialContextFrame(kernel_stack_slot_index, process_id,
                                  address_space, saved_frame) ||
        InitializeFxSaveArea(runtime_threads[thread_index].extended_state) !=
            ExtendedStateStatus::Succeeded) {
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_thread_status =
            thread_scheduler.DiscardReadyThread(thread_index);
        const ThreadSchedulerStatus discard_process_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded ||
            GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded ||
            discard_thread_status != ThreadSchedulerStatus::Succeeded ||
            discard_process_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ContextFrameFailure;
    }

    runtime_processes[process_index] = ProcessRuntimeProcess{
        .address_space = address_space,
        .result =
            ProcessExecutionResult{
                .process_id = process_id.value,
                .selection = selection,
                .termination_reason = ProcessTerminationReason::None,
                .exit_code = OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
                .exception_vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .exception_error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .exception_instruction_pointer = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .page_fault_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .system_call_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .root_physical_address = address_space.root_physical_address,
                .mapped_page_count = address_space.mapped_page_count,
                .run_tick_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .dispatch_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .pipe_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .pipe_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .file_system_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .file_system_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .console_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                .console_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            },
        .descriptors = {},
        .file_handles = {},
        .active = true,
    };
    runtime_threads[thread_index].saved_frame = saved_frame;
    runtime_threads[thread_index].active = true;
    runtime_processes[process_index].descriptors.Initialize(
        selection == UserProgramSelection::IpcConsumer,
        selection == UserProgramSelection::IpcProducer);
    KernelStack kernel_stack{};
    if (!ReadThreadKernelStack(thread_index, kernel_stack)) {
        HaltProcessor();
    }
    creation_result = ProcessCreationResult{
        .process_id = process_id.value,
        .process_index = process_index,
        .thread_id = thread_id.value,
        .thread_index = thread_index,
        .root_physical_address = address_space.root_physical_address,
        .entry_virtual_address = address_space.entry_virtual_address,
        .mapped_page_count = address_space.mapped_page_count,
        .kernel_stack_lower_guard_address = KernelStackLowerGuardAddress(kernel_stack),
        .kernel_stack_top_address = KernelStackTopAddress(kernel_stack),
        .kernel_stack_upper_guard_address = KernelStackUpperGuardAddress(kernel_stack),
    };
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus ExecuteProcesses() noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }

    const bool interrupts_were_enabled = DisableInterrupts();
    process_scheduling_active = true;
    while (process_scheduling_active) {
        ThreadSchedulingDecision decision{};
        const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus scheduler_status =
            thread_scheduler.Start(decision);
        scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
        if (scheduler_status == ThreadSchedulerStatus::NoReadyThread) {
            if (decision.completed) {
                process_scheduling_active = false;
                break;
            }
            EnableInterruptsWaitAndDisable();
            continue;
        }
        if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::SchedulerFailure;
        }
        if (!ActivateThread(decision.current_thread_index)) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::PageTableActivationFailure;
        }
        OsKernelEnterScheduledProcess(
            runtime_threads[decision.current_thread_index].saved_frame);
        if (ReadPageTableRoot() != GetKernelPageTableRoot()) {
            HaltProcessor();
        }
        if (!ReapExitedThreadsAndZombieProcesses()) {
            process_scheduling_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::KernelStackFailure;
        }
    }
    frames_after_processes = GetPhysicalFrameAllocatorStatistics();
    virtual_addresses_after_processes = GetKernelVirtualAddressAllocator().Statistics();
    kernel_stacks_after_processes = GetKernelStackManager().Statistics();
    if (GetKernelStackManager().Validate() != KernelStackManagerStatus::Succeeded ||
        kernel_stacks_after_processes.active_stack_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::KernelStackFailure;
    }
    const ThreadSchedulerStatistics final_scheduler_statistics =
        thread_scheduler.Statistics();
    const ResourceSnapshotSupplementalCounts supplemental_counts{
        .process_count = final_scheduler_statistics.owned_process_count,
        .thread_count = final_scheduler_statistics.owned_thread_count,
        .file_description_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .vnode_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .cache_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .block_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    if (thread_scheduler.Validate() != ThreadSchedulerStatus::Succeeded ||
        GetKernelResourceSnapshot(supplemental_counts,
                                  resource_snapshot_after_processes) !=
            ResourceSnapshotStatus::Succeeded ||
        CompareResourceSnapshots(resource_snapshot_before_processes,
                                 resource_snapshot_after_processes,
                                 resource_snapshot_difference) !=
            ResourceSnapshotStatus::Succeeded ||
        resource_snapshot_difference.changed_fields_mask !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        resource_snapshot_difference.changed_field_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::ResourceLeakDetected;
    }
    RestoreInterrupts(interrupts_were_enabled);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatistics GetProcessRuntimeStatistics() noexcept {
    ProcessRuntimeStatistics statistics{
        .scheduler = thread_scheduler.Statistics(),
        .extended_state = GetExtendedStateConfiguration(),
        .configured_process_capacity = process_runtime_limits.process_capacity,
        .configured_thread_capacity = process_runtime_limits.thread_capacity,
        .configured_threads_per_process =
            process_runtime_limits.maximum_threads_per_process,
        .capacity_self_test_process_count = capacity_self_test_process_count,
        .capacity_self_test_thread_count = capacity_self_test_thread_count,
        .capacity_self_test_threads_per_process =
            capacity_self_test_threads_per_process,
        .frames_before_processes = frames_before_processes,
        .frames_after_processes = frames_after_processes,
        .virtual_addresses_before_processes = virtual_addresses_before_processes,
        .virtual_addresses_after_processes = virtual_addresses_after_processes,
        .kernel_stacks_before_processes = kernel_stacks_before_processes,
        .kernel_stacks_after_processes = kernel_stacks_after_processes,
        .resource_snapshot_before_processes = resource_snapshot_before_processes,
        .resource_snapshot_after_processes = resource_snapshot_after_processes,
        .resource_snapshot_difference = resource_snapshot_difference,
        .ipc =
            ProcessIpcStatistics{
                .pipe = process_pipe.Statistics(),
                .reader_block_count = pipe_reader_block_count,
                .writer_block_count = pipe_writer_block_count,
                .end_of_file_observation_count = pipe_end_of_file_observation_count,
                .broken_pipe_observation_count = pipe_broken_observation_count,
            },
        .console_input = process_console_input.Statistics(),
        .processes = {},
    };
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_RUNTIME_RESULT_CAPACITY;
         ++process_index) {
        statistics.processes[process_index] =
            runtime_processes[process_index].result;
    }
    return statistics;
}

bool IsProcessSchedulingActive() noexcept {
    return process_scheduling_active && thread_scheduler.IsActive();
}

uint64_t CurrentProcessId() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ThreadEntry thread{};
    ProcessEntry process{};
    if (!ReadCurrentThreadAndProcess(thread, process)) {
        HaltProcessor();
    }
    return process.process_id.value;
}

uint64_t CurrentThreadId() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ThreadId thread_id{};
    if (thread_scheduler.CurrentThreadId(thread_id) !=
        ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    return thread_id.value;
}

UserProgramSelection CurrentProcessSelection() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    return CurrentRuntimeProcess().result.selection;
}

void RecordCurrentProcessSystemCall() noexcept {
    if (!IsProcessSchedulingActive()) {
        HaltProcessor();
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    ++process.result.system_call_count;
}

bool CurrentProcessCanReadPipe() noexcept {
    return CurrentProcessSelection() == UserProgramSelection::IpcConsumer;
}

bool CurrentProcessCanWritePipe() noexcept {
    return CurrentProcessSelection() == UserProgramSelection::IpcProducer;
}

PipeStatus TryReadCurrentProcessPipe(uint8_t *destination, const uint64_t capacity_bytes,
                                     uint64_t &read_bytes) noexcept {
    if (!CurrentProcessCanReadPipe()) {
        read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = process_pipe.TryRead(destination, capacity_bytes, read_bytes);
    if (status == PipeStatus::Succeeded) {
        CurrentRuntimeProcess().result.pipe_bytes_read += read_bytes;
        WakeRequiredThreads(WaitCondition::DescriptorWritable,
                            WakeReason::ConditionSatisfied);
    } else if (status == PipeStatus::EndOfFile) {
        ++pipe_end_of_file_observation_count;
    }
    return status;
}

PipeStatus TryWriteCurrentProcessPipe(const uint8_t *source, const uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept {
    if (!CurrentProcessCanWritePipe()) {
        written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = process_pipe.TryWrite(source, length_bytes, written_bytes);
    if (status == PipeStatus::Succeeded) {
        CurrentRuntimeProcess().result.pipe_bytes_written += written_bytes;
        WakeRequiredThreads(WaitCondition::DescriptorReadable,
                            WakeReason::ConditionSatisfied);
    } else if (status == PipeStatus::BrokenPipe) {
        ++pipe_broken_observation_count;
    }
    return status;
}

PipeStatus CloseCurrentProcessPipeReader() noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = process_pipe.CloseReader();
    if (status == PipeStatus::Succeeded) {
        WakeRequiredThreads(WaitCondition::PipeWritable,
                            WakeReason::ObjectClosed);
        WakeRequiredThreads(WaitCondition::DescriptorWritable,
                            WakeReason::ObjectClosed);
    }
    return status;
}

PipeStatus CloseCurrentProcessPipeWriter() noexcept {
    if (!CurrentProcessCanWritePipe()) {
        return PipeStatus::InvalidArgument;
    }
    const PipeStatus status = process_pipe.CloseWriter();
    if (status == PipeStatus::Succeeded) {
        WakeRequiredThreads(WaitCondition::PipeReadable,
                            WakeReason::ObjectClosed);
        WakeRequiredThreads(WaitCondition::DescriptorReadable,
                            WakeReason::ObjectClosed);
    }
    return status;
}

FileSystemStatus OpenCurrentProcessFile(const uint8_t *path, const uint64_t path_length_bytes,
                                        const FileSystemOpenOptions &options,
                                        uint64_t &file_descriptor) noexcept {
    file_descriptor = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    FileSystemHandle handle{};
    const FileSystemStatus status =
        process_file_system->Open(path, path_length_bytes, options, handle);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    uint64_t available_descriptor = OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    if (process.descriptors.Allocate(IoDescriptorKind::RegularFile, available_descriptor) !=
        IoDescriptorStatus::Succeeded) {
        if (process_file_system->Close(handle) != FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    process.file_handles[available_descriptor] = handle;
    file_descriptor = available_descriptor;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus ReadCurrentProcessFile(const uint64_t file_descriptor, uint8_t *destination,
                                        const uint64_t capacity_bytes,
                                        uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (file_descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(file_descriptor, descriptor_kind) !=
            IoDescriptorStatus::Succeeded ||
        descriptor_kind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus status = process_file_system->Read(
        process.file_handles[file_descriptor], destination, capacity_bytes, read_bytes);
    if (status == FileSystemStatus::Succeeded) {
        process.result.file_system_bytes_read += read_bytes;
    }
    return status;
}

FileSystemStatus WriteCurrentProcessFile(const uint64_t file_descriptor, const uint8_t *source,
                                         const uint64_t length_bytes,
                                         uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (file_descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(file_descriptor, descriptor_kind) !=
            IoDescriptorStatus::Succeeded ||
        descriptor_kind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus status = process_file_system->Write(
        process.file_handles[file_descriptor], source, length_bytes, written_bytes);
    if (status == FileSystemStatus::Succeeded) {
        process.result.file_system_bytes_written += written_bytes;
    }
    return status;
}

FileSystemStatus CloseCurrentProcessFile(const uint64_t file_descriptor) noexcept {
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (file_descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(file_descriptor, descriptor_kind) !=
            IoDescriptorStatus::Succeeded ||
        descriptor_kind != IoDescriptorKind::RegularFile) {
        return FileSystemStatus::InvalidHandle;
    }
    const FileSystemStatus close_status =
        process_file_system->Close(process.file_handles[file_descriptor]);
    if (close_status != FileSystemStatus::Succeeded) {
        return close_status;
    }
    IoDescriptorKind closed_kind = IoDescriptorKind::Closed;
    return process.descriptors.Close(file_descriptor, closed_kind) == IoDescriptorStatus::Succeeded
               ? FileSystemStatus::Succeeded
               : FileSystemStatus::InvalidHandle;
}

FileSystemStatus CreateCurrentProcessDirectory(const uint8_t *path,
                                               const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    return process_file_system->CreateDirectory(path, path_length_bytes);
}

FileSystemStatus SyncCurrentProcessFileSystem() noexcept {
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    return process_file_system->Sync();
}

ProcessIoStatus TryReadCurrentProcessDescriptor(const uint64_t descriptor,
                                                uint8_t *const destination,
                                                const uint64_t capacity_bytes, uint64_t &read_bytes,
                                                FileSystemStatus &file_system_status) noexcept {
    read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    file_system_status = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptor_kind) != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor_kind == IoDescriptorKind::ConsoleInput) {
        const ConsoleInputStatus status =
            process_console_input.TryRead(destination, capacity_bytes, read_bytes);
        if (status == ConsoleInputStatus::Empty) {
            return ProcessIoStatus::WouldBlock;
        }
        if (status != ConsoleInputStatus::Succeeded) {
            return ProcessIoStatus::InvalidArgument;
        }
        process.result.console_bytes_read += read_bytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::RegularFile) {
        file_system_status = process_file_system->Read(process.file_handles[descriptor],
                                                       destination, capacity_bytes, read_bytes);
        if (file_system_status != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
        process.result.file_system_bytes_read += read_bytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::PipeReader) {
        const PipeStatus status = process_pipe.TryRead(destination, capacity_bytes, read_bytes);
        if (status == PipeStatus::Succeeded) {
            process.result.pipe_bytes_read += read_bytes;
            WakeRequiredThreads(WaitCondition::DescriptorWritable,
                                WakeReason::ConditionSatisfied);
        } else if (status == PipeStatus::EndOfFile) {
            ++pipe_end_of_file_observation_count;
        }
        return MapPipeIoStatus(status);
    }
    return ProcessIoStatus::PermissionDenied;
}

ProcessIoStatus TryWriteCurrentProcessDescriptor(const uint64_t descriptor,
                                                 const uint8_t *const source,
                                                 const uint64_t length_bytes,
                                                 uint64_t &written_bytes,
                                                 FileSystemStatus &file_system_status) noexcept {
    written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    file_system_status = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptor_kind) != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor_kind == IoDescriptorKind::ConsoleOutput ||
        descriptor_kind == IoDescriptorKind::ConsoleError) {
        const SerialPort serial_port{OS_KERNEL_SERIAL_COM1_BASE_PORT};
        while (written_bytes < length_bytes) {
            if (!serial_port.TryWriteByte(static_cast<char>(source[written_bytes]))) {
                return ProcessIoStatus::DeviceFailure;
            }
            ++written_bytes;
        }
        process.result.console_bytes_written += written_bytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::RegularFile) {
        file_system_status = process_file_system->Write(process.file_handles[descriptor], source,
                                                        length_bytes, written_bytes);
        if (file_system_status != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
        process.result.file_system_bytes_written += written_bytes;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::PipeWriter) {
        const PipeStatus status = process_pipe.TryWrite(source, length_bytes, written_bytes);
        if (status == PipeStatus::Succeeded) {
            process.result.pipe_bytes_written += written_bytes;
            WakeRequiredThreads(WaitCondition::DescriptorReadable,
                                WakeReason::ConditionSatisfied);
        } else if (status == PipeStatus::BrokenPipe) {
            ++pipe_broken_observation_count;
        }
        return MapPipeIoStatus(status);
    }
    return ProcessIoStatus::PermissionDenied;
}

ProcessIoStatus CloseCurrentProcessDescriptor(const uint64_t descriptor,
                                              FileSystemStatus &file_system_status) noexcept {
    file_system_status = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    const IoDescriptorStatus lookup_status =
        process.descriptors.Lookup(descriptor, descriptor_kind);
    if (lookup_status != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor < OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR) {
        return ProcessIoStatus::PermissionDenied;
    }
    if (descriptor_kind == IoDescriptorKind::RegularFile ||
        descriptor_kind == IoDescriptorKind::Directory) {
        file_system_status = process_file_system->Close(process.file_handles[descriptor]);
        if (file_system_status != FileSystemStatus::Succeeded) {
            return ProcessIoStatus::FileSystemFailure;
        }
    } else if (descriptor_kind == IoDescriptorKind::PipeReader) {
        const PipeStatus status = process_pipe.CloseReader();
        if (status != PipeStatus::Succeeded) {
            return MapPipeIoStatus(status);
        }
        WakeRequiredThreads(WaitCondition::DescriptorWritable,
                            WakeReason::ObjectClosed);
    } else if (descriptor_kind == IoDescriptorKind::PipeWriter) {
        const PipeStatus status = process_pipe.CloseWriter();
        if (status != PipeStatus::Succeeded) {
            return MapPipeIoStatus(status);
        }
        WakeRequiredThreads(WaitCondition::DescriptorReadable,
                            WakeReason::ObjectClosed);
    } else {
        return ProcessIoStatus::PermissionDenied;
    }
    IoDescriptorKind closed_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Close(descriptor, closed_kind) != IoDescriptorStatus::Succeeded) {
        HaltProcessor();
    }
    return ProcessIoStatus::Succeeded;
}

FileSystemStatus OpenCurrentProcessDirectory(const uint8_t *const path,
                                             const uint64_t path_length_bytes,
                                             uint64_t &file_descriptor) noexcept {
    file_descriptor = OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    FileSystemHandle handle{};
    FileSystemStatus status = process_file_system->OpenDirectory(path, path_length_bytes, handle);
    if (status != FileSystemStatus::Succeeded) {
        return status;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    if (process.descriptors.Allocate(IoDescriptorKind::Directory, file_descriptor) !=
        IoDescriptorStatus::Succeeded) {
        if (process_file_system->Close(handle) != FileSystemStatus::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    process.file_handles[file_descriptor] = handle;
    return FileSystemStatus::Succeeded;
}

FileSystemStatus ReadCurrentProcessDirectory(const uint64_t file_descriptor,
                                             FileSystemDirectoryEntry &entry,
                                             bool &end_of_directory) noexcept {
    entry = FileSystemDirectoryEntry{};
    end_of_directory = false;
    if (!IsProcessSchedulingActive() || process_file_system == nullptr ||
        file_descriptor >= OS_KERNEL_IO_DESCRIPTOR_CAPACITY) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(file_descriptor, descriptor_kind) !=
            IoDescriptorStatus::Succeeded ||
        descriptor_kind != IoDescriptorKind::Directory) {
        return FileSystemStatus::InvalidHandle;
    }
    return process_file_system->ReadDirectory(process.file_handles[file_descriptor], entry,
                                              end_of_directory);
}

ProcessIoStatus CurrentProcessDescriptorReadCanProgress(const uint64_t descriptor,
                                                        bool &can_progress) noexcept {
    can_progress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptor_kind) != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor_kind == IoDescriptorKind::ConsoleInput) {
        can_progress = process_console_input.ReadCanProgress();
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::RegularFile ||
        descriptor_kind == IoDescriptorKind::Directory) {
        can_progress = true;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::PipeReader) {
        can_progress = process_pipe.ReadCanProgress();
        if (!can_progress) {
            ++pipe_reader_block_count;
        }
        return ProcessIoStatus::Succeeded;
    }
    return ProcessIoStatus::PermissionDenied;
}

ProcessIoStatus CurrentProcessDescriptorWriteCanProgress(const uint64_t descriptor,
                                                         bool &can_progress) noexcept {
    can_progress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    IoDescriptorKind descriptor_kind = IoDescriptorKind::Closed;
    if (process.descriptors.Lookup(descriptor, descriptor_kind) != IoDescriptorStatus::Succeeded) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (descriptor_kind == IoDescriptorKind::ConsoleOutput ||
        descriptor_kind == IoDescriptorKind::ConsoleError ||
        descriptor_kind == IoDescriptorKind::RegularFile) {
        can_progress = true;
        return ProcessIoStatus::Succeeded;
    }
    if (descriptor_kind == IoDescriptorKind::PipeWriter) {
        can_progress = process_pipe.WriteCanProgress();
        if (!can_progress) {
            ++pipe_writer_block_count;
        }
        return ProcessIoStatus::Succeeded;
    }
    return ProcessIoStatus::PermissionDenied;
}

void SubmitConsoleCharacter(const uint8_t character) noexcept {
    const ConsoleInputStatus submit_status = process_console_input.Submit(character);
    if (submit_status == ConsoleInputStatus::Succeeded && process_scheduling_active) {
        WakeRequiredThreads(WaitCondition::DescriptorReadable,
                            WakeReason::ConditionSatisfied);
    }
}

bool ProcessPipeReadCanProgress() noexcept { return process_pipe.ReadCanProgress(); }

bool ProcessPipeWriteCanProgress() noexcept { return process_pipe.WriteCanProgress(); }

ProcessRuntimeStatus BlockCurrentThread(ExceptionFrame &frame,
                                        const WaitCondition wait_condition,
                                        ExceptionFrame *&resume_frame) noexcept {
    resume_frame = &frame;
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    WaitQueue *const wait_queue = SelectWaitQueue(wait_condition);
    if (!IsProcessSchedulingActive() || wait_queue == nullptr ||
        !CurrentFrameIsValid(thread_index, frame)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    if (SaveFxState(runtime_thread.extended_state) !=
        ExtendedStateStatus::Succeeded) {
        return ProcessRuntimeStatus::ExtendedStateFailure;
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus status = thread_scheduler.BlockCurrentThread(
        *wait_queue, wait_condition, decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (status != ThreadSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (wait_condition == WaitCondition::PipeReadable) {
        ++pipe_reader_block_count;
    } else if (wait_condition == WaitCondition::PipeWritable) {
        ++pipe_writer_block_count;
    }
    if (!decision.switched) {
        ActivateKernelPageTable();
        if (!SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0())) {
            HaltProcessor();
        }
        OsKernelReturnFromUserMode();
    }
    if (!ActivateThread(decision.current_thread_index)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    resume_frame = runtime_threads[decision.current_thread_index].saved_frame;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus WakeThreads(const WaitCondition wait_condition,
                                 const WakeReason wake_reason,
                                 const uint64_t maximum_wake_count,
                                 uint64_t &woken_thread_count) noexcept {
    woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    WaitQueue *const wait_queue = SelectWaitQueue(wait_condition);
    if (!process_runtime_initialized || !process_scheduling_active ||
        wait_queue == nullptr) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus status = thread_scheduler.WakeMany(
        *wait_queue, wake_reason, maximum_wake_count, woken_thread_count);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return status == ThreadSchedulerStatus::Succeeded
               ? ProcessRuntimeStatus::Succeeded
               : ProcessRuntimeStatus::SchedulerFailure;
}

ExceptionFrame *HandleProcessTimerInterrupt(ExceptionFrame &frame) noexcept {
    if (!process_scheduling_active || !FrameOriginatedFromUser(frame)) {
        return &frame;
    }
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (!CurrentFrameIsValid(thread_index, frame)) {
        HaltProcessor();
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    if (SaveFxState(runtime_thread.extended_state) !=
        ExtendedStateStatus::Succeeded) {
        HaltProcessor();
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status =
        thread_scheduler.HandleTimerTick(decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (!decision.switched) {
        return &frame;
    }
    if (!ActivateThread(decision.current_thread_index)) {
        HaltProcessor();
    }
    return runtime_threads[decision.current_thread_index].saved_frame;
}

ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                const int64_t exit_code) noexcept {
    return CompleteCurrentThread(frame, ProcessTerminationReason::Exited,
                                 exit_code,
                                 OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}

ExceptionFrame *TerminateCurrentProcessFromException(ExceptionFrame &frame,
                                                     const uint64_t page_fault_address) noexcept {
    return CompleteCurrentThread(
        frame, ProcessTerminationReason::Exception,
        OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE, page_fault_address);
}
}
