#include "os/user/system_call.hpp"
#include "os/user/user_heap.hpp"

#include "os/abi/virtual_memory.hpp"

#include <stdint.h>

namespace {

constexpr char OS_USER_MEMORY_PROBE_STARTED_MESSAGE[] = "[OS][USER][VM] STARTED\r\n";
constexpr char OS_USER_MEMORY_PROBE_DEMAND_ZERO_MESSAGE[] =
    "[OS][USER][VM] DEMAND_ZERO_VERIFIED\r\n";
constexpr char OS_USER_MEMORY_PROBE_UNMAP_MESSAGE[] =
    "[OS][USER][VM] ANONYMOUS_UNMAP_RECLAIMED\r\n";
constexpr char OS_USER_MEMORY_PROBE_BREAK_MESSAGE[] = "[OS][USER][VM] PROGRAM_BREAK_VERIFIED\r\n";
constexpr char OS_USER_MEMORY_PROBE_STACK_MESSAGE[] = "[OS][USER][VM] STACK_GROWTH_VERIFIED\r\n";
constexpr char OS_USER_MEMORY_PROBE_HEAP_MESSAGE[] =
    "[OS][USER][VM] USER_HEAP_RANDOMIZED_VERIFIED\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_MESSAGE[] =
    "[OS][USER][VM] FILE_MAPPING_CACHE_VERIFIED\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_WRITEBACK_MESSAGE[] =
    "[OS][USER][VM] FILE_SHARED_WRITEBACK_VERIFIED\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_CREATE_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_CREATE_OR_WRITE\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_OPEN_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_OPEN_READ\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_PARTIAL_MAP_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_PARTIAL_MAP\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_PARTIAL_ACCESS_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_PARTIAL_ACCESS\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_MAP_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_SHARED_MAP\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_ACCESS_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_SHARED_ACCESS\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_INVALIDATION_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_INVALIDATION\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_PRIVATE_MAP_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_PRIVATE_MAP\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_PRIVATE_ACCESS_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_PRIVATE_ACCESS\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_PRIVATE_VERIFY_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_PRIVATE_VERIFY\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_WRITEBACK_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_SHARED_WRITEBACK\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_WRITE_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_SHARED_WRITE\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_FIRST_ALIAS_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_SHARED_FIRST_ALIAS\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_SECOND_ALIAS_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_SHARED_SECOND_ALIAS\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_SYNC_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_SHARED_SYNC\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_SHARED_CLOSE_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_SHARED_CLOSE\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_UNMAP_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_UNMAP\r\n";
constexpr char OS_USER_MEMORY_PROBE_FILE_STATISTICS_FAILURE_MESSAGE[] =
    "[OS][USER][VM][FAIL] FILE_STATISTICS\r\n";
constexpr char OS_USER_MEMORY_PROBE_COMPLETED_MESSAGE[] = "[OS][USER][VM] COMPLETED\r\n";

constexpr uint64_t OS_USER_MEMORY_PROBE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_ANONYMOUS_SIZE_BYTES = 32ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_ANONYMOUS_FAR_PAGE_INDEX = 4095ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_ANONYMOUS_SPLIT_PAGE_INDEX = 2048ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_BREAK_SIZE_BYTES = 2ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_STACK_BLOCK_SIZE_BYTES = 2048ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_STACK_RECURSION_DEPTH = 12ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_MINIMUM_STACK_GROWTH_COUNT = 4ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_HEAP_GROWTH_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_HEAP_SLOT_COUNT = 64ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_HEAP_ITERATION_COUNT = 5000ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_HEAP_MAXIMUM_ALLOCATION_BYTES = 8192ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_RANDOM_SEED = 0x56313848454150ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_RANDOM_SHIFT_FIRST = 12ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_RANDOM_SHIFT_SECOND = 25ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_RANDOM_SHIFT_THIRD = 27ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_RANDOM_MULTIPLIER = 0x2545F4914F6CDD1DULL;
constexpr uint8_t OS_USER_MEMORY_PROBE_FIRST_PATTERN = 0x5AU;
constexpr uint8_t OS_USER_MEMORY_PROBE_SECOND_PATTERN = 0xA5U;
constexpr uint64_t OS_USER_MEMORY_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_FILE_PARTIAL_MAP_SIZE_BYTES = 3000ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_FILE_TAIL_OFFSET_BYTES = 3500ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_TOUCHED_PAGE_COUNT = 2ULL;
constexpr uint64_t OS_USER_MEMORY_PROBE_SHARED_MAPPING_COUNT = 2ULL;
constexpr char OS_USER_MEMORY_PROBE_FILE_PATH[] = {
    '/', 'v', '1', '_', '9', '_', 'm', 'a', 'p', '.', 'b', 'i', 'n',
};
constexpr uint8_t OS_USER_MEMORY_PROBE_FILE_INITIAL_PATTERN = 0x31U;
constexpr uint8_t OS_USER_MEMORY_PROBE_FILE_UPDATED_PATTERN = 0x72U;
constexpr uint8_t OS_USER_MEMORY_PROBE_FILE_PRIVATE_PATTERN = 0xE4U;
constexpr uint8_t OS_USER_MEMORY_PROBE_FILE_SHARED_WRITE_PATTERN = 0xB6U;
constexpr int64_t OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE = 0LL;
constexpr int64_t OS_USER_MEMORY_PROBE_FAILURE_EXIT_CODE = 1LL;
constexpr int64_t OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT = -1LL;

struct HeapAllocation final {
    uint8_t *address;
    uint64_t size_bytes;
};

uint8_t file_initial_data[OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES];
uint8_t file_updated_data[OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES];

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message, MessageSizeBytes -
                                           OS_USER_MEMORY_PROBE_STRING_TERMINATOR_SIZE_BYTES) >
           OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT;
}

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool ReportFailure(
    const char (&message)[MessageSizeBytes]) noexcept {
    static_cast<void>(WriteMessage(message));
    return false;
}

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state >> OS_USER_MEMORY_PROBE_RANDOM_SHIFT_FIRST;
    state ^= state << OS_USER_MEMORY_PROBE_RANDOM_SHIFT_SECOND;
    state ^= state >> OS_USER_MEMORY_PROBE_RANDOM_SHIFT_THIRD;
    state *= OS_USER_MEMORY_PROBE_RANDOM_MULTIPLIER;
    return state;
}

