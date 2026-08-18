#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES = 0ULL;
constexpr int64_t OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT = 0LL;
constexpr int64_t OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT = -1LL;

extern "C" [[nodiscard]] int64_t OsUserInvokeSystemCall(uint64_t system_call_number,
                                                        uint64_t argument0, uint64_t argument1,
                                                        uint64_t argument2,
                                                        uint64_t argument3) noexcept;
extern "C" [[nodiscard]] int64_t
OsUserInvokeLegacySystemCall(uint64_t system_call_number, uint64_t argument0, uint64_t argument1,
                             uint64_t argument2, uint64_t argument3) noexcept;
extern "C" [[nodiscard]] int64_t
OsUserInvokeSystemCallWithDirectionFlag(uint64_t system_call_number, uint64_t argument0,
                                        uint64_t argument1, uint64_t argument2,
                                        uint64_t argument3) noexcept;
extern "C" [[noreturn]] void OsUserSignalReturnRestorer() noexcept;
}

int64_t InvokeSystemCall(const uint64_t system_call_number, const uint64_t argument0,
                         const uint64_t argument1, const uint64_t argument2,
                         const uint64_t argument3) noexcept {
    return OsUserInvokeSystemCall(system_call_number, argument0, argument1, argument2, argument3);
}

int64_t InvokeLegacySystemCall(const uint64_t system_call_number, const uint64_t argument0,
                               const uint64_t argument1, const uint64_t argument2,
                               const uint64_t argument3) noexcept {
    return OsUserInvokeLegacySystemCall(system_call_number, argument0, argument1, argument2,
                                        argument3);
}

int64_t InvokeSystemCallWithDirectionFlag(const uint64_t system_call_number,
                                          const uint64_t argument0, const uint64_t argument1,
                                          const uint64_t argument2,
                                          const uint64_t argument3) noexcept {
    return OsUserInvokeSystemCallWithDirectionFlag(system_call_number, argument0, argument1,
                                                   argument2, argument3);
}

int64_t WriteLog(const char *message, const uint64_t message_size_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WriteLog),
                            reinterpret_cast<uint64_t>(message), message_size_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

uint64_t GetProcessId() noexcept {
    return static_cast<uint64_t>(
        InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessId),
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT));
}

uint64_t GetThreadId() noexcept {
    return static_cast<uint64_t>(
        InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetThreadId),
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT));
}

int64_t CreateThread(const os::abi::ThreadCreateRequest &request) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::CreateThread),
                            reinterpret_cast<uint64_t>(&request), sizeof(request),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

[[noreturn]] void ExitThread(const uint64_t exit_value) noexcept {
    static_cast<void>(InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ExitThread),
                                       exit_value, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                                       OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT));
    while (true) {
        asm volatile("ud2");
    }
}

int64_t JoinThread(const uint64_t thread_id, os::abi::ThreadJoinResult &result) noexcept {
    while (true) {
        const int64_t join_result =
            InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::JoinThread),
                             thread_id, reinterpret_cast<uint64_t>(&result), sizeof(result));
        if (join_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return join_result;
        }
    }
}

int64_t SetThreadLocalStorage(const uint64_t thread_local_storage_base) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetThreadLocalStorage),
                            thread_local_storage_base, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t WaitPrivateFutex(const uint32_t *const word, const uint32_t expected_value) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPrivateFutex),
                            reinterpret_cast<uint64_t>(word), static_cast<uint64_t>(expected_value),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t WaitPrivateFutexUntil(const uint32_t *const word, const uint32_t expected_value,
                              const uint64_t deadline_nanoseconds) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPrivateFutexUntil),
                            reinterpret_cast<uint64_t>(word), static_cast<uint64_t>(expected_value),
                            deadline_nanoseconds);
}

int64_t WakePrivateFutex(const uint32_t *const word, const uint64_t maximum_wake_count) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WakePrivateFutex),
                            reinterpret_cast<uint64_t>(word), maximum_wake_count,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

uint64_t GetMonotonicTime() noexcept {
    return static_cast<uint64_t>(
        InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetMonotonicTime),
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT));
}

