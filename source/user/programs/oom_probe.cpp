#include <os/user/system_call.hpp>

#include <os/abi/signal.hpp>
#include <os/abi/terminal.hpp>
#include <os/abi/virtual_memory.hpp>

#include <stdint.h>

namespace {

constexpr char OS_USER_OOM_PROBE_STARTED_MESSAGE[] = "[OS][USER][OOM] STARTED\r\n";
constexpr char OS_USER_OOM_PROBE_VICTIM_READY_MESSAGE[] = "[OS][USER][OOM] VICTIM_READY\r\n";
constexpr char OS_USER_OOM_PROBE_VICTIM_KILLED_MESSAGE[] = "[OS][USER][OOM] VICTIM_KILLED\r\n";
constexpr char OS_USER_OOM_PROBE_COMPLETED_MESSAGE[] = "[OS][USER][OOM] COMPLETED\r\n";
constexpr char OS_USER_OOM_PROBE_FAILURE_MESSAGE[] = "[OS][USER][OOM][FAIL] PRESSURE\r\n";
constexpr char OS_USER_OOM_PROBE_OPEN_FAILURE_MESSAGE[] = "[OS][USER][OOM][FAIL] MEMINFO_OPEN\r\n";
constexpr char OS_USER_OOM_PROBE_READ_FAILURE_MESSAGE[] = "[OS][USER][OOM][FAIL] MEMINFO_READ\r\n";
constexpr char OS_USER_OOM_PROBE_PARSE_ALLOCATED_FAILURE_MESSAGE[] =
    "[OS][USER][OOM][FAIL] MEMINFO_ALLOCATED\r\n";
constexpr char OS_USER_OOM_PROBE_PARSE_LIMIT_FAILURE_MESSAGE[] =
    "[OS][USER][OOM][FAIL] MEMINFO_LIMIT\r\n";
constexpr char OS_USER_OOM_PROBE_RANGE_FAILURE_MESSAGE[] = "[OS][USER][OOM][FAIL] MEMINFO_RANGE\r\n";
constexpr char OS_USER_OOM_PROBE_MEMORY_INFORMATION_PATH[] = "/proc/meminfo";
constexpr char OS_USER_OOM_PROBE_ALLOCATED_BYTES_PREFIX[] = "allocated_bytes ";
constexpr char OS_USER_OOM_PROBE_RESIDENT_LIMIT_BYTES_PREFIX[] = "resident_limit_bytes ";
constexpr uint64_t OS_USER_OOM_PROBE_FIRST_PAGE_INDEX = 0ULL;
constexpr uint64_t OS_USER_OOM_PROBE_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_USER_OOM_PROBE_MEMORY_INFORMATION_CAPACITY_BYTES =
    os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_FILE_TRANSFER_SIZE_BYTES;
constexpr uint64_t OS_USER_OOM_PROBE_VICTIM_SAFETY_PAGE_COUNT = 512ULL;
constexpr uint64_t OS_USER_OOM_PROBE_VICTIM_ADVANTAGE_PAGE_COUNT = 64ULL;
constexpr uint64_t OS_USER_OOM_PROBE_PARENT_DELAY_NANOSECONDS =
    3ULL * 1000ULL * 1000ULL * 1000ULL;
constexpr uint64_t OS_USER_OOM_PROBE_VICTIM_DELAY_NANOSECONDS =
    5ULL * OS_USER_OOM_PROBE_PARENT_DELAY_NANOSECONDS;
constexpr uint64_t OS_USER_OOM_PROBE_WAIT_FLAGS =
    os::abi::OS_ABI_PROCESS_WAIT_EXITED_FLAG | os::abi::OS_ABI_PROCESS_WAIT_NO_HANG_FLAG;
constexpr uint8_t OS_USER_OOM_PROBE_VICTIM_PATTERN = 0x5AU;
constexpr uint8_t OS_USER_OOM_PROBE_PRESSURE_PATTERN = 0xA5U;
constexpr int64_t OS_USER_OOM_PROBE_CHILD_RESULT = 0LL;
constexpr int64_t OS_USER_OOM_PROBE_SUCCESS_RESULT = 0LL;
constexpr int64_t OS_USER_OOM_PROBE_FAILURE_RESULT = 1LL;
constexpr int64_t OS_USER_OOM_PROBE_FIRST_ERROR_RESULT = -1LL;

uint8_t memory_information[OS_USER_OOM_PROBE_MEMORY_INFORMATION_CAPACITY_BYTES];
uint64_t victim_page_count;
uint64_t pressure_page_count;

template <uint64_t MessageSizeBytes>
[[nodiscard]] bool WriteMessage(const char (&message)[MessageSizeBytes]) noexcept {
    return os::user::WriteLog(message, MessageSizeBytes - 1ULL) >
           OS_USER_OOM_PROBE_FIRST_ERROR_RESULT;
}

[[noreturn]] void Fail() noexcept {
    static_cast<void>(WriteMessage(OS_USER_OOM_PROBE_FAILURE_MESSAGE));
    os::user::ExitProcess(OS_USER_OOM_PROBE_FAILURE_RESULT);
}

[[nodiscard]] int64_t MapWritableAnonymous(const uint64_t size_bytes) noexcept {
    return os::user::MapAnonymousMemory(
        os::abi::OS_ABI_MEMORY_MAP_AUTOMATIC_ADDRESS, size_bytes,
        os::abi::OS_ABI_MEMORY_PROTECTION_READ | os::abi::OS_ABI_MEMORY_PROTECTION_WRITE,
        os::abi::OS_ABI_MEMORY_MAP_NO_FLAGS);
}

template <uint64_t PrefixSizeBytes>
[[nodiscard]] bool ParseMemoryInformationValue(const uint8_t *const buffer,
                                               const uint64_t length_bytes,
                                               const char (&prefix)[PrefixSizeBytes],
                                               uint64_t &value) noexcept {
    const uint64_t prefix_length_bytes =
        PrefixSizeBytes - OS_USER_OOM_PROBE_STRING_TERMINATOR_SIZE_BYTES;
    value = 0ULL;
    for (uint64_t offset = OS_USER_OOM_PROBE_FIRST_PAGE_INDEX;
         offset + prefix_length_bytes < length_bytes; ++offset) {
        bool prefix_matches = true;
        for (uint64_t prefix_index = OS_USER_OOM_PROBE_FIRST_PAGE_INDEX;
             prefix_index < prefix_length_bytes; ++prefix_index) {
            if (buffer[offset + prefix_index] != static_cast<uint8_t>(prefix[prefix_index])) {
                prefix_matches = false;
                break;
            }
        }
        if (!prefix_matches) {
            continue;
        }
        uint64_t cursor = offset + prefix_length_bytes;
        bool digit_observed = false;
        while (cursor < length_bytes && buffer[cursor] >= static_cast<uint8_t>('0') &&
               buffer[cursor] <= static_cast<uint8_t>('9')) {
            const uint64_t digit = static_cast<uint64_t>(buffer[cursor] - static_cast<uint8_t>('0'));
            if (value > (UINT64_MAX - digit) / 10ULL) {
                return false;
            }
            value = value * 10ULL + digit;
            digit_observed = true;
            ++cursor;
        }
        return digit_observed && cursor < length_bytes && buffer[cursor] == static_cast<uint8_t>('\n');
    }
    return false;
}

[[nodiscard]] bool CalculatePressurePageCounts() noexcept {
    const int64_t descriptor = os::user::OpenFile(
        OS_USER_OOM_PROBE_MEMORY_INFORMATION_PATH,
        sizeof(OS_USER_OOM_PROBE_MEMORY_INFORMATION_PATH) -
            OS_USER_OOM_PROBE_STRING_TERMINATOR_SIZE_BYTES,
        os::abi::OS_ABI_FILE_OPEN_READ_FLAG);
    if (descriptor < OS_USER_OOM_PROBE_SUCCESS_RESULT) {
        static_cast<void>(WriteMessage(OS_USER_OOM_PROBE_OPEN_FAILURE_MESSAGE));
        return false;
    }
    const int64_t read_result =
        os::user::ReadFile(static_cast<uint64_t>(descriptor), memory_information,
                           sizeof(memory_information));
    if (os::user::CloseFile(static_cast<uint64_t>(descriptor)) !=
            OS_USER_OOM_PROBE_SUCCESS_RESULT ||
        read_result <= OS_USER_OOM_PROBE_SUCCESS_RESULT) {
        static_cast<void>(WriteMessage(OS_USER_OOM_PROBE_READ_FAILURE_MESSAGE));
        return false;
    }
    uint64_t allocated_bytes = 0ULL;
    uint64_t resident_limit_bytes = 0ULL;
    if (!ParseMemoryInformationValue(memory_information, static_cast<uint64_t>(read_result),
                                     OS_USER_OOM_PROBE_ALLOCATED_BYTES_PREFIX, allocated_bytes)) {
        static_cast<void>(WriteMessage(OS_USER_OOM_PROBE_PARSE_ALLOCATED_FAILURE_MESSAGE));
        return false;
    }
    if (!ParseMemoryInformationValue(memory_information, static_cast<uint64_t>(read_result),
                                     OS_USER_OOM_PROBE_RESIDENT_LIMIT_BYTES_PREFIX,
                                     resident_limit_bytes)) {
        static_cast<void>(WriteMessage(OS_USER_OOM_PROBE_PARSE_LIMIT_FAILURE_MESSAGE));
        return false;
    }
    if (allocated_bytes >= resident_limit_bytes) {
        static_cast<void>(WriteMessage(OS_USER_OOM_PROBE_RANGE_FAILURE_MESSAGE));
        return false;
    }
    const uint64_t headroom_page_count =
        (resident_limit_bytes - allocated_bytes) / os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
    if (headroom_page_count <= OS_USER_OOM_PROBE_VICTIM_SAFETY_PAGE_COUNT +
                                   OS_USER_OOM_PROBE_VICTIM_ADVANTAGE_PAGE_COUNT) {
        static_cast<void>(WriteMessage(OS_USER_OOM_PROBE_RANGE_FAILURE_MESSAGE));
        return false;
    }
    victim_page_count = headroom_page_count - OS_USER_OOM_PROBE_VICTIM_SAFETY_PAGE_COUNT;
    pressure_page_count = victim_page_count - OS_USER_OOM_PROBE_VICTIM_ADVANTAGE_PAGE_COUNT;
    return true;
}

[[noreturn]] void RunVictim() noexcept {
    const uint64_t victim_size_bytes =
        victim_page_count * os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
    const int64_t mapping_result = MapWritableAnonymous(victim_size_bytes);
    if (mapping_result <= OS_USER_OOM_PROBE_FIRST_ERROR_RESULT) {
        Fail();
    }
    volatile uint8_t *const mapping =
        reinterpret_cast<volatile uint8_t *>(static_cast<uint64_t>(mapping_result));
    for (uint64_t page_index = OS_USER_OOM_PROBE_FIRST_PAGE_INDEX;
         page_index < victim_page_count; ++page_index) {
        mapping[page_index * os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES] =
            OS_USER_OOM_PROBE_VICTIM_PATTERN;
    }
    if (!WriteMessage(OS_USER_OOM_PROBE_VICTIM_READY_MESSAGE) ||
        os::user::SleepFor(OS_USER_OOM_PROBE_VICTIM_DELAY_NANOSECONDS) !=
            OS_USER_OOM_PROBE_SUCCESS_RESULT) {
        Fail();
    }
    Fail();
}

[[nodiscard]] bool VictimWasKilled(const uint64_t process_id) noexcept {
    os::abi::ProcessWaitEventResult result{};
    const int64_t wait_result =
        os::user::WaitProcessEvent(process_id, OS_USER_OOM_PROBE_WAIT_FLAGS, result);
    if (wait_result == os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
        return false;
    }
    if (wait_result != static_cast<int64_t>(process_id) || result.process_id != process_id ||
        result.event_type != os::abi::ProcessWaitEventType::Exited ||
        result.termination_reason != os::abi::ProcessTerminationReason::Signal ||
        result.exception_vector != os::abi::OS_ABI_SIGNAL_KILL_NUMBER) {
        Fail();
    }
    return true;
}

}

