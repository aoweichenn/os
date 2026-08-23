#include <os/kernel/user/user_memory.hpp>

#include <os/kernel/memory/memory_manager.hpp>
#include <os/kernel/memory/memory_pressure.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <os/kernel/memory/swap_storage.hpp>
#include <os/kernel/user/file_backing.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_MEMORY_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_MASK =
    OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
constexpr uint64_t OS_KERNEL_USER_MEMORY_STACK_GROWTH_GAP_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_BOOTSTRAP_CAPACITY = 256ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_FUNCTIONAL_CAPACITY = 4096ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_PRIMARY_CAPACITY = 8192ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_BOOTSTRAP_METADATA_ORDER = 9ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_FUNCTIONAL_METADATA_ORDER = 11ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_PRIMARY_METADATA_ORDER = 13ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_FUNCTIONAL_MINIMUM_MANAGED_PAGE_COUNT =
    32768ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_FILE_CACHE_PRIMARY_MINIMUM_MANAGED_PAGE_COUNT = 524288ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_PRESENT_BIT = 1ULL << 0ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_WRITE_BIT = 1ULL << 1ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_USER_BIT = 1ULL << 2ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_RESERVED_BIT = 1ULL << 3ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PAGE_FAULT_INSTRUCTION_BIT = 1ULL << 4ULL;
constexpr uint8_t OS_KERNEL_USER_MEMORY_ZERO_BYTE = 0U;
constexpr uint64_t OS_KERNEL_USER_MEMORY_DIRTY_HARD_LIMIT_PERCENT = 20ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_PERCENT_DENOMINATOR = 100ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_WRITEBACK_BATCH_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_HOST_RESIDENT_LIMIT_PAGE_COUNT = 1048576ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_MAXIMUM_ALLOCATION_FRAME_COUNT = 4ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_RECLAIM_SCAN_PAGE_LIMIT = 65536ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_ADMIN_RESERVE_MAXIMUM_PAGE_COUNT = 2048ULL;
constexpr uint64_t OS_KERNEL_USER_MEMORY_ADMIN_RESERVE_PERCENT = 3ULL;
constexpr uint64_t OS_KERNEL_USER_SWAP_SELF_TEST_ADDRESS_SPACE_IDENTIFIER = UINT64_MAX;
constexpr uint64_t OS_KERNEL_USER_SWAP_SELF_TEST_PATTERN_MULTIPLIER = 37ULL;
constexpr uint8_t OS_KERNEL_USER_SWAP_SELF_TEST_PATTERN_SEED = 0x5AU;

UserAddressSpaceDestructionDiagnostics user_address_space_destruction_diagnostics;

struct MemoryImageReaderContext final {
    const uint8_t *image;
    uint64_t image_size_bytes;
};

struct VfsImageReaderContext final {
    fs::Vfs *vfs;
    fs::OpenFile open_file;
};

struct VfsFilePageReaderContext final {
    fs::Vfs *vfs;
    const fs::OpenFile *open_file;
    uint64_t source_size_bytes;
};

struct FilePageCacheRuntimeConfiguration final {
    uint64_t capacity;
    uint64_t metadata_block_order;
};

VirtualMemoryAreaDescriptor
    user_virtual_memory_descriptors[OS_KERNEL_USER_VMA_DESCRIPTOR_POOL_CAPACITY]{};
VirtualMemoryAreaPool user_virtual_memory_pool{};
UserFileBackingDescriptor user_file_backing_descriptors[OS_KERNEL_USER_FILE_BACKING_CAPACITY]{};
UserFileBackingManager user_file_backing_manager{};
FilePageCache user_file_page_cache{};
FileWritebackErrorTracker user_file_writeback_error_tracker{};
constinit KernelHeap user_file_page_cache_metadata_heap{};
PhysicalFrameBlock user_file_page_cache_metadata_block{};
uint64_t user_file_page_cache_metadata_size_bytes;
uint64_t user_file_page_cache_buffered_read_operation_count;
uint64_t user_file_page_cache_buffered_read_page_count;
uint64_t user_file_page_cache_buffered_read_cache_hit_count;
uint64_t user_file_page_cache_buffered_read_bytes;
uint64_t user_file_page_cache_buffered_write_operation_count;
uint64_t user_file_page_cache_buffered_write_page_count;
uint64_t user_file_page_cache_buffered_write_cache_hit_count;
uint64_t user_file_page_cache_buffered_write_bytes;
uint64_t user_file_page_cache_truncate_operation_count;
uint64_t user_file_page_cache_writeback_worker_run_count;
uint64_t user_file_page_cache_writeback_worker_written_page_count;
uint64_t user_file_page_cache_writeback_worker_failure_count;
uint64_t user_file_page_cache_writeback_backpressure_count;
uint64_t user_file_page_cache_readahead_operation_count;
uint64_t user_file_page_cache_readahead_requested_page_count;
uint64_t user_file_page_cache_readahead_loaded_page_count;
uint64_t user_file_page_cache_readahead_existing_page_count;
uint64_t user_file_page_cache_readahead_busy_page_count;
uint64_t user_file_page_cache_readahead_failed_page_count;
uint64_t user_file_page_cache_readahead_pressure_stop_count;
UserPageReferenceEntry user_page_reference_entries[OS_KERNEL_USER_PAGE_REFERENCE_CAPACITY]{};
UserPageReferenceManager user_page_reference_manager{};
MemoryPressureController user_memory_pressure_controller{};
MemoryOvercommitAccountant user_memory_overcommit_accountant{};
UserMemoryReclaimOperations user_memory_reclaim_operations{};
UserMemoryReclaimStatistics user_memory_reclaim_statistics{};
SwapManager user_swap_manager{};
SwapStorage user_swap_storage{};
uint8_t user_swap_clone_scratch_page[OS_KERNEL_MEMORY_PAGE_SIZE_BYTES]{};
UserAddressSpace *active_user_address_space;
uint64_t next_address_space_identifier = OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
uint64_t user_memory_resident_limit_override_page_count;
uint64_t user_memory_swappiness;
bool user_memory_swappiness_configured;
bool user_virtual_memory_initialized;
bool user_swap_attached;
UserSwapInitializationStage user_swap_initialization_stage;

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] bool AccumulateCounter(uint64_t &counter, const uint64_t increment) noexcept {
    if (counter > UINT64_MAX - increment) {
        return false;
    }
    counter += increment;
    return true;
}

[[nodiscard]] FilePageCacheRuntimeConfiguration
SelectFilePageCacheRuntimeConfiguration(const uint64_t managed_page_count) noexcept {
    if (managed_page_count >= OS_KERNEL_USER_MEMORY_FILE_CACHE_PRIMARY_MINIMUM_MANAGED_PAGE_COUNT) {
        return FilePageCacheRuntimeConfiguration{
            .capacity = OS_KERNEL_USER_MEMORY_FILE_CACHE_PRIMARY_CAPACITY,
            .metadata_block_order = OS_KERNEL_USER_MEMORY_FILE_CACHE_PRIMARY_METADATA_ORDER,
        };
    }
    if (managed_page_count >=
        OS_KERNEL_USER_MEMORY_FILE_CACHE_FUNCTIONAL_MINIMUM_MANAGED_PAGE_COUNT) {
        return FilePageCacheRuntimeConfiguration{
            .capacity = OS_KERNEL_USER_MEMORY_FILE_CACHE_FUNCTIONAL_CAPACITY,
            .metadata_block_order = OS_KERNEL_USER_MEMORY_FILE_CACHE_FUNCTIONAL_METADATA_ORDER,
        };
    }
    return FilePageCacheRuntimeConfiguration{
        .capacity = OS_KERNEL_USER_MEMORY_FILE_CACHE_BOOTSTRAP_CAPACITY,
        .metadata_block_order = OS_KERNEL_USER_MEMORY_FILE_CACHE_BOOTSTRAP_METADATA_ORDER,
    };
}

[[nodiscard]] uint8_t SwapSelfTestPattern(const uint64_t byte_index) noexcept {
    return static_cast<uint8_t>(
        (byte_index * OS_KERNEL_USER_SWAP_SELF_TEST_PATTERN_MULTIPLIER +
         static_cast<uint64_t>(OS_KERNEL_USER_SWAP_SELF_TEST_PATTERN_SEED)) &
        0xFFULL);
}

[[nodiscard]] bool RunUserSwapStorageSelfTest() noexcept {
    for (uint64_t byte_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         byte_index < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byte_index) {
        user_swap_clone_scratch_page[byte_index] = SwapSelfTestPattern(byte_index);
    }
    const SwapPageIdentity identity{
        .address_space_identifier = OS_KERNEL_USER_SWAP_SELF_TEST_ADDRESS_SPACE_IDENTIFIER,
        .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
    };
    uint64_t slot_index = UINT64_MAX;
    if (user_swap_manager.Store(identity, user_swap_clone_scratch_page,
                                OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                                slot_index) != SwapManagerStatus::Succeeded) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         byte_index < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byte_index) {
        user_swap_clone_scratch_page[byte_index] = OS_KERNEL_USER_MEMORY_ZERO_BYTE;
    }
    if (user_swap_manager.LoadAndRelease(identity, user_swap_clone_scratch_page,
                                         OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) !=
        SwapManagerStatus::Succeeded) {
        static_cast<void>(user_swap_manager.Release(identity));
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         byte_index < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byte_index) {
        if (user_swap_clone_scratch_page[byte_index] != SwapSelfTestPattern(byte_index)) {
            return false;
        }
    }
    return user_swap_manager.Validate() == SwapManagerStatus::Succeeded &&
           user_swap_manager.Statistics().active_slot_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
}

[[nodiscard]] bool SynchronizeUserMemoryPressure() noexcept {
    return user_memory_pressure_controller.SynchronizeResident(
               GetPhysicalFrameAllocatorStatistics().allocated_frame_count) ==
           MemoryPressureStatus::Succeeded;
}

[[nodiscard]] bool SwapOutUserPages(UserAddressSpace &address_space, uint64_t target_page_count,
                                    uint64_t excluded_virtual_address,
                                    uint64_t protected_virtual_address, void *selection_context,
                                    UserMemoryReclaimPageSelectionOperation selection_operation,
                                    UserMemoryReclaimPageCompletionOperation completion_operation,
                                    uint64_t &reclaimed_page_count) noexcept;

struct UserMemoryReclaimContext final {
    UserAddressSpace *address_space;
    uint64_t excluded_virtual_address;
    uint64_t protected_virtual_address;
    uint64_t written_page_count;
    bool allow_current_oom_victim;
};

[[nodiscard]] bool ReclaimCleanFilePages(const uint64_t target_page_count,
                                         uint64_t &reclaimed_page_count) noexcept {
    const FilePageCacheStatistics cache_statistics = user_file_page_cache.Statistics();
    const uint64_t target_resident_page_count =
        target_page_count < cache_statistics.resident_page_count
            ? cache_statistics.resident_page_count - target_page_count
            : OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    reclaimed_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const FilePageCacheStatus trim_status =
        user_file_page_cache.Trim(target_resident_page_count, reclaimed_page_count);
    return trim_status == FilePageCacheStatus::Succeeded ||
           trim_status == FilePageCacheStatus::EntryBusy ||
           trim_status == FilePageCacheStatus::DirtyPagesRemain;
}

[[nodiscard]] bool ExecuteCleanFileReclaim(void *const context, const uint64_t requested_page_count,
                                           uint64_t &reclaimed_page_count) noexcept {
    if (context == nullptr) {
        return false;
    }
    return ReclaimCleanFilePages(requested_page_count, reclaimed_page_count);
}

[[nodiscard]] bool ExecuteWrittenFileReclaim(void *const context,
                                             const uint64_t requested_page_count,
                                             uint64_t &reclaimed_page_count) noexcept {
    reclaimed_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (context == nullptr) {
        return false;
    }
    UserMemoryReclaimContext &reclaim_context = *static_cast<UserMemoryReclaimContext *>(context);
    if (reclaim_context.address_space == nullptr) {
        return false;
    }
    const bool mappings_protected =
        user_memory_reclaim_operations.protect_shared_mappings != nullptr
            ? user_memory_reclaim_operations.protect_shared_mappings(
                  user_memory_reclaim_operations.context)
            : ProtectUserSharedFileMappings(*reclaim_context.address_space) ==
                  UserVirtualMemoryStatus::Succeeded;
    if (!mappings_protected) {
        return false;
    }
    uint64_t written_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (WritebackUserFilePageCache(requested_page_count, written_page_count) !=
        UserVirtualMemoryStatus::Succeeded) {
        return false;
    }
    reclaim_context.written_page_count = written_page_count;
    return ReclaimCleanFilePages(requested_page_count, reclaimed_page_count);
}

[[nodiscard]] bool ExecuteAnonymousReclaim(void *const context, const uint64_t requested_page_count,
                                           uint64_t &reclaimed_page_count) noexcept {
    reclaimed_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (context == nullptr) {
        return false;
    }
    UserMemoryReclaimContext &reclaim_context = *static_cast<UserMemoryReclaimContext *>(context);
    if (reclaim_context.address_space == nullptr) {
        return false;
    }
    if (user_memory_reclaim_operations.reclaim_anonymous_pages != nullptr) {
        return user_memory_reclaim_operations.reclaim_anonymous_pages(
            user_memory_reclaim_operations.context, *reclaim_context.address_space,
            requested_page_count, reclaim_context.excluded_virtual_address,
            reclaim_context.protected_virtual_address, reclaimed_page_count);
    }
    return SwapOutUserPages(*reclaim_context.address_space, requested_page_count,
                            reclaim_context.excluded_virtual_address,
                            reclaim_context.protected_virtual_address, nullptr, nullptr, nullptr,
                            reclaimed_page_count);
}

[[nodiscard]] UserResidentAllocationStatus
PrepareUserResidentAllocation(UserAddressSpace &address_space, const uint64_t requested_page_count,
                              const uint64_t excluded_virtual_address,
                              const bool allow_current_oom_victim) noexcept {
    if (!user_virtual_memory_initialized || !SynchronizeUserMemoryPressure() ||
        user_memory_reclaim_statistics.allocation_request_count == UINT64_MAX) {
        return UserResidentAllocationStatus::NotInitialized;
    }
    ++user_memory_reclaim_statistics.allocation_request_count;
    MemoryAllocationDecision decision{};
    if (user_memory_pressure_controller.PrepareAllocation(requested_page_count,
                                                          MemoryAllocationClass::User, decision) !=
        MemoryPressureStatus::Succeeded) {
        return UserResidentAllocationStatus::Corrupt;
    }
    if (decision.action == MemoryAllocationAction::Allow) {
        if (decision.level == MemoryPressureLevel::BelowLow &&
            (user_memory_reclaim_operations.request_background_reclaim == nullptr ||
             !user_memory_reclaim_operations.request_background_reclaim(
                 user_memory_reclaim_operations.context))) {
            return UserResidentAllocationStatus::Corrupt;
        }
        if (user_memory_reclaim_statistics.immediate_allocation_count == UINT64_MAX) {
            return UserResidentAllocationStatus::Corrupt;
        }
        ++user_memory_reclaim_statistics.immediate_allocation_count;
        return UserResidentAllocationStatus::Succeeded;
    }
    if (decision.action == MemoryAllocationAction::Reject ||
        user_memory_reclaim_statistics.reclaim_cycle_count == UINT64_MAX) {
        return UserResidentAllocationStatus::PressureRejected;
    }
    ++user_memory_reclaim_statistics.reclaim_cycle_count;

    const FilePageCacheStatistics cache_statistics = user_file_page_cache.Statistics();
    uint64_t non_clean_file_page_count = cache_statistics.loading_page_count;
    if (!AccumulateCounter(non_clean_file_page_count, cache_statistics.dirty_page_count) ||
        !AccumulateCounter(non_clean_file_page_count, cache_statistics.writeback_page_count) ||
        !AccumulateCounter(non_clean_file_page_count, cache_statistics.error_page_count) ||
        non_clean_file_page_count > cache_statistics.resident_page_count) {
        return UserResidentAllocationStatus::Corrupt;
    }
    const uint64_t clean_file_page_count =
        cache_statistics.resident_page_count - non_clean_file_page_count;
    uint64_t dirty_file_page_count = cache_statistics.dirty_page_count;
    if (!AccumulateCounter(dirty_file_page_count, cache_statistics.error_page_count)) {
        return UserResidentAllocationStatus::Corrupt;
    }
    const SwapManagerStatistics swap_statistics = user_swap_manager.Statistics();
    MemoryReclaimPlan plan{};
    if (PlanMemoryReclaim(
            MemoryReclaimInput{
                .target_page_count = decision.target_reclaim_page_count,
                .clean_file_page_count = clean_file_page_count,
                .dirty_file_page_count = dirty_file_page_count,
                .anonymous_page_count =
                    user_memory_pressure_controller.Statistics().resident_page_count,
                .free_swap_page_count = user_swap_attached ? swap_statistics.free_slot_count
                                                           : OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                .swappiness = user_memory_swappiness,
            },
            plan) != MemoryReclaimPlanStatus::Succeeded) {
        return UserResidentAllocationStatus::Corrupt;
    }
    if (user_memory_reclaim_statistics.planned_file_page_count >
            UINT64_MAX - plan.file_budget_page_count ||
        user_memory_reclaim_statistics.planned_anonymous_page_count >
            UINT64_MAX - plan.anonymous_budget_page_count) {
        return UserResidentAllocationStatus::Corrupt;
    }
    user_memory_reclaim_statistics.planned_file_page_count += plan.file_budget_page_count;
    user_memory_reclaim_statistics.planned_anonymous_page_count += plan.anonymous_budget_page_count;
    UserMemoryReclaimContext reclaim_context{
        .address_space = &address_space,
        .excluded_virtual_address = excluded_virtual_address,
        .protected_virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
        .written_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
        .allow_current_oom_victim = allow_current_oom_victim,
    };
    MemoryReclaimExecutionResult reclaim_result{};
    const MemoryReclaimExecutionStatus execution_status =
        ExecuteMemoryReclaim(plan,
                             MemoryReclaimOperations{
                                 .reclaim_clean_file_pages = ExecuteCleanFileReclaim,
                                 .writeback_and_reclaim_file_pages = ExecuteWrittenFileReclaim,
                                 .swap_out_anonymous_pages = ExecuteAnonymousReclaim,
                             },
                             &reclaim_context, reclaim_result);
    if (execution_status == MemoryReclaimExecutionStatus::CleanReclaimFailed) {
        return UserResidentAllocationStatus::CleanReclaimFailed;
    }
    if (execution_status == MemoryReclaimExecutionStatus::FileWritebackFailed) {
        if (user_memory_reclaim_statistics.file_writeback_failure_count == UINT64_MAX) {
            return UserResidentAllocationStatus::Corrupt;
        }
        ++user_memory_reclaim_statistics.file_writeback_failure_count;
        return UserResidentAllocationStatus::FileWritebackFailed;
    }
    if (execution_status == MemoryReclaimExecutionStatus::AnonymousSwapFailed) {
        if (user_memory_reclaim_statistics.anonymous_swap_failure_count == UINT64_MAX) {
            return UserResidentAllocationStatus::Corrupt;
        }
        ++user_memory_reclaim_statistics.anonymous_swap_failure_count;
        return UserResidentAllocationStatus::AnonymousSwapFailed;
    }
    if (execution_status != MemoryReclaimExecutionStatus::Succeeded &&
        execution_status != MemoryReclaimExecutionStatus::NoProgress) {
        return UserResidentAllocationStatus::Corrupt;
    }
    if (!AccumulateCounter(user_memory_reclaim_statistics.clean_file_page_count,
                           reclaim_result.clean_file_page_count) ||
        !AccumulateCounter(user_memory_reclaim_statistics.written_file_page_count,
                           reclaim_context.written_page_count) ||
        !AccumulateCounter(user_memory_reclaim_statistics.reclaimed_written_file_page_count,
                           reclaim_result.reclaimed_written_file_page_count) ||
        !AccumulateCounter(user_memory_reclaim_statistics.swapped_anonymous_page_count,
                           reclaim_result.swapped_anonymous_page_count) ||
        (reclaim_result.reclaimed_page_count != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
         user_memory_pressure_controller.RecordReclaim(reclaim_result.reclaimed_page_count) !=
             MemoryPressureStatus::Succeeded) ||
        !SynchronizeUserMemoryPressure()) {
        return UserResidentAllocationStatus::Corrupt;
    }
    MemoryAllocationDecision retry_decision{};
    if (user_memory_pressure_controller.PrepareAllocation(
            requested_page_count, MemoryAllocationClass::User, retry_decision) !=
        MemoryPressureStatus::Succeeded) {
        return UserResidentAllocationStatus::Corrupt;
    }
    if (retry_decision.action == MemoryAllocationAction::Allow) {
        if (user_memory_reclaim_statistics.reclaim_retry_success_count == UINT64_MAX) {
            return UserResidentAllocationStatus::Corrupt;
        }
        ++user_memory_reclaim_statistics.reclaim_retry_success_count;
        return UserResidentAllocationStatus::Succeeded;
    }
    if (execution_status == MemoryReclaimExecutionStatus::NoProgress ||
        reclaim_result.reclaimed_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        if (user_memory_reclaim_statistics.no_progress_count == UINT64_MAX) {
            return UserResidentAllocationStatus::Corrupt;
        }
        ++user_memory_reclaim_statistics.no_progress_count;
    }
    if (user_memory_reclaim_operations.recover_out_of_memory == nullptr ||
        user_memory_reclaim_statistics.oom_attempt_count == UINT64_MAX) {
        return UserResidentAllocationStatus::OutOfMemoryUnavailable;
    }
    ++user_memory_reclaim_statistics.oom_attempt_count;
    if (!user_memory_reclaim_operations.recover_out_of_memory(
            user_memory_reclaim_operations.context, address_space,
            reclaim_context.allow_current_oom_victim)) {
        return UserResidentAllocationStatus::OutOfMemoryUnavailable;
    }
    if (user_memory_reclaim_statistics.oom_recovery_count == UINT64_MAX ||
        !SynchronizeUserMemoryPressure()) {
        return UserResidentAllocationStatus::Corrupt;
    }
    ++user_memory_reclaim_statistics.oom_recovery_count;
    MemoryAllocationDecision oom_retry_decision{};
    if (user_memory_pressure_controller.PrepareAllocation(
            requested_page_count, MemoryAllocationClass::User, oom_retry_decision) !=
        MemoryPressureStatus::Succeeded) {
        return UserResidentAllocationStatus::Corrupt;
    }
    if (oom_retry_decision.action != MemoryAllocationAction::Allow) {
        return UserResidentAllocationStatus::PressureRejected;
    }
    if (user_memory_reclaim_statistics.reclaim_retry_success_count == UINT64_MAX) {
        return UserResidentAllocationStatus::Corrupt;
    }
    ++user_memory_reclaim_statistics.reclaim_retry_success_count;
    return UserResidentAllocationStatus::Succeeded;
}