int64_t GetRealtime(os::abi::RealtimeInformation &information) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetRealtime),
                            reinterpret_cast<uint64_t>(&information), sizeof(information),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetCredentials(os::abi::CredentialInformation &information) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetCredentials),
                            reinterpret_cast<uint64_t>(&information), sizeof(information),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetUserIdentifiers(const os::abi::IdentifierChangeRequest &request) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetUserIdentifiers),
                            reinterpret_cast<uint64_t>(&request), sizeof(request),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetGroupIdentifiers(const os::abi::IdentifierChangeRequest &request) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetGroupIdentifiers),
                            reinterpret_cast<uint64_t>(&request), sizeof(request),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetSupplementaryGroups(os::abi::GroupIdentifier *const groups,
                               const uint64_t capacity) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetSupplementaryGroups),
        reinterpret_cast<uint64_t>(groups), capacity, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetSupplementaryGroups(const os::abi::GroupIdentifier *const groups,
                               const uint64_t group_count) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::SetSupplementaryGroups),
        reinterpret_cast<uint64_t>(groups), group_count, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetCreationMask(const os::abi::FileMode creation_mask) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetCreationMask),
                            creation_mask, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetResourceLimit(const os::abi::ResourceLimitKind kind,
                         os::abi::ResourceLimit &limit) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetResourceLimit),
                            static_cast<uint64_t>(kind), reinterpret_cast<uint64_t>(&limit),
                            sizeof(limit));
}

int64_t SetResourceLimit(const os::abi::ResourceLimitKind kind,
                         const os::abi::ResourceLimit &limit) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetResourceLimit),
                            static_cast<uint64_t>(kind), reinterpret_cast<uint64_t>(&limit),
                            sizeof(limit));
}

int64_t SleepUntil(const uint64_t deadline_nanoseconds) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SleepUntil),
                            deadline_nanoseconds, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SleepFor(const uint64_t duration_nanoseconds) noexcept {
    const uint64_t now_nanoseconds = GetMonotonicTime();
    const uint64_t deadline_nanoseconds = duration_nanoseconds > UINT64_MAX - now_nanoseconds
                                              ? UINT64_MAX
                                              : now_nanoseconds + duration_nanoseconds;
    return SleepUntil(deadline_nanoseconds);
}

int64_t SetSignalAction(const uint64_t signal_number, const os::abi::SignalAction &action,
                        os::abi::SignalAction *const previous_action) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetSignalAction),
                            signal_number, reinterpret_cast<uint64_t>(&action),
                            reinterpret_cast<uint64_t>(previous_action), sizeof(action));
}

int64_t InstallSignalHandler(const uint64_t signal_number, const SignalHandler handler,
                             const uint64_t additional_mask, const uint64_t flags,
                             os::abi::SignalAction *const previous_action) noexcept {
    if (handler == nullptr) {
        return os::abi::OS_ABI_SYSTEM_CALL_RESULT_INVALID_ARGUMENT;
    }
    const os::abi::SignalAction action{
        .disposition = os::abi::SignalDisposition::Handler,
        .handler_address = reinterpret_cast<uint64_t>(handler),
        .restorer_address = reinterpret_cast<uint64_t>(&OsUserSignalReturnRestorer),
        .additional_mask = additional_mask,
        .flags = flags,
    };
    return SetSignalAction(signal_number, action, previous_action);
}

