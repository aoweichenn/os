#include "os/user/system_call.hpp"

#include "os/abi/system_call.hpp"

namespace os::user {

namespace {

constexpr uint64_t OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES = 0ULL;
constexpr int64_t OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT = 0LL;
constexpr int64_t OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT = -1LL;

extern "C" [[nodiscard]] int64_t
osUserInvokeSystemCall(uint64_t systemCallNumber, uint64_t argument0,
                       uint64_t argument1, uint64_t argument2,
                       uint64_t argument3) noexcept;

}

int64_t InvokeSystemCall(const uint64_t systemCallNumber, const uint64_t argument0,
                         const uint64_t argument1, const uint64_t argument2,
                         const uint64_t argument3) noexcept {
    return osUserInvokeSystemCall(systemCallNumber, argument0, argument1,
                                  argument2, argument3);
}

int64_t WriteLog(const char *message, const uint64_t messageSizeBytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::WriteLog),
                            reinterpret_cast<uint64_t>(message), messageSizeBytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

uint64_t GetProcessId() noexcept {
    return static_cast<uint64_t>(
        InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::GetProcessId),
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT));
}

int64_t TryReadPipe(uint8_t *destination, const uint64_t capacityBytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::TryReadPipe),
                            reinterpret_cast<uint64_t>(destination), capacityBytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t TryWritePipe(const uint8_t *source, const uint64_t lengthBytes) noexcept {
    return InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::TryWritePipe),
                            reinterpret_cast<uint64_t>(source), lengthBytes,
                            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ReadPipe(uint8_t *destination, const uint64_t capacityBytes) noexcept {
    if (capacityBytes == OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT;
    }
    while (true) {
        const int64_t readResult = TryReadPipe(destination, capacityBytes);
        if (readResult != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return readResult;
        }
        const int64_t waitResult = InvokeSystemCall(
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPipeReadable),
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
        if (waitResult <= OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT) {
            return waitResult;
        }
    }
}

int64_t WritePipe(const uint8_t *source, const uint64_t lengthBytes) noexcept {
    uint64_t writtenBytes = OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    while (writtenBytes < lengthBytes) {
        const uint64_t remainingBytes = lengthBytes - writtenBytes;
        const uint64_t transferBytes =
            remainingBytes < os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES
                ? remainingBytes
                : os::abi::OS_ABI_SYSTEM_CALL_MAXIMUM_PIPE_TRANSFER_SIZE_BYTES;
        const int64_t writeResult = TryWritePipe(source + writtenBytes, transferBytes);
        if (writeResult > OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT) {
            writtenBytes += static_cast<uint64_t>(writeResult);
            continue;
        }
        if (writeResult != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return writeResult;
        }
        const int64_t waitResult = InvokeSystemCall(
            static_cast<uint64_t>(os::abi::SystemCallNumber::WaitPipeWritable),
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
        if (waitResult <= OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT) {
            return waitResult;
        }
    }
    return static_cast<int64_t>(writtenBytes);
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

int64_t OpenFile(const char *path, const uint64_t pathLengthBytes,
                 const uint64_t openFlags) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::OpenFile),
        reinterpret_cast<uint64_t>(path), pathLengthBytes, openFlags);
}

int64_t ReadFile(const uint64_t fileDescriptor, uint8_t *destination,
                 const uint64_t capacityBytes) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::ReadFile),
        fileDescriptor, reinterpret_cast<uint64_t>(destination), capacityBytes);
}

int64_t WriteFile(const uint64_t fileDescriptor, const uint8_t *source,
                  const uint64_t lengthBytes) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::WriteFile),
        fileDescriptor, reinterpret_cast<uint64_t>(source), lengthBytes);
}