[[nodiscard]] uint64_t AlignDownToPage(const uint64_t value) noexcept {
    return value & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
}

[[nodiscard]] bool AlignUpToPage(const uint64_t value, uint64_t &aligned_value) noexcept {
    if (value > UINT64_MAX - OS_KERNEL_USER_MEMORY_PAGE_MASK) {
        return false;
    }
    aligned_value = (value + OS_KERNEL_USER_MEMORY_PAGE_MASK) & ~OS_KERNEL_USER_MEMORY_PAGE_MASK;
    return true;
}

[[nodiscard]] bool AssignAddressSpaceIdentifier(UserAddressSpace &address_space) noexcept {
    if (next_address_space_identifier == UINT64_MAX) {
        return false;
    }
    address_space.address_space_identifier = next_address_space_identifier;
    ++next_address_space_identifier;
    return true;
}

[[nodiscard]] uint8_t *PhysicalPagePointer(const uint64_t physical_address) noexcept {
    const uint64_t direct_map_address = PhysicalMemoryDirectMapAddress(physical_address);
    return direct_map_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE
               ? nullptr
               : reinterpret_cast<uint8_t *>(direct_map_address);
}

[[nodiscard]] bool ZeroPhysicalPage(const uint64_t physical_address) noexcept {
    uint8_t *const page = PhysicalPagePointer(physical_address);
    if (page == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         byte_index < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES; ++byte_index) {
        page[byte_index] = OS_KERNEL_USER_MEMORY_ZERO_BYTE;
    }
    return true;
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool ReleasePrivatePhysicalPage(const uint64_t physical_address) noexcept {
    bool release_frame = false;
    if (user_page_reference_manager.Release(physical_address, release_frame) !=
        UserPageReferenceStatus::Succeeded) {
        return false;
    }
    if (!release_frame) {
        return true;
    }
    if (user_memory_reclaim_operations.prepare_anonymous_page_release != nullptr &&
        !user_memory_reclaim_operations.prepare_anonymous_page_release(
            user_memory_reclaim_operations.context, physical_address)) {
        return false;
    }
    return GetKernelPhysicalFrameAllocator().Release(PhysicalFrame{
               .physical_address = physical_address}) == PhysicalFrameAllocatorStatus::Succeeded;
}

[[nodiscard]] bool IsAnonymousSwapArea(const VirtualMemoryArea &area) noexcept {
    return area.kind == VirtualMemoryAreaKind::Anonymous ||
           area.kind == VirtualMemoryAreaKind::ProgramBreak ||
           area.kind == VirtualMemoryAreaKind::UserStack;
}

[[nodiscard]] SwapPageIdentity MakeSwapIdentity(const UserAddressSpace &address_space,
                                                const uint64_t virtual_address) noexcept {
    return SwapPageIdentity{
        .address_space_identifier = address_space.address_space_identifier,
        .virtual_address = virtual_address,
    };
}

[[nodiscard]] bool
TrySwapOutPage(UserAddressSpace &address_space, const VirtualMemoryArea &area,
               const uint64_t page_address, void *const selection_context,
               const UserMemoryReclaimPageSelectionOperation selection_operation,
               const UserMemoryReclaimPageCompletionOperation completion_operation,
               bool &page_reclaimed) noexcept {
    page_reclaimed = false;
    PageMapping mapping{};
    const PageTableStatus query_status =
        QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping);
    if (query_status == PageTableStatus::NotMapped) {
        return true;
    }
    if (query_status != PageTableStatus::Succeeded || !mapping.permissions.user_accessible ||
        mapping.permissions.copy_on_write ||
        address_space.mapped_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return query_status == PageTableStatus::Succeeded && mapping.permissions.user_accessible &&
               mapping.permissions.copy_on_write;
    }
    bool selected = true;
    if (selection_operation != nullptr &&
        !selection_operation(selection_context, mapping.physical_address, selected)) {
        return false;
    }
    if (!selected) {
        return true;
    }
    uint8_t *const page = PhysicalPagePointer(mapping.physical_address);
    if (page == nullptr) {
        return false;
    }
    const SwapPageIdentity identity = MakeSwapIdentity(address_space, page_address);
    uint64_t slot_index = UINT64_MAX;
    if (user_swap_manager.Store(identity, page, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES, slot_index) !=
        SwapManagerStatus::Succeeded) {
        return false;
    }
    uint64_t unmapped_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (UnmapUserPage(address_space.root_physical_address, page_address, unmapped_physical_address,
                      reclaimed_table_frame_count) != KernelUserPageStatus::Succeeded ||
        unmapped_physical_address != mapping.physical_address) {
        static_cast<void>(user_swap_manager.Release(identity));
        return false;
    }
    const bool completion_succeeded =
        completion_operation == nullptr ||
        completion_operation(selection_context, unmapped_physical_address);
    if (!completion_succeeded || !ReleasePrivatePhysicalPage(unmapped_physical_address)) {
        const bool mapping_restored =
            MapExistingUserPage(address_space.root_physical_address, page_address,
                                unmapped_physical_address, area.permissions.writable,
                                area.permissions.executable) == KernelUserPageStatus::Succeeded;
        const bool slot_released =
            user_swap_manager.Release(identity) == SwapManagerStatus::Succeeded;
        static_cast<void>(mapping_restored);
        static_cast<void>(slot_released);
        return false;
    }
    --address_space.mapped_page_count;
    ++address_space.swapped_page_count;
    ++address_space.swap_out_page_count;
    if (address_space.swapped_page_count > address_space.peak_swapped_page_count) {
        address_space.peak_swapped_page_count = address_space.swapped_page_count;
    }
    address_space.page_table_reclaimed_frame_count += reclaimed_table_frame_count;
    page_reclaimed = true;
    return true;
}

[[nodiscard]] bool
ScanAndSwapUserPages(UserAddressSpace &address_space, const uint64_t scan_begin_address,
                     const uint64_t scan_end_address, const uint64_t target_page_count,
                     const uint64_t excluded_virtual_address,
                     const uint64_t protected_virtual_address, void *const selection_context,
                     const UserMemoryReclaimPageSelectionOperation selection_operation,
                     const UserMemoryReclaimPageCompletionOperation completion_operation,
                     uint64_t &scanned_page_count, uint64_t &reclaimed_page_count) noexcept {
    const uint64_t area_count = address_space.virtual_memory_map.AreaCount();
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         area_index < area_count && reclaimed_page_count < target_page_count &&
         scanned_page_count < OS_KERNEL_USER_MEMORY_RECLAIM_SCAN_PAGE_LIMIT;
         ++area_index) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return false;
        }
        if (!IsAnonymousSwapArea(area) || area.end_address <= scan_begin_address ||
            area.begin_address >= scan_end_address) {
            continue;
        }
        uint64_t page_address =
            area.begin_address < scan_begin_address ? scan_begin_address : area.begin_address;
        while (page_address < area.end_address && page_address < scan_end_address &&
               reclaimed_page_count < target_page_count &&
               scanned_page_count < OS_KERNEL_USER_MEMORY_RECLAIM_SCAN_PAGE_LIMIT) {
            ++scanned_page_count;
            address_space.reclaim_scan_virtual_address =
                page_address <= UINT64_MAX - OS_KERNEL_MEMORY_PAGE_SIZE_BYTES
                    ? page_address + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES
                    : OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            if (page_address != excluded_virtual_address &&
                page_address != protected_virtual_address) {
                bool page_reclaimed = false;
                if (!TrySwapOutPage(address_space, area, page_address, selection_context,
                                    selection_operation, completion_operation, page_reclaimed)) {
                    return false;
                }
                if (page_reclaimed) {
                    ++reclaimed_page_count;
                }
            }
            if (page_address > UINT64_MAX - OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
                break;
            }
            page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        }
    }
    return true;
}

[[nodiscard]] bool
SwapOutUserPages(UserAddressSpace &address_space, const uint64_t target_page_count,
                 const uint64_t excluded_virtual_address, const uint64_t protected_virtual_address,
                 void *const selection_context,
                 const UserMemoryReclaimPageSelectionOperation selection_operation,
                 const UserMemoryReclaimPageCompletionOperation completion_operation,
                 uint64_t &reclaimed_page_count) noexcept {
    reclaimed_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (target_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return true;
    }
    if (!user_swap_attached ||
        address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded ||
        address_space.address_space_identifier == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return false;
    }
    if ((selection_operation == nullptr) != (completion_operation == nullptr)) {
        return false;
    }
    const uint64_t scan_begin_address = address_space.reclaim_scan_virtual_address;
    uint64_t scanned_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!ScanAndSwapUserPages(address_space, scan_begin_address, UINT64_MAX, target_page_count,
                              excluded_virtual_address, protected_virtual_address,
                              selection_context, selection_operation, completion_operation,
                              scanned_page_count, reclaimed_page_count)) {
        return false;
    }
    if (reclaimed_page_count < target_page_count &&
        scan_begin_address != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
        scanned_page_count < OS_KERNEL_USER_MEMORY_RECLAIM_SCAN_PAGE_LIMIT &&
        !ScanAndSwapUserPages(address_space, OS_KERNEL_USER_MEMORY_EMPTY_VALUE, scan_begin_address,
                              target_page_count, excluded_virtual_address,
                              protected_virtual_address, selection_context, selection_operation,
                              completion_operation, scanned_page_count, reclaimed_page_count)) {
        return false;
    }
    if (reclaimed_page_count < target_page_count) {
        address_space.reclaim_scan_virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    }
    return true;
}

[[nodiscard]] bool ReadMemoryImage(void *const context, const uint64_t offset_bytes,
                                   uint8_t *const destination,
                                   const uint64_t length_bytes) noexcept {
    if (context == nullptr || destination == nullptr) {
        return false;
    }
    const MemoryImageReaderContext &reader_context =
        *static_cast<const MemoryImageReaderContext *>(context);
    if (offset_bytes > reader_context.image_size_bytes ||
        length_bytes > reader_context.image_size_bytes - offset_bytes) {
        return false;
    }
    CopyBytes(destination, reader_context.image + offset_bytes, length_bytes);
    return true;
}

[[nodiscard]] bool ReadVfsImage(void *const context, const uint64_t offset_bytes,
                                uint8_t *const destination, const uint64_t length_bytes) noexcept {
    if (context == nullptr || destination == nullptr) {
        return false;
    }
    VfsImageReaderContext &reader_context = *static_cast<VfsImageReaderContext *>(context);
    uint64_t read_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    return reader_context.vfs != nullptr &&
           reader_context.vfs->ReadAt(reader_context.open_file, offset_bytes, destination,
                                      length_bytes, read_bytes) == fs::Status::Succeeded &&
           read_bytes == length_bytes;
}

[[nodiscard]] uint8_t *AccessPhysicalPage(void *const context,
                                          const uint64_t physical_address) noexcept {
    static_cast<void>(context);
    return PhysicalPagePointer(physical_address);
}

[[nodiscard]] bool FileIdentitiesEqual(const FileIdentity &left,
                                       const FileIdentity &right) noexcept {
    return FileCacheIdentitiesEqual(left, right);
}

[[nodiscard]] FileIdentity FileIdentityFromVnode(const fs::Vnode &vnode) noexcept {
    return FileIdentity{
        .superblock_identifier = vnode.superblock->identifier,
        .superblock_generation = vnode.superblock->generation,
        .node_identifier = vnode.identifier,
        .node_generation = vnode.generation,
    };
}

[[nodiscard]] bool ReadVfsFilePage(void *const context, const FilePageIdentity &identity,
                                   uint8_t *const destination,
                                   const uint64_t capacity_bytes) noexcept {
    if (context == nullptr || destination == nullptr ||
        capacity_bytes != OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        identity.page_index > UINT64_MAX / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    VfsFilePageReaderContext &reader = *static_cast<VfsFilePageReaderContext *>(context);
    if (reader.vfs == nullptr || reader.open_file == nullptr || !reader.open_file->open ||
        (!reader.open_file->readable && !reader.open_file->writable)) {
        return false;
    }
    const FileIdentity open_identity = FileIdentityFromVnode(reader.open_file->path.vnode);
    const uint64_t offset_bytes = identity.page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    if (!FileCacheIdentitiesEqual(identity.file, open_identity)) {
        return false;
    }
    if (offset_bytes >= reader.source_size_bytes) {
        return true;
    }
    const uint64_t read_capacity = Minimum(capacity_bytes, reader.source_size_bytes - offset_bytes);
    uint64_t read_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    return reader.vfs->ReadUncachedAt(*reader.open_file, offset_bytes, destination, read_capacity,
                                      read_bytes) == fs::Status::Succeeded &&
           read_bytes == read_capacity;
}

[[nodiscard]] fs::Status
ReadVfsFileThroughCache(void *const context, const fs::OpenFile &open_file,
                        const uint64_t offset_bytes, uint8_t *const destination,
                        const uint64_t capacity_bytes, uint64_t &read_bytes,
                        fs::RegularFileReadCacheObservation &observation) noexcept {
    read_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    observation = fs::RegularFileReadCacheObservation{};
    if (context == nullptr || (!open_file.open || !open_file.readable) ||
        (destination == nullptr && capacity_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE)) {
        return fs::Status::InvalidArgument;
    }
    fs::Vfs &vfs = *static_cast<fs::Vfs *>(context);
    fs::NodeInformation information{};
    fs::NodeInformation source_information{};
    if (vfs.StatOpenFile(open_file, information) != fs::Status::Succeeded ||
        vfs.StatOpenFileUncached(open_file, source_information) != fs::Status::Succeeded ||
        information.type != fs::NodeType::RegularFile) {
        return fs::Status::InvalidHandle;
    }
    if (capacity_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        offset_bytes >= information.size_bytes) {
        observation.cache_used = true;
        return fs::Status::Succeeded;
    }
    const FileIdentity identity = FileIdentityFromVnode(open_file.path.vnode);
    const uint64_t requested_read_bytes =
        Minimum(capacity_bytes, information.size_bytes - offset_bytes);
    uint64_t cache_hit_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t cache_miss_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t prefetched_hit_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const auto publish_observation = [&]() noexcept {
        const uint64_t file_page_count =
            information.size_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES +
            (information.size_bytes % OS_KERNEL_MEMORY_PAGE_SIZE_BYTES !=
                     OS_KERNEL_USER_MEMORY_EMPTY_VALUE
                 ? OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT
                 : OS_KERNEL_USER_MEMORY_EMPTY_VALUE);
        uint64_t requested_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        if (read_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
            const uint64_t first_page_offset_bytes = offset_bytes & OS_KERNEL_USER_MEMORY_PAGE_MASK;
            const uint64_t tail_bytes =
                first_page_offset_bytes + read_bytes % OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            requested_page_count =
                read_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES +
                tail_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES +
                (tail_bytes % OS_KERNEL_MEMORY_PAGE_SIZE_BYTES != OS_KERNEL_USER_MEMORY_EMPTY_VALUE
                     ? OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT
                     : OS_KERNEL_USER_MEMORY_EMPTY_VALUE);
        }
        observation = fs::RegularFileReadCacheObservation{
            .first_page_index = offset_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            .requested_page_count = requested_page_count,
            .file_page_count = file_page_count,
            .cache_hit_page_count = cache_hit_page_count,
            .cache_miss_page_count = cache_miss_page_count,
            .prefetched_hit_page_count = prefetched_hit_page_count,
            .cache_used = true,
        };
    };
    if (user_file_page_cache_buffered_read_operation_count == UINT64_MAX) {
        return fs::Status::Corrupt;
    }
    ++user_file_page_cache_buffered_read_operation_count;
    VfsFilePageReaderContext reader{
        .vfs = &vfs,
        .open_file = &open_file,
        .source_size_bytes = source_information.size_bytes,
    };
    while (read_bytes < requested_read_bytes) {
        if (offset_bytes > UINT64_MAX - read_bytes) {
            return fs::Status::Corrupt;
        }
        const uint64_t current_offset_bytes = offset_bytes + read_bytes;
        const FilePageIdentity page_identity{
            .file = identity,
            .page_index = current_offset_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
        };
        FilePageCacheEntry existing_entry{};
        if (user_file_page_cache.ReadEntry(page_identity, existing_entry) ==
                FilePageCacheStatus::MappingNotFound &&
            user_file_page_cache.Statistics().resident_page_count <
                user_file_page_cache.Statistics().capacity &&
            active_user_address_space != nullptr &&
            PrepareUserResidentAllocation(*active_user_address_space,
                                          OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT,
                                          OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                                          false) != UserResidentAllocationStatus::Succeeded) {
            publish_observation();
            return read_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ? fs::Status::CapacityExhausted
                                                                   : fs::Status::Succeeded;
        }
        uint64_t physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        bool cache_hit = false;
        bool prefetched_hit = false;
        const FilePageCacheStatus acquire_status = user_file_page_cache.Acquire(
            page_identity, &reader, ReadVfsFilePage, FilePageAcquireIntent::Demand,
            FileReadaheadPageTag{}, physical_address, cache_hit, prefetched_hit);
        if (acquire_status != FilePageCacheStatus::Succeeded) {
            if (read_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                publish_observation();
                return fs::Status::Succeeded;
            }
            return acquire_status == FilePageCacheStatus::SourceReadFailed
                       ? fs::Status::DeviceFailure
                   : acquire_status == FilePageCacheStatus::CapacityExhausted ||
                           acquire_status == FilePageCacheStatus::FrameAllocationFailed ||
                           acquire_status == FilePageCacheStatus::MetadataAllocationFailed
                       ? fs::Status::CapacityExhausted
                       : fs::Status::Corrupt;
        }
        if (user_file_page_cache.ObserveFileSize(identity, information.size_bytes) !=
            FilePageCacheStatus::Succeeded) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            return fs::Status::Corrupt;
        }
        const uint8_t *const page = PhysicalPagePointer(physical_address);
        const uint64_t page_offset_bytes = current_offset_bytes & OS_KERNEL_USER_MEMORY_PAGE_MASK;
        const uint64_t chunk_bytes = Minimum(OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - page_offset_bytes,
                                             requested_read_bytes - read_bytes);
        if (page == nullptr) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            return fs::Status::Corrupt;
        }
        CopyBytes(destination + read_bytes, page + page_offset_bytes, chunk_bytes);
        if (user_file_page_cache.Release(page_identity, physical_address) !=
            FilePageCacheStatus::Succeeded) {
            return fs::Status::Corrupt;
        }
        if (user_file_page_cache_buffered_read_page_count == UINT64_MAX ||
            (cache_hit && user_file_page_cache_buffered_read_cache_hit_count == UINT64_MAX) ||
            user_file_page_cache_buffered_read_bytes > UINT64_MAX - chunk_bytes) {
            return fs::Status::Corrupt;
        }
        ++user_file_page_cache_buffered_read_page_count;
        if (cache_hit) {
            ++user_file_page_cache_buffered_read_cache_hit_count;
            ++cache_hit_page_count;
        } else {
            ++cache_miss_page_count;
        }
        if (prefetched_hit) {
            ++prefetched_hit_page_count;
        }
        user_file_page_cache_buffered_read_bytes += chunk_bytes;
        read_bytes += chunk_bytes;
    }
    publish_observation();
    return fs::Status::Succeeded;
}