int64_t SetSignalMask(const uint64_t signal_mask, uint64_t *const previous_signal_mask) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetSignalMask),
                            signal_mask, reinterpret_cast<uint64_t>(previous_signal_mask),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SendProcessSignal(const uint64_t process_id, const uint64_t signal_number) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SendProcessSignal),
                            process_id, signal_number, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SendProcessGroupSignal(const uint64_t process_group_id,
                               const uint64_t signal_number) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::SendProcessGroupSignal), process_group_id,
        signal_number, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetProcessGroup() noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessGroup),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetProcessGroup(const uint64_t process_group_id) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetProcessGroup),
                            process_group_id, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetProcessGroupFor(const uint64_t process_id, const uint64_t process_group_id) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetProcessGroupFor),
                            process_id, process_group_id, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t CreateSession() noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::CreateSession),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetSession() noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetSession),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetTerminalInformation(os::abi::TerminalInformation &information) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetTerminalInformation),
        reinterpret_cast<uint64_t>(&information), sizeof(information),
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetTerminalForegroundGroup(const uint64_t process_group_id) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::SetTerminalForegroundGroup),
        process_group_id, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetTerminalInputMode(const os::abi::TerminalInputMode mode) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetTerminalInputMode),
                            static_cast<uint64_t>(mode), OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t TryReadPipe(uint8_t *destination, const uint64_t capacity_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::TryReadPipe),
                            reinterpret_cast<uint64_t>(destination), capacity_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t TryWritePipe(const uint8_t *source, const uint64_t length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::TryWritePipe),
                            reinterpret_cast<uint64_t>(source), length_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ReadPipe(uint8_t *destination, const uint64_t capacity_bytes) noexcept {
    if (capacity_bytes == OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT;
    }
    while (true) {
        const int64_t read_result = TryReadPipe(destination, capacity_bytes);
        if (read_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return read_result;
        }
        const int64_t wait_result = InvokeSystemCall(
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPipeReadable),
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
        if (wait_result <= OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT) {
            return wait_result;
        }
    }
}

int64_t WritePipe(const uint8_t *source, const uint64_t length_bytes) noexcept {
    uint64_t written_bytes = OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    while (written_bytes < length_bytes) {
        const uint64_t remaining_bytes = length_bytes - written_bytes;
        const uint64_t transfer_bytes =
            remaining_bytes < os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES
                ? remaining_bytes
                : os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES;
        const int64_t write_result = TryWritePipe(source + written_bytes, transfer_bytes);
        if (write_result > OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT) {
            written_bytes += static_cast<uint64_t>(write_result);
            continue;
        }
        if (write_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return write_result;
        }
        const int64_t wait_result = InvokeSystemCall(
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPipeWritable),
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
        if (wait_result <= OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT) {
            return wait_result;
        }
    }
    return static_cast<int64_t>(written_bytes);
}

int64_t ClosePipeReader() noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ClosePipeReader),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ClosePipeWriter() noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ClosePipeWriter),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t OpenFile(const char *path, const uint64_t path_length_bytes,
                 const uint64_t open_flags) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::OpenFile),
                            reinterpret_cast<uint64_t>(path), path_length_bytes, open_flags);
}

int64_t ReadFile(const uint64_t file_descriptor, uint8_t *destination,
                 const uint64_t capacity_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ReadFile),
                            file_descriptor, reinterpret_cast<uint64_t>(destination),
                            capacity_bytes);
}

int64_t WriteFile(const uint64_t file_descriptor, const uint8_t *source,
                  const uint64_t length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WriteFile),
                            file_descriptor, reinterpret_cast<uint64_t>(source), length_bytes);
}

int64_t CloseFile(const uint64_t file_descriptor) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::CloseFile),
                            file_descriptor, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t CreateDirectory(const char *path, const uint64_t path_length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::CreateDirectory),
                            reinterpret_cast<uint64_t>(path), path_length_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SyncFileSystem() noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SyncFileSystem),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t TryReadDescriptor(const uint64_t descriptor, uint8_t *const destination,
                          const uint64_t capacity_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::TryReadDescriptor),
                            descriptor, reinterpret_cast<uint64_t>(destination), capacity_bytes);
}

int64_t TryWriteDescriptor(const uint64_t descriptor, const uint8_t *const source,
                           const uint64_t length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::TryWriteDescriptor),
                            descriptor, reinterpret_cast<uint64_t>(source), length_bytes);
}

int64_t ReadDescriptor(const uint64_t descriptor, uint8_t *const destination,
                       const uint64_t capacity_bytes) noexcept {
    if (capacity_bytes == OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT;
    }
    while (true) {
        const int64_t read_result = TryReadDescriptor(descriptor, destination, capacity_bytes);
        if (read_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return read_result;
        }
        const int64_t wait_result = InvokeSystemCall(
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitDescriptorReadable), descriptor,
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
        if (wait_result <= OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT) {
            return wait_result;
        }
    }
}

