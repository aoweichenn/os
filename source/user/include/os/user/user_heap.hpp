#pragma once

#include <stdint.h>

namespace os::user {

inline constexpr uint64_t OS_USER_HEAP_BLOCK_ALIGNMENT_BYTES = 16ULL;
inline constexpr uint64_t OS_USER_HEAP_BLOCK_HEADER_SIZE_BYTES = 64ULL;

using UserHeapProgramBreakOperation = int64_t (*)(void *context,
                                                  uint64_t requested_address) noexcept;

struct UserHeapConfiguration final {
    void *context;
    UserHeapProgramBreakOperation program_break_operation;
    uint64_t maximum_capacity_bytes;
    uint64_t page_size_bytes;
    uint64_t growth_quantum_bytes;
};

struct UserHeapStatistics final {
    uint64_t capacity_bytes;
    uint64_t maximum_capacity_bytes;
    uint64_t active_allocation_count;
    uint64_t active_requested_bytes;
    uint64_t free_block_count;
    uint64_t largest_free_block_bytes;
    uint64_t successful_allocation_count;
    uint64_t release_count;
    uint64_t growth_count;
    uint64_t failed_allocation_count;
    uint64_t peak_active_requested_bytes;
};

enum class UserHeapStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidConfiguration,
    InvalidSize,
    CapacityExhausted,
    ProgramBreakFailed,
    AllocationNotFound,
    AllocationAlreadyReleased,
    Corrupt,
};

class UserHeap final {
  public:
    UserHeap() noexcept;
    UserHeap(const UserHeap &) = delete;
    UserHeap &operator=(const UserHeap &) = delete;

    [[nodiscard]] UserHeapStatus Initialize(const UserHeapConfiguration &configuration) noexcept;
    [[nodiscard]] UserHeapStatus Allocate(uint64_t size_bytes, void *&allocation) noexcept;
    [[nodiscard]] UserHeapStatus Release(void *allocation) noexcept;
    [[nodiscard]] UserHeapStatistics Statistics() const noexcept;
    [[nodiscard]] UserHeapStatus Validate() const noexcept;

  private:
    struct alignas(OS_USER_HEAP_BLOCK_ALIGNMENT_BYTES) BlockHeader final {
        uint64_t payload_capacity_bytes;
        uint64_t requested_size_bytes;
        uint64_t previous_block_offset;
        uint64_t next_free_block_offset;
        uint64_t previous_free_block_offset;
        uint64_t state;
        uint64_t signature;
        uint64_t reserved;
    };
    static_assert(sizeof(BlockHeader) == OS_USER_HEAP_BLOCK_HEADER_SIZE_BYTES);

    [[nodiscard]] uint64_t AlignUp(uint64_t value, uint64_t alignment) const noexcept;
    [[nodiscard]] BlockHeader *HeaderAt(uint64_t block_offset) const noexcept;
    [[nodiscard]] uint64_t NextPhysicalOffset(uint64_t block_offset,
                                              const BlockHeader &header) const noexcept;
    [[nodiscard]] UserHeapStatus Grow(uint64_t minimum_payload_bytes) noexcept;
    [[nodiscard]] UserHeapStatus FindAllocation(void *allocation,
                                                uint64_t &block_offset) const noexcept;
    void InitializeHeader(BlockHeader &header, uint64_t payload_capacity_bytes,
                          uint64_t previous_block_offset, uint64_t state) noexcept;
    void InsertFreeBlock(uint64_t block_offset) noexcept;
    void RemoveFreeBlock(uint64_t block_offset) noexcept;
    void UpdateNextPreviousOffset(uint64_t block_offset, const BlockHeader &header) noexcept;
    void CoalesceFreeBlock(uint64_t &block_offset) noexcept;

    UserHeapConfiguration configuration_;
    uint8_t *base_address_;
    uint64_t capacity_bytes_;
    uint64_t first_free_block_offset_;
    uint64_t last_block_offset_;
    uint64_t active_allocation_count_;
    uint64_t active_requested_bytes_;
    uint64_t successful_allocation_count_;
    uint64_t release_count_;
    uint64_t growth_count_;
    uint64_t failed_allocation_count_;
    uint64_t peak_active_requested_bytes_;
    bool initialized_;
};

}