[[nodiscard]] fs::Status
WriteVfsFileThroughCache(void *const context, const fs::OpenFile &open_file,
                         const uint64_t offset_bytes, const uint8_t *const source,
                         const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (context == nullptr || !open_file.open || !open_file.writable ||
        (source == nullptr && length_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE)) {
        return fs::Status::InvalidArgument;
    }
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return fs::Status::Succeeded;
    }
    if (offset_bytes > UINT64_MAX - length_bytes) {
        return fs::Status::FileTooLarge;
    }
    fs::Vfs &vfs = *static_cast<fs::Vfs *>(context);
    fs::NodeInformation information{};
    fs::NodeInformation source_information{};
    if (vfs.StatOpenFile(open_file, information) != fs::Status::Succeeded ||
        vfs.StatOpenFileUncached(open_file, source_information) != fs::Status::Succeeded ||
        information.type != fs::NodeType::RegularFile ||
        user_file_backing_manager.RetainWritebackFile(vfs, open_file, information.size_bytes) !=
            UserFileBackingStatus::Succeeded ||
        user_file_page_cache_buffered_write_operation_count == UINT64_MAX) {
        return fs::Status::CapacityExhausted;
    }
    ++user_file_page_cache_buffered_write_operation_count;
    const FileIdentity identity = FileIdentityFromVnode(open_file.path.vnode);
    VfsFilePageReaderContext reader{
        .vfs = &vfs,
        .open_file = &open_file,
        .source_size_bytes = source_information.size_bytes,
    };
    while (written_bytes < length_bytes) {
        const uint64_t current_offset_bytes = offset_bytes + written_bytes;
        const FilePageIdentity page_identity{
            .file = identity,
            .page_index = current_offset_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
        };
        FilePageCacheEntry existing_entry{};
        if (user_file_page_cache.ReadEntry(page_identity, existing_entry) ==
                FilePageCacheStatus::MappingNotFound &&
            user_file_page_cache.Statistics().resident_page_count <
                user_file_page_cache.Statistics().capacity &&
            active_user_address_space != nullptr &&
            PrepareUserResidentAllocation(*active_user_address_space,
                                          OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT,
                                          OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                                          false) != UserResidentAllocationStatus::Succeeded) {
            return written_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE
                       ? fs::Status::CapacityExhausted
                       : fs::Status::Succeeded;
        }
        uint64_t physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        bool cache_hit = false;
        const FilePageCacheStatus acquire_status = user_file_page_cache.Acquire(
            page_identity, &reader, ReadVfsFilePage, physical_address, cache_hit);
        if (acquire_status != FilePageCacheStatus::Succeeded) {
            if (written_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                return fs::Status::Succeeded;
            }
            return acquire_status == FilePageCacheStatus::SourceReadFailed
                       ? fs::Status::DeviceFailure
                   : acquire_status == FilePageCacheStatus::CapacityExhausted ||
                           acquire_status == FilePageCacheStatus::FrameAllocationFailed ||
                           acquire_status == FilePageCacheStatus::MetadataAllocationFailed
                       ? fs::Status::CapacityExhausted
                       : fs::Status::Corrupt;
        }
        if (user_file_page_cache.ObserveFileSize(identity, information.size_bytes) !=
            FilePageCacheStatus::Succeeded) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            return fs::Status::Corrupt;
        }
        const FilePageCacheStatus dirty_status =
            user_file_page_cache.MarkDirty(page_identity, physical_address);
        if (dirty_status != FilePageCacheStatus::Succeeded) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            if (written_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                return fs::Status::Succeeded;
            }
            return dirty_status == FilePageCacheStatus::DirtyLimitReached
                       ? fs::Status::CapacityExhausted
                   : dirty_status == FilePageCacheStatus::SourceWriteFailed ||
                           dirty_status == FilePageCacheStatus::FrameAccessFailed
                       ? fs::Status::DeviceFailure
                       : fs::Status::Corrupt;
        }
        uint8_t *const page = PhysicalPagePointer(physical_address);
        const uint64_t page_offset_bytes = current_offset_bytes & OS_KERNEL_USER_MEMORY_PAGE_MASK;
        const uint64_t chunk_bytes = Minimum(OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - page_offset_bytes,
                                             length_bytes - written_bytes);
        if (page == nullptr) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            return fs::Status::Corrupt;
        }
        CopyBytes(page + page_offset_bytes, source + written_bytes, chunk_bytes);
        if (user_file_page_cache.Release(page_identity, physical_address) !=
            FilePageCacheStatus::Succeeded) {
            return fs::Status::Corrupt;
        }
        written_bytes += chunk_bytes;
        const uint64_t write_end_bytes = offset_bytes + written_bytes;
        const uint64_t size_bytes =
            information.size_bytes < write_end_bytes ? write_end_bytes : information.size_bytes;
        if (user_file_page_cache.UpdateFileSize(identity, size_bytes) !=
                FilePageCacheStatus::Succeeded ||
            user_file_backing_manager.UpdateFileSize(identity, size_bytes) !=
                UserFileBackingStatus::Succeeded ||
            user_file_page_cache_buffered_write_page_count == UINT64_MAX ||
            (cache_hit && user_file_page_cache_buffered_write_cache_hit_count == UINT64_MAX) ||
            user_file_page_cache_buffered_write_bytes > UINT64_MAX - chunk_bytes) {
            return fs::Status::Corrupt;
        }
        ++user_file_page_cache_buffered_write_page_count;
        if (cache_hit) {
            ++user_file_page_cache_buffered_write_cache_hit_count;
        }
        user_file_page_cache_buffered_write_bytes += chunk_bytes;
    }
    return fs::Status::Succeeded;
}

[[nodiscard]] fs::Status ResolveVfsFileSizeFromCache(void *const context, const fs::Vnode &vnode,
                                                     const uint64_t backend_size_bytes,
                                                     uint64_t &size_bytes) noexcept {
    if (context == nullptr || vnode.superblock == nullptr ||
        vnode.type != fs::NodeType::RegularFile) {
        return fs::Status::InvalidArgument;
    }
    return user_file_page_cache.ResolveFileSize(FileIdentityFromVnode(vnode), backend_size_bytes,
                                                size_bytes) == FilePageCacheStatus::Succeeded
               ? fs::Status::Succeeded
               : fs::Status::Corrupt;
}

[[nodiscard]] fs::Status TruncateVfsFileCache(void *const context, const fs::Vnode &vnode,
                                              const uint64_t size_bytes) noexcept {
    if (context == nullptr || vnode.superblock == nullptr ||
        vnode.type != fs::NodeType::RegularFile) {
        return fs::Status::InvalidArgument;
    }
    const FileIdentity identity = FileIdentityFromVnode(vnode);
    const FilePageCacheStatus cache_status = user_file_page_cache.Truncate(identity, size_bytes);
    if (cache_status != FilePageCacheStatus::Succeeded) {
        return cache_status == FilePageCacheStatus::EntryBusy ? fs::Status::Busy
                                                              : fs::Status::Corrupt;
    }
    if (user_file_backing_manager.UpdateFileSize(identity, size_bytes) !=
            UserFileBackingStatus::Succeeded ||
        user_file_page_cache_truncate_operation_count == UINT64_MAX) {
        return fs::Status::Corrupt;
    }
    ++user_file_page_cache_truncate_operation_count;
    return fs::Status::Succeeded;
}

[[nodiscard]] bool FileWritebackIsRequired(void *const context, const FileIdentity &identity,
                                           bool &writeback_required) noexcept {
    writeback_required = false;
    if (context == nullptr) {
        return false;
    }
    const FilePageCache &cache = *static_cast<const FilePageCache *>(context);
    FileCacheAddressSpaceStatistics statistics{};
    const FilePageCacheStatus status = cache.ReadAddressSpaceStatistics(identity, statistics);
    if (status == FilePageCacheStatus::MappingNotFound) {
        return true;
    }
    if (status != FilePageCacheStatus::Succeeded) {
        return false;
    }
    writeback_required = statistics.dirty_page_count != OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
                         statistics.writeback_page_count != OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
                         statistics.error_page_count != OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    return true;
}

[[nodiscard]] bool WriteTrackedUserFileBackingPage(void *const context,
                                                   const FilePageIdentity &identity,
                                                   const uint8_t *const source,
                                                   const uint64_t length_bytes) noexcept {
    if (context == nullptr) {
        return false;
    }
    UserFileBackingManager &manager = *static_cast<UserFileBackingManager *>(context);
    if (manager.WritePage(identity, source, length_bytes) == UserFileBackingStatus::Succeeded) {
        return true;
    }
    static_cast<void>(
        user_file_writeback_error_tracker.Record(identity.file, FileWritebackError::InputOutput));
    return false;
}

[[nodiscard]] bool ReadBackingDescriptor(const VirtualMemoryArea &area,
                                         UserFileBackingDescriptor &descriptor) noexcept {
    return IsFileBackedVirtualMemoryAreaKind(area.kind) &&
           user_file_backing_manager.ReadDescriptor(area.backing_descriptor_index,
                                                    area.backing_generation,
                                                    descriptor) == UserFileBackingStatus::Succeeded;
}

[[nodiscard]] bool PageUsesFileCache(const VirtualMemoryArea &area,
                                     const uint64_t page_virtual_address,
                                     const UserFileBackingDescriptor &descriptor,
                                     FilePageIdentity &page_identity) noexcept {
    if (!IsFileBackedVirtualMemoryAreaKind(area.kind) ||
        (area.permissions.writable && area.kind != VirtualMemoryAreaKind::FileShared) ||
        page_virtual_address < area.begin_address) {
        return false;
    }
    const uint64_t area_offset_bytes = page_virtual_address - area.begin_address;
    if (area_offset_bytes >= area.backing_data_length_bytes ||
        area.backing_data_length_bytes - area_offset_bytes < OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        area.backing_file_offset_bytes > UINT64_MAX - area_offset_bytes) {
        return false;
    }
    const uint64_t file_offset_bytes = area.backing_file_offset_bytes + area_offset_bytes;
    if ((file_offset_bytes & OS_KERNEL_USER_MEMORY_PAGE_MASK) !=
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        file_offset_bytes >= descriptor.size_bytes) {
        return false;
    }
    page_identity = FilePageIdentity{
        .file = descriptor.identity,
        .page_index = file_offset_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
    };
    return true;
}

void RecordMappedPage(UserAddressSpace &address_space, const VirtualMemoryArea &area,
                      const bool cache_backed, const bool cache_hit) noexcept {
    ++address_space.mapped_page_count;
    if (address_space.mapped_page_count > address_space.peak_mapped_page_count) {
        address_space.peak_mapped_page_count = address_space.mapped_page_count;
    }
    if (!IsFileBackedVirtualMemoryAreaKind(area.kind)) {
        return;
    }
    ++address_space.file_page_fault_count;
    if (cache_hit) {
        ++address_space.page_cache_hit_count;
    }
    if (cache_backed) {
        ++address_space.shared_file_resident_page_count;
    } else {
        ++address_space.private_file_resident_page_count;
    }
}

[[nodiscard]] UserVirtualMemoryStatus MapFileDemandPage(UserAddressSpace &address_space,
                                                        const uint64_t page_virtual_address,
                                                        const VirtualMemoryArea &area) noexcept {
    UserFileBackingDescriptor descriptor{};
    if (!ReadBackingDescriptor(area, descriptor)) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    const uint64_t area_offset_bytes = page_virtual_address - area.begin_address;
    const uint64_t remaining_data_bytes = area_offset_bytes < area.backing_data_length_bytes
                                              ? area.backing_data_length_bytes - area_offset_bytes
                                              : OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t read_length_bytes = Minimum(remaining_data_bytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
    if (area.backing_file_offset_bytes > UINT64_MAX - area_offset_bytes) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    const uint64_t file_offset_bytes = area.backing_file_offset_bytes + area_offset_bytes;
    if (read_length_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        if (file_offset_bytes >= descriptor.size_bytes) {
            return UserVirtualMemoryStatus::FileReadFailed;
        }
        read_length_bytes = Minimum(read_length_bytes, descriptor.size_bytes - file_offset_bytes);
    }

    FilePageIdentity page_identity{};
    if (PageUsesFileCache(area, page_virtual_address, descriptor, page_identity)) {
        if (PrepareUserResidentAllocation(
                address_space, OS_KERNEL_USER_MEMORY_MAXIMUM_ALLOCATION_FRAME_COUNT,
                page_virtual_address, true) != UserResidentAllocationStatus::Succeeded) {
            return UserVirtualMemoryStatus::PageAllocationFailed;
        }
        uint64_t physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        bool cache_hit = false;
        const FilePageCacheStatus cache_status = user_file_page_cache.Acquire(
            page_identity, &descriptor, ReadUserFileBackingPage, physical_address, cache_hit);
        if (cache_status == FilePageCacheStatus::CapacityExhausted) {
            return UserVirtualMemoryStatus::PageCacheExhausted;
        }
        if (cache_status == FilePageCacheStatus::FrameAllocationFailed) {
            return UserVirtualMemoryStatus::PageAllocationFailed;
        }
        if (cache_status != FilePageCacheStatus::Succeeded) {
            return cache_status == FilePageCacheStatus::SourceReadFailed
                       ? UserVirtualMemoryStatus::FileReadFailed
                       : UserVirtualMemoryStatus::PageMappingFailed;
        }
        if (user_file_page_cache.ObserveFileSize(page_identity.file, descriptor.size_bytes) !=
            FilePageCacheStatus::Succeeded) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            return UserVirtualMemoryStatus::PageMappingFailed;
        }
        const bool map_writable =
            area.permissions.writable && area.kind != VirtualMemoryAreaKind::FileShared;
        if (MapExistingUserPage(address_space.root_physical_address, page_virtual_address,
                                physical_address, map_writable,
                                area.permissions.executable) != KernelUserPageStatus::Succeeded) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            return UserVirtualMemoryStatus::PageMappingFailed;
        }
        RecordMappedPage(address_space, area, true, cache_hit);
        return UserVirtualMemoryStatus::Succeeded;
    }

    uint64_t physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (PrepareUserResidentAllocation(
            address_space, OS_KERNEL_USER_MEMORY_MAXIMUM_ALLOCATION_FRAME_COUNT,
            page_virtual_address, true) != UserResidentAllocationStatus::Succeeded) {
        return UserVirtualMemoryStatus::PageAllocationFailed;
    }
    const KernelUserPageStatus page_status = AllocateAndMapUserPage(
        address_space.root_physical_address, page_virtual_address, area.permissions.writable,
        area.permissions.executable, physical_address);
    if (page_status == KernelUserPageStatus::FrameAllocationFailed) {
        return UserVirtualMemoryStatus::PageAllocationFailed;
    }
    if (page_status != KernelUserPageStatus::Succeeded || !ZeroPhysicalPage(physical_address)) {
        if (page_status == KernelUserPageStatus::Succeeded) {
            uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            static_cast<void>(ReleaseUserPage(address_space.root_physical_address,
                                              page_virtual_address, reclaimed_table_frame_count));
        }
        return UserVirtualMemoryStatus::PageMappingFailed;
    }
    if (read_length_bytes != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        uint8_t *const page = PhysicalPagePointer(physical_address);
        if (page == nullptr ||
            user_file_backing_manager.Read(area.backing_descriptor_index, area.backing_generation,
                                           file_offset_bytes, page,
                                           read_length_bytes) != UserFileBackingStatus::Succeeded) {
            uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            static_cast<void>(ReleaseUserPage(address_space.root_physical_address,
                                              page_virtual_address, reclaimed_table_frame_count));
            return UserVirtualMemoryStatus::FileReadFailed;
        }
    }
    RecordMappedPage(address_space, area, false, false);
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] bool PermissionsAllow(const VirtualMemoryArea &area, const bool require_writable,
                                    const bool require_executable) noexcept {
    if (!area.permissions.readable) {
        return false;
    }
    if (require_writable && !area.permissions.writable) {
        return false;
    }
    if (require_executable && !area.permissions.executable) {
        return false;
    }
    return true;
}

