#pragma once

#include <os/abi/virtual_memory.hpp>
#include <os/kernel/fs/block_cache.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/memory/file_page_cache.hpp>
#include <os/kernel/memory/memory_pressure.hpp>
#include <os/kernel/memory/physical_frame_allocator.hpp>
#include <os/kernel/memory/swap_manager.hpp>
#include <os/kernel/memory/user_page_reference.hpp>
#include <os/kernel/memory/virtual_memory_area.hpp>
#include <os/kernel/user/user_elf.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_USER_STACK_TOP_VIRTUAL_ADDRESS =
    os::abi::OS_ABI_USER_STACK_TOP_ADDRESS;
inline constexpr uint64_t OS_KERNEL_USER_STACK_SIZE_BYTES =
    os::abi::OS_ABI_USER_STACK_MAXIMUM_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_USER_STACK_PAGE_COUNT =
    OS_KERNEL_USER_STACK_SIZE_BYTES / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_USER_STACK_BOTTOM_VIRTUAL_ADDRESS =
    os::abi::OS_ABI_USER_STACK_BOTTOM_ADDRESS;
inline constexpr uint64_t OS_KERNEL_USER_STACK_GUARD_VIRTUAL_ADDRESS =
    os::abi::OS_ABI_USER_STACK_GUARD_ADDRESS;
inline constexpr uint64_t OS_KERNEL_USER_VMA_DESCRIPTOR_POOL_CAPACITY = 8192ULL;
inline constexpr uint64_t OS_KERNEL_USER_VMA_PER_PROCESS_HARD_LIMIT = 4096ULL;
inline constexpr uint64_t OS_KERNEL_USER_FILE_BACKING_CAPACITY = 1024ULL;
inline constexpr uint64_t OS_KERNEL_USER_FILE_PAGE_CACHE_MAXIMUM_CAPACITY = 4096ULL;
inline constexpr uint64_t OS_KERNEL_USER_PAGE_REFERENCE_CAPACITY = 32768ULL;

struct UserAddressSpace final {
    uint64_t root_physical_address;
    uint64_t entry_virtual_address;
    uint64_t stack_top_virtual_address;
    uint64_t stack_committed_bottom_virtual_address;
    uint64_t program_break_base_address;
    uint64_t program_break_address;
    uint64_t program_break_limit_address;
    uint64_t mapped_page_count;
    uint64_t peak_mapped_page_count;
    uint64_t demand_page_fault_count;
    uint64_t stack_growth_page_fault_count;
    uint64_t unmap_released_page_count;
    uint64_t page_table_reclaimed_frame_count;
    uint64_t file_page_fault_count;
    uint64_t page_cache_hit_count;
    uint64_t private_file_resident_page_count;
    uint64_t shared_file_resident_page_count;
    uint64_t copy_on_write_page_count;
    uint64_t copy_on_write_fault_count;
    uint64_t copy_on_write_copy_count;
    uint64_t copy_on_write_exclusive_restore_count;
    uint64_t fork_clone_count;
    uint64_t swapped_page_count;
    uint64_t peak_swapped_page_count;
    uint64_t swap_out_page_count;
    uint64_t swap_in_page_count;
    uint64_t swap_read_failure_count;
    uint64_t swap_corruption_count;
    uint64_t reclaim_scan_virtual_address;
    uint64_t committed_page_count;
    uint64_t peak_committed_page_count;
    uint64_t address_space_identifier;
    VirtualMemoryMap virtual_memory_map;
};

enum class UserAddressSpaceStatus : uint64_t {
    Succeeded,
    InvalidElf,
    StackCollision,
    PageTableCreationFailed,
    PageAllocationFailed,
    PageMappingFailed,
    ImageReadFailed,
    VirtualMemoryInitializationFailed,
    VirtualMemoryAreaFailure,
    ProgramBreakCollision,
    StackPreparationFailed,
    ForkReferenceExhausted,
    ForkBackingFailure,
    AddressSpaceIdentifierExhausted,
    SwapInitializationFailed,
    CommitLimitExceeded,
    RollbackFailed,
};