[[nodiscard]] int64_t HeapProgramBreak(void *const context,
                                       const uint64_t requested_address) noexcept {
    static_cast<void>(context);
    return os::user::SetProgramBreak(requested_address);
}

[[gnu::noinline]] void TouchStack(const uint64_t depth, volatile uint64_t &checksum) noexcept {
    volatile uint8_t stack_block[OS_USER_MEMORY_PROBE_STACK_BLOCK_SIZE_BYTES]{};
    const uint64_t selected_index = depth % OS_USER_MEMORY_PROBE_STACK_BLOCK_SIZE_BYTES;
    stack_block[selected_index] = static_cast<uint8_t>(depth + OS_USER_MEMORY_PROBE_SINGLE_UNIT);
    checksum += stack_block[selected_index];
    if (depth != OS_USER_MEMORY_PROBE_EMPTY_VALUE) {
        TouchStack(depth - OS_USER_MEMORY_PROBE_SINGLE_UNIT, checksum);
    }
    checksum += stack_block[selected_index];
}

[[nodiscard]] bool VerifyAnonymousMemory() noexcept {
    os::abi::VirtualMemoryStatistics before_map{};
    if (os::user::GetVirtualMemoryStatistics(before_map) !=
        OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return false;
    }
    const int64_t map_result = os::user::MapAnonymousMemory(
        os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS, OS_USER_MEMORY_PROBE_ANONYMOUS_SIZE_BYTES,
        os::abi::OS_ABI_MEMORY_PROTECTION_READ | os::abi::OS_ABI_MEMORY_PROTECTION_WRITE,
        os::abi::OS_ABI_MEMORY_MAP_NO_FLAGS);
    if (map_result <= OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT) {
        return false;
    }
    const uint64_t mapped_address = static_cast<uint64_t>(map_result);
    os::abi::VirtualMemoryStatistics after_map{};
    if (os::user::GetVirtualMemoryStatistics(after_map) != OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        after_map.resident_page_count != before_map.resident_page_count ||
        after_map.anonymous_page_count !=
            OS_USER_MEMORY_PROBE_ANONYMOUS_SIZE_BYTES / os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES ||
        os::user::MapAnonymousMemory(mapped_address, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES,
                                     os::abi::OS_ABI_MEMORY_PROTECTION_READ,
                                     os::abi::OS_ABI_MEMORY_MAP_FIXED) !=
            os::abi::OS_ABI_SYSTEM_CALL_RESULT_ADDRESS_IN_USE) {
        return false;
    }

    volatile uint8_t *const first_byte = reinterpret_cast<volatile uint8_t *>(mapped_address);
    volatile uint8_t *const far_byte = reinterpret_cast<volatile uint8_t *>(
        mapped_address +
        OS_USER_MEMORY_PROBE_ANONYMOUS_FAR_PAGE_INDEX * os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES);
    if (*first_byte != OS_USER_MEMORY_PROBE_EMPTY_VALUE ||
        *far_byte != OS_USER_MEMORY_PROBE_EMPTY_VALUE) {
        return false;
    }
    *first_byte = OS_USER_MEMORY_PROBE_FIRST_PATTERN;
    *far_byte = OS_USER_MEMORY_PROBE_SECOND_PATTERN;
    if (*first_byte != OS_USER_MEMORY_PROBE_FIRST_PATTERN ||
        *far_byte != OS_USER_MEMORY_PROBE_SECOND_PATTERN ||
        !WriteMessage(OS_USER_MEMORY_PROBE_DEMAND_ZERO_MESSAGE)) {
        return false;
    }

    const uint64_t split_address =
        mapped_address +
        OS_USER_MEMORY_PROBE_ANONYMOUS_SPLIT_PAGE_INDEX * os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
    if (os::user::UnmapMemory(split_address, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        os::user::MapAnonymousMemory(
            split_address, os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES,
            os::abi::OS_ABI_MEMORY_PROTECTION_READ | os::abi::OS_ABI_MEMORY_PROTECTION_WRITE,
            os::abi::OS_ABI_MEMORY_MAP_FIXED) != static_cast<int64_t>(split_address) ||
        os::user::UnmapMemory(mapped_address, OS_USER_MEMORY_PROBE_ANONYMOUS_SIZE_BYTES) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return false;
    }
    os::abi::VirtualMemoryStatistics after_unmap{};
    return os::user::GetVirtualMemoryStatistics(after_unmap) ==
               OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE &&
           after_unmap.anonymous_page_count == OS_USER_MEMORY_PROBE_EMPTY_VALUE &&
           after_unmap.unmap_released_page_count >=
               before_map.unmap_released_page_count +
                   OS_USER_MEMORY_PROBE_TOUCHED_PAGE_COUNT &&
           after_unmap.page_table_reclaimed_frame_count >
               before_map.page_table_reclaimed_frame_count &&
           WriteMessage(OS_USER_MEMORY_PROBE_UNMAP_MESSAGE);
}

[[nodiscard]] bool VerifyProgramBreak() noexcept {
    const int64_t base_result = os::user::SetProgramBreak(OS_USER_MEMORY_PROBE_EMPTY_VALUE);
    if (base_result <= OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT) {
        return false;
    }
    const uint64_t base_address = static_cast<uint64_t>(base_result);
    os::abi::VirtualMemoryStatistics before_growth{};
    if (os::user::GetVirtualMemoryStatistics(before_growth) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        os::user::SetProgramBreak(base_address + OS_USER_MEMORY_PROBE_BREAK_SIZE_BYTES) !=
            static_cast<int64_t>(base_address + OS_USER_MEMORY_PROBE_BREAK_SIZE_BYTES)) {
        return false;
    }
    os::abi::VirtualMemoryStatistics after_growth{};
    if (os::user::GetVirtualMemoryStatistics(after_growth) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        after_growth.resident_page_count != before_growth.resident_page_count ||
        after_growth.program_break_page_count !=
            OS_USER_MEMORY_PROBE_BREAK_SIZE_BYTES / os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    volatile uint8_t *const first_byte = reinterpret_cast<volatile uint8_t *>(base_address);
    volatile uint8_t *const last_page_byte =
        reinterpret_cast<volatile uint8_t *>(base_address + OS_USER_MEMORY_PROBE_BREAK_SIZE_BYTES -
                                             os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES);
    if (*first_byte != OS_USER_MEMORY_PROBE_EMPTY_VALUE ||
        *last_page_byte != OS_USER_MEMORY_PROBE_EMPTY_VALUE) {
        return false;
    }
    *first_byte = OS_USER_MEMORY_PROBE_SECOND_PATTERN;
    *last_page_byte = OS_USER_MEMORY_PROBE_FIRST_PATTERN;
    if (*first_byte != OS_USER_MEMORY_PROBE_SECOND_PATTERN ||
        *last_page_byte != OS_USER_MEMORY_PROBE_FIRST_PATTERN ||
        os::user::SetProgramBreak(base_address) != static_cast<int64_t>(base_address)) {
        return false;
    }
    os::abi::VirtualMemoryStatistics after_shrink{};
    return os::user::GetVirtualMemoryStatistics(after_shrink) ==
               OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE &&
           after_shrink.program_break_page_count == OS_USER_MEMORY_PROBE_EMPTY_VALUE &&
           after_shrink.unmap_released_page_count >=
               before_growth.unmap_released_page_count +
                   OS_USER_MEMORY_PROBE_TOUCHED_PAGE_COUNT &&
           WriteMessage(OS_USER_MEMORY_PROBE_BREAK_MESSAGE);
}

[[nodiscard]] bool VerifyStackGrowth() noexcept {
    os::abi::VirtualMemoryStatistics before_growth{};
    if (os::user::GetVirtualMemoryStatistics(before_growth) !=
        OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return false;
    }
    volatile uint64_t checksum = OS_USER_MEMORY_PROBE_EMPTY_VALUE;
    TouchStack(OS_USER_MEMORY_PROBE_STACK_RECURSION_DEPTH, checksum);
    os::abi::VirtualMemoryStatistics after_growth{};
    return checksum != OS_USER_MEMORY_PROBE_EMPTY_VALUE &&
           os::user::GetVirtualMemoryStatistics(after_growth) ==
               OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE &&
           after_growth.stack_growth_page_fault_count >=
               before_growth.stack_growth_page_fault_count +
                   OS_USER_MEMORY_PROBE_MINIMUM_STACK_GROWTH_COUNT &&
           after_growth.stack_resident_page_count > before_growth.stack_resident_page_count &&
           WriteMessage(OS_USER_MEMORY_PROBE_STACK_MESSAGE);
}

[[nodiscard]] bool VerifyUserHeap() noexcept {
    os::user::UserHeap heap{};
    if (heap.Initialize(os::user::UserHeapConfiguration{
            .context = nullptr,
            .program_break_operation = HeapProgramBreak,
            .maximum_capacity_bytes = os::abi::OS_ABI_USER_HEAP_MAXIMUM_SIZE_BYTES,
            .page_size_bytes = os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES,
            .growth_quantum_bytes = OS_USER_MEMORY_PROBE_HEAP_GROWTH_BYTES,
        }) != os::user::UserHeapStatus::Succeeded) {
        return false;
    }
    HeapAllocation allocations[OS_USER_MEMORY_PROBE_HEAP_SLOT_COUNT]{};
    uint64_t random_state = OS_USER_MEMORY_PROBE_RANDOM_SEED;
    for (uint64_t iteration = OS_USER_MEMORY_PROBE_EMPTY_VALUE;
         iteration < OS_USER_MEMORY_PROBE_HEAP_ITERATION_COUNT; ++iteration) {
        const uint64_t slot_index = NextRandom(random_state) % OS_USER_MEMORY_PROBE_HEAP_SLOT_COUNT;
        HeapAllocation &allocation = allocations[slot_index];
        if (allocation.address == nullptr) {
            const uint64_t size_bytes =
                NextRandom(random_state) % OS_USER_MEMORY_PROBE_HEAP_MAXIMUM_ALLOCATION_BYTES +
                OS_USER_MEMORY_PROBE_SINGLE_UNIT;
            void *new_allocation = nullptr;
            if (heap.Allocate(size_bytes, new_allocation) != os::user::UserHeapStatus::Succeeded) {
                return false;
            }
            allocation = HeapAllocation{
                .address = static_cast<uint8_t *>(new_allocation),
                .size_bytes = size_bytes,
            };
            allocation.address[OS_USER_MEMORY_PROBE_EMPTY_VALUE] =
                OS_USER_MEMORY_PROBE_FIRST_PATTERN;
            allocation.address[size_bytes - OS_USER_MEMORY_PROBE_SINGLE_UNIT] =
                OS_USER_MEMORY_PROBE_SECOND_PATTERN;
        } else {
            const uint8_t expected_first = allocation.size_bytes == OS_USER_MEMORY_PROBE_SINGLE_UNIT
                                               ? OS_USER_MEMORY_PROBE_SECOND_PATTERN
                                               : OS_USER_MEMORY_PROBE_FIRST_PATTERN;
            if (allocation.address[OS_USER_MEMORY_PROBE_EMPTY_VALUE] != expected_first ||
                allocation.address[allocation.size_bytes - OS_USER_MEMORY_PROBE_SINGLE_UNIT] !=
                    OS_USER_MEMORY_PROBE_SECOND_PATTERN ||
                heap.Release(allocation.address) != os::user::UserHeapStatus::Succeeded) {
                return false;
            }
            allocation = HeapAllocation{};
        }
        if (iteration % OS_USER_MEMORY_PROBE_HEAP_SLOT_COUNT == OS_USER_MEMORY_PROBE_EMPTY_VALUE &&
            heap.Validate() != os::user::UserHeapStatus::Succeeded) {
            return false;
        }
    }
    for (uint64_t slot_index = OS_USER_MEMORY_PROBE_EMPTY_VALUE;
         slot_index < OS_USER_MEMORY_PROBE_HEAP_SLOT_COUNT; ++slot_index) {
        if (allocations[slot_index].address != nullptr &&
            heap.Release(allocations[slot_index].address) != os::user::UserHeapStatus::Succeeded) {
            return false;
        }
    }
    void *exhaustion_output = reinterpret_cast<void *>(UINT64_MAX);
    return heap.Allocate(os::abi::OS_ABI_USER_HEAP_MAXIMUM_SIZE_BYTES, exhaustion_output) ==
               os::user::UserHeapStatus::CapacityExhausted &&
           exhaustion_output == reinterpret_cast<void *>(UINT64_MAX) &&
           heap.Validate() == os::user::UserHeapStatus::Succeeded &&
           heap.Statistics().active_allocation_count == OS_USER_MEMORY_PROBE_EMPTY_VALUE &&
           heap.Statistics().successful_allocation_count == heap.Statistics().release_count &&
           WriteMessage(OS_USER_MEMORY_PROBE_HEAP_MESSAGE);
}

[[nodiscard]] bool VerifyFileMappings() noexcept {
    for (uint64_t byte_index = OS_USER_MEMORY_PROBE_EMPTY_VALUE;
         byte_index < OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES;
         ++byte_index) {
        file_initial_data[byte_index] =
            OS_USER_MEMORY_PROBE_FILE_INITIAL_PATTERN;
        file_updated_data[byte_index] =
            OS_USER_MEMORY_PROBE_FILE_UPDATED_PATTERN;
    }
    int64_t descriptor = os::user::OpenFile(
        OS_USER_MEMORY_PROBE_FILE_PATH,
        sizeof(OS_USER_MEMORY_PROBE_FILE_PATH),
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG |
            os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG |
            os::abi::OS_ABI_FILE_OPEN_CREATE_FLAG |
            os::abi::OS_ABI_FILE_OPEN_TRUNCATE_FLAG);
    if (descriptor < OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        os::user::WriteDescriptor(
            static_cast<uint64_t>(descriptor), file_initial_data,
            sizeof(file_initial_data)) !=
            static_cast<int64_t>(sizeof(file_initial_data)) ||
        os::user::CloseFile(static_cast<uint64_t>(descriptor)) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_CREATE_FAILURE_MESSAGE);
    }

    descriptor = os::user::OpenFile(
        OS_USER_MEMORY_PROBE_FILE_PATH,
        sizeof(OS_USER_MEMORY_PROBE_FILE_PATH),
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG |
            os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG);
    if (descriptor < OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_OPEN_FAILURE_MESSAGE);
    }
    const os::abi::FileMemoryMapRequest shared_request{
        .requested_address =
            os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS,
        .length_bytes =
            OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES,
        .protection_flags =
            os::abi::OS_ABI_MEMORY_PROTECTION_READ,
        .map_flags = os::abi::OS_ABI_MEMORY_MAP_SHARED,
        .file_descriptor = static_cast<uint64_t>(descriptor),
        .file_offset_bytes = OS_USER_MEMORY_PROBE_EMPTY_VALUE,
    };
    os::abi::FileMemoryMapRequest partial_request = shared_request;
    partial_request.length_bytes =
        OS_USER_MEMORY_PROBE_FILE_PARTIAL_MAP_SIZE_BYTES;
    const int64_t partial_result =
        os::user::MapFileMemory(partial_request);
    if (partial_result <= OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_PARTIAL_MAP_FAILURE_MESSAGE);
    }
    volatile uint8_t *const partial_mapping =
        reinterpret_cast<volatile uint8_t *>(
            static_cast<uint64_t>(partial_result));
    if (partial_mapping[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
            OS_USER_MEMORY_PROBE_FILE_INITIAL_PATTERN ||
        partial_mapping[OS_USER_MEMORY_PROBE_FILE_TAIL_OFFSET_BYTES] !=
            OS_USER_MEMORY_PROBE_EMPTY_VALUE ||
        os::user::UnmapMemory(
            static_cast<uint64_t>(partial_result),
            OS_USER_MEMORY_PROBE_FILE_PARTIAL_MAP_SIZE_BYTES) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_PARTIAL_ACCESS_FAILURE_MESSAGE);
    }
    const int64_t first_shared_result =
        os::user::MapFileMemory(shared_request);
    const int64_t second_shared_result =
        os::user::MapFileMemory(shared_request);
    if (first_shared_result <= OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT ||
        second_shared_result <= OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_SHARED_MAP_FAILURE_MESSAGE);
    }
    volatile uint8_t *const first_shared =
        reinterpret_cast<volatile uint8_t *>(
            static_cast<uint64_t>(first_shared_result));
    volatile uint8_t *const second_shared =
        reinterpret_cast<volatile uint8_t *>(
            static_cast<uint64_t>(second_shared_result));
    os::abi::VirtualMemoryStatistics after_shared_faults{};
    if (first_shared[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
            OS_USER_MEMORY_PROBE_FILE_INITIAL_PATTERN ||
        second_shared[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
            OS_USER_MEMORY_PROBE_FILE_INITIAL_PATTERN ||
        os::user::GetVirtualMemoryStatistics(after_shared_faults) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        after_shared_faults.file_shared_page_count !=
            OS_USER_MEMORY_PROBE_SHARED_MAPPING_COUNT *
                OS_USER_MEMORY_PROBE_SINGLE_UNIT ||
        after_shared_faults.page_cache_hit_count <
            OS_USER_MEMORY_PROBE_SINGLE_UNIT ||
        os::user::CloseFile(static_cast<uint64_t>(descriptor)) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_SHARED_ACCESS_FAILURE_MESSAGE);
    }

    descriptor = os::user::OpenFile(
        OS_USER_MEMORY_PROBE_FILE_PATH,
        sizeof(OS_USER_MEMORY_PROBE_FILE_PATH),
        os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG);
    if (descriptor < OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        os::user::WriteDescriptor(
            static_cast<uint64_t>(descriptor), file_updated_data,
            sizeof(file_updated_data)) !=
            static_cast<int64_t>(sizeof(file_updated_data)) ||
        os::user::CloseFile(static_cast<uint64_t>(descriptor)) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        first_shared[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
            OS_USER_MEMORY_PROBE_FILE_UPDATED_PATTERN ||
        second_shared[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
            OS_USER_MEMORY_PROBE_FILE_UPDATED_PATTERN) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_INVALIDATION_FAILURE_MESSAGE);
    }

    descriptor = os::user::OpenFile(
        OS_USER_MEMORY_PROBE_FILE_PATH,
        sizeof(OS_USER_MEMORY_PROBE_FILE_PATH),
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG |
            os::abi::OS_ABI_FILE_OPEN_WRITE_FLAG);
    if (descriptor < OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return false;
    }
    os::abi::FileMemoryMapRequest private_request = shared_request;
    private_request.protection_flags =
        os::abi::OS_ABI_MEMORY_PROTECTION_READ |
        os::abi::OS_ABI_MEMORY_PROTECTION_WRITE;
    private_request.map_flags = os::abi::OS_ABI_MEMORY_MAP_PRIVATE;
    private_request.file_descriptor =
        static_cast<uint64_t>(descriptor);
    const int64_t private_result =
        os::user::MapFileMemory(private_request);
    os::abi::FileMemoryMapRequest writable_shared_request =
        private_request;
    writable_shared_request.map_flags =
        os::abi::OS_ABI_MEMORY_MAP_SHARED;
    const int64_t writable_shared_result =
        os::user::MapFileMemory(writable_shared_request);
    if (private_result <= OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT ||
        writable_shared_result <=
            OS_USER_MEMORY_PROBE_FIRST_ERROR_RESULT) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_PRIVATE_MAP_FAILURE_MESSAGE);
    }
    volatile uint8_t *const private_mapping =
        reinterpret_cast<volatile uint8_t *>(
            static_cast<uint64_t>(private_result));
    if (private_mapping[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
        OS_USER_MEMORY_PROBE_FILE_UPDATED_PATTERN) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_PRIVATE_ACCESS_FAILURE_MESSAGE);
    }
    private_mapping[OS_USER_MEMORY_PROBE_EMPTY_VALUE] =
        OS_USER_MEMORY_PROBE_FILE_PRIVATE_PATTERN;
    volatile uint8_t *const writable_shared_mapping =
        reinterpret_cast<volatile uint8_t *>(
            static_cast<uint64_t>(writable_shared_result));
    if (writable_shared_mapping[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
        OS_USER_MEMORY_PROBE_FILE_UPDATED_PATTERN) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_SHARED_WRITEBACK_FAILURE_MESSAGE);
    }
    writable_shared_mapping[OS_USER_MEMORY_PROBE_EMPTY_VALUE] =
        OS_USER_MEMORY_PROBE_FILE_SHARED_WRITE_PATTERN;
    if (writable_shared_mapping[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
        OS_USER_MEMORY_PROBE_FILE_SHARED_WRITE_PATTERN) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_SHARED_WRITE_FAILURE_MESSAGE);
    }
    if (first_shared[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
        OS_USER_MEMORY_PROBE_FILE_SHARED_WRITE_PATTERN) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_SHARED_FIRST_ALIAS_FAILURE_MESSAGE);
    }
    if (second_shared[OS_USER_MEMORY_PROBE_EMPTY_VALUE] !=
        OS_USER_MEMORY_PROBE_FILE_SHARED_WRITE_PATTERN) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_SHARED_SECOND_ALIAS_FAILURE_MESSAGE);
    }
    if (os::user::SyncFileSystem() !=
        OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_SHARED_SYNC_FAILURE_MESSAGE);
    }
    if (os::user::CloseFile(static_cast<uint64_t>(descriptor)) !=
        OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_SHARED_CLOSE_FAILURE_MESSAGE);
    }
    if (!WriteMessage(
            OS_USER_MEMORY_PROBE_FILE_SHARED_WRITEBACK_MESSAGE)) {
        return false;
    }

    descriptor = os::user::OpenFile(
        OS_USER_MEMORY_PROBE_FILE_PATH,
        sizeof(OS_USER_MEMORY_PROBE_FILE_PATH),
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    uint8_t observed_file_byte = OS_USER_MEMORY_PROBE_EMPTY_VALUE;
    const bool private_not_written_back =
        descriptor >= OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE &&
        os::user::ReadFile(static_cast<uint64_t>(descriptor),
                           &observed_file_byte,
                           sizeof(observed_file_byte)) ==
            static_cast<int64_t>(sizeof(observed_file_byte)) &&
        observed_file_byte ==
            OS_USER_MEMORY_PROBE_FILE_SHARED_WRITE_PATTERN &&
        private_mapping[OS_USER_MEMORY_PROBE_EMPTY_VALUE] ==
            OS_USER_MEMORY_PROBE_FILE_PRIVATE_PATTERN &&
        os::user::CloseFile(static_cast<uint64_t>(descriptor)) ==
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE;
    if (!private_not_written_back ||
        os::user::UnmapMemory(
            static_cast<uint64_t>(first_shared_result),
            OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        os::user::UnmapMemory(
            static_cast<uint64_t>(second_shared_result),
            OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        os::user::UnmapMemory(
            static_cast<uint64_t>(private_result),
            OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        os::user::UnmapMemory(
            static_cast<uint64_t>(writable_shared_result),
            OS_USER_MEMORY_PROBE_FILE_PAGE_DATA_SIZE_BYTES) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE) {
        if (!private_not_written_back) {
            return ReportFailure(
                OS_USER_MEMORY_PROBE_FILE_PRIVATE_VERIFY_FAILURE_MESSAGE);
        }
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_UNMAP_FAILURE_MESSAGE);
    }
    os::abi::VirtualMemoryStatistics after_unmap{};
    if (os::user::GetVirtualMemoryStatistics(after_unmap) !=
            OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE ||
        after_unmap.file_private_page_count !=
            OS_USER_MEMORY_PROBE_EMPTY_VALUE ||
        after_unmap.file_shared_page_count !=
            OS_USER_MEMORY_PROBE_EMPTY_VALUE) {
        return ReportFailure(
            OS_USER_MEMORY_PROBE_FILE_STATISTICS_FAILURE_MESSAGE);
    }
    return WriteMessage(OS_USER_MEMORY_PROBE_FILE_MESSAGE);
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]]
void OsUserEntry() noexcept {
    if (!WriteMessage(OS_USER_MEMORY_PROBE_STARTED_MESSAGE) || !VerifyAnonymousMemory() ||
        !VerifyProgramBreak() || !VerifyStackGrowth() || !VerifyUserHeap() ||
        !VerifyFileMappings() ||
        !WriteMessage(OS_USER_MEMORY_PROBE_COMPLETED_MESSAGE)) {
        os::user::ExitProcess(OS_USER_MEMORY_PROBE_FAILURE_EXIT_CODE);
    }
    os::user::ExitProcess(OS_USER_MEMORY_PROBE_SUCCESS_EXIT_CODE);
}