[[nodiscard]] UserVirtualMemoryStatus ResolveSwappedPage(UserAddressSpace &address_space,
                                                         const uint64_t page_virtual_address,
                                                         const VirtualMemoryArea &area,
                                                         bool &swap_mapping_found) noexcept {
    swap_mapping_found = false;
    if (!user_swap_attached) {
        return UserVirtualMemoryStatus::Succeeded;
    }
    const SwapPageIdentity identity = MakeSwapIdentity(address_space, page_virtual_address);
    uint64_t slot_index = UINT64_MAX;
    const SwapManagerStatus find_status = user_swap_manager.FindSlot(identity, slot_index);
    static_cast<void>(slot_index);
    if (find_status == SwapManagerStatus::MappingNotFound) {
        return UserVirtualMemoryStatus::Succeeded;
    }
    if (find_status != SwapManagerStatus::Succeeded ||
        address_space.swapped_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    swap_mapping_found = true;
    if (PrepareUserResidentAllocation(
            address_space, OS_KERNEL_USER_MEMORY_MAXIMUM_ALLOCATION_FRAME_COUNT,
            page_virtual_address, true) != UserResidentAllocationStatus::Succeeded) {
        return UserVirtualMemoryStatus::PageAllocationFailed;
    }
    uint64_t physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const KernelUserPageStatus allocation_status = AllocateAndMapUserPage(
        address_space.root_physical_address, page_virtual_address, area.permissions.writable,
        area.permissions.executable, physical_address);
    if (allocation_status == KernelUserPageStatus::FrameAllocationFailed) {
        return UserVirtualMemoryStatus::PageAllocationFailed;
    }
    if (allocation_status != KernelUserPageStatus::Succeeded) {
        return UserVirtualMemoryStatus::PageMappingFailed;
    }
    uint8_t *const page = PhysicalPagePointer(physical_address);
    const SwapManagerStatus load_status =
        page == nullptr
            ? SwapManagerStatus::InvalidStorage
            : user_swap_manager.LoadAndRelease(identity, page, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
    if (load_status != SwapManagerStatus::Succeeded) {
        uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        if (ReleaseUserPage(address_space.root_physical_address, page_virtual_address,
                            reclaimed_table_frame_count) != KernelUserPageStatus::Succeeded) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        ++address_space.swap_read_failure_count;
        if (load_status == SwapManagerStatus::ChecksumMismatch) {
            ++address_space.swap_corruption_count;
            return UserVirtualMemoryStatus::SwapCorrupt;
        }
        return UserVirtualMemoryStatus::SwapReadFailed;
    }
    --address_space.swapped_page_count;
    ++address_space.swap_in_page_count;
    RecordMappedPage(address_space, area, false, false);
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus MapDemandPage(UserAddressSpace &address_space,
                                                    const uint64_t page_virtual_address,
                                                    const VirtualMemoryArea &area) noexcept {
    bool swap_mapping_found = false;
    const UserVirtualMemoryStatus swap_status =
        ResolveSwappedPage(address_space, page_virtual_address, area, swap_mapping_found);
    if (swap_mapping_found || swap_status != UserVirtualMemoryStatus::Succeeded) {
        return swap_status;
    }
    if (IsFileBackedVirtualMemoryAreaKind(area.kind)) {
        return MapFileDemandPage(address_space, page_virtual_address, area);
    }
    if (PrepareUserResidentAllocation(
            address_space, OS_KERNEL_USER_MEMORY_MAXIMUM_ALLOCATION_FRAME_COUNT,
            page_virtual_address, true) != UserResidentAllocationStatus::Succeeded) {
        return UserVirtualMemoryStatus::PageAllocationFailed;
    }
    uint64_t page_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const KernelUserPageStatus page_status = AllocateAndMapUserPage(
        address_space.root_physical_address, page_virtual_address, area.permissions.writable,
        area.permissions.executable, page_physical_address);
    if (page_status == KernelUserPageStatus::FrameAllocationFailed) {
        return UserVirtualMemoryStatus::PageAllocationFailed;
    }
    if (page_status != KernelUserPageStatus::Succeeded) {
        return UserVirtualMemoryStatus::PageMappingFailed;
    }
    if (!ZeroPhysicalPage(page_physical_address)) {
        uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        return ReleaseUserPage(address_space.root_physical_address, page_virtual_address,
                               reclaimed_table_frame_count) == KernelUserPageStatus::Succeeded
                   ? UserVirtualMemoryStatus::PageMappingFailed
                   : UserVirtualMemoryStatus::Corrupt;
    }
    RecordMappedPage(address_space, area, false, false);
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus ReleaseMappedPages(UserAddressSpace &address_space,
                                                         const uint64_t begin_address,
                                                         const uint64_t end_address,
                                                         const bool count_as_unmap) noexcept {
    for (uint64_t page_address = begin_address; page_address < end_address;
         page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        PageMapping mapping{};
        const PageTableStatus query_status =
            QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping);
        if (query_status == PageTableStatus::NotMapped) {
            if (user_swap_attached &&
                address_space.address_space_identifier != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                const SwapPageIdentity swap_identity =
                    MakeSwapIdentity(address_space, page_address);
                uint64_t swap_slot_index = UINT64_MAX;
                const SwapManagerStatus swap_status =
                    user_swap_manager.FindSlot(swap_identity, swap_slot_index);
                static_cast<void>(swap_slot_index);
                if (swap_status == SwapManagerStatus::Succeeded) {
                    if (address_space.swapped_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
                        user_swap_manager.Release(swap_identity) != SwapManagerStatus::Succeeded) {
                        return UserVirtualMemoryStatus::PageReleaseFailed;
                    }
                    --address_space.swapped_page_count;
                } else if (swap_status != SwapManagerStatus::MappingNotFound) {
                    return UserVirtualMemoryStatus::Corrupt;
                }
            }
            continue;
        }
        if (query_status != PageTableStatus::Succeeded || !mapping.permissions.user_accessible ||
            address_space.mapped_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
            user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                .stage = UserAddressSpaceDestructionStage::QueryPage,
                .virtual_address = page_address,
                .physical_address = mapping.physical_address,
                .status = static_cast<uint64_t>(query_status),
            };
            return UserVirtualMemoryStatus::Corrupt;
        }
        uint64_t reclaimed_table_frame_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        VirtualMemoryArea area{};
        const VirtualMemoryAreaStatus area_status =
            address_space.virtual_memory_map.FindContaining(page_address, area);
        bool cache_mapping = false;
        FilePageIdentity page_identity{};
        if (area_status == VirtualMemoryAreaStatus::Succeeded &&
            IsFileBackedVirtualMemoryAreaKind(area.kind)) {
            UserFileBackingDescriptor descriptor{};
            if (!ReadBackingDescriptor(area, descriptor)) {
                user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                    .stage = UserAddressSpaceDestructionStage::BackingDescriptor,
                    .virtual_address = page_address,
                    .physical_address = mapping.physical_address,
                    .status = static_cast<uint64_t>(area_status),
                };
                return UserVirtualMemoryStatus::Corrupt;
            }
            cache_mapping = PageUsesFileCache(area, page_address, descriptor, page_identity);
        }
        if (cache_mapping) {
            uint64_t unmapped_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            const KernelUserPageStatus unmap_status =
                UnmapUserPage(address_space.root_physical_address, page_address,
                              unmapped_physical_address, reclaimed_table_frame_count);
            const FilePageCacheStatus release_status =
                unmap_status == KernelUserPageStatus::Succeeded &&
                        unmapped_physical_address == mapping.physical_address
                    ? user_file_page_cache.Release(page_identity, unmapped_physical_address)
                    : FilePageCacheStatus::Corrupt;
            if (unmap_status != KernelUserPageStatus::Succeeded ||
                unmapped_physical_address != mapping.physical_address ||
                release_status != FilePageCacheStatus::Succeeded) {
                user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                    .stage = UserAddressSpaceDestructionStage::FileCacheRelease,
                    .virtual_address = page_address,
                    .physical_address = mapping.physical_address,
                    .status = unmap_status != KernelUserPageStatus::Succeeded
                                  ? static_cast<uint64_t>(unmap_status)
                                  : static_cast<uint64_t>(release_status),
                };
                return UserVirtualMemoryStatus::PageReleaseFailed;
            }
            if (address_space.shared_file_resident_page_count ==
                OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            --address_space.shared_file_resident_page_count;
        } else {
            uint64_t unmapped_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            const KernelUserPageStatus unmap_status =
                UnmapUserPage(address_space.root_physical_address, page_address,
                              unmapped_physical_address, reclaimed_table_frame_count);
            const bool release_succeeded = unmap_status == KernelUserPageStatus::Succeeded &&
                                           unmapped_physical_address == mapping.physical_address &&
                                           ReleasePrivatePhysicalPage(unmapped_physical_address);
            if (unmap_status != KernelUserPageStatus::Succeeded ||
                unmapped_physical_address != mapping.physical_address || !release_succeeded) {
                user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                    .stage = UserAddressSpaceDestructionStage::PrivatePageRelease,
                    .virtual_address = page_address,
                    .physical_address = mapping.physical_address,
                    .status = static_cast<uint64_t>(unmap_status),
                };
                return UserVirtualMemoryStatus::PageReleaseFailed;
            }
            if (area_status == VirtualMemoryAreaStatus::Succeeded &&
                IsFileBackedVirtualMemoryAreaKind(area.kind)) {
                if (address_space.private_file_resident_page_count ==
                    OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                    return UserVirtualMemoryStatus::Corrupt;
                }
                --address_space.private_file_resident_page_count;
            }
        }
        if (mapping.permissions.copy_on_write) {
            if (address_space.copy_on_write_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            --address_space.copy_on_write_page_count;
        }
        --address_space.mapped_page_count;
        address_space.page_table_reclaimed_frame_count += reclaimed_table_frame_count;
        if (count_as_unmap) {
            ++address_space.unmap_released_page_count;
        }
    }
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus BreakCopyOnWritePage(UserAddressSpace &address_space,
                                                           const uint64_t page_address) noexcept {
    VirtualMemoryArea area{};
    PageMapping mapping{};
    if (address_space.virtual_memory_map.FindContaining(page_address, area) !=
            VirtualMemoryAreaStatus::Succeeded ||
        !area.permissions.writable ||
        QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping) !=
            PageTableStatus::Succeeded ||
        !mapping.permissions.user_accessible || mapping.permissions.writable ||
        !mapping.permissions.copy_on_write) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    uint64_t reference_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (user_page_reference_manager.ReadReferenceCount(mapping.physical_address, reference_count) !=
            UserPageReferenceStatus::Succeeded ||
        reference_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::Corrupt;
    }

    if (reference_count == OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) {
        if (ReplaceUserPage(address_space.root_physical_address, page_address,
                            mapping.physical_address, true, mapping.permissions.executable,
                            false) != KernelUserPageStatus::Succeeded ||
            user_page_reference_manager.RestoreExclusive(mapping.physical_address) !=
                UserPageReferenceStatus::Succeeded) {
            return UserVirtualMemoryStatus::CopyOnWriteFailure;
        }
        ++address_space.copy_on_write_exclusive_restore_count;
    } else {
        if (PrepareUserResidentAllocation(address_space, OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT,
                                          page_address,
                                          true) != UserResidentAllocationStatus::Succeeded) {
            return UserVirtualMemoryStatus::PageAllocationFailed;
        }
        PhysicalFrame replacement_frame{};
        if (GetKernelPhysicalFrameAllocator().Allocate(replacement_frame) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
            return UserVirtualMemoryStatus::PageAllocationFailed;
        }
        uint8_t *const source_page = PhysicalPagePointer(mapping.physical_address);
        uint8_t *const replacement_page = PhysicalPagePointer(replacement_frame.physical_address);
        if (source_page == nullptr || replacement_page == nullptr) {
            static_cast<void>(GetKernelPhysicalFrameAllocator().Release(replacement_frame));
            return UserVirtualMemoryStatus::CopyOnWriteFailure;
        }
        CopyBytes(replacement_page, source_page, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES);
        if (ReplaceUserPage(address_space.root_physical_address, page_address,
                            replacement_frame.physical_address, true,
                            mapping.permissions.executable,
                            false) != KernelUserPageStatus::Succeeded) {
            static_cast<void>(GetKernelPhysicalFrameAllocator().Release(replacement_frame));
            return UserVirtualMemoryStatus::CopyOnWriteFailure;
        }
        bool release_frame = false;
        if (user_page_reference_manager.Release(mapping.physical_address, release_frame) !=
                UserPageReferenceStatus::Succeeded ||
            release_frame) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        ++address_space.copy_on_write_copy_count;
    }
    if (address_space.copy_on_write_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    --address_space.copy_on_write_page_count;
    ++address_space.copy_on_write_fault_count;
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus
MakeSharedFilePageWritable(UserAddressSpace &address_space, const uint64_t page_address) noexcept {
    VirtualMemoryArea area{};
    PageMapping mapping{};
    if (address_space.virtual_memory_map.FindContaining(page_address, area) !=
            VirtualMemoryAreaStatus::Succeeded ||
        area.kind != VirtualMemoryAreaKind::FileShared || !area.permissions.writable ||
        QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping) !=
            PageTableStatus::Succeeded ||
        !mapping.permissions.user_accessible || mapping.permissions.writable ||
        mapping.permissions.copy_on_write) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    UserFileBackingDescriptor descriptor{};
    FilePageIdentity page_identity{};
    if (!ReadBackingDescriptor(area, descriptor) ||
        !PageUsesFileCache(area, page_address, descriptor, page_identity)) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    const FilePageCacheStatus dirty_status =
        user_file_page_cache.MarkDirty(page_identity, mapping.physical_address);
    if (dirty_status == FilePageCacheStatus::DirtyLimitReached) {
        return UserVirtualMemoryStatus::PageCacheExhausted;
    }
    if (dirty_status == FilePageCacheStatus::SourceWriteFailed ||
        dirty_status == FilePageCacheStatus::FrameAccessFailed) {
        return UserVirtualMemoryStatus::FileWriteFailed;
    }
    if (dirty_status != FilePageCacheStatus::Succeeded ||
        ReplaceUserPage(address_space.root_physical_address, page_address, mapping.physical_address,
                        true, mapping.permissions.executable,
                        false) != KernelUserPageStatus::Succeeded) {
        return UserVirtualMemoryStatus::PageMappingFailed;
    }
    return UserVirtualMemoryStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus ResolveNonStackPage(UserAddressSpace &address_space,
                                                          const uint64_t page_address,
                                                          const bool require_writable,
                                                          const bool require_executable) noexcept {
    PageMapping mapping{};
    const PageTableStatus query_status =
        QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping);
    if (query_status == PageTableStatus::Succeeded) {
        if (!mapping.permissions.user_accessible) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (require_writable && !mapping.permissions.writable) {
            if (mapping.permissions.copy_on_write) {
                return BreakCopyOnWritePage(address_space, page_address);
            }
            return MakeSharedFilePageWritable(address_space, page_address);
        }
        if (require_executable && !mapping.permissions.executable) {
            return UserVirtualMemoryStatus::InvalidProtection;
        }
        return UserVirtualMemoryStatus::Succeeded;
    }
    if (query_status != PageTableStatus::NotMapped) {
        return UserVirtualMemoryStatus::Corrupt;
    }

    VirtualMemoryArea area{};
    if (address_space.virtual_memory_map.FindContaining(page_address, area) !=
        VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    if (area.kind == VirtualMemoryAreaKind::UserStack ||
        !PermissionsAllow(area, require_writable, require_executable)) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    const UserVirtualMemoryStatus map_status = MapDemandPage(address_space, page_address, area);
    if (map_status == UserVirtualMemoryStatus::Succeeded) {
        ++address_space.demand_page_fault_count;
    }
    return map_status;
}

[[nodiscard]] UserMemoryCopyStatus ValidateUserMemory(const uint64_t user_address,
                                                      const uint64_t length_bytes,
                                                      const bool require_writable) noexcept {
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserMemoryCopyStatus::Succeeded;
    }
    if (!IsUserVirtualAddressRange(user_address, length_bytes)) {
        return UserMemoryCopyStatus::InvalidUserRange;
    }

    const uint64_t inclusive_end_address =
        user_address + length_bytes - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
    uint64_t page_address = AlignDownToPage(user_address);
    const uint64_t final_page_address = AlignDownToPage(inclusive_end_address);
    while (true) {
        PageMapping mapping{};
        PageTableStatus query_status = QueryActivePage(page_address, mapping);
        if (query_status == PageTableStatus::NotMapped && active_user_address_space != nullptr) {
            if (ResolveNonStackPage(*active_user_address_space, page_address, require_writable,
                                    false) != UserVirtualMemoryStatus::Succeeded) {
                return UserMemoryCopyStatus::PageResolutionFailed;
            }
            query_status = QueryActivePage(page_address, mapping);
        }
        if (query_status != PageTableStatus::Succeeded) {
            return UserMemoryCopyStatus::PageNotMapped;
        }
        if (!mapping.permissions.user_accessible) {
            return UserMemoryCopyStatus::PageNotUserAccessible;
        }
        if (require_writable && !mapping.permissions.writable) {
            if (active_user_address_space == nullptr || !mapping.permissions.copy_on_write ||
                BreakCopyOnWritePage(*active_user_address_space, page_address) !=
                    UserVirtualMemoryStatus::Succeeded ||
                QueryActivePage(page_address, mapping) != PageTableStatus::Succeeded ||
                !mapping.permissions.writable) {
                return UserMemoryCopyStatus::PageNotWritable;
            }
        }
        if (page_address == final_page_address) {
            break;
        }
        page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    }
    return UserMemoryCopyStatus::Succeeded;
}

[[nodiscard]] bool ElfOverlapsStackReservation(const UserElfLayout &layout) noexcept {
    for (uint64_t segment_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segment_index < layout.load_segment_count; ++segment_index) {
        const UserElfLoadSegment &segment = layout.load_segments[segment_index];
        const uint64_t segment_end_address = segment.virtual_address + segment.memory_size_bytes;
        if (segment.virtual_address < OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS &&
            OS_KERNEL_USER_STACK_GUARD_VIRTUAL_ADDRESS < segment_end_address) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool CalculateProgramBreakBase(const UserElfLayout &layout,
                                             uint64_t &program_break_base) noexcept {
    uint64_t highest_segment_end = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    for (uint64_t segment_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segment_index < layout.load_segment_count; ++segment_index) {
        const UserElfLoadSegment &segment = layout.load_segments[segment_index];
        const uint64_t segment_end = segment.virtual_address + segment.memory_size_bytes;
        if (segment_end > highest_segment_end) {
            highest_segment_end = segment_end;
        }
    }
    return AlignUpToPage(highest_segment_end, program_break_base) &&
           program_break_base < os::abi::OS_ABI_USER_PROGRAM_BREAK_LIMIT_ADDRESS;
}

[[nodiscard]] bool InsertInitialAreas(const UserElfLayout &layout,
                                      const uint64_t backing_descriptor_index,
                                      const uint64_t backing_generation,
                                      UserAddressSpace &address_space) noexcept {
    for (uint64_t segment_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         segment_index < layout.load_segment_count; ++segment_index) {
        const UserElfLoadSegment &segment = layout.load_segments[segment_index];
        uint64_t segment_end_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        if (!AlignUpToPage(segment.virtual_address + segment.memory_size_bytes,
                           segment_end_address) ||
            address_space.virtual_memory_map.Insert(VirtualMemoryArea{
                .begin_address = segment.virtual_address,
                .end_address = segment_end_address,
                .permissions =
                    {
                        .readable = true,
                        .writable = segment.writable,
                        .executable = segment.executable,
                    },
                .kind = VirtualMemoryAreaKind::ExecutableImage,
                .backing_descriptor_index = backing_descriptor_index,
                .backing_generation = backing_generation,
                .backing_file_offset_bytes = segment.file_offset,
                .backing_data_length_bytes = segment.file_size_bytes,
            }) != VirtualMemoryAreaStatus::Succeeded) {
            return false;
        }
    }
    return address_space.virtual_memory_map.Insert(VirtualMemoryArea{
               .begin_address = OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS,
               .end_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS,
               .permissions =
                   {
                       .readable = true,
                       .writable = true,
                       .executable = false,
                   },
               .kind = VirtualMemoryAreaKind::UserStack,
           }) == VirtualMemoryAreaStatus::Succeeded;
}

[[nodiscard]] UserVirtualMemoryStatus
MapVirtualMemoryAreaStatus(const VirtualMemoryAreaStatus status) noexcept {
    if (status == VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::Succeeded;
    }
    if (status == VirtualMemoryAreaStatus::Overlap) {
        return UserVirtualMemoryStatus::AddressInUse;
    }
    if (status == VirtualMemoryAreaStatus::MetadataExhausted ||
        status == VirtualMemoryAreaStatus::AreaLimitExceeded) {
        return UserVirtualMemoryStatus::MetadataExhausted;
    }
    if (status == VirtualMemoryAreaStatus::NotMapped ||
        status == VirtualMemoryAreaStatus::KindMismatch ||
        status == VirtualMemoryAreaStatus::InvalidRange ||
        status == VirtualMemoryAreaStatus::InvalidAlignment) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    if (status == VirtualMemoryAreaStatus::NotInitialized) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    return UserVirtualMemoryStatus::Corrupt;
}

[[nodiscard]] bool ProtectionFlagsAreValid(const uint64_t protection_flags) noexcept {
    if ((protection_flags & ~os::abi::OS_ABI_MEMORY_PROTECTION_VALID_MASK) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return false;
    }
    const bool readable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_READ) !=
                          OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool writable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_WRITE) !=
                          OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool executable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_EXECUTE) !=
                            OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    return (!writable && !executable) || (readable && !(writable && executable));
}

[[nodiscard]] VirtualMemoryAreaPermissions
DecodeProtectionFlags(const uint64_t protection_flags) noexcept {
    return VirtualMemoryAreaPermissions{
        .readable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_READ) !=
                    OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
        .writable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_WRITE) !=
                    OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
        .executable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_EXECUTE) !=
                      OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
    };
}

[[nodiscard]] uint64_t CountKindPages(const UserAddressSpace &address_space,
                                      const VirtualMemoryAreaKind kind) noexcept {
    uint64_t page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         area_index < address_space.virtual_memory_map.AreaCount(); ++area_index) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        }
        if (area.kind == kind) {
            page_count +=
                (area.end_address - area.begin_address) / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        }
    }
    return page_count;
}

[[nodiscard]] bool BackingIsStillReferenced(const UserAddressSpace &address_space,
                                            const uint64_t descriptor_index,
                                            const uint64_t generation) noexcept {
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         area_index < address_space.virtual_memory_map.AreaCount(); ++area_index) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return true;
        }
        if (IsFileBackedVirtualMemoryAreaKind(area.kind) &&
            area.backing_descriptor_index == descriptor_index &&
            area.backing_generation == generation) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] UserVirtualMemoryStatus ReleaseBackingIfUnused(UserAddressSpace &address_space,
                                                             const uint64_t descriptor_index,
                                                             const uint64_t generation) noexcept {
    if (BackingIsStillReferenced(address_space, descriptor_index, generation)) {
        return UserVirtualMemoryStatus::Succeeded;
    }
    return user_file_backing_manager.Release(address_space.root_physical_address, descriptor_index,
                                             generation) == UserFileBackingStatus::Succeeded
               ? UserVirtualMemoryStatus::Succeeded
               : UserVirtualMemoryStatus::Corrupt;
}

[[nodiscard]] UserAddressSpaceStatus
LoadUserAddressSpaceInternal(const UserElfReader &reader, const uint8_t *const memory_image,
                             fs::Vfs *const vfs, const fs::OpenFile *const open_file,
                             UserAddressSpace &address_space,
                             UserElfValidationStatus &elf_validation_status) noexcept {
    address_space = UserAddressSpace{};
    if (InitializeUserVirtualMemory() != UserAddressSpaceStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    UserElfLayout layout{};
    elf_validation_status = ValidateUserElf(reader, layout);
    if (elf_validation_status != UserElfValidationStatus::Succeeded) {
        return UserAddressSpaceStatus::InvalidElf;
    }
    if (ElfOverlapsStackReservation(layout)) {
        return UserAddressSpaceStatus::StackCollision;
    }
    uint64_t program_break_base = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!CalculateProgramBreakBase(layout, program_break_base)) {
        return UserAddressSpaceStatus::ProgramBreakCollision;
    }
    if (CreateUserPageTable(address_space.root_physical_address) !=
        KernelUserPageStatus::Succeeded) {
        address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::PageTableCreationFailed;
    }

    uint64_t backing_descriptor_index = UINT64_MAX;
    uint64_t backing_generation = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const UserFileBackingStatus backing_status =
        memory_image != nullptr
            ? user_file_backing_manager.AcquireMemoryImage(
                  address_space.root_physical_address, memory_image, reader.image_size_bytes,
                  backing_descriptor_index, backing_generation)
        : vfs != nullptr && open_file != nullptr
            ? user_file_backing_manager.AcquireVfsFile(address_space.root_physical_address, *vfs,
                                                       *open_file, backing_descriptor_index,
                                                       backing_generation)
            : UserFileBackingStatus::InvalidSource;
    if (backing_status != UserFileBackingStatus::Succeeded) {
        static_cast<void>(DestroyUserPageTable(address_space.root_physical_address));
        address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::VirtualMemoryAreaFailure;
    }
    if (address_space.virtual_memory_map.Initialize(
            user_virtual_memory_pool, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            OS_KERNEL_USER_VMA_PER_PROCESS_HARD_LIMIT) != VirtualMemoryAreaStatus::Succeeded ||
        !InsertInitialAreas(layout, backing_descriptor_index, backing_generation, address_space)) {
        if (address_space.virtual_memory_map.Validate() == VirtualMemoryAreaStatus::Succeeded) {
            static_cast<void>(address_space.virtual_memory_map.Destroy());
        }
        static_cast<void>(user_file_backing_manager.Release(
            address_space.root_physical_address, backing_descriptor_index, backing_generation));
        static_cast<void>(DestroyUserPageTable(address_space.root_physical_address));
        address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::VirtualMemoryAreaFailure;
    }

    address_space.entry_virtual_address = layout.entry_virtual_address;
    address_space.stack_top_virtual_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS;
    address_space.stack_committed_bottom_virtual_address = OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS;
    address_space.program_break_base_address = program_break_base;
    address_space.program_break_address = program_break_base;
    address_space.program_break_limit_address = os::abi::OS_ABI_USER_PROGRAM_BREAK_LIMIT_ADDRESS;
    if (!AssignAddressSpaceIdentifier(address_space)) {
        static_cast<void>(DestroyUserAddressSpace(address_space));
        return UserAddressSpaceStatus::AddressSpaceIdentifierExhausted;
    }
    return UserAddressSpaceStatus::Succeeded;
}

}