enum class UserVirtualMemoryStatus : uint64_t {
    Succeeded,
    NotInitialized,
    InvalidRange,
    InvalidProtection,
    InvalidFile,
    UnsupportedMapping,
    AddressInUse,
    AddressSpaceExhausted,
    MetadataExhausted,
    PageAllocationFailed,
    PageMappingFailed,
    PageReleaseFailed,
    FileReadFailed,
    FileWriteFailed,
    PageCacheExhausted,
    SwapReadFailed,
    SwapCorrupt,
    CopyOnWriteFailure,
    ThreadMemoryInUse,
    ResourceLimitExceeded,
    CommitLimitExceeded,
    Corrupt,
};

enum class UserPageFaultStatus : uint64_t {
    Handled,
    NotUserFault,
    PresentPageViolation,
    ReservedBitViolation,
    InstructionFetchViolation,
    AreaNotMapped,
    PermissionDenied,
    InvalidStackGrowth,
    PageAllocationFailed,
    PageMappingFailed,
    FileReadFailed,
    PageCacheExhausted,
    SwapReadFailed,
    SwapCorrupt,
    CopyOnWriteFailure,
    Corrupt,
};

enum class UserMemoryCopyStatus : uint64_t {
    Succeeded,
    NullDestination,
    NullSource,
    DestinationTooSmall,
    SourceTooSmall,
    InvalidUserRange,
    PageNotMapped,
    PageNotUserAccessible,
    PageNotWritable,
    PageResolutionFailed,
};

enum class UserSwapInitializationStage : uint64_t {
    NotStarted,
    StorageReady,
    ManagerReady,
    PressureReady,
    OvercommitReady,
    StorageSelfTestPassed,
    Ready,
};

[[nodiscard]] UserAddressSpaceStatus InitializeUserVirtualMemory() noexcept;
[[nodiscard]] UserAddressSpaceStatus AttachUserSwap(FileSystemBlockDevice &device) noexcept;
[[nodiscard]] UserSwapInitializationStage GetUserSwapInitializationStage() noexcept;
[[nodiscard]] VirtualMemoryAreaPoolStatistics GetUserVirtualMemoryPoolStatistics() noexcept;
[[nodiscard]] FilePageCacheStatistics GetUserFilePageCacheStatistics() noexcept;
[[nodiscard]] MemoryPressureStatistics GetUserMemoryPressureStatistics() noexcept;
[[nodiscard]] MemoryOvercommitStatistics GetUserMemoryOvercommitStatistics() noexcept;
[[nodiscard]] SwapManagerStatistics GetUserSwapStatistics() noexcept;
[[nodiscard]] bool ValidateUserMemoryManagement() noexcept;
[[nodiscard]] MemoryOvercommitStatus
CommitUserMemory(UserAddressSpace &address_space, uint64_t page_count, bool privileged) noexcept;
[[nodiscard]] MemoryOvercommitStatus UncommitUserMemory(UserAddressSpace &address_space,
                                                        uint64_t page_count) noexcept;
[[nodiscard]] UserPageReferenceStatistics GetUserPageReferenceStatistics() noexcept;
[[nodiscard]] UserAddressSpaceStatus
LoadUserAddressSpace(const uint8_t *image, uint64_t image_size_bytes,
                     UserAddressSpace &address_space,
                     UserElfValidationStatus &elf_validation_status) noexcept;
[[nodiscard]] UserAddressSpaceStatus
LoadUserAddressSpace(fs::Vfs &vfs, const fs::OpenFile &open_file, UserAddressSpace &address_space,
                     UserElfValidationStatus &elf_validation_status) noexcept;
[[nodiscard]] UserAddressSpaceStatus
CloneUserAddressSpaceForFork(UserAddressSpace &parent_address_space,
                             UserAddressSpace &child_address_space, bool privileged) noexcept;
