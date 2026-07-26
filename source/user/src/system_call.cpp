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

int64_t DuplicateDescriptor(const uint64_t source_descriptor, const uint64_t minimum_descriptor,
                            const uint64_t descriptor_flags) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::DuplicateDescriptor),
                            source_descriptor, minimum_descriptor, descriptor_flags);
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
