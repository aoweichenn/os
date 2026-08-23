#include <os/kernel/process/process_runtime.hpp>

#include <os/kernel/arch/cpu_local.hpp>
#include <os/kernel/arch/descriptor_tables.hpp>
#include <os/kernel/arch/interrupt_runtime.hpp>
#include <os/kernel/arch/native_system_call.hpp>
#include <os/kernel/arch/processor.hpp>
#include <os/kernel/arch/user_context.hpp>
#include <os/kernel/core/freestanding_memory.hpp>
#include <os/kernel/device/port_io.hpp>
#include <os/kernel/device/vga_text_console.hpp>
#include <os/kernel/fs/legacy_file_system.hpp>
#include <os/kernel/fs/root_file_system_format.hpp>
#include <os/kernel/memory/memory_manager.hpp>
#include <os/kernel/process/block_io.hpp>
#include <os/kernel/process/block_io_device.hpp>
#include <os/kernel/process/file_page_load.hpp>
#include <os/kernel/process/program_arguments.hpp>
#include <os/kernel/sync/runtime_mutex.hpp>
#include <os/kernel/sync/spin_lock.hpp>

namespace os::kernel {

[[nodiscard]] static bool TryRecoverFromOutOfMemory(uint64_t current_process_index,
                                                    bool allow_current_victim) noexcept;

namespace {

constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BOOLEAN_TRUE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INVALID_RETURN_VECTOR = 13ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_NORMALIZED_ERROR_CODE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS = 0x202ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INITIAL_KERNEL_FLAGS = 0x202ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PAGE_MASK =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INIT_PROCESS_ID = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT = OS_KERNEL_THREAD_CAPACITY_LIMIT;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_STACK_POINTER_PROBE_SIZE_BYTES = sizeof(uint64_t);
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FUNCTIONAL_MEMORY_BYTES = 256ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_MEMORY_BYTES =
    32ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_BASE = 0x00007FFFFFF00000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_STRIDE_BYTES =
    0x0000000000001000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PIPE_READABLE_QUEUE_ID = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PIPE_WRITABLE_QUEUE_ID = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_READABLE_QUEUE_ID = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_WRITABLE_QUEUE_ID = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CHILD_EXIT_QUEUE_ID = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_THREAD_JOIN_QUEUE_ID = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SLEEP_QUEUE_ID = 7ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_QUEUE_ID = 8ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_TEST_QUEUE_ID = 9ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_WORK_QUEUE_ID = 10ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_COMPLETION_QUEUE_ID = 11ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PROCESS_IO_DRAIN_QUEUE_ID = 12ULL;
constexpr uint32_t OS_KERNEL_PROCESS_RUNTIME_IA32_GS_BASE_MSR = 0xC0000101U;
constexpr uint32_t OS_KERNEL_PROCESS_RUNTIME_IA32_KERNEL_GS_BASE_MSR = 0xC0000102U;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_LAUNCH_WAIT_QUEUE_ID = 0x8000000000000106ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_CAPACITY = 64ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_DEVICE_CAPACITY = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_CAPACITY = 64ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_FEEDBACK_CAPACITY = 4096ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FILE_PAGE_LOAD_WAIT_QUEUE_BASE = 0x8000000000020000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FILE_PAGE_WRITEBACK_WAIT_QUEUE_BASE =
    0x8000000000030000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_PROBE_TIMEOUT_NANOSECONDS =
    5ULL * 1000ULL * 1000ULL * 1000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_REGISTER_SLOT_COUNT = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_FLAGS_SLOT = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_ENTRY_SLOT = 7ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SENTINEL_SLOT = 8ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SLOT_COUNT = 9ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_CONTROL_SLOT_COUNT = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SIZE_BYTES =
    OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SLOT_COUNT * sizeof(uint64_t);
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_SAVED_CONTEXT_SIZE_BYTES =
    (OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_REGISTER_SLOT_COUNT +
     OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_CONTROL_SLOT_COUNT) *
    sizeof(uint64_t);
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_COUNT = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_SECOND_STEP = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_THIRD_STEP = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_FOURTH_STEP = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_FINAL_STEP = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_CAPACITY = 8ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_REGISTERED_COUNT = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_EXECUTION_COUNT = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_IMMEDIATE_COUNT = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_DELAYED_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_PEAK_PENDING_COUNT = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_RESET_COUNT = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_INITIAL_TIME_NANOSECONDS = 100ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_DELAY_TIME_NANOSECONDS = 200ULL;
// Linux 默认 dirty_writeback_centisecs 为 500，即 5 秒；低水位脏页沿用这一老化窗口。
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FILE_WRITEBACK_AGE_NANOSECONDS =
    5ULL * 1000ULL * 1000ULL * 1000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_INTERVAL_NANOSECONDS =
    1ULL * 1000ULL * 1000ULL * 1000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BACKGROUND_RECLAIM_BATCH_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS =
    1ULL * 1000ULL * 1000ULL * 1000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FUNCTIONAL_CAPACITY = 4096ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FUNCTIONAL_HASH_CAPACITY = 8192ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_CAPACITY_CAPACITY =
    OS_KERNEL_PAGE_AGING_CAPACITY_LIMIT;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_CAPACITY_HASH_CAPACITY =
    OS_KERNEL_PAGE_AGING_HASH_CAPACITY_LIMIT;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FAILURE_INDEX = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_THIRD_INDEX = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_DELAYED_INDEX = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_CANCELLED_INDEX = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_IDLE_INDEX = 5ULL;
static_assert(OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SIZE_BYTES <= OS_KERNEL_STACK_SIZE_BYTES);
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_PRIVATE_FUTEX_FIRST_QUEUE_ID = 0x1000ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_THREAD_STACK_ALIGNMENT_BYTES =
    os::abi::OS_ABI_THREAD_STACK_ALIGNMENT_BYTES;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_THREAD_LOCAL_STORAGE_PROBE_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SYSTEM_CALL_INSTRUCTION_SIZE_BYTES = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SIGNAL_STACK_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SIGNAL_RETURN_SLOT_SIZE_BYTES = sizeof(uint64_t);
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_STATE_STAGE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_STACK_POINTER_STAGE = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_STACK_BOUNDS_STAGE = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_MEMORY_STAGE = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_FRAME_STAGE = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_MASK_STAGE = 6ULL;
constexpr int64_t OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE = 0LL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_BOOTSTRAP_FILE_DESCRIPTOR_LIMIT = 64ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR = UINT64_MAX;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_ARGUMENT_COPY_CHUNK_SIZE_BYTES = 256ULL;
constexpr uint8_t OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR = 0U;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX = UINT64_MAX;
constexpr char OS_KERNEL_PROCESS_RUNTIME_SPAWN_PREFIX[] = "[OS][KERNEL][PROC] SPAWN_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_EXEC_PREFIX[] = "[OS][KERNEL][PROC] EXEC_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FORK_PREFIX[] = "[OS][KERNEL][PROC] FORK_CHILD_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX[] =
    "[OS][KERNEL][PROC] FORK_FAILURE_STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STATUS_PREFIX[] =
    "[OS][KERNEL][PROC] FORK_FAILURE_STATUS=";
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FORK_ADDRESS_SPACE_STAGE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FORK_PROCESS_STAGE = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FORK_STACK_STAGE = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FORK_THREAD_STAGE = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FORK_CONTEXT_STAGE = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FORK_FILE_SYSTEM_STAGE = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_FORK_PROCESS_TREE_STAGE = 7ULL;
constexpr char OS_KERNEL_PROCESS_RUNTIME_EXIT_PREFIX[] = "[OS][KERNEL][PROC] EXIT_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_THREAD_CREATE_COUNT_PREFIX[] =
    "[OS][KERNEL][THREAD] CREATE_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_THREAD_EXIT_COUNT_PREFIX[] =
    "[OS][KERNEL][THREAD] EXIT_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_THREAD_JOIN_COUNT_PREFIX[] =
    "[OS][KERNEL][THREAD] JOIN_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FUTEX_WAIT_COUNT_PREFIX[] =
    "[OS][KERNEL][FUTEX] WAIT_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FUTEX_WAKE_COUNT_PREFIX[] =
    "[OS][KERNEL][FUTEX] WAKE_OPERATION_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_REPARENT_PREFIX[] =
    "[OS][KERNEL][PROC] REPARENTED_CHILDREN=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_WAIT_PREFIX[] = "[OS][KERNEL][PROC] WAIT_REAPED_PID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_VM_DEMAND_FAULT_PREFIX[] =
    "[OS][KERNEL][VM] DEMAND_FAULT_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_VM_STACK_GROWTH_PREFIX[] =
    "[OS][KERNEL][VM] STACK_GROWTH_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_VM_FILE_FAULT_PREFIX[] =
    "[OS][KERNEL][VM] FILE_FAULT_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_VM_PAGE_CACHE_HIT_PREFIX[] =
    "[OS][KERNEL][VM] PAGE_CACHE_HIT_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_IO_DESCRIPTOR_READ_BLOCK_PREFIX[] =
    "[OS][KERNEL][IO] DESCRIPTOR_READ_BLOCK_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_COMPLETION_RESULT_PREFIX[] =
    "[OS][KERNEL][BLOCK] COMPLETION_RESULT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_COMPLETION_TIME_PREFIX[] =
    "[OS][KERNEL][BLOCK] COMPLETION_TIME_NS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_OTHER_THREAD_PROGRESS_PREFIX[] =
    "[OS][KERNEL][BLOCK] OTHER_THREAD_PROGRESS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_ABANDONED_REQUEST_PREFIX[] =
    "[OS][KERNEL][BLOCK] ABANDONED_REQUEST=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FILE_CACHE_WRITEBACK_STATUS_PREFIX[] =
    "[OS][KERNEL][CACHE] WRITEBACK_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FILE_CACHE_WRITTEN_PAGE_COUNT_PREFIX[] =
    "[OS][KERNEL][CACHE] WRITTEN_PAGE_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FILE_SYSTEM_SYNC_STATUS_PREFIX[] =
    "[OS][KERNEL][FS] SYNC_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_STAGE_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_STATUS_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_PHYSICAL_ADDRESS_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] PHYSICAL_ADDRESS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_EXISTING_KIND_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] EXISTING_KIND=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_OBSERVED_KIND_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] OBSERVED_KIND=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_PROCESS_ID_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] PROCESS_ID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_VIRTUAL_ADDRESS_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] VIRTUAL_ADDRESS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_AREA_KIND_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] AREA_KIND=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_COPY_ON_WRITE_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] COPY_ON_WRITE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FORGET_FAILURE_STATUS_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] FORGET_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FORGET_FAILURE_ADDRESS_PREFIX[] =
    "[OS][KERNEL][AGING][FAIL] FORGET_PHYSICAL_ADDRESS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_FAILURE_STAGE_PREFIX[] =
    "[OS][KERNEL][READAHEAD][FAIL] STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_STATUS_PREFIX[] =
    "[OS][KERNEL][READAHEAD][FAIL] REQUEST_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_PREFETCH_STATUS_PREFIX[] =
    "[OS][KERNEL][READAHEAD][FAIL] PREFETCH_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_CLOSE_STATUS_PREFIX[] =
    "[OS][KERNEL][READAHEAD][FAIL] CLOSE_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_COMPLETION_STATUS_PREFIX[] =
    "[OS][KERNEL][READAHEAD][FAIL] COMPLETION_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_PROCESS_ID_PREFIX[] =
    "[OS][KERNEL][FATAL] EXIT_PROCESS_ID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_PROCESS_TREE_STATUS_PREFIX[] =
    "[OS][KERNEL][FATAL] PROCESS_TREE_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_TERMINATION_REASON_PREFIX[] =
    "[OS][KERNEL][FATAL] TERMINATION_REASON=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_TERMINATION_CODE_PREFIX[] =
    "[OS][KERNEL][FATAL] TERMINATION_CODE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX[] =
    "[OS][KERNEL][FATAL] EXIT_STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STATUS_PREFIX[] =
    "[OS][KERNEL][FATAL] EXIT_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_ADDRESS_SPACE_STAGE_PREFIX[] =
    "[OS][KERNEL][FATAL] ADDRESS_SPACE_STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_ADDRESS_SPACE_VIRTUAL_ADDRESS_PREFIX[] =
    "[OS][KERNEL][FATAL] ADDRESS_SPACE_VIRTUAL_ADDRESS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_ADDRESS_SPACE_PHYSICAL_ADDRESS_PREFIX[] =
    "[OS][KERNEL][FATAL] ADDRESS_SPACE_PHYSICAL_ADDRESS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_FATAL_ADDRESS_SPACE_DETAIL_STATUS_PREFIX[] =
    "[OS][KERNEL][FATAL] ADDRESS_SPACE_DETAIL_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_USER_CONTINUATION_ACTIVATION_STAGE_PREFIX[] =
    "[OS][KERNEL][PROC] USER_CONTINUATION_ACTIVATION_STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_USER_THREAD_ACTIVATION_STAGE_PREFIX[] =
    "[OS][KERNEL][PROC] USER_THREAD_ACTIVATION_STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_USER_THREAD_ACTIVATION_INDEX_PREFIX[] =
    "[OS][KERNEL][PROC] USER_THREAD_ACTIVATION_INDEX=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_USER_CONTINUATION_STACK_POINTER_PREFIX[] =
    "[OS][KERNEL][PROC] USER_CONTINUATION_STACK_POINTER=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_USER_CONTINUATION_STACK_BEGIN_PREFIX[] =
    "[OS][KERNEL][PROC] USER_CONTINUATION_STACK_BEGIN=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_USER_CONTINUATION_STACK_TOP_PREFIX[] =
    "[OS][KERNEL][PROC] USER_CONTINUATION_STACK_TOP=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_STAGE_PREFIX[] =
    "[OS][KERNEL][PROC] RESOURCE_VALIDATION_STAGE=";
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_MEMORY_STAGE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_GLOBAL_STAGE = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_SCHEDULER_DETAIL = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_TREE_DETAIL = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_JOB_DETAIL = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_TERMINAL_DETAIL = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_SIGNAL_DETAIL = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_SNAPSHOT_DETAIL = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_DISCOUNT_DETAIL = 7ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_COMPARE_DETAIL = 8ULL;
constexpr char OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_DETAIL_PREFIX[] =
    "[OS][KERNEL][PROC] RESOURCE_VALIDATION_DETAIL=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_DIFFERENCE_PREFIX[] =
    "[OS][KERNEL][PROC] RESOURCE_VALIDATION_DIFFERENCE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_JOB_VALIDATION_STATUS_PREFIX[] =
    "[OS][KERNEL][JOB] VALIDATION_STATUS=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_PROCESS_COUNT_PREFIX[] =
    "[OS][KERNEL][JOB] VALIDATION_ACTIVE_PROCESSES=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_PROCESS_INDEX_PREFIX[] =
    "[OS][KERNEL][JOB] ACTIVE_PROCESS_INDEX=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_PROCESS_ID_PREFIX[] =
    "[OS][KERNEL][JOB] ACTIVE_PROCESS_ID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_PROCESS_GROUP_ID_PREFIX[] =
    "[OS][KERNEL][JOB] ACTIVE_PROCESS_GROUP_ID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_SESSION_ID_PREFIX[] =
    "[OS][KERNEL][JOB] ACTIVE_SESSION_ID=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_CLEANUP_STAGE_PREFIX[] =
    "[OS][KERNEL][PROC] WAIT_EVENT_CLEANUP_STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_CLEANUP_STATUS_PREFIX[] =
    "[OS][KERNEL][PROC] WAIT_EVENT_CLEANUP_STATUS=";
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_SCHEDULER_STAGE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_SIGNAL_STAGE = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_JOB_STAGE = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_INDEX_STAGE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_THREAD_STAGE = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_METADATA_STAGE = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_STACK_STAGE = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_STACK_RANGE_STAGE = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_ACTIVATE_STAGE = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_THREAD_STAGE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_PREEMPTION_STAGE = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_CONTEXT_STAGE = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_RUNTIME_STAGE = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_FRAME_STAGE = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_STACK_READ_STAGE = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_STACK_RANGE_STAGE = 7ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_PAGE_TABLE_STAGE = 8ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_TSS_STAGE = 9ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_CPU_LOCAL_STAGE = 10ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_EXTENDED_STATE_STAGE = 11ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_TLS_STAGE = 12ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_EXTENDED_STATE = 1ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_FUTEX = 2ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_WRITEBACK = 3ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_FILE_SYSTEM_CONTEXT = 4ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_SCHEDULER = 5ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_SIGNAL = 6ULL;
constexpr uint64_t OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_ADDRESS_SPACE = 7ULL;
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_QUEUED_PREFIX[] =
    "[OS][KERNEL][SIGNAL] QUEUED_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERED_PREFIX[] =
    "[OS][KERNEL][SIGNAL] HANDLER_DELIVERY_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_TERMINATE_PREFIX[] =
    "[OS][KERNEL][SIGNAL] TERMINATE_NUMBER=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_REJECTED_FRAME_PREFIX[] =
    "[OS][KERNEL][SIGNAL] REJECTED_FRAME_COUNT=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_ACTION_NUMBER_PREFIX[] =
    "[OS][KERNEL][SIGNAL] ACTION_NUMBER=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_ACTION_DISPOSITION_PREFIX[] =
    "[OS][KERNEL][SIGNAL] ACTION_DISPOSITION=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_DISPOSITION_PREFIX[] =
    "[OS][KERNEL][SIGNAL] DELIVERY_DISPOSITION=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_FAILURE_STAGE_PREFIX[] =
    "[OS][KERNEL][SIGNAL] DELIVERY_FAILURE_STAGE=";
constexpr char OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DEFAULT_CONTINUE_DELIVERED[] =
    "[OS][KERNEL][SIGNAL] DEFAULT_CONTINUE_DELIVERED\r\n";
constexpr uint8_t OS_KERNEL_PROCESS_RUNTIME_TERMINAL_NEWLINE_SEQUENCE[]{static_cast<uint8_t>('\r'),
                                                                        static_cast<uint8_t>('\n')};
constexpr uint8_t OS_KERNEL_PROCESS_RUNTIME_TERMINAL_ERASE_SEQUENCE[]{
    static_cast<uint8_t>('\b'), static_cast<uint8_t>(' '), static_cast<uint8_t>('\b')};
constexpr uint8_t OS_KERNEL_PROCESS_RUNTIME_TERMINAL_INTERRUPT_SEQUENCE[]{
    static_cast<uint8_t>('^'), static_cast<uint8_t>('C'), static_cast<uint8_t>('\r'),
    static_cast<uint8_t>('\n')};
constexpr uint8_t OS_KERNEL_PROCESS_RUNTIME_TERMINAL_STOP_SEQUENCE[]{
    static_cast<uint8_t>('^'), static_cast<uint8_t>('Z'), static_cast<uint8_t>('\r'),
    static_cast<uint8_t>('\n')};

enum class FileReadaheadWorkerFailureStage : uint64_t {
    AcquireRequest = 1ULL,
    Prefetch = 2ULL,
    CloseFile = 3ULL,
    CompleteRequest = 4ULL,
};

[[nodiscard]] bool IsPowerOfTwoCounter(const uint64_t value) noexcept {
    return value != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           (value & (value - OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT)) ==
               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
}

[[nodiscard]] ProcessIoStatus
MapFileDescriptionStatus(const FileDescriptionStatus status) noexcept {
    if (status == FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::Succeeded;
    }
    if (status == FileDescriptionStatus::WouldBlock) {
        return ProcessIoStatus::WouldBlock;
    }
    if (status == FileDescriptionStatus::EndOfFile) {
        return ProcessIoStatus::EndOfFile;
    }
    if (status == FileDescriptionStatus::BrokenPipe) {
        return ProcessIoStatus::BrokenPipe;
    }
    if (status == FileDescriptionStatus::InvalidReference) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (status == FileDescriptionStatus::PermissionDenied) {
        return ProcessIoStatus::PermissionDenied;
    }
    if (status == FileDescriptionStatus::DeviceFailure) {
        return ProcessIoStatus::DeviceFailure;
    }
    if (status == FileDescriptionStatus::FileSystemFailure) {
        return ProcessIoStatus::FileSystemFailure;
    }
    if (status == FileDescriptionStatus::ObjectFailure) {
        return ProcessIoStatus::ObjectFailure;
    }
    return ProcessIoStatus::InvalidArgument;
}

[[nodiscard]] ProcessIoStatus MapFileTableStatus(const FileTableStatus status) noexcept {
    if (status == FileTableStatus::Succeeded) {
        return ProcessIoStatus::Succeeded;
    }
    if (status == FileTableStatus::InvalidDescriptor) {
        return ProcessIoStatus::InvalidDescriptor;
    }
    if (status == FileTableStatus::SoftLimitExceeded ||
        status == FileTableStatus::AllocationFailed) {
        return ProcessIoStatus::DescriptorLimitExceeded;
    }
    if (status == FileTableStatus::InvalidFlags || status == FileTableStatus::InvalidLimit) {
        return ProcessIoStatus::InvalidArgument;
    }
    return ProcessIoStatus::ObjectFailure;
}

[[nodiscard]] PipeStatus MapProcessIoToPipeStatus(const ProcessIoStatus status) noexcept {
    if (status == ProcessIoStatus::Succeeded) {
        return PipeStatus::Succeeded;
    }
    if (status == ProcessIoStatus::WouldBlock) {
        return PipeStatus::WouldBlock;
    }
    if (status == ProcessIoStatus::EndOfFile) {
        return PipeStatus::EndOfFile;
    }
    if (status == ProcessIoStatus::BrokenPipe) {
        return PipeStatus::BrokenPipe;
    }
    if (status == ProcessIoStatus::InvalidDescriptor) {
        return PipeStatus::AlreadyClosed;
    }
    return PipeStatus::InvalidArgument;
}

[[nodiscard]] FileSystemStatus
MapProcessIoToFileSystemStatus(const ProcessIoStatus status,
                               const FileSystemStatus file_system_status) noexcept {
    if (status == ProcessIoStatus::Succeeded) {
        return FileSystemStatus::Succeeded;
    }
    if (status == ProcessIoStatus::InvalidDescriptor) {
        return FileSystemStatus::InvalidHandle;
    }
    if (status == ProcessIoStatus::PermissionDenied) {
        return FileSystemStatus::PermissionDenied;
    }
    if (status == ProcessIoStatus::FileSystemFailure) {
        return file_system_status;
    }
    if (status == ProcessIoStatus::DeviceFailure) {
        return FileSystemStatus::DeviceFailure;
    }
    if (status == ProcessIoStatus::WouldBlock) {
        return FileSystemStatus::WouldBlock;
    }
    if (status == ProcessIoStatus::BackgroundTerminalRead) {
        return FileSystemStatus::BackgroundTerminalRead;
    }
    if (status == ProcessIoStatus::DescriptorLimitExceeded ||
        status == ProcessIoStatus::ObjectFailure) {
        return FileSystemStatus::DataCapacityExhausted;
    }
    return FileSystemStatus::InvalidArgument;
}

struct ProcessRuntimeLimits final {
    uint64_t process_capacity;
    uint64_t thread_capacity;
    uint64_t maximum_threads_per_process;
    uint64_t file_descriptor_hard_limit;
    uint64_t pipe_capacity;
};

struct ProcessRuntimeProcess final {
    UserAddressSpace address_space;
    ProcessExecutionResult result;
    FileTable file_table;
    fs::FsContext file_system_context;
    os::abi::ResourceLimit resource_limits[os::abi::OS_ABI_RESOURCE_LIMIT_KIND_COUNT];
    bool active;
};

struct alignas(OS_KERNEL_EXTENDED_STATE_AREA_ALIGNMENT_BYTES) ProcessRuntimeThread final {
    ExceptionFrame *saved_frame;
    FxSaveArea extended_state;
    uint64_t kernel_stack_pointer;
    KernelThreadEntryOperation kernel_entry_operation;
    void *kernel_entry_context;
    uint64_t user_stack_base_address;
    uint64_t user_stack_size_bytes;
    uint64_t thread_local_storage_base;
    uint64_t thread_local_storage_size_bytes;
    uint64_t exit_value;
    uint64_t join_owner_thread_id;
    uint64_t blocked_system_call_number;
    uint64_t block_io_request_identifier;
    UserContextEntryMethod user_kernel_continuation_entry_method;
    bool blocked_system_call_restartable;
    bool user_kernel_continuation_active;
    bool user_kernel_continuation_system_call_active;
    bool user_kernel_continuation_swap_gs_required;
    bool user_kernel_continuation_uses_kernel_page_table;
    bool joinable;
    bool active;
};

struct KernelWorkQueueSelfTestContext final {
    uint64_t expected_execution_order;
    bool fail;
};

ThreadScheduler thread_scheduler;
ProcessEntry process_entries[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ThreadEntry thread_entries[OS_KERNEL_THREAD_CAPACITY_LIMIT];
Pipe process_pipe;
PipeManager dynamic_pipe_manager;
Terminal process_terminal;
KernelObjectManager kernel_object_manager;
FileDescriptionManager file_description_manager;
ProcessRuntimeProcess runtime_processes[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ProcessRuntimeThread runtime_threads[OS_KERNEL_THREAD_CAPACITY_LIMIT];
WaitQueue pipe_readable_wait_queue;
WaitQueue pipe_writable_wait_queue;
WaitQueue descriptor_readable_wait_queue;
WaitQueue descriptor_writable_wait_queue;
WaitQueue child_exit_wait_queue;
WaitQueue thread_join_wait_queue;
WaitQueue sleep_wait_queue;
WaitQueue block_io_wait_queue;
WaitQueue block_io_completion_wait_queue;
WaitQueue process_io_drain_wait_queue;
WaitQueue kernel_thread_test_wait_queue;
WaitQueue kernel_work_wait_queue;
PrivateFutexManager private_futex_manager;
PrivateFutexEntry private_futex_entries[OS_KERNEL_PRIVATE_FUTEX_CAPACITY_LIMIT];
ProcessTree process_tree;
ProcessTreeEntry process_tree_entries[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
JobControlManager job_control_manager;
JobControlProcessState job_control_process_states[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
SignalManager signal_manager;
SignalProcessState signal_process_states[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
SignalThreadState signal_thread_states[OS_KERNEL_THREAD_CAPACITY_LIMIT];
OomCandidate oom_candidates[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ProgramArgumentPlan program_argument_plan;
RuntimeMutex process_launch_mutex;
uint8_t launch_path_buffer[fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES];
constinit IrqSaveSpinLock scheduler_lock{DisableInterrupts, RestoreInterrupts};
fs::Vfs *process_vfs;
ProcessRuntimeLimits process_runtime_limits;
PhysicalFrameAllocatorStatistics frames_before_processes;
PhysicalFrameAllocatorStatistics frames_after_processes;
KernelVirtualAddressAllocatorStatistics virtual_addresses_before_processes;
KernelVirtualAddressAllocatorStatistics virtual_addresses_after_processes;
KernelStackManagerStatistics kernel_stacks_before_processes;
KernelStackManagerStatistics kernel_stacks_after_processes;
VirtualMemoryAreaPoolStatistics virtual_memory_areas_before_processes;
VirtualMemoryAreaPoolStatistics virtual_memory_areas_after_processes;
UserPageReferenceStatistics user_page_references_before_processes;
UserPageReferenceStatistics user_page_references_after_processes;
ResourceSnapshot resource_snapshot_before_processes;
ResourceSnapshot resource_snapshot_after_processes;
ResourceSnapshotDifference resource_snapshot_difference;
uint64_t pipe_reader_block_count;
uint64_t pipe_writer_block_count;
uint64_t descriptor_reader_block_count;
uint64_t pipe_end_of_file_observation_count;
uint64_t pipe_broken_observation_count;
uint64_t capacity_self_test_process_count;
uint64_t capacity_self_test_thread_count;
uint64_t capacity_self_test_threads_per_process;
uint64_t oom_invocation_count;
uint64_t oom_kill_count;
uint64_t last_oom_victim_process_id;
uint64_t last_oom_victim_score;
uint64_t file_synchronization_count;
uint64_t file_data_synchronization_count;
uint64_t memory_synchronous_synchronization_count;
uint64_t memory_asynchronous_synchronization_count;
uint64_t anonymous_reclaim_process_cursor;
UserThreadRuntimeStatistics user_thread_runtime_statistics;
KernelThreadRuntimeStatistics kernel_thread_runtime_statistics;
WorkQueue kernel_work_queue;
WorkQueueEntry kernel_work_queue_entries[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_CAPACITY];
uint64_t kernel_work_queue_delayed_heap[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_CAPACITY];
KernelWorkQueueSelfTestContext
    kernel_work_queue_contexts[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_REGISTERED_COUNT];
uint64_t kernel_work_queue_execution_order;
uint64_t kernel_thread_self_test_step;
WorkHandle file_writeback_work_handle;
WorkHandle page_aging_work_handle;
WorkHandle background_reclaim_work_handle;
WorkHandle file_readahead_work_handle;
FileReadaheadRequestQueue file_readahead_request_queue;
FileReadaheadRequestSlot
    file_readahead_request_slots[OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_CAPACITY];
uint64_t
    file_readahead_request_ready_storage[OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_CAPACITY];
FileReadaheadRequest
    file_readahead_cancelled_requests[OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_CAPACITY];
FileReadaheadFeedbackLedger file_readahead_feedback_ledger;
FileReadaheadFeedbackSlot
    file_readahead_feedback_slots[OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_FEEDBACK_CAPACITY];
BackgroundReclaimController background_reclaim_controller;
PageAgingManager page_aging_manager;
PageAgingEntry *page_aging_entries;
uint64_t *page_aging_hash;
KernelPageAllocation page_aging_entry_storage;
KernelPageAllocation page_aging_hash_storage;
uint64_t page_aging_capacity;
uint64_t page_aging_hash_capacity;
bool file_writeback_work_registered;
bool page_aging_work_registered;
bool background_reclaim_work_registered;
bool file_readahead_work_registered;
bool page_aging_worker_failed;
bool background_reclaim_worker_failed;
bool file_readahead_worker_failed;
bool file_readahead_worker_io_active;
bool kernel_work_thread_stop_requested;
BlockIoCoordinator block_io_coordinator;
BlockIoSlot block_io_slots[OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_CAPACITY];
FilePageLoadCoordinator file_page_load_coordinator;
FilePageLoadSlot file_page_load_slots[OS_KERNEL_THREAD_CAPACITY_LIMIT];
FilePageLoadWaiter file_page_load_waiters[OS_KERNEL_THREAD_CAPACITY_LIMIT];
WaitQueue file_page_load_wait_queues[OS_KERNEL_THREAD_CAPACITY_LIMIT];
FilePageWritebackCoordinator file_page_writeback_coordinator;
FilePageWritebackSlot file_page_writeback_slots[OS_KERNEL_THREAD_CAPACITY_LIMIT];
FilePageWritebackWaiter file_page_writeback_waiters[OS_KERNEL_THREAD_CAPACITY_LIMIT];
WaitQueue file_page_writeback_wait_queues[OS_KERNEL_THREAD_CAPACITY_LIMIT];
AsynchronousBlockDevice *block_io_devices[OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_DEVICE_CAPACITY];
uint64_t block_io_device_count;
bool block_io_completion_worker_stop_requested;
bool block_io_completion_worker_started;
volatile uint64_t block_io_completion_notification_generation;
bool block_io_runtime_probe_succeeded;
bool block_io_runtime_probe_wait_active;
bool kernel_thread_runtime_available;
bool kernel_thread_dispatch_active;
bool process_runtime_initialized;
bool process_scheduling_active;
bool current_process_oom_kill_pending;
uint64_t user_kernel_continuation_activation_failure_stage;
uint64_t user_thread_activation_failure_stage;

void WriteProcessRuntimeValue(const char *prefix, uint64_t value) noexcept;
void WakeRequiredThreads(WaitCondition wait_condition, WakeReason wake_reason) noexcept;
[[nodiscard]] bool WaitForProcessKernelContinuations(uint64_t process_index,
                                                     uint64_t current_thread_index) noexcept;
[[nodiscard]] bool AnyUserKernelContinuationActive() noexcept;

enum class PageAgingFailureStage : uint64_t {
    None,
    BeginObservation,
    FileCacheObservation,
    ProcessPageObservation,
    EndObservation,
    Validation,
};

struct PageAgingObservationContext final {
    PageAgingStatus failure_status;
};

struct BackgroundReclaimSelectionContext final {
    PageAgingStatus failure_status;
};

[[nodiscard]] bool ScheduleRuntimeBackgroundReclaimWork() noexcept;
[[nodiscard]] bool ScheduleRuntimeFileReadaheadWork() noexcept;
[[nodiscard]] bool CancelRuntimeFileReadaheadFile(const FileCacheIdentity &identity) noexcept;

void RecordPageAgingFailure(const PageAgingFailureStage stage,
                            const PageAgingStatus status) noexcept {
    page_aging_worker_failed = true;
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_STAGE_PREFIX,
                             static_cast<uint64_t>(stage));
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_STATUS_PREFIX,
                             static_cast<uint64_t>(status));
    if (status == PageAgingStatus::KindConflict) {
        const PageAgingStatistics statistics = page_aging_manager.Statistics();
        WriteProcessRuntimeValue(
            OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_PHYSICAL_ADDRESS_PREFIX,
            statistics.last_kind_conflict_physical_address);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_EXISTING_KIND_PREFIX,
                                 static_cast<uint64_t>(statistics.last_existing_kind));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_OBSERVED_KIND_PREFIX,
                                 static_cast<uint64_t>(statistics.last_observed_kind));
    }
}

[[nodiscard]] bool CalculatePageCount(const uint64_t length_bytes, uint64_t &page_count) noexcept {
    page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const uint64_t page_mask =
        os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
    if (length_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        length_bytes > UINT64_MAX - page_mask) {
        return false;
    }
    page_count = (length_bytes + page_mask) / os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
    return page_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
}

[[nodiscard]] bool ResourceLimitKindIsValid(const os::abi::ResourceLimitKind kind) noexcept {
    return static_cast<uint64_t>(kind) < os::abi::OS_ABI_RESOURCE_LIMIT_KIND_COUNT;
}

[[nodiscard]] uint64_t ResourceLimitSystemMaximum(const os::abi::ResourceLimitKind kind) noexcept {
    if (kind == os::abi::ResourceLimitKind::FileSize) {
        return fs::OS_KERNEL_ROOTFS_MAXIMUM_FILE_SIZE_BYTES;
    }
    if (kind == os::abi::ResourceLimitKind::Data) {
        return os::abi::OS_ABI_USER_HEAP_MAXIMUM_SIZE_BYTES;
    }
    if (kind == os::abi::ResourceLimitKind::Stack) {
        return os::abi::OS_ABI_USER_STACK_MAXIMUM_SIZE_BYTES;
    }
    if (kind == os::abi::ResourceLimitKind::Core ||
        kind == os::abi::ResourceLimitKind::LockedMemory ||
        kind == os::abi::ResourceLimitKind::MessageQueueBytes ||
        kind == os::abi::ResourceLimitKind::Nice ||
        kind == os::abi::ResourceLimitKind::RealtimePriority) {
        return OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }
    if (kind == os::abi::ResourceLimitKind::ProcessCount) {
        return process_runtime_limits.process_capacity;
    }
    if (kind == os::abi::ResourceLimitKind::OpenFileCount) {
        return process_runtime_limits.file_descriptor_hard_limit;
    }
    return os::abi::OS_ABI_RESOURCE_LIMIT_INFINITY;
}

void InitializeResourceLimits(ProcessRuntimeProcess &process,
                              const ProcessRuntimeProcess *const parent) noexcept {
    if (parent != nullptr) {
        for (uint64_t limit_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
             limit_index < os::abi::OS_ABI_RESOURCE_LIMIT_KIND_COUNT; ++limit_index) {
            process.resource_limits[limit_index] = parent->resource_limits[limit_index];
        }
        return;
    }
    for (uint64_t limit_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         limit_index < os::abi::OS_ABI_RESOURCE_LIMIT_KIND_COUNT; ++limit_index) {
        const auto kind = static_cast<os::abi::ResourceLimitKind>(limit_index);
        const uint64_t maximum = ResourceLimitSystemMaximum(kind);
        process.resource_limits[limit_index] = os::abi::ResourceLimit{
            .current = maximum,
            .maximum = maximum,
        };
    }
}

[[nodiscard]] bool IdentifierRequestAllowed(const uint32_t requested_identifier,
                                            const uint32_t real_identifier,
                                            const uint32_t effective_identifier,
                                            const uint32_t saved_identifier) noexcept {
    return requested_identifier == os::abi::OS_ABI_IDENTIFIER_UNCHANGED ||
           requested_identifier == real_identifier ||
           requested_identifier == effective_identifier || requested_identifier == saved_identifier;
}

[[nodiscard]] bool ProcessCountLimitReached(const uint64_t parent_process_index) noexcept {
    if (parent_process_index >= process_runtime_limits.process_capacity ||
        !runtime_processes[parent_process_index].active) {
        return true;
    }
    const ProcessRuntimeProcess &parent = runtime_processes[parent_process_index];
    const uint64_t limit =
        parent.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::ProcessCount)]
            .current;
    uint64_t process_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const os::abi::UserIdentifier user_identifier =
        parent.file_system_context.credentials.real_user_identifier;
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < process_runtime_limits.process_capacity; ++process_index) {
        if (runtime_processes[process_index].active &&
            runtime_processes[process_index].file_system_context.credentials.real_user_identifier ==
                user_identifier) {
            ++process_count;
        }
    }
    return process_count >= limit;
}

void ApplyExecutableCredentials(fs::FsContext &context,
                                const fs::NodeInformation &information) noexcept {
    if ((information.mode & os::abi::OS_ABI_FILE_MODE_SET_USER_IDENTIFIER) != 0U) {
        context.credentials.effective_user_identifier = information.owner_user_identifier;
        context.credentials.saved_user_identifier = information.owner_user_identifier;
    }
    if ((information.mode & os::abi::OS_ABI_FILE_MODE_SET_GROUP_IDENTIFIER) != 0U) {
        context.credentials.effective_group_identifier = information.owner_group_identifier;
        context.credentials.saved_group_identifier = information.owner_group_identifier;
    }
}

[[nodiscard]] bool AddressSpaceWithinLimit(const UserAddressSpace &address_space,
                                           const uint64_t limit_bytes) noexcept {
    const os::abi::VirtualMemoryStatistics statistics =
        GetUserVirtualMemoryStatistics(address_space);
    return statistics.virtual_page_count <= limit_bytes / os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
}

[[nodiscard]] bool RemoveSignalThreadIfPresent(uint64_t thread_index) noexcept;
[[nodiscard]] bool RemoveSignalProcessIfPresent(uint64_t process_index) noexcept;

ProcessEntry capacity_process_entries[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
ThreadEntry capacity_thread_entries[OS_KERNEL_THREAD_CAPACITY_LIMIT];
FxSaveArea capacity_thread_extended_states[OS_KERNEL_THREAD_CAPACITY_LIMIT];
uint64_t capacity_process_roots[OS_KERNEL_PROCESS_CAPACITY_LIMIT];
bool capacity_stack_active[OS_KERNEL_THREAD_CAPACITY_LIMIT];
Pipe *capacity_pipe_entries[OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY];

[[nodiscard]] bool AllocatePipePage(void *, uint64_t &physical_address,
                                    uint8_t *&virtual_address) noexcept {
    physical_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    virtual_address = nullptr;
    PhysicalFrame frame{};
    if (GetKernelPhysicalFrameAllocator().Allocate(frame) !=
        PhysicalFrameAllocatorStatus::Succeeded) {
        return false;
    }
    physical_address = frame.physical_address;
    virtual_address =
        reinterpret_cast<uint8_t *>(PhysicalMemoryDirectMapAddress(frame.physical_address));
    if (virtual_address != nullptr) {
        return true;
    }
    static_cast<void>(GetKernelPhysicalFrameAllocator().Release(frame));
    physical_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    return false;
}

[[nodiscard]] bool ReleasePipePage(void *, const uint64_t physical_address,
                                   uint8_t *const virtual_address) noexcept {
    if (physical_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE || virtual_address == nullptr) {
        return false;
    }
    return GetKernelPhysicalFrameAllocator().Release(PhysicalFrame{
               .physical_address = physical_address}) == PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool RunPipeCapacitySelfTest(const uint64_t capacity) noexcept {
    for (uint64_t pipe_index = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
         pipe_index < OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY; ++pipe_index) {
        capacity_pipe_entries[pipe_index] = nullptr;
    }
    for (uint64_t pipe_index = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE; pipe_index < capacity;
         ++pipe_index) {
        if (dynamic_pipe_manager.Create(capacity_pipe_entries[pipe_index]) !=
                PipeManagerStatus::Succeeded ||
            capacity_pipe_entries[pipe_index] == nullptr) {
            return false;
        }
    }
    Pipe *overflow_pipe = nullptr;
    if (dynamic_pipe_manager.Create(overflow_pipe) != PipeManagerStatus::CapacityExhausted ||
        overflow_pipe != nullptr) {
        return false;
    }
    for (uint64_t pipe_index = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE; pipe_index < capacity;
         ++pipe_index) {
        Pipe *const pipe = capacity_pipe_entries[pipe_index];
        if (pipe == nullptr ||
            dynamic_pipe_manager.CloseReader(*pipe) != PipeManagerStatus::Succeeded ||
            dynamic_pipe_manager.CloseWriter(*pipe) != PipeManagerStatus::Succeeded) {
            return false;
        }
        capacity_pipe_entries[pipe_index] = nullptr;
    }
    const PipeManagerStatistics statistics = dynamic_pipe_manager.Statistics();
    return dynamic_pipe_manager.Validate() == PipeManagerStatus::Succeeded &&
           statistics.capacity == capacity &&
           statistics.active_pipe_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           statistics.peak_active_pipe_count == capacity && statistics.creation_count == capacity &&
           statistics.release_count == capacity;
}

[[nodiscard]] FileIdentity
FileIdentityFromInformation(const fs::NodeInformation &information) noexcept {
    return FileIdentity{
        .superblock_identifier = information.superblock_identifier,
        .superblock_generation = information.superblock_generation,
        .node_identifier = information.node_identifier,
        .node_generation = information.generation,
    };
}

[[nodiscard]] FileIdentity FileIdentityFromVnode(const fs::Vnode &vnode) noexcept {
    return FileIdentity{
        .superblock_identifier = vnode.superblock->identifier,
        .superblock_generation = vnode.superblock->generation,
        .node_identifier = vnode.identifier,
        .node_generation = vnode.generation,
    };
}

void WriteProcessRuntimeValue(const char *const prefix, const uint64_t value) noexcept;

[[nodiscard]] bool PrepareRuntimeFileTruncate(const FileIdentity &identity,
                                              const uint64_t size_bytes) noexcept {
    if (!CancelRuntimeFileReadaheadFile(identity)) {
        return false;
    }
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < process_runtime_limits.process_capacity; ++process_index) {
        ProcessRuntimeProcess &runtime_process = runtime_processes[process_index];
        // Zombie 仍由父进程持有结果槽，但其地址空间已经销毁。truncate 只处理
        // 仍然存在的地址空间，不能把合法的 Zombie 生命周期误判为页缓存损坏。
        if (!runtime_process.active || runtime_process.address_space.root_physical_address ==
                                           OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            continue;
        }
        if (TruncateUserFileMappings(runtime_process.address_space, identity, size_bytes) !=
            UserVirtualMemoryStatus::Succeeded) {
            return false;
        }
        runtime_process.result.mapped_page_count = runtime_process.address_space.mapped_page_count;
    }
    return true;
}

[[nodiscard]] bool ProtectRuntimeSharedFileMappings() noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < process_runtime_limits.process_capacity; ++process_index) {
        ProcessRuntimeProcess &runtime_process = runtime_processes[process_index];
        if (!runtime_process.active || runtime_process.address_space.root_physical_address ==
                                           OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            continue;
        }
        if (ProtectUserSharedFileMappings(runtime_process.address_space) !=
            UserVirtualMemoryStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ProtectRuntimeSharedFileMappingsForReclaim(void *const context) noexcept {
    static_cast<void>(context);
    return ProtectRuntimeSharedFileMappings();
}

[[nodiscard]] bool PrepareRuntimeAnonymousPageRelease(void *const context,
                                                      const uint64_t physical_address) noexcept {
    static_cast<void>(context);
    const PageAgingStatus status =
        page_aging_manager.Forget(physical_address, PageAgingKind::Anonymous);
    if (status != PageAgingStatus::Succeeded && status != PageAgingStatus::EntryNotFound) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FORGET_FAILURE_STATUS_PREFIX,
                                 static_cast<uint64_t>(status));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FORGET_FAILURE_ADDRESS_PREFIX,
                                 physical_address);
    }
    return status == PageAgingStatus::Succeeded || status == PageAgingStatus::EntryNotFound;
}

[[nodiscard]] bool FindRuntimeProtectedStackPage(const uint64_t process_index,
                                                 uint64_t &protected_stack_page_address,
                                                 bool &multiple_stack_pages) noexcept {
    protected_stack_page_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    multiple_stack_pages = false;
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        if (!runtime_threads[thread_index].active ||
            runtime_threads[thread_index].saved_frame == nullptr) {
            continue;
        }
        ThreadEntry scheduler_thread{};
        if (thread_scheduler.ReadThread(thread_index, scheduler_thread) !=
            ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (scheduler_thread.process_index != process_index) {
            continue;
        }
        const uint64_t stack_page_address =
            AsUserContext(*runtime_threads[thread_index].saved_frame).stack_pointer &
            ~OS_KERNEL_PROCESS_RUNTIME_PAGE_MASK;
        if (protected_stack_page_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            protected_stack_page_address = stack_page_address;
        } else if (protected_stack_page_address != stack_page_address) {
            multiple_stack_pages = true;
            break;
        }
    }
    return true;
}

[[nodiscard]] bool ReclaimRuntimeAnonymousPagesInternal(
    UserAddressSpace *const requester, const uint64_t target_page_count,
    const uint64_t excluded_virtual_address, void *const selection_context,
    const UserMemoryReclaimPageSelectionOperation selection_operation,
    const UserMemoryReclaimPageCompletionOperation completion_operation,
    uint64_t &reclaimed_page_count) noexcept {
    reclaimed_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (target_page_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        process_runtime_limits.process_capacity == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return true;
    }
    for (uint64_t scan_index = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
         scan_index < process_runtime_limits.process_capacity &&
         reclaimed_page_count < target_page_count;
         ++scan_index) {
        const uint64_t process_index = (anonymous_reclaim_process_cursor + scan_index) %
                                       process_runtime_limits.process_capacity;
        ProcessRuntimeProcess &process = runtime_processes[process_index];
        if (!process.active ||
            process.address_space.root_physical_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
            process.result.process_id == OS_KERNEL_PROCESS_RUNTIME_INIT_PROCESS_ID) {
            continue;
        }
        uint64_t protected_stack_page_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        bool multiple_stack_pages = false;
        if (!FindRuntimeProtectedStackPage(process_index, protected_stack_page_address,
                                           multiple_stack_pages)) {
            return false;
        }
        if (multiple_stack_pages) {
            continue;
        }
        const uint64_t remaining_page_count = target_page_count - reclaimed_page_count;
        const uint64_t process_excluded_virtual_address =
            requester != nullptr && &process.address_space == requester
                ? excluded_virtual_address
                : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t process_reclaimed_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        const UserVirtualMemoryStatus reclaim_status =
            selection_operation == nullptr
                ? ReclaimUserAnonymousPages(
                      process.address_space, remaining_page_count, process_excluded_virtual_address,
                      protected_stack_page_address, process_reclaimed_page_count)
                : ReclaimSelectedUserAnonymousPages(
                      process.address_space, remaining_page_count, process_excluded_virtual_address,
                      protected_stack_page_address, selection_context, selection_operation,
                      completion_operation, process_reclaimed_page_count);
        if (reclaim_status != UserVirtualMemoryStatus::Succeeded ||
            reclaimed_page_count > UINT64_MAX - process_reclaimed_page_count) {
            return false;
        }
        reclaimed_page_count += process_reclaimed_page_count;
        process.result.mapped_page_count = process.address_space.mapped_page_count;
    }
    anonymous_reclaim_process_cursor =
        (anonymous_reclaim_process_cursor + OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) %
        process_runtime_limits.process_capacity;
    return true;
}

[[nodiscard]] bool ReclaimRuntimeAnonymousPages(void *const context, UserAddressSpace &requester,
                                                const uint64_t target_page_count,
                                                const uint64_t excluded_virtual_address,
                                                const uint64_t protected_virtual_address,
                                                uint64_t &reclaimed_page_count) noexcept {
    static_cast<void>(context);
    static_cast<void>(protected_virtual_address);
    return ReclaimRuntimeAnonymousPagesInternal(&requester, target_page_count,
                                                excluded_virtual_address, nullptr, nullptr, nullptr,
                                                reclaimed_page_count);
}

[[nodiscard]] bool RecoverRuntimeOutOfMemory(void *const context, UserAddressSpace &requester,
                                             const bool allow_current_victim) noexcept {
    static_cast<void>(context);
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < process_runtime_limits.process_capacity; ++process_index) {
        if (runtime_processes[process_index].active &&
            &runtime_processes[process_index].address_space == &requester) {
            return TryRecoverFromOutOfMemory(process_index, allow_current_victim);
        }
    }
    return false;
}

[[nodiscard]] FileSystemStatus BalanceRuntimeFileWritebackPressure() noexcept {
    if (!UserFileWritebackBackpressureRequired()) {
        return FileSystemStatus::Succeeded;
    }
    RecordUserFileWritebackBackpressure();
    if (UserFileWritebackWorkerPaused() || !ProtectRuntimeSharedFileMappings()) {
        return UserFileWritebackWorkerPaused() ? FileSystemStatus::DeviceFailure
                                               : FileSystemStatus::Corrupt;
    }
    uint64_t written_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const UserVirtualMemoryStatus status = RunUserFileWritebackWorker(written_page_count);
    if (status != UserVirtualMemoryStatus::Succeeded) {
        return status == UserVirtualMemoryStatus::FileWriteFailed ? FileSystemStatus::DeviceFailure
                                                                  : FileSystemStatus::Corrupt;
    }
    return !UserFileWritebackBackpressureRequired() ||
                   written_page_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE
               ? FileSystemStatus::Succeeded
               : FileSystemStatus::DeviceFailure;
}

[[nodiscard]] bool FlushOutstandingUserFilePages() noexcept {
    const FilePageCacheStatistics statistics = GetUserFilePageCacheStatistics();
    if (statistics.dirty_page_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        statistics.writeback_page_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        statistics.error_page_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        uint64_t written_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return WritebackUserFilePageCache(OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                                          written_page_count) ==
                   UserVirtualMemoryStatus::Succeeded &&
               written_page_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }
    return SyncCurrentProcessFileSystem() == FileSystemStatus::Succeeded;
}

extern "C" void OsKernelEnterScheduledProcess(ExceptionFrame *frame) noexcept;
extern "C" void OsKernelEnterScheduledUserContinuation(uint64_t stack_pointer,
                                                       bool swap_gs_required) noexcept;
extern "C" void OsKernelSuspendScheduledUserContinuation(uint64_t *stack_pointer,
                                                         bool swap_gs_required) noexcept;
extern "C" [[noreturn]] void OsKernelReturnFromUserMode(uint64_t swap_gs_required) noexcept;
extern "C" void OsKernelEnterScheduledKernelThread(uint64_t stack_pointer) noexcept;
extern "C" void OsKernelSwitchKernelThread(uint64_t *previous_stack_pointer,
                                           uint64_t next_stack_pointer) noexcept;
extern "C" void OsKernelSuspendScheduledKernelThread(uint64_t *stack_pointer) noexcept;
extern "C" void OsKernelLeaveScheduledKernelThread() noexcept;
extern "C" [[noreturn]] void OsKernelThreadBootstrap() noexcept;

void WriteProcessRuntimeValue(const char *const prefix, const uint64_t value) noexcept {
    const VgaTextConsole vga_console{VgaTextConsole::Hardware(WritePort8)};
    if (!vga_console.TryWriteDiagnosticHexLine(prefix, value)) {
        HaltProcessor();
    }
}

[[nodiscard]] ProcessRuntimeLimits
SelectProcessRuntimeLimits(const uint64_t managed_memory_bytes) noexcept {
    if (managed_memory_bytes >= OS_KERNEL_PROCESS_RUNTIME_CAPACITY_MEMORY_BYTES) {
        return ProcessRuntimeLimits{
            .process_capacity = OS_KERNEL_PROCESS_CAPACITY_LIMIT,
            .thread_capacity = OS_KERNEL_THREAD_CAPACITY_LIMIT,
            .maximum_threads_per_process = OS_KERNEL_CAPACITY_THREADS_PER_PROCESS,
            .file_descriptor_hard_limit = OS_KERNEL_FILE_TABLE_MAXIMUM_HARD_LIMIT,
            .pipe_capacity = OS_KERNEL_PIPE_MANAGER_MAXIMUM_CAPACITY,
        };
    }
    if (managed_memory_bytes >= OS_KERNEL_PROCESS_RUNTIME_FUNCTIONAL_MEMORY_BYTES) {
        return ProcessRuntimeLimits{
            .process_capacity = OS_KERNEL_PROCESS_FUNCTIONAL_CAPACITY,
            .thread_capacity = OS_KERNEL_THREAD_FUNCTIONAL_CAPACITY,
            .maximum_threads_per_process = OS_KERNEL_FUNCTIONAL_THREADS_PER_PROCESS,
            .file_descriptor_hard_limit = OS_KERNEL_FILE_TABLE_FUNCTIONAL_HARD_LIMIT,
            .pipe_capacity = OS_KERNEL_PIPE_MANAGER_FUNCTIONAL_CAPACITY,
        };
    }
    return ProcessRuntimeLimits{
        .process_capacity = OS_KERNEL_PROCESS_BOOTSTRAP_CAPACITY,
        .thread_capacity = OS_KERNEL_THREAD_BOOTSTRAP_CAPACITY,
        .maximum_threads_per_process = OS_KERNEL_BOOTSTRAP_THREADS_PER_PROCESS,
        .file_descriptor_hard_limit = OS_KERNEL_PROCESS_RUNTIME_BOOTSTRAP_FILE_DESCRIPTOR_LIMIT,
        .pipe_capacity = OS_KERNEL_PIPE_MANAGER_BOOTSTRAP_CAPACITY,
    };
}

[[nodiscard]] bool DiscountPersistentVfsResources(ResourceSnapshot &snapshot,
                                                  const fs::ResourceUsage &usage) noexcept {
    const fs::NamespaceBackingResourceUsage &namespace_usage = usage.namespace_backing;
    if (snapshot.heap_consumed_bytes < usage.heap_consumed_bytes ||
        snapshot.heap_active_requested_bytes < usage.heap_active_requested_bytes ||
        snapshot.heap_allocation_count < usage.heap_allocation_count ||
        snapshot.vnode_count < usage.vnode_count ||
        snapshot.allocated_frame_count < namespace_usage.allocated_frame_count ||
        snapshot.buddy_active_block_count < namespace_usage.buddy_active_block_count ||
        snapshot.free_frame_count > UINT64_MAX - namespace_usage.allocated_frame_count ||
        snapshot.virtual_address_allocated_page_count <
            namespace_usage.virtual_address_allocated_page_count ||
        snapshot.virtual_address_free_page_count >
            UINT64_MAX - namespace_usage.virtual_address_allocated_page_count ||
        snapshot.virtual_address_active_descriptor_count <
            namespace_usage.virtual_address_active_descriptor_count ||
        snapshot.virtual_address_active_allocation_count <
            namespace_usage.virtual_address_active_allocation_count) {
        return false;
    }
    snapshot.heap_consumed_bytes -= usage.heap_consumed_bytes;
    snapshot.heap_active_requested_bytes -= usage.heap_active_requested_bytes;
    snapshot.heap_allocation_count -= usage.heap_allocation_count;
    snapshot.vnode_count -= usage.vnode_count;
    snapshot.free_frame_count += namespace_usage.allocated_frame_count;
    snapshot.allocated_frame_count -= namespace_usage.allocated_frame_count;
    snapshot.buddy_active_block_count -= namespace_usage.buddy_active_block_count;
    snapshot.virtual_address_free_page_count +=
        namespace_usage.virtual_address_allocated_page_count;
    snapshot.virtual_address_allocated_page_count -=
        namespace_usage.virtual_address_allocated_page_count;
    snapshot.virtual_address_active_descriptor_count -=
        namespace_usage.virtual_address_active_descriptor_count;
    snapshot.virtual_address_active_allocation_count -=
        namespace_usage.virtual_address_active_allocation_count;
    return ValidateResourceSnapshot(snapshot) == ResourceSnapshotStatus::Succeeded;
}

void ResetRuntimeStorage() noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY_LIMIT; ++process_index) {
        runtime_processes[process_index].address_space = UserAddressSpace{};
        runtime_processes[process_index].result = ProcessExecutionResult{};
        runtime_processes[process_index].file_system_context = fs::FsContext{};
        runtime_processes[process_index].active = false;
    }
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < OS_KERNEL_THREAD_CAPACITY_LIMIT; ++thread_index) {
        runtime_threads[thread_index] = ProcessRuntimeThread{};
    }
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY_LIMIT; ++process_index) {
        oom_candidates[process_index] = OomCandidate{};
    }
    user_thread_runtime_statistics = UserThreadRuntimeStatistics{};
    kernel_thread_runtime_statistics = KernelThreadRuntimeStatistics{};
    kernel_thread_self_test_step = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    kernel_thread_runtime_available = false;
    kernel_thread_dispatch_active = false;
    for (uint64_t device_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         device_index < OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_DEVICE_CAPACITY; ++device_index) {
        block_io_devices[device_index] = nullptr;
    }
    block_io_device_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    block_io_completion_worker_stop_requested = false;
    block_io_completion_worker_started = false;
    block_io_completion_notification_generation = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    block_io_runtime_probe_succeeded = false;
    block_io_runtime_probe_wait_active = false;
    user_kernel_continuation_activation_failure_stage = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    user_thread_activation_failure_stage = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
}

[[nodiscard]] bool ReadThreadKernelStack(const uint64_t thread_index, KernelStack &stack) noexcept {
    ThreadEntry thread{};
    return thread_scheduler.ReadThread(thread_index, thread) == ThreadSchedulerStatus::Succeeded &&
           thread.kernel_stack_slot_index < OS_KERNEL_THREAD_CAPACITY_LIMIT &&
           GetKernelStackManager().Read(thread.kernel_stack_slot_index, stack) ==
               KernelStackManagerStatus::Succeeded;
}

[[nodiscard]] bool BuildInitialContextFrame(const uint64_t kernel_stack_slot_index,
                                            const UserAddressSpace &address_space,
                                            const ProgramArgumentLayout &argument_layout,
                                            ExceptionFrame *&saved_frame) noexcept {
    KernelStack stack{};
    if (GetKernelStackManager().Read(kernel_stack_slot_index, stack) !=
        KernelStackManagerStatus::Succeeded) {
        return false;
    }
    const uint64_t stack_top_address = KernelStackTopAddress(stack);
    if (stack_top_address < OS_KERNEL_USER_CONTEXT_SIZE_BYTES) {
        return false;
    }
    const uint64_t frame_address = stack_top_address - OS_KERNEL_USER_CONTEXT_SIZE_BYTES;
    if (!GetKernelStackManager().Contains(kernel_stack_slot_index, frame_address,
                                          OS_KERNEL_USER_CONTEXT_SIZE_BYTES)) {
        return false;
    }

    UserContext *const frame = reinterpret_cast<UserContext *>(frame_address);
    *frame = UserContext{};
    frame->common.register_rdi = argument_layout.argument_count;
    frame->common.register_rsi = argument_layout.argument_vector_address;
    frame->common.register_rdx = argument_layout.environment_vector_address;
    frame->common.vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    frame->common.instruction_pointer = address_space.entry_virtual_address;
    frame->common.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    frame->common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    frame->stack_pointer = argument_layout.stack_pointer;
    frame->stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    saved_frame = &frame->common;
    return true;
}

[[nodiscard]] bool BuildForkContextFrame(const uint64_t kernel_stack_slot_index,
                                         const ExceptionFrame &parent_frame,
                                         ExceptionFrame *&saved_frame) noexcept {
    KernelStack stack{};
    if (GetKernelStackManager().Read(kernel_stack_slot_index, stack) !=
        KernelStackManagerStatus::Succeeded) {
        return false;
    }
    const uint64_t stack_top_address = KernelStackTopAddress(stack);
    if (stack_top_address < OS_KERNEL_USER_CONTEXT_SIZE_BYTES) {
        return false;
    }
    const uint64_t frame_address = stack_top_address - OS_KERNEL_USER_CONTEXT_SIZE_BYTES;
    if (!GetKernelStackManager().Contains(kernel_stack_slot_index, frame_address,
                                          OS_KERNEL_USER_CONTEXT_SIZE_BYTES)) {
        return false;
    }
    UserContext *const child_context = reinterpret_cast<UserContext *>(frame_address);
    *child_context = AsUserContext(parent_frame);
    child_context->common.register_rax = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    saved_frame = &child_context->common;
    return true;
}

[[nodiscard]] bool BuildThreadContextFrame(const uint64_t kernel_stack_slot_index,
                                           const os::abi::ThreadCreateRequest &request,
                                           ExceptionFrame *&saved_frame) noexcept {
    KernelStack stack{};
    if (GetKernelStackManager().Read(kernel_stack_slot_index, stack) !=
        KernelStackManagerStatus::Succeeded) {
        return false;
    }
    const uint64_t stack_top_address = KernelStackTopAddress(stack);
    if (stack_top_address < OS_KERNEL_USER_CONTEXT_SIZE_BYTES) {
        return false;
    }
    const uint64_t frame_address = stack_top_address - OS_KERNEL_USER_CONTEXT_SIZE_BYTES;
    if (!GetKernelStackManager().Contains(kernel_stack_slot_index, frame_address,
                                          OS_KERNEL_USER_CONTEXT_SIZE_BYTES)) {
        return false;
    }
    UserContext *const context = reinterpret_cast<UserContext *>(frame_address);
    *context = UserContext{};
    context->common.register_rdi = request.argument;
    context->common.vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    context->common.error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    context->common.instruction_pointer = request.entry_address;
    context->common.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    context->common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    context->stack_pointer = request.stack_pointer;
    context->stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    saved_frame = &context->common;
    return true;
}

[[nodiscard]] bool BuildKernelThreadContext(const uint64_t kernel_stack_slot_index,
                                            uint64_t &stack_pointer) noexcept {
    stack_pointer = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    KernelStack stack{};
    if (GetKernelStackManager().Read(kernel_stack_slot_index, stack) !=
        KernelStackManagerStatus::Succeeded) {
        return false;
    }
    const uint64_t stack_top_address = KernelStackTopAddress(stack);
    if (stack_top_address < OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SIZE_BYTES) {
        return false;
    }
    const uint64_t context_address =
        stack_top_address - OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SIZE_BYTES;
    if (!GetKernelStackManager().Contains(kernel_stack_slot_index, context_address,
                                          OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SIZE_BYTES)) {
        return false;
    }
    uint64_t *const context = reinterpret_cast<uint64_t *>(context_address);
    for (uint64_t slot_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         slot_index < OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SLOT_COUNT; ++slot_index) {
        context[slot_index] = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }
    context[OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_FLAGS_SLOT] =
        OS_KERNEL_PROCESS_RUNTIME_INITIAL_KERNEL_FLAGS;
    context[OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_ENTRY_SLOT] =
        reinterpret_cast<uint64_t>(&OsKernelThreadBootstrap);
    context[OS_KERNEL_PROCESS_RUNTIME_KERNEL_CONTEXT_SENTINEL_SLOT] =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    stack_pointer = context_address;
    return true;
}

[[nodiscard]] bool UserThreadRequestIsValid(const ProcessRuntimeProcess &process,
                                            const os::abi::ThreadCreateRequest &request) noexcept {
    if (request.stack_size_bytes < os::abi::OS_ABI_THREAD_STACK_MINIMUM_SIZE_BYTES ||
        request.stack_base_address % OS_KERNEL_MEMORY_PAGE_SIZE_BYTES !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.stack_size_bytes % OS_KERNEL_MEMORY_PAGE_SIZE_BYTES !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.stack_base_address > UINT64_MAX - request.stack_size_bytes) {
        return false;
    }
    const uint64_t stack_end_address = request.stack_base_address + request.stack_size_bytes;
    if (stack_end_address < os::abi::OS_ABI_THREAD_ENTRY_STACK_REMAINDER_BYTES ||
        request.stack_pointer !=
            stack_end_address - os::abi::OS_ABI_THREAD_ENTRY_STACK_REMAINDER_BYTES ||
        request.stack_pointer % OS_KERNEL_PROCESS_RUNTIME_THREAD_STACK_ALIGNMENT_BYTES !=
            os::abi::OS_ABI_THREAD_ENTRY_STACK_REMAINDER_BYTES ||
        request.thread_local_storage_base % os::abi::OS_ABI_THREAD_LOCAL_STORAGE_ALIGNMENT_BYTES !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        !IsUserProgramVirtualAddressRange(request.entry_address,
                                          OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) ||
        !IsUserProgramVirtualAddressRange(request.stack_base_address, request.stack_size_bytes) ||
        !IsUserProgramVirtualAddressRange(
            request.thread_local_storage_base,
            OS_KERNEL_PROCESS_RUNTIME_THREAD_LOCAL_STORAGE_PROBE_BYTES)) {
        return false;
    }
    VirtualMemoryArea entry_area{};
    VirtualMemoryArea stack_area{};
    VirtualMemoryArea thread_local_storage_area{};
    return process.address_space.virtual_memory_map.FindContaining(
               request.entry_address, entry_area) == VirtualMemoryAreaStatus::Succeeded &&
           entry_area.permissions.executable && !entry_area.permissions.writable &&
           process.address_space.virtual_memory_map.FindContaining(
               request.stack_base_address, stack_area) == VirtualMemoryAreaStatus::Succeeded &&
           stack_area.kind == VirtualMemoryAreaKind::Anonymous && stack_area.permissions.readable &&
           stack_area.permissions.writable && !stack_area.permissions.executable &&
           stack_area.end_address >= stack_end_address &&
           process.address_space.virtual_memory_map.FindContaining(
               request.thread_local_storage_base, thread_local_storage_area) ==
               VirtualMemoryAreaStatus::Succeeded &&
           thread_local_storage_area.permissions.readable &&
           thread_local_storage_area.permissions.writable &&
           !thread_local_storage_area.permissions.executable;
}

[[nodiscard]] uint64_t FindAvailableKernelStackSlot() noexcept {
    for (uint64_t candidate_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         candidate_index < process_runtime_limits.thread_capacity; ++candidate_index) {
        KernelStack candidate_stack{};
        if (!runtime_threads[candidate_index].active &&
            GetKernelStackManager().Read(candidate_index, candidate_stack) ==
                KernelStackManagerStatus::SlotNotActive) {
            return candidate_index;
        }
    }
    return OS_KERNEL_THREAD_INVALID_INDEX;
}

[[nodiscard]] bool CurrentFrameIsValid(const uint64_t thread_index,
                                       const ExceptionFrame &frame) noexcept {
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
        thread.process_index >= process_runtime_limits.process_capacity ||
        !FrameOriginatedFromUser(frame) ||
        !GetKernelStackManager().Contains(thread.kernel_stack_slot_index,
                                          reinterpret_cast<uint64_t>(&frame),
                                          OS_KERNEL_USER_CONTEXT_SIZE_BYTES)) {
        return false;
    }
    const ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
    return GetCpuLocal().Statistics().current_thread_index == thread_index &&
           process.address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           ReadPageTableRoot() == process.address_space.root_physical_address;
}

[[nodiscard]] bool ActivateThread(const uint64_t thread_index) noexcept {
    user_thread_activation_failure_stage = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
        thread.process_index >= process_runtime_limits.process_capacity) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_THREAD_STAGE;
        return false;
    }
    CpuPreemptionGuard preemption_guard{};
    if (!preemption_guard.Succeeded()) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_PREEMPTION_STAGE;
        return false;
    }
    ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    KernelStack stack{};
    if (!process.active) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_CONTEXT_STAGE;
        return false;
    }
    if (!runtime_thread.active) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_RUNTIME_STAGE;
        return false;
    }
    if (runtime_thread.saved_frame == nullptr) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_FRAME_STAGE;
        return false;
    }
    if (!ReadThreadKernelStack(thread_index, stack)) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_STACK_READ_STAGE;
        return false;
    }
    if (!GetKernelStackManager().Contains(thread.kernel_stack_slot_index,
                                          reinterpret_cast<uint64_t>(runtime_thread.saved_frame),
                                          OS_KERNEL_USER_CONTEXT_SIZE_BYTES)) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_STACK_RANGE_STAGE;
        return false;
    }
    if (!ActivateUserPageTable(process.address_space.root_physical_address)) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_PAGE_TABLE_STAGE;
        return false;
    }
    const uint64_t kernel_stack_top = KernelStackTopAddress(stack);
    if (!SetPrivilegeStackPointer0(kernel_stack_top)) {
        user_thread_activation_failure_stage = OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_TSS_STAGE;
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        return false;
    }
    if (GetCpuLocal().SetCurrentThread(thread_index, kernel_stack_top) !=
        CpuLocalStatus::Succeeded) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_CPU_LOCAL_STAGE;
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        return false;
    }
    if (RestoreFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        user_thread_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_EXTENDED_STATE_STAGE;
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        return false;
    }
    WriteModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR,
                               thread.thread_local_storage_base);
    if (ReadModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR) !=
        thread.thread_local_storage_base) {
        user_thread_activation_failure_stage = OS_KERNEL_PROCESS_RUNTIME_USER_ACTIVATION_TLS_STAGE;
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        return false;
    }
    SetActiveUserAddressSpace(&process.address_space);
    return true;
}

[[nodiscard]] bool ActivateUserKernelContinuation(const uint64_t thread_index) noexcept {
    user_kernel_continuation_activation_failure_stage = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (thread_index >= process_runtime_limits.thread_capacity) {
        user_kernel_continuation_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_INDEX_STAGE;
        return false;
    }
    ThreadEntry thread{};
    KernelStack stack{};
    const ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    if (thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
        thread.kind != ThreadKind::User) {
        user_kernel_continuation_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_THREAD_STAGE;
        return false;
    }
    if (!runtime_thread.user_kernel_continuation_active ||
        runtime_thread.kernel_stack_pointer == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        user_kernel_continuation_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_METADATA_STAGE;
        return false;
    }
    if (!ReadThreadKernelStack(thread_index, stack)) {
        user_kernel_continuation_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_STACK_STAGE;
        return false;
    }
    if (!GetKernelStackManager().Contains(
            thread.kernel_stack_slot_index, runtime_thread.kernel_stack_pointer,
            OS_KERNEL_PROCESS_RUNTIME_KERNEL_SAVED_CONTEXT_SIZE_BYTES)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_USER_CONTINUATION_STACK_POINTER_PREFIX,
                                 runtime_thread.kernel_stack_pointer);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_USER_CONTINUATION_STACK_BEGIN_PREFIX,
                                 KernelStackMappedBeginAddress(stack));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_USER_CONTINUATION_STACK_TOP_PREFIX,
                                 KernelStackTopAddress(stack));
        user_kernel_continuation_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_STACK_RANGE_STAGE;
        return false;
    }
    bool activation_succeeded = false;
    if (runtime_thread.user_kernel_continuation_uses_kernel_page_table) {
        SetActiveUserAddressSpace(nullptr);
        ActivateKernelPageTable();
        const uint64_t kernel_stack_top = KernelStackTopAddress(stack);
        activation_succeeded =
            ReadPageTableRoot() == GetKernelPageTableRoot() &&
            SetPrivilegeStackPointer0(kernel_stack_top) &&
            GetCpuLocal().SetCurrentThread(thread_index, kernel_stack_top) ==
                CpuLocalStatus::Succeeded &&
            RestoreFxState(runtime_thread.extended_state) == ExtendedStateStatus::Succeeded;
        if (activation_succeeded) {
            WriteModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR,
                                       thread.thread_local_storage_base);
            activation_succeeded =
                ReadModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR) ==
                thread.thread_local_storage_base;
        }
    } else {
        activation_succeeded = ActivateThread(thread_index);
    }
    if (!activation_succeeded ||
        (runtime_thread.user_kernel_continuation_system_call_active &&
         GetCpuLocal().ResumeSystemCall(runtime_thread.user_kernel_continuation_entry_method) !=
             CpuLocalStatus::Succeeded)) {
        user_kernel_continuation_activation_failure_stage =
            OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_ACTIVATE_STAGE;
        return false;
    }
    if (runtime_thread.user_kernel_continuation_swap_gs_required) {
        WriteModelSpecificRegister(OS_KERNEL_PROCESS_RUNTIME_IA32_GS_BASE_MSR,
                                   GetCpuLocal().Address());
        WriteModelSpecificRegister(OS_KERNEL_PROCESS_RUNTIME_IA32_KERNEL_GS_BASE_MSR,
                                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
        if (ReadModelSpecificRegister(OS_KERNEL_PROCESS_RUNTIME_IA32_GS_BASE_MSR) !=
                GetCpuLocal().Address() ||
            ReadModelSpecificRegister(OS_KERNEL_PROCESS_RUNTIME_IA32_KERNEL_GS_BASE_MSR) !=
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            user_kernel_continuation_activation_failure_stage =
                OS_KERNEL_PROCESS_RUNTIME_CONTINUATION_ACTIVATE_STAGE;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ActivateKernelRuntimeThread(const uint64_t thread_index) noexcept {
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
        thread.kind != ThreadKind::Kernel ||
        thread.process_index != OS_KERNEL_PROCESS_INVALID_INDEX) {
        return false;
    }
    if (kernel_thread_runtime_statistics.dispatch_count == UINT64_MAX) {
        return false;
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    KernelStack stack{};
    if (!runtime_thread.active || runtime_thread.saved_frame != nullptr ||
        runtime_thread.kernel_entry_operation == nullptr ||
        runtime_thread.kernel_stack_pointer == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        !ReadThreadKernelStack(thread_index, stack) ||
        !GetKernelStackManager().Contains(
            thread.kernel_stack_slot_index, runtime_thread.kernel_stack_pointer,
            OS_KERNEL_PROCESS_RUNTIME_KERNEL_SAVED_CONTEXT_SIZE_BYTES)) {
        return false;
    }
    SetActiveUserAddressSpace(nullptr);
    ActivateKernelPageTable();
    const uint64_t kernel_stack_top = KernelStackTopAddress(stack);
    if (!SetPrivilegeStackPointer0(kernel_stack_top) ||
        GetCpuLocal().SetCurrentThread(thread_index, kernel_stack_top) !=
            CpuLocalStatus::Succeeded ||
        RestoreFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        return false;
    }
    WriteModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR,
                               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
    if (ReadModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR) !=
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    ++kernel_thread_runtime_statistics.dispatch_count;
    return true;
}

[[nodiscard]] bool ClearActiveKernelRuntimeThread() noexcept {
    SetActiveUserAddressSpace(nullptr);
    ActivateKernelPageTable();
    WriteModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR,
                               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
    return SetPrivilegeStackPointer0(DefaultPrivilegeStackPointer0()) &&
           GetCpuLocal().ClearCurrentThread(DefaultPrivilegeStackPointer0()) ==
               CpuLocalStatus::Succeeded;
}

[[noreturn]] void ReturnUserModeToProcessDispatcher(const bool unwind_interrupt) noexcept {
    const uint64_t swap_gs_required = GetCpuLocal().NativeSystemCallActive()
                                          ? OS_KERNEL_PROCESS_RUNTIME_BOOLEAN_TRUE
                                          : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (unwind_interrupt && GetCpuLocal().LeaveInterrupt() != CpuLocalStatus::Succeeded) {
        HaltProcessor();
    }
    if (!ClearActiveKernelRuntimeThread()) {
        HaltProcessor();
    }
    OsKernelReturnFromUserMode(swap_gs_required);
}

[[nodiscard]] ExceptionFrame *
ActivateScheduledUserOrReturnToDispatcher(const ThreadSchedulingDecision &decision,
                                          const bool unwind_interrupt = false) noexcept {
    if (decision.current_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        ReturnUserModeToProcessDispatcher(unwind_interrupt);
    }
    ThreadEntry next_thread{};
    if (thread_scheduler.ReadThread(decision.current_thread_index, next_thread) !=
        ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (next_thread.kind == ThreadKind::Kernel) {
        if (kernel_thread_runtime_statistics.user_to_kernel_switch_count == UINT64_MAX) {
            HaltProcessor();
        }
        ++kernel_thread_runtime_statistics.user_to_kernel_switch_count;
        ReturnUserModeToProcessDispatcher(unwind_interrupt);
    }
    if (next_thread.kind != ThreadKind::User) {
        HaltProcessor();
    }
    // 目标 User Thread 的 return-frame 准备可能触发文件缺页并睡眠；必须先回到
    // dispatcher，再从目标 Thread 自己的可信内核栈继续，不能借用前任栈执行 C++。
    ReturnUserModeToProcessDispatcher(unwind_interrupt);
}

void SwitchKernelThreadOrReturnToDispatcher(ProcessRuntimeThread &current_runtime_thread,
                                            const ThreadSchedulingDecision &decision) noexcept {
    if (decision.current_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        if (!ClearActiveKernelRuntimeThread()) {
            HaltProcessor();
        }
        OsKernelSuspendScheduledKernelThread(&current_runtime_thread.kernel_stack_pointer);
        return;
    }
    ThreadEntry next_thread{};
    if (thread_scheduler.ReadThread(decision.current_thread_index, next_thread) !=
        ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (next_thread.kind == ThreadKind::User) {
        if (kernel_thread_runtime_statistics.kernel_to_user_switch_count == UINT64_MAX ||
            !ClearActiveKernelRuntimeThread()) {
            HaltProcessor();
        }
        ++kernel_thread_runtime_statistics.kernel_to_user_switch_count;
        OsKernelSuspendScheduledKernelThread(&current_runtime_thread.kernel_stack_pointer);
        return;
    }
    if (next_thread.kind != ThreadKind::Kernel ||
        !ActivateKernelRuntimeThread(decision.current_thread_index)) {
        HaltProcessor();
    }
    OsKernelSwitchKernelThread(&current_runtime_thread.kernel_stack_pointer,
                               runtime_threads[decision.current_thread_index].kernel_stack_pointer);
}

[[nodiscard]] bool SuspendBlockedRuntimeThread(const uint64_t current_thread_index,
                                               const ThreadEntry &current_thread,
                                               ProcessRuntimeThread &current_runtime_thread,
                                               const ThreadSchedulingDecision &decision,
                                               WakeReason &wake_reason) noexcept {
    wake_reason = WakeReason::None;
    if (current_thread.kind == ThreadKind::Kernel) {
        if (kernel_thread_runtime_statistics.block_count == UINT64_MAX) {
            return false;
        }
        ++kernel_thread_runtime_statistics.block_count;
        SwitchKernelThreadOrReturnToDispatcher(current_runtime_thread, decision);
    } else if (current_thread.kind == ThreadKind::User) {
        if (current_runtime_thread.user_kernel_continuation_active ||
            current_runtime_thread.saved_frame == nullptr) {
            return false;
        }
        const CpuLocalStatistics cpu_local_statistics = GetCpuLocal().Statistics();
        current_runtime_thread.user_kernel_continuation_entry_method =
            UserContextEntryMethod::Initial;
        current_runtime_thread.user_kernel_continuation_system_call_active =
            cpu_local_statistics.system_call_active;
        current_runtime_thread.user_kernel_continuation_swap_gs_required =
            GetCpuLocal().NativeSystemCallActive();
        current_runtime_thread.user_kernel_continuation_uses_kernel_page_table =
            ReadPageTableRoot() == GetKernelPageTableRoot();
        if (cpu_local_statistics.system_call_active &&
            GetCpuLocal().SuspendSystemCall(
                current_runtime_thread.user_kernel_continuation_entry_method) !=
                CpuLocalStatus::Succeeded) {
            return false;
        }
        if (!ClearActiveKernelRuntimeThread()) {
            return false;
        }
        if (current_runtime_thread.user_kernel_continuation_swap_gs_required) {
            WriteModelSpecificRegister(OS_KERNEL_PROCESS_RUNTIME_IA32_GS_BASE_MSR,
                                       OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
            WriteModelSpecificRegister(OS_KERNEL_PROCESS_RUNTIME_IA32_KERNEL_GS_BASE_MSR,
                                       GetCpuLocal().Address());
            if (ReadModelSpecificRegister(OS_KERNEL_PROCESS_RUNTIME_IA32_GS_BASE_MSR) !=
                    OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
                ReadModelSpecificRegister(OS_KERNEL_PROCESS_RUNTIME_IA32_KERNEL_GS_BASE_MSR) !=
                    GetCpuLocal().Address()) {
                return false;
            }
        }
        current_runtime_thread.user_kernel_continuation_active = true;
        OsKernelSuspendScheduledUserContinuation(
            &current_runtime_thread.kernel_stack_pointer,
            current_runtime_thread.user_kernel_continuation_swap_gs_required);
        if (thread_scheduler.CurrentThreadIndex() != current_thread_index ||
            !current_runtime_thread.user_kernel_continuation_active) {
            return false;
        }
    } else {
        return false;
    }

    const bool consume_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus consume_status =
        thread_scheduler.ConsumeCurrentThreadWakeReason(wake_reason);
    scheduler_lock.Unlock(consume_interrupts_were_enabled);
    if (current_thread.kind == ThreadKind::User) {
        current_runtime_thread.user_kernel_continuation_active = false;
        current_runtime_thread.kernel_stack_pointer = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        current_runtime_thread.user_kernel_continuation_entry_method =
            UserContextEntryMethod::Initial;
        current_runtime_thread.user_kernel_continuation_system_call_active = false;
        current_runtime_thread.user_kernel_continuation_swap_gs_required = false;
        current_runtime_thread.user_kernel_continuation_uses_kernel_page_table = false;
        WakeRequiredThreads(WaitCondition::ProcessIoDrain, WakeReason::ConditionSatisfied);
    }
    return consume_status == ThreadSchedulerStatus::Succeeded;
}

[[nodiscard]] KernelThreadRuntimeStatus
CreateKernelThreadCore(const KernelThreadEntryOperation entry_operation, void *const context,
                       ThreadId &thread_id) noexcept {
    thread_id = ThreadId{};
    if (entry_operation == nullptr) {
        return KernelThreadRuntimeStatus::InvalidEntry;
    }
    if (kernel_thread_runtime_statistics.create_count == UINT64_MAX ||
        kernel_thread_runtime_statistics.active_thread_count == UINT64_MAX) {
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    const uint64_t kernel_stack_slot_index = FindAvailableKernelStackSlot();
    if (kernel_stack_slot_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        return KernelThreadRuntimeStatus::CapacityExhausted;
    }
    if (GetKernelStackManager().TryCreate(kernel_stack_slot_index) !=
        KernelStackManagerStatus::Succeeded) {
        return KernelThreadRuntimeStatus::StackFailure;
    }
    uint64_t kernel_stack_pointer = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    FxSaveArea initial_extended_state{};
    if (!BuildKernelThreadContext(kernel_stack_slot_index, kernel_stack_pointer) ||
        InitializeFxSaveArea(initial_extended_state) != ExtendedStateStatus::Succeeded) {
        return GetKernelStackManager().TryDestroy(kernel_stack_slot_index) ==
                       KernelStackManagerStatus::Succeeded
                   ? KernelThreadRuntimeStatus::ContextFailure
                   : KernelThreadRuntimeStatus::StackFailure;
    }
    uint64_t thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    ThreadId assigned_thread_id{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus create_status = thread_scheduler.CreateKernelThread(
        kernel_stack_slot_index, thread_index, assigned_thread_id);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (create_status != ThreadSchedulerStatus::Succeeded) {
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
            KernelStackManagerStatus::Succeeded) {
            return KernelThreadRuntimeStatus::StackFailure;
        }
        return create_status == ThreadSchedulerStatus::ThreadCapacityExhausted
                   ? KernelThreadRuntimeStatus::CapacityExhausted
                   : KernelThreadRuntimeStatus::SchedulerFailure;
    }
    if (thread_index >= process_runtime_limits.thread_capacity ||
        runtime_threads[thread_index].active) {
        const bool rollback_interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_status =
            thread_scheduler.DiscardReadyThread(thread_index);
        scheduler_lock.Unlock(rollback_interrupts_were_enabled);
        const KernelStackManagerStatus stack_status =
            GetKernelStackManager().TryDestroy(kernel_stack_slot_index);
        return discard_status == ThreadSchedulerStatus::Succeeded &&
                       stack_status == KernelStackManagerStatus::Succeeded
                   ? KernelThreadRuntimeStatus::SchedulerFailure
                   : KernelThreadRuntimeStatus::StackFailure;
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread = ProcessRuntimeThread{};
    runtime_thread.extended_state = initial_extended_state;
    runtime_thread.kernel_stack_pointer = kernel_stack_pointer;
    runtime_thread.kernel_entry_operation = entry_operation;
    runtime_thread.kernel_entry_context = context;
    runtime_thread.active = true;
    ++kernel_thread_runtime_statistics.create_count;
    ++kernel_thread_runtime_statistics.active_thread_count;
    if (kernel_thread_runtime_statistics.active_thread_count >
        kernel_thread_runtime_statistics.peak_active_thread_count) {
        kernel_thread_runtime_statistics.peak_active_thread_count =
            kernel_thread_runtime_statistics.active_thread_count;
    }
    thread_id = assigned_thread_id;
    return KernelThreadRuntimeStatus::Succeeded;
}

[[nodiscard]] bool ReapExitedKernelThreads() noexcept {
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        if (!runtime_threads[thread_index].active ||
            runtime_threads[thread_index].kernel_entry_operation == nullptr) {
            continue;
        }
        ThreadEntry thread{};
        if (thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (thread.kind != ThreadKind::Kernel || thread.state != ThreadState::Exited) {
            continue;
        }
        if (kernel_thread_runtime_statistics.active_thread_count ==
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
            kernel_thread_runtime_statistics.reap_count == UINT64_MAX) {
            return false;
        }
        const uint64_t kernel_stack_slot_index = thread.kernel_stack_slot_index;
        const bool interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus reap_status = thread_scheduler.ReapExitedThread(thread_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (reap_status != ThreadSchedulerStatus::Succeeded ||
            GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded) {
            return false;
        }
        runtime_threads[thread_index] = ProcessRuntimeThread{};
        --kernel_thread_runtime_statistics.active_thread_count;
        ++kernel_thread_runtime_statistics.reap_count;
    }
    return true;
}

[[nodiscard]] bool DiscardReadyKernelThreads() noexcept {
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        if (!runtime_threads[thread_index].active ||
            runtime_threads[thread_index].kernel_entry_operation == nullptr) {
            continue;
        }
        ThreadEntry thread{};
        if (thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
            thread.kind != ThreadKind::Kernel || thread.state != ThreadState::Ready ||
            kernel_thread_runtime_statistics.active_thread_count ==
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            return false;
        }
        const bool interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_status =
            thread_scheduler.DiscardReadyThread(thread_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (discard_status != ThreadSchedulerStatus::Succeeded ||
            GetKernelStackManager().TryDestroy(thread.kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded) {
            return false;
        }
        runtime_threads[thread_index] = ProcessRuntimeThread{};
        --kernel_thread_runtime_statistics.active_thread_count;
    }
    return true;
}

void KernelThreadSelfTestFirst(void *const context) noexcept {
    static_cast<void>(context);
    if (kernel_thread_self_test_step != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        HaltProcessor();
    }
    ++kernel_thread_self_test_step;
    if (YieldCurrentKernelThread() != KernelThreadRuntimeStatus::Succeeded ||
        kernel_thread_self_test_step !=
            OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_SECOND_STEP) {
        HaltProcessor();
    }
    ++kernel_thread_self_test_step;
    WakeReason wake_reason = WakeReason::None;
    if (BlockCurrentKernelThread(kernel_thread_test_wait_queue, WaitCondition::TestCondition,
                                 wake_reason) != KernelThreadRuntimeStatus::Succeeded ||
        wake_reason != WakeReason::ConditionSatisfied ||
        kernel_thread_self_test_step !=
            OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_FOURTH_STEP) {
        HaltProcessor();
    }
    ++kernel_thread_self_test_step;
}

void KernelThreadSelfTestSecond(void *const context) noexcept {
    static_cast<void>(context);
    if (kernel_thread_self_test_step != OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) {
        HaltProcessor();
    }
    ++kernel_thread_self_test_step;
    if (YieldCurrentKernelThread() != KernelThreadRuntimeStatus::Succeeded ||
        kernel_thread_self_test_step !=
            OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_THIRD_STEP) {
        HaltProcessor();
    }
    ++kernel_thread_self_test_step;
    bool wake_won = false;
    if (WakeOneKernelThread(kernel_thread_test_wait_queue, WakeReason::ConditionSatisfied,
                            wake_won) != KernelThreadRuntimeStatus::Succeeded ||
        !wake_won) {
        HaltProcessor();
    }
}

[[nodiscard]] bool RunKernelThreadLifecycleSelfTest() noexcept {
    kernel_thread_self_test_step = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    ThreadId first_thread_id{};
    ThreadId second_thread_id{};
    if (CreateKernelThreadCore(KernelThreadSelfTestFirst, nullptr, first_thread_id) !=
        KernelThreadRuntimeStatus::Succeeded) {
        return false;
    }
    if (CreateKernelThreadCore(KernelThreadSelfTestSecond, nullptr, second_thread_id) !=
        KernelThreadRuntimeStatus::Succeeded) {
        static_cast<void>(DiscardReadyKernelThreads());
        return false;
    }
    if (first_thread_id.value < OS_KERNEL_THREAD_FIRST_KERNEL_IDENTIFIER ||
        second_thread_id.value !=
            first_thread_id.value + OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT ||
        ExecuteReadyKernelThreads() != KernelThreadRuntimeStatus::Succeeded) {
        return false;
    }
    const ThreadSchedulerStatistics scheduler_statistics = thread_scheduler.Statistics();
    return kernel_thread_self_test_step ==
               OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_FINAL_STEP &&
           kernel_thread_runtime_statistics.active_thread_count ==
               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           kernel_thread_runtime_statistics.peak_active_thread_count ==
               OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_COUNT &&
           kernel_thread_runtime_statistics.create_count ==
               OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_COUNT &&
           kernel_thread_runtime_statistics.yield_count ==
               OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_COUNT &&
           kernel_thread_runtime_statistics.block_count ==
               OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT &&
           kernel_thread_runtime_statistics.wake_count ==
               OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT &&
           kernel_thread_runtime_statistics.exit_count ==
               OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_COUNT &&
           kernel_thread_runtime_statistics.reap_count ==
               OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_SELF_TEST_COUNT &&
           scheduler_statistics.owned_process_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           scheduler_statistics.owned_thread_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           thread_scheduler.Validate() == ThreadSchedulerStatus::Succeeded &&
           thread_scheduler.ValidateWaitQueue(kernel_thread_test_wait_queue) ==
               ThreadSchedulerStatus::Succeeded &&
           GetKernelStackManager().Validate() == KernelStackManagerStatus::Succeeded &&
           GetKernelStackManager().Statistics().active_stack_count ==
               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
}

[[nodiscard]] WorkExecutionResult ExecuteKernelWorkQueueSelfTestWork(void *const context) noexcept {
    if (context == nullptr) {
        return WorkExecutionResult::Failed;
    }
    const KernelWorkQueueSelfTestContext &work_context =
        *static_cast<const KernelWorkQueueSelfTestContext *>(context);
    if (work_context.expected_execution_order != kernel_work_queue_execution_order ||
        kernel_work_queue_execution_order == UINT64_MAX ||
        CurrentSpinLockDepth() != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        HaltProcessor();
    }
    ++kernel_work_queue_execution_order;
    return work_context.fail ? WorkExecutionResult::Failed : WorkExecutionResult::Succeeded;
}

void ExecuteKernelWorkQueueWorker(void *const context) noexcept {
    static_cast<void>(context);
    uint64_t now_nanoseconds = OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_INITIAL_TIME_NANOSECONDS;
    while (!kernel_work_queue.DrainComplete()) {
        WorkExecution execution{};
        const WorkQueueStatus acquire_status =
            kernel_work_queue.AcquireNext(now_nanoseconds, execution);
        if (acquire_status == WorkQueueStatus::NoReadyWork) {
            now_nanoseconds = OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_DELAY_TIME_NANOSECONDS;
            continue;
        }
        if (acquire_status != WorkQueueStatus::Succeeded || execution.operation == nullptr) {
            HaltProcessor();
        }
        const WorkExecutionResult result = execution.operation(execution.context);
        if (kernel_work_queue.Complete(execution.handle, result) != WorkQueueStatus::Succeeded) {
            HaltProcessor();
        }
    }
}

[[nodiscard]] bool RunKernelWorkQueueLifecycleSelfTest() noexcept {
    if (kernel_work_queue.Validate() != WorkQueueStatus::Succeeded) {
        return false;
    }
    kernel_work_queue_execution_order = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    for (uint64_t work_index = OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FIRST_INDEX;
         work_index < OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_REGISTERED_COUNT; ++work_index) {
        kernel_work_queue_contexts[work_index] = KernelWorkQueueSelfTestContext{
            .expected_execution_order = work_index,
            .fail = work_index == OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FAILURE_INDEX,
        };
    }
    WorkHandle handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_REGISTERED_COUNT]{};
    for (uint64_t work_index = OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FIRST_INDEX;
         work_index < OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_REGISTERED_COUNT; ++work_index) {
        if (kernel_work_queue.Register(ExecuteKernelWorkQueueSelfTestWork,
                                       &kernel_work_queue_contexts[work_index],
                                       handles[work_index]) != WorkQueueStatus::Succeeded) {
            return false;
        }
    }
    if (kernel_work_queue.Queue(handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FIRST_INDEX]) !=
            WorkQueueStatus::Succeeded ||
        kernel_work_queue.Queue(handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FIRST_INDEX]) !=
            WorkQueueStatus::AlreadyPending ||
        kernel_work_queue.Queue(handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FAILURE_INDEX]) !=
            WorkQueueStatus::Succeeded ||
        kernel_work_queue.Queue(handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_THIRD_INDEX]) !=
            WorkQueueStatus::Succeeded ||
        kernel_work_queue.QueueDelayed(
            handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_DELAYED_INDEX],
            OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_DELAY_TIME_NANOSECONDS) !=
            WorkQueueStatus::Succeeded ||
        kernel_work_queue.Queue(handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_CANCELLED_INDEX]) !=
            WorkQueueStatus::Succeeded ||
        kernel_work_queue.Cancel(handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_CANCELLED_INDEX]) !=
            WorkQueueStatus::Succeeded ||
        kernel_work_queue.BeginDrain() != WorkQueueStatus::Succeeded ||
        kernel_work_queue.Queue(handles[OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_IDLE_INDEX]) !=
            WorkQueueStatus::DrainInProgress) {
        return false;
    }
    ThreadId worker_thread_id{};
    if (CreateKernelThreadCore(ExecuteKernelWorkQueueWorker, nullptr, worker_thread_id) !=
            KernelThreadRuntimeStatus::Succeeded ||
        worker_thread_id.value < OS_KERNEL_THREAD_FIRST_KERNEL_IDENTIFIER ||
        ExecuteReadyKernelThreads() != KernelThreadRuntimeStatus::Succeeded ||
        kernel_work_queue_execution_order != OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_EXECUTION_COUNT ||
        !kernel_work_queue.DrainComplete() ||
        kernel_work_queue.EndDrain() != WorkQueueStatus::Succeeded) {
        return false;
    }
    for (uint64_t work_index = OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_FIRST_INDEX;
         work_index <= OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_CANCELLED_INDEX; ++work_index) {
        if (kernel_work_queue.Reset(handles[work_index]) != WorkQueueStatus::Succeeded) {
            return false;
        }
    }
    for (const WorkHandle handle : handles) {
        if (kernel_work_queue.Release(handle) != WorkQueueStatus::Succeeded) {
            return false;
        }
    }
    const WorkQueueStatistics statistics = kernel_work_queue.Statistics();
    return kernel_work_queue.Validate() == WorkQueueStatus::Succeeded &&
           statistics.registered_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
           statistics.registration_count == OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_REGISTERED_COUNT &&
           statistics.release_count == OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_REGISTERED_COUNT &&
           statistics.immediate_queue_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_IMMEDIATE_COUNT &&
           statistics.delayed_queue_count == OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_DELAYED_COUNT &&
           statistics.coalesced_queue_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT &&
           statistics.delayed_promotion_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT &&
           statistics.acquisition_count == OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_EXECUTION_COUNT &&
           statistics.completion_count == OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_EXECUTION_COUNT &&
           statistics.failed_execution_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT &&
           statistics.cancellation_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT &&
           statistics.reset_count == OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_RESET_COUNT &&
           statistics.drain_begin_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT &&
           statistics.drain_end_count == OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT &&
           statistics.drain_rejection_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT &&
           statistics.peak_registered_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_REGISTERED_COUNT &&
           statistics.peak_pending_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_PEAK_PENDING_COUNT &&
           statistics.peak_running_count ==
               OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_SINGLE_EVENT_COUNT &&
           !statistics.draining;
}

[[nodiscard]] bool ObserveFileCacheEntryForAging(void *const context,
                                                 const FilePageCacheEntry &entry) noexcept {
    PageAgingObservationContext *const observation =
        static_cast<PageAgingObservationContext *>(context);
    if (observation == nullptr || entry.state == FilePageCacheEntryState::Empty ||
        entry.state == FilePageCacheEntryState::Loading) {
        return entry.state == FilePageCacheEntryState::Loading;
    }
    const bool reclaim_eligible =
        entry.state == FilePageCacheEntryState::Clean &&
        entry.mapping_reference_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    observation->failure_status =
        page_aging_manager.Observe(entry.physical_address, PageAgingKind::File, false,
                                   reclaim_eligible, entry.access_generation);
    return observation->failure_status == PageAgingStatus::Succeeded;
}

[[nodiscard]] PageAgingKind ClassifyAgingPage(const VirtualMemoryArea &area,
                                              const PageMapping &mapping) noexcept {
    if (area.kind == VirtualMemoryAreaKind::ExecutableImage ||
        area.kind == VirtualMemoryAreaKind::FileShared ||
        (area.kind == VirtualMemoryAreaKind::FilePrivate &&
         (!area.permissions.writable || mapping.permissions.copy_on_write))) {
        return PageAgingKind::File;
    }
    return PageAgingKind::Anonymous;
}

[[nodiscard]] PageAgingStatus
ObserveProcessPagesForAging(const ProcessRuntimeProcess &process) noexcept {
    if (!process.active) {
        return PageAgingStatus::Succeeded;
    }
    // Zombie 在父进程 wait 前仍保留结果槽，但退出路径已经销毁 CR3/VMA。
    if (process.address_space.root_physical_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return PageAgingStatus::Succeeded;
    }
    if (process.address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return PageAgingStatus::CorruptedState;
    }
    const bool init_process =
        process.result.process_id == OS_KERNEL_PROCESS_RUNTIME_INIT_PROCESS_ID;
    for (uint64_t area_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         area_index < process.address_space.virtual_memory_map.AreaCount(); ++area_index) {
        VirtualMemoryArea area{};
        if (process.address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return PageAgingStatus::CorruptedState;
        }
        for (uint64_t page_address = area.begin_address; page_address < area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            PageMapping mapping{};
            bool accessed = false;
            const PageTableStatus sample_status = TestAndClearAddressSpacePageAccessed(
                process.address_space.root_physical_address, page_address, mapping, accessed);
            if (sample_status == PageTableStatus::NotMapped) {
                continue;
            }
            if (sample_status != PageTableStatus::Succeeded ||
                mapping.page_size_bytes != OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
                !mapping.permissions.user_accessible) {
                return PageAgingStatus::CorruptedState;
            }
            const PageAgingKind kind = ClassifyAgingPage(area, mapping);
            const bool reclaim_eligible = kind == PageAgingKind::Anonymous && !init_process &&
                                          area.kind != VirtualMemoryAreaKind::UserStack &&
                                          !mapping.permissions.copy_on_write;
            const PageAgingStatus observe_status = page_aging_manager.Observe(
                mapping.physical_address, kind, accessed, reclaim_eligible);
            if (observe_status != PageAgingStatus::Succeeded) {
                WriteProcessRuntimeValue(
                    OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_PROCESS_ID_PREFIX,
                    process.result.process_id);
                WriteProcessRuntimeValue(
                    OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_VIRTUAL_ADDRESS_PREFIX,
                    page_address);
                WriteProcessRuntimeValue(
                    OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_AREA_KIND_PREFIX,
                    static_cast<uint64_t>(area.kind));
                WriteProcessRuntimeValue(
                    OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FAILURE_COPY_ON_WRITE_PREFIX,
                    mapping.permissions.copy_on_write ? OS_KERNEL_PROCESS_RUNTIME_BOOLEAN_TRUE
                                                      : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
                return observe_status;
            }
        }
    }
    return PageAgingStatus::Succeeded;
}

[[nodiscard]] WorkExecutionResult ExecuteRuntimePageAgingWork(void *const context) noexcept {
    static_cast<void>(context);
    if (AnyUserKernelContinuationActive()) {
        return WorkExecutionResult::Succeeded;
    }
    const PageAgingStatus begin_status = page_aging_manager.BeginObservation();
    if (begin_status != PageAgingStatus::Succeeded) {
        RecordPageAgingFailure(PageAgingFailureStage::BeginObservation, begin_status);
        return WorkExecutionResult::Failed;
    }
    PageAgingObservationContext observation{
        .failure_status = PageAgingStatus::Succeeded,
    };
    const UserVirtualMemoryStatus file_cache_status =
        VisitUserFilePageCache(&observation, ObserveFileCacheEntryForAging);
    if (file_cache_status != UserVirtualMemoryStatus::Succeeded) {
        RecordPageAgingFailure(PageAgingFailureStage::FileCacheObservation,
                               observation.failure_status);
        static_cast<void>(page_aging_manager.CancelObservation());
        return WorkExecutionResult::Failed;
    }
    PageAgingStatus process_observation_status = PageAgingStatus::Succeeded;
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < process_runtime_limits.process_capacity; ++process_index) {
        const ProcessRuntimeProcess &process = runtime_processes[process_index];
        if (process.active) {
            process_observation_status = ObserveProcessPagesForAging(process);
            if (process_observation_status != PageAgingStatus::Succeeded) {
                break;
            }
        }
    }
    if (process_observation_status != PageAgingStatus::Succeeded) {
        RecordPageAgingFailure(PageAgingFailureStage::ProcessPageObservation,
                               process_observation_status);
        static_cast<void>(page_aging_manager.CancelObservation());
        return WorkExecutionResult::Failed;
    }
    const PageAgingStatus end_status = page_aging_manager.EndObservation();
    if (end_status != PageAgingStatus::Succeeded) {
        RecordPageAgingFailure(PageAgingFailureStage::EndObservation, end_status);
        return WorkExecutionResult::Failed;
    }
    const PageAgingStatus validation_status = page_aging_manager.Validate();
    if (validation_status != PageAgingStatus::Succeeded) {
        RecordPageAgingFailure(PageAgingFailureStage::Validation, validation_status);
        return WorkExecutionResult::Failed;
    }
    if (!ScheduleRuntimeBackgroundReclaimWork()) {
        background_reclaim_worker_failed = true;
        return WorkExecutionResult::Failed;
    }
    return WorkExecutionResult::Succeeded;
}

[[nodiscard]] bool PageIsReclaimCandidate(const PageAgingEntrySnapshot &entry) noexcept {
    return entry.reclaim_candidate;
}

[[nodiscard]] bool SelectFilePageForBackgroundReclaim(void *const context,
                                                      const FilePageCacheEntry &entry,
                                                      bool &selected) noexcept {
    selected = false;
    if (context == nullptr) {
        return false;
    }
    BackgroundReclaimSelectionContext &selection =
        *static_cast<BackgroundReclaimSelectionContext *>(context);
    PageAgingEntrySnapshot aging_entry{};
    const PageAgingStatus status =
        page_aging_manager.Read(entry.physical_address, PageAgingKind::File, aging_entry);
    if (status == PageAgingStatus::EntryNotFound) {
        return true;
    }
    if (status != PageAgingStatus::Succeeded) {
        selection.failure_status = status;
        return false;
    }
    selected = PageIsReclaimCandidate(aging_entry) &&
               aging_entry.identity_generation == entry.access_generation;
    return true;
}

[[nodiscard]] bool CompleteFilePageBackgroundReclaim(void *const context,
                                                     const FilePageCacheEntry &entry) noexcept {
    if (context == nullptr) {
        return false;
    }
    BackgroundReclaimSelectionContext &selection =
        *static_cast<BackgroundReclaimSelectionContext *>(context);
    selection.failure_status =
        page_aging_manager.Forget(entry.physical_address, PageAgingKind::File);
    return selection.failure_status == PageAgingStatus::Succeeded;
}

[[nodiscard]] bool SelectAnonymousPageForBackgroundReclaim(void *const context,
                                                           const uint64_t physical_address,
                                                           bool &selected) noexcept {
    selected = false;
    if (context == nullptr) {
        return false;
    }
    BackgroundReclaimSelectionContext &selection =
        *static_cast<BackgroundReclaimSelectionContext *>(context);
    PageAgingEntrySnapshot aging_entry{};
    const PageAgingStatus status =
        page_aging_manager.Read(physical_address, PageAgingKind::Anonymous, aging_entry);
    if (status == PageAgingStatus::EntryNotFound) {
        return true;
    }
    if (status != PageAgingStatus::Succeeded) {
        selection.failure_status = status;
        return false;
    }
    selected = PageIsReclaimCandidate(aging_entry);
    return true;
}

[[nodiscard]] bool
CompleteAnonymousPageBackgroundReclaim(void *const context,
                                       const uint64_t physical_address) noexcept {
    if (context == nullptr) {
        return false;
    }
    BackgroundReclaimSelectionContext &selection =
        *static_cast<BackgroundReclaimSelectionContext *>(context);
    selection.failure_status =
        page_aging_manager.Forget(physical_address, PageAgingKind::Anonymous);
    return selection.failure_status == PageAgingStatus::Succeeded;
}

[[nodiscard]] WorkExecutionResult
ExecuteRuntimeBackgroundReclaimWork(void *const context) noexcept {
    static_cast<void>(context);
    if (AnyUserKernelContinuationActive()) {
        return WorkExecutionResult::Succeeded;
    }
    const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
    const MemoryPressureStatistics pressure_statistics = GetUserMemoryPressureStatistics();
    BackgroundReclaimDecision decision{};
    if (background_reclaim_controller.Evaluate(
            pressure_statistics.watermarks, pressure_statistics.resident_page_count,
            now_nanoseconds, decision) != BackgroundReclaimStatus::Succeeded) {
        background_reclaim_worker_failed = true;
        return WorkExecutionResult::Failed;
    }
    if (decision.action != BackgroundReclaimAction::Reclaim) {
        return WorkExecutionResult::Succeeded;
    }

    if (process_vfs != nullptr) {
        // 先收缩逻辑条目；首次压力批次还会切到 compact hash 并归还 preferred backing 页。
        fs::NamespaceCacheReclaimResult namespace_reclaim{};
        if (process_vfs->ReclaimNamespaceCache(
                OS_KERNEL_PROCESS_RUNTIME_BACKGROUND_RECLAIM_BATCH_PAGE_COUNT,
                OS_KERNEL_PROCESS_RUNTIME_BACKGROUND_RECLAIM_BATCH_PAGE_COUNT,
                namespace_reclaim) != fs::Status::Succeeded) {
            background_reclaim_worker_failed = true;
            return WorkExecutionResult::Failed;
        }
    }

    BackgroundReclaimSelectionContext selection{
        .failure_status = PageAgingStatus::Succeeded,
    };
    const PageAgingStatistics aging_statistics = page_aging_manager.Statistics();
    const FilePageCacheStatistics cache_statistics = GetUserFilePageCacheStatistics();
    uint64_t non_clean_file_page_count = cache_statistics.loading_page_count;
    bool failed = non_clean_file_page_count > UINT64_MAX - cache_statistics.dirty_page_count;
    if (!failed) {
        non_clean_file_page_count += cache_statistics.dirty_page_count;
        failed = non_clean_file_page_count > UINT64_MAX - cache_statistics.writeback_page_count;
    }
    if (!failed) {
        non_clean_file_page_count += cache_statistics.writeback_page_count;
        failed = non_clean_file_page_count > UINT64_MAX - cache_statistics.error_page_count;
    }
    if (!failed) {
        non_clean_file_page_count += cache_statistics.error_page_count;
        failed = non_clean_file_page_count > cache_statistics.resident_page_count;
    }
    const uint64_t clean_file_page_count =
        failed ? OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE
               : cache_statistics.resident_page_count - non_clean_file_page_count;
    const uint64_t clean_file_candidate_page_count =
        clean_file_page_count < aging_statistics.file_reclaim_candidate_count
            ? clean_file_page_count
            : aging_statistics.file_reclaim_candidate_count;
    uint64_t dirty_file_page_count = cache_statistics.dirty_page_count;
    failed = failed || dirty_file_page_count > UINT64_MAX - cache_statistics.error_page_count;
    if (!failed) {
        dirty_file_page_count += cache_statistics.error_page_count;
    }
    const SwapManagerStatistics swap_statistics = GetUserSwapStatistics();
    const uint64_t free_swap_page_count =
        swap_statistics.active_slot_count <= swap_statistics.slot_capacity
            ? swap_statistics.slot_capacity - swap_statistics.active_slot_count
            : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    failed = failed || swap_statistics.active_slot_count > swap_statistics.slot_capacity;
    MemoryReclaimPlan plan{};
    if (!failed &&
        PlanMemoryReclaim(
            MemoryReclaimInput{
                .target_page_count = decision.target_page_count,
                .clean_file_page_count = clean_file_candidate_page_count,
                .dirty_file_page_count = dirty_file_page_count,
                .anonymous_page_count = aging_statistics.anonymous_reclaim_candidate_count,
                .free_swap_page_count = free_swap_page_count,
                .swappiness = GetUserMemorySwappiness(),
            },
            plan) != MemoryReclaimPlanStatus::Succeeded) {
        failed = true;
    }
    uint64_t reclaimed_clean_file_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    uint64_t written_file_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    uint64_t swapped_anonymous_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!failed && plan.clean_file_page_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        failed = ReclaimSelectedUserFilePages(
                     plan.clean_file_page_count, &selection, SelectFilePageForBackgroundReclaim,
                     CompleteFilePageBackgroundReclaim,
                     reclaimed_clean_file_page_count) != UserVirtualMemoryStatus::Succeeded;
    }
    if (failed || selection.failure_status != PageAgingStatus::Succeeded) {
        background_reclaim_worker_failed = true;
    }
    if (!failed && plan.writeback_file_page_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        failed =
            !ProtectRuntimeSharedFileMappings() ||
            RequestUserFileWriteback() != UserVirtualMemoryStatus::Succeeded ||
            RunUserFileWritebackWorker(plan.writeback_file_page_count, written_file_page_count) !=
                UserVirtualMemoryStatus::Succeeded;
    }
    if (!failed && plan.swap_out_page_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        failed = !ReclaimRuntimeAnonymousPagesInternal(
            nullptr, plan.swap_out_page_count, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, &selection,
            SelectAnonymousPageForBackgroundReclaim, CompleteAnonymousPageBackgroundReclaim,
            swapped_anonymous_page_count);
        if (selection.failure_status != PageAgingStatus::Succeeded) {
            background_reclaim_worker_failed = true;
        }
    }
    uint64_t reclaimed_page_count = reclaimed_clean_file_page_count;
    if (reclaimed_page_count > UINT64_MAX - swapped_anonymous_page_count) {
        failed = true;
    } else {
        reclaimed_page_count += swapped_anonymous_page_count;
    }
    if (!RecordUserBackgroundMemoryReclaim(reclaimed_clean_file_page_count, written_file_page_count,
                                           swapped_anonymous_page_count)) {
        background_reclaim_worker_failed = true;
        failed = true;
    }
    const BackgroundReclaimStatus record_status = background_reclaim_controller.RecordBatch(
        BackgroundReclaimBatchResult{
            .requested_page_count = decision.target_page_count,
            .clean_file_page_count = reclaimed_clean_file_page_count,
            .swapped_anonymous_page_count = swapped_anonymous_page_count,
            .reclaimed_page_count = reclaimed_page_count,
            .written_page_count = written_file_page_count,
            .failed = failed,
        },
        now_nanoseconds);
    if (record_status != BackgroundReclaimStatus::Succeeded) {
        background_reclaim_worker_failed = true;
        return WorkExecutionResult::Failed;
    }
    return failed ? WorkExecutionResult::Failed : WorkExecutionResult::Succeeded;
}

[[nodiscard]] bool
QueueRuntimeBackgroundReclaimDecision(const BackgroundReclaimDecision &decision) noexcept {
    if (decision.action == BackgroundReclaimAction::Sleep) {
        return true;
    }
    WorkQueueEntry entry{};
    if (kernel_work_queue.Read(background_reclaim_work_handle, entry) !=
        WorkQueueStatus::Succeeded) {
        return false;
    }
    if (entry.state == WorkState::Completed || entry.state == WorkState::Cancelled) {
        if (kernel_work_queue.Reset(background_reclaim_work_handle) != WorkQueueStatus::Succeeded) {
            return false;
        }
    }
    const WorkQueueStatus queue_status =
        decision.action == BackgroundReclaimAction::Reclaim
            ? kernel_work_queue.Queue(background_reclaim_work_handle)
            : kernel_work_queue.QueueDelayed(background_reclaim_work_handle,
                                             decision.deadline_nanoseconds);
    if (queue_status == WorkQueueStatus::AlreadyPending ||
        queue_status == WorkQueueStatus::AlreadyRunning) {
        return true;
    }
    if (queue_status != WorkQueueStatus::Succeeded) {
        return false;
    }
    bool wake_won = false;
    if (WakeOneKernelThread(kernel_work_wait_queue, WakeReason::ConditionSatisfied, wake_won) !=
        KernelThreadRuntimeStatus::Succeeded) {
        return false;
    }
    GetCpuLocal().RequestReschedule();
    return true;
}

[[nodiscard]] bool ScheduleRuntimeBackgroundReclaimWork() noexcept {
    if (!background_reclaim_work_registered || kernel_work_thread_stop_requested) {
        return kernel_work_thread_stop_requested;
    }
    const MemoryPressureStatistics pressure_statistics = GetUserMemoryPressureStatistics();
    BackgroundReclaimDecision decision{};
    if (background_reclaim_controller.Evaluate(
            pressure_statistics.watermarks, pressure_statistics.resident_page_count,
            GetMonotonicNanoseconds(), decision) != BackgroundReclaimStatus::Succeeded) {
        return false;
    }
    return QueueRuntimeBackgroundReclaimDecision(decision);
}

[[nodiscard]] bool RequestRuntimeBackgroundReclaim(void *const context) noexcept {
    static_cast<void>(context);
    return IsProcessSchedulingActive() && background_reclaim_work_registered &&
           !kernel_work_thread_stop_requested && ScheduleRuntimeBackgroundReclaimWork();
}

[[nodiscard]] WorkExecutionResult ExecuteRuntimeFileWritebackWork(void *const context) noexcept {
    static_cast<void>(context);
    if (AnyUserKernelContinuationActive()) {
        return WorkExecutionResult::Succeeded;
    }
    if (!UserFileWritebackWorkerRequested()) {
        return WorkExecutionResult::Succeeded;
    }
    if (!ProtectRuntimeSharedFileMappings()) {
        return WorkExecutionResult::Failed;
    }
    uint64_t written_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    return RunUserFileWritebackWorker(written_page_count) == UserVirtualMemoryStatus::Succeeded
               ? WorkExecutionResult::Succeeded
               : WorkExecutionResult::Failed;
}

[[nodiscard]] bool
ReleaseRuntimeCancelledReadaheadRequests(const uint64_t cancelled_request_count) noexcept {
    if (cancelled_request_count > OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_CAPACITY) {
        return false;
    }
    bool released = true;
    for (uint64_t request_index = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
         request_index < cancelled_request_count; ++request_index) {
        FileReadaheadRequest &request = file_readahead_cancelled_requests[request_index];
        const bool file_closed = request.vfs != nullptr &&
                                 request.vfs->Close(request.open_file) == fs::Status::Succeeded;
        const bool task_released = file_readahead_feedback_ledger.ReleaseTask(request.stream) ==
                                   FileReadaheadFeedbackStatus::Succeeded;
        if (!file_closed || !task_released) {
            released = false;
        }
        request = FileReadaheadRequest{};
    }
    return released;
}

[[nodiscard]] bool RegisterRuntimeFileReadaheadStream(void *const context,
                                                      const FileCacheIdentity &identity,
                                                      FileReadaheadStreamToken &stream) noexcept {
    static_cast<void>(context);
    return file_readahead_feedback_ledger.RegisterStream(identity, stream) ==
           FileReadaheadFeedbackStatus::Succeeded;
}

[[nodiscard]] bool TakeRuntimeFileReadaheadFeedback(void *const context,
                                                    const FileReadaheadStreamToken stream,
                                                    FileReadaheadFeedback &feedback) noexcept {
    static_cast<void>(context);
    return file_readahead_feedback_ledger.Take(stream, feedback) ==
           FileReadaheadFeedbackStatus::Succeeded;
}

[[nodiscard]] bool
RetireRuntimeFileReadaheadStream(void *const context,
                                 const FileReadaheadStreamToken stream) noexcept {
    static_cast<void>(context);
    return file_readahead_feedback_ledger.RetireStream(stream) ==
           FileReadaheadFeedbackStatus::Succeeded;
}

[[nodiscard]] bool
RecordRuntimeFileReadaheadFeedback(void *const context, const FileReadaheadPageTag &tag,
                                   const FileReadaheadFeedback &feedback) noexcept {
    static_cast<void>(context);
    return FileReadaheadPageTagIsValid(tag) &&
           file_readahead_feedback_ledger.Record(tag.stream, feedback) ==
               FileReadaheadFeedbackStatus::Succeeded;
}

[[nodiscard]] bool
CancelRuntimeFileReadaheadStream(void *const context, const FileReadaheadStreamToken stream,
                                 const uint64_t maximum_policy_generation) noexcept {
    static_cast<void>(context);
    uint64_t cancelled_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (file_readahead_request_queue.CancelStream(
            stream, maximum_policy_generation, file_readahead_cancelled_requests,
            OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_CAPACITY,
            cancelled_request_count) != FileReadaheadRequestStatus::Succeeded ||
        !ReleaseRuntimeCancelledReadaheadRequests(cancelled_request_count)) {
        return false;
    }
    uint64_t discarded_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    return DiscardUserFilePrefetchedPages(stream, maximum_policy_generation,
                                          discarded_page_count) ==
           UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] bool CancelRuntimeFileReadaheadFile(const FileCacheIdentity &identity) noexcept {
    uint64_t cancelled_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    return file_readahead_request_queue.CancelFile(
               identity, file_readahead_cancelled_requests,
               OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_CAPACITY,
               cancelled_request_count) == FileReadaheadRequestStatus::Succeeded &&
           ReleaseRuntimeCancelledReadaheadRequests(cancelled_request_count);
}

[[nodiscard]] bool ContinueRuntimeFileReadahead(void *const context,
                                                bool &continue_readahead) noexcept {
    continue_readahead = false;
    if (context == nullptr) {
        return false;
    }
    const FileReadaheadRequestToken token =
        *static_cast<const FileReadaheadRequestToken *>(context);
    bool cancellation_requested = false;
    if (file_readahead_request_queue.CancellationRequested(token, cancellation_requested) !=
        FileReadaheadRequestStatus::Succeeded) {
        return false;
    }
    continue_readahead = !cancellation_requested;
    return true;
}

[[nodiscard]] bool ReadRuntimeFileReadaheadPressure(void *const context,
                                                    MemoryPressureLevel &pressure_level) noexcept {
    static_cast<void>(context);
    if (!process_runtime_initialized || !process_scheduling_active ||
        kernel_work_thread_stop_requested) {
        pressure_level = MemoryPressureLevel::BelowMinimum;
        return false;
    }
    pressure_level = GetUserMemoryPressureLevel();
    return true;
}

[[nodiscard]] bool ScheduleRuntimeFileReadahead(void *const context, fs::Vfs &vfs,
                                                const fs::OpenFile &open_file,
                                                const FileReadaheadStreamToken stream,
                                                const FileReadaheadDecision &decision) noexcept {
    static_cast<void>(context);
    if (!process_runtime_initialized || !process_scheduling_active ||
        kernel_work_thread_stop_requested || !file_readahead_work_registered ||
        decision.action != FileReadaheadAction::Submit ||
        decision.prefetch_page_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        !FileReadaheadStreamTokenIsValid(stream)) {
        return false;
    }
    if (file_readahead_feedback_ledger.RetainTask(stream) !=
        FileReadaheadFeedbackStatus::Succeeded) {
        return false;
    }
    fs::OpenFile retained_open_file{};
    if (vfs.RetainOpenFile(open_file, retained_open_file) != fs::Status::Succeeded) {
        if (file_readahead_feedback_ledger.ReleaseTask(stream) !=
            FileReadaheadFeedbackStatus::Succeeded) {
            HaltProcessor();
        }
        return false;
    }
    FileReadaheadRequestToken token{};
    const FileReadaheadRequestStatus enqueue_status = file_readahead_request_queue.Enqueue(
        FileReadaheadRequest{
            .vfs = &vfs,
            .open_file = retained_open_file,
            .start_page_index = decision.prefetch_start_page_index,
            .page_count = decision.prefetch_page_count,
            .policy_generation = decision.generation,
            .stream = stream,
        },
        token);
    static_cast<void>(token);
    if (enqueue_status != FileReadaheadRequestStatus::Succeeded) {
        if (vfs.Close(retained_open_file) != fs::Status::Succeeded ||
            file_readahead_feedback_ledger.ReleaseTask(stream) !=
                FileReadaheadFeedbackStatus::Succeeded) {
            HaltProcessor();
        }
        return false;
    }
    if (!ScheduleRuntimeFileReadaheadWork()) {
        // 入队后丢失唯一 drain 通知会泄漏 retained OpenFile，不能作为普通预测失败返回。
        HaltProcessor();
    }
    return true;
}

[[nodiscard]] WorkExecutionResult ExecuteRuntimeFileReadaheadWork(void *const context) noexcept {
    static_cast<void>(context);
    FileReadaheadRequestToken token{};
    FileReadaheadRequest request{};
    const FileReadaheadRequestStatus acquire_status =
        file_readahead_request_queue.Acquire(token, request);
    if (acquire_status == FileReadaheadRequestStatus::NoQueuedRequest) {
        return WorkExecutionResult::Succeeded;
    }
    if (acquire_status != FileReadaheadRequestStatus::Succeeded || request.vfs == nullptr) {
        file_readahead_worker_failed = true;
        WriteProcessRuntimeValue(
            OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_FAILURE_STAGE_PREFIX,
            static_cast<uint64_t>(FileReadaheadWorkerFailureStage::AcquireRequest));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_STATUS_PREFIX,
                                 static_cast<uint64_t>(acquire_status));
        return WorkExecutionResult::Failed;
    }
    UserFileReadaheadResult result{};
    file_readahead_worker_io_active = true;
    const UserVirtualMemoryStatus prefetch_status = PrefetchUserFilePages(
        *request.vfs, request.open_file, request.start_page_index, request.page_count,
        FileReadaheadPageTag{
            .stream = request.stream,
            .policy_generation = request.policy_generation,
        },
        UserFileReadaheadControl{
            .context = &token,
            .continue_operation = ContinueRuntimeFileReadahead,
        },
        result);
    file_readahead_worker_io_active = false;
    bool cancellation_requested = false;
    const FileReadaheadRequestStatus cancellation_status =
        file_readahead_request_queue.CancellationRequested(token, cancellation_requested);
    uint64_t discarded_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const UserVirtualMemoryStatus discard_status =
        cancellation_status == FileReadaheadRequestStatus::Succeeded &&
                (result.cancelled || cancellation_requested)
            ? DiscardUserFilePrefetchedPages(request.stream, request.policy_generation,
                                             discarded_page_count)
            : UserVirtualMemoryStatus::Succeeded;
    const fs::Status close_status = request.vfs->Close(request.open_file);
    const FileReadaheadRequestStatus completion_status =
        file_readahead_request_queue.Complete(token);
    const FileReadaheadFeedbackStatus task_release_status =
        file_readahead_feedback_ledger.ReleaseTask(request.stream);
    if (prefetch_status != UserVirtualMemoryStatus::Succeeded ||
        cancellation_status != FileReadaheadRequestStatus::Succeeded ||
        discard_status != UserVirtualMemoryStatus::Succeeded ||
        close_status != fs::Status::Succeeded ||
        completion_status != FileReadaheadRequestStatus::Succeeded ||
        task_release_status != FileReadaheadFeedbackStatus::Succeeded) {
        file_readahead_worker_failed = true;
        const FileReadaheadWorkerFailureStage failure_stage =
            prefetch_status != UserVirtualMemoryStatus::Succeeded
                ? FileReadaheadWorkerFailureStage::Prefetch
            : close_status != fs::Status::Succeeded
                ? FileReadaheadWorkerFailureStage::CloseFile
                : FileReadaheadWorkerFailureStage::CompleteRequest;
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_FAILURE_STAGE_PREFIX,
                                 static_cast<uint64_t>(failure_stage));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_PREFETCH_STATUS_PREFIX,
                                 static_cast<uint64_t>(prefetch_status));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_CLOSE_STATUS_PREFIX,
                                 static_cast<uint64_t>(close_status));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_COMPLETION_STATUS_PREFIX,
                                 static_cast<uint64_t>(completion_status));
        return WorkExecutionResult::Failed;
    }
    return WorkExecutionResult::Succeeded;
}

[[nodiscard]] bool ScheduleRuntimeFileReadaheadWork() noexcept {
    if (!file_readahead_work_registered || kernel_work_thread_stop_requested) {
        return false;
    }
    WorkQueueEntry entry{};
    if (kernel_work_queue.Read(file_readahead_work_handle, entry) != WorkQueueStatus::Succeeded) {
        return false;
    }
    if (entry.state == WorkState::Completed || entry.state == WorkState::Cancelled) {
        if (kernel_work_queue.Reset(file_readahead_work_handle) != WorkQueueStatus::Succeeded) {
            return false;
        }
    }
    const WorkQueueStatus queue_status = kernel_work_queue.Queue(file_readahead_work_handle);
    if (queue_status != WorkQueueStatus::Succeeded &&
        queue_status != WorkQueueStatus::AlreadyPending &&
        queue_status != WorkQueueStatus::AlreadyRunning) {
        return false;
    }
    bool wake_won = false;
    if (WakeOneKernelThread(kernel_work_wait_queue, WakeReason::ConditionSatisfied, wake_won) !=
        KernelThreadRuntimeStatus::Succeeded) {
        return false;
    }
    GetCpuLocal().RequestReschedule();
    return true;
}

[[nodiscard]] bool WorkHandlesEqual(const WorkHandle left, const WorkHandle right) noexcept {
    return left.slot_index == right.slot_index && left.generation == right.generation;
}

[[nodiscard]] bool WakeRuntimeBlockIoOwner(const uint64_t owner_thread_index) noexcept {
    ThreadEntry owner_thread{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    bool wake_won = false;
    const ThreadSchedulerStatus read_status =
        thread_scheduler.ReadThread(owner_thread_index, owner_thread);
    if (read_status == ThreadSchedulerStatus::Succeeded &&
        owner_thread.kind == ThreadKind::Kernel &&
        kernel_thread_runtime_statistics.wake_count == UINT64_MAX) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return false;
    }
    const ThreadSchedulerStatus wake_status =
        read_status == ThreadSchedulerStatus::Succeeded &&
                (owner_thread.kind == ThreadKind::User || owner_thread.kind == ThreadKind::Kernel)
            ? thread_scheduler.WakeThread(block_io_wait_queue, owner_thread_index,
                                          WakeReason::ConditionSatisfied, wake_won)
            : ThreadSchedulerStatus::InvalidThreadIndex;
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (wake_status != ThreadSchedulerStatus::Succeeded || !wake_won) {
        return false;
    }
    if (owner_thread.kind == ThreadKind::Kernel) {
        ++kernel_thread_runtime_statistics.wake_count;
    }
    GetCpuLocal().RequestReschedule();
    return true;
}

[[nodiscard]] bool ServiceRuntimeBlockIoCompletions(bool &made_progress) noexcept {
    made_progress = false;
    for (uint64_t device_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         device_index < block_io_device_count; ++device_index) {
        AsynchronousBlockDevice *const device = block_io_devices[device_index];
        if (device == nullptr) {
            return false;
        }
        for (uint64_t completion_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
             completion_index < OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_CAPACITY; ++completion_index) {
            BlockCompletion completion{};
            bool available = false;
            if (device->TakeCompletion(completion, available) !=
                AsynchronousBlockDeviceStatus::Succeeded) {
                return false;
            }
            if (!available) {
                break;
            }
            made_progress = true;
            BlockIoCompletionDecision decision{};
            const bool completion_interrupts_were_enabled = DisableInterrupts();
            const BlockIoStatus coordinator_status = block_io_coordinator.Complete(
                completion.owner_thread_index, completion.request_identifier, completion.result,
                decision);
            RestoreInterrupts(completion_interrupts_were_enabled);
            if (coordinator_status == BlockIoStatus::RequestNotFound) {
                const ProcessRuntimeStatus legacy_status =
                    CompleteBlockIoRequest(completion.owner_thread_index,
                                           completion.request_identifier, completion.result);
                if (legacy_status != ProcessRuntimeStatus::Succeeded &&
                    legacy_status != ProcessRuntimeStatus::BlockIoRequestAbandoned) {
                    return false;
                }
                continue;
            }
            if (coordinator_status != BlockIoStatus::Succeeded ||
                (decision.wake_required && !WakeRuntimeBlockIoOwner(decision.owner_thread_index))) {
                return false;
            }
        }
    }
    return true;
}

void RuntimeBlockIoCompletionWorker(void *const context) noexcept {
    static_cast<void>(context);
    while (true) {
        const uint64_t observed_notification_generation =
            block_io_completion_notification_generation;
        bool made_progress = false;
        if (!ServiceRuntimeBlockIoCompletions(made_progress)) {
            HaltProcessor();
        }
        const BlockIoStatistics statistics = block_io_coordinator.Statistics();
        if (block_io_completion_worker_stop_requested && block_io_runtime_probe_succeeded &&
            statistics.active_request_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            return;
        }
        if (made_progress) {
            continue;
        }
        const bool interrupts_were_enabled = DisableInterrupts();
        if (block_io_completion_notification_generation != observed_notification_generation) {
            RestoreInterrupts(interrupts_were_enabled);
            continue;
        }
        WakeReason wake_reason = WakeReason::None;
        if (BlockCurrentKernelThread(block_io_completion_wait_queue,
                                     WaitCondition::BlockIoCompletion,
                                     wake_reason) != KernelThreadRuntimeStatus::Succeeded ||
            (wake_reason != WakeReason::ConditionSatisfied &&
             wake_reason != WakeReason::Cancelled)) {
            RestoreInterrupts(interrupts_were_enabled);
            HaltProcessor();
        }
        RestoreInterrupts(interrupts_were_enabled);
    }
}

void RuntimeBlockIoProbeWorker(void *const context) noexcept {
    static_cast<void>(context);
    block_io_runtime_probe_wait_active = true;
    block_io_runtime_probe_succeeded =
        AwaitRuntimeBlockIo(GetRuntimeAtaSwapAsynchronousBlockDevice(), BlockOperation::Flush,
                            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, nullptr,
                            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                            OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_PROBE_TIMEOUT_NANOSECONDS) ==
        RuntimeBlockIoStatus::Succeeded;
    block_io_runtime_probe_wait_active = false;
    if (!block_io_runtime_probe_succeeded) {
        HaltProcessor();
    }
}

void RuntimeFileWritebackWorker(void *const context) noexcept {
    static_cast<void>(context);
    while (!kernel_work_thread_stop_requested) {
        const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
        WorkExecution execution{};
        const WorkQueueStatus acquire_status =
            kernel_work_queue.AcquireNext(now_nanoseconds, execution);
        if (acquire_status == WorkQueueStatus::Succeeded) {
            if (execution.operation == nullptr ||
                kernel_work_queue.Complete(execution.handle,
                                           execution.operation(execution.context)) !=
                    WorkQueueStatus::Succeeded ||
                kernel_work_queue.Reset(execution.handle) != WorkQueueStatus::Succeeded) {
                HaltProcessor();
            }
            if (!kernel_work_thread_stop_requested &&
                WorkHandlesEqual(execution.handle, file_writeback_work_handle) &&
                UserFileWritebackWorkerRequested()) {
                const WorkQueueStatus queue_status =
                    kernel_work_queue.Queue(file_writeback_work_handle);
                if (queue_status != WorkQueueStatus::Succeeded &&
                    queue_status != WorkQueueStatus::AlreadyPending) {
                    HaltProcessor();
                }
            }
            if (!kernel_work_thread_stop_requested && !page_aging_worker_failed &&
                WorkHandlesEqual(execution.handle, page_aging_work_handle)) {
                const uint64_t next_deadline_nanoseconds = GetMonotonicNanoseconds();
                if (next_deadline_nanoseconds >
                        UINT64_MAX - OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_INTERVAL_NANOSECONDS ||
                    kernel_work_queue.QueueDelayed(
                        page_aging_work_handle,
                        next_deadline_nanoseconds +
                            OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_INTERVAL_NANOSECONDS) !=
                        WorkQueueStatus::Succeeded) {
                    HaltProcessor();
                }
            }
            if (!kernel_work_thread_stop_requested && !background_reclaim_worker_failed &&
                WorkHandlesEqual(execution.handle, background_reclaim_work_handle) &&
                !ScheduleRuntimeBackgroundReclaimWork()) {
                HaltProcessor();
            }
            if (!kernel_work_thread_stop_requested && !file_readahead_worker_failed &&
                WorkHandlesEqual(execution.handle, file_readahead_work_handle) &&
                file_readahead_request_queue.Statistics().active_request_count !=
                    OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
                !ScheduleRuntimeFileReadaheadWork()) {
                HaltProcessor();
            }
            // 每个有界 batch 后都交还调度权，避免存储延迟垄断 Ring 0。
            if (!kernel_work_thread_stop_requested &&
                YieldCurrentKernelThread() != KernelThreadRuntimeStatus::Succeeded) {
                HaltProcessor();
            }
            continue;
        }
        if (acquire_status != WorkQueueStatus::NoReadyWork) {
            HaltProcessor();
        }
        uint64_t deadline_nanoseconds = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        bool deadline_available = false;
        if (kernel_work_queue.NextDeadline(deadline_nanoseconds, deadline_available) !=
            WorkQueueStatus::Succeeded) {
            HaltProcessor();
        }
        if (kernel_work_thread_stop_requested) {
            break;
        }
        WakeReason wake_reason = WakeReason::None;
        const KernelThreadRuntimeStatus block_status =
            deadline_available
                ? BlockCurrentKernelThreadUntil(kernel_work_wait_queue, WaitCondition::KernelWork,
                                                deadline_nanoseconds, wake_reason)
                : BlockCurrentKernelThread(kernel_work_wait_queue, WaitCondition::KernelWork,
                                           wake_reason);
        if (block_status != KernelThreadRuntimeStatus::Succeeded ||
            (wake_reason != WakeReason::ConditionSatisfied && wake_reason != WakeReason::Timeout &&
             wake_reason != WakeReason::Cancelled)) {
            HaltProcessor();
        }
    }
}

[[nodiscard]] bool HasLiveUserThread() noexcept {
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        ThreadEntry thread{};
        if (thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded) {
            continue;
        }
        if (thread.kind == ThreadKind::User && thread.state != ThreadState::Unused &&
            thread.state != ThreadState::Exited) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool PrepareRuntimeFileWritebackWorker() noexcept {
    if (file_writeback_work_registered || page_aging_work_registered ||
        background_reclaim_work_registered || file_readahead_work_registered ||
        kernel_work_thread_stop_requested) {
        return false;
    }
    page_aging_worker_failed = false;
    background_reclaim_worker_failed = false;
    file_readahead_worker_failed = false;
    file_readahead_worker_io_active = false;
    if (kernel_work_queue.Register(ExecuteRuntimeFileWritebackWork, nullptr,
                                   file_writeback_work_handle) != WorkQueueStatus::Succeeded) {
        return false;
    }
    if (kernel_work_queue.Queue(file_writeback_work_handle) != WorkQueueStatus::Succeeded) {
        static_cast<void>(kernel_work_queue.Release(file_writeback_work_handle));
        file_writeback_work_handle = WorkHandle{};
        return false;
    }
    file_writeback_work_registered = true;
    const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
    if (now_nanoseconds > UINT64_MAX - OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_INTERVAL_NANOSECONDS ||
        kernel_work_queue.Register(ExecuteRuntimePageAgingWork, nullptr, page_aging_work_handle) !=
            WorkQueueStatus::Succeeded) {
        static_cast<void>(kernel_work_queue.Cancel(file_writeback_work_handle));
        static_cast<void>(kernel_work_queue.Release(file_writeback_work_handle));
        file_writeback_work_registered = false;
        file_writeback_work_handle = WorkHandle{};
        return false;
    }
    if (kernel_work_queue.QueueDelayed(
            page_aging_work_handle,
            now_nanoseconds + OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_INTERVAL_NANOSECONDS) !=
        WorkQueueStatus::Succeeded) {
        static_cast<void>(kernel_work_queue.Release(page_aging_work_handle));
        static_cast<void>(kernel_work_queue.Cancel(file_writeback_work_handle));
        static_cast<void>(kernel_work_queue.Release(file_writeback_work_handle));
        file_writeback_work_registered = false;
        file_writeback_work_handle = WorkHandle{};
        page_aging_work_handle = WorkHandle{};
        return false;
    }
    page_aging_work_registered = true;
    if (kernel_work_queue.Register(ExecuteRuntimeBackgroundReclaimWork, nullptr,
                                   background_reclaim_work_handle) != WorkQueueStatus::Succeeded) {
        static_cast<void>(kernel_work_queue.Cancel(page_aging_work_handle));
        static_cast<void>(kernel_work_queue.Cancel(file_writeback_work_handle));
        static_cast<void>(kernel_work_queue.Release(page_aging_work_handle));
        static_cast<void>(kernel_work_queue.Release(file_writeback_work_handle));
        file_writeback_work_registered = false;
        page_aging_work_registered = false;
        file_writeback_work_handle = WorkHandle{};
        page_aging_work_handle = WorkHandle{};
        return false;
    }
    background_reclaim_work_registered = true;
    if (kernel_work_queue.Register(ExecuteRuntimeFileReadaheadWork, nullptr,
                                   file_readahead_work_handle) != WorkQueueStatus::Succeeded) {
        static_cast<void>(kernel_work_queue.Release(background_reclaim_work_handle));
        static_cast<void>(kernel_work_queue.Cancel(page_aging_work_handle));
        static_cast<void>(kernel_work_queue.Cancel(file_writeback_work_handle));
        static_cast<void>(kernel_work_queue.Release(page_aging_work_handle));
        static_cast<void>(kernel_work_queue.Release(file_writeback_work_handle));
        file_writeback_work_registered = false;
        page_aging_work_registered = false;
        background_reclaim_work_registered = false;
        file_writeback_work_handle = WorkHandle{};
        page_aging_work_handle = WorkHandle{};
        background_reclaim_work_handle = WorkHandle{};
        return false;
    }
    file_readahead_work_registered = true;
    ThreadId block_io_thread_id{};
    ThreadId block_io_probe_thread_id{};
    ThreadId work_thread_id{};
    if (CreateKernelThreadCore(RuntimeBlockIoCompletionWorker, nullptr, block_io_thread_id) !=
            KernelThreadRuntimeStatus::Succeeded ||
        CreateKernelThreadCore(RuntimeBlockIoProbeWorker, nullptr, block_io_probe_thread_id) !=
            KernelThreadRuntimeStatus::Succeeded ||
        CreateKernelThreadCore(RuntimeFileWritebackWorker, nullptr, work_thread_id) !=
            KernelThreadRuntimeStatus::Succeeded) {
        static_cast<void>(DiscardReadyKernelThreads());
        static_cast<void>(kernel_work_queue.Cancel(file_writeback_work_handle));
        static_cast<void>(kernel_work_queue.Cancel(page_aging_work_handle));
        static_cast<void>(kernel_work_queue.Release(file_writeback_work_handle));
        static_cast<void>(kernel_work_queue.Release(page_aging_work_handle));
        static_cast<void>(kernel_work_queue.Release(background_reclaim_work_handle));
        static_cast<void>(kernel_work_queue.Release(file_readahead_work_handle));
        file_writeback_work_registered = false;
        page_aging_work_registered = false;
        background_reclaim_work_registered = false;
        file_readahead_work_registered = false;
        file_writeback_work_handle = WorkHandle{};
        page_aging_work_handle = WorkHandle{};
        background_reclaim_work_handle = WorkHandle{};
        file_readahead_work_handle = WorkHandle{};
        return false;
    }
    if (block_io_thread_id.value < OS_KERNEL_THREAD_FIRST_KERNEL_IDENTIFIER ||
        block_io_probe_thread_id.value !=
            block_io_thread_id.value + OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT ||
        work_thread_id.value !=
            block_io_probe_thread_id.value + OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) {
        HaltProcessor();
    }
    block_io_completion_worker_started = true;
    return true;
}

[[nodiscard]] bool RequestRuntimeFileWritebackWorkerStop() noexcept {
    if (!file_writeback_work_registered || !page_aging_work_registered ||
        !background_reclaim_work_registered || !file_readahead_work_registered) {
        return false;
    }
    kernel_work_thread_stop_requested = true;
    block_io_completion_worker_stop_requested = true;
    const WorkHandle handles[] = {
        file_writeback_work_handle,
        page_aging_work_handle,
        background_reclaim_work_handle,
        file_readahead_work_handle,
    };
    for (const WorkHandle handle : handles) {
        WorkQueueEntry entry{};
        if (kernel_work_queue.Read(handle, entry) != WorkQueueStatus::Succeeded ||
            ((entry.state == WorkState::Queued || entry.state == WorkState::Delayed) &&
             kernel_work_queue.Cancel(handle) != WorkQueueStatus::Succeeded)) {
            return false;
        }
    }
    bool work_wake_won = false;
    bool block_io_wake_won = false;
    return WakeOneKernelThread(kernel_work_wait_queue, WakeReason::Cancelled, work_wake_won) ==
               KernelThreadRuntimeStatus::Succeeded &&
           WakeOneKernelThread(block_io_completion_wait_queue, WakeReason::Cancelled,
                               block_io_wake_won) == KernelThreadRuntimeStatus::Succeeded;
}

[[nodiscard]] bool ReleaseRuntimeFileWritebackWorker() noexcept {
    if (!file_writeback_work_registered || !page_aging_work_registered ||
        !background_reclaim_work_registered || !file_readahead_work_registered ||
        !block_io_completion_worker_started || !block_io_runtime_probe_succeeded) {
        return false;
    }
    const WorkHandle handles[] = {
        file_writeback_work_handle,
        page_aging_work_handle,
        background_reclaim_work_handle,
        file_readahead_work_handle,
    };
    for (const WorkHandle handle : handles) {
        WorkQueueEntry entry{};
        if (kernel_work_queue.Read(handle, entry) != WorkQueueStatus::Succeeded ||
            ((entry.state == WorkState::Completed || entry.state == WorkState::Cancelled) &&
             kernel_work_queue.Reset(handle) != WorkQueueStatus::Succeeded) ||
            kernel_work_queue.Release(handle) != WorkQueueStatus::Succeeded) {
            return false;
        }
    }
    if (page_aging_manager.Reset() != PageAgingStatus::Succeeded ||
        page_aging_manager.Validate() != PageAgingStatus::Succeeded ||
        page_aging_manager.Statistics().tracked_page_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        background_reclaim_controller.Reset() != BackgroundReclaimStatus::Succeeded ||
        background_reclaim_controller.Validate() != BackgroundReclaimStatus::Succeeded ||
        background_reclaim_controller.Statistics().state != BackgroundReclaimState::Sleeping) {
        return false;
    }
    if (file_readahead_request_queue.Validate() != FileReadaheadRequestStatus::Succeeded ||
        file_readahead_request_queue.Statistics().active_request_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        file_readahead_feedback_ledger.Validate() != FileReadaheadFeedbackStatus::Succeeded ||
        file_readahead_feedback_ledger.Statistics().active_stream_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        file_readahead_feedback_ledger.Statistics().retiring_stream_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        file_readahead_feedback_ledger.Statistics().active_task_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        file_readahead_worker_io_active) {
        return false;
    }
    if (block_io_coordinator.Validate() != BlockIoStatus::Succeeded ||
        block_io_coordinator.Statistics().active_request_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        block_io_wait_queue.Statistics().waiting_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        block_io_completion_wait_queue.Statistics().waiting_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        process_io_drain_wait_queue.Statistics().waiting_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    file_writeback_work_registered = false;
    page_aging_work_registered = false;
    background_reclaim_work_registered = false;
    file_readahead_work_registered = false;
    file_writeback_work_handle = WorkHandle{};
    page_aging_work_handle = WorkHandle{};
    background_reclaim_work_handle = WorkHandle{};
    file_readahead_work_handle = WorkHandle{};
    kernel_work_thread_stop_requested = false;
    block_io_completion_worker_stop_requested = false;
    block_io_completion_worker_started = false;
    file_readahead_worker_io_active = false;
    return true;
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
    if (wait_condition == WaitCondition::ChildProcess) {
        return &child_exit_wait_queue;
    }
    if (wait_condition == WaitCondition::ThreadJoin) {
        return &thread_join_wait_queue;
    }
    if (wait_condition == WaitCondition::Sleep) {
        return &sleep_wait_queue;
    }
    if (wait_condition == WaitCondition::BlockIo) {
        return &block_io_wait_queue;
    }
    if (wait_condition == WaitCondition::BlockIoCompletion) {
        return &block_io_completion_wait_queue;
    }
    if (wait_condition == WaitCondition::ProcessIoDrain) {
        return &process_io_drain_wait_queue;
    }
    if (wait_condition == WaitCondition::KernelWork) {
        return &kernel_work_wait_queue;
    }
    return nullptr;
}

void WakeRequiredThreads(const WaitCondition wait_condition,
                         const WakeReason wake_reason) noexcept {
    uint64_t woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (WakeThreads(wait_condition, wake_reason, OS_KERNEL_PROCESS_RUNTIME_WAKE_ALL_COUNT,
                    woken_thread_count) != ProcessRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
}

[[nodiscard]] bool ReadCurrentThreadAndProcess(ThreadEntry &thread,
                                               ProcessEntry &process) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    return thread_index < process_runtime_limits.thread_capacity &&
           thread_scheduler.ReadThread(thread_index, thread) == ThreadSchedulerStatus::Succeeded &&
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

[[nodiscard]] bool WriteConsoleDevice(void *const context, const uint8_t *const source,
                                      const uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept {
    static_cast<void>(context);
    written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (source == nullptr && length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    const VgaTextConsole vga_console{VgaTextConsole::Hardware(WritePort8)};
    while (written_bytes < length_bytes) {
        if (!vga_console.TryWriteTerminalByte(static_cast<char>(source[written_bytes]))) {
            return false;
        }
        ++written_bytes;
    }
    return true;
}

void EchoTerminalBytes(const uint8_t *const source, const uint64_t length_bytes) noexcept {
    uint64_t written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (process_terminal.TryWrite(source, length_bytes, WriteConsoleDevice, nullptr,
                                  written_bytes) != TerminalStatus::Succeeded ||
        written_bytes != length_bytes) {
        HaltProcessor();
    }
}

[[nodiscard]] bool CreateAndInstallInitialDescription(ProcessRuntimeProcess &process,
                                                      const FileDescriptionKind kind,
                                                      const uint64_t file_status_flags,
                                                      const uint64_t descriptor) noexcept {
    const bool terminal_output =
        kind == FileDescriptionKind::TerminalOutput || kind == FileDescriptionKind::TerminalError;
    const bool pipe_endpoint =
        kind == FileDescriptionKind::PipeReader || kind == FileDescriptionKind::PipeWriter;
    const FileDescriptionCreateRequest request{
        .kind = kind,
        .file_status_flags = file_status_flags,
        .terminal = kind == FileDescriptionKind::TerminalInput || terminal_output
                        ? &process_terminal
                        : nullptr,
        .device_write_operation = terminal_output ? WriteConsoleDevice : nullptr,
        .device_write_context = nullptr,
        .pipe = pipe_endpoint ? &process_pipe : nullptr,
        .pipe_manager = nullptr,
        .vfs = nullptr,
        .open_file = {},
    };
    KernelObjectReference reference{};
    return file_description_manager.Create(request, reference) ==
               FileDescriptionStatus::Succeeded &&
           process.file_table.InstallExact(reference, descriptor,
                                           OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) ==
               FileTableStatus::Succeeded;
}

[[nodiscard]] bool InitializeProcessFileTable(ProcessRuntimeProcess &process,
                                              const UserProgramSelection selection) noexcept {
    if (process.file_table.Initialize(GetKernelHeap(), kernel_object_manager,
                                      process_runtime_limits.file_descriptor_hard_limit,
                                      process_runtime_limits.file_descriptor_hard_limit) !=
        FileTableStatus::Succeeded) {
        return false;
    }
    const bool standard_descriptions_installed =
        CreateAndInstallInitialDescription(process, FileDescriptionKind::TerminalInput,
                                           OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
                                           OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR) &&
        CreateAndInstallInitialDescription(process, FileDescriptionKind::TerminalOutput,
                                           OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
                                           OS_KERNEL_FILE_TABLE_STANDARD_OUTPUT_DESCRIPTOR) &&
        CreateAndInstallInitialDescription(process, FileDescriptionKind::TerminalError,
                                           OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
                                           OS_KERNEL_FILE_TABLE_STANDARD_ERROR_DESCRIPTOR);
    bool pipe_description_installed = true;
    if (standard_descriptions_installed && selection == UserProgramSelection::IpcConsumer) {
        pipe_description_installed =
            CreateAndInstallInitialDescription(process, FileDescriptionKind::PipeReader,
                                               OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
                                               OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR);
    } else if (standard_descriptions_installed && selection == UserProgramSelection::IpcProducer) {
        pipe_description_installed =
            CreateAndInstallInitialDescription(process, FileDescriptionKind::PipeWriter,
                                               OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
                                               OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR);
    }
    if (standard_descriptions_installed && pipe_description_installed) {
        return true;
    }
    static_cast<void>(process.file_table.Destroy());
    return false;
}

[[nodiscard]] bool ReadUserProcessString(const uint64_t vector_address, const uint64_t string_index,
                                         os::abi::ProcessString &string) noexcept {
    if (vector_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        string_index > (UINT64_MAX - vector_address) / sizeof(os::abi::ProcessString)) {
        return false;
    }
    const uint64_t descriptor_address =
        vector_address + string_index * sizeof(os::abi::ProcessString);
    return CopyFromUser(descriptor_address, sizeof(string), reinterpret_cast<uint8_t *>(&string),
                        sizeof(string)) == UserMemoryCopyStatus::Succeeded;
}

[[nodiscard]] ProcessRuntimeStatus
PlanUserProgramArguments(const os::abi::ProcessLaunchRequest &request) noexcept {
    if (request.argument_count > os::abi::OS_ABI_PROCESS_MAXIMUM_ARGUMENT_COUNT ||
        request.environment_count > os::abi::OS_ABI_PROCESS_MAXIMUM_ENVIRONMENT_COUNT) {
        return ProcessRuntimeStatus::ArgumentListTooLarge;
    }
    if ((request.argument_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
         request.argument_vector_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) ||
        (request.environment_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
         request.environment_vector_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE)) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    program_argument_plan.Reset();
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < request.argument_count; ++argument_index) {
        os::abi::ProcessString string{};
        if (!ReadUserProcessString(request.argument_vector_address, argument_index, string) ||
            (string.length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             !IsUserVirtualAddressRange(string.address, string.length_bytes))) {
            return ProcessRuntimeStatus::InvalidArguments;
        }
        const ProgramArgumentStatus plan_status =
            program_argument_plan.AddArgument(string.length_bytes);
        if (plan_status != ProgramArgumentStatus::Succeeded) {
            return plan_status == ProgramArgumentStatus::TooManyArguments ||
                           plan_status == ProgramArgumentStatus::StringTooLarge ||
                           plan_status == ProgramArgumentStatus::TotalSizeTooLarge
                       ? ProcessRuntimeStatus::ArgumentListTooLarge
                       : ProcessRuntimeStatus::InvalidArguments;
        }
    }
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < request.environment_count; ++environment_index) {
        os::abi::ProcessString string{};
        if (!ReadUserProcessString(request.environment_vector_address, environment_index, string) ||
            (string.length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             !IsUserVirtualAddressRange(string.address, string.length_bytes))) {
            return ProcessRuntimeStatus::InvalidArguments;
        }
        const ProgramArgumentStatus plan_status =
            program_argument_plan.AddEnvironment(string.length_bytes);
        if (plan_status != ProgramArgumentStatus::Succeeded) {
            return plan_status == ProgramArgumentStatus::TooManyEnvironmentEntries ||
                           plan_status == ProgramArgumentStatus::StringTooLarge ||
                           plan_status == ProgramArgumentStatus::TotalSizeTooLarge
                       ? ProcessRuntimeStatus::ArgumentListTooLarge
                       : ProcessRuntimeStatus::InvalidArguments;
        }
    }
    return ProcessRuntimeStatus::Succeeded;
}

[[nodiscard]] ProcessRuntimeStatus PlanKernelProgramArguments(
    const KernelProgramString *const arguments, const uint64_t argument_count,
    const KernelProgramString *const environment, const uint64_t environment_count) noexcept {
    if (argument_count > OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ARGUMENT_COUNT ||
        environment_count > OS_KERNEL_PROGRAM_ARGUMENT_MAXIMUM_ENVIRONMENT_COUNT) {
        return ProcessRuntimeStatus::ArgumentListTooLarge;
    }
    if ((argument_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE && arguments == nullptr) ||
        (environment_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE && environment == nullptr)) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    program_argument_plan.Reset();
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < argument_count; ++argument_index) {
        if (arguments[argument_index].length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            arguments[argument_index].data == nullptr) {
            return ProcessRuntimeStatus::InvalidArguments;
        }
        if (program_argument_plan.AddArgument(arguments[argument_index].length_bytes) !=
            ProgramArgumentStatus::Succeeded) {
            return ProcessRuntimeStatus::ArgumentListTooLarge;
        }
    }
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < environment_count; ++environment_index) {
        if (environment[environment_index].length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            environment[environment_index].data == nullptr) {
            return ProcessRuntimeStatus::InvalidArguments;
        }
        if (program_argument_plan.AddEnvironment(environment[environment_index].length_bytes) !=
            ProgramArgumentStatus::Succeeded) {
            return ProcessRuntimeStatus::ArgumentListTooLarge;
        }
    }
    return ProcessRuntimeStatus::Succeeded;
}

[[nodiscard]] bool WriteCandidateBytes(UserAddressSpace &address_space,
                                       const uint64_t destination_address,
                                       const uint8_t *const source,
                                       const uint64_t length_bytes) noexcept {
    return CopyToUserAddressSpace(address_space, destination_address, length_bytes, source,
                                  length_bytes) == UserMemoryCopyStatus::Succeeded;
}

[[nodiscard]] bool WriteCandidateValue(UserAddressSpace &address_space,
                                       const uint64_t destination_address,
                                       const uint64_t value) noexcept {
    return WriteCandidateBytes(address_space, destination_address,
                               reinterpret_cast<const uint8_t *>(&value), sizeof(value));
}

[[nodiscard]] bool WriteProgramArgumentMetadata(UserAddressSpace &address_space) noexcept {
    if (!program_argument_plan.IsFinalized() ||
        program_argument_plan.Validate() != ProgramArgumentStatus::Succeeded) {
        return false;
    }
    const ProgramArgumentLayout &layout = program_argument_plan.Layout();
    if (!WriteCandidateValue(address_space, layout.stack_pointer, layout.argument_count)) {
        return false;
    }
    uint64_t metadata_address = layout.argument_vector_address;
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < program_argument_plan.ArgumentCount(); ++argument_index) {
        uint64_t string_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t string_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (program_argument_plan.ReadArgument(argument_index, string_length_bytes,
                                               string_address) !=
                ProgramArgumentStatus::Succeeded ||
            !WriteCandidateValue(address_space, metadata_address, string_address)) {
            return false;
        }
        metadata_address += sizeof(uint64_t);
    }
    if (!WriteCandidateValue(address_space, metadata_address,
                             OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE)) {
        return false;
    }
    metadata_address = layout.environment_vector_address;
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < program_argument_plan.EnvironmentCount(); ++environment_index) {
        uint64_t string_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t string_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (program_argument_plan.ReadEnvironment(environment_index, string_length_bytes,
                                                  string_address) !=
                ProgramArgumentStatus::Succeeded ||
            !WriteCandidateValue(address_space, metadata_address, string_address)) {
            return false;
        }
        metadata_address += sizeof(uint64_t);
    }
    return WriteCandidateValue(address_space, metadata_address,
                               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}

[[nodiscard]] bool CopyUserStringToCandidate(UserAddressSpace &address_space,
                                             const os::abi::ProcessString &string,
                                             const uint64_t destination_address) noexcept {
    uint8_t transfer_buffer[OS_KERNEL_PROCESS_RUNTIME_ARGUMENT_COPY_CHUNK_SIZE_BYTES]{};
    uint64_t copied_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    while (copied_bytes < string.length_bytes) {
        const uint64_t remaining_bytes = string.length_bytes - copied_bytes;
        const uint64_t chunk_bytes =
            remaining_bytes < OS_KERNEL_PROCESS_RUNTIME_ARGUMENT_COPY_CHUNK_SIZE_BYTES
                ? remaining_bytes
                : OS_KERNEL_PROCESS_RUNTIME_ARGUMENT_COPY_CHUNK_SIZE_BYTES;
        if (CopyFromUser(string.address + copied_bytes, chunk_bytes, transfer_buffer,
                         sizeof(transfer_buffer)) != UserMemoryCopyStatus::Succeeded ||
            !WriteCandidateBytes(address_space, destination_address + copied_bytes, transfer_buffer,
                                 chunk_bytes)) {
            return false;
        }
        copied_bytes += chunk_bytes;
    }
    return WriteCandidateBytes(address_space, destination_address + string.length_bytes,
                               &OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR,
                               sizeof(OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR));
}

[[nodiscard]] bool PopulateUserProgramArguments(const os::abi::ProcessLaunchRequest &request,
                                                UserAddressSpace &address_space) noexcept {
    if (program_argument_plan.Finalize(OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS,
                                       OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) !=
            ProgramArgumentStatus::Succeeded ||
        PrepareUserStack(address_space, program_argument_plan.Layout().stack_pointer) !=
            UserAddressSpaceStatus::Succeeded) {
        return false;
    }
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < request.argument_count; ++argument_index) {
        os::abi::ProcessString string{};
        uint64_t planned_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t destination_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (!ReadUserProcessString(request.argument_vector_address, argument_index, string) ||
            program_argument_plan.ReadArgument(argument_index, planned_length_bytes,
                                               destination_address) !=
                ProgramArgumentStatus::Succeeded ||
            planned_length_bytes != string.length_bytes ||
            !CopyUserStringToCandidate(address_space, string, destination_address)) {
            return false;
        }
    }
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < request.environment_count; ++environment_index) {
        os::abi::ProcessString string{};
        uint64_t planned_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t destination_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (!ReadUserProcessString(request.environment_vector_address, environment_index, string) ||
            program_argument_plan.ReadEnvironment(environment_index, planned_length_bytes,
                                                  destination_address) !=
                ProgramArgumentStatus::Succeeded ||
            planned_length_bytes != string.length_bytes ||
            !CopyUserStringToCandidate(address_space, string, destination_address)) {
            return false;
        }
    }
    return WriteProgramArgumentMetadata(address_space);
}

[[nodiscard]] bool PopulateKernelProgramArguments(const KernelProgramString *const arguments,
                                                  const uint64_t argument_count,
                                                  const KernelProgramString *const environment,
                                                  const uint64_t environment_count,
                                                  UserAddressSpace &address_space) noexcept {
    if (program_argument_plan.Finalize(OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS,
                                       OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) !=
            ProgramArgumentStatus::Succeeded ||
        PrepareUserStack(address_space, program_argument_plan.Layout().stack_pointer) !=
            UserAddressSpaceStatus::Succeeded) {
        return false;
    }
    for (uint64_t argument_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         argument_index < argument_count; ++argument_index) {
        uint64_t planned_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t destination_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (program_argument_plan.ReadArgument(argument_index, planned_length_bytes,
                                               destination_address) !=
                ProgramArgumentStatus::Succeeded ||
            planned_length_bytes != arguments[argument_index].length_bytes ||
            (planned_length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             !WriteCandidateBytes(address_space, destination_address,
                                  arguments[argument_index].data, planned_length_bytes)) ||
            !WriteCandidateBytes(address_space, destination_address + planned_length_bytes,
                                 &OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR,
                                 sizeof(OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR))) {
            return false;
        }
    }
    for (uint64_t environment_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         environment_index < environment_count; ++environment_index) {
        uint64_t planned_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t destination_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (program_argument_plan.ReadEnvironment(environment_index, planned_length_bytes,
                                                  destination_address) !=
                ProgramArgumentStatus::Succeeded ||
            planned_length_bytes != environment[environment_index].length_bytes ||
            (planned_length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             !WriteCandidateBytes(address_space, destination_address,
                                  environment[environment_index].data, planned_length_bytes)) ||
            !WriteCandidateBytes(address_space, destination_address + planned_length_bytes,
                                 &OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR,
                                 sizeof(OS_KERNEL_PROCESS_RUNTIME_STRING_TERMINATOR))) {
            return false;
        }
    }
    return WriteProgramArgumentMetadata(address_space);
}

[[nodiscard]] ProcessRuntimeStatus
LoadExecutableFromPath(fs::FsContext &file_system_context, const uint8_t *const path,
                       const uint64_t path_length_bytes, UserAddressSpace &address_space,
                       UserElfValidationStatus &elf_validation_status,
                       UserAddressSpaceStatus &address_space_status,
                       fs::NodeInformation &executable_information) noexcept {
    address_space = UserAddressSpace{};
    executable_information = fs::NodeInformation{};
    elf_validation_status = UserElfValidationStatus::Succeeded;
    address_space_status = UserAddressSpaceStatus::Succeeded;
    if (process_vfs == nullptr || path == nullptr ||
        path_length_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        path_length_bytes > fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    const fs::Status stat_status =
        process_vfs->Stat(file_system_context, path, path_length_bytes, executable_information);
    if (stat_status != fs::Status::Succeeded ||
        executable_information.type != fs::NodeType::RegularFile) {
        return ProcessRuntimeStatus::ExecutableReadFailure;
    }
    fs::OpenFile open_file{};
    const fs::Status open_status =
        process_vfs->OpenExecutable(file_system_context, path, path_length_bytes, open_file);
    if (open_status != fs::Status::Succeeded) {
        return ProcessRuntimeStatus::ExecutableReadFailure;
    }
    address_space_status =
        LoadUserAddressSpace(*process_vfs, open_file, address_space, elf_validation_status);
    const fs::Status close_status = process_vfs->Close(open_file);
    if (close_status != fs::Status::Succeeded) {
        if (address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ExecutableReadFailure;
    }
    if (address_space_status != UserAddressSpaceStatus::Succeeded) {
        if (address_space_status == UserAddressSpaceStatus::InvalidElf) {
            return elf_validation_status == UserElfValidationStatus::ReadFailed
                       ? ProcessRuntimeStatus::ExecutableReadFailure
                       : ProcessRuntimeStatus::InvalidElf;
        }
        return address_space_status == UserAddressSpaceStatus::ImageReadFailed
                   ? ProcessRuntimeStatus::ExecutableReadFailure
                   : ProcessRuntimeStatus::AddressSpaceFailure;
    }
    return ProcessRuntimeStatus::Succeeded;
}

[[nodiscard]] ProcessRuntimeStatus
RegisterRuntimeProcess(UserAddressSpace &address_space, const UserProgramSelection selection,
                       const uint64_t parent_process_index,
                       const fs::NodeInformation *const executable_information,
                       ProcessCreationResult &creation_result) noexcept {
    uint64_t process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    ProcessId process_id{};
    bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus create_process_status = thread_scheduler.CreateProcess(
        address_space.root_physical_address, process_index, process_id);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (create_process_status != ThreadSchedulerStatus::Succeeded) {
        return create_process_status == ThreadSchedulerStatus::ProcessCapacityExhausted
                   ? ProcessRuntimeStatus::ProcessLimitExceeded
                   : ProcessRuntimeStatus::SchedulerFailure;
    }

    uint64_t kernel_stack_slot_index = OS_KERNEL_THREAD_INVALID_INDEX;
    for (uint64_t candidate_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         candidate_index < process_runtime_limits.thread_capacity; ++candidate_index) {
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
        const ThreadSchedulerStatus discard_status = thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (discard_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::KernelStackFailure;
    }

    uint64_t thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    ThreadId thread_id{};
    interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus create_thread_status = thread_scheduler.CreateThread(
        process_index, kernel_stack_slot_index, program_argument_plan.Layout().stack_pointer,
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, thread_index,
        thread_id, false);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (create_thread_status != ThreadSchedulerStatus::Succeeded) {
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_process_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded ||
            discard_process_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    ExceptionFrame *saved_frame = nullptr;
    if (!BuildInitialContextFrame(kernel_stack_slot_index, address_space,
                                  program_argument_plan.Layout(), saved_frame) ||
        InitializeFxSaveArea(runtime_threads[thread_index].extended_state) !=
            ExtendedStateStatus::Succeeded) {
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_thread_status =
            thread_scheduler.DiscardInitializingThread(thread_index);
        const ThreadSchedulerStatus discard_process_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded ||
            discard_thread_status != ThreadSchedulerStatus::Succeeded ||
            discard_process_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ContextFrameFailure;
    }

    ProcessRuntimeProcess &runtime_process = runtime_processes[process_index];
    runtime_process.address_space = address_space;
    runtime_process.result = ProcessExecutionResult{
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
    };
    runtime_process.file_system_context = fs::FsContext{};
    const ProcessRuntimeProcess *const parent_runtime_process =
        parent_process_index != OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX &&
                parent_process_index < process_runtime_limits.process_capacity &&
                runtime_processes[parent_process_index].active
            ? &runtime_processes[parent_process_index]
            : nullptr;
    const bool file_system_context_initialized =
        process_vfs == nullptr ||
        (parent_runtime_process == nullptr
             ? process_vfs->InitializeContext(runtime_process.file_system_context)
             : process_vfs->CloneContext(parent_runtime_process->file_system_context,
                                         runtime_process.file_system_context)) ==
            fs::Status::Succeeded;
    InitializeResourceLimits(runtime_process, parent_runtime_process);
    if (file_system_context_initialized && executable_information != nullptr) {
        ApplyExecutableCredentials(runtime_process.file_system_context, *executable_information);
    }
    const bool file_table_initialized =
        file_system_context_initialized && InitializeProcessFileTable(runtime_process, selection);
    if (file_table_initialized &&
        runtime_process.file_table.SetSoftLimit(
            runtime_process
                .resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::OpenFileCount)]
                .current) != FileTableStatus::Succeeded) {
        HaltProcessor();
    }
    ProcessTreeStatus tree_status = ProcessTreeStatus::InvalidState;
    JobControlStatus job_control_status = JobControlStatus::InvalidProcessId;
    SignalManagerStatus signal_process_status = SignalManagerStatus::InvalidProcessId;
    SignalManagerStatus signal_thread_status = SignalManagerStatus::InvalidThreadId;
    if (file_table_initialized) {
        uint64_t process_group_id = process_id.value;
        if (parent_process_index != OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX &&
            signal_manager.GetProcessGroup(parent_process_index, process_group_id) !=
                SignalManagerStatus::Succeeded) {
            process_group_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        }
        signal_process_status =
            signal_manager.RegisterProcess(process_index, process_id.value, process_group_id);
        if (signal_process_status == SignalManagerStatus::Succeeded) {
            signal_thread_status =
                signal_manager.RegisterThread(thread_index, process_index, thread_id.value,
                                              OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
        }
        if (signal_thread_status == SignalManagerStatus::Succeeded) {
            job_control_status =
                parent_process_index == OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX
                    ? job_control_manager.RegisterInit(process_index, process_id.value)
                    : job_control_manager.ForkProcess(parent_process_index, process_index,
                                                      process_id.value);
        }
        if (job_control_status == JobControlStatus::Succeeded &&
            parent_process_index == OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX) {
            job_control_status = process_terminal.AcquireControllingSession(
                                     process_id.value, process_id.value, process_id.value) ==
                                         TerminalStatus::Succeeded
                                     ? JobControlStatus::Succeeded
                                     : JobControlStatus::PermissionDenied;
        }
        if (job_control_status == JobControlStatus::Succeeded) {
            tree_status = parent_process_index == OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX
                              ? process_tree.RegisterInit(process_index, process_id.value)
                              : process_tree.RegisterChild(process_index, process_id.value,
                                                           parent_process_index);
        }
    }
    if (!file_table_initialized || signal_process_status != SignalManagerStatus::Succeeded ||
        signal_thread_status != SignalManagerStatus::Succeeded ||
        tree_status != ProcessTreeStatus::Succeeded ||
        job_control_status != JobControlStatus::Succeeded) {
        if (job_control_status == JobControlStatus::Succeeded &&
            job_control_manager.RemoveProcess(process_index) != JobControlStatus::Succeeded) {
            HaltProcessor();
        }
        if (signal_thread_status == SignalManagerStatus::Succeeded &&
            signal_manager.RemoveThread(thread_index) != SignalManagerStatus::Succeeded) {
            HaltProcessor();
        }
        if (signal_process_status == SignalManagerStatus::Succeeded &&
            signal_manager.RemoveProcess(process_index) != SignalManagerStatus::Succeeded) {
            HaltProcessor();
        }
        if (file_table_initialized &&
            runtime_process.file_table.Destroy() != FileTableStatus::Succeeded) {
            HaltProcessor();
        }
        if (file_system_context_initialized && process_vfs != nullptr &&
            process_vfs->ReleaseContext(runtime_process.file_system_context) !=
                fs::Status::Succeeded) {
            HaltProcessor();
        }
        interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus discard_thread_status =
            thread_scheduler.DiscardInitializingThread(thread_index);
        const ThreadSchedulerStatus discard_process_status =
            thread_scheduler.DiscardProcess(process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded ||
            discard_thread_status != ThreadSchedulerStatus::Succeeded ||
            discard_process_status != ThreadSchedulerStatus::Succeeded) {
            HaltProcessor();
        }
        runtime_process.address_space = UserAddressSpace{};
        runtime_process.result = ProcessExecutionResult{};
        runtime_process.file_system_context = fs::FsContext{};
        return signal_process_status != SignalManagerStatus::Succeeded ||
                       signal_thread_status != SignalManagerStatus::Succeeded
                   ? ProcessRuntimeStatus::SignalFailure
               : tree_status != ProcessTreeStatus::Succeeded && file_table_initialized
                   ? ProcessRuntimeStatus::ProcessTreeFailure
               : file_system_context_initialized ? ProcessRuntimeStatus::DescriptorTableFailure
                                                 : ProcessRuntimeStatus::FileSystemFailure;
    }

    runtime_process.active = true;
    runtime_threads[thread_index].saved_frame = saved_frame;
    runtime_threads[thread_index].user_stack_base_address =
        OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS;
    runtime_threads[thread_index].user_stack_size_bytes = OS_KERNEL_USER_STACK_SIZE_BYTES;
    runtime_threads[thread_index].thread_local_storage_base = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].thread_local_storage_size_bytes =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].exit_value = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].join_owner_thread_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].blocked_system_call_number =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].block_io_request_identifier =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].user_kernel_continuation_entry_method =
        UserContextEntryMethod::Initial;
    runtime_threads[thread_index].blocked_system_call_restartable = false;
    runtime_threads[thread_index].user_kernel_continuation_active = false;
    runtime_threads[thread_index].user_kernel_continuation_system_call_active = false;
    runtime_threads[thread_index].user_kernel_continuation_swap_gs_required = false;
    runtime_threads[thread_index].user_kernel_continuation_uses_kernel_page_table = false;
    runtime_threads[thread_index].joinable = false;
    runtime_threads[thread_index].active = true;
    interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus publish_thread_status =
        thread_scheduler.PublishInitializingThread(thread_index);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (publish_thread_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
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
    address_space = UserAddressSpace{};
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SPAWN_PREFIX, process_id.value);
    return ProcessRuntimeStatus::Succeeded;
}

[[nodiscard]] bool RollbackForkCreation(UserAddressSpace &parent_address_space,
                                        UserAddressSpace &child_address_space,
                                        const uint64_t process_index, const uint64_t thread_index,
                                        const uint64_t kernel_stack_slot_index,
                                        const bool file_table_initialized,
                                        const bool file_system_context_initialized) noexcept {
    bool rollback_succeeded = true;
    SignalThreadState signal_thread{};
    if (thread_index < process_runtime_limits.thread_capacity &&
        signal_manager.ReadThread(thread_index, signal_thread) == SignalManagerStatus::Succeeded) {
        rollback_succeeded =
            signal_manager.RemoveThread(thread_index) == SignalManagerStatus::Succeeded &&
            rollback_succeeded;
    }
    SignalProcessState signal_process{};
    if (process_index < process_runtime_limits.process_capacity &&
        signal_manager.ReadProcess(process_index, signal_process) ==
            SignalManagerStatus::Succeeded) {
        rollback_succeeded =
            signal_manager.RemoveProcess(process_index) == SignalManagerStatus::Succeeded &&
            rollback_succeeded;
    }
    JobControlProcessState job_state{};
    if (process_index < process_runtime_limits.process_capacity &&
        job_control_manager.ReadProcess(process_index, job_state) == JobControlStatus::Succeeded) {
        rollback_succeeded =
            job_control_manager.RemoveProcess(process_index) == JobControlStatus::Succeeded &&
            rollback_succeeded;
    }
    if (process_index < process_runtime_limits.process_capacity) {
        ProcessRuntimeProcess &process = runtime_processes[process_index];
        if (file_table_initialized) {
            rollback_succeeded =
                process.file_table.Destroy() == FileTableStatus::Succeeded && rollback_succeeded;
        }
        if (file_system_context_initialized && process_vfs != nullptr) {
            rollback_succeeded =
                process_vfs->ReleaseContext(process.file_system_context) == fs::Status::Succeeded &&
                rollback_succeeded;
        }
    }
    bool interrupts_were_enabled = scheduler_lock.Lock();
    if (thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
        rollback_succeeded = thread_scheduler.DiscardInitializingThread(thread_index) ==
                                 ThreadSchedulerStatus::Succeeded &&
                             rollback_succeeded;
    }
    if (process_index != OS_KERNEL_PROCESS_INVALID_INDEX) {
        rollback_succeeded =
            thread_scheduler.DiscardProcess(process_index) == ThreadSchedulerStatus::Succeeded &&
            rollback_succeeded;
    }
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (kernel_stack_slot_index != OS_KERNEL_THREAD_INVALID_INDEX) {
        rollback_succeeded = GetKernelStackManager().TryDestroy(kernel_stack_slot_index) ==
                                 KernelStackManagerStatus::Succeeded &&
                             rollback_succeeded;
    }
    if (child_address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        rollback_succeeded =
            DestroyUserAddressSpace(child_address_space) == UserAddressSpaceStatus::Succeeded &&
            rollback_succeeded;
    }
    rollback_succeeded = RestoreUserAddressSpaceAfterFailedFork(parent_address_space) ==
                             UserAddressSpaceStatus::Succeeded &&
                         rollback_succeeded;
    if (process_index < process_runtime_limits.process_capacity) {
        runtime_processes[process_index].address_space = UserAddressSpace{};
        runtime_processes[process_index].result = ProcessExecutionResult{};
        runtime_processes[process_index].file_system_context = fs::FsContext{};
        runtime_processes[process_index].active = false;
    }
    if (thread_index < process_runtime_limits.thread_capacity) {
        runtime_threads[thread_index] = ProcessRuntimeThread{};
    }
    return rollback_succeeded;
}

void WakeAfterDescriptionRelease(const KernelObjectReleaseResult &release_result) noexcept {
    if (!release_result.released_last_reference ||
        release_result.type != KernelObjectType::FileDescription) {
        return;
    }
    const FileDescriptionKind kind = static_cast<FileDescriptionKind>(release_result.variant);
    if (kind == FileDescriptionKind::PipeReader) {
        WakeRequiredThreads(WaitCondition::DescriptorWritable, WakeReason::ObjectClosed);
    } else if (kind == FileDescriptionKind::PipeWriter) {
        WakeRequiredThreads(WaitCondition::DescriptorReadable, WakeReason::ObjectClosed);
    }
}

void CloseProcessIoDescriptors(ProcessRuntimeProcess &process) noexcept {
    if (process.file_table.Destroy() != FileTableStatus::Succeeded) {
        HaltProcessor();
    }
    WakeRequiredThreads(WaitCondition::DescriptorReadable, WakeReason::ObjectClosed);
    WakeRequiredThreads(WaitCondition::DescriptorWritable, WakeReason::ObjectClosed);
}

[[nodiscard]] bool ReapExitedThreads() noexcept {
    const uint64_t current_stack_pointer = ReadStackPointer();
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        ThreadEntry thread{};
        if (thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
            thread.state != ThreadState::Exited || thread.kind == ThreadKind::Kernel) {
            continue;
        }
        if (thread.kind != ThreadKind::User) {
            return false;
        }
        ProcessEntry process_entry{};
        if (thread.process_index >= process_runtime_limits.process_capacity ||
            thread_scheduler.ReadProcess(thread.process_index, process_entry) !=
                ThreadSchedulerStatus::Succeeded) {
            return false;
        }
        if (runtime_threads[thread_index].joinable && process_entry.state == ProcessState::Alive) {
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
        ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
        process.result.run_tick_count = thread.run_tick_count;
        process.result.dispatch_count = thread.dispatch_count;
        if (!RemoveSignalThreadIfPresent(thread_index)) {
            return false;
        }
        runtime_threads[thread_index].saved_frame = nullptr;
        runtime_threads[thread_index].active = false;

        const bool interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus reap_thread_status =
            thread_scheduler.ReapExitedThread(thread_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (reap_thread_status != ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool CollectTerminalInitProcess() noexcept {
    ProcessTreeWaitResult wait_result{};
    if (process_tree.CollectInit(wait_result) != ProcessTreeStatus::Succeeded ||
        wait_result.process_index >= process_runtime_limits.process_capacity) {
        return false;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus reap_status =
        thread_scheduler.ReapZombieProcess(wait_result.process_index);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (reap_status != ThreadSchedulerStatus::Succeeded ||
        !RemoveSignalProcessIfPresent(wait_result.process_index)) {
        return false;
    }
    if (job_control_manager.RemoveProcess(wait_result.process_index) !=
        JobControlStatus::Succeeded) {
        return false;
    }
    runtime_processes[wait_result.process_index].active = false;
    return true;
}

[[nodiscard]] bool CleanupCapacitySelfTestResources(const ProcessRuntimeLimits limits) noexcept {
    bool cleanup_succeeded = true;
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < limits.thread_capacity; ++thread_index) {
        if (!capacity_stack_active[thread_index]) {
            continue;
        }
        cleanup_succeeded = GetKernelStackManager().TryDestroy(thread_index) ==
                                KernelStackManagerStatus::Succeeded &&
                            cleanup_succeeded;
        capacity_stack_active[thread_index] = false;
    }
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < limits.process_capacity; ++process_index) {
        if (capacity_process_roots[process_index] == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            continue;
        }
        cleanup_succeeded = DestroyUserPageTable(capacity_process_roots[process_index]) ==
                                KernelUserPageStatus::Succeeded &&
                            cleanup_succeeded;
        capacity_process_roots[process_index] = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }
    return cleanup_succeeded;
}

[[nodiscard]] bool RunProcessThreadCapacitySelfTest(const ProcessRuntimeLimits limits) noexcept {
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_CAPACITY_LIMIT; ++process_index) {
        capacity_process_roots[process_index] = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
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
            capacity_process_entries, limits.process_capacity, capacity_thread_entries,
            limits.thread_capacity, limits.maximum_threads_per_process,
            OS_KERNEL_THREAD_DEFAULT_QUANTUM_TICKS) != ThreadSchedulerStatus::Succeeded) {
        return false;
    }

    for (uint64_t process_ordinal = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_ordinal < limits.process_capacity; ++process_ordinal) {
        uint64_t root_physical_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        uint64_t process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
        ProcessId process_id{};
        if (CreateUserPageTable(root_physical_address) != KernelUserPageStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        // 页表根一旦创建就先登记到回滚表；后续调度器校验失败也不能遗失本次资源。
        capacity_process_roots[process_ordinal] = root_physical_address;
        if (capacity_scheduler.CreateProcess(root_physical_address, process_index, process_id) !=
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
        } else if (created_thread_count < limits.maximum_threads_per_process +
                                              limits.process_capacity -
                                              OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) {
            process_index = created_thread_count - limits.maximum_threads_per_process +
                            OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
        } else {
            const uint64_t remaining_process_count =
                limits.process_capacity - OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
            process_index = OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT +
                            (created_thread_count - limits.maximum_threads_per_process -
                             remaining_process_count) %
                                remaining_process_count;
        }

        const uint64_t kernel_stack_slot_index = created_thread_count;
        const uint64_t user_stack_pointer =
            OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_BASE -
            created_thread_count * OS_KERNEL_PROCESS_RUNTIME_CAPACITY_TEST_USER_STACK_STRIDE_BYTES;
        uint64_t thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
        ThreadId thread_id{};
        if (GetKernelStackManager().TryCreate(kernel_stack_slot_index) !=
            KernelStackManagerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        capacity_stack_active[kernel_stack_slot_index] = true;
        if (InitializeFxSaveArea(capacity_thread_extended_states[kernel_stack_slot_index]) !=
                ExtendedStateStatus::Succeeded ||
            capacity_scheduler.CreateThread(
                process_index, kernel_stack_slot_index, user_stack_pointer,
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                thread_index, thread_id) != ThreadSchedulerStatus::Succeeded ||
            thread_index >= limits.thread_capacity ||
            thread_id.value == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        created_thread_count += OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
    }

    const ThreadSchedulerStatistics active_statistics = capacity_scheduler.Statistics();
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
        capacity_scheduler.Start(decision) != ThreadSchedulerStatus::Succeeded) {
        static_cast<void>(CleanupCapacitySelfTestResources(limits));
        return false;
    }
    for (uint64_t terminated_thread_count = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         terminated_thread_count < limits.thread_capacity; ++terminated_thread_count) {
        if (capacity_scheduler.TerminateCurrentThread(decision) !=
            ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }
    if (!decision.completed || capacity_scheduler.Validate() != ThreadSchedulerStatus::Succeeded) {
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
        if (capacity_scheduler.ReapExitedThread(thread_index) != ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }
    for (uint64_t capacity_process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         capacity_process_index < limits.process_capacity; ++capacity_process_index) {
        if (DestroyUserPageTable(capacity_process_roots[capacity_process_index]) !=
            KernelUserPageStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
        capacity_process_roots[capacity_process_index] = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (capacity_scheduler.ReapZombieProcess(capacity_process_index) !=
            ThreadSchedulerStatus::Succeeded) {
            static_cast<void>(CleanupCapacitySelfTestResources(limits));
            return false;
        }
    }

    const ThreadSchedulerStatistics statistics = capacity_scheduler.Statistics();
    const ResourceSnapshotSupplementalCounts final_supplemental_counts{
        .process_count = statistics.owned_process_count,
        .thread_count = statistics.owned_thread_count,
        .file_description_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .vnode_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .cache_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .block_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    if (capacity_scheduler.Validate() != ThreadSchedulerStatus::Succeeded ||
        statistics.owned_process_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        statistics.owned_thread_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        statistics.created_process_count != limits.process_capacity ||
        statistics.created_thread_count != limits.thread_capacity ||
        statistics.reaped_process_count != limits.process_capacity ||
        statistics.reaped_thread_count != limits.thread_capacity ||
        GetKernelResourceSnapshot(final_supplemental_counts, after) !=
            ResourceSnapshotStatus::Succeeded ||
        CompareResourceSnapshots(before, after, difference) != ResourceSnapshotStatus::Succeeded ||
        difference.changed_fields_mask != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        difference.changed_field_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }

    capacity_self_test_process_count = limits.process_capacity;
    capacity_self_test_thread_count = limits.thread_capacity;
    capacity_self_test_threads_per_process = limits.maximum_threads_per_process;
    return true;
}

[[nodiscard]] bool RangesOverlap(const uint64_t left_begin, const uint64_t left_size_bytes,
                                 const uint64_t right_begin,
                                 const uint64_t right_size_bytes) noexcept {
    if (left_size_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        right_size_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        left_begin > UINT64_MAX - left_size_bytes || right_begin > UINT64_MAX - right_size_bytes) {
        return false;
    }
    return left_begin < right_begin + right_size_bytes &&
           right_begin < left_begin + left_size_bytes;
}

[[nodiscard]] bool RemoveSignalThreadIfPresent(const uint64_t thread_index) noexcept {
    SignalThreadState state{};
    const SignalManagerStatus read_status = signal_manager.ReadThread(thread_index, state);
    return read_status == SignalManagerStatus::ThreadNotFound ||
           (read_status == SignalManagerStatus::Succeeded &&
            signal_manager.RemoveThread(thread_index) == SignalManagerStatus::Succeeded);
}

[[nodiscard]] bool RemoveSignalProcessIfPresent(const uint64_t process_index) noexcept {
    SignalProcessState state{};
    const SignalManagerStatus read_status = signal_manager.ReadProcess(process_index, state);
    return read_status == SignalManagerStatus::ProcessNotFound ||
           (read_status == SignalManagerStatus::Succeeded &&
            signal_manager.RemoveProcess(process_index) == SignalManagerStatus::Succeeded);
}

[[nodiscard]] bool WakeThreadForSignal(const uint64_t thread_index) noexcept {
    ThreadEntry thread{};
    if (thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    if (thread.state != ThreadState::Blocked) {
        return thread.state == ThreadState::Ready || thread.state == ThreadState::Running;
    }
    if (thread.wait_queue == nullptr || thread_index >= process_runtime_limits.thread_capacity ||
        runtime_threads[thread_index].saved_frame == nullptr) {
        return false;
    }
    if (thread.wait_condition == WaitCondition::BlockIo) {
        // 块设备请求仍拥有该线程的系统调用返回槽；信号保留到 I/O 完成后再交付。
        return true;
    }
    WaitQueue *const wait_queue = thread.wait_queue;
    const WaitCondition wait_condition = thread.wait_condition;
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    bool wake_won = false;
    const ThreadSchedulerStatus wake_status =
        thread_scheduler.WakeThread(*wait_queue, thread_index, WakeReason::Signal, wake_won);
    if (wake_status != ThreadSchedulerStatus::Succeeded || !wake_won) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return false;
    }
    runtime_threads[thread_index].saved_frame->register_rax =
        static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INTERRUPTED);
    if (wait_condition == WaitCondition::PrivateFutex) {
        bool entry_found = false;
        for (uint64_t entry_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
             entry_index < OS_KERNEL_PRIVATE_FUTEX_CAPACITY_LIMIT; ++entry_index) {
            if (!private_futex_entries[entry_index].active ||
                &private_futex_entries[entry_index].wait_queue != wait_queue) {
                continue;
            }
            entry_found = true;
            if (wait_queue->Statistics().waiting_thread_count ==
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
                bool released = false;
                if (private_futex_manager.ReleaseIfEmpty(entry_index, released) !=
                        PrivateFutexStatus::Succeeded ||
                    !released) {
                    scheduler_lock.Unlock(interrupts_were_enabled);
                    return false;
                }
            }
            break;
        }
        if (!entry_found) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            return false;
        }
    }
    scheduler_lock.Unlock(interrupts_were_enabled);
    return true;
}

[[nodiscard]] bool ContinueStoppedProcess(const uint64_t process_index) noexcept {
    ProcessEntry process{};
    if (thread_scheduler.ReadProcess(process_index, process) != ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    if (process.state != ProcessState::Stopped) {
        return process.state == ProcessState::Alive;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus continue_status = thread_scheduler.ContinueProcess(process_index);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (continue_status != ThreadSchedulerStatus::Succeeded ||
        process_tree.MarkContinued(process_index) != ProcessTreeStatus::Succeeded) {
        return false;
    }
    WakeRequiredThreads(WaitCondition::ChildProcess, WakeReason::ConditionSatisfied);
    return true;
}

[[nodiscard]] UserSignalStatus MapSignalManagerStatus(const SignalManagerStatus status) noexcept {
    if (status == SignalManagerStatus::Succeeded) {
        return UserSignalStatus::Succeeded;
    }
    if (status == SignalManagerStatus::ProcessNotFound) {
        return UserSignalStatus::ProcessNotFound;
    }
    if (status == SignalManagerStatus::InvalidSignal ||
        status == SignalManagerStatus::InvalidAction ||
        status == SignalManagerStatus::InvalidProcessGroup ||
        status == SignalManagerStatus::InvalidProcessId) {
        return UserSignalStatus::InvalidArgument;
    }
    if (status == SignalManagerStatus::SignalFrameNotActive ||
        status == SignalManagerStatus::SignalFrameMismatch) {
        return UserSignalStatus::InvalidState;
    }
    return UserSignalStatus::RuntimeFailure;
}

[[nodiscard]] UserSignalStatus MapJobControlStatus(const JobControlStatus status) noexcept {
    if (status == JobControlStatus::Succeeded) {
        return UserSignalStatus::Succeeded;
    }
    if (status == JobControlStatus::ProcessNotFound) {
        return UserSignalStatus::ProcessNotFound;
    }
    if (status == JobControlStatus::PermissionDenied) {
        return UserSignalStatus::PermissionDenied;
    }
    if (status == JobControlStatus::InvalidProcessId ||
        status == JobControlStatus::InvalidProcessGroup ||
        status == JobControlStatus::InvalidProcessIndex) {
        return UserSignalStatus::InvalidArgument;
    }
    if (status == JobControlStatus::SessionLeader ||
        status == JobControlStatus::ProcessGroupLeader ||
        status == JobControlStatus::InvalidSession) {
        return UserSignalStatus::InvalidState;
    }
    return UserSignalStatus::RuntimeFailure;
}

[[nodiscard]] bool CurrentProcessThreadMemoryOverlaps(const uint64_t address,
                                                      const uint64_t length_bytes) noexcept {
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return true;
    }
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        ThreadEntry thread{};
        if (!runtime_threads[thread_index].active ||
            thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
            thread.process_index != current_thread.process_index) {
            continue;
        }
        const ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
        if (RangesOverlap(address, length_bytes, runtime_thread.user_stack_base_address,
                          runtime_thread.user_stack_size_bytes) ||
            (runtime_thread.thread_local_storage_base != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             RangesOverlap(address, length_bytes, runtime_thread.thread_local_storage_base,
                           runtime_thread.thread_local_storage_size_bytes))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool CancelPrivateFutexRange(const uint64_t address_space_identifier,
                                           const uint64_t begin_address,
                                           const uint64_t length_bytes, const bool cancel_all,
                                           uint64_t &cancelled_thread_count) noexcept {
    cancelled_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (address_space_identifier == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        (!cancel_all && (length_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
                         begin_address > UINT64_MAX - length_bytes))) {
        return false;
    }
    const uint64_t end_address =
        cancel_all ? OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE : begin_address + length_bytes;
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    for (uint64_t entry_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         entry_index < OS_KERNEL_PRIVATE_FUTEX_CAPACITY_LIMIT; ++entry_index) {
        PrivateFutexEntry entry{};
        const PrivateFutexStatus read_status = private_futex_manager.Read(entry_index, entry);
        if (read_status == PrivateFutexStatus::EntryNotFound) {
            continue;
        }
        if (read_status != PrivateFutexStatus::Succeeded) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            return false;
        }
        if (entry.key.address_space_identifier != address_space_identifier ||
            (!cancel_all &&
             (entry.key.user_address < begin_address || entry.key.user_address >= end_address))) {
            continue;
        }
        uint64_t actual_entry_index = OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
        WaitQueue *wait_queue = nullptr;
        if (private_futex_manager.Find(entry.key, actual_entry_index, wait_queue) !=
                PrivateFutexStatus::Succeeded ||
            actual_entry_index != entry_index || wait_queue == nullptr) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            return false;
        }
        while (wait_queue->Statistics().waiting_thread_count !=
               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            uint64_t woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
            bool wake_won = false;
            if (thread_scheduler.WakeOne(*wait_queue, WakeReason::Cancelled, woken_thread_index,
                                         wake_won) != ThreadSchedulerStatus::Succeeded ||
                !wake_won || woken_thread_index >= process_runtime_limits.thread_capacity ||
                runtime_threads[woken_thread_index].saved_frame == nullptr) {
                scheduler_lock.Unlock(interrupts_were_enabled);
                return false;
            }
            runtime_threads[woken_thread_index].saved_frame->register_rax =
                static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_WAIT_CANCELLED);
            ++cancelled_thread_count;
        }
        bool released = false;
        if (private_futex_manager.ReleaseIfEmpty(entry_index, released) !=
                PrivateFutexStatus::Succeeded ||
            !released) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            return false;
        }
    }
    const PrivateFutexStatus record_status = private_futex_manager.RecordCancellationOperation();
    scheduler_lock.Unlock(interrupts_were_enabled);
    return record_status == PrivateFutexStatus::Succeeded;
}

[[nodiscard]] bool ReapProcessSiblingsForExec(const uint64_t process_index,
                                              const uint64_t current_thread_index) noexcept {
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        if (thread_index == current_thread_index) {
            continue;
        }
        ThreadEntry thread{};
        if (thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
            thread.process_index != process_index) {
            continue;
        }
        if (thread.state != ThreadState::Exited ||
            GetKernelStackManager().TryDestroy(thread.kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded) {
            return false;
        }
        runtime_threads[thread_index] = ProcessRuntimeThread{};
        const bool interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus reap_status = thread_scheduler.ReapExitedThread(thread_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (reap_status != ThreadSchedulerStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] ExceptionFrame *StopCurrentProcessFromSignal(ExceptionFrame &frame,
                                                           const uint64_t signal_number) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (!process_scheduling_active || !CurrentFrameIsValid(thread_index, frame) ||
        thread_scheduler.ReadThread(thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.process_index >= process_runtime_limits.process_capacity ||
        signal_number == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        HaltProcessor();
    }
    runtime_threads[thread_index].saved_frame = &frame;
    if (!WaitForProcessKernelContinuations(current_thread.process_index, thread_index)) {
        HaltProcessor();
    }
    if (SaveFxState(runtime_threads[thread_index].extended_state) !=
            ExtendedStateStatus::Succeeded ||
        process_tree.MarkStopped(current_thread.process_index, signal_number) !=
            ProcessTreeStatus::Succeeded) {
        HaltProcessor();
    }
    WakeRequiredThreads(WaitCondition::ChildProcess, WakeReason::ConditionSatisfied);

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus stop_status =
        thread_scheduler.StopCurrentProcess(current_thread.process_index, decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (stop_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (!decision.switched) {
        ReturnUserModeToProcessDispatcher(false);
    }
    return ActivateScheduledUserOrReturnToDispatcher(decision);
}

[[nodiscard]] ExceptionFrame *
CompleteCurrentThread(ExceptionFrame &frame, const ProcessTerminationReason termination_reason,
                      const int64_t exit_code, const uint64_t page_fault_address) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (!process_scheduling_active || !CurrentFrameIsValid(thread_index, frame) ||
        thread_scheduler.ReadThread(thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.process_index >= process_runtime_limits.process_capacity) {
        HaltProcessor();
    }
    runtime_threads[thread_index].saved_frame = &frame;
    if (!WaitForProcessKernelContinuations(current_thread.process_index, thread_index)) {
        HaltProcessor();
    }
    uint64_t terminated_sibling_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const bool sibling_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus sibling_status = thread_scheduler.TerminateProcessSiblings(
        current_thread.process_index, thread_index, terminated_sibling_count);
    scheduler_lock.Unlock(sibling_interrupts_were_enabled);
    if (sibling_status != ThreadSchedulerStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_SCHEDULER);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STATUS_PREFIX,
                                 static_cast<uint64_t>(sibling_status));
        HaltProcessor();
    }

    ProcessRuntimeProcess &process = runtime_processes[current_thread.process_index];
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    const ExtendedStateStatus save_status = SaveFxState(runtime_thread.extended_state);
    if (save_status != ExtendedStateStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_EXTENDED_STATE);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STATUS_PREFIX,
                                 static_cast<uint64_t>(save_status));
        HaltProcessor();
    }
    uint64_t cancelled_futex_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!CancelPrivateFutexRange(
            process.address_space.address_space_identifier, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, true, cancelled_futex_thread_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_FUTEX);
        HaltProcessor();
    }
    static_cast<void>(cancelled_futex_thread_count);
    process.result.termination_reason = termination_reason;
    process.result.exit_code = exit_code;
    if (termination_reason == ProcessTerminationReason::Exception) {
        process.result.exception_vector = frame.vector;
        process.result.exception_error_code = frame.error_code;
        process.result.exception_instruction_pointer = frame.instruction_pointer;
        process.result.page_fault_address = page_fault_address;
    } else if (termination_reason == ProcessTerminationReason::Signal) {
        process.result.exception_vector = frame.vector;
    }
    // 最后一个 writable shared 后备引用消失前必须先把脏页移交给 VFS。
    // Sync 会同时重新写保护其他地址空间中的 alias，避免写回后继续无通知写入。
    if (!FlushOutstandingUserFilePages()) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_WRITEBACK);
        HaltProcessor();
    }
    CloseProcessIoDescriptors(process);
    fs::Status release_context_status = fs::Status::Succeeded;
    if (process_vfs != nullptr && process.file_system_context.initialized) {
        release_context_status = process_vfs->ReleaseContext(process.file_system_context);
    }
    if (release_context_status != fs::Status::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_FILE_SYSTEM_CONTEXT);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STATUS_PREFIX,
                                 static_cast<uint64_t>(release_context_status));
        HaltProcessor();
    }
    for (uint64_t process_thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_thread_index < process_runtime_limits.thread_capacity; ++process_thread_index) {
        SignalThreadState signal_thread{};
        if (signal_manager.ReadThread(process_thread_index, signal_thread) ==
                SignalManagerStatus::Succeeded &&
            signal_thread.process_index == current_thread.process_index &&
            !RemoveSignalThreadIfPresent(process_thread_index)) {
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX,
                                     OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_SIGNAL);
            HaltProcessor();
        }
    }
    // 保留进程级信号与进程组身份直到父进程收集僵尸进程。这样快速退出的
    // 管线组长仍能作为稳定的 PGID 锚点，父子双方的 setpgid 不会产生竞态。
    user_thread_runtime_statistics.process_exit_cancelled_thread_count += terminated_sibling_count;

    SetActiveUserAddressSpace(nullptr);
    ActivateKernelPageTable();
    const UserAddressSpaceStatus destroy_status = DestroyUserAddressSpace(process.address_space);
    if (destroy_status != UserAddressSpaceStatus::Succeeded) {
        const UserAddressSpaceDestructionDiagnostics diagnostics =
            GetUserAddressSpaceDestructionDiagnostics();
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_ADDRESS_SPACE);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STATUS_PREFIX,
                                 static_cast<uint64_t>(destroy_status));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_ADDRESS_SPACE_STAGE_PREFIX,
                                 static_cast<uint64_t>(diagnostics.stage));
        WriteProcessRuntimeValue(
            OS_KERNEL_PROCESS_RUNTIME_FATAL_ADDRESS_SPACE_VIRTUAL_ADDRESS_PREFIX,
            diagnostics.virtual_address);
        WriteProcessRuntimeValue(
            OS_KERNEL_PROCESS_RUNTIME_FATAL_ADDRESS_SPACE_PHYSICAL_ADDRESS_PREFIX,
            diagnostics.physical_address);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_ADDRESS_SPACE_DETAIL_STATUS_PREFIX,
                                 diagnostics.status);
        HaltProcessor();
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status =
        thread_scheduler.TerminateCurrentThread(decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_EXIT_STAGE_SCHEDULER);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STATUS_PREFIX,
                                 static_cast<uint64_t>(scheduler_status));
        HaltProcessor();
    }
    // 先把调度器进程推进到 Zombie，再公开进程树的退出事件。ChildProcess 是共享
    // 等待队列，其他子进程的唤醒可能让父进程提前扫描本进程；反向顺序会让 wait
    // 收集到进程树条目，却因调度器仍为 Alive 而产生不可恢复的半提交。
    uint64_t reparented_process_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const ProcessTreeTerminationReason tree_termination_reason =
        termination_reason == ProcessTerminationReason::Exited
            ? ProcessTreeTerminationReason::Exited
        : termination_reason == ProcessTerminationReason::Signal
            ? ProcessTreeTerminationReason::Signal
            : ProcessTreeTerminationReason::Exception;
    const ProcessTreeStatus mark_exited_status =
        process_tree.MarkExited(current_thread.process_index,
                                ProcessTreeExitStatus{
                                    .termination_reason = tree_termination_reason,
                                    .exit_code = exit_code,
                                    .exception_vector = process.result.exception_vector,
                                },
                                reparented_process_count);
    if (mark_exited_status != ProcessTreeStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_PROCESS_ID_PREFIX,
                                 process.result.process_id);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_TERMINATION_REASON_PREFIX,
                                 static_cast<uint64_t>(termination_reason));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_TERMINATION_CODE_PREFIX,
                                 static_cast<uint64_t>(exit_code));
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_EXIT_STATUS_PREFIX,
                                 process.result.exception_vector);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FATAL_PROCESS_TREE_STATUS_PREFIX,
                                 static_cast<uint64_t>(mark_exited_status));
        HaltProcessor();
    }
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_EXIT_PREFIX, process.result.process_id);
    if (reparented_process_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_REPARENT_PREFIX,
                                 reparented_process_count);
    }
    WakeRequiredThreads(WaitCondition::ChildProcess, WakeReason::ConditionSatisfied);

    if (decision.completed || !decision.switched) {
        ReturnUserModeToProcessDispatcher(false);
    }
    return ActivateScheduledUserOrReturnToDispatcher(decision);
}
}

[[nodiscard]] KernelThreadRuntimeStatus
BlockCurrentRuntimeThreadInKernel(WaitQueue &wait_queue, const WaitCondition wait_condition,
                                  WakeReason &wake_reason) noexcept {
    wake_reason = WakeReason::None;
    if (!kernel_thread_runtime_available || !kernel_thread_dispatch_active ||
        CurrentSpinLockDepth() != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return KernelThreadRuntimeStatus::NotInitialized;
    }
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (current_thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(current_thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        (current_thread.kind != ThreadKind::User && current_thread.kind != ThreadKind::Kernel)) {
        return KernelThreadRuntimeStatus::InvalidThreadKind;
    }
    ProcessRuntimeThread &current_runtime_thread = runtime_threads[current_thread_index];
    const bool interrupts_were_enabled = DisableInterrupts();
    if (SaveFxState(current_runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::ExtendedStateFailure;
    }
    ThreadSchedulingDecision decision{};
    const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status =
        thread_scheduler.BlockCurrentThread(wait_queue, wait_condition, decision);
    scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded ||
        !SuspendBlockedRuntimeThread(current_thread_index, current_thread, current_runtime_thread,
                                     decision, wake_reason)) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    RestoreInterrupts(interrupts_were_enabled);
    return KernelThreadRuntimeStatus::Succeeded;
}

namespace {

[[nodiscard]] bool RuntimeFilePageLoadingOwnerAvailable(void *const context) noexcept {
    static_cast<void>(context);
    if (!process_runtime_initialized || !process_scheduling_active ||
        !kernel_thread_dispatch_active) {
        return false;
    }
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
        !runtime_threads[thread_index].active) {
        return false;
    }
    return (thread.kind == ThreadKind::User &&
            runtime_threads[thread_index].saved_frame != nullptr &&
            !runtime_threads[thread_index].user_kernel_continuation_active) ||
           (thread.kind == ThreadKind::Kernel && file_readahead_worker_io_active);
}

[[nodiscard]] bool RuntimeFilePageLoadingWaitAvailable(void *const context) noexcept {
    static_cast<void>(context);
    if (!process_runtime_initialized || !process_scheduling_active ||
        !kernel_thread_dispatch_active) {
        return false;
    }
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry thread{};
    return thread_index < process_runtime_limits.thread_capacity &&
           thread_scheduler.ReadThread(thread_index, thread) == ThreadSchedulerStatus::Succeeded &&
           thread.kind == ThreadKind::User && runtime_threads[thread_index].active &&
           runtime_threads[thread_index].saved_frame != nullptr &&
           !runtime_threads[thread_index].user_kernel_continuation_active;
}

[[nodiscard]] bool BeginRuntimeFilePageLoad(void *const context, const FilePageIdentity &identity,
                                            const uint64_t physical_address,
                                            const uint64_t load_generation,
                                            FilePageLoadToken &token) noexcept {
    static_cast<void>(context);
    if (!RuntimeFilePageLoadingOwnerAvailable(nullptr)) {
        token = FilePageLoadToken{};
        return false;
    }
    const uint64_t owner_thread_index = thread_scheduler.CurrentThreadIndex();
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageLoadStatus status = file_page_load_coordinator.Begin(
        identity, physical_address, load_generation, owner_thread_index, token);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return status == FilePageLoadStatus::Succeeded;
}

[[nodiscard]] bool RegisterRuntimeFilePageLoadWaiter(void *const context,
                                                     const FilePageIdentity &identity,
                                                     const uint64_t physical_address,
                                                     const uint64_t load_generation,
                                                     FilePageLoadToken &token) noexcept {
    static_cast<void>(context);
    if (!RuntimeFilePageLoadingWaitAvailable(nullptr)) {
        token = FilePageLoadToken{};
        return false;
    }
    const uint64_t waiter_thread_index = thread_scheduler.CurrentThreadIndex();
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageLoadStatus status = file_page_load_coordinator.RegisterWaiter(
        identity, physical_address, load_generation, waiter_thread_index, token);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return status == FilePageLoadStatus::Succeeded;
}

[[nodiscard]] bool WaitForRuntimeFilePageLoad(void *const context, const FilePageLoadToken token,
                                              FilePageCacheStatus &result) noexcept {
    static_cast<void>(context);
    result = FilePageCacheStatus::LoadingWaitFailed;
    if (!RuntimeFilePageLoadingWaitAvailable(nullptr) ||
        token.slot_index >= process_runtime_limits.thread_capacity) {
        // waiter 已在页缓存锁内登记；此后失败若继续返回会留下无法回收的协调器状态。
        HaltProcessor();
    }
    const uint64_t waiter_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry waiter_thread{};
    if (thread_scheduler.ReadThread(waiter_thread_index, waiter_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        waiter_thread.kind != ThreadKind::User) {
        HaltProcessor();
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[waiter_thread_index];
    const bool interrupts_were_enabled = DisableInterrupts();
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        HaltProcessor();
    }
    bool wait_required = false;
    ThreadSchedulingDecision decision{};
    const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageLoadStatus prepare_status =
        file_page_load_coordinator.PrepareWait(token, waiter_thread_index, wait_required);
    ThreadSchedulerStatus block_status = ThreadSchedulerStatus::Succeeded;
    if (prepare_status == FilePageLoadStatus::Succeeded && wait_required) {
        block_status = thread_scheduler.BlockCurrentThread(
            file_page_load_wait_queues[token.slot_index], WaitCondition::FilePageLoading, decision);
    }
    scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
    if (prepare_status != FilePageLoadStatus::Succeeded ||
        block_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (wait_required) {
        WakeReason wake_reason = WakeReason::None;
        if (!SuspendBlockedRuntimeThread(waiter_thread_index, waiter_thread, runtime_thread,
                                         decision, wake_reason) ||
            wake_reason != WakeReason::ConditionSatisfied) {
            HaltProcessor();
        }
    }
    const bool take_interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageLoadStatus take_status =
        file_page_load_coordinator.TakeResult(token, waiter_thread_index, result);
    scheduler_lock.Unlock(take_interrupts_were_enabled);
    if (take_status != FilePageLoadStatus::Succeeded) {
        HaltProcessor();
    }
    RestoreInterrupts(interrupts_were_enabled);
    return true;
}

[[nodiscard]] bool CompleteRuntimeFilePageLoad(void *const context, const FilePageLoadToken token,
                                               const FilePageCacheStatus result) noexcept {
    static_cast<void>(context);
    if (!process_runtime_initialized ||
        token.slot_index >= process_runtime_limits.thread_capacity) {
        HaltProcessor();
    }
    const uint64_t owner_thread_index = thread_scheduler.CurrentThreadIndex();
    FilePageLoadCompletionDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageLoadStatus completion_status =
        file_page_load_coordinator.Complete(token, owner_thread_index, result, decision);
    uint64_t woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    ThreadSchedulerStatus wake_status = ThreadSchedulerStatus::Succeeded;
    if (completion_status == FilePageLoadStatus::Succeeded &&
        decision.wake_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        wake_status = thread_scheduler.WakeMany(file_page_load_wait_queues[decision.slot_index],
                                                WakeReason::ConditionSatisfied, decision.wake_count,
                                                woken_thread_count);
    }
    scheduler_lock.Unlock(interrupts_were_enabled);
    const bool succeeded = completion_status == FilePageLoadStatus::Succeeded &&
                           wake_status == ThreadSchedulerStatus::Succeeded &&
                           woken_thread_count == decision.wake_count;
    if (!succeeded) {
        // owner 已发布页状态；完成协议失配时无法安全遗忘仍持有 token 的 waiter。
        HaltProcessor();
    }
    if (woken_thread_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        GetCpuLocal().RequestReschedule();
    }
    return true;
}

[[nodiscard]] bool ReadRuntimeFilePageLoadWaiterCount(void *const context,
                                                      const FilePageLoadToken token,
                                                      uint64_t &waiter_count) noexcept {
    static_cast<void>(context);
    if (!process_runtime_initialized ||
        token.slot_index >= process_runtime_limits.thread_capacity) {
        waiter_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return false;
    }
    const uint64_t owner_thread_index = thread_scheduler.CurrentThreadIndex();
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageLoadStatus status =
        file_page_load_coordinator.RegisteredWaiterCount(token, owner_thread_index, waiter_count);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return status == FilePageLoadStatus::Succeeded;
}

[[nodiscard]] bool ValidateRuntimeFilePageLoadState() noexcept {
    if (file_page_load_coordinator.Validate() != FilePageLoadStatus::Succeeded ||
        file_page_load_coordinator.Statistics().active_load_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    for (uint64_t slot_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         slot_index < process_runtime_limits.thread_capacity; ++slot_index) {
        if (thread_scheduler.ValidateWaitQueue(file_page_load_wait_queues[slot_index]) !=
                ThreadSchedulerStatus::Succeeded ||
            file_page_load_wait_queues[slot_index].Statistics().waiting_thread_count !=
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RuntimeFilePageWritebackOwnerAvailable(void *const context) noexcept {
    static_cast<void>(context);
    if (!process_runtime_initialized || !process_scheduling_active ||
        !kernel_thread_dispatch_active) {
        return false;
    }
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry thread{};
    if (thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(thread_index, thread) != ThreadSchedulerStatus::Succeeded ||
        !runtime_threads[thread_index].active) {
        return false;
    }
    return (thread.kind == ThreadKind::User &&
            runtime_threads[thread_index].saved_frame != nullptr &&
            !runtime_threads[thread_index].user_kernel_continuation_active) ||
           thread.kind == ThreadKind::Kernel;
}

[[nodiscard]] bool RuntimeFilePageWritebackWaitAvailable(void *const context) noexcept {
    return RuntimeFilePageWritebackOwnerAvailable(context);
}

[[nodiscard]] bool BeginRuntimeFilePageWriteback(void *const context,
                                                 const FilePageIdentity &identity,
                                                 const uint64_t physical_address,
                                                 const uint64_t writeback_generation,
                                                 FilePageWritebackToken &token) noexcept {
    static_cast<void>(context);
    if (!RuntimeFilePageWritebackOwnerAvailable(nullptr)) {
        token = FilePageWritebackToken{};
        return false;
    }
    const uint64_t owner_thread_index = thread_scheduler.CurrentThreadIndex();
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageWritebackStatus status = file_page_writeback_coordinator.Begin(
        identity, physical_address, writeback_generation, owner_thread_index, token);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return status == FilePageWritebackStatus::Succeeded;
}

[[nodiscard]] bool RegisterRuntimeFilePageWritebackWaiter(void *const context,
                                                          const FilePageIdentity &identity,
                                                          const uint64_t physical_address,
                                                          const uint64_t writeback_generation,
                                                          FilePageWritebackToken &token) noexcept {
    static_cast<void>(context);
    if (!RuntimeFilePageWritebackWaitAvailable(nullptr)) {
        token = FilePageWritebackToken{};
        return false;
    }
    const uint64_t waiter_thread_index = thread_scheduler.CurrentThreadIndex();
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageWritebackStatus status = file_page_writeback_coordinator.RegisterWaiter(
        identity, physical_address, writeback_generation, waiter_thread_index, token);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return status == FilePageWritebackStatus::Succeeded;
}

[[nodiscard]] bool WaitForRuntimeFilePageWriteback(void *const context,
                                                   const FilePageWritebackToken token,
                                                   FilePageCacheStatus &result) noexcept {
    static_cast<void>(context);
    result = FilePageCacheStatus::WritebackWaitFailed;
    if (!RuntimeFilePageWritebackWaitAvailable(nullptr) ||
        token.slot_index >= process_runtime_limits.thread_capacity) {
        // 页缓存锁内登记完成后必须消费结果，否则 generation 槽与 waiter 身份无法回收。
        HaltProcessor();
    }
    const uint64_t waiter_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry waiter_thread{};
    if (thread_scheduler.ReadThread(waiter_thread_index, waiter_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        (waiter_thread.kind != ThreadKind::User && waiter_thread.kind != ThreadKind::Kernel)) {
        HaltProcessor();
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[waiter_thread_index];
    const bool interrupts_were_enabled = DisableInterrupts();
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        HaltProcessor();
    }
    bool wait_required = false;
    ThreadSchedulingDecision decision{};
    const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageWritebackStatus prepare_status =
        file_page_writeback_coordinator.PrepareWait(token, waiter_thread_index, wait_required);
    ThreadSchedulerStatus block_status = ThreadSchedulerStatus::Succeeded;
    if (prepare_status == FilePageWritebackStatus::Succeeded && wait_required) {
        block_status =
            thread_scheduler.BlockCurrentThread(file_page_writeback_wait_queues[token.slot_index],
                                                WaitCondition::FilePageWriteback, decision);
    }
    scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
    if (prepare_status != FilePageWritebackStatus::Succeeded ||
        block_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (wait_required) {
        WakeReason wake_reason = WakeReason::None;
        if (!SuspendBlockedRuntimeThread(waiter_thread_index, waiter_thread, runtime_thread,
                                         decision, wake_reason) ||
            wake_reason != WakeReason::ConditionSatisfied) {
            HaltProcessor();
        }
    }
    const bool take_interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageWritebackStatus take_status =
        file_page_writeback_coordinator.TakeResult(token, waiter_thread_index, result);
    scheduler_lock.Unlock(take_interrupts_were_enabled);
    if (take_status != FilePageWritebackStatus::Succeeded) {
        HaltProcessor();
    }
    RestoreInterrupts(interrupts_were_enabled);
    return true;
}

[[nodiscard]] bool CompleteRuntimeFilePageWriteback(void *const context,
                                                    const FilePageWritebackToken token,
                                                    const FilePageCacheStatus result) noexcept {
    static_cast<void>(context);
    if (!process_runtime_initialized ||
        token.slot_index >= process_runtime_limits.thread_capacity) {
        HaltProcessor();
    }
    const uint64_t owner_thread_index = thread_scheduler.CurrentThreadIndex();
    FilePageWritebackCompletionDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const FilePageWritebackStatus completion_status =
        file_page_writeback_coordinator.Complete(token, owner_thread_index, result, decision);
    uint64_t woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    ThreadSchedulerStatus wake_status = ThreadSchedulerStatus::Succeeded;
    if (completion_status == FilePageWritebackStatus::Succeeded &&
        decision.wake_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        wake_status = thread_scheduler.WakeMany(
            file_page_writeback_wait_queues[decision.slot_index], WakeReason::ConditionSatisfied,
            decision.wake_count, woken_thread_count);
    }
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (completion_status != FilePageWritebackStatus::Succeeded ||
        wake_status != ThreadSchedulerStatus::Succeeded ||
        woken_thread_count != decision.wake_count) {
        // 页状态已经发布，协调器完成失败时不能安全遗忘已持有 token 的 waiter。
        HaltProcessor();
    }
    if (woken_thread_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        GetCpuLocal().RequestReschedule();
    }
    return true;
}

[[nodiscard]] bool ValidateRuntimeFilePageWritebackState() noexcept {
    if (file_page_writeback_coordinator.Validate() != FilePageWritebackStatus::Succeeded ||
        file_page_writeback_coordinator.Statistics().active_writeback_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    for (uint64_t slot_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         slot_index < process_runtime_limits.thread_capacity; ++slot_index) {
        if (thread_scheduler.ValidateWaitQueue(file_page_writeback_wait_queues[slot_index]) !=
                ThreadSchedulerStatus::Succeeded ||
            file_page_writeback_wait_queues[slot_index].Statistics().waiting_thread_count !=
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
            return false;
        }
    }
    return true;
}

bool AnyUserKernelContinuationActive() noexcept {
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        if (runtime_threads[thread_index].active &&
            runtime_threads[thread_index].user_kernel_continuation_active) {
            ThreadEntry thread{};
            if (thread_scheduler.ReadThread(thread_index, thread) ==
                    ThreadSchedulerStatus::Succeeded &&
                thread.kind == ThreadKind::User) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool ProcessHasOtherKernelContinuation(const uint64_t process_index,
                                                     const uint64_t current_thread_index) noexcept {
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        if (thread_index == current_thread_index || !runtime_threads[thread_index].active ||
            !runtime_threads[thread_index].user_kernel_continuation_active) {
            continue;
        }
        ThreadEntry thread{};
        if (thread_scheduler.ReadThread(thread_index, thread) == ThreadSchedulerStatus::Succeeded &&
            thread.kind == ThreadKind::User && thread.process_index == process_index) {
            return true;
        }
    }
    return false;
}

bool WaitForProcessKernelContinuations(const uint64_t process_index,
                                       const uint64_t current_thread_index) noexcept {
    while (ProcessHasOtherKernelContinuation(process_index, current_thread_index)) {
        WakeReason wake_reason = WakeReason::None;
        if (BlockCurrentRuntimeThreadInKernel(process_io_drain_wait_queue,
                                              WaitCondition::ProcessIoDrain, wake_reason) !=
                KernelThreadRuntimeStatus::Succeeded ||
            wake_reason != WakeReason::ConditionSatisfied) {
            return false;
        }
    }
    return true;
}

}

bool RuntimeBlockIoWaitAvailable() noexcept {
    if (!process_runtime_initialized || !process_scheduling_active ||
        !kernel_thread_dispatch_active || !block_io_completion_worker_started ||
        CurrentSpinLockDepth() != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    return current_thread_index < process_runtime_limits.thread_capacity &&
           thread_scheduler.ReadThread(current_thread_index, current_thread) ==
               ThreadSchedulerStatus::Succeeded &&
           (current_thread.kind == ThreadKind::User ||
            (current_thread.kind == ThreadKind::Kernel &&
             (block_io_runtime_probe_wait_active || file_readahead_worker_io_active)));
}

RuntimeBlockIoStatus RegisterRuntimeBlockIoDevice(AsynchronousBlockDevice &device) noexcept {
    if (!process_runtime_initialized || process_scheduling_active) {
        return RuntimeBlockIoStatus::NotAvailable;
    }
    const BlockDeviceGeometry geometry = device.Geometry();
    if (geometry.logical_block_size_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        geometry.logical_block_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        geometry.maximum_transfer_block_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        geometry.maximum_outstanding_request_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return RuntimeBlockIoStatus::InvalidRequest;
    }
    for (uint64_t device_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         device_index < block_io_device_count; ++device_index) {
        if (block_io_devices[device_index] == &device) {
            return RuntimeBlockIoStatus::Succeeded;
        }
    }
    if (block_io_device_count >= OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_DEVICE_CAPACITY) {
        return RuntimeBlockIoStatus::NotAvailable;
    }
    block_io_devices[block_io_device_count] = &device;
    ++block_io_device_count;
    return RuntimeBlockIoStatus::Succeeded;
}

RuntimeBlockIoStatus AwaitRuntimeBlockIo(AsynchronousBlockDevice &device,
                                         const BlockOperation operation,
                                         const uint64_t logical_block_address,
                                         uint8_t *const buffer, const uint64_t buffer_size_bytes,
                                         const uint64_t timeout_nanoseconds) noexcept {
    if (!RuntimeBlockIoWaitAvailable() ||
        timeout_nanoseconds == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return RuntimeBlockIoStatus::NotAvailable;
    }
    bool device_registered = false;
    for (uint64_t device_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         device_index < block_io_device_count; ++device_index) {
        device_registered = device_registered || block_io_devices[device_index] == &device;
    }
    const uint64_t owner_thread_index = thread_scheduler.CurrentThreadIndex();
    const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
    if (!device_registered || owner_thread_index >= process_runtime_limits.thread_capacity ||
        now_nanoseconds > UINT64_MAX - timeout_nanoseconds) {
        return RuntimeBlockIoStatus::InvalidRequest;
    }
    const uint64_t deadline_nanoseconds = now_nanoseconds + timeout_nanoseconds;
    const bool interrupts_were_enabled = DisableInterrupts();
    uint64_t request_identifier = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const AsynchronousBlockDeviceStatus submit_status =
        device.Submit(operation, logical_block_address, buffer, buffer_size_bytes,
                      owner_thread_index, deadline_nanoseconds, request_identifier);
    if (submit_status != AsynchronousBlockDeviceStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return submit_status == AsynchronousBlockDeviceStatus::InvalidRequest
                   ? RuntimeBlockIoStatus::InvalidRequest
                   : RuntimeBlockIoStatus::SubmitFailed;
    }
    BlockIoTicket ticket{};
    if (block_io_coordinator.Register(request_identifier, owner_thread_index, ticket) !=
        BlockIoStatus::Succeeded) {
        // 请求已交给设备，协调器若拒绝登记就不能安全释放调用者缓冲区。
        HaltProcessor();
    }
    NotifyRuntimeBlockIoCompletion();
    bool wait_required = false;
    if (block_io_coordinator.PrepareWait(ticket, wait_required) != BlockIoStatus::Succeeded) {
        // 提交后的状态机损坏无法通过返回错误恢复，否则设备仍可能写入失效缓冲区。
        HaltProcessor();
    }
    if (wait_required) {
        WakeReason wake_reason = WakeReason::None;
        if (BlockCurrentRuntimeThreadInKernel(block_io_wait_queue, WaitCondition::BlockIo,
                                              wake_reason) !=
                KernelThreadRuntimeStatus::Succeeded ||
            wake_reason != WakeReason::ConditionSatisfied) {
            // 当前设备协议尚无取消并等待硬件静止的能力，异常唤醒必须 fail-stop。
            HaltProcessor();
        }
    }
    BlockRequestResult result = BlockRequestResult::None;
    if (block_io_coordinator.TakeResult(ticket, result) != BlockIoStatus::Succeeded) {
        // 正常唤醒却取不到对应完成结果说明内核等待协议已经失去一致性。
        HaltProcessor();
    }
    RestoreInterrupts(interrupts_were_enabled);
    return result == BlockRequestResult::Succeeded   ? RuntimeBlockIoStatus::Succeeded
           : result == BlockRequestResult::TimedOut  ? RuntimeBlockIoStatus::TimedOut
           : result == BlockRequestResult::Cancelled ? RuntimeBlockIoStatus::Cancelled
                                                     : RuntimeBlockIoStatus::DeviceFailed;
}

void NotifyRuntimeBlockIoCompletion() noexcept {
    if (!process_runtime_initialized || !process_scheduling_active ||
        !block_io_completion_worker_started) {
        return;
    }
    if (block_io_completion_notification_generation == UINT64_MAX) {
        HaltProcessor();
    }
    block_io_completion_notification_generation =
        block_io_completion_notification_generation + OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
    bool wake_won = false;
    if (WakeOneKernelThread(block_io_completion_wait_queue, WakeReason::ConditionSatisfied,
                            wake_won) != KernelThreadRuntimeStatus::Succeeded) {
        HaltProcessor();
    }
    if (wake_won) {
        GetCpuLocal().RequestReschedule();
    }
}

void ServiceRuntimeBlockIoTimeouts(const uint64_t now_nanoseconds) noexcept {
    if (!process_runtime_initialized ||
        block_io_device_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return;
    }
    for (uint64_t device_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         device_index < block_io_device_count; ++device_index) {
        AsynchronousBlockDevice *const device = block_io_devices[device_index];
        if (device == nullptr ||
            device->ResolveTimeouts(now_nanoseconds) != AsynchronousBlockDeviceStatus::Succeeded) {
            HaltProcessor();
        }
    }
    NotifyRuntimeBlockIoCompletion();
}

BlockIoStatistics GetRuntimeBlockIoStatistics() noexcept {
    return block_io_coordinator.Statistics();
}

KernelThreadRuntimeStatus CreateKernelThread(const KernelThreadEntryOperation entry_operation,
                                             void *const context, ThreadId &thread_id) noexcept {
    if (!kernel_thread_runtime_available) {
        thread_id = ThreadId{};
        return KernelThreadRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active || kernel_thread_dispatch_active) {
        thread_id = ThreadId{};
        return KernelThreadRuntimeStatus::SchedulingActive;
    }
    return CreateKernelThreadCore(entry_operation, context, thread_id);
}

KernelThreadRuntimeStatus ExecuteReadyKernelThreads() noexcept {
    if (!kernel_thread_runtime_available) {
        return KernelThreadRuntimeStatus::NotInitialized;
    }
    const ThreadSchedulerStatistics initial_statistics = thread_scheduler.Statistics();
    if (process_scheduling_active || kernel_thread_dispatch_active ||
        initial_statistics.owned_user_thread_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return KernelThreadRuntimeStatus::SchedulingActive;
    }
    if (initial_statistics.ready_thread_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        initial_statistics.blocked_thread_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return KernelThreadRuntimeStatus::NoReadyThread;
    }

    const bool interrupts_were_enabled = DisableInterrupts();
    kernel_thread_dispatch_active = true;
    while (true) {
        ThreadSchedulingDecision decision{};
        const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus scheduler_status = thread_scheduler.Start(decision);
        scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
        if (scheduler_status == ThreadSchedulerStatus::NoReadyThread) {
            if (decision.completed) {
                break;
            }
            EnableInterruptsWaitAndDisable();
            continue;
        }
        if (scheduler_status != ThreadSchedulerStatus::Succeeded ||
            !ActivateKernelRuntimeThread(decision.current_thread_index)) {
            kernel_thread_dispatch_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return scheduler_status == ThreadSchedulerStatus::Succeeded
                       ? KernelThreadRuntimeStatus::ContextFailure
                       : KernelThreadRuntimeStatus::SchedulerFailure;
        }
        OsKernelEnterScheduledKernelThread(
            runtime_threads[decision.current_thread_index].kernel_stack_pointer);
        if (ReadPageTableRoot() != GetKernelPageTableRoot() || !ReapExitedKernelThreads()) {
            kernel_thread_dispatch_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return KernelThreadRuntimeStatus::StackFailure;
        }
    }
    const bool final_state_valid =
        ReapExitedKernelThreads() &&
        thread_scheduler.Validate() == ThreadSchedulerStatus::Succeeded &&
        thread_scheduler.Statistics().owned_kernel_thread_count ==
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        GetCpuLocal().Statistics().current_thread_index == OS_KERNEL_CPU_LOCAL_INVALID_THREAD_INDEX;
    kernel_thread_dispatch_active = false;
    RestoreInterrupts(interrupts_were_enabled);
    return final_state_valid ? KernelThreadRuntimeStatus::Succeeded
                             : KernelThreadRuntimeStatus::SchedulerFailure;
}

KernelThreadRuntimeStatus YieldCurrentKernelThread() noexcept {
    if (!kernel_thread_runtime_available || !kernel_thread_dispatch_active) {
        return KernelThreadRuntimeStatus::NotInitialized;
    }
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (current_thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(current_thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.kind != ThreadKind::Kernel) {
        return KernelThreadRuntimeStatus::InvalidThreadKind;
    }
    if (kernel_thread_runtime_statistics.yield_count == UINT64_MAX) {
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeThread &current_runtime_thread = runtime_threads[current_thread_index];
    const bool interrupts_were_enabled = DisableInterrupts();
    if (SaveFxState(current_runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::ExtendedStateFailure;
    }
    ThreadSchedulingDecision decision{};
    const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status = thread_scheduler.YieldCurrentThread(decision);
    scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    if (!decision.switched) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::Succeeded;
    }
    ++kernel_thread_runtime_statistics.yield_count;
    SwitchKernelThreadOrReturnToDispatcher(current_runtime_thread, decision);
    RestoreInterrupts(interrupts_were_enabled);
    return KernelThreadRuntimeStatus::Succeeded;
}

KernelThreadRuntimeStatus BlockCurrentKernelThread(WaitQueue &wait_queue,
                                                   const WaitCondition wait_condition,
                                                   WakeReason &wake_reason) noexcept {
    wake_reason = WakeReason::None;
    if (!kernel_thread_runtime_available || !kernel_thread_dispatch_active) {
        return KernelThreadRuntimeStatus::NotInitialized;
    }
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (current_thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(current_thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.kind != ThreadKind::Kernel) {
        return KernelThreadRuntimeStatus::InvalidThreadKind;
    }
    if (kernel_thread_runtime_statistics.block_count == UINT64_MAX) {
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeThread &current_runtime_thread = runtime_threads[current_thread_index];
    const bool interrupts_were_enabled = DisableInterrupts();
    if (SaveFxState(current_runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::ExtendedStateFailure;
    }
    ThreadSchedulingDecision decision{};
    const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status =
        thread_scheduler.BlockCurrentThread(wait_queue, wait_condition, decision);
    scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    ++kernel_thread_runtime_statistics.block_count;
    SwitchKernelThreadOrReturnToDispatcher(current_runtime_thread, decision);
    const bool consume_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus consume_status =
        thread_scheduler.ConsumeCurrentThreadWakeReason(wake_reason);
    scheduler_lock.Unlock(consume_interrupts_were_enabled);
    RestoreInterrupts(interrupts_were_enabled);
    return consume_status == ThreadSchedulerStatus::Succeeded
               ? KernelThreadRuntimeStatus::Succeeded
               : KernelThreadRuntimeStatus::SchedulerFailure;
}

KernelThreadRuntimeStatus BlockCurrentKernelThreadUntil(WaitQueue &wait_queue,
                                                        const WaitCondition wait_condition,
                                                        const uint64_t deadline_nanoseconds,
                                                        WakeReason &wake_reason) noexcept {
    wake_reason = WakeReason::None;
    if (!kernel_thread_runtime_available || !kernel_thread_dispatch_active) {
        return KernelThreadRuntimeStatus::NotInitialized;
    }
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (current_thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(current_thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.kind != ThreadKind::Kernel) {
        return KernelThreadRuntimeStatus::InvalidThreadKind;
    }
    if (kernel_thread_runtime_statistics.block_count == UINT64_MAX) {
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeThread &current_runtime_thread = runtime_threads[current_thread_index];
    const bool interrupts_were_enabled = DisableInterrupts();
    if (SaveFxState(current_runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::ExtendedStateFailure;
    }
    ThreadSchedulingDecision decision{};
    const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
    const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status = thread_scheduler.BlockCurrentThreadUntil(
        wait_queue, wait_condition, now_nanoseconds, deadline_nanoseconds, decision);
    scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
    if (scheduler_status == ThreadSchedulerStatus::DeadlineAlreadyReached) {
        wake_reason = WakeReason::Timeout;
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::Succeeded;
    }
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    ++kernel_thread_runtime_statistics.block_count;
    SwitchKernelThreadOrReturnToDispatcher(current_runtime_thread, decision);
    const bool consume_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus consume_status =
        thread_scheduler.ConsumeCurrentThreadWakeReason(wake_reason);
    scheduler_lock.Unlock(consume_interrupts_were_enabled);
    RestoreInterrupts(interrupts_were_enabled);
    return consume_status == ThreadSchedulerStatus::Succeeded
               ? KernelThreadRuntimeStatus::Succeeded
               : KernelThreadRuntimeStatus::SchedulerFailure;
}

KernelThreadRuntimeStatus WakeOneKernelThread(WaitQueue &wait_queue, const WakeReason wake_reason,
                                              bool &wake_won) noexcept {
    wake_won = false;
    if (!kernel_thread_runtime_available) {
        return KernelThreadRuntimeStatus::NotInitialized;
    }
    if (kernel_thread_runtime_statistics.wake_count == UINT64_MAX) {
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    uint64_t woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus wake_status =
        thread_scheduler.WakeOne(wait_queue, wake_reason, woken_thread_index, wake_won);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (wake_status != ThreadSchedulerStatus::Succeeded) {
        return KernelThreadRuntimeStatus::SchedulerFailure;
    }
    if (!wake_won) {
        return KernelThreadRuntimeStatus::Succeeded;
    }
    ThreadEntry woken_thread{};
    if (thread_scheduler.ReadThread(woken_thread_index, woken_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        woken_thread.kind != ThreadKind::Kernel) {
        return KernelThreadRuntimeStatus::InvalidThreadKind;
    }
    ++kernel_thread_runtime_statistics.wake_count;
    return KernelThreadRuntimeStatus::Succeeded;
}

KernelThreadRuntimeStatistics GetKernelThreadRuntimeStatistics() noexcept {
    return kernel_thread_runtime_statistics;
}

namespace {

[[nodiscard]] bool RuntimeMutexSchedulingAvailable() noexcept {
    if (!process_runtime_initialized || !process_scheduling_active ||
        !kernel_thread_dispatch_active ||
        CurrentSpinLockDepth() != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    return current_thread_index < process_runtime_limits.thread_capacity &&
           thread_scheduler.ReadThread(current_thread_index, current_thread) ==
               ThreadSchedulerStatus::Succeeded &&
           (current_thread.kind == ThreadKind::User || current_thread.kind == ThreadKind::Kernel);
}

[[nodiscard]] bool LockRuntimeMutex(RuntimeMutex &runtime_mutex) noexcept {
    if (!RuntimeMutexSchedulingAvailable()) {
        return false;
    }
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (thread_scheduler.ReadThread(current_thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.thread_id.value == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[current_thread_index];
    const bool interrupts_were_enabled = DisableInterrupts();
    if (!runtime_mutex.RuntimePrimitiveInitialized()) {
        if (runtime_mutex.Primitive().Initialize(runtime_mutex.WaitQueueIdentifier()) !=
            MutexStatus::Succeeded) {
            RestoreInterrupts(interrupts_were_enabled);
            return false;
        }
        runtime_mutex.MarkRuntimePrimitiveInitialized();
    }
    const MutexStatus immediate_status =
        runtime_mutex.Primitive().TryLock(current_thread.thread_id);
    if (immediate_status == MutexStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return true;
    }
    if (immediate_status != MutexStatus::WouldBlock ||
        SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return false;
    }
    ThreadSchedulingDecision decision{};
    const MutexStatus lock_status =
        runtime_mutex.Primitive().Lock(thread_scheduler, current_thread.thread_id, decision);
    WakeReason wake_reason = WakeReason::None;
    if (lock_status != MutexStatus::Blocked ||
        !SuspendBlockedRuntimeThread(current_thread_index, current_thread, runtime_thread, decision,
                                     wake_reason) ||
        wake_reason != WakeReason::ConditionSatisfied ||
        runtime_mutex.Primitive().TryLock(current_thread.thread_id) != MutexStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return false;
    }
    RestoreInterrupts(interrupts_were_enabled);
    return true;
}

[[nodiscard]] bool UnlockRuntimeMutex(RuntimeMutex &runtime_mutex) noexcept {
    if (!RuntimeMutexSchedulingAvailable() || !runtime_mutex.RuntimePrimitiveInitialized()) {
        return false;
    }
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (thread_scheduler.ReadThread(current_thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.thread_id.value == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return false;
    }
    const bool interrupts_were_enabled = DisableInterrupts();
    uint64_t woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    const MutexStatus unlock_status = runtime_mutex.Primitive().Unlock(
        thread_scheduler, current_thread.thread_id, woken_thread_index);
    if (unlock_status == MutexStatus::Succeeded &&
        woken_thread_index != OS_KERNEL_THREAD_INVALID_INDEX) {
        GetCpuLocal().RequestReschedule();
    }
    RestoreInterrupts(interrupts_were_enabled);
    return unlock_status == MutexStatus::Succeeded;
}

[[noreturn]] void FailRuntimeMutex() noexcept { HaltProcessor(); }

}

extern "C" [[noreturn]] void OsKernelThreadBootstrap() noexcept {
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (current_thread_index >= process_runtime_limits.thread_capacity ||
        thread_scheduler.ReadThread(current_thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.kind != ThreadKind::Kernel ||
        !runtime_threads[current_thread_index].active ||
        runtime_threads[current_thread_index].kernel_entry_operation == nullptr) {
        HaltProcessor();
    }
    ProcessRuntimeThread &current_runtime_thread = runtime_threads[current_thread_index];
    current_runtime_thread.kernel_entry_operation(current_runtime_thread.kernel_entry_context);

    if (kernel_thread_runtime_statistics.exit_count == UINT64_MAX) {
        HaltProcessor();
    }
    const bool interrupts_were_enabled = DisableInterrupts();
    static_cast<void>(interrupts_were_enabled);
    ThreadSchedulingDecision decision{};
    const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus exit_status = thread_scheduler.TerminateCurrentThread(decision);
    scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
    if (exit_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    ++kernel_thread_runtime_statistics.exit_count;
    if (decision.current_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
        if (!ClearActiveKernelRuntimeThread()) {
            HaltProcessor();
        }
        OsKernelLeaveScheduledKernelThread();
        HaltProcessor();
    }
    ThreadEntry next_thread{};
    if (thread_scheduler.ReadThread(decision.current_thread_index, next_thread) !=
        ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (next_thread.kind == ThreadKind::User) {
        if (kernel_thread_runtime_statistics.kernel_to_user_switch_count == UINT64_MAX ||
            !ClearActiveKernelRuntimeThread()) {
            HaltProcessor();
        }
        ++kernel_thread_runtime_statistics.kernel_to_user_switch_count;
        OsKernelLeaveScheduledKernelThread();
        HaltProcessor();
    }
    if (next_thread.kind != ThreadKind::Kernel ||
        !ActivateKernelRuntimeThread(decision.current_thread_index)) {
        HaltProcessor();
    }
    OsKernelSwitchKernelThread(&current_runtime_thread.kernel_stack_pointer,
                               runtime_threads[decision.current_thread_index].kernel_stack_pointer);
    HaltProcessor();
}

ProcessRuntimeStatus InitializeProcessRuntime() noexcept {
    if (process_scheduling_active || process_runtime_initialized) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    if (!GetExtendedStateConfiguration().initialized) {
        return ProcessRuntimeStatus::ExtendedStateFailure;
    }
    if (GetCpuLocal().Validate() != CpuLocalStatus::Succeeded) {
        return ProcessRuntimeStatus::CpuLocalFailure;
    }
    if (!GetNativeSystemCallConfiguration().initialized) {
        return ProcessRuntimeStatus::NativeSystemCallFailure;
    }
    if (InitializeUserVirtualMemory() != UserAddressSpaceStatus::Succeeded) {
        return ProcessRuntimeStatus::AddressSpaceFailure;
    }
    const uint64_t managed_memory_bytes = GetKernelMemoryStatistics().managed_usable_memory_bytes;
    process_runtime_limits = SelectProcessRuntimeLimits(managed_memory_bytes);
    const bool capacity_aging_profile =
        managed_memory_bytes >= OS_KERNEL_PROCESS_RUNTIME_CAPACITY_MEMORY_BYTES;
    page_aging_capacity = capacity_aging_profile
                              ? OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_CAPACITY_CAPACITY
                              : OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FUNCTIONAL_CAPACITY;
    page_aging_hash_capacity = capacity_aging_profile
                                   ? OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_CAPACITY_HASH_CAPACITY
                                   : OS_KERNEL_PROCESS_RUNTIME_PAGE_AGING_FUNCTIONAL_HASH_CAPACITY;
    const PipePageAllocator pipe_page_allocator{
        .allocate_page = AllocatePipePage,
        .release_page = ReleasePipePage,
        .context = nullptr,
    };
    if (dynamic_pipe_manager.Initialize(pipe_page_allocator,
                                        process_runtime_limits.pipe_capacity) !=
            PipeManagerStatus::Succeeded ||
        !RunPipeCapacitySelfTest(process_runtime_limits.pipe_capacity)) {
        return ProcessRuntimeStatus::PipeFailure;
    }
    if (!RunProcessThreadCapacitySelfTest(process_runtime_limits)) {
        return ProcessRuntimeStatus::CapacitySelfTestFailure;
    }
    if (thread_scheduler.Initialize(process_entries, process_runtime_limits.process_capacity,
                                    thread_entries, process_runtime_limits.thread_capacity,
                                    process_runtime_limits.maximum_threads_per_process,
                                    OS_KERNEL_THREAD_DEFAULT_QUANTUM_TICKS) !=
        ThreadSchedulerStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (process_launch_mutex.Initialize(WaitQueueId{
            .value = OS_KERNEL_PROCESS_RUNTIME_LAUNCH_WAIT_QUEUE_ID,
        }) != RuntimeMutexStatus::Succeeded) {
        return ProcessRuntimeStatus::LockFailure;
    }
    if (ConfigureRuntimeMutexOperations(RuntimeMutexOperations{
            .available = RuntimeMutexSchedulingAvailable,
            .lock = LockRuntimeMutex,
            .unlock = UnlockRuntimeMutex,
            .failure = FailRuntimeMutex,
        }) != RuntimeMutexStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (file_page_load_coordinator.Initialize(
            file_page_load_slots, process_runtime_limits.thread_capacity, file_page_load_waiters,
            process_runtime_limits.thread_capacity) != FilePageLoadStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    for (uint64_t slot_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         slot_index < process_runtime_limits.thread_capacity; ++slot_index) {
        if (file_page_load_wait_queues[slot_index].Initialize(WaitQueueId{
                .value = OS_KERNEL_PROCESS_RUNTIME_FILE_PAGE_LOAD_WAIT_QUEUE_BASE + slot_index,
            }) != WaitQueueStatus::Succeeded) {
            return ProcessRuntimeStatus::SchedulerFailure;
        }
    }
    if (file_page_writeback_coordinator.Initialize(
            file_page_writeback_slots, process_runtime_limits.thread_capacity,
            file_page_writeback_waiters,
            process_runtime_limits.thread_capacity) != FilePageWritebackStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    for (uint64_t slot_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         slot_index < process_runtime_limits.thread_capacity; ++slot_index) {
        if (file_page_writeback_wait_queues[slot_index].Initialize(WaitQueueId{
                .value = OS_KERNEL_PROCESS_RUNTIME_FILE_PAGE_WRITEBACK_WAIT_QUEUE_BASE + slot_index,
            }) != WaitQueueStatus::Succeeded) {
            return ProcessRuntimeStatus::SchedulerFailure;
        }
    }
    if (kernel_object_manager.Initialize(GetKernelHeap()) != KernelObjectStatus::Succeeded ||
        file_description_manager.Initialize(kernel_object_manager) !=
            FileDescriptionStatus::Succeeded) {
        return ProcessRuntimeStatus::DescriptorTableFailure;
    }
    ResetRuntimeStorage();
    anonymous_reclaim_process_cursor = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!ConfigureUserMemoryReclaimOperations(UserMemoryReclaimOperations{
            .protect_shared_mappings = ProtectRuntimeSharedFileMappingsForReclaim,
            .request_background_reclaim = RequestRuntimeBackgroundReclaim,
            .prepare_anonymous_page_release = PrepareRuntimeAnonymousPageRelease,
            .reclaim_anonymous_pages = ReclaimRuntimeAnonymousPages,
            .recover_out_of_memory = RecoverRuntimeOutOfMemory,
            .context = nullptr,
        })) {
        return ProcessRuntimeStatus::AddressSpaceFailure;
    }
    if (pipe_readable_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_PIPE_READABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        pipe_writable_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_PIPE_WRITABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        descriptor_readable_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_READABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        descriptor_writable_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_DESCRIPTOR_WRITABLE_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        child_exit_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_CHILD_EXIT_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        thread_join_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_THREAD_JOIN_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        sleep_wait_queue.Initialize(WaitQueueId{
            .value = OS_KERNEL_PROCESS_RUNTIME_SLEEP_QUEUE_ID}) != WaitQueueStatus::Succeeded ||
        block_io_wait_queue.Initialize(WaitQueueId{
            .value = OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_QUEUE_ID}) != WaitQueueStatus::Succeeded ||
        block_io_completion_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_COMPLETION_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        process_io_drain_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_PROCESS_IO_DRAIN_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        kernel_thread_test_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_KERNEL_THREAD_TEST_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        kernel_work_wait_queue.Initialize(
            WaitQueueId{.value = OS_KERNEL_PROCESS_RUNTIME_KERNEL_WORK_QUEUE_ID}) !=
            WaitQueueStatus::Succeeded ||
        private_futex_manager.Initialize(private_futex_entries,
                                         OS_KERNEL_PRIVATE_FUTEX_CAPACITY_LIMIT,
                                         OS_KERNEL_PROCESS_RUNTIME_PRIVATE_FUTEX_FIRST_QUEUE_ID) !=
            PrivateFutexStatus::Succeeded ||
        process_tree.Initialize(process_tree_entries, process_runtime_limits.process_capacity) !=
            ProcessTreeStatus::Succeeded ||
        job_control_manager.Initialize(job_control_process_states,
                                       process_runtime_limits.process_capacity) !=
            JobControlStatus::Succeeded ||
        signal_manager.Initialize(signal_process_states, process_runtime_limits.process_capacity,
                                  signal_thread_states, process_runtime_limits.thread_capacity) !=
            SignalManagerStatus::Succeeded ||
        block_io_coordinator.Initialize(block_io_slots,
                                        OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_CAPACITY) !=
            BlockIoStatus::Succeeded) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    kernel_thread_runtime_available = true;
    file_writeback_work_handle = WorkHandle{};
    page_aging_work_handle = WorkHandle{};
    background_reclaim_work_handle = WorkHandle{};
    file_readahead_work_handle = WorkHandle{};
    file_writeback_work_registered = false;
    page_aging_work_registered = false;
    background_reclaim_work_registered = false;
    file_readahead_work_registered = false;
    page_aging_worker_failed = false;
    background_reclaim_worker_failed = false;
    file_readahead_worker_failed = false;
    file_readahead_worker_io_active = false;
    kernel_work_thread_stop_requested = false;
    if (background_reclaim_controller.Initialize(BackgroundReclaimConfiguration{
            .batch_page_count = OS_KERNEL_PROCESS_RUNTIME_BACKGROUND_RECLAIM_BATCH_PAGE_COUNT,
            .no_progress_backoff_nanoseconds =
                OS_KERNEL_PROCESS_RUNTIME_BACKGROUND_RECLAIM_BACKOFF_NANOSECONDS,
        }) != BackgroundReclaimStatus::Succeeded) {
        return ProcessRuntimeStatus::ThreadFailure;
    }
    uint64_t page_aging_entry_storage_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    uint64_t page_aging_hash_storage_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (page_aging_capacity > UINT64_MAX / sizeof(PageAgingEntry) ||
        page_aging_hash_capacity > UINT64_MAX / sizeof(uint64_t) ||
        !CalculatePageCount(page_aging_capacity * sizeof(PageAgingEntry),
                            page_aging_entry_storage_page_count) ||
        !CalculatePageCount(page_aging_hash_capacity * sizeof(uint64_t),
                            page_aging_hash_storage_page_count)) {
        return ProcessRuntimeStatus::AddressSpaceFailure;
    }
    if (AllocateKernelPages(page_aging_entry_storage_page_count, page_aging_entry_storage) !=
        KernelPageAllocationStatus::Succeeded) {
        return ProcessRuntimeStatus::AddressSpaceFailure;
    }
    page_aging_entries =
        reinterpret_cast<PageAgingEntry *>(page_aging_entry_storage.virtual_address);
    if (AllocateKernelPages(page_aging_hash_storage_page_count, page_aging_hash_storage) !=
        KernelPageAllocationStatus::Succeeded) {
        static_cast<void>(ReleaseKernelPages(page_aging_entry_storage));
        page_aging_entries = nullptr;
        return ProcessRuntimeStatus::AddressSpaceFailure;
    }
    page_aging_hash = reinterpret_cast<uint64_t *>(page_aging_hash_storage.virtual_address);
    if (page_aging_manager.Initialize(page_aging_entries, page_aging_capacity, page_aging_hash,
                                      page_aging_hash_capacity) != PageAgingStatus::Succeeded) {
        static_cast<void>(ReleaseKernelPages(page_aging_hash_storage));
        static_cast<void>(ReleaseKernelPages(page_aging_entry_storage));
        page_aging_entries = nullptr;
        page_aging_hash = nullptr;
        return ProcessRuntimeStatus::AddressSpaceFailure;
    }
    if (file_readahead_feedback_ledger.Initialize(
            file_readahead_feedback_slots,
            OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_FEEDBACK_CAPACITY) !=
            FileReadaheadFeedbackStatus::Succeeded ||
        file_readahead_request_queue.Initialize(
            file_readahead_request_slots, file_readahead_request_ready_storage,
            OS_KERNEL_PROCESS_RUNTIME_FILE_READAHEAD_REQUEST_CAPACITY) !=
            FileReadaheadRequestStatus::Succeeded ||
        kernel_work_queue.Initialize(kernel_work_queue_entries, kernel_work_queue_delayed_heap,
                                     OS_KERNEL_PROCESS_RUNTIME_WORK_QUEUE_CAPACITY) !=
            WorkQueueStatus::Succeeded ||
        !RunKernelThreadLifecycleSelfTest() || !RunKernelWorkQueueLifecycleSelfTest()) {
        return ProcessRuntimeStatus::ThreadFailure;
    }
    frames_before_processes = GetPhysicalFrameAllocatorStatistics();
    frames_after_processes = PhysicalFrameAllocatorStatistics{};
    virtual_addresses_before_processes = GetKernelVirtualAddressAllocator().Statistics();
    virtual_addresses_after_processes = KernelVirtualAddressAllocatorStatistics{};
    kernel_stacks_before_processes = GetKernelStackManager().Statistics();
    kernel_stacks_after_processes = KernelStackManagerStatistics{};
    virtual_memory_areas_before_processes = GetUserVirtualMemoryPoolStatistics();
    virtual_memory_areas_after_processes = VirtualMemoryAreaPoolStatistics{};
    user_page_references_before_processes = GetUserPageReferenceStatistics();
    user_page_references_after_processes = UserPageReferenceStatistics{};
    resource_snapshot_before_processes = ResourceSnapshot{};
    resource_snapshot_after_processes = ResourceSnapshot{};
    resource_snapshot_difference = ResourceSnapshotDifference{};
    if (GetKernelStackManager().Validate() != KernelStackManagerStatus::Succeeded ||
        kernel_stacks_before_processes.active_stack_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_before_processes.active_descriptor_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_before_processes.free_descriptor_count !=
            virtual_memory_areas_before_processes.capacity ||
        user_page_references_before_processes.active_entry_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        user_page_references_before_processes.active_reference_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        GetKernelResourceSnapshot(ResourceSnapshotSupplementalCounts{},
                                  resource_snapshot_before_processes) !=
            ResourceSnapshotStatus::Succeeded) {
        return ProcessRuntimeStatus::KernelStackFailure;
    }
    program_argument_plan.Reset();
    process_pipe.Initialize();
    process_terminal.Initialize(OS_KERNEL_TERMINAL_IDENTIFIER, DisableInterrupts,
                                RestoreInterrupts);
    pipe_reader_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_writer_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    descriptor_reader_block_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_end_of_file_observation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    pipe_broken_observation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    oom_invocation_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    oom_kill_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    last_oom_victim_process_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    last_oom_victim_score = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    file_synchronization_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    file_data_synchronization_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    memory_synchronous_synchronization_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    memory_asynchronous_synchronization_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    current_process_oom_kill_pending = false;
    process_vfs = nullptr;
    process_runtime_initialized = true;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus RefreshProcessRuntimeResourceBaseline() noexcept {
    const ThreadSchedulerStatistics scheduler_statistics = thread_scheduler.Statistics();
    if (!process_runtime_initialized || process_scheduling_active ||
        scheduler_statistics.owned_process_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        scheduler_statistics.owned_thread_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    frames_before_processes = GetPhysicalFrameAllocatorStatistics();
    virtual_addresses_before_processes = GetKernelVirtualAddressAllocator().Statistics();
    kernel_stacks_before_processes = GetKernelStackManager().Statistics();
    virtual_memory_areas_before_processes = GetUserVirtualMemoryPoolStatistics();
    user_page_references_before_processes = GetUserPageReferenceStatistics();
    resource_snapshot_before_processes = ResourceSnapshot{};
    if (kernel_stacks_before_processes.active_stack_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_before_processes.active_descriptor_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        user_page_references_before_processes.active_entry_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        user_page_references_before_processes.active_reference_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        GetKernelResourceSnapshot(ResourceSnapshotSupplementalCounts{},
                                  resource_snapshot_before_processes) !=
            ResourceSnapshotStatus::Succeeded) {
        return ProcessRuntimeStatus::ResourceLeakDetected;
    }
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus AttachProcessVfs(fs::Vfs &vfs, FileSystemBlockDevice &swap_device) noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    if (vfs.Validate() != fs::Status::Succeeded) {
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    if (ConfigureUserFilePageCacheLoadingWait(FilePageLoadWaitOperations{
            .context = nullptr,
            .owner_available = RuntimeFilePageLoadingOwnerAvailable,
            .available = RuntimeFilePageLoadingWaitAvailable,
            .begin = BeginRuntimeFilePageLoad,
            .register_waiter = RegisterRuntimeFilePageLoadWaiter,
            .wait = WaitForRuntimeFilePageLoad,
            .waiter_count = ReadRuntimeFilePageLoadWaiterCount,
            .complete = CompleteRuntimeFilePageLoad,
        }) != UserAddressSpaceStatus::Succeeded ||
        ConfigureUserFilePageCacheWritebackWait(FilePageWritebackWaitOperations{
            .context = nullptr,
            .owner_available = RuntimeFilePageWritebackOwnerAvailable,
            .available = RuntimeFilePageWritebackWaitAvailable,
            .begin = BeginRuntimeFilePageWriteback,
            .register_waiter = RegisterRuntimeFilePageWritebackWaiter,
            .wait = WaitForRuntimeFilePageWriteback,
            .complete = CompleteRuntimeFilePageWriteback,
        }) != UserAddressSpaceStatus::Succeeded ||
        ConfigureUserFilePageCacheReadaheadFeedback(FilePageReadaheadFeedbackOperations{
            .context = &file_readahead_feedback_ledger,
            .record = RecordRuntimeFileReadaheadFeedback,
        }) != UserAddressSpaceStatus::Succeeded ||
        file_description_manager.ConfigureReadahead(FileDescriptionReadaheadOperations{
            .context = nullptr,
            .register_stream = RegisterRuntimeFileReadaheadStream,
            .take_feedback = TakeRuntimeFileReadaheadFeedback,
            .cancel = CancelRuntimeFileReadaheadStream,
            .retire_stream = RetireRuntimeFileReadaheadStream,
            .pressure = ReadRuntimeFileReadaheadPressure,
            .schedule = ScheduleRuntimeFileReadahead,
        }) != FileDescriptionStatus::Succeeded ||
        AttachUserFilePageCache(vfs) != UserAddressSpaceStatus::Succeeded) {
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < process_runtime_limits.process_capacity; ++process_index) {
        ProcessRuntimeProcess &process = runtime_processes[process_index];
        if (process.active &&
            vfs.InitializeContext(process.file_system_context) != fs::Status::Succeeded) {
            for (uint64_t rollback_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
                 rollback_index < process_index; ++rollback_index) {
                ProcessRuntimeProcess &rollback_process = runtime_processes[rollback_index];
                if (rollback_process.active && rollback_process.file_system_context.initialized &&
                    vfs.ReleaseContext(rollback_process.file_system_context) !=
                        fs::Status::Succeeded) {
                    HaltProcessor();
                }
            }
            return ProcessRuntimeStatus::FileSystemFailure;
        }
    }
    if (AttachUserSwap(swap_device) != UserAddressSpaceStatus::Succeeded) {
        for (uint64_t rollback_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
             rollback_index < process_runtime_limits.process_capacity; ++rollback_index) {
            ProcessRuntimeProcess &rollback_process = runtime_processes[rollback_index];
            if (rollback_process.active && rollback_process.file_system_context.initialized &&
                vfs.ReleaseContext(rollback_process.file_system_context) != fs::Status::Succeeded) {
                HaltProcessor();
            }
        }
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    process_vfs = &vfs;
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
    const UserProgramImage image = SelectUserProgramImage(selection);
    UserAddressSpace address_space{};
    address_space_status = LoadUserAddressSpace(image.image, image.image_size_bytes, address_space,
                                                elf_validation_status);
    if (address_space_status != UserAddressSpaceStatus::Succeeded) {
        return address_space_status == UserAddressSpaceStatus::InvalidElf
                   ? ProcessRuntimeStatus::InvalidElf
                   : ProcessRuntimeStatus::AddressSpaceFailure;
    }
    if (PlanKernelProgramArguments(nullptr, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, nullptr,
                                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) !=
            ProcessRuntimeStatus::Succeeded ||
        !PopulateKernelProgramArguments(nullptr, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, nullptr,
                                        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, address_space)) {
        if (DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::InvalidArguments;
    }
    const ProcessRuntimeStatus register_status = RegisterRuntimeProcess(
        address_space, selection, OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX, nullptr,
        creation_result);
    if (register_status != ProcessRuntimeStatus::Succeeded &&
        DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }
    return register_status;
}

ProcessRuntimeStatus CreateInitialProcessFromPath(
    const uint8_t *const path, const uint64_t path_length_bytes,
    const KernelProgramString *const arguments, const uint64_t argument_count,
    const KernelProgramString *const environment, const uint64_t environment_count,
    ProcessCreationResult &creation_result, UserElfValidationStatus &elf_validation_status,
    UserAddressSpaceStatus &address_space_status) noexcept {
    if (!process_runtime_initialized || process_vfs == nullptr) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    const ProcessRuntimeStatus plan_status =
        PlanKernelProgramArguments(arguments, argument_count, environment, environment_count);
    if (plan_status != ProcessRuntimeStatus::Succeeded) {
        return plan_status;
    }
    fs::FsContext loading_context{};
    if (process_vfs->InitializeContext(loading_context) != fs::Status::Succeeded) {
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    UserAddressSpace address_space{};
    fs::NodeInformation executable_information{};
    ProcessRuntimeStatus status =
        LoadExecutableFromPath(loading_context, path, path_length_bytes, address_space,
                               elf_validation_status, address_space_status, executable_information);
    if (status == ProcessRuntimeStatus::Succeeded &&
        !PopulateKernelProgramArguments(arguments, argument_count, environment, environment_count,
                                        address_space)) {
        status = ProcessRuntimeStatus::InvalidArguments;
    }
    if (process_vfs->ReleaseContext(loading_context) != fs::Status::Succeeded) {
        if (address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    if (status == ProcessRuntimeStatus::Succeeded) {
        status = RegisterRuntimeProcess(address_space, UserProgramSelection::DiskExecutable,
                                        OS_KERNEL_PROCESS_RUNTIME_INVALID_PARENT_INDEX,
                                        &executable_information, creation_result);
    }
    if (status != ProcessRuntimeStatus::Succeeded &&
        address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }
    return status;
}

ProcessRuntimeStatus SpawnCurrentProcess(const os::abi::ProcessLaunchRequest &request,
                                         uint64_t &process_id) noexcept {
    process_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    RuntimeMutexGuard launch_guard{process_launch_mutex};
    if (request.path_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.path_length_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.path_length_bytes > sizeof(launch_path_buffer) ||
        CopyFromUser(request.path_address, request.path_length_bytes, launch_path_buffer,
                     sizeof(launch_path_buffer)) != UserMemoryCopyStatus::Succeeded) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    const ProcessRuntimeStatus plan_status = PlanUserProgramArguments(request);
    if (plan_status != ProcessRuntimeStatus::Succeeded) {
        return plan_status;
    }
    ThreadEntry parent_thread{};
    ProcessEntry parent_process{};
    if (!ReadCurrentThreadAndProcess(parent_thread, parent_process)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (ProcessCountLimitReached(parent_thread.process_index)) {
        return ProcessRuntimeStatus::ProcessLimitExceeded;
    }
    ProcessRuntimeProcess &parent_runtime_process = runtime_processes[parent_thread.process_index];
    UserAddressSpace address_space{};
    UserElfValidationStatus elf_validation_status = UserElfValidationStatus::Succeeded;
    UserAddressSpaceStatus address_space_status = UserAddressSpaceStatus::Succeeded;
    fs::NodeInformation executable_information{};
    ProcessRuntimeStatus status = LoadExecutableFromPath(
        parent_runtime_process.file_system_context, launch_path_buffer, request.path_length_bytes,
        address_space, elf_validation_status, address_space_status, executable_information);
    if (status == ProcessRuntimeStatus::Succeeded &&
        !AddressSpaceWithinLimit(
            address_space,
            parent_runtime_process
                .resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::AddressSpace)]
                .current)) {
        status = ProcessRuntimeStatus::ResourceLimitExceeded;
    }
    if (status == ProcessRuntimeStatus::Succeeded &&
        !PopulateUserProgramArguments(request, address_space)) {
        status = ProcessRuntimeStatus::InvalidArguments;
    }
    ProcessCreationResult creation_result{};
    if (status == ProcessRuntimeStatus::Succeeded) {
        status = RegisterRuntimeProcess(address_space, UserProgramSelection::DiskExecutable,
                                        parent_thread.process_index, &executable_information,
                                        creation_result);
    }
    if (status != ProcessRuntimeStatus::Succeeded &&
        address_space.root_physical_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        DestroyUserAddressSpace(address_space) != UserAddressSpaceStatus::Succeeded) {
        HaltProcessor();
    }
    if (status == ProcessRuntimeStatus::Succeeded) {
        process_id = creation_result.process_id;
    }
    return status;
}

ProcessRuntimeStatus ForkCurrentProcess(ExceptionFrame &frame, uint64_t &process_id) noexcept {
    process_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const uint64_t parent_thread_index = thread_scheduler.CurrentThreadIndex();
    if (!IsProcessSchedulingActive() || process_vfs == nullptr ||
        !CurrentFrameIsValid(parent_thread_index, frame)) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    ThreadEntry parent_thread{};
    ProcessEntry parent_process{};
    if (!ReadCurrentThreadAndProcess(parent_thread, parent_process) ||
        parent_thread.process_index >= process_runtime_limits.process_capacity) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (ProcessCountLimitReached(parent_thread.process_index)) {
        return ProcessRuntimeStatus::ProcessLimitExceeded;
    }
    ProcessRuntimeProcess &parent_runtime_process = runtime_processes[parent_thread.process_index];
    UserAddressSpace child_address_space{};
    const UserAddressSpaceStatus clone_status = CloneUserAddressSpaceForFork(
        parent_runtime_process.address_space, child_address_space,
        security::IsSuperuser(parent_runtime_process.file_system_context.credentials));
    if (clone_status != UserAddressSpaceStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_ADDRESS_SPACE_STAGE);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STATUS_PREFIX,
                                 static_cast<uint64_t>(clone_status));
        return clone_status == UserAddressSpaceStatus::PageTableCreationFailed ||
                       clone_status == UserAddressSpaceStatus::ForkReferenceExhausted
                   ? ProcessRuntimeStatus::ProcessLimitExceeded
                   : ProcessRuntimeStatus::ForkFailure;
    }

    uint64_t child_process_index = OS_KERNEL_PROCESS_INVALID_INDEX;
    ProcessId child_process_id{};
    bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus process_status = thread_scheduler.CreateProcess(
        child_address_space.root_physical_address, child_process_index, child_process_id);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (process_status != ThreadSchedulerStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_PROCESS_STAGE);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STATUS_PREFIX,
                                 static_cast<uint64_t>(process_status));
        if (DestroyUserAddressSpace(child_address_space) != UserAddressSpaceStatus::Succeeded ||
            RestoreUserAddressSpaceAfterFailedFork(parent_runtime_process.address_space) !=
                UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return process_status == ThreadSchedulerStatus::ProcessCapacityExhausted
                   ? ProcessRuntimeStatus::ProcessLimitExceeded
                   : ProcessRuntimeStatus::SchedulerFailure;
    }

    uint64_t kernel_stack_slot_index = OS_KERNEL_THREAD_INVALID_INDEX;
    for (uint64_t candidate_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         candidate_index < process_runtime_limits.thread_capacity; ++candidate_index) {
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
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_STACK_STAGE);
        if (!RollbackForkCreation(parent_runtime_process.address_space, child_address_space,
                                  child_process_index, OS_KERNEL_THREAD_INVALID_INDEX,
                                  OS_KERNEL_THREAD_INVALID_INDEX, false, false)) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::KernelStackFailure;
    }

    uint64_t child_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    ThreadId child_thread_id{};
    const UserContext &parent_context = AsUserContext(frame);
    interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus thread_status = thread_scheduler.CreateThread(
        child_process_index, kernel_stack_slot_index, parent_context.stack_pointer,
        parent_thread.thread_local_storage_base, parent_thread.signal_mask, child_thread_index,
        child_thread_id, false);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (thread_status != ThreadSchedulerStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_THREAD_STAGE);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STATUS_PREFIX,
                                 static_cast<uint64_t>(thread_status));
        if (!RollbackForkCreation(parent_runtime_process.address_space, child_address_space,
                                  child_process_index, OS_KERNEL_THREAD_INVALID_INDEX,
                                  kernel_stack_slot_index, false, false)) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    ExceptionFrame *child_saved_frame = nullptr;
    if (!BuildForkContextFrame(kernel_stack_slot_index, frame, child_saved_frame) ||
        SaveFxState(runtime_threads[parent_thread_index].extended_state) !=
            ExtendedStateStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_CONTEXT_STAGE);
        if (!RollbackForkCreation(parent_runtime_process.address_space, child_address_space,
                                  child_process_index, child_thread_index, kernel_stack_slot_index,
                                  false, false)) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ContextFrameFailure;
    }
    runtime_threads[child_thread_index].extended_state =
        runtime_threads[parent_thread_index].extended_state;

    ProcessRuntimeProcess &child_runtime_process = runtime_processes[child_process_index];
    const bool file_system_context_initialized =
        process_vfs->CloneContext(parent_runtime_process.file_system_context,
                                  child_runtime_process.file_system_context) ==
        fs::Status::Succeeded;
    const bool file_table_initialized =
        file_system_context_initialized &&
        child_runtime_process.file_table.CloneFrom(parent_runtime_process.file_table) ==
            FileTableStatus::Succeeded;
    if (!file_table_initialized) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_FILE_SYSTEM_STAGE);
        if (!RollbackForkCreation(parent_runtime_process.address_space, child_address_space,
                                  child_process_index, child_thread_index, kernel_stack_slot_index,
                                  file_table_initialized, file_system_context_initialized)) {
            HaltProcessor();
        }
        return file_system_context_initialized ? ProcessRuntimeStatus::DescriptorTableFailure
                                               : ProcessRuntimeStatus::FileSystemFailure;
    }
    InitializeResourceLimits(child_runtime_process, &parent_runtime_process);
    if (signal_manager.ForkProcess(parent_thread.process_index, child_process_index,
                                   child_process_id.value) != SignalManagerStatus::Succeeded ||
        signal_manager.RegisterThread(child_thread_index, child_process_index,
                                      child_thread_id.value, parent_thread.signal_mask) !=
            SignalManagerStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_PROCESS_TREE_STAGE);
        if (!RollbackForkCreation(parent_runtime_process.address_space, child_address_space,
                                  child_process_index, child_thread_index, kernel_stack_slot_index,
                                  true, true)) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::SignalFailure;
    }
    if (job_control_manager.ForkProcess(parent_thread.process_index, child_process_index,
                                        child_process_id.value) != JobControlStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_PROCESS_TREE_STAGE);
        if (!RollbackForkCreation(parent_runtime_process.address_space, child_address_space,
                                  child_process_index, child_thread_index, kernel_stack_slot_index,
                                  true, true)) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ProcessTreeFailure;
    }
    const ProcessTreeStatus tree_status = process_tree.RegisterChild(
        child_process_index, child_process_id.value, parent_thread.process_index);
    if (tree_status != ProcessTreeStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_FORK_PROCESS_TREE_STAGE);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_FAILURE_STATUS_PREFIX,
                                 static_cast<uint64_t>(tree_status));
        if (!RollbackForkCreation(parent_runtime_process.address_space, child_address_space,
                                  child_process_index, child_thread_index, kernel_stack_slot_index,
                                  true, true)) {
            HaltProcessor();
        }
        return ProcessRuntimeStatus::ProcessTreeFailure;
    }

    child_runtime_process.address_space = child_address_space;
    child_address_space = UserAddressSpace{};
    child_runtime_process.result = ProcessExecutionResult{
        .process_id = child_process_id.value,
        .selection = parent_runtime_process.result.selection,
        .termination_reason = ProcessTerminationReason::None,
        .exit_code = OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
        .exception_vector = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .exception_error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .exception_instruction_pointer = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .page_fault_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .system_call_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .root_physical_address = child_runtime_process.address_space.root_physical_address,
        .mapped_page_count = child_runtime_process.address_space.mapped_page_count,
        .run_tick_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .dispatch_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .pipe_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .pipe_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .file_system_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .file_system_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .console_bytes_read = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .console_bytes_written = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    child_runtime_process.active = true;
    runtime_threads[child_thread_index].saved_frame = child_saved_frame;
    runtime_threads[child_thread_index].user_stack_base_address =
        runtime_threads[parent_thread_index].user_stack_base_address;
    runtime_threads[child_thread_index].user_stack_size_bytes =
        runtime_threads[parent_thread_index].user_stack_size_bytes;
    runtime_threads[child_thread_index].thread_local_storage_base =
        parent_thread.thread_local_storage_base;
    runtime_threads[child_thread_index].thread_local_storage_size_bytes =
        runtime_threads[parent_thread_index].thread_local_storage_size_bytes;
    runtime_threads[child_thread_index].exit_value = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[child_thread_index].join_owner_thread_id =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[child_thread_index].blocked_system_call_number =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[child_thread_index].block_io_request_identifier =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[child_thread_index].user_kernel_continuation_entry_method =
        UserContextEntryMethod::Initial;
    runtime_threads[child_thread_index].blocked_system_call_restartable = false;
    runtime_threads[child_thread_index].user_kernel_continuation_active = false;
    runtime_threads[child_thread_index].user_kernel_continuation_system_call_active = false;
    runtime_threads[child_thread_index].user_kernel_continuation_swap_gs_required = false;
    runtime_threads[child_thread_index].user_kernel_continuation_uses_kernel_page_table = false;
    runtime_threads[child_thread_index].joinable = false;
    runtime_threads[child_thread_index].active = true;
    interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus publish_thread_status =
        thread_scheduler.PublishInitializingThread(child_thread_index);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (publish_thread_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    process_id = child_process_id.value;
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FORK_PREFIX, process_id);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus ExecCurrentProcess(ExceptionFrame &frame,
                                        const os::abi::ProcessLaunchRequest &request) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (!IsProcessSchedulingActive() || process_vfs == nullptr ||
        !CurrentFrameIsValid(thread_index, frame)) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    RuntimeMutexGuard launch_guard{process_launch_mutex};
    if (request.path_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.path_length_bytes == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        request.path_length_bytes > sizeof(launch_path_buffer) ||
        CopyFromUser(request.path_address, request.path_length_bytes, launch_path_buffer,
                     sizeof(launch_path_buffer)) != UserMemoryCopyStatus::Succeeded) {
        return ProcessRuntimeStatus::InvalidArguments;
    }
    const ProcessRuntimeStatus plan_status = PlanUserProgramArguments(request);
    if (plan_status != ProcessRuntimeStatus::Succeeded) {
        return plan_status;
    }
    ThreadEntry thread{};
    ProcessEntry process_entry{};
    if (!ReadCurrentThreadAndProcess(thread, process_entry) ||
        thread_index >= process_runtime_limits.thread_capacity) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (!WaitForProcessKernelContinuations(thread.process_index, thread_index)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeProcess &process = runtime_processes[thread.process_index];
    UserAddressSpace candidate_address_space{};
    UserElfValidationStatus elf_validation_status = UserElfValidationStatus::Succeeded;
    UserAddressSpaceStatus address_space_status = UserAddressSpaceStatus::Succeeded;
    fs::NodeInformation executable_information{};
    ProcessRuntimeStatus status =
        LoadExecutableFromPath(process.file_system_context, launch_path_buffer,
                               request.path_length_bytes, candidate_address_space,
                               elf_validation_status, address_space_status, executable_information);
    if (status == ProcessRuntimeStatus::Succeeded &&
        !AddressSpaceWithinLimit(
            candidate_address_space,
            process.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::AddressSpace)]
                .current)) {
        status = ProcessRuntimeStatus::ResourceLimitExceeded;
    }
    if (status == ProcessRuntimeStatus::Succeeded &&
        !PopulateUserProgramArguments(request, candidate_address_space)) {
        status = ProcessRuntimeStatus::InvalidArguments;
    }
    if (status != ProcessRuntimeStatus::Succeeded) {
        if (candidate_address_space.root_physical_address !=
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
            DestroyUserAddressSpace(candidate_address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        return status;
    }

    UserAddressSpace previous_address_space = process.address_space;
    const uint64_t previous_entry_vector = frame.vector;
    SetActiveUserAddressSpace(nullptr);
    ActivateKernelPageTable();
    if (!ActivateUserPageTable(candidate_address_space.root_physical_address)) {
        if (!ActivateUserPageTable(previous_address_space.root_physical_address) ||
            DestroyUserAddressSpace(candidate_address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        SetActiveUserAddressSpace(&process.address_space);
        return ProcessRuntimeStatus::PageTableActivationFailure;
    }
    if (!ActivateUserPageTable(previous_address_space.root_physical_address)) {
        HaltProcessor();
    }
    ActivateKernelPageTable();

    uint64_t cancelled_futex_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!CancelPrivateFutexRange(
            previous_address_space.address_space_identifier, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, true, cancelled_futex_thread_count)) {
        if (!ActivateUserPageTable(previous_address_space.root_physical_address) ||
            DestroyUserAddressSpace(candidate_address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        SetActiveUserAddressSpace(&process.address_space);
        return ProcessRuntimeStatus::FutexFailure;
    }
    bool interrupts_were_enabled = scheduler_lock.Lock();
    uint64_t terminated_sibling_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const ThreadSchedulerStatus sibling_status = thread_scheduler.TerminateProcessSiblings(
        thread.process_index, thread_index, terminated_sibling_count);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (sibling_status != ThreadSchedulerStatus::Succeeded ||
        !ReapProcessSiblingsForExec(thread.process_index, thread_index)) {
        HaltProcessor();
    }
    if (!FlushOutstandingUserFilePages()) {
        HaltProcessor();
    }
    if (signal_manager.ExecProcess(thread.process_index, thread_index) !=
        SignalManagerStatus::Succeeded) {
        HaltProcessor();
    }
    user_thread_runtime_statistics.exec_cancelled_thread_count += terminated_sibling_count;
    static_cast<void>(cancelled_futex_thread_count);

    interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus commit_status = thread_scheduler.CommitProcessImage(
        thread.process_index, thread_index, candidate_address_space.root_physical_address,
        program_argument_plan.Layout().stack_pointer);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (commit_status != ThreadSchedulerStatus::Succeeded) {
        if (!ActivateUserPageTable(previous_address_space.root_physical_address) ||
            DestroyUserAddressSpace(candidate_address_space) != UserAddressSpaceStatus::Succeeded) {
            HaltProcessor();
        }
        SetActiveUserAddressSpace(&process.address_space);
        return ProcessRuntimeStatus::SchedulerFailure;
    }

    process.address_space = candidate_address_space;
    candidate_address_space = UserAddressSpace{};
    process.result.selection = UserProgramSelection::DiskExecutable;
    process.result.root_physical_address = process.address_space.root_physical_address;
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    ApplyExecutableCredentials(process.file_system_context, executable_information);
    uint64_t closed_descriptor_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (process.file_table.CloseOnExec(closed_descriptor_count) != FileTableStatus::Succeeded ||
        DestroyUserAddressSpace(previous_address_space) != UserAddressSpaceStatus::Succeeded ||
        !ActivateUserPageTable(process.address_space.root_physical_address)) {
        HaltProcessor();
    }
    SetActiveUserAddressSpace(&process.address_space);

    UserContext &context = AsUserContext(frame);
    context = UserContext{};
    context.common.register_rdi = program_argument_plan.Layout().argument_count;
    context.common.register_rsi = program_argument_plan.Layout().argument_vector_address;
    context.common.register_rdx = program_argument_plan.Layout().environment_vector_address;
    context.common.vector = previous_entry_vector;
    context.common.error_code = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    context.common.instruction_pointer = process.address_space.entry_virtual_address;
    context.common.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    context.common.flags = OS_KERNEL_PROCESS_RUNTIME_INITIAL_USER_FLAGS;
    context.stack_pointer = program_argument_plan.Layout().stack_pointer;
    context.stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR);
    runtime_threads[thread_index].saved_frame = &frame;
    runtime_threads[thread_index].user_stack_base_address =
        OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS;
    runtime_threads[thread_index].user_stack_size_bytes = OS_KERNEL_USER_STACK_SIZE_BYTES;
    runtime_threads[thread_index].thread_local_storage_base = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].thread_local_storage_size_bytes =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].exit_value = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].join_owner_thread_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].blocked_system_call_number =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].block_io_request_identifier =
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_threads[thread_index].user_kernel_continuation_entry_method =
        UserContextEntryMethod::Initial;
    runtime_threads[thread_index].blocked_system_call_restartable = false;
    runtime_threads[thread_index].user_kernel_continuation_active = false;
    runtime_threads[thread_index].user_kernel_continuation_system_call_active = false;
    runtime_threads[thread_index].user_kernel_continuation_swap_gs_required = false;
    runtime_threads[thread_index].user_kernel_continuation_uses_kernel_page_table = false;
    runtime_threads[thread_index].joinable = false;
    WriteModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR,
                               OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_EXEC_PREFIX, process.result.process_id);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessWaitStatus TryWaitCurrentProcess(const uint64_t requested_process_id,
                                        os::abi::ProcessWaitResult &wait_result) noexcept {
    wait_result = os::abi::ProcessWaitResult{};
    if (!IsProcessSchedulingActive() ||
        requested_process_id == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return ProcessWaitStatus::InvalidArgument;
    }
    if (!ReapExitedThreads()) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    ThreadEntry parent_thread{};
    ProcessEntry parent_process{};
    if (!ReadCurrentThreadAndProcess(parent_thread, parent_process)) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    ProcessTreeWaitResult tree_result{};
    const ProcessTreeStatus tree_status =
        process_tree.TryWait(parent_thread.process_index, requested_process_id, tree_result);
    if (tree_status == ProcessTreeStatus::ChildStillRunning) {
        return ProcessWaitStatus::WouldBlock;
    }
    if (tree_status == ProcessTreeStatus::NoMatchingChild) {
        return ProcessWaitStatus::NoChild;
    }
    if (tree_status != ProcessTreeStatus::Succeeded ||
        tree_result.process_index >= process_runtime_limits.process_capacity) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus reap_status =
        thread_scheduler.ReapZombieProcess(tree_result.process_index);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (reap_status != ThreadSchedulerStatus::Succeeded ||
        !RemoveSignalProcessIfPresent(tree_result.process_index)) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    if (job_control_manager.RemoveProcess(tree_result.process_index) !=
        JobControlStatus::Succeeded) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    runtime_processes[tree_result.process_index].active = false;
    const os::abi::ProcessTerminationReason termination_reason =
        tree_result.exit_status.termination_reason == ProcessTreeTerminationReason::Exited
            ? os::abi::ProcessTerminationReason::Exited
        : tree_result.exit_status.termination_reason == ProcessTreeTerminationReason::Signal
            ? os::abi::ProcessTerminationReason::Signal
            : os::abi::ProcessTerminationReason::Exception;
    wait_result = os::abi::ProcessWaitResult{
        .process_id = tree_result.process_id,
        .parent_process_id = parent_process.process_id.value,
        .termination_reason = termination_reason,
        .exit_code = tree_result.exit_status.exit_code,
        .exception_vector = tree_result.exit_status.exception_vector,
    };
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_WAIT_PREFIX, tree_result.process_id);
    return ProcessWaitStatus::Succeeded;
}

ProcessWaitStatus
TryWaitCurrentProcessEvent(const uint64_t requested_process_id, const uint64_t wait_flags,
                           os::abi::ProcessWaitEventResult &wait_result) noexcept {
    wait_result = os::abi::ProcessWaitEventResult{};
    const uint64_t observable_flags = wait_flags & (os::abi::OS_ABI_PROCESS_WAIT_EXITED_FLAG |
                                                    os::abi::OS_ABI_PROCESS_WAIT_STOPPED_FLAG |
                                                    os::abi::OS_ABI_PROCESS_WAIT_CONTINUED_FLAG);
    if (!IsProcessSchedulingActive() ||
        requested_process_id == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        observable_flags == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        (wait_flags & ~os::abi::OS_ABI_PROCESS_WAIT_VALID_FLAG_MASK) !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return ProcessWaitStatus::InvalidArgument;
    }
    if (!ReapExitedThreads()) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    ThreadEntry parent_thread{};
    ProcessEntry parent_process{};
    if (!ReadCurrentThreadAndProcess(parent_thread, parent_process)) {
        return ProcessWaitStatus::RuntimeFailure;
    }
    uint64_t tree_wait_flags = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if ((observable_flags & os::abi::OS_ABI_PROCESS_WAIT_EXITED_FLAG) !=
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        tree_wait_flags |= OS_KERNEL_PROCESS_TREE_WAIT_EXITED_FLAG;
    }
    if ((observable_flags & os::abi::OS_ABI_PROCESS_WAIT_STOPPED_FLAG) !=
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        tree_wait_flags |= OS_KERNEL_PROCESS_TREE_WAIT_STOPPED_FLAG;
    }
    if ((observable_flags & os::abi::OS_ABI_PROCESS_WAIT_CONTINUED_FLAG) !=
        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        tree_wait_flags |= OS_KERNEL_PROCESS_TREE_WAIT_CONTINUED_FLAG;
    }
    ProcessTreeWaitEventResult tree_result{};
    const ProcessTreeStatus tree_status = process_tree.TryWaitEvent(
        parent_thread.process_index, requested_process_id, tree_wait_flags, tree_result);
    if (tree_status == ProcessTreeStatus::ChildStillRunning) {
        return ProcessWaitStatus::WouldBlock;
    }
    if (tree_status == ProcessTreeStatus::NoMatchingChild) {
        return ProcessWaitStatus::NoChild;
    }
    if (tree_status != ProcessTreeStatus::Succeeded ||
        tree_result.process_index >= process_runtime_limits.process_capacity) {
        return ProcessWaitStatus::RuntimeFailure;
    }

    os::abi::ProcessTerminationReason termination_reason = os::abi::ProcessTerminationReason::None;
    if (tree_result.event_type == ProcessTreeEventType::Exited) {
        const bool interrupts_were_enabled = scheduler_lock.Lock();
        const ThreadSchedulerStatus reap_status =
            thread_scheduler.ReapZombieProcess(tree_result.process_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (reap_status != ThreadSchedulerStatus::Succeeded) {
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_CLEANUP_STAGE_PREFIX,
                                     OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_SCHEDULER_STAGE);
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_CLEANUP_STATUS_PREFIX,
                                     static_cast<uint64_t>(reap_status));
            return ProcessWaitStatus::RuntimeFailure;
        }
        if (!RemoveSignalProcessIfPresent(tree_result.process_index)) {
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_CLEANUP_STAGE_PREFIX,
                                     OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_SIGNAL_STAGE);
            return ProcessWaitStatus::RuntimeFailure;
        }
        const JobControlStatus job_status =
            job_control_manager.RemoveProcess(tree_result.process_index);
        if (job_status != JobControlStatus::Succeeded) {
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_CLEANUP_STAGE_PREFIX,
                                     OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_JOB_STAGE);
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_WAIT_EVENT_CLEANUP_STATUS_PREFIX,
                                     static_cast<uint64_t>(job_status));
            return ProcessWaitStatus::RuntimeFailure;
        }
        runtime_processes[tree_result.process_index].active = false;
        termination_reason =
            tree_result.exit_status.termination_reason == ProcessTreeTerminationReason::Exited
                ? os::abi::ProcessTerminationReason::Exited
            : tree_result.exit_status.termination_reason == ProcessTreeTerminationReason::Signal
                ? os::abi::ProcessTerminationReason::Signal
                : os::abi::ProcessTerminationReason::Exception;
    }
    const os::abi::ProcessWaitEventType event_type =
        tree_result.event_type == ProcessTreeEventType::Exited
            ? os::abi::ProcessWaitEventType::Exited
        : tree_result.event_type == ProcessTreeEventType::Stopped
            ? os::abi::ProcessWaitEventType::Stopped
            : os::abi::ProcessWaitEventType::Continued;
    wait_result = os::abi::ProcessWaitEventResult{
        .process_id = tree_result.process_id,
        .parent_process_id = parent_process.process_id.value,
        .event_type = event_type,
        .termination_reason = termination_reason,
        .exit_code = tree_result.exit_status.exit_code,
        .exception_vector = tree_result.exit_status.exception_vector,
        .signal_number = tree_result.signal_number,
    };
    return ProcessWaitStatus::Succeeded;
}

UserThreadStatus CreateCurrentProcessThread(ExceptionFrame &frame,
                                            const os::abi::ThreadCreateRequest &request,
                                            uint64_t &thread_id) noexcept {
    thread_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() || !CurrentFrameIsValid(current_thread_index, frame) ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserThreadStatus::RuntimeFailure;
    }
    ProcessRuntimeProcess &process = runtime_processes[current_thread.process_index];
    if (!UserThreadRequestIsValid(process, request) ||
        ValidateUserWritableMemory(request.stack_pointer,
                                   os::abi::OS_ABI_THREAD_ENTRY_STACK_REMAINDER_BYTES) !=
            UserMemoryCopyStatus::Succeeded ||
        ValidateUserWritableMemory(request.thread_local_storage_base,
                                   OS_KERNEL_PROCESS_RUNTIME_THREAD_LOCAL_STORAGE_PROBE_BYTES) !=
            UserMemoryCopyStatus::Succeeded) {
        return UserThreadStatus::InvalidMemory;
    }
    const uint64_t requested_stack_end = request.stack_base_address + request.stack_size_bytes;
    for (uint64_t candidate_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         candidate_index < process_runtime_limits.thread_capacity; ++candidate_index) {
        ThreadEntry candidate_thread{};
        if (!runtime_threads[candidate_index].active ||
            thread_scheduler.ReadThread(candidate_index, candidate_thread) !=
                ThreadSchedulerStatus::Succeeded ||
            candidate_thread.process_index != current_thread.process_index) {
            continue;
        }
        const ProcessRuntimeThread &candidate_runtime = runtime_threads[candidate_index];
        const uint64_t candidate_stack_end =
            candidate_runtime.user_stack_base_address + candidate_runtime.user_stack_size_bytes;
        const bool stack_overlaps = request.stack_base_address < candidate_stack_end &&
                                    candidate_runtime.user_stack_base_address < requested_stack_end;
        if (stack_overlaps ||
            (candidate_runtime.thread_local_storage_base != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
             candidate_runtime.thread_local_storage_base == request.thread_local_storage_base)) {
            return UserThreadStatus::InvalidMemory;
        }
    }

    const uint64_t kernel_stack_slot_index = FindAvailableKernelStackSlot();
    if (kernel_stack_slot_index == OS_KERNEL_THREAD_INVALID_INDEX ||
        GetKernelStackManager().TryCreate(kernel_stack_slot_index) !=
            KernelStackManagerStatus::Succeeded) {
        return UserThreadStatus::ThreadLimitExceeded;
    }

    uint64_t created_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    ThreadId created_thread_id{};
    bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus create_status = thread_scheduler.CreateThread(
        current_thread.process_index, kernel_stack_slot_index, request.stack_pointer,
        request.thread_local_storage_base, current_thread.signal_mask, created_thread_index,
        created_thread_id);
    if (create_status != ThreadSchedulerStatus::Succeeded) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
            KernelStackManagerStatus::Succeeded) {
            HaltProcessor();
        }
        return create_status == ThreadSchedulerStatus::ThreadCapacityExhausted ||
                       create_status == ThreadSchedulerStatus::ProcessThreadLimitReached
                   ? UserThreadStatus::ThreadLimitExceeded
                   : UserThreadStatus::RuntimeFailure;
    }

    ExceptionFrame *saved_frame = nullptr;
    if (!BuildThreadContextFrame(kernel_stack_slot_index, request, saved_frame) ||
        InitializeFxSaveArea(runtime_threads[created_thread_index].extended_state) !=
            ExtendedStateStatus::Succeeded) {
        const ThreadSchedulerStatus discard_status =
            thread_scheduler.DiscardReadyThread(created_thread_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (discard_status != ThreadSchedulerStatus::Succeeded ||
            GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded) {
            HaltProcessor();
        }
        return UserThreadStatus::RuntimeFailure;
    }
    if (signal_manager.RegisterThread(created_thread_index, current_thread.process_index,
                                      created_thread_id.value, current_thread.signal_mask) !=
        SignalManagerStatus::Succeeded) {
        const ThreadSchedulerStatus discard_status =
            thread_scheduler.DiscardReadyThread(created_thread_index);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (discard_status != ThreadSchedulerStatus::Succeeded ||
            GetKernelStackManager().TryDestroy(kernel_stack_slot_index) !=
                KernelStackManagerStatus::Succeeded) {
            HaltProcessor();
        }
        return UserThreadStatus::RuntimeFailure;
    }

    ProcessRuntimeThread &runtime_thread = runtime_threads[created_thread_index];
    runtime_thread.saved_frame = saved_frame;
    runtime_thread.user_stack_base_address = request.stack_base_address;
    runtime_thread.user_stack_size_bytes = request.stack_size_bytes;
    runtime_thread.thread_local_storage_base = request.thread_local_storage_base;
    runtime_thread.thread_local_storage_size_bytes = OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    runtime_thread.exit_value = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_thread.join_owner_thread_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_thread.blocked_system_call_number = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_thread.block_io_request_identifier = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_thread.user_kernel_continuation_entry_method = UserContextEntryMethod::Initial;
    runtime_thread.blocked_system_call_restartable = false;
    runtime_thread.user_kernel_continuation_active = false;
    runtime_thread.user_kernel_continuation_system_call_active = false;
    runtime_thread.user_kernel_continuation_swap_gs_required = false;
    runtime_thread.user_kernel_continuation_uses_kernel_page_table = false;
    runtime_thread.joinable = true;
    runtime_thread.active = true;
    // Ready Thread 的上下文和运行时元数据必须在恢复中断前一次性发布。
    scheduler_lock.Unlock(interrupts_were_enabled);
    ++user_thread_runtime_statistics.create_count;
    if (IsPowerOfTwoCounter(user_thread_runtime_statistics.create_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_THREAD_CREATE_COUNT_PREFIX,
                                 user_thread_runtime_statistics.create_count);
    }
    thread_id = created_thread_id.value;
    return UserThreadStatus::Succeeded;
}

ExceptionFrame *ExitCurrentUserThread(ExceptionFrame &frame, const uint64_t exit_value) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!process_scheduling_active || !CurrentFrameIsValid(thread_index, frame) ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        HaltProcessor();
    }
    if (current_process.live_thread_count == OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) {
        return CompleteCurrentThread(frame, ProcessTerminationReason::Exited,
                                     static_cast<int64_t>(exit_value),
                                     OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    runtime_thread.exit_value = exit_value;
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        HaltProcessor();
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status =
        thread_scheduler.TerminateCurrentThread(decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    ++user_thread_runtime_statistics.exit_count;
    if (IsPowerOfTwoCounter(user_thread_runtime_statistics.exit_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_THREAD_EXIT_COUNT_PREFIX,
                                 user_thread_runtime_statistics.exit_count);
    }
    WakeRequiredThreads(WaitCondition::ThreadJoin, WakeReason::ConditionSatisfied);

    if (!decision.switched) {
        ReturnUserModeToProcessDispatcher(false);
    }
    return ActivateScheduledUserOrReturnToDispatcher(decision);
}

UserThreadStatus TryJoinCurrentProcessThread(const uint64_t requested_thread_id,
                                             os::abi::ThreadJoinResult &join_result) noexcept {
    join_result = os::abi::ThreadJoinResult{};
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() ||
        requested_thread_id == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserThreadStatus::InvalidArgument;
    }
    if (requested_thread_id == current_thread.thread_id.value) {
        return UserThreadStatus::Deadlock;
    }
    uint64_t target_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
    ThreadEntry target_thread{};
    const ThreadSchedulerStatus find_status = thread_scheduler.FindProcessThread(
        current_thread.process_index, ThreadId{.value = requested_thread_id}, target_thread_index,
        target_thread);
    if (find_status == ThreadSchedulerStatus::ThreadNotFound) {
        return UserThreadStatus::ThreadNotFound;
    }
    if (find_status != ThreadSchedulerStatus::Succeeded ||
        target_thread_index >= process_runtime_limits.thread_capacity) {
        return UserThreadStatus::RuntimeFailure;
    }
    ProcessRuntimeThread &target_runtime = runtime_threads[target_thread_index];
    if (!target_runtime.active || !target_runtime.joinable) {
        return UserThreadStatus::ThreadNotFound;
    }
    if (target_runtime.join_owner_thread_id != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        target_runtime.join_owner_thread_id != current_thread.thread_id.value) {
        return UserThreadStatus::AlreadyJoined;
    }
    target_runtime.join_owner_thread_id = current_thread.thread_id.value;
    if (target_thread.state != ThreadState::Exited) {
        return UserThreadStatus::WouldBlock;
    }

    const uint64_t exit_value = target_runtime.exit_value;
    if (GetKernelStackManager().TryDestroy(target_thread.kernel_stack_slot_index) !=
            KernelStackManagerStatus::Succeeded ||
        !RemoveSignalThreadIfPresent(target_thread_index)) {
        return UserThreadStatus::RuntimeFailure;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus reap_status =
        thread_scheduler.ReapExitedThread(target_thread_index);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (reap_status != ThreadSchedulerStatus::Succeeded) {
        return UserThreadStatus::RuntimeFailure;
    }
    target_runtime = ProcessRuntimeThread{};
    join_result = os::abi::ThreadJoinResult{
        .thread_id = requested_thread_id,
        .exit_value = exit_value,
    };
    ++user_thread_runtime_statistics.join_count;
    if (IsPowerOfTwoCounter(user_thread_runtime_statistics.join_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_THREAD_JOIN_COUNT_PREFIX,
                                 user_thread_runtime_statistics.join_count);
    }
    return UserThreadStatus::Succeeded;
}

UserThreadStatus SetCurrentThreadLocalStorage(const uint64_t thread_local_storage_base) noexcept {
    if (!IsProcessSchedulingActive()) {
        return UserThreadStatus::RuntimeFailure;
    }
    if (thread_local_storage_base != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        (thread_local_storage_base % os::abi::OS_ABI_THREAD_LOCAL_STORAGE_ALIGNMENT_BYTES !=
             OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
         ValidateUserWritableMemory(thread_local_storage_base,
                                    OS_KERNEL_PROCESS_RUNTIME_THREAD_LOCAL_STORAGE_PROBE_BYTES) !=
             UserMemoryCopyStatus::Succeeded)) {
        return UserThreadStatus::InvalidMemory;
    }
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (thread_index >= process_runtime_limits.thread_capacity) {
        return UserThreadStatus::RuntimeFailure;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus status =
        thread_scheduler.SetCurrentThreadLocalStorageBase(thread_local_storage_base);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (status != ThreadSchedulerStatus::Succeeded) {
        return UserThreadStatus::RuntimeFailure;
    }
    runtime_threads[thread_index].thread_local_storage_base = thread_local_storage_base;
    runtime_threads[thread_index].thread_local_storage_size_bytes =
        thread_local_storage_base == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE
            ? OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE
            : OS_KERNEL_PROCESS_RUNTIME_THREAD_LOCAL_STORAGE_PROBE_BYTES;
    WriteModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR, thread_local_storage_base);
    if (ReadModelSpecificRegister(OS_KERNEL_PROCESSOR_IA32_FS_BASE_MSR) !=
        thread_local_storage_base) {
        return UserThreadStatus::RuntimeFailure;
    }
    ++user_thread_runtime_statistics.thread_local_storage_update_count;
    return UserThreadStatus::Succeeded;
}

UserSignalStatus SetCurrentProcessSignalAction(const uint64_t signal_number,
                                               const os::abi::SignalAction &action,
                                               os::abi::SignalAction &previous_action) noexcept {
    previous_action = os::abi::SignalAction{};
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserSignalStatus::RuntimeFailure;
    }
    if (action.disposition == os::abi::SignalDisposition::Handler &&
        (!IsUserProgramVirtualAddressRange(action.handler_address,
                                           OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) ||
         !IsUserProgramVirtualAddressRange(action.restorer_address,
                                           OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT))) {
        return UserSignalStatus::InvalidMemory;
    }
    const UserSignalStatus status = MapSignalManagerStatus(signal_manager.SetAction(
        current_thread.process_index, signal_number, action, previous_action));
    if (status == UserSignalStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_ACTION_NUMBER_PREFIX,
                                 signal_number);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_ACTION_DISPOSITION_PREFIX,
                                 static_cast<uint64_t>(action.disposition));
    }
    return status;
}

UserSignalStatus SetCurrentThreadSignalMask(const uint64_t signal_mask,
                                            uint64_t &previous_signal_mask) noexcept {
    previous_signal_mask = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive()) {
        return UserSignalStatus::RuntimeFailure;
    }
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    const SignalManagerStatus signal_status =
        signal_manager.SetThreadMask(thread_index, signal_mask, previous_signal_mask);
    if (signal_status != SignalManagerStatus::Succeeded) {
        return MapSignalManagerStatus(signal_status);
    }
    SignalThreadState signal_thread{};
    if (signal_manager.ReadThread(thread_index, signal_thread) != SignalManagerStatus::Succeeded) {
        return UserSignalStatus::RuntimeFailure;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status =
        thread_scheduler.SetCurrentThreadSignalMask(signal_thread.signal_mask);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return scheduler_status == ThreadSchedulerStatus::Succeeded ? UserSignalStatus::Succeeded
                                                                : UserSignalStatus::RuntimeFailure;
}

UserSignalStatus SendSignalToProcess(const uint64_t process_id,
                                     const uint64_t signal_number) noexcept {
    if (!IsProcessSchedulingActive()) {
        return UserSignalStatus::RuntimeFailure;
    }
    const uint64_t queued_signal_count_before = signal_manager.Statistics().queued_signal_count;
    uint64_t selected_thread_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
    const SignalManagerStatus status =
        signal_manager.SendToProcess(process_id, signal_number, selected_thread_index);
    if (status != SignalManagerStatus::Succeeded) {
        return MapSignalManagerStatus(status);
    }
    if (selected_thread_index != OS_KERNEL_SIGNAL_INVALID_INDEX &&
        !WakeThreadForSignal(selected_thread_index)) {
        return UserSignalStatus::RuntimeFailure;
    }
    if (signal_number == os::abi::OS_ABI_SIGNAL_CONTINUE_NUMBER ||
        signal_number == os::abi::OS_ABI_SIGNAL_KILL_NUMBER) {
        uint64_t process_index = OS_KERNEL_SIGNAL_INVALID_INDEX;
        if (signal_manager.FindProcess(process_id, process_index) !=
                SignalManagerStatus::Succeeded ||
            !ContinueStoppedProcess(process_index)) {
            return UserSignalStatus::RuntimeFailure;
        }
    }
    const uint64_t queued_signal_count = signal_manager.Statistics().queued_signal_count;
    if (queued_signal_count != queued_signal_count_before &&
        IsPowerOfTwoCounter(queued_signal_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_QUEUED_PREFIX,
                                 queued_signal_count);
    }
    return UserSignalStatus::Succeeded;
}

UserSignalStatus SendSignalToProcessGroup(const uint64_t process_group_id,
                                          const uint64_t signal_number,
                                          uint64_t &target_process_count) noexcept {
    target_process_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    // 控制终端可能在所有用户线程都阻塞、调度器暂时没有 current thread 时
    // 从键盘中断投递信号；此时生命周期仍有效，并且信号正用于唤醒目标线程。
    if (!process_scheduling_active) {
        return UserSignalStatus::RuntimeFailure;
    }
    uint64_t selected_threads[OS_KERNEL_PROCESS_CAPACITY_LIMIT]{};
    uint64_t selected_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const SignalManagerStatus status = signal_manager.SendToProcessGroup(
        process_group_id, signal_number, selected_threads, OS_KERNEL_PROCESS_CAPACITY_LIMIT,
        selected_thread_count, target_process_count);
    if (status != SignalManagerStatus::Succeeded) {
        return MapSignalManagerStatus(status);
    }
    for (uint64_t selected_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         selected_index < selected_thread_count; ++selected_index) {
        if (!WakeThreadForSignal(selected_threads[selected_index])) {
            return UserSignalStatus::RuntimeFailure;
        }
    }
    if (signal_number == os::abi::OS_ABI_SIGNAL_CONTINUE_NUMBER ||
        signal_number == os::abi::OS_ABI_SIGNAL_KILL_NUMBER) {
        for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
             process_index < process_runtime_limits.process_capacity; ++process_index) {
            JobControlProcessState job_state{};
            if (job_control_manager.ReadProcess(process_index, job_state) !=
                    JobControlStatus::Succeeded ||
                job_state.process_group_id != process_group_id) {
                continue;
            }
            if (!ContinueStoppedProcess(process_index)) {
                return UserSignalStatus::RuntimeFailure;
            }
        }
    }
    return UserSignalStatus::Succeeded;
}

UserSignalStatus GetCurrentProcessGroup(uint64_t &process_group_id) noexcept {
    process_group_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserSignalStatus::RuntimeFailure;
    }
    JobControlProcessState state{};
    const JobControlStatus status =
        job_control_manager.ReadProcess(current_thread.process_index, state);
    if (status == JobControlStatus::Succeeded) {
        process_group_id = state.process_group_id;
    }
    return MapJobControlStatus(status);
}

UserSignalStatus SetCurrentProcessGroup(const uint64_t process_group_id) noexcept {
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserSignalStatus::RuntimeFailure;
    }
    return SetCurrentProcessGroupFor(current_process.process_id.value, process_group_id);
}

UserSignalStatus SetCurrentProcessGroupFor(const uint64_t process_id,
                                           const uint64_t process_group_id) noexcept {
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserSignalStatus::RuntimeFailure;
    }
    const uint64_t effective_process_id = process_id == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE
                                              ? current_process.process_id.value
                                              : process_id;
    uint64_t target_process_index = OS_KERNEL_JOB_CONTROL_INVALID_INDEX;
    if (job_control_manager.FindProcess(effective_process_id, target_process_index) !=
        JobControlStatus::Succeeded) {
        return UserSignalStatus::ProcessNotFound;
    }
    if (target_process_index != current_thread.process_index) {
        ProcessTreeEntry target_tree_entry{};
        if (process_tree.Read(target_process_index, target_tree_entry) !=
                ProcessTreeStatus::Succeeded ||
            target_tree_entry.parent_process_index != current_thread.process_index ||
            (target_tree_entry.state != ProcessTreeState::Alive &&
             target_tree_entry.state != ProcessTreeState::Stopped &&
             target_tree_entry.state != ProcessTreeState::Zombie)) {
            return UserSignalStatus::PermissionDenied;
        }
    }
    JobControlProcessState previous_state{};
    if (job_control_manager.ReadProcess(target_process_index, previous_state) !=
        JobControlStatus::Succeeded) {
        return UserSignalStatus::RuntimeFailure;
    }
    const uint64_t effective_group_id = process_group_id == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE
                                            ? effective_process_id
                                            : process_group_id;
    const JobControlStatus job_status = job_control_manager.SetProcessGroup(
        current_thread.process_index, target_process_index, effective_group_id);
    if (job_status != JobControlStatus::Succeeded) {
        return MapJobControlStatus(job_status);
    }
    const SignalManagerStatus signal_status =
        signal_manager.SetProcessGroup(target_process_index, effective_group_id);
    if (signal_status != SignalManagerStatus::Succeeded) {
        static_cast<void>(job_control_manager.SetProcessGroup(
            current_thread.process_index, target_process_index, previous_state.process_group_id));
        return MapSignalManagerStatus(signal_status);
    }
    return UserSignalStatus::Succeeded;
}

UserSignalStatus CreateCurrentSession(uint64_t &session_id) noexcept {
    session_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserSignalStatus::RuntimeFailure;
    }
    const JobControlStatus status =
        job_control_manager.CreateSession(current_thread.process_index, session_id);
    if (status != JobControlStatus::Succeeded) {
        return MapJobControlStatus(status);
    }
    if (signal_manager.SetProcessGroup(current_thread.process_index,
                                       current_process.process_id.value) !=
        SignalManagerStatus::Succeeded) {
        return UserSignalStatus::RuntimeFailure;
    }
    return UserSignalStatus::Succeeded;
}

UserSignalStatus GetCurrentSession(uint64_t &session_id) noexcept {
    session_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserSignalStatus::RuntimeFailure;
    }
    JobControlProcessState state{};
    const JobControlStatus status =
        job_control_manager.ReadProcess(current_thread.process_index, state);
    if (status == JobControlStatus::Succeeded) {
        session_id = state.session_id;
    }
    return MapJobControlStatus(status);
}

UserSignalStatus GetCurrentTerminalInformation(os::abi::TerminalInformation &information) noexcept {
    information = os::abi::TerminalInformation{};
    uint64_t session_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const UserSignalStatus session_status = GetCurrentSession(session_id);
    if (session_status != UserSignalStatus::Succeeded) {
        return session_status;
    }
    if (session_id != process_terminal.ControllingSessionId()) {
        return UserSignalStatus::PermissionDenied;
    }
    information = os::abi::TerminalInformation{
        .terminal_id = process_terminal.Identifier(),
        .controlling_session_id = process_terminal.ControllingSessionId(),
        .foreground_process_group_id = process_terminal.ForegroundProcessGroupId(),
    };
    return UserSignalStatus::Succeeded;
}

UserSignalStatus SetCurrentTerminalForegroundGroup(const uint64_t process_group_id) noexcept {
    if (process_group_id == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return UserSignalStatus::InvalidArgument;
    }
    uint64_t session_id = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const UserSignalStatus session_status = GetCurrentSession(session_id);
    if (session_status != UserSignalStatus::Succeeded) {
        return session_status;
    }
    if (!job_control_manager.GroupBelongsToSession(process_group_id, session_id)) {
        return UserSignalStatus::PermissionDenied;
    }
    const TerminalStatus terminal_status =
        process_terminal.SetForegroundProcessGroup(session_id, process_group_id);
    return terminal_status == TerminalStatus::Succeeded ? UserSignalStatus::Succeeded
           : terminal_status == TerminalStatus::PermissionDenied
               ? UserSignalStatus::PermissionDenied
               : UserSignalStatus::InvalidArgument;
}

UserSignalStatus SetCurrentTerminalInputMode(const os::abi::TerminalInputMode mode) noexcept {
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    if (!IsProcessSchedulingActive() ||
        !ReadCurrentThreadAndProcess(current_thread, current_process)) {
        return UserSignalStatus::RuntimeFailure;
    }
    JobControlProcessState state{};
    if (job_control_manager.ReadProcess(current_thread.process_index, state) !=
        JobControlStatus::Succeeded) {
        return UserSignalStatus::RuntimeFailure;
    }
    const TerminalStatus status =
        process_terminal.SetInputMode(state.session_id, state.process_group_id, mode);
    return status == TerminalStatus::Succeeded          ? UserSignalStatus::Succeeded
           : status == TerminalStatus::PermissionDenied ? UserSignalStatus::PermissionDenied
                                                        : UserSignalStatus::InvalidArgument;
}

ExceptionFrame *PrepareCurrentThreadSignalDelivery(ExceptionFrame &frame) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (!IsProcessSchedulingActive() || !CurrentFrameIsValid(thread_index, frame) ||
        thread_index >= process_runtime_limits.thread_capacity) {
        return TerminateCurrentProcessFromInvalidReturn(frame);
    }

    WakeReason wake_reason = WakeReason::None;
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus wake_status =
        thread_scheduler.ConsumeCurrentThreadWakeReason(wake_reason);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (wake_status != ThreadSchedulerStatus::Succeeded) {
        return TerminateCurrentProcessFromInvalidReturn(frame);
    }

    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    const uint64_t blocked_system_call_number = runtime_thread.blocked_system_call_number;
    const bool blocked_system_call_restartable = runtime_thread.blocked_system_call_restartable;
    runtime_thread.blocked_system_call_number = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_thread.block_io_request_identifier = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_thread.blocked_system_call_restartable = false;

    SignalDelivery delivery{};
    if (signal_manager.BeginThreadDelivery(thread_index, delivery) !=
        SignalManagerStatus::Succeeded) {
        return TerminateCurrentProcessFromInvalidReturn(frame);
    }
    if (delivery.kind == SignalDeliveryKind::None) {
        return &frame;
    }
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_DISPOSITION_PREFIX,
                             static_cast<uint64_t>(delivery.action.disposition));
    if (delivery.kind == SignalDeliveryKind::DefaultTerminate) {
        return TerminateCurrentProcessFromSignal(frame, delivery.signal_number);
    }
    if (delivery.kind == SignalDeliveryKind::DefaultStop) {
        return StopCurrentProcessFromSignal(frame, delivery.signal_number);
    }
    if (delivery.kind == SignalDeliveryKind::DefaultContinue) {
        const VgaTextConsole vga_console{VgaTextConsole::Hardware(WritePort8)};
        if (!vga_console.TryWriteDiagnosticString(
                OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DEFAULT_CONTINUE_DELIVERED)) {
            HaltProcessor();
        }
        return &frame;
    }

    SignalThreadState signal_thread{};
    if (signal_manager.ReadThread(thread_index, signal_thread) != SignalManagerStatus::Succeeded ||
        delivery.action.handler_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        delivery.action.restorer_address == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_STATE_STAGE);
        return TerminateCurrentProcessFromSignal(frame, delivery.signal_number);
    }
    const bool restart_wait =
        wake_reason == WakeReason::Signal && blocked_system_call_restartable &&
        blocked_system_call_number != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        (delivery.action.flags & os::abi::OS_ABI_SIGNAL_ACTION_RESTART_WAIT_FLAG) !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE &&
        frame.instruction_pointer >= OS_KERNEL_PROCESS_RUNTIME_SYSTEM_CALL_INSTRUCTION_SIZE_BYTES;
    UserContext saved_context = AsUserContext(frame);
    if (wake_reason == WakeReason::Signal) {
        if (restart_wait) {
            saved_context.common.instruction_pointer -=
                OS_KERNEL_PROCESS_RUNTIME_SYSTEM_CALL_INSTRUCTION_SIZE_BYTES;
            saved_context.common.register_rax = blocked_system_call_number;
        }
        signal_manager.RecordInterruptedWait(restart_wait);
    }

    const uint64_t previous_stack_pointer = saved_context.stack_pointer;
    if (previous_stack_pointer <
        sizeof(os::abi::SignalFrame) + OS_KERNEL_PROCESS_RUNTIME_SIGNAL_RETURN_SLOT_SIZE_BYTES) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_STACK_POINTER_STAGE);
        return TerminateCurrentProcessFromSignal(frame, delivery.signal_number);
    }
    const uint64_t unaligned_frame_address = previous_stack_pointer - sizeof(os::abi::SignalFrame);
    const uint64_t frame_address =
        unaligned_frame_address & ~(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_STACK_ALIGNMENT_BYTES -
                                    OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT);
    if (frame_address < OS_KERNEL_PROCESS_RUNTIME_SIGNAL_RETURN_SLOT_SIZE_BYTES) {
        return TerminateCurrentProcessFromSignal(frame, delivery.signal_number);
    }
    const uint64_t return_slot_address =
        frame_address - OS_KERNEL_PROCESS_RUNTIME_SIGNAL_RETURN_SLOT_SIZE_BYTES;
    const uint64_t stack_end_address =
        runtime_thread.user_stack_base_address + runtime_thread.user_stack_size_bytes;
    if (stack_end_address < runtime_thread.user_stack_base_address ||
        return_slot_address < runtime_thread.user_stack_base_address ||
        frame_address > stack_end_address - sizeof(os::abi::SignalFrame)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_STACK_BOUNDS_STAGE);
        return TerminateCurrentProcessFromSignal(frame, delivery.signal_number);
    }

    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    if ((runtime_thread.user_stack_base_address == OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS &&
         PrepareUserStackRange(process.address_space, return_slot_address,
                               previous_stack_pointer) != UserAddressSpaceStatus::Succeeded) ||
        ResolveUserReturnMemory(process.address_space, delivery.action.restorer_address,
                                return_slot_address +
                                    OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT) !=
            UserVirtualMemoryStatus::Succeeded ||
        ResolveUserReturnMemory(process.address_space, delivery.action.handler_address,
                                frame_address + sizeof(os::abi::SignalFrame)) !=
            UserVirtualMemoryStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_MEMORY_STAGE);
        return TerminateCurrentProcessFromSignal(frame, delivery.signal_number);
    }

    os::abi::SignalFrame signal_frame{
        .magic = os::abi::OS_ABI_SIGNAL_FRAME_MAGIC,
        .version = os::abi::OS_ABI_SIGNAL_FRAME_VERSION,
        .size_bytes = sizeof(os::abi::SignalFrame),
        .cookie = signal_thread.active_frame_cookie,
        .signal_number = delivery.signal_number,
        .previous_mask = delivery.previous_mask,
        .restorer_address = delivery.action.restorer_address,
        .reserved = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .context = {},
    };
    static_assert(sizeof(signal_frame.context) == sizeof(saved_context));
    memcpy(&signal_frame.context, &saved_context, sizeof(saved_context));
    const uint64_t return_address = delivery.action.restorer_address;
    if (CopyToUser(frame_address, sizeof(signal_frame),
                   reinterpret_cast<const uint8_t *>(&signal_frame),
                   sizeof(signal_frame)) != UserMemoryCopyStatus::Succeeded ||
        CopyToUser(return_slot_address, sizeof(return_address),
                   reinterpret_cast<const uint8_t *>(&return_address),
                   sizeof(return_address)) != UserMemoryCopyStatus::Succeeded ||
        signal_manager.CommitHandlerFrame(thread_index, frame_address, signal_frame.cookie) !=
            SignalManagerStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_FRAME_STAGE);
        return TerminateCurrentProcessFromSignal(frame, delivery.signal_number);
    }

    UserContext &handler_context = AsUserContext(frame);
    handler_context.common.register_rdi = delivery.signal_number;
    handler_context.common.register_rsi = frame_address;
    handler_context.common.instruction_pointer = delivery.action.handler_address;
    handler_context.stack_pointer = return_slot_address;
    const bool mask_interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus mask_status =
        thread_scheduler.SetCurrentThreadSignalMask(signal_thread.signal_mask);
    scheduler_lock.Unlock(mask_interrupts_were_enabled);
    if (mask_status != ThreadSchedulerStatus::Succeeded) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERY_FAILURE_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_SIGNAL_FAILURE_MASK_STAGE);
        return TerminateCurrentProcessFromSignal(frame, delivery.signal_number);
    }
    const uint64_t delivery_count = signal_manager.Statistics().handler_delivery_count;
    if (IsPowerOfTwoCounter(delivery_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_DELIVERED_PREFIX, delivery_count);
    }
    return &frame;
}

ExceptionFrame *ReturnFromCurrentThreadSignal(ExceptionFrame &frame,
                                              const uint64_t user_frame_address) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    SignalThreadState signal_thread{};
    if (!IsProcessSchedulingActive() || !CurrentFrameIsValid(thread_index, frame) ||
        thread_index >= process_runtime_limits.thread_capacity ||
        signal_manager.ReadThread(thread_index, signal_thread) != SignalManagerStatus::Succeeded ||
        !signal_thread.frame_active || user_frame_address != signal_thread.active_frame_address ||
        user_frame_address % OS_KERNEL_PROCESS_RUNTIME_SIGNAL_STACK_ALIGNMENT_BYTES !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        signal_manager.RecordRejectedFrame();
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_REJECTED_FRAME_PREFIX,
                                 signal_manager.Statistics().rejected_frame_count);
        return TerminateCurrentProcessFromInvalidReturn(frame);
    }
    const ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    const uint64_t stack_end_address =
        runtime_thread.user_stack_base_address + runtime_thread.user_stack_size_bytes;
    if (stack_end_address < runtime_thread.user_stack_base_address ||
        user_frame_address < runtime_thread.user_stack_base_address ||
        user_frame_address > stack_end_address - sizeof(os::abi::SignalFrame)) {
        signal_manager.RecordRejectedFrame();
        return TerminateCurrentProcessFromInvalidReturn(frame);
    }

    os::abi::SignalFrame signal_frame{};
    if (CopyFromUser(user_frame_address, sizeof(signal_frame),
                     reinterpret_cast<uint8_t *>(&signal_frame),
                     sizeof(signal_frame)) != UserMemoryCopyStatus::Succeeded ||
        signal_frame.magic != os::abi::OS_ABI_SIGNAL_FRAME_MAGIC ||
        signal_frame.version != os::abi::OS_ABI_SIGNAL_FRAME_VERSION ||
        signal_frame.size_bytes != sizeof(os::abi::SignalFrame) ||
        signal_frame.reserved != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        (signal_frame.previous_mask & ~os::abi::OS_ABI_SIGNAL_VALID_SET) !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        (signal_frame.previous_mask & os::abi::OS_ABI_SIGNAL_UNMASKABLE_SET) !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        signal_manager.ValidateHandlerFrame(
            thread_index, user_frame_address, signal_frame.cookie, signal_frame.signal_number,
            signal_frame.restorer_address,
            signal_frame.previous_mask) != SignalManagerStatus::Succeeded) {
        signal_manager.RecordRejectedFrame();
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_REJECTED_FRAME_PREFIX,
                                 signal_manager.Statistics().rejected_frame_count);
        return TerminateCurrentProcessFromInvalidReturn(frame);
    }

    UserContext restored_context{};
    static_assert(sizeof(signal_frame.context) == sizeof(restored_context));
    memcpy(&restored_context, &signal_frame.context, sizeof(restored_context));
    const UserContextRequirements requirements{
        .virtual_address_width_bits = GetNativeSystemCallConfiguration().virtual_address_width_bits,
        .user_code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR),
        .user_stack_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_DATA_SELECTOR),
    };
    if (ValidateUserContext(restored_context, requirements) != UserContextStatus::Succeeded ||
        restored_context.stack_pointer <= runtime_thread.user_stack_base_address ||
        restored_context.stack_pointer > stack_end_address ||
        ResolveUserReturnMemory(
            CurrentRuntimeProcess().address_space, restored_context.common.instruction_pointer,
            restored_context.stack_pointer) != UserVirtualMemoryStatus::Succeeded ||
        signal_manager.CompleteHandlerFrame(
            thread_index, user_frame_address, signal_frame.cookie, signal_frame.signal_number,
            signal_frame.restorer_address,
            signal_frame.previous_mask) != SignalManagerStatus::Succeeded) {
        signal_manager.RecordRejectedFrame();
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_REJECTED_FRAME_PREFIX,
                                 signal_manager.Statistics().rejected_frame_count);
        return TerminateCurrentProcessFromInvalidReturn(frame);
    }
    AsUserContext(frame) = restored_context;
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus mask_status =
        thread_scheduler.SetCurrentThreadSignalMask(signal_frame.previous_mask);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (mask_status != ThreadSchedulerStatus::Succeeded) {
        return TerminateCurrentProcessFromInvalidReturn(frame);
    }
    return &frame;
}

PrivateFutexWaitStatus WaitCurrentProcessPrivateFutex(ExceptionFrame &frame,
                                                      const uint64_t user_address,
                                                      const uint32_t expected_value,
                                                      const bool deadline_enabled,
                                                      const uint64_t deadline_nanoseconds,
                                                      ExceptionFrame *&resume_frame) noexcept {
    resume_frame = &frame;
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    uint32_t observed_value = 0U;
    if (!IsProcessSchedulingActive() || !CurrentFrameIsValid(thread_index, frame) ||
        !ReadCurrentThreadAndProcess(current_thread, current_process) ||
        user_address % os::abi::OS_ABI_PRIVATE_FUTEX_WORD_SIZE_BYTES !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return PrivateFutexWaitStatus::InvalidArgument;
    }
    if (CopyFromUser(user_address, sizeof(observed_value),
                     reinterpret_cast<uint8_t *>(&observed_value),
                     sizeof(observed_value)) != UserMemoryCopyStatus::Succeeded) {
        return PrivateFutexWaitStatus::InvalidMemory;
    }
    ProcessRuntimeProcess &process = runtime_processes[current_thread.process_index];
    const PrivateFutexKey key{
        .address_space_identifier = process.address_space.address_space_identifier,
        .user_address = user_address,
    };
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    runtime_thread.blocked_system_call_number =
        static_cast<uint64_t>(deadline_enabled ? os::abi::SystemCallNumber::WaitPrivateFutexUntil
                                               : os::abi::SystemCallNumber::WaitPrivateFutex);
    runtime_thread.blocked_system_call_restartable = false;
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        return PrivateFutexWaitStatus::RuntimeFailure;
    }

    ThreadSchedulingDecision decision{};
    bool interrupts_were_enabled = scheduler_lock.Lock();
    // 在同一调度器临界区内复查用户值并入队，保证 wake 无法穿过 compare-and-block。
    observed_value = 0U;
    if (CopyFromUser(user_address, sizeof(observed_value),
                     reinterpret_cast<uint8_t *>(&observed_value),
                     sizeof(observed_value)) != UserMemoryCopyStatus::Succeeded) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return PrivateFutexWaitStatus::InvalidMemory;
    }
    if (observed_value != expected_value) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return PrivateFutexWaitStatus::ValueChanged;
    }
    const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
    if (deadline_enabled && deadline_nanoseconds <= now_nanoseconds) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return PrivateFutexWaitStatus::TimedOut;
    }
    uint64_t futex_entry_index = OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
    WaitQueue *wait_queue = nullptr;
    const PrivateFutexStatus acquire_status =
        private_futex_manager.Acquire(key, futex_entry_index, wait_queue);
    if (acquire_status != PrivateFutexStatus::Succeeded || wait_queue == nullptr) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return acquire_status == PrivateFutexStatus::EntryCapacityExhausted
                   ? PrivateFutexWaitStatus::CapacityExhausted
                   : PrivateFutexWaitStatus::RuntimeFailure;
    }
    const ThreadSchedulerStatus block_status =
        deadline_enabled
            ? thread_scheduler.BlockCurrentThreadUntil(*wait_queue, WaitCondition::PrivateFutex,
                                                       now_nanoseconds, deadline_nanoseconds,
                                                       decision)
            : thread_scheduler.BlockCurrentThread(*wait_queue, WaitCondition::PrivateFutex,
                                                  decision);
    if (block_status != ThreadSchedulerStatus::Succeeded) {
        bool released = false;
        const PrivateFutexStatus release_status =
            private_futex_manager.ReleaseIfEmpty(futex_entry_index, released);
        scheduler_lock.Unlock(interrupts_were_enabled);
        if (release_status != PrivateFutexStatus::Succeeded || !released) {
            HaltProcessor();
        }
        return PrivateFutexWaitStatus::RuntimeFailure;
    }
    if (private_futex_manager.RecordWaitPrepared() != PrivateFutexStatus::Succeeded) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        HaltProcessor();
    }
    scheduler_lock.Unlock(interrupts_were_enabled);
    const uint64_t wait_count = private_futex_manager.Statistics().wait_prepare_count;
    if (IsPowerOfTwoCounter(wait_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FUTEX_WAIT_COUNT_PREFIX, wait_count);
    }

    if (!decision.switched) {
        ReturnUserModeToProcessDispatcher(false);
    }
    resume_frame = ActivateScheduledUserOrReturnToDispatcher(decision);
    return PrivateFutexWaitStatus::Succeeded;
}

TimedWaitStatus SleepCurrentThreadUntil(ExceptionFrame &frame, const uint64_t deadline_nanoseconds,
                                        ExceptionFrame *&resume_frame) noexcept {
    resume_frame = &frame;
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (!IsProcessSchedulingActive() || !CurrentFrameIsValid(thread_index, frame)) {
        return TimedWaitStatus::InvalidArgument;
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    runtime_thread.saved_frame = &frame;
    runtime_thread.blocked_system_call_number =
        static_cast<uint64_t>(os::abi::SystemCallNumber::SleepUntil);
    runtime_thread.blocked_system_call_restartable = false;
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        return TimedWaitStatus::RuntimeFailure;
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    // 关中断后重新读取时钟，避免读时钟与入队之间跨过截止时刻。
    const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
    const ThreadSchedulerStatus block_status = thread_scheduler.BlockCurrentThreadUntil(
        sleep_wait_queue, WaitCondition::Sleep, now_nanoseconds, deadline_nanoseconds, decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (block_status == ThreadSchedulerStatus::DeadlineAlreadyReached) {
        return TimedWaitStatus::DeadlineReached;
    }
    if (block_status != ThreadSchedulerStatus::Succeeded) {
        return TimedWaitStatus::RuntimeFailure;
    }
    if (!decision.switched) {
        ReturnUserModeToProcessDispatcher(false);
    }
    resume_frame = ActivateScheduledUserOrReturnToDispatcher(decision);
    return TimedWaitStatus::Succeeded;
}

PrivateFutexWaitStatus WakeCurrentProcessPrivateFutex(const uint64_t user_address,
                                                      const uint64_t maximum_wake_count,
                                                      uint64_t &woken_thread_count) noexcept {
    woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    ThreadEntry current_thread{};
    ProcessEntry current_process{};
    uint32_t user_value = 0U;
    if (!IsProcessSchedulingActive() ||
        !ReadCurrentThreadAndProcess(current_thread, current_process) ||
        maximum_wake_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        user_address % os::abi::OS_ABI_PRIVATE_FUTEX_WORD_SIZE_BYTES !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return PrivateFutexWaitStatus::InvalidArgument;
    }
    if (CopyFromUser(user_address, sizeof(user_value), reinterpret_cast<uint8_t *>(&user_value),
                     sizeof(user_value)) != UserMemoryCopyStatus::Succeeded) {
        return PrivateFutexWaitStatus::InvalidMemory;
    }
    ProcessRuntimeProcess &process = runtime_processes[current_thread.process_index];
    const PrivateFutexKey key{
        .address_space_identifier = process.address_space.address_space_identifier,
        .user_address = user_address,
    };
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    uint64_t entry_index = OS_KERNEL_PRIVATE_FUTEX_INVALID_INDEX;
    WaitQueue *wait_queue = nullptr;
    const PrivateFutexStatus find_status = private_futex_manager.Find(key, entry_index, wait_queue);
    if (find_status == PrivateFutexStatus::EntryNotFound) {
        static_cast<void>(private_futex_manager.RecordWakeOperation());
        scheduler_lock.Unlock(interrupts_were_enabled);
        return PrivateFutexWaitStatus::Succeeded;
    }
    if (find_status != PrivateFutexStatus::Succeeded || wait_queue == nullptr) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return PrivateFutexWaitStatus::RuntimeFailure;
    }
    while (woken_thread_count < maximum_wake_count &&
           wait_queue->Statistics().waiting_thread_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        uint64_t woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
        bool wake_won = false;
        if (thread_scheduler.WakeOne(*wait_queue, WakeReason::ConditionSatisfied,
                                     woken_thread_index,
                                     wake_won) != ThreadSchedulerStatus::Succeeded ||
            !wake_won || woken_thread_index >= process_runtime_limits.thread_capacity ||
            runtime_threads[woken_thread_index].saved_frame == nullptr) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            return PrivateFutexWaitStatus::RuntimeFailure;
        }
        runtime_threads[woken_thread_index].saved_frame->register_rax =
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        ++woken_thread_count;
    }
    bool released = false;
    if (private_futex_manager.ReleaseIfEmpty(entry_index, released) !=
            PrivateFutexStatus::Succeeded ||
        !released || private_futex_manager.RecordWakeOperation() != PrivateFutexStatus::Succeeded) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return PrivateFutexWaitStatus::RuntimeFailure;
    }
    scheduler_lock.Unlock(interrupts_were_enabled);
    const uint64_t wake_operation_count = private_futex_manager.Statistics().wake_operation_count;
    if (IsPowerOfTwoCounter(wake_operation_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FUTEX_WAKE_COUNT_PREFIX,
                                 wake_operation_count);
    }
    return PrivateFutexWaitStatus::Succeeded;
}

ProcessRuntimeStatus ExecuteProcesses() noexcept {
    if (!process_runtime_initialized) {
        return ProcessRuntimeStatus::NotInitialized;
    }
    if (process_scheduling_active) {
        return ProcessRuntimeStatus::AlreadyActive;
    }
    if (!PrepareRuntimeFileWritebackWorker()) {
        return ProcessRuntimeStatus::ThreadFailure;
    }

    const bool interrupts_were_enabled = DisableInterrupts();
    process_scheduling_active = true;
    kernel_thread_dispatch_active = true;
    while (process_scheduling_active) {
        if (!HasLiveUserThread() && !kernel_work_thread_stop_requested) {
            const bool readahead_draining =
                file_readahead_request_queue.Statistics().active_request_count !=
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
            if ((readahead_draining && !ScheduleRuntimeFileReadaheadWork()) ||
                (!readahead_draining && !RequestRuntimeFileWritebackWorkerStop())) {
                process_scheduling_active = false;
                kernel_thread_dispatch_active = false;
                RestoreInterrupts(interrupts_were_enabled);
                return ProcessRuntimeStatus::ThreadFailure;
            }
        }
        ThreadSchedulingDecision decision{};
        ThreadSchedulerStatus scheduler_status = ThreadSchedulerStatus::Succeeded;
        const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
        if (current_thread_index == OS_KERNEL_THREAD_INVALID_INDEX) {
            const bool scheduler_interrupts_were_enabled = scheduler_lock.Lock();
            scheduler_status = thread_scheduler.Start(decision);
            scheduler_lock.Unlock(scheduler_interrupts_were_enabled);
        } else {
            decision.current_thread_index = current_thread_index;
        }
        if (scheduler_status == ThreadSchedulerStatus::NoReadyThread) {
            if (decision.completed) {
                if (!CollectTerminalInitProcess() || !ReapExitedKernelThreads() ||
                    !ReleaseRuntimeFileWritebackWorker()) {
                    process_scheduling_active = false;
                    kernel_thread_dispatch_active = false;
                    RestoreInterrupts(interrupts_were_enabled);
                    return ProcessRuntimeStatus::KernelStackFailure;
                }
                if (page_aging_worker_failed || background_reclaim_worker_failed ||
                    file_readahead_worker_failed) {
                    process_scheduling_active = false;
                    kernel_thread_dispatch_active = false;
                    RestoreInterrupts(interrupts_were_enabled);
                    return ProcessRuntimeStatus::ThreadFailure;
                }
                process_scheduling_active = false;
                kernel_thread_dispatch_active = false;
                break;
            }
            EnableInterruptsWaitAndDisable();
            continue;
        }
        if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
            process_scheduling_active = false;
            kernel_thread_dispatch_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::SchedulerFailure;
        }
        ThreadEntry scheduled_thread{};
        if (thread_scheduler.ReadThread(decision.current_thread_index, scheduled_thread) !=
            ThreadSchedulerStatus::Succeeded) {
            process_scheduling_active = false;
            kernel_thread_dispatch_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::SchedulerFailure;
        }
        if (scheduled_thread.kind == ThreadKind::User) {
            ProcessRuntimeThread &runtime_thread = runtime_threads[decision.current_thread_index];
            if (runtime_thread.user_kernel_continuation_active) {
                if (!ActivateUserKernelContinuation(decision.current_thread_index)) {
                    WriteProcessRuntimeValue(
                        OS_KERNEL_PROCESS_RUNTIME_USER_THREAD_ACTIVATION_INDEX_PREFIX,
                        decision.current_thread_index);
                    WriteProcessRuntimeValue(
                        OS_KERNEL_PROCESS_RUNTIME_USER_CONTINUATION_ACTIVATION_STAGE_PREFIX,
                        user_kernel_continuation_activation_failure_stage);
                    process_scheduling_active = false;
                    kernel_thread_dispatch_active = false;
                    RestoreInterrupts(interrupts_were_enabled);
                    return ProcessRuntimeStatus::PageTableActivationFailure;
                }
                OsKernelEnterScheduledUserContinuation(
                    runtime_thread.kernel_stack_pointer,
                    runtime_thread.user_kernel_continuation_swap_gs_required);
            } else if (!ActivateThread(decision.current_thread_index)) {
                WriteProcessRuntimeValue(
                    OS_KERNEL_PROCESS_RUNTIME_USER_THREAD_ACTIVATION_INDEX_PREFIX,
                    decision.current_thread_index);
                WriteProcessRuntimeValue(
                    OS_KERNEL_PROCESS_RUNTIME_USER_THREAD_ACTIVATION_STAGE_PREFIX,
                    user_thread_activation_failure_stage);
                process_scheduling_active = false;
                kernel_thread_dispatch_active = false;
                RestoreInterrupts(interrupts_were_enabled);
                return ProcessRuntimeStatus::PageTableActivationFailure;
            } else {
                OsKernelEnterScheduledProcess(runtime_thread.saved_frame);
            }
        } else if (scheduled_thread.kind == ThreadKind::Kernel) {
            if (!ActivateKernelRuntimeThread(decision.current_thread_index)) {
                process_scheduling_active = false;
                kernel_thread_dispatch_active = false;
                RestoreInterrupts(interrupts_were_enabled);
                return ProcessRuntimeStatus::ThreadFailure;
            }
            OsKernelEnterScheduledKernelThread(
                runtime_threads[decision.current_thread_index].kernel_stack_pointer);
        } else {
            HaltProcessor();
        }
        if (ReadPageTableRoot() != GetKernelPageTableRoot()) {
            HaltProcessor();
        }
        if (!ReapExitedThreads() || !ReapExitedKernelThreads()) {
            process_scheduling_active = false;
            kernel_thread_dispatch_active = false;
            RestoreInterrupts(interrupts_were_enabled);
            return ProcessRuntimeStatus::KernelStackFailure;
        }
    }
    // 所有用户与后台请求都已结束；资源快照前回收无引用 clean cache，避免把持久缓存误判为
    // Process 泄漏。运行期缓存行为不受此 quiescent 收束影响。
    if (TrimUserFilePageCache() != UserVirtualMemoryStatus::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::ResourceLeakDetected;
    }
    frames_after_processes = GetPhysicalFrameAllocatorStatistics();
    virtual_addresses_after_processes = GetKernelVirtualAddressAllocator().Statistics();
    kernel_stacks_after_processes = GetKernelStackManager().Statistics();
    virtual_memory_areas_after_processes = GetUserVirtualMemoryPoolStatistics();
    user_page_references_after_processes = GetUserPageReferenceStatistics();
    if (GetKernelStackManager().Validate() != KernelStackManagerStatus::Succeeded ||
        kernel_stacks_after_processes.active_stack_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_after_processes.capacity !=
            virtual_memory_areas_before_processes.capacity ||
        virtual_memory_areas_after_processes.active_descriptor_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        virtual_memory_areas_after_processes.free_descriptor_count !=
            virtual_memory_areas_after_processes.capacity ||
        virtual_memory_areas_after_processes.successful_acquire_count <
            virtual_memory_areas_before_processes.successful_acquire_count ||
        virtual_memory_areas_after_processes.release_count <
            virtual_memory_areas_before_processes.release_count ||
        virtual_memory_areas_after_processes.successful_acquire_count -
                virtual_memory_areas_before_processes.successful_acquire_count !=
            virtual_memory_areas_after_processes.release_count -
                virtual_memory_areas_before_processes.release_count ||
        user_page_references_after_processes.active_entry_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        user_page_references_after_processes.active_reference_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        !ValidateUserMemoryManagement() ||
        GetUserMemoryOvercommitStatistics().committed_page_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        GetUserSwapStatistics().active_slot_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        private_futex_manager.Validate() != PrivateFutexStatus::Succeeded ||
        private_futex_manager.Statistics().active_entry_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        private_futex_manager.Statistics().waiting_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        thread_scheduler.ValidateWaitQueue(kernel_work_wait_queue) !=
            ThreadSchedulerStatus::Succeeded ||
        kernel_work_wait_queue.Statistics().waiting_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        thread_scheduler.ValidateWaitQueue(block_io_wait_queue) !=
            ThreadSchedulerStatus::Succeeded ||
        block_io_wait_queue.Statistics().waiting_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        thread_scheduler.ValidateWaitQueue(block_io_completion_wait_queue) !=
            ThreadSchedulerStatus::Succeeded ||
        block_io_completion_wait_queue.Statistics().waiting_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        thread_scheduler.ValidateWaitQueue(process_io_drain_wait_queue) !=
            ThreadSchedulerStatus::Succeeded ||
        process_io_drain_wait_queue.Statistics().waiting_thread_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        block_io_coordinator.Validate() != BlockIoStatus::Succeeded ||
        block_io_coordinator.Statistics().active_request_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        !ValidateRuntimeFilePageLoadState() || !ValidateRuntimeFilePageWritebackState() ||
        file_readahead_request_queue.Validate() != FileReadaheadRequestStatus::Succeeded ||
        file_readahead_request_queue.Statistics().active_request_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        file_readahead_feedback_ledger.Validate() != FileReadaheadFeedbackStatus::Succeeded ||
        file_readahead_feedback_ledger.Statistics().active_stream_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        file_readahead_feedback_ledger.Statistics().retiring_stream_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        file_readahead_feedback_ledger.Statistics().active_task_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        kernel_work_queue.Validate() != WorkQueueStatus::Succeeded ||
        kernel_work_queue.Statistics().registered_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        background_reclaim_controller.Validate() != BackgroundReclaimStatus::Succeeded ||
        background_reclaim_controller.Statistics().state != BackgroundReclaimState::Sleeping ||
        page_aging_manager.Validate() != PageAgingStatus::Succeeded ||
        page_aging_manager.Statistics().tracked_page_count !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        page_aging_manager.Statistics().observation_active) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_MEMORY_STAGE);
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::ResourceLeakDetected;
    }
    const ThreadSchedulerStatistics final_scheduler_statistics = thread_scheduler.Statistics();
    fs::ResourceUsage vfs_resource_usage{};
    if (process_vfs == nullptr ||
        process_vfs->ReadResourceUsage(vfs_resource_usage) != fs::Status::Succeeded) {
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::FileSystemFailure;
    }
    const ResourceSnapshotSupplementalCounts supplemental_counts{
        .process_count = final_scheduler_statistics.owned_process_count,
        .thread_count = final_scheduler_statistics.owned_thread_count,
        .file_description_count = kernel_object_manager.Statistics().active_file_description_count,
        .vnode_count = vfs_resource_usage.vnode_count,
        .cache_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
        .block_request_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
    };
    uint64_t resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (thread_scheduler.Validate() != ThreadSchedulerStatus::Succeeded) {
        resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_SCHEDULER_DETAIL;
    } else if (process_tree.Validate() != ProcessTreeStatus::Succeeded) {
        resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_TREE_DETAIL;
    } else if (job_control_manager.Validate() != JobControlStatus::Succeeded ||
               job_control_manager.Statistics().active_process_count !=
                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_JOB_DETAIL;
    } else if (process_terminal.Validate() != TerminalStatus::Succeeded) {
        resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_TERMINAL_DETAIL;
    } else if (signal_manager.Validate() != SignalManagerStatus::Succeeded ||
               signal_manager.Statistics().active_process_count !=
                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
               signal_manager.Statistics().active_thread_count !=
                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_SIGNAL_DETAIL;
    } else if (GetKernelResourceSnapshot(supplemental_counts, resource_snapshot_after_processes) !=
               ResourceSnapshotStatus::Succeeded) {
        resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_SNAPSHOT_DETAIL;
    } else if (!DiscountPersistentVfsResources(resource_snapshot_after_processes,
                                               vfs_resource_usage)) {
        resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_DISCOUNT_DETAIL;
    } else if (CompareResourceSnapshots(
                   resource_snapshot_before_processes, resource_snapshot_after_processes,
                   resource_snapshot_difference) != ResourceSnapshotStatus::Succeeded ||
               resource_snapshot_difference.changed_fields_mask !=
                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
               resource_snapshot_difference.changed_field_count !=
                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        resource_validation_detail = OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_COMPARE_DETAIL;
    }
    if (resource_validation_detail != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_STAGE_PREFIX,
                                 OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_GLOBAL_STAGE);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_DETAIL_PREFIX,
                                 resource_validation_detail);
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_DIFFERENCE_PREFIX,
                                 resource_snapshot_difference.changed_fields_mask);
        if (resource_validation_detail ==
            OS_KERNEL_PROCESS_RUNTIME_RESOURCE_VALIDATION_JOB_DETAIL) {
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_JOB_VALIDATION_STATUS_PREFIX,
                                     static_cast<uint64_t>(job_control_manager.Validate()));
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_PROCESS_COUNT_PREFIX,
                                     job_control_manager.Statistics().active_process_count);
            for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
                 process_index < process_runtime_limits.process_capacity; ++process_index) {
                JobControlProcessState job_state{};
                if (job_control_manager.ReadProcess(process_index, job_state) !=
                    JobControlStatus::Succeeded) {
                    continue;
                }
                WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_PROCESS_INDEX_PREFIX,
                                         process_index);
                WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_PROCESS_ID_PREFIX,
                                         job_state.process_id);
                WriteProcessRuntimeValue(
                    OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_PROCESS_GROUP_ID_PREFIX,
                    job_state.process_group_id);
                WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_JOB_ACTIVE_SESSION_ID_PREFIX,
                                         job_state.session_id);
            }
        }
        RestoreInterrupts(interrupts_were_enabled);
        return ProcessRuntimeStatus::ResourceLeakDetected;
    }
    RestoreInterrupts(interrupts_were_enabled);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatistics GetProcessRuntimeStatistics() noexcept {
    ProcessRuntimeStatistics statistics{
        .scheduler = thread_scheduler.Statistics(),
        .kernel_threads = kernel_thread_runtime_statistics,
        .file_page_loads = file_page_load_coordinator.Statistics(),
        .file_page_writebacks = file_page_writeback_coordinator.Statistics(),
        .file_readahead_requests = file_readahead_request_queue.Statistics(),
        .file_readahead_feedback = file_readahead_feedback_ledger.Statistics(),
        .file_readahead_worker_failed = file_readahead_worker_failed,
        .work_queue = kernel_work_queue.Statistics(),
        .background_reclaim = background_reclaim_controller.Statistics(),
        .background_reclaim_worker_failed = background_reclaim_worker_failed,
        .page_aging = page_aging_manager.Statistics(),
        .page_aging_worker_failed = page_aging_worker_failed,
        .extended_state = GetExtendedStateConfiguration(),
        .configured_process_capacity = process_runtime_limits.process_capacity,
        .configured_thread_capacity = process_runtime_limits.thread_capacity,
        .configured_threads_per_process = process_runtime_limits.maximum_threads_per_process,
        .capacity_self_test_process_count = capacity_self_test_process_count,
        .capacity_self_test_thread_count = capacity_self_test_thread_count,
        .capacity_self_test_threads_per_process = capacity_self_test_threads_per_process,
        .frames_before_processes = frames_before_processes,
        .frames_after_processes = frames_after_processes,
        .virtual_addresses_before_processes = virtual_addresses_before_processes,
        .virtual_addresses_after_processes = virtual_addresses_after_processes,
        .kernel_stacks_before_processes = kernel_stacks_before_processes,
        .kernel_stacks_after_processes = kernel_stacks_after_processes,
        .virtual_memory_areas_before_processes = virtual_memory_areas_before_processes,
        .virtual_memory_areas_after_processes = virtual_memory_areas_after_processes,
        .user_page_references_before_processes = user_page_references_before_processes,
        .user_page_references_after_processes = user_page_references_after_processes,
        .resource_snapshot_before_processes = resource_snapshot_before_processes,
        .resource_snapshot_after_processes = resource_snapshot_after_processes,
        .resource_snapshot_difference = resource_snapshot_difference,
        .memory_pressure = GetUserMemoryPressureStatistics(),
        .memory_reclaim = GetUserMemoryReclaimStatistics(),
        .memory_overcommit = GetUserMemoryOvercommitStatistics(),
        .swap = GetUserSwapStatistics(),
        .oom_invocation_count = oom_invocation_count,
        .oom_kill_count = oom_kill_count,
        .last_oom_victim_process_id = last_oom_victim_process_id,
        .last_oom_victim_score = last_oom_victim_score,
        .file_synchronization_count = file_synchronization_count,
        .file_data_synchronization_count = file_data_synchronization_count,
        .memory_synchronous_synchronization_count = memory_synchronous_synchronization_count,
        .memory_asynchronous_synchronization_count = memory_asynchronous_synchronization_count,
        .ipc =
            ProcessIpcStatistics{
                .pipe = process_pipe.Statistics(),
                .dynamic_pipes = dynamic_pipe_manager.Statistics(),
                .reader_block_count = pipe_reader_block_count,
                .writer_block_count = pipe_writer_block_count,
                .end_of_file_observation_count = pipe_end_of_file_observation_count,
                .broken_pipe_observation_count = pipe_broken_observation_count,
            },
        .user_threads = user_thread_runtime_statistics,
        .private_futexes = private_futex_manager.Statistics(),
        .process_tree = process_tree.Statistics(),
        .job_control = job_control_manager.Statistics(),
        .signals = signal_manager.Statistics(),
        .terminal = process_terminal.Statistics(),
        .object_manager = kernel_object_manager.Statistics(),
        .file_descriptions = file_description_manager.Statistics(),
        .file_tables = {},
        .processes = {},
    };
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < OS_KERNEL_PROCESS_RUNTIME_RESULT_CAPACITY; ++process_index) {
        statistics.processes[process_index] = runtime_processes[process_index].result;
        statistics.file_tables[process_index] =
            runtime_processes[process_index].file_table.Statistics();
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
    if (thread_scheduler.CurrentThreadId(thread_id) != ThreadSchedulerStatus::Succeeded) {
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

ProcessCredentialStatus
GetCurrentProcessCredentials(os::abi::CredentialInformation &information) noexcept {
    information = os::abi::CredentialInformation{};
    if (!IsProcessSchedulingActive()) {
        return ProcessCredentialStatus::NotInitialized;
    }
    const fs::FsContext &context = CurrentRuntimeProcess().file_system_context;
    if (!context.initialized || context.credentials.supplementary_group_count >
                                    security::OS_KERNEL_CREDENTIAL_SUPPLEMENTARY_GROUP_CAPACITY) {
        return ProcessCredentialStatus::InvalidArgument;
    }
    information = os::abi::CredentialInformation{
        .real_user_identifier = context.credentials.real_user_identifier,
        .effective_user_identifier = context.credentials.effective_user_identifier,
        .saved_user_identifier = context.credentials.saved_user_identifier,
        .real_group_identifier = context.credentials.real_group_identifier,
        .effective_group_identifier = context.credentials.effective_group_identifier,
        .saved_group_identifier = context.credentials.saved_group_identifier,
        .supplementary_group_count =
            static_cast<uint32_t>(context.credentials.supplementary_group_count),
        .creation_mask = context.creation_mask,
    };
    return ProcessCredentialStatus::Succeeded;
}

ProcessCredentialStatus
SetCurrentProcessUserIdentifiers(const os::abi::IdentifierChangeRequest &request) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessCredentialStatus::NotInitialized;
    }
    if (request.reserved != 0U) {
        return ProcessCredentialStatus::InvalidArgument;
    }
    security::Credentials &credentials = CurrentRuntimeProcess().file_system_context.credentials;
    if (!security::IsSuperuser(credentials) &&
        (!IdentifierRequestAllowed(request.real_identifier, credentials.real_user_identifier,
                                   credentials.effective_user_identifier,
                                   credentials.saved_user_identifier) ||
         !IdentifierRequestAllowed(request.effective_identifier, credentials.real_user_identifier,
                                   credentials.effective_user_identifier,
                                   credentials.saved_user_identifier) ||
         !IdentifierRequestAllowed(request.saved_identifier, credentials.real_user_identifier,
                                   credentials.effective_user_identifier,
                                   credentials.saved_user_identifier))) {
        return ProcessCredentialStatus::PermissionDenied;
    }
    if (request.real_identifier != os::abi::OS_ABI_IDENTIFIER_UNCHANGED) {
        credentials.real_user_identifier = request.real_identifier;
    }
    if (request.effective_identifier != os::abi::OS_ABI_IDENTIFIER_UNCHANGED) {
        credentials.effective_user_identifier = request.effective_identifier;
    }
    if (request.saved_identifier != os::abi::OS_ABI_IDENTIFIER_UNCHANGED) {
        credentials.saved_user_identifier = request.saved_identifier;
    }
    return ProcessCredentialStatus::Succeeded;
}

ProcessCredentialStatus
SetCurrentProcessGroupIdentifiers(const os::abi::IdentifierChangeRequest &request) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessCredentialStatus::NotInitialized;
    }
    if (request.reserved != 0U) {
        return ProcessCredentialStatus::InvalidArgument;
    }
    security::Credentials &credentials = CurrentRuntimeProcess().file_system_context.credentials;
    if (!security::IsSuperuser(credentials) &&
        (!IdentifierRequestAllowed(request.real_identifier, credentials.real_group_identifier,
                                   credentials.effective_group_identifier,
                                   credentials.saved_group_identifier) ||
         !IdentifierRequestAllowed(request.effective_identifier, credentials.real_group_identifier,
                                   credentials.effective_group_identifier,
                                   credentials.saved_group_identifier) ||
         !IdentifierRequestAllowed(request.saved_identifier, credentials.real_group_identifier,
                                   credentials.effective_group_identifier,
                                   credentials.saved_group_identifier))) {
        return ProcessCredentialStatus::PermissionDenied;
    }
    if (request.real_identifier != os::abi::OS_ABI_GROUP_IDENTIFIER_UNCHANGED) {
        credentials.real_group_identifier = request.real_identifier;
    }
    if (request.effective_identifier != os::abi::OS_ABI_GROUP_IDENTIFIER_UNCHANGED) {
        credentials.effective_group_identifier = request.effective_identifier;
    }
    if (request.saved_identifier != os::abi::OS_ABI_GROUP_IDENTIFIER_UNCHANGED) {
        credentials.saved_group_identifier = request.saved_identifier;
    }
    return ProcessCredentialStatus::Succeeded;
}

ProcessCredentialStatus GetCurrentProcessSupplementaryGroups(os::abi::GroupIdentifier *const groups,
                                                             const uint64_t capacity,
                                                             uint64_t &group_count) noexcept {
    group_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive()) {
        return ProcessCredentialStatus::NotInitialized;
    }
    const security::Credentials &credentials =
        CurrentRuntimeProcess().file_system_context.credentials;
    if (credentials.supplementary_group_count >
        security::OS_KERNEL_CREDENTIAL_SUPPLEMENTARY_GROUP_CAPACITY) {
        return ProcessCredentialStatus::InvalidArgument;
    }
    group_count = credentials.supplementary_group_count;
    if (capacity < group_count) {
        return ProcessCredentialStatus::CapacityExhausted;
    }
    if (group_count != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE && groups == nullptr) {
        return ProcessCredentialStatus::InvalidArgument;
    }
    for (uint64_t group_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX; group_index < group_count;
         ++group_index) {
        groups[group_index] = credentials.supplementary_groups[group_index];
    }
    return ProcessCredentialStatus::Succeeded;
}

ProcessCredentialStatus
SetCurrentProcessSupplementaryGroups(const os::abi::GroupIdentifier *const groups,
                                     const uint64_t group_count) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessCredentialStatus::NotInitialized;
    }
    security::Credentials &credentials = CurrentRuntimeProcess().file_system_context.credentials;
    if (!security::IsSuperuser(credentials)) {
        return ProcessCredentialStatus::PermissionDenied;
    }
    const security::CredentialStatus status =
        security::SetSupplementaryGroups(credentials, groups, group_count);
    if (status == security::CredentialStatus::Succeeded) {
        return ProcessCredentialStatus::Succeeded;
    }
    return status == security::CredentialStatus::CapacityExhausted
               ? ProcessCredentialStatus::CapacityExhausted
               : ProcessCredentialStatus::InvalidArgument;
}

ProcessCredentialStatus
SetCurrentProcessCreationMask(const os::abi::FileMode creation_mask,
                              os::abi::FileMode &previous_creation_mask) noexcept {
    previous_creation_mask = 0U;
    if (!IsProcessSchedulingActive()) {
        return ProcessCredentialStatus::NotInitialized;
    }
    fs::FsContext &context = CurrentRuntimeProcess().file_system_context;
    previous_creation_mask = context.creation_mask;
    context.creation_mask = creation_mask & os::abi::OS_ABI_FILE_MODE_PERMISSION_MASK;
    return ProcessCredentialStatus::Succeeded;
}

ProcessResourceLimitStatus GetCurrentProcessResourceLimit(const os::abi::ResourceLimitKind kind,
                                                          os::abi::ResourceLimit &limit) noexcept {
    limit = os::abi::ResourceLimit{};
    if (!IsProcessSchedulingActive()) {
        return ProcessResourceLimitStatus::NotInitialized;
    }
    if (!ResourceLimitKindIsValid(kind)) {
        return ProcessResourceLimitStatus::InvalidArgument;
    }
    limit = CurrentRuntimeProcess().resource_limits[static_cast<uint64_t>(kind)];
    return ProcessResourceLimitStatus::Succeeded;
}

ProcessResourceLimitStatus
SetCurrentProcessResourceLimit(const os::abi::ResourceLimitKind kind,
                               const os::abi::ResourceLimit &limit) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessResourceLimitStatus::NotInitialized;
    }
    if (!ResourceLimitKindIsValid(kind) || limit.current > limit.maximum ||
        limit.maximum > ResourceLimitSystemMaximum(kind)) {
        return ProcessResourceLimitStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    const os::abi::ResourceLimit previous = process.resource_limits[static_cast<uint64_t>(kind)];
    if (!security::IsSuperuser(process.file_system_context.credentials) &&
        limit.maximum > previous.maximum) {
        return ProcessResourceLimitStatus::PermissionDenied;
    }
    if (kind == os::abi::ResourceLimitKind::OpenFileCount &&
        process.file_table.SetSoftLimit(limit.current) != FileTableStatus::Succeeded) {
        return ProcessResourceLimitStatus::InvalidArgument;
    }
    process.resource_limits[static_cast<uint64_t>(kind)] = limit;
    return ProcessResourceLimitStatus::Succeeded;
}

UserVirtualMemoryStatus MapCurrentProcessAnonymousMemory(const uint64_t requested_address,
                                                         const uint64_t length_bytes,
                                                         const uint64_t protection_flags,
                                                         const uint64_t map_flags,
                                                         uint64_t &mapped_address) noexcept {
    if (!IsProcessSchedulingActive()) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    const os::abi::VirtualMemoryStatistics statistics =
        GetUserVirtualMemoryStatistics(process.address_space);
    const uint64_t address_space_limit =
        process.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::AddressSpace)]
            .current;
    const uint64_t current_virtual_bytes =
        statistics.virtual_page_count > UINT64_MAX / os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES
            ? UINT64_MAX
            : statistics.virtual_page_count * os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
    if (current_virtual_bytes > address_space_limit ||
        length_bytes > address_space_limit - current_virtual_bytes) {
        return UserVirtualMemoryStatus::ResourceLimitExceeded;
    }
    uint64_t commit_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!CalculatePageCount(length_bytes, commit_page_count)) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    if (CommitUserMemory(process.address_space, commit_page_count,
                         security::IsSuperuser(process.file_system_context.credentials)) !=
        MemoryOvercommitStatus::Succeeded) {
        return UserVirtualMemoryStatus::CommitLimitExceeded;
    }
    const UserVirtualMemoryStatus status =
        MapAnonymousMemory(process.address_space, requested_address, length_bytes, protection_flags,
                           map_flags, mapped_address);
    if (status != UserVirtualMemoryStatus::Succeeded &&
        UncommitUserMemory(process.address_space, commit_page_count) !=
            MemoryOvercommitStatus::Succeeded) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    return status;
}

UserVirtualMemoryStatus MapCurrentProcessFileMemory(const os::abi::FileMemoryMapRequest &request,
                                                    uint64_t &mapped_address) noexcept {
    mapped_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    const os::abi::VirtualMemoryStatistics statistics =
        GetUserVirtualMemoryStatistics(process.address_space);
    const uint64_t address_space_limit =
        process.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::AddressSpace)]
            .current;
    const uint64_t current_virtual_bytes =
        statistics.virtual_page_count > UINT64_MAX / os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES
            ? UINT64_MAX
            : statistics.virtual_page_count * os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
    if (current_virtual_bytes > address_space_limit ||
        request.length_bytes > address_space_limit - current_virtual_bytes) {
        return UserVirtualMemoryStatus::ResourceLimitExceeded;
    }
    KernelObjectReference reference{};
    if (process.file_table.Lookup(request.file_descriptor, reference) !=
        FileTableStatus::Succeeded) {
        return UserVirtualMemoryStatus::InvalidFile;
    }
    RetainedRegularFile retained_file{};
    if (file_description_manager.RetainRegularFile(reference, retained_file) !=
        FileDescriptionStatus::Succeeded) {
        return UserVirtualMemoryStatus::InvalidFile;
    }
    const UserVirtualMemoryStatus status =
        MapFileMemory(process.address_space, *retained_file.vfs, retained_file.open_file,
                      request.requested_address, request.length_bytes, request.protection_flags,
                      request.map_flags, request.file_offset_bytes, mapped_address);
    if (retained_file.vfs->Close(retained_file.open_file) != fs::Status::Succeeded) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    return status;
}

UserVirtualMemoryStatus UnmapCurrentProcessMemory(const uint64_t address,
                                                  const uint64_t length_bytes) noexcept {
    if (!IsProcessSchedulingActive()) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    if (CurrentProcessThreadMemoryOverlaps(address, length_bytes)) {
        return UserVirtualMemoryStatus::ThreadMemoryInUse;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    const os::abi::VirtualMemoryStatistics statistics_before =
        GetUserVirtualMemoryStatistics(process.address_space);
    VirtualMemoryArea area{};
    if (process.address_space.virtual_memory_map.FindContaining(address, area) !=
        VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    const UserVirtualMemoryStatus status =
        area.kind == VirtualMemoryAreaKind::Anonymous
            ? UnmapAnonymousMemory(process.address_space, address, length_bytes)
        : area.kind == VirtualMemoryAreaKind::FilePrivate ||
                area.kind == VirtualMemoryAreaKind::FileShared
            ? (!FlushOutstandingUserFilePages()
                   ? UserVirtualMemoryStatus::FileWriteFailed
                   : UnmapFileMemory(process.address_space, address, length_bytes))
            : UserVirtualMemoryStatus::InvalidRange;
    if (status == UserVirtualMemoryStatus::Succeeded) {
        if (area.kind == VirtualMemoryAreaKind::Anonymous) {
            const os::abi::VirtualMemoryStatistics statistics_after =
                GetUserVirtualMemoryStatistics(process.address_space);
            if (statistics_after.anonymous_page_count > statistics_before.anonymous_page_count ||
                UncommitUserMemory(process.address_space,
                                   statistics_before.anonymous_page_count -
                                       statistics_after.anonymous_page_count) !=
                    MemoryOvercommitStatus::Succeeded) {
                return UserVirtualMemoryStatus::Corrupt;
            }
        }
        uint64_t cancelled_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (!CancelPrivateFutexRange(process.address_space.address_space_identifier, address,
                                     length_bytes, false, cancelled_thread_count)) {
            return UserVirtualMemoryStatus::Corrupt;
        }
    }
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    return status;
}

UserVirtualMemoryStatus SetCurrentProcessProgramBreak(const uint64_t requested_address,
                                                      uint64_t &program_break_address) noexcept {
    if (!IsProcessSchedulingActive()) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    if (requested_address > process.address_space.program_break_base_address &&
        requested_address - process.address_space.program_break_base_address >
            process.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::Data)]
                .current) {
        return UserVirtualMemoryStatus::ResourceLimitExceeded;
    }
    const uint64_t previous_program_break_address = process.address_space.program_break_address;
    const os::abi::VirtualMemoryStatistics statistics_before =
        GetUserVirtualMemoryStatistics(process.address_space);
    const UserVirtualMemoryStatus status =
        SetProgramBreak(process.address_space, requested_address, program_break_address);
    if (status == UserVirtualMemoryStatus::Succeeded &&
        requested_address != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        const os::abi::VirtualMemoryStatistics statistics_after =
            GetUserVirtualMemoryStatistics(process.address_space);
        if (statistics_after.program_break_page_count >
            statistics_before.program_break_page_count) {
            const uint64_t additional_page_count = statistics_after.program_break_page_count -
                                                   statistics_before.program_break_page_count;
            if (CommitUserMemory(process.address_space, additional_page_count,
                                 security::IsSuperuser(process.file_system_context.credentials)) !=
                MemoryOvercommitStatus::Succeeded) {
                uint64_t rollback_program_break_address = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
                if (SetProgramBreak(process.address_space, previous_program_break_address,
                                    rollback_program_break_address) !=
                        UserVirtualMemoryStatus::Succeeded ||
                    rollback_program_break_address != previous_program_break_address) {
                    return UserVirtualMemoryStatus::Corrupt;
                }
                program_break_address = previous_program_break_address;
                return UserVirtualMemoryStatus::CommitLimitExceeded;
            }
        } else if (statistics_after.program_break_page_count <
                   statistics_before.program_break_page_count) {
            if (UncommitUserMemory(process.address_space,
                                   statistics_before.program_break_page_count -
                                       statistics_after.program_break_page_count) !=
                MemoryOvercommitStatus::Succeeded) {
                return UserVirtualMemoryStatus::Corrupt;
            }
        }
    }
    process.result.mapped_page_count = process.address_space.mapped_page_count;
    return status;
}

ProcessObservationSnapshot GetProcessObservationSnapshot() noexcept {
    const ThreadSchedulerStatistics scheduler_statistics = thread_scheduler.Statistics();
    return ProcessObservationSnapshot{
        .active_process_count = scheduler_statistics.owned_process_count,
        .active_thread_count = scheduler_statistics.owned_thread_count,
        .process_capacity = scheduler_statistics.process_capacity,
        .thread_capacity = scheduler_statistics.thread_capacity,
        .active_file_description_count =
            kernel_object_manager.Statistics().active_file_description_count,
        .active_pipe_count = dynamic_pipe_manager.Statistics().active_pipe_count,
        .oom_kill_count = oom_kill_count,
    };
}

os::abi::VirtualMemoryStatistics GetCurrentProcessVirtualMemoryStatistics() noexcept {
    if (!IsProcessSchedulingActive()) {
        return os::abi::VirtualMemoryStatistics{};
    }
    return GetUserVirtualMemoryStatistics(CurrentRuntimeProcess().address_space);
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
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToPipeStatus(
        TryReadCurrentProcessDescriptor(OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR, destination,
                                        capacity_bytes, read_bytes, file_system_status));
}

PipeStatus TryWriteCurrentProcessPipe(const uint8_t *source, const uint64_t length_bytes,
                                      uint64_t &written_bytes) noexcept {
    if (!CurrentProcessCanWritePipe()) {
        written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        return PipeStatus::InvalidArgument;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToPipeStatus(
        TryWriteCurrentProcessDescriptor(OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR, source,
                                         length_bytes, written_bytes, file_system_status));
}

PipeStatus CloseCurrentProcessPipeReader() noexcept {
    if (!CurrentProcessCanReadPipe()) {
        return PipeStatus::InvalidArgument;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToPipeStatus(CloseCurrentProcessDescriptor(
        OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR, file_system_status));
}

PipeStatus CloseCurrentProcessPipeWriter() noexcept {
    if (!CurrentProcessCanWritePipe()) {
        return PipeStatus::InvalidArgument;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToPipeStatus(CloseCurrentProcessDescriptor(
        OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR, file_system_status));
}

FileSystemStatus OpenCurrentProcessFile(const uint8_t *path, const uint64_t path_length_bytes,
                                        const fs::OpenOptions &options,
                                        uint64_t &file_descriptor) noexcept {
    file_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (options.truncate && !options.writable) {
        return FileSystemStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    fs::OpenFile open_file{};
    fs::OpenOptions open_options = options;
    open_options.truncate = false;
    fs::Status status = process_vfs->Open(process.file_system_context, path, path_length_bytes,
                                          open_options, open_file);
    if (status != fs::Status::Succeeded) {
        return fs::ToFileSystemStatus(status);
    }
    if (options.append && open_file.path.vnode.type != fs::NodeType::RegularFile) {
        if (process_vfs->Close(open_file) != fs::Status::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::InvalidArgument;
    }
    if (options.truncate) {
        if (open_file.path.vnode.type != fs::NodeType::RegularFile) {
            if (process_vfs->Close(open_file) != fs::Status::Succeeded) {
                HaltProcessor();
            }
            return FileSystemStatus::Unsupported;
        }
        fs::NodeInformation information{};
        if (process_vfs->StatOpenFile(open_file, information) != fs::Status::Succeeded ||
            !PrepareRuntimeFileTruncate(FileIdentityFromInformation(information),
                                        OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE)) {
            if (process_vfs->Close(open_file) != fs::Status::Succeeded) {
                HaltProcessor();
            }
            return FileSystemStatus::Corrupt;
        }
        status = process_vfs->TruncateOpenFile(open_file, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
        if (status != fs::Status::Succeeded) {
            if (process_vfs->Close(open_file) != fs::Status::Succeeded) {
                HaltProcessor();
            }
            return fs::ToFileSystemStatus(status);
        }
    }
    const uint64_t file_status_flags =
        (options.readable ? OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG
                          : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) |
        (options.writable ? OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG
                          : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) |
        (options.append ? OS_KERNEL_FILE_DESCRIPTION_APPEND_STATUS_FLAG
                        : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
    const bool terminal_device = open_file.path.vnode.type == fs::NodeType::CharacterDevice;
    const FileDescriptionCreateRequest request{
        .kind = terminal_device ? FileDescriptionKind::TerminalDevice
                                : FileDescriptionKind::RegularFile,
        .file_status_flags = file_status_flags,
        .terminal = terminal_device ? &process_terminal : nullptr,
        .device_write_operation =
            terminal_device && options.writable ? WriteConsoleDevice : nullptr,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .pipe_manager = nullptr,
        .vfs = process_vfs,
        .open_file = open_file,
        .writeback_identity =
            terminal_device ? FileIdentity{} : FileIdentityFromVnode(open_file.path.vnode),
        .writeback_error_register_operation =
            terminal_device ? nullptr : RegisterUserFileWritebackDescription,
        .writeback_error_unregister_operation =
            terminal_device ? nullptr : UnregisterUserFileWritebackDescription,
    };
    KernelObjectReference reference{};
    if (file_description_manager.Create(request, reference) != FileDescriptionStatus::Succeeded) {
        if (process_vfs->Close(open_file) != fs::Status::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    const FileTableStatus install_status =
        process.file_table.Install(reference, OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR,
                                   OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, file_descriptor);
    return install_status == FileTableStatus::Succeeded ? FileSystemStatus::Succeeded
                                                        : FileSystemStatus::DataCapacityExhausted;
}

FileSystemStatus ReadCurrentProcessFile(const uint64_t file_descriptor, uint8_t *destination,
                                        const uint64_t capacity_bytes,
                                        uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    KernelObjectReference reference{};
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    if (process.file_table.Lookup(file_descriptor, reference) != FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
            FileDescriptionStatus::Succeeded ||
        (snapshot.kind != FileDescriptionKind::RegularFile &&
         snapshot.kind != FileDescriptionKind::TerminalDevice)) {
        return FileSystemStatus::InvalidHandle;
    }
    if (snapshot.kind == FileDescriptionKind::TerminalDevice) {
        ThreadEntry current_thread{};
        ProcessEntry current_process{};
        JobControlProcessState job_state{};
        if (!ReadCurrentThreadAndProcess(current_thread, current_process) ||
            job_control_manager.ReadProcess(current_thread.process_index, job_state) !=
                JobControlStatus::Succeeded) {
            return FileSystemStatus::Corrupt;
        }
        if (!process_terminal.CanRead(job_state.session_id, job_state.process_group_id)) {
            return FileSystemStatus::BackgroundTerminalRead;
        }
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    PipeStatus pipe_status = PipeStatus::Succeeded;
    const FileDescriptionStatus status = file_description_manager.TryRead(
        reference, destination, capacity_bytes, read_bytes, file_system_status, pipe_status);
    if (status == FileDescriptionStatus::Succeeded) {
        if (snapshot.kind == FileDescriptionKind::TerminalDevice) {
            process.result.console_bytes_read += read_bytes;
        } else {
            process.result.file_system_bytes_read += read_bytes;
        }
    }
    return MapProcessIoToFileSystemStatus(MapFileDescriptionStatus(status), file_system_status);
}

FileSystemStatus WriteCurrentProcessFile(const uint64_t file_descriptor, const uint8_t *source,
                                         const uint64_t length_bytes,
                                         uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    KernelObjectReference reference{};
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    if (process.file_table.Lookup(file_descriptor, reference) != FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
            FileDescriptionStatus::Succeeded ||
        (snapshot.kind != FileDescriptionKind::RegularFile &&
         snapshot.kind != FileDescriptionKind::TerminalDevice)) {
        return FileSystemStatus::InvalidHandle;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    PipeStatus pipe_status = PipeStatus::Succeeded;
    if (snapshot.kind == FileDescriptionKind::RegularFile &&
        length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        const FileSystemStatus balance_status = BalanceRuntimeFileWritebackPressure();
        if (balance_status != FileSystemStatus::Succeeded) {
            return balance_status;
        }
    }
    const FileDescriptionStatus status = file_description_manager.TryWrite(
        reference, source, length_bytes, written_bytes, file_system_status, pipe_status);
    if (status == FileDescriptionStatus::Succeeded) {
        if (snapshot.kind == FileDescriptionKind::TerminalDevice) {
            process.result.console_bytes_written += written_bytes;
        } else {
            process.result.file_system_bytes_written += written_bytes;
        }
    }
    return MapProcessIoToFileSystemStatus(MapFileDescriptionStatus(status), file_system_status);
}

FileSystemStatus CloseCurrentProcessFile(const uint64_t file_descriptor) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    if (process.file_table.Lookup(file_descriptor, reference) != FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
            FileDescriptionStatus::Succeeded ||
        (snapshot.kind != FileDescriptionKind::RegularFile &&
         snapshot.kind != FileDescriptionKind::TerminalDevice) ||
        reference.Reset() != KernelObjectStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    return MapProcessIoToFileSystemStatus(
        CloseCurrentProcessDescriptor(file_descriptor, file_system_status), file_system_status);
}

FileSystemStatus CreateCurrentProcessDirectory(const uint8_t *path,
                                               const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->CreateDirectory(process.file_system_context, path, path_length_bytes));
}

FileSystemStatus RemoveCurrentProcessFile(const uint8_t *path,
                                          const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->RemoveFile(process.file_system_context, path, path_length_bytes));
}

FileSystemStatus RemoveCurrentProcessDirectory(const uint8_t *path,
                                               const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->RemoveDirectory(process.file_system_context, path, path_length_bytes));
}

FileSystemStatus RenameCurrentProcessPath(const uint8_t *source_path,
                                          const uint64_t source_path_length_bytes,
                                          const uint8_t *destination_path,
                                          const uint64_t destination_path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(process_vfs->Rename(process.file_system_context, source_path,
                                                      source_path_length_bytes, destination_path,
                                                      destination_path_length_bytes, true));
}

FileSystemStatus TruncateCurrentProcessFile(const uint8_t *path, const uint64_t path_length_bytes,
                                            const uint64_t size_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    fs::NodeInformation information{};
    const fs::Status stat_status =
        process_vfs->Stat(process.file_system_context, path, path_length_bytes, information);
    if (stat_status != fs::Status::Succeeded) {
        return fs::ToFileSystemStatus(stat_status);
    }
    const uint64_t file_size_limit =
        process.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::FileSize)]
            .current;
    if (size_bytes > file_size_limit) {
        return FileSystemStatus::FileTooLarge;
    }
    if (!PrepareRuntimeFileTruncate(FileIdentityFromInformation(information), size_bytes)) {
        return FileSystemStatus::Corrupt;
    }
    const fs::Status truncate_status =
        process_vfs->Truncate(process.file_system_context, path, path_length_bytes, size_bytes);
    return fs::ToFileSystemStatus(truncate_status);
}

FileSystemStatus StatCurrentProcessPath(const uint8_t *path, const uint64_t path_length_bytes,
                                        fs::NodeInformation &information) noexcept {
    information = fs::NodeInformation{};
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->Stat(process.file_system_context, path, path_length_bytes, information));
}

FileSystemStatus ChangeCurrentProcessPathMode(const uint8_t *const path,
                                              const uint64_t path_length_bytes,
                                              const os::abi::FileMode mode) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->ChangeMode(process.file_system_context, path, path_length_bytes, mode));
}

FileSystemStatus
ChangeCurrentProcessPathOwner(const uint8_t *const path, const uint64_t path_length_bytes,
                              const os::abi::UserIdentifier user_identifier,
                              const os::abi::GroupIdentifier group_identifier) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(process_vfs->ChangeOwner(
        process.file_system_context, path, path_length_bytes, user_identifier, group_identifier));
}

FileSystemStatus LinkCurrentProcessPath(const uint8_t *const source_path,
                                        const uint64_t source_path_length_bytes,
                                        const uint8_t *const destination_path,
                                        const uint64_t destination_path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(process_vfs->Link(process.file_system_context, source_path,
                                                    source_path_length_bytes, destination_path,
                                                    destination_path_length_bytes));
}

FileSystemStatus
CreateCurrentProcessSymbolicLink(const uint8_t *const target, const uint64_t target_length_bytes,
                                 const uint8_t *const destination_path,
                                 const uint64_t destination_path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->CreateSymbolicLink(process.file_system_context, target, target_length_bytes,
                                        destination_path, destination_path_length_bytes));
}

FileSystemStatus ReadCurrentProcessSymbolicLink(const uint8_t *const path,
                                                const uint64_t path_length_bytes,
                                                uint8_t *const destination,
                                                const uint64_t capacity_bytes,
                                                uint64_t &target_length_bytes) noexcept {
    target_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->ReadSymbolicLink(process.file_system_context, path, path_length_bytes,
                                      destination, capacity_bytes, target_length_bytes));
}

FileSystemStatus SyncCurrentProcessFileSystem() noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    if (!ProtectRuntimeSharedFileMappings()) {
        return FileSystemStatus::Corrupt;
    }
    uint64_t written_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const UserVirtualMemoryStatus writeback_status =
        WritebackUserFilePageCache(UINT64_MAX, written_page_count);
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FILE_CACHE_WRITEBACK_STATUS_PREFIX,
                             static_cast<uint64_t>(writeback_status));
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FILE_CACHE_WRITTEN_PAGE_COUNT_PREFIX,
                             written_page_count);
    if (writeback_status != UserVirtualMemoryStatus::Succeeded) {
        return FileSystemStatus::DeviceFailure;
    }
    const fs::Status sync_status = process_vfs->Sync();
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_FILE_SYSTEM_SYNC_STATUS_PREFIX,
                             static_cast<uint64_t>(sync_status));
    return fs::ToFileSystemStatus(sync_status);
}

FileSystemStatus SynchronizeCurrentProcessFile(const uint64_t file_descriptor,
                                               const bool data_only) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    uint64_t &synchronization_count =
        data_only ? file_data_synchronization_count : file_synchronization_count;
    if (synchronization_count == UINT64_MAX) {
        return FileSystemStatus::Corrupt;
    }
    ++synchronization_count;
    KernelObjectReference reference{};
    if (CurrentRuntimeProcess().file_table.Lookup(file_descriptor, reference) !=
        FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileIdentity identity{};
    uint64_t sampled_sequence = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (file_description_manager.ReadSynchronizationState(reference, identity, sampled_sequence) !=
        FileDescriptionStatus::Succeeded) {
        return FileSystemStatus::Unsupported;
    }
    if (!ProtectRuntimeSharedFileMappings()) {
        return FileSystemStatus::Corrupt;
    }
    uint64_t written_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const UserVirtualMemoryStatus writeback_status =
        WritebackUserFilePageCacheRange(identity, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, UINT64_MAX,
                                        UINT64_MAX, written_page_count);
    uint64_t current_sequence = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    FileWritebackError writeback_error = FileWritebackError::None;
    if (CheckUserFileWritebackError(identity, sampled_sequence, current_sequence,
                                    writeback_error) != UserVirtualMemoryStatus::Succeeded ||
        file_description_manager.AdvanceWritebackErrorCursor(reference, current_sequence) !=
            FileDescriptionStatus::Succeeded) {
        return FileSystemStatus::Corrupt;
    }
    if (writeback_error != FileWritebackError::None ||
        writeback_status == UserVirtualMemoryStatus::FileWriteFailed) {
        return FileSystemStatus::DeviceFailure;
    }
    if (writeback_status != UserVirtualMemoryStatus::Succeeded) {
        return FileSystemStatus::Corrupt;
    }
    return fs::ToFileSystemStatus(process_vfs->Sync());
}

FileSystemStatus SynchronizeCurrentProcessMemory(const uint64_t address,
                                                 const uint64_t length_bytes,
                                                 const uint64_t flags) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    const bool asynchronous =
        (flags & os::abi::OS_ABI_MEMORY_SYNC_ASYNCHRONOUS) != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const bool synchronous =
        (flags & os::abi::OS_ABI_MEMORY_SYNC_SYNCHRONOUS) != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if ((flags & ~os::abi::OS_ABI_MEMORY_SYNC_VALID_FLAG_MASK) !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        asynchronous == synchronous) {
        return FileSystemStatus::InvalidArgument;
    }
    uint64_t &synchronization_count = synchronous ? memory_synchronous_synchronization_count
                                                  : memory_asynchronous_synchronization_count;
    if (synchronization_count == UINT64_MAX) {
        return FileSystemStatus::Corrupt;
    }
    ++synchronization_count;
    if (synchronous && !ProtectRuntimeSharedFileMappings()) {
        return FileSystemStatus::Corrupt;
    }
    uint64_t written_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const UserVirtualMemoryStatus status =
        SynchronizeUserFileMappings(CurrentRuntimeProcess().address_space, address, length_bytes,
                                    synchronous, written_page_count);
    if (status == UserVirtualMemoryStatus::FileWriteFailed) {
        return FileSystemStatus::DeviceFailure;
    }
    if (status == UserVirtualMemoryStatus::InvalidRange) {
        return FileSystemStatus::InvalidArgument;
    }
    if (status != UserVirtualMemoryStatus::Succeeded) {
        return FileSystemStatus::Corrupt;
    }
    return synchronous ? fs::ToFileSystemStatus(process_vfs->Sync()) : FileSystemStatus::Succeeded;
}

FileSystemStatus ScheduleRuntimeFileWritebackWorker() noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr || !file_writeback_work_registered ||
        kernel_work_thread_stop_requested) {
        return FileSystemStatus::NotInitialized;
    }
    const FilePageCacheStatistics statistics = GetUserFilePageCacheStatistics();
    const bool immediate = UserFileWritebackWorkerRequested();
    if (!immediate && statistics.dirty_page_count == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return FileSystemStatus::Succeeded;
    }
    WorkQueueEntry entry{};
    if (kernel_work_queue.Read(file_writeback_work_handle, entry) != WorkQueueStatus::Succeeded) {
        return FileSystemStatus::Corrupt;
    }
    if (entry.state == WorkState::Completed || entry.state == WorkState::Cancelled) {
        if (kernel_work_queue.Reset(file_writeback_work_handle) != WorkQueueStatus::Succeeded) {
            return FileSystemStatus::Corrupt;
        }
    }
    WorkQueueStatus queue_status = WorkQueueStatus::Succeeded;
    if (immediate) {
        queue_status = kernel_work_queue.Queue(file_writeback_work_handle);
    } else {
        const uint64_t now_nanoseconds = GetMonotonicNanoseconds();
        if (now_nanoseconds >
            UINT64_MAX - OS_KERNEL_PROCESS_RUNTIME_FILE_WRITEBACK_AGE_NANOSECONDS) {
            return FileSystemStatus::Corrupt;
        }
        queue_status = kernel_work_queue.QueueDelayed(
            file_writeback_work_handle,
            now_nanoseconds + OS_KERNEL_PROCESS_RUNTIME_FILE_WRITEBACK_AGE_NANOSECONDS);
    }
    if (queue_status == WorkQueueStatus::AlreadyPending) {
        return FileSystemStatus::Succeeded;
    }
    if (queue_status != WorkQueueStatus::Succeeded) {
        return FileSystemStatus::Corrupt;
    }
    bool wake_won = false;
    if (WakeOneKernelThread(kernel_work_wait_queue, WakeReason::ConditionSatisfied, wake_won) !=
        KernelThreadRuntimeStatus::Succeeded) {
        return FileSystemStatus::Corrupt;
    }
    // 新工作要么需要立刻执行，要么需要让 Worker 先挂接 deadline。
    GetCpuLocal().RequestReschedule();
    return FileSystemStatus::Succeeded;
}

FileSystemStatus ChangeCurrentProcessDirectory(const uint8_t *path,
                                               const uint64_t path_length_bytes) noexcept {
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(
        process_vfs->ChangeDirectory(process.file_system_context, path, path_length_bytes));
}

FileSystemStatus GetCurrentProcessWorkingDirectory(uint8_t *const destination,
                                                   const uint64_t capacity_bytes,
                                                   uint64_t &path_length_bytes) noexcept {
    path_length_bytes = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    return fs::ToFileSystemStatus(process_vfs->GetWorkingDirectory(
        process.file_system_context, destination, capacity_bytes, path_length_bytes));
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
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = process.file_table.Lookup(descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(lookup_status);
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
        FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::ObjectFailure;
    }
    if (snapshot.kind == FileDescriptionKind::TerminalInput ||
        snapshot.kind == FileDescriptionKind::TerminalDevice) {
        ThreadEntry current_thread{};
        ProcessEntry current_process{};
        JobControlProcessState job_state{};
        if (!ReadCurrentThreadAndProcess(current_thread, current_process) ||
            job_control_manager.ReadProcess(current_thread.process_index, job_state) !=
                JobControlStatus::Succeeded) {
            return ProcessIoStatus::ObjectFailure;
        }
        if (!process_terminal.CanRead(job_state.session_id, job_state.process_group_id)) {
            return ProcessIoStatus::BackgroundTerminalRead;
        }
    }
    PipeStatus pipe_status = PipeStatus::Succeeded;
    const FileDescriptionStatus description_status = file_description_manager.TryRead(
        reference, destination, capacity_bytes, read_bytes, file_system_status, pipe_status);
    const ProcessIoStatus status = MapFileDescriptionStatus(description_status);
    if (status == ProcessIoStatus::Succeeded) {
        if (snapshot.kind == FileDescriptionKind::TerminalInput ||
            snapshot.kind == FileDescriptionKind::TerminalDevice) {
            process.result.console_bytes_read += read_bytes;
        } else if (snapshot.kind == FileDescriptionKind::RegularFile) {
            process.result.file_system_bytes_read += read_bytes;
        } else if (snapshot.kind == FileDescriptionKind::PipeReader) {
            process.result.pipe_bytes_read += read_bytes;
            WakeRequiredThreads(WaitCondition::DescriptorWritable, WakeReason::ConditionSatisfied);
        }
    } else if (status == ProcessIoStatus::EndOfFile &&
               snapshot.kind == FileDescriptionKind::PipeReader) {
        ++pipe_end_of_file_observation_count;
    }
    return status;
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
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = process.file_table.Lookup(descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(lookup_status);
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
        FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::ObjectFailure;
    }
    uint64_t effective_length_bytes = length_bytes;
    if (snapshot.kind == FileDescriptionKind::RegularFile && length_bytes != 0ULL) {
        const uint64_t file_size_limit =
            process.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::FileSize)]
                .current;
        const uint64_t write_offset =
            (snapshot.file_status_flags & OS_KERNEL_FILE_DESCRIPTION_APPEND_STATUS_FLAG) != 0ULL
                ? snapshot.size_bytes
                : snapshot.offset_bytes;
        if (write_offset >= file_size_limit) {
            file_system_status = FileSystemStatus::FileTooLarge;
            return ProcessIoStatus::FileSystemFailure;
        }
        const uint64_t available_bytes = file_size_limit - write_offset;
        if (effective_length_bytes > available_bytes) {
            effective_length_bytes = available_bytes;
        }
    }
    if (snapshot.kind == FileDescriptionKind::RegularFile &&
        effective_length_bytes != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        const FileSystemStatus balance_status = BalanceRuntimeFileWritebackPressure();
        if (balance_status != FileSystemStatus::Succeeded) {
            file_system_status = balance_status;
            return ProcessIoStatus::FileSystemFailure;
        }
    }
    PipeStatus pipe_status = PipeStatus::Succeeded;
    const FileDescriptionStatus description_status = file_description_manager.TryWrite(
        reference, source, effective_length_bytes, written_bytes, file_system_status, pipe_status);
    const ProcessIoStatus status = MapFileDescriptionStatus(description_status);
    if (status == ProcessIoStatus::Succeeded) {
        if (snapshot.kind == FileDescriptionKind::TerminalOutput ||
            snapshot.kind == FileDescriptionKind::TerminalError ||
            snapshot.kind == FileDescriptionKind::TerminalDevice) {
            process.result.console_bytes_written += written_bytes;
        } else if (snapshot.kind == FileDescriptionKind::RegularFile) {
            process.result.file_system_bytes_written += written_bytes;
        } else if (snapshot.kind == FileDescriptionKind::PipeWriter) {
            process.result.pipe_bytes_written += written_bytes;
            WakeRequiredThreads(WaitCondition::DescriptorReadable, WakeReason::ConditionSatisfied);
        }
    } else if (status == ProcessIoStatus::BrokenPipe &&
               snapshot.kind == FileDescriptionKind::PipeWriter) {
        ++pipe_broken_observation_count;
    }
    return status;
}

ProcessIoStatus CloseCurrentProcessDescriptor(const uint64_t descriptor,
                                              FileSystemStatus &file_system_status) noexcept {
    file_system_status = FileSystemStatus::Succeeded;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReleaseResult release_result{};
    const FileTableStatus close_status = process.file_table.Close(descriptor, release_result);
    if (close_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(close_status);
    }
    WakeAfterDescriptionRelease(release_result);
    return ProcessIoStatus::Succeeded;
}

ProcessIoStatus DuplicateCurrentProcessDescriptor(const uint64_t source_descriptor,
                                                  const uint64_t minimum_descriptor,
                                                  const uint64_t descriptor_flags,
                                                  uint64_t &destination_descriptor) noexcept {
    destination_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    return MapFileTableStatus(CurrentRuntimeProcess().file_table.Duplicate(
        source_descriptor, minimum_descriptor, descriptor_flags, destination_descriptor));
}

ProcessIoStatus DuplicateCurrentProcessDescriptorTo(const uint64_t source_descriptor,
                                                    const uint64_t destination_descriptor,
                                                    const uint64_t descriptor_flags) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    KernelObjectReleaseResult replaced_release_result{};
    const FileTableStatus status = CurrentRuntimeProcess().file_table.DuplicateTo(
        source_descriptor, destination_descriptor, descriptor_flags, replaced_release_result);
    if (status == FileTableStatus::Succeeded) {
        WakeAfterDescriptionRelease(replaced_release_result);
    }
    return MapFileTableStatus(status);
}

ProcessIoStatus CreateCurrentProcessPipe(uint64_t &reader_descriptor,
                                         uint64_t &writer_descriptor) noexcept {
    reader_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    writer_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    Pipe *pipe = nullptr;
    const PipeManagerStatus create_pipe_status = dynamic_pipe_manager.Create(pipe);
    if (create_pipe_status == PipeManagerStatus::CapacityExhausted) {
        return ProcessIoStatus::PipeLimitExceeded;
    }
    if (create_pipe_status != PipeManagerStatus::Succeeded || pipe == nullptr) {
        return ProcessIoStatus::ObjectFailure;
    }

    const FileDescriptionCreateRequest reader_request{
        .kind = FileDescriptionKind::PipeReader,
        .file_status_flags = OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
        .terminal = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = pipe,
        .pipe_manager = &dynamic_pipe_manager,
        .vfs = nullptr,
        .open_file = {},
    };
    const FileDescriptionCreateRequest writer_request{
        .kind = FileDescriptionKind::PipeWriter,
        .file_status_flags = OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
        .terminal = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = pipe,
        .pipe_manager = &dynamic_pipe_manager,
        .vfs = nullptr,
        .open_file = {},
    };
    KernelObjectReference reader_reference{};
    if (file_description_manager.Create(reader_request, reader_reference) !=
        FileDescriptionStatus::Succeeded) {
        static_cast<void>(dynamic_pipe_manager.CloseReader(*pipe));
        static_cast<void>(dynamic_pipe_manager.CloseWriter(*pipe));
        return ProcessIoStatus::ObjectFailure;
    }
    KernelObjectReference writer_reference{};
    if (file_description_manager.Create(writer_request, writer_reference) !=
        FileDescriptionStatus::Succeeded) {
        static_cast<void>(reader_reference.Reset());
        static_cast<void>(dynamic_pipe_manager.CloseWriter(*pipe));
        return ProcessIoStatus::ObjectFailure;
    }

    FileTable &file_table = CurrentRuntimeProcess().file_table;
    const FileTableStatus reader_install_status =
        file_table.Install(reader_reference, OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR,
                           OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, reader_descriptor);
    if (reader_install_status != FileTableStatus::Succeeded) {
        static_cast<void>(reader_reference.Reset());
        static_cast<void>(writer_reference.Reset());
        reader_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
        return MapFileTableStatus(reader_install_status);
    }
    const FileTableStatus writer_install_status =
        file_table.Install(writer_reference, OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR,
                           OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, writer_descriptor);
    if (writer_install_status == FileTableStatus::Succeeded) {
        return ProcessIoStatus::Succeeded;
    }

    KernelObjectReleaseResult release_result{};
    if (file_table.Close(reader_descriptor, release_result) != FileTableStatus::Succeeded) {
        HaltProcessor();
    }
    WakeAfterDescriptionRelease(release_result);
    static_cast<void>(writer_reference.Reset());
    reader_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    writer_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    return MapFileTableStatus(writer_install_status);
}

ProcessIoStatus GetCurrentProcessDescriptorFlags(const uint64_t descriptor,
                                                 uint64_t &descriptor_flags) noexcept {
    descriptor_flags = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    return MapFileTableStatus(
        CurrentRuntimeProcess().file_table.GetDescriptorFlags(descriptor, descriptor_flags));
}

ProcessIoStatus SetCurrentProcessDescriptorFlags(const uint64_t descriptor,
                                                 const uint64_t descriptor_flags) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    return MapFileTableStatus(
        CurrentRuntimeProcess().file_table.SetDescriptorFlags(descriptor, descriptor_flags));
}

ProcessIoStatus SetCurrentProcessDescriptorSoftLimit(const uint64_t soft_limit) noexcept {
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    os::abi::ResourceLimit &limit =
        process.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::OpenFileCount)];
    if (soft_limit > limit.maximum) {
        return ProcessIoStatus::DescriptorLimitExceeded;
    }
    const ProcessIoStatus status = MapFileTableStatus(process.file_table.SetSoftLimit(soft_limit));
    if (status == ProcessIoStatus::Succeeded) {
        limit.current = soft_limit;
    }
    return status;
}

ProcessIoStatus GetCurrentProcessDescriptorLimits(uint64_t &soft_limit,
                                                  uint64_t &hard_limit) noexcept {
    soft_limit = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    hard_limit = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    const FileTableStatistics statistics = CurrentRuntimeProcess().file_table.Statistics();
    soft_limit = statistics.soft_limit;
    hard_limit = statistics.hard_limit;
    return ProcessIoStatus::Succeeded;
}

FileSystemStatus OpenCurrentProcessDirectory(const uint8_t *const path,
                                             const uint64_t path_length_bytes,
                                             uint64_t &file_descriptor) noexcept {
    file_descriptor = OS_KERNEL_PROCESS_RUNTIME_INVALID_FILE_DESCRIPTOR;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::NotInitialized;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    fs::OpenFile open_file{};
    const fs::Status status =
        process_vfs->OpenDirectory(process.file_system_context, path, path_length_bytes, open_file);
    if (status != fs::Status::Succeeded) {
        return fs::ToFileSystemStatus(status);
    }
    const FileDescriptionCreateRequest request{
        .kind = FileDescriptionKind::Directory,
        .file_status_flags = OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
        .terminal = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .pipe_manager = nullptr,
        .vfs = process_vfs,
        .open_file = open_file,
    };
    KernelObjectReference reference{};
    if (file_description_manager.Create(request, reference) != FileDescriptionStatus::Succeeded) {
        if (process_vfs->Close(open_file) != fs::Status::Succeeded) {
            HaltProcessor();
        }
        return FileSystemStatus::DataCapacityExhausted;
    }
    return process.file_table.Install(reference, OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR,
                                      OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
                                      file_descriptor) == FileTableStatus::Succeeded
               ? FileSystemStatus::Succeeded
               : FileSystemStatus::DataCapacityExhausted;
}

FileSystemStatus ReadCurrentProcessDirectory(const uint64_t file_descriptor,
                                             fs::DirectoryEntry &entry,
                                             bool &end_of_directory) noexcept {
    entry = fs::DirectoryEntry{};
    end_of_directory = false;
    if (!IsProcessSchedulingActive() || process_vfs == nullptr) {
        return FileSystemStatus::InvalidHandle;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    if (process.file_table.Lookup(file_descriptor, reference) != FileTableStatus::Succeeded) {
        return FileSystemStatus::InvalidHandle;
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
            FileDescriptionStatus::Succeeded ||
        snapshot.kind != FileDescriptionKind::Directory) {
        return FileSystemStatus::InvalidHandle;
    }
    FileSystemStatus file_system_status = FileSystemStatus::Succeeded;
    const FileDescriptionStatus read_status = file_description_manager.ReadDirectory(
        reference, entry, end_of_directory, file_system_status);
    return MapProcessIoToFileSystemStatus(MapFileDescriptionStatus(read_status),
                                          file_system_status);
}

ProcessIoStatus CurrentProcessDescriptorReadCanProgress(const uint64_t descriptor,
                                                        bool &can_progress) noexcept {
    can_progress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = process.file_table.Lookup(descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(lookup_status);
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
        FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::ObjectFailure;
    }
    if (snapshot.kind == FileDescriptionKind::TerminalInput ||
        snapshot.kind == FileDescriptionKind::TerminalDevice) {
        ThreadEntry current_thread{};
        ProcessEntry current_process{};
        JobControlProcessState job_state{};
        if (!ReadCurrentThreadAndProcess(current_thread, current_process) ||
            job_control_manager.ReadProcess(current_thread.process_index, job_state) !=
                JobControlStatus::Succeeded) {
            return ProcessIoStatus::ObjectFailure;
        }
        if (!process_terminal.CanRead(job_state.session_id, job_state.process_group_id)) {
            return ProcessIoStatus::BackgroundTerminalRead;
        }
    }
    const FileDescriptionStatus progress_status =
        file_description_manager.ReadCanProgress(reference, can_progress);
    if (progress_status == FileDescriptionStatus::Succeeded &&
        snapshot.kind == FileDescriptionKind::PipeReader && !can_progress) {
        ++pipe_reader_block_count;
    }
    return MapFileDescriptionStatus(progress_status);
}

ProcessIoStatus CurrentProcessDescriptorWriteCanProgress(const uint64_t descriptor,
                                                         bool &can_progress) noexcept {
    can_progress = false;
    if (!IsProcessSchedulingActive()) {
        return ProcessIoStatus::InvalidArgument;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    KernelObjectReference reference{};
    const FileTableStatus lookup_status = process.file_table.Lookup(descriptor, reference);
    if (lookup_status != FileTableStatus::Succeeded) {
        return MapFileTableStatus(lookup_status);
    }
    FileDescriptionSnapshot snapshot{};
    if (file_description_manager.ReadSnapshot(reference, snapshot) !=
        FileDescriptionStatus::Succeeded) {
        return ProcessIoStatus::ObjectFailure;
    }
    const FileDescriptionStatus progress_status =
        file_description_manager.WriteCanProgress(reference, can_progress);
    if (progress_status == FileDescriptionStatus::Succeeded &&
        snapshot.kind == FileDescriptionKind::PipeWriter && !can_progress) {
        ++pipe_writer_block_count;
    }
    return MapFileDescriptionStatus(progress_status);
}

void SubmitConsoleCharacter(const uint8_t character) noexcept {
    TerminalInputAction action = TerminalInputAction::None;
    const TerminalStatus submit_status = process_terminal.SubmitCharacter(character, action);
    if (submit_status != TerminalStatus::Succeeded) {
        return;
    }
    if (action == TerminalInputAction::Buffered) {
        EchoTerminalBytes(&character, sizeof(character));
    } else if (action == TerminalInputAction::Erased) {
        EchoTerminalBytes(OS_KERNEL_PROCESS_RUNTIME_TERMINAL_ERASE_SEQUENCE,
                          sizeof(OS_KERNEL_PROCESS_RUNTIME_TERMINAL_ERASE_SEQUENCE));
    } else if (action == TerminalInputAction::InputReady &&
               (character == OS_KERNEL_TERMINAL_NEWLINE_CHARACTER ||
                character == OS_KERNEL_TERMINAL_CARRIAGE_RETURN_CHARACTER)) {
        EchoTerminalBytes(OS_KERNEL_PROCESS_RUNTIME_TERMINAL_NEWLINE_SEQUENCE,
                          sizeof(OS_KERNEL_PROCESS_RUNTIME_TERMINAL_NEWLINE_SEQUENCE));
    } else if (action == TerminalInputAction::InterruptForeground) {
        EchoTerminalBytes(OS_KERNEL_PROCESS_RUNTIME_TERMINAL_INTERRUPT_SEQUENCE,
                          sizeof(OS_KERNEL_PROCESS_RUNTIME_TERMINAL_INTERRUPT_SEQUENCE));
        uint64_t target_process_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        static_cast<void>(SendSignalToProcessGroup(process_terminal.ForegroundProcessGroupId(),
                                                   os::abi::OS_ABI_SIGNAL_INTERRUPT_NUMBER,
                                                   target_process_count));
    } else if (action == TerminalInputAction::StopForeground) {
        EchoTerminalBytes(OS_KERNEL_PROCESS_RUNTIME_TERMINAL_STOP_SEQUENCE,
                          sizeof(OS_KERNEL_PROCESS_RUNTIME_TERMINAL_STOP_SEQUENCE));
        uint64_t target_process_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        static_cast<void>(SendSignalToProcessGroup(process_terminal.ForegroundProcessGroupId(),
                                                   os::abi::OS_ABI_SIGNAL_TERMINAL_STOP_NUMBER,
                                                   target_process_count));
    }
    if (process_scheduling_active && (action == TerminalInputAction::InputReady ||
                                      action == TerminalInputAction::InputReadyNoEcho ||
                                      action == TerminalInputAction::EndOfFileReady)) {
        WakeRequiredThreads(WaitCondition::DescriptorReadable, WakeReason::ConditionSatisfied);
    }
}

bool ProcessPipeReadCanProgress() noexcept { return process_pipe.ReadCanProgress(); }

bool ProcessPipeWriteCanProgress() noexcept { return process_pipe.WriteCanProgress(); }

ProcessRuntimeStatus BlockCurrentThread(ExceptionFrame &frame, const WaitCondition wait_condition,
                                        const uint64_t blocked_system_call_number,
                                        const bool restartable,
                                        ExceptionFrame *&resume_frame) noexcept {
    resume_frame = &frame;
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    WaitQueue *const wait_queue = SelectWaitQueue(wait_condition);
    if (!IsProcessSchedulingActive() || wait_queue == nullptr ||
        !CurrentFrameIsValid(thread_index, frame)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[thread_index];
    if ((wait_condition == WaitCondition::BlockIo) !=
        (runtime_thread.block_io_request_identifier != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE)) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    runtime_thread.saved_frame = &frame;
    runtime_thread.blocked_system_call_number = blocked_system_call_number;
    runtime_thread.blocked_system_call_restartable = restartable;
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        return ProcessRuntimeStatus::ExtendedStateFailure;
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus status =
        thread_scheduler.BlockCurrentThread(*wait_queue, wait_condition, decision);
    bool pending_signal_wake_failed = false;
    if (status == ThreadSchedulerStatus::Succeeded && wait_condition != WaitCondition::BlockIo) {
        SignalThreadState signal_thread{};
        const SignalManagerStatus signal_status =
            signal_manager.ReadThread(thread_index, signal_thread);
        const bool eligible_signal_pending =
            signal_status == SignalManagerStatus::Succeeded &&
            (signal_thread.pending_set & ~signal_thread.signal_mask) !=
                OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (signal_status != SignalManagerStatus::Succeeded) {
            pending_signal_wake_failed = true;
        } else if (eligible_signal_pending) {
            bool wake_won = false;
            const ThreadSchedulerStatus wake_status = thread_scheduler.WakeThread(
                *wait_queue, thread_index, WakeReason::Signal, wake_won);
            pending_signal_wake_failed =
                wake_status != ThreadSchedulerStatus::Succeeded || !wake_won;
            if (!pending_signal_wake_failed) {
                runtime_thread.saved_frame->register_rax =
                    static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_INTERRUPTED);
            }
        }
    }
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (status != ThreadSchedulerStatus::Succeeded || pending_signal_wake_failed) {
        runtime_thread.blocked_system_call_number = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        if (wait_condition == WaitCondition::BlockIo) {
            runtime_thread.block_io_request_identifier = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        }
        runtime_thread.blocked_system_call_restartable = false;
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (wait_condition == WaitCondition::PipeReadable) {
        ++pipe_reader_block_count;
    } else if (wait_condition == WaitCondition::PipeWritable) {
        ++pipe_writer_block_count;
    } else if (wait_condition == WaitCondition::DescriptorReadable) {
        ++descriptor_reader_block_count;
        if (IsPowerOfTwoCounter(descriptor_reader_block_count)) {
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_IO_DESCRIPTOR_READ_BLOCK_PREFIX,
                                     descriptor_reader_block_count);
        }
    }
    if (!decision.switched) {
        ReturnUserModeToProcessDispatcher(false);
    }
    resume_frame = ActivateScheduledUserOrReturnToDispatcher(decision);
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus WakeThreads(const WaitCondition wait_condition, const WakeReason wake_reason,
                                 const uint64_t maximum_wake_count,
                                 uint64_t &woken_thread_count) noexcept {
    woken_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    WaitQueue *const wait_queue = SelectWaitQueue(wait_condition);
    if (!process_runtime_initialized || !process_scheduling_active || wait_queue == nullptr) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus status =
        thread_scheduler.WakeMany(*wait_queue, wake_reason, maximum_wake_count, woken_thread_count);
    scheduler_lock.Unlock(interrupts_were_enabled);
    return status == ThreadSchedulerStatus::Succeeded ? ProcessRuntimeStatus::Succeeded
                                                      : ProcessRuntimeStatus::SchedulerFailure;
}

uint64_t CurrentThreadIndexForBlockIo() noexcept {
    if (!process_runtime_initialized || !process_scheduling_active) {
        return OS_KERNEL_THREAD_INVALID_INDEX;
    }
    return thread_scheduler.CurrentThreadIndex();
}

ProcessRuntimeStatus RegisterCurrentBlockIoRequest(const uint64_t request_identifier) noexcept {
    const uint64_t owner_thread_index = CurrentThreadIndexForBlockIo();
    if (request_identifier == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        owner_thread_index == OS_KERNEL_THREAD_INVALID_INDEX ||
        owner_thread_index >= process_runtime_limits.thread_capacity ||
        !runtime_threads[owner_thread_index].active ||
        runtime_threads[owner_thread_index].block_io_request_identifier !=
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    runtime_threads[owner_thread_index].block_io_request_identifier = request_identifier;
    return ProcessRuntimeStatus::Succeeded;
}

ProcessRuntimeStatus CompleteBlockIoRequest(const uint64_t owner_thread_index,
                                            const uint64_t request_identifier,
                                            const BlockRequestResult result) noexcept {
    if (!process_runtime_initialized || !process_scheduling_active ||
        request_identifier == OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE ||
        owner_thread_index >= process_runtime_limits.thread_capacity) {
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    if (!runtime_threads[owner_thread_index].active ||
        runtime_threads[owner_thread_index].saved_frame == nullptr ||
        runtime_threads[owner_thread_index].block_io_request_identifier != request_identifier) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_ABANDONED_REQUEST_PREFIX,
                                 request_identifier);
        return ProcessRuntimeStatus::BlockIoRequestAbandoned;
    }
    const int64_t system_call_result = result == BlockRequestResult::Succeeded
                                           ? OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE
                                       : result == BlockRequestResult::TimedOut
                                           ? os::abi::OS_ABI_SYSTEM_CALL_RESULT_TIMED_OUT
                                           : os::abi::OS_ABI_SYSTEM_CALL_RESULT_DEVICE_FAILURE;
    const WakeReason wake_reason = result == BlockRequestResult::TimedOut
                                       ? WakeReason::Timeout
                                       : WakeReason::ConditionSatisfied;
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const uint64_t completing_thread_index = thread_scheduler.CurrentThreadIndex();
    const bool other_thread_made_progress =
        completing_thread_index != OS_KERNEL_THREAD_INVALID_INDEX &&
        completing_thread_index != owner_thread_index;
    bool wake_won = false;
    const ThreadSchedulerStatus wake_status =
        thread_scheduler.WakeThread(block_io_wait_queue, owner_thread_index, wake_reason, wake_won);
    if (wake_status != ThreadSchedulerStatus::Succeeded || !wake_won) {
        scheduler_lock.Unlock(interrupts_were_enabled);
        return ProcessRuntimeStatus::SchedulerFailure;
    }
    ProcessRuntimeThread &runtime_thread = runtime_threads[owner_thread_index];
    runtime_thread.saved_frame->register_rax = static_cast<uint64_t>(system_call_result);
    runtime_thread.blocked_system_call_number = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_thread.block_io_request_identifier = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    runtime_thread.blocked_system_call_restartable = false;
    scheduler_lock.Unlock(interrupts_were_enabled);
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_COMPLETION_RESULT_PREFIX,
                             static_cast<uint64_t>(result));
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_COMPLETION_TIME_PREFIX,
                             GetMonotonicNanoseconds());
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_BLOCK_IO_OTHER_THREAD_PROGRESS_PREFIX,
                             other_thread_made_progress ? OS_KERNEL_PROCESS_RUNTIME_BOOLEAN_TRUE
                                                        : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
    return ProcessRuntimeStatus::Succeeded;
}

uint64_t HandleProcessDeadlineInterrupt(const uint64_t now_nanoseconds) noexcept {
    if (!process_runtime_initialized || !process_scheduling_active) {
        return OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    }

    uint64_t expired_deadline_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    while (true) {
        uint64_t woken_thread_index = OS_KERNEL_THREAD_INVALID_INDEX;
        WaitCondition wait_condition = WaitCondition::None;
        WaitQueue *wait_queue = nullptr;
        bool expired = false;
        const ThreadSchedulerStatus expire_status = thread_scheduler.ExpireNextDeadline(
            now_nanoseconds, woken_thread_index, wait_condition, wait_queue, expired);
        if (expire_status != ThreadSchedulerStatus::Succeeded) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            HaltProcessor();
        }
        if (!expired) {
            break;
        }
        if (woken_thread_index >= process_runtime_limits.thread_capacity || wait_queue == nullptr) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            HaltProcessor();
        }
        ThreadEntry woken_thread{};
        if (thread_scheduler.ReadThread(woken_thread_index, woken_thread) !=
            ThreadSchedulerStatus::Succeeded) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            HaltProcessor();
        }
        if (woken_thread.kind == ThreadKind::Kernel) {
            if (wait_condition != WaitCondition::KernelWork ||
                wait_queue != &kernel_work_wait_queue ||
                kernel_thread_runtime_statistics.wake_count == UINT64_MAX) {
                scheduler_lock.Unlock(interrupts_were_enabled);
                HaltProcessor();
            }
            ++kernel_thread_runtime_statistics.wake_count;
            ++expired_deadline_count;
            continue;
        }
        if (woken_thread.kind != ThreadKind::User ||
            runtime_threads[woken_thread_index].saved_frame == nullptr) {
            scheduler_lock.Unlock(interrupts_were_enabled);
            HaltProcessor();
        }
        ExceptionFrame &saved_frame = *runtime_threads[woken_thread_index].saved_frame;
        if (wait_condition == WaitCondition::Sleep) {
            saved_frame.register_rax = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
        } else if (wait_condition == WaitCondition::PrivateFutex) {
            saved_frame.register_rax =
                static_cast<uint64_t>(os::abi::OS_ABI_SYSTEM_CALL_RESULT_TIMED_OUT);
            bool entry_found = false;
            for (uint64_t entry_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
                 entry_index < OS_KERNEL_PRIVATE_FUTEX_CAPACITY_LIMIT; ++entry_index) {
                if (!private_futex_entries[entry_index].active ||
                    &private_futex_entries[entry_index].wait_queue != wait_queue) {
                    continue;
                }
                entry_found = true;
                if (wait_queue->Statistics().waiting_thread_count ==
                    OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE) {
                    bool entry_released = false;
                    if (private_futex_manager.ReleaseIfEmpty(entry_index, entry_released) !=
                            PrivateFutexStatus::Succeeded ||
                        !entry_released) {
                        scheduler_lock.Unlock(interrupts_were_enabled);
                        HaltProcessor();
                    }
                }
                break;
            }
            if (!entry_found ||
                private_futex_manager.RecordTimeoutOperation() != PrivateFutexStatus::Succeeded) {
                scheduler_lock.Unlock(interrupts_were_enabled);
                HaltProcessor();
            }
        } else {
            scheduler_lock.Unlock(interrupts_were_enabled);
            HaltProcessor();
        }
        expired_deadline_count += OS_KERNEL_PROCESS_RUNTIME_COUNTER_INCREMENT;
    }
    scheduler_lock.Unlock(interrupts_were_enabled);
    return expired_deadline_count;
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
    if (SaveFxState(runtime_thread.extended_state) != ExtendedStateStatus::Succeeded) {
        HaltProcessor();
    }

    ThreadSchedulingDecision decision{};
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus scheduler_status = thread_scheduler.HandleTimerTick(decision);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (scheduler_status != ThreadSchedulerStatus::Succeeded) {
        HaltProcessor();
    }
    if (!decision.switched) {
        return &frame;
    }
    const bool unwind_interrupt =
        GetCpuLocal().Statistics().interrupt_depth != OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    return ActivateScheduledUserOrReturnToDispatcher(decision, unwind_interrupt);
}

ExceptionFrame *RescheduleBeforeUserReturn(ExceptionFrame &frame) noexcept {
    return HandleProcessTimerInterrupt(frame);
}

bool CurrentThreadOwnsUserContext(const ExceptionFrame &frame) noexcept {
    if (!process_scheduling_active) {
        return false;
    }
    return CurrentFrameIsValid(thread_scheduler.CurrentThreadIndex(), frame);
}

UserVirtualMemoryStatus
ResolveCurrentProcessUserReturnMemory(const uint64_t instruction_pointer,
                                      const uint64_t stack_pointer) noexcept {
    if (!process_scheduling_active) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    return ResolveUserReturnMemory(CurrentRuntimeProcess().address_space, instruction_pointer,
                                   stack_pointer);
}

[[nodiscard]] bool TerminateNonCurrentOomVictim(const uint64_t process_index) noexcept {
    const uint64_t current_thread_index = thread_scheduler.CurrentThreadIndex();
    ThreadEntry current_thread{};
    if (process_index >= process_runtime_limits.process_capacity ||
        thread_scheduler.ReadThread(current_thread_index, current_thread) !=
            ThreadSchedulerStatus::Succeeded ||
        current_thread.process_index == process_index || !runtime_processes[process_index].active ||
        ProcessHasOtherKernelContinuation(process_index, OS_KERNEL_THREAD_INVALID_INDEX)) {
        return false;
    }
    ProcessRuntimeProcess &process = runtime_processes[process_index];
    uint64_t cancelled_futex_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (!CancelPrivateFutexRange(
            process.address_space.address_space_identifier, OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE, true, cancelled_futex_thread_count) ||
        !FlushOutstandingUserFilePages()) {
        return false;
    }
    static_cast<void>(cancelled_futex_thread_count);
    process.result.termination_reason = ProcessTerminationReason::Signal;
    process.result.exit_code = OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE;
    process.result.exception_vector = os::abi::OS_ABI_SIGNAL_KILL_NUMBER;
    CloseProcessIoDescriptors(process);
    if (process_vfs != nullptr && process.file_system_context.initialized &&
        process_vfs->ReleaseContext(process.file_system_context) != fs::Status::Succeeded) {
        return false;
    }
    uint64_t reparented_process_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    if (process_tree.MarkExited(process_index,
                                ProcessTreeExitStatus{
                                    .termination_reason = ProcessTreeTerminationReason::Signal,
                                    .exit_code = OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
                                    .exception_vector = os::abi::OS_ABI_SIGNAL_KILL_NUMBER,
                                },
                                reparented_process_count) != ProcessTreeStatus::Succeeded) {
        return false;
    }
    static_cast<void>(reparented_process_count);
    WakeRequiredThreads(WaitCondition::ChildProcess, WakeReason::ConditionSatisfied);

    uint64_t terminated_thread_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    const bool interrupts_were_enabled = scheduler_lock.Lock();
    const ThreadSchedulerStatus terminate_status =
        thread_scheduler.TerminateNonCurrentProcess(process_index, terminated_thread_count);
    scheduler_lock.Unlock(interrupts_were_enabled);
    if (terminate_status != ThreadSchedulerStatus::Succeeded) {
        return false;
    }
    for (uint64_t thread_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         thread_index < process_runtime_limits.thread_capacity; ++thread_index) {
        SignalThreadState signal_thread{};
        if (signal_manager.ReadThread(thread_index, signal_thread) ==
                SignalManagerStatus::Succeeded &&
            signal_thread.process_index == process_index &&
            !RemoveSignalThreadIfPresent(thread_index)) {
            return false;
        }
    }
    user_thread_runtime_statistics.process_exit_cancelled_thread_count += terminated_thread_count;
    if (DestroyUserAddressSpace(process.address_space) != UserAddressSpaceStatus::Succeeded) {
        return false;
    }
    process.result.mapped_page_count = OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE;
    return true;
}

[[nodiscard]] static bool TryRecoverFromOutOfMemory(const uint64_t current_process_index,
                                                    const bool allow_current_victim) noexcept {
    if (oom_invocation_count == UINT64_MAX) {
        return false;
    }
    ++oom_invocation_count;
    for (uint64_t process_index = OS_KERNEL_PROCESS_RUNTIME_FIRST_INDEX;
         process_index < process_runtime_limits.process_capacity; ++process_index) {
        ProcessEntry scheduler_process{};
        const bool eligible = runtime_processes[process_index].active &&
                              thread_scheduler.ReadProcess(process_index, scheduler_process) ==
                                  ThreadSchedulerStatus::Succeeded &&
                              scheduler_process.state == ProcessState::Alive;
        oom_candidates[process_index] = OomCandidate{
            .process_id = eligible ? runtime_processes[process_index].result.process_id
                                   : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            .resident_page_count =
                eligible ? runtime_processes[process_index].address_space.mapped_page_count
                         : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            .swapped_page_count =
                eligible ? runtime_processes[process_index].address_space.swapped_page_count
                         : OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE,
            .score_adjustment = 0LL,
            .protected_process =
                eligible &&
                (runtime_processes[process_index].result.process_id ==
                     OS_KERNEL_PROCESS_RUNTIME_INIT_PROCESS_ID ||
                 (!allow_current_victim && process_index == current_process_index) ||
                 ProcessHasOtherKernelContinuation(process_index, OS_KERNEL_THREAD_INVALID_INDEX)),
            .active = eligible,
        };
    }
    const MemoryPressureStatistics pressure_statistics = GetUserMemoryPressureStatistics();
    OomVictim victim{};
    if (SelectOomVictim(oom_candidates, process_runtime_limits.process_capacity,
                        pressure_statistics.watermarks.resident_limit_page_count,
                        victim) != OomSelectionStatus::Succeeded ||
        victim.candidate_index >= process_runtime_limits.process_capacity) {
        return false;
    }
    last_oom_victim_process_id = victim.process_id;
    last_oom_victim_score = victim.score;
    if (oom_kill_count == UINT64_MAX) {
        return false;
    }
    ++oom_kill_count;
    if (victim.candidate_index == current_process_index) {
        current_process_oom_kill_pending = true;
        return false;
    }
    return TerminateNonCurrentOomVictim(victim.candidate_index);
}

bool HandleCurrentProcessPageFault(ExceptionFrame &frame, const uint64_t fault_address) noexcept {
    const uint64_t thread_index = thread_scheduler.CurrentThreadIndex();
    if (!process_scheduling_active || !CurrentFrameIsValid(thread_index, frame)) {
        return false;
    }
    ProcessRuntimeProcess &process = CurrentRuntimeProcess();
    if (fault_address >= OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS &&
        fault_address < OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        const uint64_t stack_limit =
            process.resource_limits[static_cast<uint64_t>(os::abi::ResourceLimitKind::Stack)]
                .current;
        if (fault_address < OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS - stack_limit) {
            return false;
        }
    }
    const uint64_t previous_stack_growth_count =
        process.address_space.stack_growth_page_fault_count;
    const uint64_t previous_file_fault_count = process.address_space.file_page_fault_count;
    const uint64_t previous_page_cache_hit_count = process.address_space.page_cache_hit_count;
    const UserPageFaultStatus status = HandleUserPageFault(
        process.address_space, fault_address, frame.error_code, AsUserContext(frame).stack_pointer);
    if (status != UserPageFaultStatus::Handled) {
        return false;
    }
    process.result.mapped_page_count = process.address_space.mapped_page_count;

    // 只在 1、2、4、8……次故障打印，既保留增长轨迹，也避免大堆触页持续滚屏。
    const uint64_t demand_fault_count = process.address_space.demand_page_fault_count;
    if (IsPowerOfTwoCounter(demand_fault_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_VM_DEMAND_FAULT_PREFIX,
                                 demand_fault_count);
    }
    if (process.address_space.stack_growth_page_fault_count != previous_stack_growth_count) {
        const uint64_t stack_growth_count = process.address_space.stack_growth_page_fault_count;
        if (IsPowerOfTwoCounter(stack_growth_count)) {
            WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_VM_STACK_GROWTH_PREFIX,
                                     stack_growth_count);
        }
    }
    if (process.address_space.file_page_fault_count != previous_file_fault_count &&
        IsPowerOfTwoCounter(process.address_space.file_page_fault_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_VM_FILE_FAULT_PREFIX,
                                 process.address_space.file_page_fault_count);
    }
    if (process.address_space.page_cache_hit_count != previous_page_cache_hit_count &&
        IsPowerOfTwoCounter(process.address_space.page_cache_hit_count)) {
        WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_VM_PAGE_CACHE_HIT_PREFIX,
                                 process.address_space.page_cache_hit_count);
    }
    return true;
}

ExceptionFrame *TerminateCurrentProcessFromExit(ExceptionFrame &frame,
                                                const int64_t exit_code) noexcept {
    return CompleteCurrentThread(frame, ProcessTerminationReason::Exited, exit_code,
                                 OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}

ExceptionFrame *TerminateCurrentProcessFromException(ExceptionFrame &frame,
                                                     const uint64_t page_fault_address) noexcept {
    if (current_process_oom_kill_pending) {
        current_process_oom_kill_pending = false;
        frame.vector = os::abi::OS_ABI_SIGNAL_KILL_NUMBER;
        frame.error_code = OS_KERNEL_PROCESS_RUNTIME_NORMALIZED_ERROR_CODE;
        return CompleteCurrentThread(frame, ProcessTerminationReason::Signal,
                                     OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
                                     OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
    }
    return CompleteCurrentThread(frame, ProcessTerminationReason::Exception,
                                 OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE, page_fault_address);
}

ExceptionFrame *TerminateCurrentProcessFromInvalidReturn(ExceptionFrame &frame) noexcept {
    frame.vector = OS_KERNEL_PROCESS_RUNTIME_INVALID_RETURN_VECTOR;
    frame.error_code = OS_KERNEL_PROCESS_RUNTIME_NORMALIZED_ERROR_CODE;
    frame.code_segment = static_cast<uint64_t>(OS_KERNEL_DESCRIPTOR_USER_CODE_SELECTOR);
    return CompleteCurrentThread(frame, ProcessTerminationReason::Exception,
                                 OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
                                 OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}

ExceptionFrame *TerminateCurrentProcessFromSignal(ExceptionFrame &frame,
                                                  const uint64_t signal_number) noexcept {
    frame.vector = signal_number;
    frame.error_code = OS_KERNEL_PROCESS_RUNTIME_NORMALIZED_ERROR_CODE;
    WriteProcessRuntimeValue(OS_KERNEL_PROCESS_RUNTIME_SIGNAL_TERMINATE_PREFIX, signal_number);
    return CompleteCurrentThread(frame, ProcessTerminationReason::Signal,
                                 OS_KERNEL_PROCESS_RUNTIME_SUCCESS_EXIT_CODE,
                                 OS_KERNEL_PROCESS_RUNTIME_EMPTY_VALUE);
}
}