extern "C" [[noreturn, gnu::section(".text.os_user_entry")]] void OsUserEntry() noexcept {
    if (!CalculatePressurePageCounts() || !WriteMessage(OS_USER_OOM_PROBE_STARTED_MESSAGE)) {
        Fail();
    }
    const int64_t child_process_id = os::user::ForkProcess();
    if (child_process_id == OS_USER_OOM_PROBE_CHILD_RESULT) {
        RunVictim();
    }
    if (child_process_id <= OS_USER_OOM_PROBE_FIRST_ERROR_RESULT ||
        os::user::SleepFor(OS_USER_OOM_PROBE_PARENT_DELAY_NANOSECONDS) !=
            OS_USER_OOM_PROBE_SUCCESS_RESULT) {
        Fail();
    }

    const uint64_t pressure_size_bytes =
        pressure_page_count * os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES;
    const int64_t mapping_result = MapWritableAnonymous(pressure_size_bytes);
    if (mapping_result <= OS_USER_OOM_PROBE_FIRST_ERROR_RESULT) {
        Fail();
    }
    volatile uint8_t *const mapping =
        reinterpret_cast<volatile uint8_t *>(static_cast<uint64_t>(mapping_result));
    bool victim_killed = false;
    for (uint64_t page_index = OS_USER_OOM_PROBE_FIRST_PAGE_INDEX;
         page_index < pressure_page_count && !victim_killed; ++page_index) {
        mapping[page_index * os::abi::OS_ABI_MEMORY_PAGE_SIZE_BYTES] =
            OS_USER_OOM_PROBE_PRESSURE_PATTERN;
        victim_killed = VictimWasKilled(static_cast<uint64_t>(child_process_id));
    }
    if (!victim_killed ||
        os::user::UnmapMemory(static_cast<uint64_t>(mapping_result),
                              pressure_size_bytes) !=
            OS_USER_OOM_PROBE_SUCCESS_RESULT ||
        !WriteMessage(OS_USER_OOM_PROBE_VICTIM_KILLED_MESSAGE) ||
        !WriteMessage(OS_USER_OOM_PROBE_COMPLETED_MESSAGE)) {
        Fail();
    }
    os::user::ExitProcess(OS_USER_OOM_PROBE_SUCCESS_RESULT);
}