int64_t WriteDescriptor(const uint64_t descriptor, const uint8_t *const source,
                        const uint64_t length_bytes) noexcept {
    uint64_t written_bytes = OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    while (written_bytes < length_bytes) {
        const uint64_t remaining_bytes = length_bytes - written_bytes;
        const uint64_t transfer_bytes =
            remaining_bytes < os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES
                ? remaining_bytes
                : os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES;
        const int64_t write_result =
            TryWriteDescriptor(descriptor, source + written_bytes, transfer_bytes);
        if (write_result > OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT) {
            written_bytes += static_cast<uint64_t>(write_result);
            continue;
        }
        if (write_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return write_result;
        }
        const int64_t wait_result = InvokeSystemCall(
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitDescriptorWritable), descriptor,
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
        if (wait_result <= OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT) {
            return wait_result;
        }
    }
    return static_cast<int64_t>(written_bytes);
}

int64_t CloseDescriptor(const uint64_t descriptor) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::CloseDescriptor),
                            descriptor, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t CreatePipe(os::abi::PipeDescriptorPair &descriptor_pair) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::CreatePipe),
                            reinterpret_cast<uint64_t>(&descriptor_pair),
                            os::abi::OS_ABI_PIPE_DESCRIPTOR_PAIR_SIZE_BYTES,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t DuplicateDescriptor(const uint64_t source_descriptor, const uint64_t minimum_descriptor,
                            const uint64_t descriptor_flags) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::DuplicateDescriptor),
                            source_descriptor, minimum_descriptor, descriptor_flags);
}

int64_t DuplicateDescriptorTo(const uint64_t source_descriptor,
                              const uint64_t destination_descriptor,
                              const uint64_t descriptor_flags) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::DuplicateDescriptorTo),
                            source_descriptor, destination_descriptor, descriptor_flags);
}

int64_t GetDescriptorFlags(const uint64_t descriptor) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetDescriptorFlags),
                            descriptor, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetDescriptorFlags(const uint64_t descriptor, const uint64_t descriptor_flags) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetDescriptorFlags),
                            descriptor, descriptor_flags, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetDescriptorSoftLimit(const uint64_t soft_limit) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::SetDescriptorSoftLimit), soft_limit,
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetDescriptorSoftLimit() noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetDescriptorSoftLimit),
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetDescriptorHardLimit() noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetDescriptorHardLimit),
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t OpenDirectory(const char *const path, const uint64_t path_length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::OpenDirectory),
                            reinterpret_cast<uint64_t>(path), path_length_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ReadDirectory(const uint64_t descriptor, os::abi::DirectoryEntry &entry) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ReadDirectory),
                            descriptor, reinterpret_cast<uint64_t>(&entry),
                            sizeof(os::abi::DirectoryEntry));
}

int64_t ChangeDirectory(const char *const path, const uint64_t path_length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeDirectory),
                            reinterpret_cast<uint64_t>(path), path_length_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetWorkingDirectory(char *const destination, const uint64_t capacity_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetWorkingDirectory),
                            reinterpret_cast<uint64_t>(destination), capacity_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t UnlinkFile(const char *const path, const uint64_t path_length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::UnlinkFile),
                            reinterpret_cast<uint64_t>(path), path_length_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t RemoveDirectory(const char *const path, const uint64_t path_length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::RemoveDirectory),
                            reinterpret_cast<uint64_t>(path), path_length_bytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t Rename(const char *const source_path, const uint64_t source_length_bytes,
               const char *const destination_path,
               const uint64_t destination_length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::Rename),
                            reinterpret_cast<uint64_t>(source_path), source_length_bytes,
                            reinterpret_cast<uint64_t>(destination_path), destination_length_bytes);
}

int64_t TruncateFile(const char *const path, const uint64_t path_length_bytes,
                     const uint64_t size_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::TruncateFile),
                            reinterpret_cast<uint64_t>(path), path_length_bytes, size_bytes);
}

int64_t StatFile(const char *const path, const uint64_t path_length_bytes,
                 os::abi::FileInformation &information) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::StatFile),
                            reinterpret_cast<uint64_t>(path), path_length_bytes,
                            reinterpret_cast<uint64_t>(&information), sizeof(information));
}

