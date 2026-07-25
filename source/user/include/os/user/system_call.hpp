#pragma once

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace os::user {

inline constexpr uint64_t OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT = 0ULL;

[[nodiscard]] int64_t InvokeSystemCall(uint64_t systemCallNumber, uint64_t argument0,
                                       uint64_t argument1, uint64_t argument2,
                                       uint64_t argument3 =
                                           OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT) noexcept;
[[nodiscard]] int64_t WriteLog(const char *message, uint64_t messageSizeBytes) noexcept;
[[nodiscard]] uint64_t GetProcessId() noexcept;
[[nodiscard]] int64_t TryReadPipe(uint8_t *destination, uint64_t capacityBytes) noexcept;
[[nodiscard]] int64_t TryWritePipe(const uint8_t *source, uint64_t lengthBytes) noexcept;
[[nodiscard]] int64_t ReadPipe(uint8_t *destination, uint64_t capacityBytes) noexcept;
[[nodiscard]] int64_t WritePipe(const uint8_t *source, uint64_t lengthBytes) noexcept;
[[nodiscard]] int64_t ClosePipeReader() noexcept;
[[nodiscard]] int64_t ClosePipeWriter() noexcept;
[[nodiscard]] int64_t OpenFile(const char *path, uint64_t pathLengthBytes,
                               uint64_t openFlags) noexcept;
[[nodiscard]] int64_t ReadFile(uint64_t fileDescriptor, uint8_t *destination,
                               uint64_t capacityBytes) noexcept;
[[nodiscard]] int64_t WriteFile(uint64_t fileDescriptor, const uint8_t *source,
                                uint64_t lengthBytes) noexcept;
[[nodiscard]] int64_t CloseFile(uint64_t fileDescriptor) noexcept;
[[nodiscard]] int64_t CreateDirectory(const char *path,
                                      uint64_t pathLengthBytes) noexcept;
[[nodiscard]] int64_t SyncFileSystem() noexcept;
[[nodiscard]] int64_t TryReadDescriptor(uint64_t descriptor,
                                        uint8_t *destination,
                                        uint64_t capacityBytes) noexcept;
[[nodiscard]] int64_t TryWriteDescriptor(uint64_t descriptor,
                                         const uint8_t *source,
                                         uint64_t lengthBytes) noexcept;
[[nodiscard]] int64_t ReadDescriptor(uint64_t descriptor,
                                     uint8_t *destination,
                                     uint64_t capacityBytes) noexcept;
[[nodiscard]] int64_t WriteDescriptor(uint64_t descriptor,
                                      const uint8_t *source,
                                      uint64_t lengthBytes) noexcept;
[[nodiscard]] int64_t CloseDescriptor(uint64_t descriptor) noexcept;
[[nodiscard]] int64_t OpenDirectory(const char *path,
                                    uint64_t pathLengthBytes) noexcept;
[[nodiscard]] int64_t ReadDirectory(
    uint64_t descriptor, os::abi::DirectoryEntry &entry) noexcept;
[[noreturn]] void ExitProcess(int64_t exitCode) noexcept;

}
