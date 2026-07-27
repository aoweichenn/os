#pragma once

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX = UINT64_MAX;

enum class VirtualMemoryAreaKind : uint64_t {
    ExecutableImage,
    FilePrivate,
    FileShared,
    Anonymous,
    ProgramBreak,
    UserStack,
};

struct VirtualMemoryAreaPermissions final {
    bool readable;
    bool writable;
    bool executable;
};

struct VirtualMemoryArea final {
    uint64_t begin_address;
    uint64_t end_address;
    VirtualMemoryAreaPermissions permissions;
    VirtualMemoryAreaKind kind;
    uint64_t backing_descriptor_index{OS_KERNEL_VMA_INVALID_DESCRIPTOR_INDEX};
    uint64_t backing_generation{};
    uint64_t backing_file_offset_bytes{};
    uint64_t backing_data_length_bytes{};
};

struct VirtualMemoryAreaDescriptor final {
    VirtualMemoryArea area;
    uint64_t previous_descriptor_index;
    uint64_t next_descriptor_index;
    uint64_t owner_identifier;
    bool active;
};

struct VirtualMemoryAreaPoolStatistics final {
    uint64_t capacity;
    uint64_t active_descriptor_count;
    uint64_t free_descriptor_count;
    uint64_t peak_active_descriptor_count;
    uint64_t successful_acquire_count;
    uint64_t release_count;
};

struct VirtualMemoryMapStatistics final {
    uint64_t area_count;
    uint64_t mapped_page_count;
    uint64_t readable_page_count;
    uint64_t writable_page_count;
    uint64_t executable_page_count;
};

enum class VirtualMemoryAreaStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidHardLimit,
    InvalidRange,
    InvalidAlignment,
    InvalidBacking,
    AddressOverflow,
    Overlap,
    MetadataExhausted,
    AreaLimitExceeded,
    KindMismatch,
    NotMapped,
    Corrupt,
};

[[nodiscard]] bool
IsFileBackedVirtualMemoryAreaKind(VirtualMemoryAreaKind kind) noexcept;

class VirtualMemoryMap;

class VirtualMemoryAreaPool final {
  public:
    VirtualMemoryAreaPool() noexcept;
    VirtualMemoryAreaPool(const VirtualMemoryAreaPool &) = delete;
    VirtualMemoryAreaPool &operator=(const VirtualMemoryAreaPool &) = delete;

    [[nodiscard]] VirtualMemoryAreaStatus Initialize(VirtualMemoryAreaDescriptor *descriptors,
                                                     uint64_t capacity) noexcept;
    [[nodiscard]] VirtualMemoryAreaPoolStatistics Statistics() const noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus Validate() const noexcept;

  private:
    friend class VirtualMemoryMap;

    [[nodiscard]] VirtualMemoryAreaStatus Acquire(uint64_t owner_identifier,
                                                  uint64_t &descriptor_index) noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus Release(uint64_t owner_identifier,
                                                  uint64_t descriptor_index) noexcept;
    [[nodiscard]] uint64_t AllocateOwnerIdentifier() noexcept;
    [[nodiscard]] bool IsDescriptorIndexValid(uint64_t descriptor_index) const noexcept;

    VirtualMemoryAreaDescriptor *descriptors_;
    uint64_t capacity_;
    uint64_t free_descriptor_head_index_;
    uint64_t active_descriptor_count_;
    uint64_t peak_active_descriptor_count_;
    uint64_t successful_acquire_count_;
    uint64_t release_count_;
    uint64_t next_owner_identifier_;
    bool initialized_;
};

class VirtualMemoryMap final {
  public:
    VirtualMemoryMap() noexcept;
    VirtualMemoryMap(const VirtualMemoryMap &) = default;
    VirtualMemoryMap &operator=(const VirtualMemoryMap &) = default;

    [[nodiscard]] VirtualMemoryAreaStatus Initialize(VirtualMemoryAreaPool &pool,
                                                     uint64_t page_size_bytes,
                                                     uint64_t hard_area_limit) noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus Insert(const VirtualMemoryArea &area) noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus Remove(uint64_t begin_address, uint64_t end_address,
                                                 VirtualMemoryAreaKind required_kind) noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus FindContaining(uint64_t address,
                                                         VirtualMemoryArea &area) const noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus
    FindFirstGap(uint64_t window_begin_address, uint64_t window_end_address, uint64_t length_bytes,
                 uint64_t alignment_bytes, uint64_t &begin_address) const noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus ReadAt(uint64_t ordinal,
                                                 VirtualMemoryArea &area) const noexcept;
    [[nodiscard]] VirtualMemoryMapStatistics Statistics() const noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus Validate() const noexcept;
    [[nodiscard]] VirtualMemoryAreaStatus Destroy() noexcept;
    [[nodiscard]] uint64_t AreaCount() const noexcept;
    [[nodiscard]] uint64_t PageSizeBytes() const noexcept;

  private:
    [[nodiscard]] bool IsRangeValid(uint64_t begin_address, uint64_t end_address) const noexcept;
    [[nodiscard]] bool AreAttributesEqual(const VirtualMemoryArea &left,
                                          const VirtualMemoryArea &right) const noexcept;
    [[nodiscard]] bool IsBackingValid(const VirtualMemoryArea &area) const noexcept;
    [[nodiscard]] uint64_t AlignUp(uint64_t value, uint64_t alignment) const noexcept;
    void LinkBetween(uint64_t descriptor_index, uint64_t previous_descriptor_index,
                     uint64_t next_descriptor_index) noexcept;
    void Unlink(uint64_t descriptor_index) noexcept;

    VirtualMemoryAreaPool *pool_;
    uint64_t head_descriptor_index_;
    uint64_t area_count_;
    uint64_t page_size_bytes_;
    uint64_t hard_area_limit_;
    uint64_t owner_identifier_;
    bool initialized_;
};

}