UserAddressSpaceStatus InitializeUserVirtualMemory() noexcept {
    if (user_virtual_memory_initialized) {
        return UserAddressSpaceStatus::Succeeded;
    }
    if (user_virtual_memory_pool.Initialize(user_virtual_memory_descriptors,
                                            OS_KERNEL_USER_VMA_DESCRIPTOR_POOL_CAPACITY) !=
        VirtualMemoryAreaStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    if (user_file_backing_manager.Initialize(user_file_backing_descriptors,
                                             OS_KERNEL_USER_FILE_BACKING_CAPACITY) !=
        UserFileBackingStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    const PhysicalFrameAllocatorStatistics frame_statistics = GetPhysicalFrameAllocatorStatistics();
    const uint64_t resident_limit_page_count = Minimum(
        frame_statistics.managed_frame_count, OS_KERNEL_USER_MEMORY_HOST_RESIDENT_LIMIT_PAGE_COUNT);
    if (user_memory_pressure_controller.Initialize(MemoryPressureConfiguration{
            .managed_page_count = frame_statistics.managed_frame_count,
            .initial_resident_page_count = frame_statistics.allocated_frame_count,
            .resident_limit_page_count = resident_limit_page_count,
            .swap_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            .watermark_scale_factor = OS_KERNEL_MEMORY_PRESSURE_DEFAULT_WATERMARK_SCALE_FACTOR,
            .swappiness = user_memory_swappiness_configured
                              ? user_memory_swappiness
                              : OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS,
        }) != MemoryPressureStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    const FilePageCacheRuntimeConfiguration cache_configuration =
        SelectFilePageCacheRuntimeConfiguration(frame_statistics.managed_frame_count);
    if (cache_configuration.capacity > OS_KERNEL_USER_FILE_PAGE_CACHE_MAXIMUM_CAPACITY ||
        cache_configuration.metadata_block_order >= 64ULL) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    const uint64_t metadata_page_count = 1ULL << cache_configuration.metadata_block_order;
    if (metadata_page_count > UINT64_MAX / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        GetKernelPhysicalFrameAllocator().AllocateBlock(cache_configuration.metadata_block_order,
                                                        user_file_page_cache_metadata_block) !=
            PhysicalFrameAllocatorStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    user_file_page_cache_metadata_size_bytes =
        metadata_page_count * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    const uint64_t metadata_virtual_address =
        PhysicalMemoryDirectMapAddress(user_file_page_cache_metadata_block.physical_address);
    if (metadata_virtual_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        user_file_page_cache_metadata_heap.Initialize(metadata_virtual_address,
                                                      user_file_page_cache_metadata_size_bytes) !=
            KernelHeapStatus::Succeeded) {
        static_cast<void>(
            GetKernelPhysicalFrameAllocator().ReleaseBlock(user_file_page_cache_metadata_block));
        user_file_page_cache_metadata_block = PhysicalFrameBlock{};
        user_file_page_cache_metadata_size_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    uint64_t dirty_page_limit =
        (cache_configuration.capacity / OS_KERNEL_USER_MEMORY_PERCENT_DENOMINATOR) *
            OS_KERNEL_USER_MEMORY_DIRTY_HARD_LIMIT_PERCENT +
        ((cache_configuration.capacity % OS_KERNEL_USER_MEMORY_PERCENT_DENOMINATOR) *
         OS_KERNEL_USER_MEMORY_DIRTY_HARD_LIMIT_PERCENT) /
            OS_KERNEL_USER_MEMORY_PERCENT_DENOMINATOR;
    if (dirty_page_limit == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        dirty_page_limit = OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
    }
    if (user_file_page_cache.Initialize(user_file_page_cache_metadata_heap,
                                        cache_configuration.capacity, dirty_page_limit,
                                        GetKernelPhysicalFrameAllocator(), nullptr,
                                        AccessPhysicalPage) != FilePageCacheStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    if (user_file_writeback_error_tracker.Initialize(user_file_page_cache_metadata_heap) !=
        FileWritebackErrorTrackerStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    if (user_page_reference_manager.Initialize(
            user_page_reference_entries, OS_KERNEL_USER_PAGE_REFERENCE_CAPACITY,
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) != UserPageReferenceStatus::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    active_user_address_space = nullptr;
    user_file_page_cache_buffered_read_operation_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_buffered_read_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_buffered_read_cache_hit_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_buffered_read_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_buffered_write_operation_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_buffered_write_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_buffered_write_cache_hit_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_buffered_write_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_truncate_operation_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_writeback_worker_run_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_writeback_worker_written_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_writeback_worker_failure_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_writeback_backpressure_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_readahead_operation_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_readahead_requested_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_readahead_loaded_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_readahead_existing_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_readahead_busy_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_readahead_failed_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_file_page_cache_readahead_pressure_stop_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    user_memory_reclaim_operations = UserMemoryReclaimOperations{};
    user_memory_reclaim_statistics = UserMemoryReclaimStatistics{};
    if (!user_memory_swappiness_configured) {
        user_memory_swappiness = OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS;
    }
    user_virtual_memory_initialized = true;
    return UserAddressSpaceStatus::Succeeded;
}

bool ConfigureUserMemoryResidentLimit(const uint64_t resident_limit_page_count) noexcept {
    if (user_virtual_memory_initialized ||
        resident_limit_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return false;
    }
    user_memory_resident_limit_override_page_count = resident_limit_page_count;
    return true;
}

bool ConfigureUserMemorySwappiness(const uint64_t swappiness) noexcept {
    if (user_virtual_memory_initialized ||
        swappiness > OS_KERNEL_MEMORY_PRESSURE_MAXIMUM_SWAPPINESS) {
        return false;
    }
    user_memory_swappiness = swappiness;
    user_memory_swappiness_configured = true;
    return true;
}

bool ApplyConfiguredUserMemoryResidentLimit() noexcept {
    if (!user_virtual_memory_initialized) {
        return false;
    }
    if (user_memory_resident_limit_override_page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return true;
    }
    return SynchronizeUserMemoryPressure() &&
           user_memory_pressure_controller.ConfigureResidentLimit(
               user_memory_resident_limit_override_page_count) == MemoryPressureStatus::Succeeded;
}

UserAddressSpaceStatus AttachUserSwap(FileSystemBlockDevice &device) noexcept {
    user_swap_initialization_stage = UserSwapInitializationStage::NotStarted;
    if (!user_virtual_memory_initialized) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    if (user_swap_attached) {
        user_swap_initialization_stage = UserSwapInitializationStage::Ready;
        return UserAddressSpaceStatus::Succeeded;
    }

    if (user_swap_storage.Initialize(device) != SwapStorageStatus::Succeeded) {
        return UserAddressSpaceStatus::SwapInitializationFailed;
    }
    user_swap_initialization_stage = UserSwapInitializationStage::StorageReady;
    const PhysicalFrameAllocatorStatistics frame_statistics = GetPhysicalFrameAllocatorStatistics();
    const uint64_t admin_reserve_page_count = Minimum(
        (frame_statistics.managed_frame_count / OS_KERNEL_MEMORY_PRESSURE_PERCENT_DENOMINATOR) *
                OS_KERNEL_USER_MEMORY_ADMIN_RESERVE_PERCENT +
            ((frame_statistics.managed_frame_count %
              OS_KERNEL_MEMORY_PRESSURE_PERCENT_DENOMINATOR) *
             OS_KERNEL_USER_MEMORY_ADMIN_RESERVE_PERCENT) /
                OS_KERNEL_MEMORY_PRESSURE_PERCENT_DENOMINATOR,
        OS_KERNEL_USER_MEMORY_ADMIN_RESERVE_MAXIMUM_PAGE_COUNT);
    if (user_swap_manager.Initialize(
            user_swap_storage.SlotCapacity(), OS_KERNEL_MEMORY_PAGE_SIZE_BYTES, &user_swap_storage,
            SwapStorage::ReadEntryOperation, SwapStorage::WriteEntryOperation,
            SwapStorage::ReadPageOperation,
            SwapStorage::WritePageOperation) != SwapManagerStatus::Succeeded) {
        return UserAddressSpaceStatus::SwapInitializationFailed;
    }
    user_swap_initialization_stage = UserSwapInitializationStage::ManagerReady;
    if (user_memory_pressure_controller.ConfigureSwap(user_swap_storage.SlotCapacity()) !=
        MemoryPressureStatus::Succeeded) {
        return UserAddressSpaceStatus::SwapInitializationFailed;
    }
    user_swap_initialization_stage = UserSwapInitializationStage::PressureReady;
    if (user_memory_overcommit_accountant.Initialize(MemoryOvercommitConfiguration{
            .mode = MemoryOvercommitMode::Heuristic,
            .physical_page_count = frame_statistics.managed_frame_count,
            .swap_page_count = user_swap_storage.SlotCapacity(),
            .overcommit_ratio_percent = OS_KERNEL_MEMORY_PRESSURE_DEFAULT_OVERCOMMIT_RATIO_PERCENT,
            .admin_reserve_page_count = admin_reserve_page_count,
            .user_reserve_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
        }) != MemoryOvercommitStatus::Succeeded) {
        return UserAddressSpaceStatus::SwapInitializationFailed;
    }
    user_swap_initialization_stage = UserSwapInitializationStage::OvercommitReady;
    if (!RunUserSwapStorageSelfTest()) {
        return UserAddressSpaceStatus::SwapInitializationFailed;
    }
    user_swap_initialization_stage = UserSwapInitializationStage::StorageSelfTestPassed;
    user_swap_attached = true;
    user_swap_initialization_stage = UserSwapInitializationStage::Ready;
    return UserAddressSpaceStatus::Succeeded;
}

UserSwapInitializationStage GetUserSwapInitializationStage() noexcept {
    return user_swap_initialization_stage;
}

VirtualMemoryAreaPoolStatistics GetUserVirtualMemoryPoolStatistics() noexcept {
    return user_virtual_memory_pool.Statistics();
}

FilePageCacheStatistics GetUserFilePageCacheStatistics() noexcept {
    return user_file_page_cache.Statistics();
}

uint64_t GetUserFilePageCacheMetadataSizeBytes() noexcept {
    return user_file_page_cache_metadata_size_bytes;
}

UserFilePageCacheRuntimeStatistics GetUserFilePageCacheRuntimeStatistics() noexcept {
    return UserFilePageCacheRuntimeStatistics{
        .metadata_size_bytes = user_file_page_cache_metadata_size_bytes,
        .buffered_read_operation_count = user_file_page_cache_buffered_read_operation_count,
        .buffered_read_page_count = user_file_page_cache_buffered_read_page_count,
        .buffered_read_cache_hit_count = user_file_page_cache_buffered_read_cache_hit_count,
        .buffered_read_bytes = user_file_page_cache_buffered_read_bytes,
        .buffered_write_operation_count = user_file_page_cache_buffered_write_operation_count,
        .buffered_write_page_count = user_file_page_cache_buffered_write_page_count,
        .buffered_write_cache_hit_count = user_file_page_cache_buffered_write_cache_hit_count,
        .buffered_write_bytes = user_file_page_cache_buffered_write_bytes,
        .truncate_operation_count = user_file_page_cache_truncate_operation_count,
        .writeback_worker_run_count = user_file_page_cache_writeback_worker_run_count,
        .writeback_worker_written_page_count =
            user_file_page_cache_writeback_worker_written_page_count,
        .writeback_worker_failure_count = user_file_page_cache_writeback_worker_failure_count,
        .writeback_backpressure_count = user_file_page_cache_writeback_backpressure_count,
        .readahead_operation_count = user_file_page_cache_readahead_operation_count,
        .readahead_requested_page_count = user_file_page_cache_readahead_requested_page_count,
        .readahead_loaded_page_count = user_file_page_cache_readahead_loaded_page_count,
        .readahead_existing_page_count = user_file_page_cache_readahead_existing_page_count,
        .readahead_busy_page_count = user_file_page_cache_readahead_busy_page_count,
        .readahead_failed_page_count = user_file_page_cache_readahead_failed_page_count,
        .readahead_pressure_stop_count = user_file_page_cache_readahead_pressure_stop_count,
    };
}

UserVirtualMemoryStatus
VisitUserFilePageCache(void *const context, const FilePageCacheVisitOperation operation) noexcept {
    const FilePageCacheStatus status = user_file_page_cache.VisitEntries(context, operation);
    return status == FilePageCacheStatus::Succeeded ? UserVirtualMemoryStatus::Succeeded
                                                    : UserVirtualMemoryStatus::Corrupt;
}

UserAddressSpaceStatus AttachUserFilePageCache(fs::Vfs &vfs) noexcept {
    if (!user_virtual_memory_initialized ||
        user_file_page_cache.Validate() != FilePageCacheStatus::Succeeded ||
        vfs.ConfigureRegularFileDataCache(&vfs, ReadVfsFileThroughCache, WriteVfsFileThroughCache,
                                          ResolveVfsFileSizeFromCache,
                                          TruncateVfsFileCache) != fs::Status::Succeeded) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus
ConfigureUserFilePageCacheLoadingWait(const FilePageLoadWaitOperations &operations) noexcept {
    return user_virtual_memory_initialized && user_file_page_cache.ConfigureLoadingWait(
                                                  operations) == FilePageCacheStatus::Succeeded
               ? UserAddressSpaceStatus::Succeeded
               : UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
}

UserAddressSpaceStatus ConfigureUserFilePageCacheWritebackWait(
    const FilePageWritebackWaitOperations &operations) noexcept {
    return user_virtual_memory_initialized && user_file_page_cache.ConfigureWritebackWait(
                                                  operations) == FilePageCacheStatus::Succeeded
               ? UserAddressSpaceStatus::Succeeded
               : UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
}

UserAddressSpaceStatus ConfigureUserFilePageCacheReadaheadFeedback(
    const FilePageReadaheadFeedbackOperations &operations) noexcept {
    return user_virtual_memory_initialized && user_file_page_cache.ConfigureReadaheadFeedback(
                                                  operations) == FilePageCacheStatus::Succeeded
               ? UserAddressSpaceStatus::Succeeded
               : UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
}

UserVirtualMemoryStatus PrefetchUserFilePages(fs::Vfs &vfs, const fs::OpenFile &open_file,
                                              const uint64_t start_page_index,
                                              const uint64_t page_count,
                                              const FileReadaheadPageTag &tag,
                                              const UserFileReadaheadControl &control,
                                              UserFileReadaheadResult &result) noexcept {
    result = UserFileReadaheadResult{};
    if (!user_virtual_memory_initialized) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    if (!open_file.open || !open_file.readable ||
        open_file.path.vnode.type != fs::NodeType::RegularFile ||
        page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        start_page_index > UINT64_MAX - page_count || !FileReadaheadPageTagIsValid(tag) ||
        control.context == nullptr || control.continue_operation == nullptr) {
        return UserVirtualMemoryStatus::InvalidFile;
    }
    fs::NodeInformation information{};
    fs::NodeInformation source_information{};
    if (vfs.StatOpenFile(open_file, information) != fs::Status::Succeeded ||
        vfs.StatOpenFileUncached(open_file, source_information) != fs::Status::Succeeded) {
        return UserVirtualMemoryStatus::InvalidFile;
    }
    result.requested_page_count = page_count;
    const FileIdentity identity = FileIdentityFromVnode(open_file.path.vnode);
    VfsFilePageReaderContext reader{
        .vfs = &vfs,
        .open_file = &open_file,
        .source_size_bytes = source_information.size_bytes,
    };
    for (uint64_t page_ordinal = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; page_ordinal < page_count;
         ++page_ordinal) {
        bool continue_readahead = false;
        if (!control.continue_operation(control.context, continue_readahead)) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (!continue_readahead) {
            result.cancelled = true;
            break;
        }
        const uint64_t page_index = start_page_index + page_ordinal;
        if (page_index > UINT64_MAX / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
            page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES >= information.size_bytes) {
            break;
        }
        if (GetUserMemoryPressureLevel() == MemoryPressureLevel::BelowMinimum) {
            result.pressure_stopped = true;
            break;
        }
        const FilePageIdentity page_identity{
            .file = identity,
            .page_index = page_index,
        };
        FilePageCacheEntry existing_entry{};
        const FilePageCacheStatus existing_status =
            user_file_page_cache.ReadEntry(page_identity, existing_entry);
        if (existing_status == FilePageCacheStatus::Succeeded) {
            if (existing_entry.state == FilePageCacheEntryState::Loading) {
                ++result.busy_page_count;
            } else {
                ++result.existing_page_count;
            }
            continue;
        }
        if (existing_status != FilePageCacheStatus::MappingNotFound) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (user_file_page_cache.Statistics().resident_page_count >=
            user_file_page_cache.Statistics().capacity) {
            result.pressure_stopped = true;
            break;
        }
        uint64_t physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
        bool cache_hit = false;
        bool prefetched_hit = false;
        const FilePageCacheStatus acquire_status = user_file_page_cache.Acquire(
            page_identity, &reader, ReadVfsFilePage, FilePageAcquireIntent::Prefetch, tag,
            physical_address, cache_hit, prefetched_hit);
        if (acquire_status == FilePageCacheStatus::EntryBusy ||
            acquire_status == FilePageCacheStatus::LoadingWaitUnavailable) {
            ++result.busy_page_count;
            continue;
        }
        if (acquire_status == FilePageCacheStatus::CapacityExhausted ||
            acquire_status == FilePageCacheStatus::FrameAllocationFailed ||
            acquire_status == FilePageCacheStatus::MetadataAllocationFailed) {
            result.pressure_stopped = true;
            break;
        }
        if (acquire_status == FilePageCacheStatus::SourceReadFailed ||
            acquire_status == FilePageCacheStatus::FrameAccessFailed) {
            ++result.failed_page_count;
            continue;
        }
        if (acquire_status != FilePageCacheStatus::Succeeded || prefetched_hit) {
            if (acquire_status == FilePageCacheStatus::Succeeded) {
                static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            }
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (user_file_page_cache.ObserveFileSize(identity, information.size_bytes) !=
            FilePageCacheStatus::Succeeded) {
            static_cast<void>(user_file_page_cache.Release(page_identity, physical_address));
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (user_file_page_cache.Release(page_identity, physical_address) !=
            FilePageCacheStatus::Succeeded) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (cache_hit) {
            ++result.existing_page_count;
        } else {
            ++result.loaded_page_count;
        }
    }
    if (user_file_page_cache_readahead_operation_count == UINT64_MAX ||
        !AccumulateCounter(user_file_page_cache_readahead_requested_page_count,
                           result.requested_page_count) ||
        !AccumulateCounter(user_file_page_cache_readahead_loaded_page_count,
                           result.loaded_page_count) ||
        !AccumulateCounter(user_file_page_cache_readahead_existing_page_count,
                           result.existing_page_count) ||
        !AccumulateCounter(user_file_page_cache_readahead_busy_page_count,
                           result.busy_page_count) ||
        !AccumulateCounter(user_file_page_cache_readahead_failed_page_count,
                           result.failed_page_count) ||
        (result.pressure_stopped &&
         user_file_page_cache_readahead_pressure_stop_count == UINT64_MAX)) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    ++user_file_page_cache_readahead_operation_count;
    if (result.pressure_stopped) {
        ++user_file_page_cache_readahead_pressure_stop_count;
    }
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus DiscardUserFilePrefetchedPages(const FileReadaheadStreamToken stream,
                                                       const uint64_t maximum_policy_generation,
                                                       uint64_t &discarded_page_count) noexcept {
    return user_file_page_cache.DiscardPrefetched(stream, maximum_policy_generation,
                                                  discarded_page_count) ==
                   FilePageCacheStatus::Succeeded
               ? UserVirtualMemoryStatus::Succeeded
               : UserVirtualMemoryStatus::Corrupt;
}

MemoryPressureStatistics GetUserMemoryPressureStatistics() noexcept {
    static_cast<void>(SynchronizeUserMemoryPressure());
    return user_memory_pressure_controller.Statistics();
}

MemoryPressureLevel GetUserMemoryPressureLevel() noexcept {
    const MemoryPressureStatistics statistics = GetUserMemoryPressureStatistics();
    const uint64_t resident_limit_page_count = statistics.watermarks.resident_limit_page_count;
    const uint64_t free_page_count =
        statistics.resident_page_count < resident_limit_page_count
            ? resident_limit_page_count - statistics.resident_page_count
            : OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    return ClassifyMemoryPressure(statistics.watermarks, free_page_count);
}

uint64_t GetUserMemorySwappiness() noexcept {
    return user_virtual_memory_initialized ? user_memory_swappiness
                                           : OS_KERNEL_MEMORY_PRESSURE_DEFAULT_SWAPPINESS;
}

MemoryOvercommitStatistics GetUserMemoryOvercommitStatistics() noexcept {
    return user_memory_overcommit_accountant.Statistics();
}

SwapManagerStatistics GetUserSwapStatistics() noexcept { return user_swap_manager.Statistics(); }

bool ValidateUserMemoryManagement() noexcept {
    return user_memory_pressure_controller.Validate() == MemoryPressureStatus::Succeeded &&
           user_memory_overcommit_accountant.Validate() == MemoryOvercommitStatus::Succeeded &&
           user_swap_manager.Validate() == SwapManagerStatus::Succeeded;
}

bool ConfigureUserMemoryReclaimOperations(const UserMemoryReclaimOperations &operations) noexcept {
    if (!user_virtual_memory_initialized || operations.protect_shared_mappings == nullptr ||
        operations.request_background_reclaim == nullptr ||
        operations.prepare_anonymous_page_release == nullptr ||
        operations.reclaim_anonymous_pages == nullptr ||
        operations.recover_out_of_memory == nullptr) {
        return false;
    }
    user_memory_reclaim_operations = operations;
    return true;
}

UserMemoryReclaimStatistics GetUserMemoryReclaimStatistics() noexcept {
    return user_memory_reclaim_statistics;
}

UserVirtualMemoryStatus ReclaimUserAnonymousPages(UserAddressSpace &address_space,
                                                  const uint64_t target_page_count,
                                                  const uint64_t excluded_virtual_address,
                                                  const uint64_t protected_virtual_address,
                                                  uint64_t &reclaimed_page_count) noexcept {
    return SwapOutUserPages(address_space, target_page_count, excluded_virtual_address,
                            protected_virtual_address, nullptr, nullptr, nullptr,
                            reclaimed_page_count)
               ? UserVirtualMemoryStatus::Succeeded
               : UserVirtualMemoryStatus::SwapCorrupt;
}

UserVirtualMemoryStatus ReclaimSelectedUserAnonymousPages(
    UserAddressSpace &address_space, const uint64_t target_page_count,
    const uint64_t excluded_virtual_address, const uint64_t protected_virtual_address,
    void *const context, const UserMemoryReclaimPageSelectionOperation selection_operation,
    const UserMemoryReclaimPageCompletionOperation completion_operation,
    uint64_t &reclaimed_page_count) noexcept {
    return SwapOutUserPages(address_space, target_page_count, excluded_virtual_address,
                            protected_virtual_address, context, selection_operation,
                            completion_operation, reclaimed_page_count)
               ? UserVirtualMemoryStatus::Succeeded
               : UserVirtualMemoryStatus::SwapCorrupt;
}

UserVirtualMemoryStatus
ReclaimSelectedUserFilePages(const uint64_t maximum_page_count, void *const context,
                             const FilePageCacheReclaimSelectionOperation selection_operation,
                             const FilePageCacheReclaimCompletionOperation completion_operation,
                             uint64_t &reclaimed_page_count) noexcept {
    const FilePageCacheStatus status =
        user_file_page_cache.ReclaimCleanPages(maximum_page_count, context, selection_operation,
                                               completion_operation, reclaimed_page_count);
    return status == FilePageCacheStatus::Succeeded ? UserVirtualMemoryStatus::Succeeded
                                                    : UserVirtualMemoryStatus::Corrupt;
}

bool RecordUserBackgroundMemoryReclaim(const uint64_t clean_file_page_count,
                                       const uint64_t written_file_page_count,
                                       const uint64_t swapped_anonymous_page_count) noexcept {
    uint64_t reclaimed_page_count = clean_file_page_count;
    if (!AccumulateCounter(reclaimed_page_count, swapped_anonymous_page_count) ||
        user_memory_reclaim_statistics.clean_file_page_count > UINT64_MAX - clean_file_page_count ||
        user_memory_reclaim_statistics.written_file_page_count >
            UINT64_MAX - written_file_page_count ||
        user_memory_reclaim_statistics.swapped_anonymous_page_count >
            UINT64_MAX - swapped_anonymous_page_count) {
        return false;
    }
    const UserMemoryReclaimStatistics previous_statistics = user_memory_reclaim_statistics;
    user_memory_reclaim_statistics.clean_file_page_count += clean_file_page_count;
    user_memory_reclaim_statistics.written_file_page_count += written_file_page_count;
    user_memory_reclaim_statistics.swapped_anonymous_page_count += swapped_anonymous_page_count;
    if ((reclaimed_page_count != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
         user_memory_pressure_controller.RecordReclaim(reclaimed_page_count) !=
             MemoryPressureStatus::Succeeded) ||
        !SynchronizeUserMemoryPressure()) {
        user_memory_reclaim_statistics = previous_statistics;
        return false;
    }
    return true;
}

MemoryOvercommitStatus CommitUserMemory(UserAddressSpace &address_space, const uint64_t page_count,
                                        const bool privileged) noexcept {
    if (page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return MemoryOvercommitStatus::Succeeded;
    }
    if (address_space.committed_page_count > UINT64_MAX - page_count) {
        return MemoryOvercommitStatus::CounterOverflow;
    }
    const MemoryOvercommitStatus commit_status =
        user_memory_overcommit_accountant.TryCommit(page_count, privileged);
    if (commit_status != MemoryOvercommitStatus::Succeeded) {
        return commit_status;
    }
    address_space.committed_page_count += page_count;
    if (address_space.committed_page_count > address_space.peak_committed_page_count) {
        address_space.peak_committed_page_count = address_space.committed_page_count;
    }
    return MemoryOvercommitStatus::Succeeded;
}

MemoryOvercommitStatus UncommitUserMemory(UserAddressSpace &address_space,
                                          const uint64_t page_count) noexcept {
    if (page_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return MemoryOvercommitStatus::Succeeded;
    }
    if (page_count > address_space.committed_page_count) {
        return MemoryOvercommitStatus::CommitAccountingUnderflow;
    }
    const MemoryOvercommitStatus uncommit_status =
        user_memory_overcommit_accountant.Uncommit(page_count);
    if (uncommit_status != MemoryOvercommitStatus::Succeeded) {
        return uncommit_status;
    }
    address_space.committed_page_count -= page_count;
    return MemoryOvercommitStatus::Succeeded;
}

UserPageReferenceStatistics GetUserPageReferenceStatistics() noexcept {
    return user_page_reference_manager.Statistics();
}

UserAddressSpaceStatus
LoadUserAddressSpace(const uint8_t *const image, const uint64_t image_size_bytes,
                     UserAddressSpace &address_space,
                     UserElfValidationStatus &elf_validation_status) noexcept {
    if (image == nullptr) {
        elf_validation_status = UserElfValidationStatus::NullImage;
        return UserAddressSpaceStatus::InvalidElf;
    }
    MemoryImageReaderContext context{
        .image = image,
        .image_size_bytes = image_size_bytes,
    };
    const UserElfReader reader{
        .context = &context,
        .image_size_bytes = image_size_bytes,
        .read = ReadMemoryImage,
    };
    return LoadUserAddressSpaceInternal(reader, image, nullptr, nullptr, address_space,
                                        elf_validation_status);
}

UserAddressSpaceStatus
LoadUserAddressSpace(fs::Vfs &vfs, const fs::OpenFile &open_file, UserAddressSpace &address_space,
                     UserElfValidationStatus &elf_validation_status) noexcept {
    fs::NodeInformation information{};
    if (!open_file.open || !open_file.readable ||
        vfs.StatOpenFile(open_file, information) != fs::Status::Succeeded ||
        information.type != fs::NodeType::RegularFile) {
        elf_validation_status = UserElfValidationStatus::NullReader;
        return UserAddressSpaceStatus::InvalidElf;
    }
    VfsImageReaderContext context{
        .vfs = &vfs,
        .open_file = open_file,
    };
    const UserElfReader reader{
        .context = &context,
        .image_size_bytes = information.size_bytes,
        .read = ReadVfsImage,
    };
    return LoadUserAddressSpaceInternal(reader, nullptr, &vfs, &open_file, address_space,
                                        elf_validation_status);
}

[[nodiscard]] bool RestoreExclusiveForkPages(UserAddressSpace &parent_address_space) noexcept {
    const uint64_t area_count = parent_address_space.virtual_memory_map.AreaCount();
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea area{};
        if (parent_address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return false;
        }
        for (uint64_t page_address = area.begin_address; page_address < area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            PageMapping mapping{};
            const PageTableStatus query_status = QueryAddressSpacePage(
                parent_address_space.root_physical_address, page_address, mapping);
            if (query_status == PageTableStatus::NotMapped) {
                continue;
            }
            if (query_status != PageTableStatus::Succeeded) {
                return false;
            }
            bool cache_mapping = false;
            if (IsFileBackedVirtualMemoryAreaKind(area.kind)) {
                UserFileBackingDescriptor descriptor{};
                FilePageIdentity page_identity{};
                if (!ReadBackingDescriptor(area, descriptor)) {
                    return false;
                }
                cache_mapping = PageUsesFileCache(area, page_address, descriptor, page_identity);
            }
            if (cache_mapping) {
                continue;
            }
            uint64_t reference_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
            const UserPageReferenceStatus reference_status =
                user_page_reference_manager.ReadReferenceCount(mapping.physical_address,
                                                               reference_count);
            if (reference_status == UserPageReferenceStatus::ReferenceNotFound) {
                continue;
            }
            if (reference_status != UserPageReferenceStatus::Succeeded) {
                return false;
            }
            if (reference_count != OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) {
                continue;
            }
            if (mapping.permissions.copy_on_write) {
                if (!area.permissions.writable ||
                    ReplaceUserPage(parent_address_space.root_physical_address, page_address,
                                    mapping.physical_address, true, mapping.permissions.executable,
                                    false) != KernelUserPageStatus::Succeeded ||
                    parent_address_space.copy_on_write_page_count ==
                        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
                    return false;
                }
                --parent_address_space.copy_on_write_page_count;
            }
            if (user_page_reference_manager.RestoreExclusive(mapping.physical_address) !=
                UserPageReferenceStatus::Succeeded) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] UserAddressSpaceStatus
RollbackForkClone(UserAddressSpace &parent_address_space, UserAddressSpace &child_address_space,
                  const UserAddressSpaceStatus failure_status) noexcept {
    return DestroyUserAddressSpace(child_address_space) == UserAddressSpaceStatus::Succeeded &&
                   RestoreExclusiveForkPages(parent_address_space)
               ? failure_status
               : UserAddressSpaceStatus::RollbackFailed;
}

UserAddressSpaceStatus CloneUserAddressSpaceForFork(UserAddressSpace &parent_address_space,
                                                    UserAddressSpace &child_address_space,
                                                    const bool privileged) noexcept {
    child_address_space = UserAddressSpace{};
    if (parent_address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded ||
        parent_address_space.root_physical_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }
    if (CreateUserPageTable(child_address_space.root_physical_address) !=
        KernelUserPageStatus::Succeeded) {
        return UserAddressSpaceStatus::PageTableCreationFailed;
    }
    if (child_address_space.virtual_memory_map.Initialize(
            user_virtual_memory_pool, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
            OS_KERNEL_USER_VMA_PER_PROCESS_HARD_LIMIT) != VirtualMemoryAreaStatus::Succeeded) {
        static_cast<void>(DestroyUserPageTable(child_address_space.root_physical_address));
        child_address_space = UserAddressSpace{};
        return UserAddressSpaceStatus::VirtualMemoryInitializationFailed;
    }

    const uint64_t area_count = parent_address_space.virtual_memory_map.AreaCount();
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea parent_area{};
        if (parent_address_space.virtual_memory_map.ReadAt(area_index, parent_area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            static_cast<void>(DestroyUserAddressSpace(child_address_space));
            return UserAddressSpaceStatus::RollbackFailed;
        }
        VirtualMemoryArea child_area = parent_area;
        bool cloned_new_backing = false;
        if (IsFileBackedVirtualMemoryAreaKind(parent_area.kind)) {
            bool reused_backing = false;
            for (uint64_t previous_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
                 previous_index < area_index; ++previous_index) {
                VirtualMemoryArea previous_parent_area{};
                VirtualMemoryArea previous_child_area{};
                if (parent_address_space.virtual_memory_map.ReadAt(previous_index,
                                                                   previous_parent_area) !=
                        VirtualMemoryAreaStatus::Succeeded ||
                    child_address_space.virtual_memory_map.ReadAt(previous_index,
                                                                  previous_child_area) !=
                        VirtualMemoryAreaStatus::Succeeded) {
                    static_cast<void>(DestroyUserAddressSpace(child_address_space));
                    return UserAddressSpaceStatus::RollbackFailed;
                }
                if (previous_parent_area.backing_descriptor_index ==
                        parent_area.backing_descriptor_index &&
                    previous_parent_area.backing_generation == parent_area.backing_generation) {
                    child_area.backing_descriptor_index =
                        previous_child_area.backing_descriptor_index;
                    child_area.backing_generation = previous_child_area.backing_generation;
                    reused_backing = true;
                    break;
                }
            }
            if (!reused_backing) {
                if (user_file_backing_manager.Clone(
                        child_address_space.root_physical_address,
                        parent_area.backing_descriptor_index, parent_area.backing_generation,
                        child_area.backing_descriptor_index,
                        child_area.backing_generation) != UserFileBackingStatus::Succeeded) {
                    static_cast<void>(DestroyUserAddressSpace(child_address_space));
                    return UserAddressSpaceStatus::ForkBackingFailure;
                }
                cloned_new_backing = true;
            }
        }
        if (child_address_space.virtual_memory_map.Insert(child_area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            if (cloned_new_backing) {
                static_cast<void>(user_file_backing_manager.Release(
                    child_address_space.root_physical_address, child_area.backing_descriptor_index,
                    child_area.backing_generation));
            }
            static_cast<void>(DestroyUserAddressSpace(child_address_space));
            return UserAddressSpaceStatus::VirtualMemoryAreaFailure;
        }
    }

    if (!AssignAddressSpaceIdentifier(child_address_space)) {
        return RollbackForkClone(parent_address_space, child_address_space,
                                 UserAddressSpaceStatus::AddressSpaceIdentifierExhausted);
    }

    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea parent_area{};
        VirtualMemoryArea child_area{};
        if (parent_address_space.virtual_memory_map.ReadAt(area_index, parent_area) !=
                VirtualMemoryAreaStatus::Succeeded ||
            child_address_space.virtual_memory_map.ReadAt(area_index, child_area) !=
                VirtualMemoryAreaStatus::Succeeded) {
            return RollbackForkClone(parent_address_space, child_address_space,
                                     UserAddressSpaceStatus::RollbackFailed);
        }
        for (uint64_t page_address = parent_area.begin_address;
             page_address < parent_area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            PageMapping mapping{};
            const PageTableStatus query_status = QueryAddressSpacePage(
                parent_address_space.root_physical_address, page_address, mapping);
            if (query_status == PageTableStatus::NotMapped) {
                if (user_swap_attached) {
                    const SwapPageIdentity parent_identity =
                        MakeSwapIdentity(parent_address_space, page_address);
                    uint64_t parent_slot_index = UINT64_MAX;
                    const SwapManagerStatus find_status =
                        user_swap_manager.FindSlot(parent_identity, parent_slot_index);
                    static_cast<void>(parent_slot_index);
                    if (find_status == SwapManagerStatus::Succeeded) {
                        uint64_t child_slot_index = UINT64_MAX;
                        if (user_swap_manager.Clone(
                                parent_identity,
                                MakeSwapIdentity(child_address_space, page_address),
                                user_swap_clone_scratch_page, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
                                child_slot_index) != SwapManagerStatus::Succeeded) {
                            return RollbackForkClone(parent_address_space, child_address_space,
                                                     UserAddressSpaceStatus::ForkBackingFailure);
                        }
                        static_cast<void>(child_slot_index);
                        ++child_address_space.swapped_page_count;
                        child_address_space.peak_swapped_page_count =
                            child_address_space.swapped_page_count;
                    } else if (find_status != SwapManagerStatus::MappingNotFound) {
                        return RollbackForkClone(parent_address_space, child_address_space,
                                                 UserAddressSpaceStatus::RollbackFailed);
                    }
                }
                continue;
            }
            if (query_status != PageTableStatus::Succeeded ||
                !mapping.permissions.user_accessible) {
                return RollbackForkClone(parent_address_space, child_address_space,
                                         UserAddressSpaceStatus::RollbackFailed);
            }
            bool cache_mapping = false;
            FilePageIdentity page_identity{};
            UserFileBackingDescriptor child_backing{};
            if (IsFileBackedVirtualMemoryAreaKind(parent_area.kind) &&
                ReadBackingDescriptor(child_area, child_backing)) {
                cache_mapping =
                    PageUsesFileCache(child_area, page_address, child_backing, page_identity);
            }
            bool retained_private_page = false;
            bool first_share = false;
            if (cache_mapping) {
                uint64_t retained_physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
                bool cache_hit = false;
                const FilePageCacheStatus acquire_status = user_file_page_cache.Acquire(
                    page_identity, &child_backing, ReadUserFileBackingPage,
                    retained_physical_address, cache_hit);
                if (acquire_status != FilePageCacheStatus::Succeeded || !cache_hit ||
                    retained_physical_address != mapping.physical_address) {
                    if (acquire_status == FilePageCacheStatus::Succeeded) {
                        static_cast<void>(
                            user_file_page_cache.Release(page_identity, retained_physical_address));
                    }
                    return RollbackForkClone(parent_address_space, child_address_space,
                                             UserAddressSpaceStatus::RollbackFailed);
                }
            } else {
                const UserPageReferenceStatus retain_status =
                    user_page_reference_manager.RetainForFork(mapping.physical_address,
                                                              first_share);
                if (retain_status != UserPageReferenceStatus::Succeeded) {
                    return RollbackForkClone(parent_address_space, child_address_space,
                                             retain_status ==
                                                     UserPageReferenceStatus::CapacityExhausted
                                                 ? UserAddressSpaceStatus::ForkReferenceExhausted
                                                 : UserAddressSpaceStatus::RollbackFailed);
                }
                retained_private_page = true;
            }
            const bool copy_on_write = parent_area.permissions.writable &&
                                       parent_area.kind != VirtualMemoryAreaKind::FileShared;
            if (MapExistingUserPage(child_address_space.root_physical_address, page_address,
                                    mapping.physical_address, false, mapping.permissions.executable,
                                    copy_on_write) != KernelUserPageStatus::Succeeded) {
                if (cache_mapping) {
                    static_cast<void>(
                        user_file_page_cache.Release(page_identity, mapping.physical_address));
                } else if (retained_private_page) {
                    bool release_frame = false;
                    static_cast<void>(user_page_reference_manager.Release(mapping.physical_address,
                                                                          release_frame));
                    if (first_share) {
                        static_cast<void>(
                            user_page_reference_manager.RestoreExclusive(mapping.physical_address));
                    }
                }
                return RollbackForkClone(parent_address_space, child_address_space,
                                         UserAddressSpaceStatus::PageMappingFailed);
            }
            ++child_address_space.mapped_page_count;
            if (child_address_space.mapped_page_count >
                child_address_space.peak_mapped_page_count) {
                child_address_space.peak_mapped_page_count = child_address_space.mapped_page_count;
            }
            if (cache_mapping) {
                ++child_address_space.shared_file_resident_page_count;
            } else if (IsFileBackedVirtualMemoryAreaKind(child_area.kind)) {
                ++child_address_space.private_file_resident_page_count;
            }
            if (copy_on_write) {
                ++child_address_space.copy_on_write_page_count;
            }
        }
    }

    uint64_t copy_on_write_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea area{};
        if (parent_address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return RollbackForkClone(parent_address_space, child_address_space,
                                     UserAddressSpaceStatus::RollbackFailed);
        }
        if (!area.permissions.writable) {
            continue;
        }
        for (uint64_t page_address = area.begin_address; page_address < area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            PageMapping mapping{};
            const PageTableStatus query_status = QueryAddressSpacePage(
                parent_address_space.root_physical_address, page_address, mapping);
            if (query_status == PageTableStatus::NotMapped) {
                continue;
            }
            if (query_status != PageTableStatus::Succeeded) {
                return RollbackForkClone(parent_address_space, child_address_space,
                                         UserAddressSpaceStatus::RollbackFailed);
            }
            if (!mapping.permissions.copy_on_write) {
                if (ReplaceUserPage(parent_address_space.root_physical_address, page_address,
                                    mapping.physical_address, false, mapping.permissions.executable,
                                    true) != KernelUserPageStatus::Succeeded) {
                    return RollbackForkClone(parent_address_space, child_address_space,
                                             UserAddressSpaceStatus::RollbackFailed);
                }
                ++parent_address_space.copy_on_write_page_count;
            }
            ++copy_on_write_page_count;
        }
    }

    child_address_space.entry_virtual_address = parent_address_space.entry_virtual_address;
    child_address_space.stack_top_virtual_address = parent_address_space.stack_top_virtual_address;
    child_address_space.stack_committed_bottom_virtual_address =
        parent_address_space.stack_committed_bottom_virtual_address;
    child_address_space.program_break_base_address =
        parent_address_space.program_break_base_address;
    child_address_space.program_break_address = parent_address_space.program_break_address;
    child_address_space.program_break_limit_address =
        parent_address_space.program_break_limit_address;
    if (child_address_space.mapped_page_count != parent_address_space.mapped_page_count ||
        child_address_space.private_file_resident_page_count !=
            parent_address_space.private_file_resident_page_count ||
        child_address_space.shared_file_resident_page_count !=
            parent_address_space.shared_file_resident_page_count ||
        child_address_space.swapped_page_count != parent_address_space.swapped_page_count ||
        child_address_space.copy_on_write_page_count != copy_on_write_page_count ||
        parent_address_space.copy_on_write_page_count != copy_on_write_page_count) {
        return RollbackForkClone(parent_address_space, child_address_space,
                                 UserAddressSpaceStatus::RollbackFailed);
    }
    if (CommitUserMemory(child_address_space, parent_address_space.committed_page_count,
                         privileged) != MemoryOvercommitStatus::Succeeded) {
        return RollbackForkClone(parent_address_space, child_address_space,
                                 UserAddressSpaceStatus::CommitLimitExceeded);
    }
    ++parent_address_space.fork_clone_count;
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus
RestoreUserAddressSpaceAfterFailedFork(UserAddressSpace &parent_address_space) noexcept {
    if (parent_address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    if (!RestoreExclusiveForkPages(parent_address_space)) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    if (parent_address_space.fork_clone_count == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserAddressSpaceStatus::RollbackFailed;
    }
    --parent_address_space.fork_clone_count;
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus DestroyUserAddressSpace(UserAddressSpace &address_space) noexcept {
    user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{};
    if (active_user_address_space == &address_space) {
        active_user_address_space = nullptr;
    }
    const VirtualMemoryAreaStatus validation_status = address_space.virtual_memory_map.Validate();
    if (validation_status != VirtualMemoryAreaStatus::Succeeded &&
        validation_status != VirtualMemoryAreaStatus::NotInitialized) {
        user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
            .stage = UserAddressSpaceDestructionStage::ValidateMap,
            .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            .physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            .status = static_cast<uint64_t>(validation_status),
        };
        return UserAddressSpaceStatus::RollbackFailed;
    }
    if (validation_status == VirtualMemoryAreaStatus::Succeeded) {
        const uint64_t area_count = address_space.virtual_memory_map.AreaCount();
        for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
             ++area_index) {
            VirtualMemoryArea area{};
            if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
                VirtualMemoryAreaStatus::Succeeded) {
                user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                    .stage = UserAddressSpaceDestructionStage::ReadArea,
                    .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                    .physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                    .status = area_index,
                };
                return UserAddressSpaceStatus::RollbackFailed;
            }
            if (ReleaseMappedPages(address_space, area.begin_address, area.end_address, false) !=
                UserVirtualMemoryStatus::Succeeded) {
                return UserAddressSpaceStatus::RollbackFailed;
            }
        }
        if (address_space.mapped_page_count != OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
            address_space.swapped_page_count != OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
            user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                .stage = UserAddressSpaceDestructionStage::ResidentAccounting,
                .virtual_address = address_space.mapped_page_count,
                .physical_address = address_space.swapped_page_count,
                .status = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            };
            return UserAddressSpaceStatus::RollbackFailed;
        }
        if (address_space.root_physical_address != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
            DestroyUserPageTable(address_space.root_physical_address) !=
                KernelUserPageStatus::Succeeded) {
            user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                .stage = UserAddressSpaceDestructionStage::DestroyPageTable,
                .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                .physical_address = address_space.root_physical_address,
                .status = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            };
            return UserAddressSpaceStatus::RollbackFailed;
        }
        for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
             ++area_index) {
            VirtualMemoryArea area{};
            if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
                VirtualMemoryAreaStatus::Succeeded) {
                user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                    .stage = UserAddressSpaceDestructionStage::ReadArea,
                    .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                    .physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                    .status = area_index,
                };
                return UserAddressSpaceStatus::RollbackFailed;
            }
            if (!IsFileBackedVirtualMemoryAreaKind(area.kind)) {
                continue;
            }
            bool already_released = false;
            for (uint64_t previous_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
                 previous_index < area_index; ++previous_index) {
                VirtualMemoryArea previous_area{};
                if (address_space.virtual_memory_map.ReadAt(previous_index, previous_area) !=
                    VirtualMemoryAreaStatus::Succeeded) {
                    user_address_space_destruction_diagnostics =
                        UserAddressSpaceDestructionDiagnostics{
                            .stage = UserAddressSpaceDestructionStage::ReadArea,
                            .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                            .physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                            .status = previous_index,
                        };
                    return UserAddressSpaceStatus::RollbackFailed;
                }
                if (previous_area.backing_descriptor_index == area.backing_descriptor_index &&
                    previous_area.backing_generation == area.backing_generation) {
                    already_released = true;
                    break;
                }
            }
            const UserFileBackingStatus backing_release_status =
                already_released
                    ? UserFileBackingStatus::Succeeded
                    : user_file_backing_manager.Release(address_space.root_physical_address,
                                                        area.backing_descriptor_index,
                                                        area.backing_generation);
            if (backing_release_status != UserFileBackingStatus::Succeeded) {
                user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                    .stage = UserAddressSpaceDestructionStage::ReleaseBacking,
                    .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                    .physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                    .status =
                        backing_release_status == UserFileBackingStatus::CloseFailed
                            ? static_cast<uint64_t>(user_file_backing_manager.LastCloseStatus())
                            : static_cast<uint64_t>(backing_release_status),
                };
                return UserAddressSpaceStatus::RollbackFailed;
            }
        }
        if (address_space.virtual_memory_map.Destroy() != VirtualMemoryAreaStatus::Succeeded) {
            user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
                .stage = UserAddressSpaceDestructionStage::DestroyMap,
                .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                .physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
                .status = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            };
            return UserAddressSpaceStatus::RollbackFailed;
        }
    } else if (address_space.root_physical_address != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
               DestroyUserPageTable(address_space.root_physical_address) !=
                   KernelUserPageStatus::Succeeded) {
        user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
            .stage = UserAddressSpaceDestructionStage::DestroyPageTable,
            .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            .physical_address = address_space.root_physical_address,
            .status = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
        };
        return UserAddressSpaceStatus::RollbackFailed;
    }
    const FilePageCacheStatus trim_status =
        user_file_page_cache.Trim(OS_KERNEL_USER_MEMORY_EMPTY_VALUE);
    if (trim_status != FilePageCacheStatus::Succeeded &&
        trim_status != FilePageCacheStatus::EntryBusy) {
        user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
            .stage = UserAddressSpaceDestructionStage::TrimCache,
            .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            .physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            .status = static_cast<uint64_t>(trim_status),
        };
        return UserAddressSpaceStatus::RollbackFailed;
    }
    if (address_space.committed_page_count != OS_KERNEL_USER_MEMORY_EMPTY_VALUE &&
        UncommitUserMemory(address_space, address_space.committed_page_count) !=
            MemoryOvercommitStatus::Succeeded) {
        user_address_space_destruction_diagnostics = UserAddressSpaceDestructionDiagnostics{
            .stage = UserAddressSpaceDestructionStage::UncommitMemory,
            .virtual_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            .physical_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE,
            .status = address_space.committed_page_count,
        };
        return UserAddressSpaceStatus::RollbackFailed;
    }
    address_space = UserAddressSpace{};
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceDestructionDiagnostics GetUserAddressSpaceDestructionDiagnostics() noexcept {
    return user_address_space_destruction_diagnostics;
}

UserAddressSpaceStatus PrepareUserStack(UserAddressSpace &address_space,
                                        const uint64_t lowest_required_address) noexcept {
    if (lowest_required_address < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
        lowest_required_address >= OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS ||
        address_space.stack_committed_bottom_virtual_address !=
            OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return UserAddressSpaceStatus::StackPreparationFailed;
    }
    const uint64_t first_page_address = AlignDownToPage(lowest_required_address);
    VirtualMemoryArea stack_area{};
    if (address_space.virtual_memory_map.FindContaining(first_page_address, stack_area) !=
            VirtualMemoryAreaStatus::Succeeded ||
        stack_area.kind != VirtualMemoryAreaKind::UserStack) {
        return UserAddressSpaceStatus::StackPreparationFailed;
    }
    for (uint64_t page_address = first_page_address;
         page_address < OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS;
         page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        const UserVirtualMemoryStatus map_status =
            MapDemandPage(address_space, page_address, stack_area);
        if (map_status != UserVirtualMemoryStatus::Succeeded) {
            return UserAddressSpaceStatus::StackPreparationFailed;
        }
    }
    address_space.stack_committed_bottom_virtual_address = first_page_address;
    return UserAddressSpaceStatus::Succeeded;
}

UserAddressSpaceStatus PrepareUserStackRange(UserAddressSpace &address_space,
                                             const uint64_t lowest_required_address,
                                             const uint64_t current_stack_pointer) noexcept {
    if (lowest_required_address < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
        lowest_required_address >= OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS ||
        current_stack_pointer <= lowest_required_address ||
        current_stack_pointer > OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return UserAddressSpaceStatus::StackPreparationFailed;
    }
    if (address_space.stack_committed_bottom_virtual_address ==
        OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS) {
        return PrepareUserStack(address_space, lowest_required_address);
    }
    while (lowest_required_address < address_space.stack_committed_bottom_virtual_address) {
        if (address_space.stack_committed_bottom_virtual_address <
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            return UserAddressSpaceStatus::StackPreparationFailed;
        }
        const uint64_t next_page_address =
            address_space.stack_committed_bottom_virtual_address - OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
        const uint64_t synthetic_write_fault =
            OS_KERNEL_USER_MEMORY_PAGE_FAULT_WRITE_BIT | OS_KERNEL_USER_MEMORY_PAGE_FAULT_USER_BIT;
        if (HandleUserPageFault(address_space, next_page_address, synthetic_write_fault,
                                current_stack_pointer) != UserPageFaultStatus::Handled) {
            return UserAddressSpaceStatus::StackPreparationFailed;
        }
    }
    return UserAddressSpaceStatus::Succeeded;
}

UserVirtualMemoryStatus
MapAnonymousMemory(UserAddressSpace &address_space, const uint64_t requested_address,
                   const uint64_t length_bytes, const uint64_t protection_flags,
                   const uint64_t map_flags, uint64_t &mapped_address) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        (map_flags & ~os::abi::OS_ABI_ANONYMOUS_MEMORY_MAP_VALID_FLAG_MASK) !=
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        !ProtectionFlagsAreValid(protection_flags)) {
        return length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE
                   ? UserVirtualMemoryStatus::InvalidRange
                   : UserVirtualMemoryStatus::InvalidProtection;
    }
    uint64_t aligned_length_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!AlignUpToPage(length_bytes, aligned_length_bytes) ||
        aligned_length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        aligned_length_bytes > os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS -
                                   os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS) {
        return UserVirtualMemoryStatus::InvalidRange;
    }

    const bool fixed_mapping =
        (map_flags & os::abi::OS_ABI_MEMORY_MAP_FIXED) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t area_begin_address = requested_address;
    if (fixed_mapping) {
        if (requested_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
            (requested_address & OS_KERNEL_USER_MEMORY_PAGE_MASK) !=
                OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
            requested_address < os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS ||
            requested_address >
                os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS - aligned_length_bytes) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
    } else {
        if (requested_address != os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
        const VirtualMemoryAreaStatus gap_status = address_space.virtual_memory_map.FindFirstGap(
            os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS,
            os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS, aligned_length_bytes,
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES, area_begin_address);
        if (gap_status != VirtualMemoryAreaStatus::Succeeded) {
            return gap_status == VirtualMemoryAreaStatus::NotMapped
                       ? UserVirtualMemoryStatus::AddressSpaceExhausted
                       : MapVirtualMemoryAreaStatus(gap_status);
        }
    }

    const VirtualMemoryAreaStatus insert_status =
        address_space.virtual_memory_map.Insert(VirtualMemoryArea{
            .begin_address = area_begin_address,
            .end_address = area_begin_address + aligned_length_bytes,
            .permissions = DecodeProtectionFlags(protection_flags),
            .kind = VirtualMemoryAreaKind::Anonymous,
        });
    if (insert_status != VirtualMemoryAreaStatus::Succeeded) {
        return MapVirtualMemoryAreaStatus(insert_status);
    }
    mapped_address = area_begin_address;
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus MapFileMemory(UserAddressSpace &address_space, fs::Vfs &vfs,
                                      const fs::OpenFile &open_file,
                                      const uint64_t requested_address, const uint64_t length_bytes,
                                      const uint64_t protection_flags, const uint64_t map_flags,
                                      const uint64_t file_offset_bytes,
                                      uint64_t &mapped_address) noexcept {
    mapped_address = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    const bool private_mapping =
        (map_flags & os::abi::OS_ABI_MEMORY_MAP_PRIVATE) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool shared_mapping =
        (map_flags & os::abi::OS_ABI_MEMORY_MAP_SHARED) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool writable = (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_WRITE) !=
                          OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        (file_offset_bytes & OS_KERNEL_USER_MEMORY_PAGE_MASK) !=
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    if (!ProtectionFlagsAreValid(protection_flags) ||
        (protection_flags & os::abi::OS_ABI_MEMORY_PROTECTION_READ) ==
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    if ((map_flags & ~os::abi::OS_ABI_FILE_MEMORY_MAP_VALID_FLAG_MASK) !=
            OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        private_mapping == shared_mapping ||
        (shared_mapping && writable &&
         (length_bytes & OS_KERNEL_USER_MEMORY_PAGE_MASK) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE)) {
        return UserVirtualMemoryStatus::UnsupportedMapping;
    }
    fs::NodeInformation information{};
    if (!open_file.open || !open_file.readable ||
        (shared_mapping && writable && !open_file.writable) ||
        vfs.StatOpenFile(open_file, information) != fs::Status::Succeeded ||
        information.type != fs::NodeType::RegularFile) {
        return UserVirtualMemoryStatus::InvalidFile;
    }
    if (file_offset_bytes > information.size_bytes ||
        length_bytes > information.size_bytes - file_offset_bytes) {
        return UserVirtualMemoryStatus::InvalidRange;
    }

    uint64_t aligned_length_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!AlignUpToPage(length_bytes, aligned_length_bytes) ||
        aligned_length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        aligned_length_bytes > os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS -
                                   os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    const bool fixed_mapping =
        (map_flags & os::abi::OS_ABI_MEMORY_MAP_FIXED) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t area_begin_address = requested_address;
    if (fixed_mapping) {
        if (requested_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
            (requested_address & OS_KERNEL_USER_MEMORY_PAGE_MASK) !=
                OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
            requested_address < os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS ||
            requested_address >
                os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS - aligned_length_bytes) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
    } else {
        if (requested_address != os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
        const VirtualMemoryAreaStatus gap_status = address_space.virtual_memory_map.FindFirstGap(
            os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS,
            os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS, aligned_length_bytes,
            OS_KERNEL_MEMORY_PAGE_SIZE_BYTES, area_begin_address);
        if (gap_status != VirtualMemoryAreaStatus::Succeeded) {
            return gap_status == VirtualMemoryAreaStatus::NotMapped
                       ? UserVirtualMemoryStatus::AddressSpaceExhausted
                       : MapVirtualMemoryAreaStatus(gap_status);
        }
    }

    uint64_t backing_descriptor_index = UINT64_MAX;
    uint64_t backing_generation = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (user_file_backing_manager.AcquireVfsFile(
            address_space.root_physical_address, vfs, open_file, backing_descriptor_index,
            backing_generation) != UserFileBackingStatus::Succeeded) {
        return UserVirtualMemoryStatus::MetadataExhausted;
    }
    const VirtualMemoryAreaStatus insert_status =
        address_space.virtual_memory_map.Insert(VirtualMemoryArea{
            .begin_address = area_begin_address,
            .end_address = area_begin_address + aligned_length_bytes,
            .permissions = DecodeProtectionFlags(protection_flags),
            .kind = private_mapping ? VirtualMemoryAreaKind::FilePrivate
                                    : VirtualMemoryAreaKind::FileShared,
            .backing_descriptor_index = backing_descriptor_index,
            .backing_generation = backing_generation,
            .backing_file_offset_bytes = file_offset_bytes,
            .backing_data_length_bytes = length_bytes,
        });
    if (insert_status != VirtualMemoryAreaStatus::Succeeded) {
        static_cast<void>(user_file_backing_manager.Release(
            address_space.root_physical_address, backing_descriptor_index, backing_generation));
        return MapVirtualMemoryAreaStatus(insert_status);
    }
    mapped_address = area_begin_address;
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus UnmapAnonymousMemory(UserAddressSpace &address_space,
                                             const uint64_t address,
                                             const uint64_t length_bytes) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    uint64_t aligned_length_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        (address & OS_KERNEL_USER_MEMORY_PAGE_MASK) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        !AlignUpToPage(length_bytes, aligned_length_bytes) ||
        address < os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS ||
        address > os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS - aligned_length_bytes) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    const uint64_t end_address = address + aligned_length_bytes;
    const VirtualMemoryAreaStatus remove_status = address_space.virtual_memory_map.Remove(
        address, end_address, VirtualMemoryAreaKind::Anonymous);
    if (remove_status != VirtualMemoryAreaStatus::Succeeded) {
        return MapVirtualMemoryAreaStatus(remove_status);
    }
    return ReleaseMappedPages(address_space, address, end_address, true);
}

UserVirtualMemoryStatus UnmapFileMemory(UserAddressSpace &address_space, const uint64_t address,
                                        const uint64_t length_bytes) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    uint64_t aligned_length_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        (address & OS_KERNEL_USER_MEMORY_PAGE_MASK) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        !AlignUpToPage(length_bytes, aligned_length_bytes) ||
        address < os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_BEGIN_ADDRESS ||
        address > os::abi::OS_ABI_USER_ANONYMOUS_WINDOW_END_ADDRESS - aligned_length_bytes) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    const uint64_t end_address = address + aligned_length_bytes;
    uint64_t validation_address = address;
    while (validation_address < end_address) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.FindContaining(validation_address, area) !=
                VirtualMemoryAreaStatus::Succeeded ||
            (area.kind != VirtualMemoryAreaKind::FilePrivate &&
             area.kind != VirtualMemoryAreaKind::FileShared)) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
        validation_address = Minimum(end_address, area.end_address);
    }

    uint64_t current_address = address;
    while (current_address < end_address) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.FindContaining(current_address, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        const uint64_t current_end_address = Minimum(end_address, area.end_address);
        const UserVirtualMemoryStatus release_status =
            ReleaseMappedPages(address_space, current_address, current_end_address, true);
        if (release_status != UserVirtualMemoryStatus::Succeeded) {
            return release_status;
        }
        const uint64_t backing_descriptor_index = area.backing_descriptor_index;
        const uint64_t backing_generation = area.backing_generation;
        const VirtualMemoryAreaStatus remove_status = address_space.virtual_memory_map.Remove(
            current_address, current_end_address, area.kind);
        if (remove_status != VirtualMemoryAreaStatus::Succeeded) {
            return MapVirtualMemoryAreaStatus(remove_status);
        }
        const UserVirtualMemoryStatus backing_status =
            ReleaseBackingIfUnused(address_space, backing_descriptor_index, backing_generation);
        if (backing_status != UserVirtualMemoryStatus::Succeeded) {
            return backing_status;
        }
        current_address = current_end_address;
    }
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus TruncateUserFileMappings(UserAddressSpace &address_space,
                                                 const FileIdentity &identity,
                                                 const uint64_t size_bytes) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
         area_index < address_space.virtual_memory_map.AreaCount(); ++area_index) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (!IsFileBackedVirtualMemoryAreaKind(area.kind)) {
            continue;
        }
        UserFileBackingDescriptor descriptor{};
        if (!ReadBackingDescriptor(area, descriptor)) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (!FileIdentitiesEqual(descriptor.identity, identity)) {
            continue;
        }
        for (uint64_t page_address = area.begin_address; page_address < area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            const uint64_t area_offset_bytes = page_address - area.begin_address;
            if (area.backing_file_offset_bytes > UINT64_MAX - area_offset_bytes) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            const uint64_t file_offset_bytes = area.backing_file_offset_bytes + area_offset_bytes;
            if (file_offset_bytes < size_bytes) {
                continue;
            }
            PageMapping mapping{};
            const PageTableStatus query_status =
                QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping);
            if (query_status == PageTableStatus::NotMapped) {
                continue;
            }
            if (query_status != PageTableStatus::Succeeded) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            const UserVirtualMemoryStatus release_status =
                ReleaseMappedPages(address_space, page_address,
                                   page_address + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES, false);
            if (release_status != UserVirtualMemoryStatus::Succeeded) {
                return release_status;
            }
        }
    }
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus TrimUserFilePageCache() noexcept {
    const FilePageCacheStatus trim_status =
        user_file_page_cache.Trim(OS_KERNEL_USER_MEMORY_EMPTY_VALUE);
    return trim_status == FilePageCacheStatus::Succeeded ? UserVirtualMemoryStatus::Succeeded
           : trim_status == FilePageCacheStatus::EntryBusy
               ? UserVirtualMemoryStatus::PageReleaseFailed
               : UserVirtualMemoryStatus::Corrupt;
}

