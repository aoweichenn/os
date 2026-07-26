#pragma once

#include "os/kernel/memory/kernel_heap.hpp"
#include "os/kernel/object/kernel_object.hpp"
#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_FILE_TABLE_STANDARD_INPUT_DESCRIPTOR = 0ULL;
inline constexpr uint64_t OS_KERNEL_FILE_TABLE_STANDARD_OUTPUT_DESCRIPTOR = 1ULL;
inline constexpr uint64_t OS_KERNEL_FILE_TABLE_STANDARD_ERROR_DESCRIPTOR = 2ULL;
inline constexpr uint64_t OS_KERNEL_FILE_TABLE_FIRST_DYNAMIC_DESCRIPTOR = 3ULL;
inline constexpr uint64_t OS_KERNEL_FILE_TABLE_CHUNK_DESCRIPTOR_COUNT = 64ULL;
inline constexpr uint64_t OS_KERNEL_FILE_TABLE_FUNCTIONAL_HARD_LIMIT = 256ULL;
inline constexpr uint64_t OS_KERNEL_FILE_TABLE_MAXIMUM_HARD_LIMIT = 4096ULL;
inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG = 1ULL << 0ULL;
inline constexpr uint64_t OS_KERNEL_FILE_DESCRIPTOR_VALID_FLAG_MASK =
    OS_KERNEL_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG;

enum class FileTableStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    NotInitialized,
    InvalidDependency,
    InvalidLimit,
    InvalidDescriptor,
    DescriptorOccupied,
    SoftLimitExceeded,
    InvalidFlags,
    AllocationFailed,
    ObjectFailure,
    ReleaseFailed,
    CorruptedState,
};

struct FileTableStatistics final {
    uint64_t soft_limit;
    uint64_t hard_limit;
    uint64_t active_descriptor_count;
    uint64_t allocated_chunk_count;
    uint64_t peak_active_descriptor_count;
    uint64_t peak_allocated_chunk_count;
    uint64_t successful_installation_count;
    uint64_t successful_lookup_count;
    uint64_t successful_duplicate_count;
    uint64_t successful_close_count;
    uint64_t close_on_exec_count;
    uint64_t chunk_allocation_count;
    uint64_t chunk_release_count;
    uint64_t limit_rejection_count;
    uint64_t two_phase_rollback_count;
};

struct FileTableChunk;

class FileTable final {
  public:
    FileTable() noexcept = default;

    FileTable(const FileTable &) = delete;
    FileTable &operator=(const FileTable &) = delete;

    // 表只保存强引用句柄；成功安装会把传入引用的所有权转移给表。
    [[nodiscard]] FileTableStatus Initialize(KernelHeap &heap, KernelObjectManager &object_manager,
                                             uint64_t soft_limit, uint64_t hard_limit) noexcept;
    [[nodiscard]] FileTableStatus Install(KernelObjectReference &reference,
                                          uint64_t minimum_descriptor, uint64_t descriptor_flags,
                                          uint64_t &descriptor) noexcept;
    [[nodiscard]] FileTableStatus InstallExact(KernelObjectReference &reference,
                                               uint64_t descriptor,
                                               uint64_t descriptor_flags) noexcept;
    [[nodiscard]] FileTableStatus Lookup(uint64_t descriptor,
                                         KernelObjectReference &reference) noexcept;
    [[nodiscard]] FileTableStatus Duplicate(uint64_t source_descriptor, uint64_t minimum_descriptor,
                                            uint64_t descriptor_flags,
                                            uint64_t &destination_descriptor) noexcept;
    [[nodiscard]] FileTableStatus Close(uint64_t descriptor,
                                        KernelObjectReleaseResult &release_result) noexcept;
    [[nodiscard]] FileTableStatus GetDescriptorFlags(uint64_t descriptor,
                                                     uint64_t &descriptor_flags) noexcept;
    [[nodiscard]] FileTableStatus SetDescriptorFlags(uint64_t descriptor,
                                                     uint64_t descriptor_flags) noexcept;
    [[nodiscard]] FileTableStatus SetSoftLimit(uint64_t soft_limit) noexcept;
    [[nodiscard]] FileTableStatus CloseOnExec(uint64_t &closed_descriptor_count) noexcept;
    [[nodiscard]] FileTableStatus Destroy() noexcept;
    [[nodiscard]] FileTableStatus Validate() noexcept;
    [[nodiscard]] FileTableStatistics Statistics() noexcept;

  private:
    [[nodiscard]] FileTableChunk *FindChunk(uint64_t chunk_base_descriptor) noexcept;
    [[nodiscard]] FileTableStatus EnsureChunk(uint64_t chunk_base_descriptor) noexcept;
    [[nodiscard]] bool
    FindLowestAvailableDescriptor(uint64_t minimum_descriptor, uint64_t &descriptor,
                                  uint64_t &missing_chunk_base_descriptor) noexcept;
    [[nodiscard]] FileTableStatus ValidateLimits(uint64_t soft_limit,
                                                 uint64_t hard_limit) const noexcept;
    [[nodiscard]] bool AreDescriptorFlagsValid(uint64_t descriptor_flags) const noexcept;
    void InsertChunk(FileTableChunk &chunk) noexcept;

    KernelHeap *heap_;
    KernelObjectManager *object_manager_;
    FileTableChunk *chunk_head_;
    SpinLock lock_;
    FileTableStatistics statistics_;
    bool initialized_;
};

}