int64_t ChangeMode(const char *const path, const uint64_t path_length_bytes,
                   const os::abi::FileMode mode) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeMode),
                            reinterpret_cast<uint64_t>(path), path_length_bytes, mode);
}

int64_t ChangeOwner(const char *const path, const uint64_t path_length_bytes,
                    const os::abi::UserIdentifier user_identifier,
                    const os::abi::GroupIdentifier group_identifier) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ChangeOwner),
                            reinterpret_cast<uint64_t>(path), path_length_bytes, user_identifier,
                            group_identifier);
}

int64_t LinkFile(const char *const source_path, const uint64_t source_length_bytes,
                 const char *const destination_path,
                 const uint64_t destination_length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::LinkFile),
                            reinterpret_cast<uint64_t>(source_path), source_length_bytes,
                            reinterpret_cast<uint64_t>(destination_path), destination_length_bytes);
}

int64_t CreateSymbolicLink(const char *const target, const uint64_t target_length_bytes,
                           const char *const destination_path,
                           const uint64_t destination_length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::CreateSymbolicLink),
                            reinterpret_cast<uint64_t>(target), target_length_bytes,
                            reinterpret_cast<uint64_t>(destination_path), destination_length_bytes);
}

int64_t ReadSymbolicLink(const char *const path, const uint64_t path_length_bytes,
                         char *const destination, const uint64_t capacity_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ReadSymbolicLink),
                            reinterpret_cast<uint64_t>(path), path_length_bytes,
                            reinterpret_cast<uint64_t>(destination), capacity_bytes);
}

int64_t SpawnProcess(const os::abi::ProcessLaunchRequest &request) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SpawnProcess),
                            reinterpret_cast<uint64_t>(&request), sizeof(request),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ExecProcess(const os::abi::ProcessLaunchRequest &request) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ExecProcess),
                            reinterpret_cast<uint64_t>(&request), sizeof(request),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t WaitProcess(const uint64_t process_id, os::abi::ProcessWaitResult &result) noexcept {
    while (true) {
        const int64_t wait_result =
            InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WaitProcess),
                             process_id, reinterpret_cast<uint64_t>(&result), sizeof(result));
        if (wait_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return wait_result;
        }
    }
}

int64_t WaitProcessEvent(const uint64_t process_id, const uint64_t wait_flags,
                         os::abi::ProcessWaitEventResult &result) noexcept {
    while (true) {
        const int64_t wait_result = InvokeSystemCall(
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitProcessEvent), process_id,
            wait_flags, reinterpret_cast<uint64_t>(&result), sizeof(result));
        if (wait_result != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK ||
            (wait_flags & os::abi::OS_ABI_PROCESS_WAIT_NO_HANG_FLAG) !=
                OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT) {
            return wait_result;
        }
    }
}

int64_t MapAnonymousMemory(const uint64_t requested_address, const uint64_t length_bytes,
                           const uint64_t protection_flags, const uint64_t map_flags) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::MapAnonymousMemory),
                            requested_address, length_bytes, protection_flags, map_flags);
}

int64_t MapFileMemory(const os::abi::FileMemoryMapRequest &request) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::MapFileMemory),
                            reinterpret_cast<uint64_t>(&request), sizeof(request),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t UnmapMemory(const uint64_t address, const uint64_t length_bytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::UnmapMemory), address,
                            length_bytes, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SetProgramBreak(const uint64_t requested_address) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::SetProgramBreak),
                            requested_address, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t GetVirtualMemoryStatistics(os::abi::VirtualMemoryStatistics &statistics) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::GetVirtualMemoryStatistics),
        reinterpret_cast<uint64_t>(&statistics), sizeof(statistics),
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ForkProcess() noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ForkProcess),
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

[[noreturn]] void ExitProcess(const int64_t exit_code) noexcept {
    static_cast<void>(
        InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ExitProcess),
                         static_cast<uint64_t>(exit_code), OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT));
    while (true) {
        asm volatile("ud2");
    }
}
}