UserVirtualMemoryStatus ProtectUserSharedFileMappings(UserAddressSpace &address_space) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    const uint64_t area_count = address_space.virtual_memory_map.AreaCount();
    for (uint64_t area_index = OS_KERNEL_USER_MEMORY_EMPTY_VALUE; area_index < area_count;
         ++area_index) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.ReadAt(area_index, area) !=
            VirtualMemoryAreaStatus::Succeeded) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        if (area.kind != VirtualMemoryAreaKind::FileShared || !area.permissions.writable) {
            continue;
        }
        for (uint64_t page_address = area.begin_address; page_address < area.end_address;
             page_address += OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
            PageMapping mapping{};
            const PageTableStatus query_status =
                QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping);
            if (query_status == PageTableStatus::NotMapped) {
                continue;
            }
            if (query_status != PageTableStatus::Succeeded ||
                !mapping.permissions.user_accessible || mapping.permissions.copy_on_write) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            if (mapping.permissions.writable &&
                ReplaceUserPage(address_space.root_physical_address, page_address,
                                mapping.physical_address, false, mapping.permissions.executable,
                                false) != KernelUserPageStatus::Succeeded) {
                return UserVirtualMemoryStatus::PageMappingFailed;
            }
        }
    }
    return UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus SynchronizeUserFileMappings(UserAddressSpace &address_space,
                                                    const uint64_t address,
                                                    const uint64_t length_bytes,
                                                    const bool synchronous,
                                                    uint64_t &written_page_count) noexcept {
    written_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    uint64_t aligned_length_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        (address & OS_KERNEL_USER_MEMORY_PAGE_MASK) != OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
        !AlignUpToPage(length_bytes, aligned_length_bytes) ||
        address > UINT64_MAX - aligned_length_bytes) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    const uint64_t end_address = address + aligned_length_bytes;
    uint64_t current_address = address;
    bool shared_mapping_present = false;
    while (current_address < end_address) {
        VirtualMemoryArea area{};
        if (address_space.virtual_memory_map.FindContaining(current_address, area) !=
                VirtualMemoryAreaStatus::Succeeded ||
            (area.kind != VirtualMemoryAreaKind::FilePrivate &&
             area.kind != VirtualMemoryAreaKind::FileShared)) {
            return UserVirtualMemoryStatus::InvalidRange;
        }
        const uint64_t current_end_address = Minimum(end_address, area.end_address);
        if (area.kind == VirtualMemoryAreaKind::FileShared) {
            UserFileBackingDescriptor descriptor{};
            if (!ReadBackingDescriptor(area, descriptor)) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            const uint64_t area_offset_bytes = current_address - area.begin_address;
            const uint64_t range_length_bytes = current_end_address - current_address;
            if (area.backing_file_offset_bytes > UINT64_MAX - area_offset_bytes) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            const uint64_t first_file_offset_bytes =
                area.backing_file_offset_bytes + area_offset_bytes;
            if (range_length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE ||
                first_file_offset_bytes >
                    UINT64_MAX - (range_length_bytes - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT)) {
                return UserVirtualMemoryStatus::Corrupt;
            }
            const uint64_t first_page_index =
                first_file_offset_bytes / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            const uint64_t last_page_index = (first_file_offset_bytes + range_length_bytes -
                                              OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) /
                                             OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
            shared_mapping_present = true;
            if (synchronous) {
                uint64_t range_written_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
                const UserVirtualMemoryStatus writeback_status = WritebackUserFilePageCacheRange(
                    descriptor.identity, first_page_index, last_page_index, UINT64_MAX,
                    range_written_page_count);
                if (writeback_status != UserVirtualMemoryStatus::Succeeded ||
                    written_page_count > UINT64_MAX - range_written_page_count) {
                    return writeback_status == UserVirtualMemoryStatus::Succeeded
                               ? UserVirtualMemoryStatus::Corrupt
                               : writeback_status;
                }
                written_page_count += range_written_page_count;
            }
        }
        current_address = current_end_address;
    }
    return !synchronous && shared_mapping_present ? RequestUserFileWriteback()
                                                  : UserVirtualMemoryStatus::Succeeded;
}