[[nodiscard]] UserAddressSpaceStatus
RestoreUserAddressSpaceAfterFailedFork(UserAddressSpace &parent_address_space) noexcept;
[[nodiscard]] UserAddressSpaceStatus
DestroyUserAddressSpace(UserAddressSpace &address_space) noexcept;
[[nodiscard]] UserAddressSpaceStatus PrepareUserStack(UserAddressSpace &address_space,
                                                      uint64_t lowest_required_address) noexcept;
[[nodiscard]] UserAddressSpaceStatus PrepareUserStackRange(UserAddressSpace &address_space,
                                                           uint64_t lowest_required_address,
                                                           uint64_t current_stack_pointer) noexcept;
[[nodiscard]] UserVirtualMemoryStatus
MapAnonymousMemory(UserAddressSpace &address_space, uint64_t requested_address,
                   uint64_t length_bytes, uint64_t protection_flags, uint64_t map_flags,
                   uint64_t &mapped_address) noexcept;
[[nodiscard]] UserVirtualMemoryStatus
MapFileMemory(UserAddressSpace &address_space, fs::Vfs &vfs, const fs::OpenFile &open_file,
              uint64_t requested_address, uint64_t length_bytes, uint64_t protection_flags,
              uint64_t map_flags, uint64_t file_offset_bytes, uint64_t &mapped_address) noexcept;
[[nodiscard]] UserVirtualMemoryStatus UnmapAnonymousMemory(UserAddressSpace &address_space,
                                                           uint64_t address,
                                                           uint64_t length_bytes) noexcept;
[[nodiscard]] UserVirtualMemoryStatus
UnmapFileMemory(UserAddressSpace &address_space, uint64_t address, uint64_t length_bytes) noexcept;
[[nodiscard]] UserVirtualMemoryStatus RevokeUserFileMappings(UserAddressSpace &address_space,
                                                             const FileIdentity &identity) noexcept;
[[nodiscard]] UserVirtualMemoryStatus
InvalidateUserFilePageCache(const FileIdentity &identity,
                            uint64_t current_file_size_bytes) noexcept;
[[nodiscard]] UserVirtualMemoryStatus TrimUserFilePageCache() noexcept;
[[nodiscard]] UserVirtualMemoryStatus
ProtectUserSharedFileMappings(UserAddressSpace &address_space) noexcept;
[[nodiscard]] UserVirtualMemoryStatus
WritebackUserFilePageCache(uint64_t maximum_page_count, uint64_t &written_page_count) noexcept;
[[nodiscard]] UserVirtualMemoryStatus SetProgramBreak(UserAddressSpace &address_space,
                                                      uint64_t requested_address,
                                                      uint64_t &program_break_address) noexcept;
[[nodiscard]] os::abi::VirtualMemoryStatistics
GetUserVirtualMemoryStatistics(const UserAddressSpace &address_space) noexcept;
[[nodiscard]] UserPageFaultStatus HandleUserPageFault(UserAddressSpace &address_space,
                                                      uint64_t fault_address, uint64_t error_code,
                                                      uint64_t user_stack_pointer) noexcept;
[[nodiscard]] UserVirtualMemoryStatus ResolveUserReturnMemory(UserAddressSpace &address_space,
                                                              uint64_t instruction_pointer,
                                                              uint64_t stack_pointer) noexcept;
void SetActiveUserAddressSpace(UserAddressSpace *address_space) noexcept;
[[nodiscard]] UserMemoryCopyStatus CopyToUserAddressSpace(UserAddressSpace &address_space,
                                                          uint64_t user_address,
                                                          uint64_t length_bytes,
                                                          const uint8_t *source,
                                                          uint64_t source_size_bytes) noexcept;
[[nodiscard]] UserMemoryCopyStatus CopyFromUser(uint64_t user_address, uint64_t length_bytes,
                                                uint8_t *destination,
                                                uint64_t destination_capacity_bytes) noexcept;
[[nodiscard]] UserMemoryCopyStatus ValidateUserWritableMemory(uint64_t user_address,
                                                              uint64_t length_bytes) noexcept;
[[nodiscard]] UserMemoryCopyStatus CopyToUser(uint64_t user_address, uint64_t length_bytes,
                                              const uint8_t *source,
                                              uint64_t source_size_bytes) noexcept;
}
