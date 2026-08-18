#pragma once

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace os::user {

inline constexpr uint64_t OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT = 0ULL;
using SignalHandler = void (*)(uint64_t signal_number, os::abi::SignalFrame *signal_frame) noexcept;

[[nodiscard]] int64_t
InvokeSystemCall(uint64_t system_call_number, uint64_t argument0, uint64_t argument1,
                 uint64_t argument2,
                 uint64_t argument3 = OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT) noexcept;
[[nodiscard]] int64_t
InvokeLegacySystemCall(uint64_t system_call_number, uint64_t argument0, uint64_t argument1,
                       uint64_t argument2,
                       uint64_t argument3 = OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT) noexcept;
[[nodiscard]] int64_t InvokeSystemCallWithDirectionFlag(
    uint64_t system_call_number, uint64_t argument0, uint64_t argument1, uint64_t argument2,
    uint64_t argument3 = OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT) noexcept;
[[nodiscard]] int64_t WriteLog(const char *message, uint64_t message_size_bytes) noexcept;
[[nodiscard]] uint64_t GetProcessId() noexcept;
[[nodiscard]] uint64_t GetThreadId() noexcept;
[[nodiscard]] int64_t CreateThread(const os::abi::ThreadCreateRequest &request) noexcept;
[[noreturn]] void ExitThread(uint64_t exit_value) noexcept;
[[nodiscard]] int64_t JoinThread(uint64_t thread_id, os::abi::ThreadJoinResult &result) noexcept;
[[nodiscard]] int64_t SetThreadLocalStorage(uint64_t thread_local_storage_base) noexcept;
[[nodiscard]] int64_t WaitPrivateFutex(const uint32_t *word, uint32_t expected_value) noexcept;
[[nodiscard]] int64_t WaitPrivateFutexUntil(const uint32_t *word, uint32_t expected_value,
                                            uint64_t deadline_nanoseconds) noexcept;
[[nodiscard]] int64_t WakePrivateFutex(const uint32_t *word, uint64_t maximum_wake_count) noexcept;
[[nodiscard]] uint64_t GetMonotonicTime() noexcept;
[[nodiscard]] int64_t GetRealtime(os::abi::RealtimeInformation &information) noexcept;
[[nodiscard]] int64_t GetCredentials(os::abi::CredentialInformation &information) noexcept;
[[nodiscard]] int64_t SetUserIdentifiers(const os::abi::IdentifierChangeRequest &request) noexcept;
[[nodiscard]] int64_t SetGroupIdentifiers(const os::abi::IdentifierChangeRequest &request) noexcept;
[[nodiscard]] int64_t GetSupplementaryGroups(os::abi::GroupIdentifier *groups,
                                             uint64_t capacity) noexcept;
[[nodiscard]] int64_t SetSupplementaryGroups(const os::abi::GroupIdentifier *groups,
                                             uint64_t group_count) noexcept;
[[nodiscard]] int64_t SetCreationMask(os::abi::FileMode creation_mask) noexcept;
[[nodiscard]] int64_t GetResourceLimit(os::abi::ResourceLimitKind kind,
                                       os::abi::ResourceLimit &limit) noexcept;
[[nodiscard]] int64_t SetResourceLimit(os::abi::ResourceLimitKind kind,
                                       const os::abi::ResourceLimit &limit) noexcept;
[[nodiscard]] int64_t SleepUntil(uint64_t deadline_nanoseconds) noexcept;
[[nodiscard]] int64_t SleepFor(uint64_t duration_nanoseconds) noexcept;
[[nodiscard]] int64_t SetSignalAction(uint64_t signal_number, const os::abi::SignalAction &action,
                                      os::abi::SignalAction *previous_action) noexcept;
[[nodiscard]] int64_t InstallSignalHandler(uint64_t signal_number, SignalHandler handler,
                                           uint64_t additional_mask, uint64_t flags,
                                           os::abi::SignalAction *previous_action) noexcept;
[[nodiscard]] int64_t SetSignalMask(uint64_t signal_mask, uint64_t *previous_signal_mask) noexcept;
[[nodiscard]] int64_t SendProcessSignal(uint64_t process_id, uint64_t signal_number) noexcept;
[[nodiscard]] int64_t SendProcessGroupSignal(uint64_t process_group_id,
                                             uint64_t signal_number) noexcept;
[[nodiscard]] int64_t GetProcessGroup() noexcept;
[[nodiscard]] int64_t SetProcessGroup(uint64_t process_group_id) noexcept;
[[nodiscard]] int64_t SetProcessGroupFor(uint64_t process_id, uint64_t process_group_id) noexcept;
[[nodiscard]] int64_t CreateSession() noexcept;
[[nodiscard]] int64_t GetSession() noexcept;
[[nodiscard]] int64_t GetTerminalInformation(os::abi::TerminalInformation &information) noexcept;
[[nodiscard]] int64_t SetTerminalForegroundGroup(uint64_t process_group_id) noexcept;
[[nodiscard]] int64_t SetTerminalInputMode(os::abi::TerminalInputMode mode) noexcept;
[[nodiscard]] int64_t TryReadPipe(uint8_t *destination, uint64_t capacity_bytes) noexcept;
[[nodiscard]] int64_t TryWritePipe(const uint8_t *source, uint64_t length_bytes) noexcept;
[[nodiscard]] int64_t ReadPipe(uint8_t *destination, uint64_t capacity_bytes) noexcept;
[[nodiscard]] int64_t WritePipe(const uint8_t *source, uint64_t length_bytes) noexcept;
[[nodiscard]] int64_t ClosePipeReader() noexcept;
[[nodiscard]] int64_t ClosePipeWriter() noexcept;
[[nodiscard]] int64_t OpenFile(const char *path, uint64_t path_length_bytes,
                               uint64_t open_flags) noexcept;
[[nodiscard]] int64_t ReadFile(uint64_t file_descriptor, uint8_t *destination,
                               uint64_t capacity_bytes) noexcept;
[[nodiscard]] int64_t WriteFile(uint64_t file_descriptor, const uint8_t *source,
                                uint64_t length_bytes) noexcept;
[[nodiscard]] int64_t CloseFile(uint64_t file_descriptor) noexcept;
[[nodiscard]] int64_t CreateDirectory(const char *path, uint64_t path_length_bytes) noexcept;
[[nodiscard]] int64_t SyncFileSystem() noexcept;
[[nodiscard]] int64_t TryReadDescriptor(uint64_t descriptor, uint8_t *destination,
                                        uint64_t capacity_bytes) noexcept;
[[nodiscard]] int64_t TryWriteDescriptor(uint64_t descriptor, const uint8_t *source,
                                         uint64_t length_bytes) noexcept;
[[nodiscard]] int64_t ReadDescriptor(uint64_t descriptor, uint8_t *destination,
                                     uint64_t capacity_bytes) noexcept;
[[nodiscard]] int64_t WriteDescriptor(uint64_t descriptor, const uint8_t *source,
                                      uint64_t length_bytes) noexcept;
[[nodiscard]] int64_t CloseDescriptor(uint64_t descriptor) noexcept;
[[nodiscard]] int64_t CreatePipe(os::abi::PipeDescriptorPair &descriptor_pair) noexcept;
[[nodiscard]] int64_t DuplicateDescriptor(uint64_t source_descriptor, uint64_t minimum_descriptor,
                                          uint64_t descriptor_flags) noexcept;
[[nodiscard]] int64_t DuplicateDescriptorTo(uint64_t source_descriptor,
                                            uint64_t destination_descriptor,
                                            uint64_t descriptor_flags) noexcept;
[[nodiscard]] int64_t GetDescriptorFlags(uint64_t descriptor) noexcept;
[[nodiscard]] int64_t SetDescriptorFlags(uint64_t descriptor, uint64_t descriptor_flags) noexcept;
[[nodiscard]] int64_t SetDescriptorSoftLimit(uint64_t soft_limit) noexcept;
[[nodiscard]] int64_t GetDescriptorSoftLimit() noexcept;
[[nodiscard]] int64_t GetDescriptorHardLimit() noexcept;
[[nodiscard]] int64_t OpenDirectory(const char *path, uint64_t path_length_bytes) noexcept;
[[nodiscard]] int64_t ReadDirectory(uint64_t descriptor, os::abi::DirectoryEntry &entry) noexcept;
[[nodiscard]] int64_t ChangeDirectory(const char *path, uint64_t path_length_bytes) noexcept;
[[nodiscard]] int64_t GetWorkingDirectory(char *destination, uint64_t capacity_bytes) noexcept;
[[nodiscard]] int64_t UnlinkFile(const char *path, uint64_t path_length_bytes) noexcept;
[[nodiscard]] int64_t RemoveDirectory(const char *path, uint64_t path_length_bytes) noexcept;
[[nodiscard]] int64_t Rename(const char *source_path, uint64_t source_length_bytes,
                             const char *destination_path,
                             uint64_t destination_length_bytes) noexcept;
[[nodiscard]] int64_t TruncateFile(const char *path, uint64_t path_length_bytes,
                                   uint64_t size_bytes) noexcept;
[[nodiscard]] int64_t StatFile(const char *path, uint64_t path_length_bytes,
                               os::abi::FileInformation &information) noexcept;
[[nodiscard]] int64_t ChangeMode(const char *path, uint64_t path_length_bytes,
                                 os::abi::FileMode mode) noexcept;
[[nodiscard]] int64_t ChangeOwner(const char *path, uint64_t path_length_bytes,
                                  os::abi::UserIdentifier user_identifier,
                                  os::abi::GroupIdentifier group_identifier) noexcept;
[[nodiscard]] int64_t LinkFile(const char *source_path, uint64_t source_length_bytes,
                               const char *destination_path,
                               uint64_t destination_length_bytes) noexcept;
[[nodiscard]] int64_t CreateSymbolicLink(const char *target, uint64_t target_length_bytes,
                                         const char *destination_path,
                                         uint64_t destination_length_bytes) noexcept;
[[nodiscard]] int64_t ReadSymbolicLink(const char *path, uint64_t path_length_bytes,
                                       char *destination, uint64_t capacity_bytes) noexcept;
[[nodiscard]] int64_t SpawnProcess(const os::abi::ProcessLaunchRequest &request) noexcept;
[[nodiscard]] int64_t ExecProcess(const os::abi::ProcessLaunchRequest &request) noexcept;
[[nodiscard]] int64_t WaitProcess(uint64_t process_id, os::abi::ProcessWaitResult &result) noexcept;
[[nodiscard]] int64_t WaitProcessEvent(uint64_t process_id, uint64_t wait_flags,
                                       os::abi::ProcessWaitEventResult &result) noexcept;
[[nodiscard]] int64_t ForkProcess() noexcept;
[[nodiscard]] int64_t MapAnonymousMemory(uint64_t requested_address, uint64_t length_bytes,
                                         uint64_t protection_flags, uint64_t map_flags) noexcept;
[[nodiscard]] int64_t MapFileMemory(const os::abi::FileMemoryMapRequest &request) noexcept;
[[nodiscard]] int64_t UnmapMemory(uint64_t address, uint64_t length_bytes) noexcept;
[[nodiscard]] int64_t SetProgramBreak(uint64_t requested_address) noexcept;
[[nodiscard]] int64_t
GetVirtualMemoryStatistics(os::abi::VirtualMemoryStatistics &statistics) noexcept;
[[noreturn]] void ExitProcess(int64_t exit_code) noexcept;
}