UserVirtualMemoryStatus WritebackUserFilePageCache(const uint64_t maximum_page_count,
                                                   uint64_t &written_page_count) noexcept {
    const FilePageCacheStatus writeback_status =
        user_file_page_cache.Writeback(&user_file_backing_manager, WriteTrackedUserFileBackingPage,
                                       maximum_page_count, written_page_count);
    if (writeback_status == FilePageCacheStatus::Succeeded) {
        return user_file_backing_manager.ReleaseCleanWritebackFiles(&user_file_page_cache,
                                                                    FileWritebackIsRequired) ==
                       UserFileBackingStatus::Succeeded
                   ? UserVirtualMemoryStatus::Succeeded
                   : UserVirtualMemoryStatus::Corrupt;
    }
    return writeback_status == FilePageCacheStatus::SourceWriteFailed
               ? UserVirtualMemoryStatus::FileWriteFailed
               : UserVirtualMemoryStatus::Corrupt;
}

UserVirtualMemoryStatus WritebackUserFilePageCacheRange(const FileIdentity &identity,
                                                        const uint64_t first_page_index,
                                                        const uint64_t last_page_index,
                                                        const uint64_t maximum_page_count,
                                                        uint64_t &written_page_count) noexcept {
    const FilePageCacheStatus writeback_status = user_file_page_cache.WritebackFile(
        identity, first_page_index, last_page_index, &user_file_backing_manager,
        WriteTrackedUserFileBackingPage, maximum_page_count, written_page_count);
    if (writeback_status == FilePageCacheStatus::Succeeded) {
        return user_file_backing_manager.ReleaseCleanWritebackFiles(&user_file_page_cache,
                                                                    FileWritebackIsRequired) ==
                       UserFileBackingStatus::Succeeded
                   ? UserVirtualMemoryStatus::Succeeded
                   : UserVirtualMemoryStatus::Corrupt;
    }
    return writeback_status == FilePageCacheStatus::SourceWriteFailed
               ? UserVirtualMemoryStatus::FileWriteFailed
               : UserVirtualMemoryStatus::Corrupt;
}

