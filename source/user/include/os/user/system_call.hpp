#pragma once

#include "os/abi/system_call.hpp"

#include <stdint.h>

namespace os::user {

inline constexpr uint64_t OS_USER_SYSTEM_CALL_UNUSED_ARGUMENT = 0ULL;

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
[[nodiscard]] int64_t DuplicateDescriptor(uint64_t source_descriptor, uint64_t minimum_descriptor,
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
[[noreturn]] void ExitProcess(int64_t exit_code) noexcept;
}