int64_t CloseFile(const uint64_t fileDescriptor) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::CloseFile),
        fileDescriptor, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t CreateDirectory(const char *path,
                        const uint64_t pathLengthBytes) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::CreateDirectory),
        reinterpret_cast<uint64_t>(path), pathLengthBytes,
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t SyncFileSystem() noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(os::abi::SystemCallNumber::SyncFileSystem),
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t TryReadDescriptor(const uint64_t descriptor,
                          uint8_t *const destination,
                          const uint64_t capacityBytes) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(
            os::abi::SystemCallNumber::TryReadDescriptor),
        descriptor, reinterpret_cast<uint64_t>(destination), capacityBytes);
}

int64_t TryWriteDescriptor(const uint64_t descriptor,
                           const uint8_t *const source,
                           const uint64_t lengthBytes) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(
            os::abi::SystemCallNumber::TryWriteDescriptor),
        descriptor, reinterpret_cast<uint64_t>(source), lengthBytes);
}

int64_t ReadDescriptor(const uint64_t descriptor,
                       uint8_t *const destination,
                       const uint64_t capacityBytes) noexcept {
    if (capacityBytes == OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES) {
        return OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT;
    }
    while (true) {
        const int64_t readResult =
            TryReadDescriptor(descriptor, destination, capacityBytes);
        if (readResult != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return readResult;
        }
        const int64_t waitResult = InvokeSystemCall(
            static_cast<uint64_t>(
                os::abi::SystemCallNumber::WaitDescriptorReadable),
            descriptor, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
        if (waitResult <= OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT) {
            return waitResult;
        }
    }
}

int64_t WriteDescriptor(const uint64_t descriptor,
                        const uint8_t *const source,
                        const uint64_t lengthBytes) noexcept {
    uint64_t writtenBytes = OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_SIZE_BYTES;
    while (writtenBytes < lengthBytes) {
        const uint64_t remainingBytes = lengthBytes - writtenBytes;
        const uint64_t transferBytes =
            remainingBytes <
                    os::abi::
                        OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES
                ? remainingBytes
                : os::abi::
                      OS_ABI_SYSTEM_CALL_MAXIMUM_DESCRIPTOR_TRANSFER_SIZE_BYTES;
        const int64_t writeResult =
            TryWriteDescriptor(descriptor, source + writtenBytes,
                               transferBytes);
        if (writeResult > OS_USER_SYSTEM_CALL_EMPTY_TRANSFER_RESULT) {
            writtenBytes += static_cast<uint64_t>(writeResult);
            continue;
        }
        if (writeResult != os::abi::OS_ABI_SYSTEM_CALL_RESULT_WOULD_BLOCK) {
            return writeResult;
        }
        const int64_t waitResult = InvokeSystemCall(
            static_cast<uint64_t>(
                os::abi::SystemCallNumber::WaitDescriptorWritable),
            descriptor, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
            OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
        if (waitResult <= OS_USER_SYSTEM_CALL_FIRST_ERROR_RESULT) {
            return waitResult;
        }
    }
    return static_cast<int64_t>(writtenBytes);
}

int64_t CloseDescriptor(const uint64_t descriptor) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(
            os::abi::SystemCallNumber::CloseDescriptor),
        descriptor, OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t OpenDirectory(const char *const path,
                      const uint64_t pathLengthBytes) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(
            os::abi::SystemCallNumber::OpenDirectory),
        reinterpret_cast<uint64_t>(path), pathLengthBytes,
        OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT);
}

int64_t ReadDirectory(const uint64_t descriptor,
                      os::abi::DirectoryEntry &entry) noexcept {
    return InvokeSystemCall(
        static_cast<uint64_t>(
            os::abi::SystemCallNumber::ReadDirectory),
        descriptor, reinterpret_cast<uint64_t>(&entry),
        sizeof(os::abi::DirectoryEntry));
}

[[noreturn]] void ExitProcess(const int64_t exitCode) noexcept {
    static_cast<void>(
        InvokeSystemCall(static_cast<uint64_t>(os::abi::SystemCallNumber::ExitProcess),
                         static_cast<uint64_t>(exitCode),
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT,
                         OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT));
    while (true) {
        asm volatile("ud2");
    }
}

}
