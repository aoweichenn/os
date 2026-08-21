#pragma once

#include <os/abi/resource.hpp>
#include <os/abi/security.hpp>
#include <os/abi/signal.hpp>
#include <os/abi/system_call.hpp>
#include <os/abi/terminal.hpp>
#include <os/abi/thread.hpp>
#include <os/abi/version.hpp>
#include <os/abi/virtual_memory.hpp>

#include <stddef.h>
#include <stdint.h>

namespace os::abi {

// ABI v2 的字段偏移是用户程序与 Kernel 共同消费的线协议，不允许依赖源码顺序猜测。
inline constexpr uint64_t OS_ABI_LAYOUT_FIRST_SYSTEM_CALL_NUMBER = 1ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES = 0ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES = 8ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES = 16ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES = 24ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES = 32ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_SIXTH_FIELD_OFFSET_BYTES = 40ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_SEVENTH_FIELD_OFFSET_BYTES = 48ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_EIGHTH_FIELD_OFFSET_BYTES = 56ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_SIGNAL_CONTEXT_OFFSET_BYTES = 64ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_DIRECTORY_NAME_OFFSET_BYTES = 24ULL;
inline constexpr uint64_t OS_ABI_LAYOUT_DIRECTORY_RESERVED_OFFSET_BYTES = 279ULL;

static_assert(static_cast<uint64_t>(SystemCallNumber::WriteLog) ==
              OS_ABI_LAYOUT_FIRST_SYSTEM_CALL_NUMBER);
static_assert(static_cast<uint64_t>(SystemCallNumber::SynchronizeMemory) ==
              OS_ABI_SYSTEM_CALL_LAST_NUMBER);
static_assert(OS_ABI_SYSTEM_CALL_RESULT_INVALID_USER_MEMORY == OS_ABI_SYSTEM_CALL_FIRST_ERROR);
static_assert(OS_ABI_SYSTEM_CALL_RESULT_RESOURCE_LIMIT_EXCEEDED == OS_ABI_SYSTEM_CALL_LAST_ERROR);

static_assert(offsetof(PipeDescriptorPair, reader_descriptor) ==
              OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(PipeDescriptorPair, writer_descriptor) ==
              OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);

static_assert(offsetof(RealtimeInformation, year) == OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(RealtimeInformation, unix_seconds) ==
              OS_ABI_LAYOUT_SEVENTH_FIELD_OFFSET_BYTES);

static_assert(offsetof(DirectoryEntry, inode_number) == OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(DirectoryEntry, type) == OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(DirectoryEntry, name_length_bytes) ==
              OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(DirectoryEntry, name) == OS_ABI_LAYOUT_DIRECTORY_NAME_OFFSET_BYTES);
static_assert(offsetof(DirectoryEntry, reserved) == OS_ABI_LAYOUT_DIRECTORY_RESERVED_OFFSET_BYTES);

static_assert(offsetof(FileInformation, mount_identifier) ==
              OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileInformation, superblock_identifier) ==
              OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileInformation, inode_number) == OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileInformation, generation) == OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileInformation, type) == OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileInformation, size_bytes) == OS_ABI_LAYOUT_SIXTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileInformation, allocated_size_bytes) ==
              OS_ABI_LAYOUT_SEVENTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileInformation, link_count) == OS_ABI_LAYOUT_EIGHTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileInformation, mode) == 96ULL);
static_assert(offsetof(FileInformation, owner_user_identifier) == 100ULL);
static_assert(offsetof(FileInformation, owner_group_identifier) == 104ULL);
static_assert(offsetof(FileInformation, reserved) == 108ULL);

static_assert(offsetof(CredentialInformation, real_user_identifier) == 0ULL);
static_assert(offsetof(CredentialInformation, real_group_identifier) == 12ULL);
static_assert(offsetof(CredentialInformation, supplementary_group_count) == 24ULL);
static_assert(offsetof(CredentialInformation, creation_mask) == 28ULL);