UserVirtualMemoryStatus RequestUserFileWriteback() noexcept {
    const FilePageCacheStatus status = user_file_page_cache.RequestBackgroundWriteback();
    return status == FilePageCacheStatus::Succeeded ? UserVirtualMemoryStatus::Succeeded
           : status == FilePageCacheStatus::SourceWriteFailed
               ? UserVirtualMemoryStatus::FileWriteFailed
               : UserVirtualMemoryStatus::Corrupt;
}

bool UserFileWritebackWorkerRequested() noexcept {
    return user_file_page_cache.BackgroundWritebackRequested();
}

bool UserFileWritebackBackpressureRequired() noexcept {
    return user_file_page_cache.DirtyBackpressureRequired();
}

bool UserFileWritebackWorkerPaused() noexcept {
    return user_file_page_cache.BackgroundWritebackPaused();
}

UserVirtualMemoryStatus RunUserFileWritebackWorker(uint64_t &written_page_count) noexcept {
    return RunUserFileWritebackWorker(OS_KERNEL_USER_MEMORY_WRITEBACK_BATCH_PAGE_COUNT,
                                      written_page_count);
}

UserVirtualMemoryStatus RunUserFileWritebackWorker(const uint64_t maximum_page_count,
                                                   uint64_t &written_page_count) noexcept {
    written_page_count = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!UserFileWritebackWorkerRequested()) {
        return UserVirtualMemoryStatus::Succeeded;
    }
    if (user_file_page_cache_writeback_worker_run_count == UINT64_MAX) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    ++user_file_page_cache_writeback_worker_run_count;
    const UserVirtualMemoryStatus status =
        WritebackUserFilePageCache(maximum_page_count, written_page_count);
    if (status != UserVirtualMemoryStatus::Succeeded) {
        if (user_file_page_cache_writeback_worker_failure_count == UINT64_MAX) {
            return UserVirtualMemoryStatus::Corrupt;
        }
        ++user_file_page_cache_writeback_worker_failure_count;
        return status;
    }
    if (user_file_page_cache_writeback_worker_written_page_count >
        UINT64_MAX - written_page_count) {
        return UserVirtualMemoryStatus::Corrupt;
    }
    user_file_page_cache_writeback_worker_written_page_count += written_page_count;
    return UserVirtualMemoryStatus::Succeeded;
}

void RecordUserFileWritebackBackpressure() noexcept {
    if (user_file_page_cache_writeback_backpressure_count != UINT64_MAX) {
        ++user_file_page_cache_writeback_backpressure_count;
    }
}

bool RegisterUserFileWritebackDescription(const FileIdentity &identity,
                                          uint64_t &sampled_sequence) noexcept {
    return user_file_writeback_error_tracker.Register(identity, sampled_sequence) ==
           FileWritebackErrorTrackerStatus::Succeeded;
}

bool UnregisterUserFileWritebackDescription(const FileIdentity &identity) noexcept {
    return user_file_writeback_error_tracker.Unregister(identity) ==
           FileWritebackErrorTrackerStatus::Succeeded;
}

UserVirtualMemoryStatus CheckUserFileWritebackError(const FileIdentity &identity,
                                                    const uint64_t sampled_sequence,
                                                    uint64_t &current_sequence,
                                                    FileWritebackError &error) noexcept {
    return user_file_writeback_error_tracker.Check(identity, sampled_sequence, current_sequence,
                                                   error) ==
                   FileWritebackErrorTrackerStatus::Succeeded
               ? UserVirtualMemoryStatus::Succeeded
               : UserVirtualMemoryStatus::Corrupt;
}

FileWritebackErrorTrackerStatistics GetUserFileWritebackErrorTrackerStatistics() noexcept {
    return user_file_writeback_error_tracker.Statistics();
}

UserVirtualMemoryStatus SetProgramBreak(UserAddressSpace &address_space,
                                        const uint64_t requested_address,
                                        uint64_t &program_break_address) noexcept {
    if (address_space.virtual_memory_map.Validate() != VirtualMemoryAreaStatus::Succeeded) {
        return UserVirtualMemoryStatus::NotInitialized;
    }
    if (requested_address == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        program_break_address = address_space.program_break_address;
        return UserVirtualMemoryStatus::Succeeded;
    }
    if (requested_address < address_space.program_break_base_address ||
        requested_address > address_space.program_break_limit_address) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    uint64_t previous_page_end = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    uint64_t requested_page_end = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    if (!AlignUpToPage(address_space.program_break_address, previous_page_end) ||
        !AlignUpToPage(requested_address, requested_page_end)) {
        return UserVirtualMemoryStatus::InvalidRange;
    }

    if (requested_page_end > previous_page_end) {
        const VirtualMemoryAreaStatus insert_status =
            address_space.virtual_memory_map.Insert(VirtualMemoryArea{
                .begin_address = previous_page_end,
                .end_address = requested_page_end,
                .permissions =
                    {
                        .readable = true,
                        .writable = true,
                        .executable = false,
                    },
                .kind = VirtualMemoryAreaKind::ProgramBreak,
            });
        if (insert_status != VirtualMemoryAreaStatus::Succeeded) {
            return MapVirtualMemoryAreaStatus(insert_status);
        }
    } else if (requested_page_end < previous_page_end) {
        const VirtualMemoryAreaStatus remove_status = address_space.virtual_memory_map.Remove(
            requested_page_end, previous_page_end, VirtualMemoryAreaKind::ProgramBreak);
        if (remove_status != VirtualMemoryAreaStatus::Succeeded) {
            return MapVirtualMemoryAreaStatus(remove_status);
        }
        const UserVirtualMemoryStatus release_status =
            ReleaseMappedPages(address_space, requested_page_end, previous_page_end, true);
        if (release_status != UserVirtualMemoryStatus::Succeeded) {
            return release_status;
        }
    }
    address_space.program_break_address = requested_address;
    program_break_address = requested_address;
    return UserVirtualMemoryStatus::Succeeded;
}

os::abi::VirtualMemoryStatistics
GetUserVirtualMemoryStatistics(const UserAddressSpace &address_space) noexcept {
    const VirtualMemoryMapStatistics map_statistics = address_space.virtual_memory_map.Statistics();
    return os::abi::VirtualMemoryStatistics{
        .area_count = map_statistics.area_count,
        .virtual_page_count = map_statistics.mapped_page_count,
        .resident_page_count = address_space.mapped_page_count,
        .peak_resident_page_count = address_space.peak_mapped_page_count,
        .executable_image_page_count =
            CountKindPages(address_space, VirtualMemoryAreaKind::ExecutableImage),
        .anonymous_page_count = CountKindPages(address_space, VirtualMemoryAreaKind::Anonymous),
        .program_break_page_count =
            CountKindPages(address_space, VirtualMemoryAreaKind::ProgramBreak),
        .stack_reserved_page_count =
            CountKindPages(address_space, VirtualMemoryAreaKind::UserStack),
        .stack_resident_page_count = (address_space.stack_top_virtual_address -
                                      address_space.stack_committed_bottom_virtual_address) /
                                     OS_KERNEL_MEMORY_PAGE_SIZE_BYTES,
        .demand_page_fault_count = address_space.demand_page_fault_count,
        .stack_growth_page_fault_count = address_space.stack_growth_page_fault_count,
        .unmap_released_page_count = address_space.unmap_released_page_count,
        .page_table_reclaimed_frame_count = address_space.page_table_reclaimed_frame_count,
        .program_break_address = address_space.program_break_address,
        .file_private_page_count =
            CountKindPages(address_space, VirtualMemoryAreaKind::FilePrivate),
        .file_shared_page_count = CountKindPages(address_space, VirtualMemoryAreaKind::FileShared),
        .file_page_fault_count = address_space.file_page_fault_count,
        .page_cache_hit_count = address_space.page_cache_hit_count,
        .private_file_resident_page_count = address_space.private_file_resident_page_count,
        .shared_file_resident_page_count = address_space.shared_file_resident_page_count,
        .copy_on_write_page_count = address_space.copy_on_write_page_count,
        .copy_on_write_fault_count = address_space.copy_on_write_fault_count,
        .copy_on_write_copy_count = address_space.copy_on_write_copy_count,
        .copy_on_write_exclusive_restore_count =
            address_space.copy_on_write_exclusive_restore_count,
        .fork_clone_count = address_space.fork_clone_count,
    };
}

UserPageFaultStatus HandleUserPageFault(UserAddressSpace &address_space,
                                        const uint64_t fault_address, const uint64_t error_code,
                                        const uint64_t user_stack_pointer) noexcept {
    if ((error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_USER_BIT) ==
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserPageFaultStatus::NotUserFault;
    }
    if ((error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_RESERVED_BIT) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserPageFaultStatus::ReservedBitViolation;
    }
    const bool write_access = (error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_WRITE_BIT) !=
                              OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const bool instruction_access =
        (error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_INSTRUCTION_BIT) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    const uint64_t page_address = AlignDownToPage(fault_address);
    if ((error_code & OS_KERNEL_USER_MEMORY_PAGE_FAULT_PRESENT_BIT) !=
        OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        PageMapping mapping{};
        if (!write_access ||
            QueryAddressSpacePage(address_space.root_physical_address, page_address, mapping) !=
                PageTableStatus::Succeeded) {
            return UserPageFaultStatus::PresentPageViolation;
        }
        const UserVirtualMemoryStatus break_status =
            mapping.permissions.copy_on_write
                ? BreakCopyOnWritePage(address_space, page_address)
                : MakeSharedFilePageWritable(address_space, page_address);
        if (break_status == UserVirtualMemoryStatus::Succeeded) {
            return UserPageFaultStatus::Handled;
        }
        if (break_status == UserVirtualMemoryStatus::PageCacheExhausted) {
            return UserPageFaultStatus::PageCacheExhausted;
        }
        return break_status == UserVirtualMemoryStatus::PageAllocationFailed
                   ? UserPageFaultStatus::PageAllocationFailed
                   : UserPageFaultStatus::CopyOnWriteFailure;
    }
    VirtualMemoryArea area{};
    if (address_space.virtual_memory_map.FindContaining(page_address, area) !=
        VirtualMemoryAreaStatus::Succeeded) {
        return UserPageFaultStatus::AreaNotMapped;
    }
    if (!PermissionsAllow(area, write_access, instruction_access)) {
        return instruction_access ? UserPageFaultStatus::InstructionFetchViolation
                                  : UserPageFaultStatus::PermissionDenied;
    }

    bool swap_mapping = false;
    if (user_swap_attached) {
        uint64_t swap_slot_index = UINT64_MAX;
        const SwapManagerStatus swap_status = user_swap_manager.FindSlot(
            MakeSwapIdentity(address_space, page_address), swap_slot_index);
        static_cast<void>(swap_slot_index);
        if (swap_status == SwapManagerStatus::Succeeded) {
            swap_mapping = true;
        } else if (swap_status != SwapManagerStatus::MappingNotFound) {
            return UserPageFaultStatus::Corrupt;
        }
    }

    if (area.kind == VirtualMemoryAreaKind::UserStack && !swap_mapping) {
        if (page_address + OS_KERNEL_MEMORY_PAGE_SIZE_BYTES !=
                address_space.stack_committed_bottom_virtual_address ||
            page_address < OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS ||
            (fault_address > user_stack_pointer &&
             fault_address - user_stack_pointer > OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ||
            (user_stack_pointer > fault_address &&
             user_stack_pointer - fault_address > OS_KERNEL_USER_MEMORY_STACK_GROWTH_GAP_BYTES)) {
            return UserPageFaultStatus::InvalidStackGrowth;
        }
    }

    const UserVirtualMemoryStatus map_status = MapDemandPage(address_space, page_address, area);
    if (map_status == UserVirtualMemoryStatus::PageAllocationFailed) {
        return UserPageFaultStatus::PageAllocationFailed;
    }
    if (map_status == UserVirtualMemoryStatus::FileReadFailed) {
        return UserPageFaultStatus::FileReadFailed;
    }
    if (map_status == UserVirtualMemoryStatus::PageCacheExhausted) {
        return UserPageFaultStatus::PageCacheExhausted;
    }
    if (map_status == UserVirtualMemoryStatus::SwapReadFailed) {
        return UserPageFaultStatus::SwapReadFailed;
    }
    if (map_status == UserVirtualMemoryStatus::SwapCorrupt) {
        return UserPageFaultStatus::SwapCorrupt;
    }
    if (map_status != UserVirtualMemoryStatus::Succeeded) {
        return UserPageFaultStatus::PageMappingFailed;
    }
    ++address_space.demand_page_fault_count;
    if (area.kind == VirtualMemoryAreaKind::UserStack && !swap_mapping) {
        address_space.stack_committed_bottom_virtual_address = page_address;
        ++address_space.stack_growth_page_fault_count;
    }
    return UserPageFaultStatus::Handled;
}

UserVirtualMemoryStatus ResolveUserReturnMemory(UserAddressSpace &address_space,
                                                const uint64_t instruction_pointer,
                                                const uint64_t stack_pointer) noexcept {
    const uint64_t stack_probe_address =
        stack_pointer == OS_KERNEL_USER_MEMORY_EMPTY_VALUE
            ? OS_KERNEL_USER_MEMORY_EMPTY_VALUE
            : stack_pointer - OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT;
    if (!IsUserProgramVirtualAddressRange(instruction_pointer,
                                          OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT) ||
        !IsUserVirtualAddressRange(stack_probe_address, OS_KERNEL_USER_MEMORY_COUNTER_INCREMENT)) {
        return UserVirtualMemoryStatus::InvalidRange;
    }
    const uint64_t instruction_page = AlignDownToPage(instruction_pointer);
    const UserVirtualMemoryStatus resolve_status =
        ResolveNonStackPage(address_space, instruction_page, false, true);
    if (resolve_status != UserVirtualMemoryStatus::Succeeded) {
        return resolve_status;
    }
    PageMapping instruction_mapping{};
    PageMapping stack_mapping{};
    const uint64_t stack_page = AlignDownToPage(stack_probe_address);
    if (QueryAddressSpacePage(address_space.root_physical_address, stack_page, stack_mapping) ==
            PageTableStatus::Succeeded &&
        stack_mapping.permissions.copy_on_write &&
        BreakCopyOnWritePage(address_space, stack_page) != UserVirtualMemoryStatus::Succeeded) {
        return UserVirtualMemoryStatus::CopyOnWriteFailure;
    }
    if (QueryAddressSpacePage(address_space.root_physical_address, instruction_pointer,
                              instruction_mapping) != PageTableStatus::Succeeded ||
        !instruction_mapping.permissions.user_accessible ||
        !instruction_mapping.permissions.executable || instruction_mapping.permissions.writable ||
        QueryAddressSpacePage(address_space.root_physical_address, stack_probe_address,
                              stack_mapping) != PageTableStatus::Succeeded ||
        !stack_mapping.permissions.user_accessible || !stack_mapping.permissions.writable ||
        stack_mapping.permissions.executable) {
        return UserVirtualMemoryStatus::InvalidProtection;
    }
    return UserVirtualMemoryStatus::Succeeded;
}

void SetActiveUserAddressSpace(UserAddressSpace *const address_space) noexcept {
    active_user_address_space = address_space;
}

UserMemoryCopyStatus CopyToUserAddressSpace(UserAddressSpace &address_space,
                                            const uint64_t user_address,
                                            const uint64_t length_bytes,
                                            const uint8_t *const source,
                                            const uint64_t source_size_bytes) noexcept {
    if (source == nullptr) {
        return UserMemoryCopyStatus::NullSource;
    }
    if (length_bytes > source_size_bytes) {
        return UserMemoryCopyStatus::SourceTooSmall;
    }
    if (length_bytes == OS_KERNEL_USER_MEMORY_EMPTY_VALUE) {
        return UserMemoryCopyStatus::Succeeded;
    }
    if (!IsUserVirtualAddressRange(user_address, length_bytes)) {
        return UserMemoryCopyStatus::InvalidUserRange;
    }

    uint64_t copied_bytes = OS_KERNEL_USER_MEMORY_EMPTY_VALUE;
    while (copied_bytes < length_bytes) {
        const uint64_t current_user_address = user_address + copied_bytes;
        const uint64_t page_virtual_address = AlignDownToPage(current_user_address);
        const uint64_t page_offset = current_user_address & OS_KERNEL_USER_MEMORY_PAGE_MASK;
        const uint64_t chunk_bytes =
            Minimum(length_bytes - copied_bytes, OS_KERNEL_MEMORY_PAGE_SIZE_BYTES - page_offset);
        PageMapping mapping{};
        if (QueryAddressSpacePage(address_space.root_physical_address, page_virtual_address,
                                  mapping) != PageTableStatus::Succeeded) {
            return UserMemoryCopyStatus::PageNotMapped;
        }
        if (!mapping.permissions.user_accessible) {
            return UserMemoryCopyStatus::PageNotUserAccessible;
        }
        if (!mapping.permissions.writable) {
            if (!mapping.permissions.copy_on_write ||
                BreakCopyOnWritePage(address_space, page_virtual_address) !=
                    UserVirtualMemoryStatus::Succeeded ||
                QueryAddressSpacePage(address_space.root_physical_address, page_virtual_address,
                                      mapping) != PageTableStatus::Succeeded ||
                !mapping.permissions.writable) {
                return UserMemoryCopyStatus::PageNotWritable;
            }
        }
        uint8_t *const page = PhysicalPagePointer(mapping.physical_address);
        if (page == nullptr) {
            return UserMemoryCopyStatus::PageNotMapped;
        }
        CopyBytes(page + page_offset, source + copied_bytes, chunk_bytes);
        copied_bytes += chunk_bytes;
    }
    return UserMemoryCopyStatus::Succeeded;
}

UserMemoryCopyStatus CopyFromUser(const uint64_t user_address, const uint64_t length_bytes,
                                  uint8_t *const destination,
                                  const uint64_t destination_capacity_bytes) noexcept {
    if (destination == nullptr) {
        return UserMemoryCopyStatus::NullDestination;
    }
    if (length_bytes > destination_capacity_bytes) {
        return UserMemoryCopyStatus::DestinationTooSmall;
    }
    const UserMemoryCopyStatus validation_status =
        ValidateUserMemory(user_address, length_bytes, false);
    if (validation_status != UserMemoryCopyStatus::Succeeded) {
        return validation_status;
    }
    CopyBytes(destination, reinterpret_cast<const uint8_t *>(user_address), length_bytes);
    return UserMemoryCopyStatus::Succeeded;
}

UserMemoryCopyStatus ValidateUserWritableMemory(const uint64_t user_address,
                                                const uint64_t length_bytes) noexcept {
    return ValidateUserMemory(user_address, length_bytes, true);
}

UserMemoryCopyStatus CopyToUser(const uint64_t user_address, const uint64_t length_bytes,
                                const uint8_t *const source,
                                const uint64_t source_size_bytes) noexcept {
    if (source == nullptr) {
        return UserMemoryCopyStatus::NullSource;
    }
    if (length_bytes > source_size_bytes) {
        return UserMemoryCopyStatus::SourceTooSmall;
    }
    const UserMemoryCopyStatus validation_status =
        ValidateUserWritableMemory(user_address, length_bytes);
    if (validation_status != UserMemoryCopyStatus::Succeeded) {
        return validation_status;
    }
    CopyBytes(reinterpret_cast<uint8_t *>(user_address), source, length_bytes);
    return UserMemoryCopyStatus::Succeeded;
}

}