static_assert(offsetof(IdentifierChangeRequest, real_identifier) == 0ULL);
static_assert(offsetof(IdentifierChangeRequest, effective_identifier) == 4ULL);
static_assert(offsetof(IdentifierChangeRequest, saved_identifier) == 8ULL);
static_assert(offsetof(IdentifierChangeRequest, reserved) == 12ULL);

static_assert(offsetof(ResourceLimit, current) == OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(ResourceLimit, maximum) == OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);

static_assert(offsetof(ProcessLaunchRequest, path_address) ==
              OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessLaunchRequest, path_length_bytes) ==
              OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessLaunchRequest, argument_vector_address) ==
              OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessLaunchRequest, argument_count) ==
              OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessLaunchRequest, environment_vector_address) ==
              OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessLaunchRequest, environment_count) ==
              OS_ABI_LAYOUT_SIXTH_FIELD_OFFSET_BYTES);

static_assert(offsetof(ProcessWaitResult, process_id) == OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitResult, parent_process_id) ==
              OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitResult, termination_reason) ==
              OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitResult, exit_code) == OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitResult, exception_vector) ==
              OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES);

static_assert(offsetof(ThreadCreateRequest, entry_address) ==
              OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(ThreadCreateRequest, argument) == OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(ThreadCreateRequest, stack_base_address) ==
              OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(ThreadCreateRequest, stack_size_bytes) ==
              OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(ThreadCreateRequest, stack_pointer) ==
              OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(ThreadCreateRequest, thread_local_storage_base) ==
              OS_ABI_LAYOUT_SIXTH_FIELD_OFFSET_BYTES);

static_assert(offsetof(SignalAction, disposition) == OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalAction, handler_address) == OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalAction, restorer_address) == OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalAction, additional_mask) == OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalAction, flags) == OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES);

static_assert(offsetof(SignalFrame, magic) == OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalFrame, version) == OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalFrame, size_bytes) == OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalFrame, cookie) == OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalFrame, signal_number) == OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalFrame, previous_mask) == OS_ABI_LAYOUT_SIXTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalFrame, restorer_address) == OS_ABI_LAYOUT_SEVENTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalFrame, reserved) == OS_ABI_LAYOUT_EIGHTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(SignalFrame, context) == OS_ABI_LAYOUT_SIGNAL_CONTEXT_OFFSET_BYTES);

static_assert(offsetof(FileMemoryMapRequest, requested_address) ==
              OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileMemoryMapRequest, length_bytes) ==
              OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileMemoryMapRequest, protection_flags) ==
              OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileMemoryMapRequest, map_flags) == OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileMemoryMapRequest, file_descriptor) ==
              OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(FileMemoryMapRequest, file_offset_bytes) ==
              OS_ABI_LAYOUT_SIXTH_FIELD_OFFSET_BYTES);

static_assert(offsetof(ProcessWaitEventResult, process_id) ==
              OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitEventResult, parent_process_id) ==
              OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitEventResult, event_type) ==
              OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitEventResult, termination_reason) ==
              OS_ABI_LAYOUT_FOURTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitEventResult, exit_code) ==
              OS_ABI_LAYOUT_FIFTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitEventResult, exception_vector) ==
              OS_ABI_LAYOUT_SIXTH_FIELD_OFFSET_BYTES);
static_assert(offsetof(ProcessWaitEventResult, signal_number) ==
              OS_ABI_LAYOUT_SEVENTH_FIELD_OFFSET_BYTES);

static_assert(offsetof(TerminalInformation, terminal_id) == OS_ABI_LAYOUT_FIRST_FIELD_OFFSET_BYTES);
static_assert(offsetof(TerminalInformation, controlling_session_id) ==
              OS_ABI_LAYOUT_SECOND_FIELD_OFFSET_BYTES);
static_assert(offsetof(TerminalInformation, foreground_process_group_id) ==
              OS_ABI_LAYOUT_THIRD_FIELD_OFFSET_BYTES);

}
