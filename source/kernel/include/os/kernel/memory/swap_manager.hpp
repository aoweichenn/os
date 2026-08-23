#pragma once

#include <os/kernel/sync/runtime_mutex.hpp>

#include <stdint.h>

namespace os::kernel {

struct SwapPageIdentity final {
    uint64_t address_space_identifier;
    uint64_t virtual_address;
};

enum class SwapSlotState : uint64_t {
    Empty,
    Active,
    Tombstone,
};

struct SwapSlotEntry final {
    SwapPageIdentity identity;
    uint64_t checksum;
    SwapSlotState state;
};

using SwapEntryReadOperation = bool (*)(void *context, uint64_t slot_index,
                                        SwapSlotEntry &entry) noexcept;
using SwapEntryWriteOperation = bool (*)(void *context, uint64_t slot_index,
                                         const SwapSlotEntry &entry) noexcept;
using SwapReadOperation = bool (*)(void *context, uint64_t slot_index, uint8_t *destination,
                                   uint64_t length_bytes) noexcept;
using SwapWriteOperation = bool (*)(void *context, uint64_t slot_index, const uint8_t *source,
                                    uint64_t length_bytes) noexcept;

struct SwapManagerStatistics final {
    uint64_t slot_capacity;
    uint64_t active_slot_count;
    uint64_t free_slot_count;
    uint64_t peak_active_slot_count;
    uint64_t successful_store_count;
    uint64_t failed_store_count;
    uint64_t successful_load_count;
    uint64_t failed_load_count;
    uint64_t checksum_failure_count;
    uint64_t successful_clone_count;
    uint64_t failed_clone_count;
    uint64_t release_count;
};

enum class SwapManagerStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidPageSize,
    InvalidOperation,
    InvalidIdentity,
    MappingAlreadyStored,
    MappingNotFound,
    CapacityExhausted,
    MetadataReadFailed,
    MetadataWriteFailed,
    WriteFailed,
    ReadFailed,
    ChecksumMismatch,
    CounterOverflow,
    Corrupt,
};

class SwapManager final {
  public:
    [[nodiscard]] SwapManagerStatus Initialize(uint64_t slot_capacity, uint64_t page_size_bytes,
                                               void *operation_context,
                                               SwapEntryReadOperation entry_read_operation,
                                               SwapEntryWriteOperation entry_write_operation,
                                               SwapReadOperation read_operation,
                                               SwapWriteOperation write_operation) noexcept;
    [[nodiscard]] SwapManagerStatus Store(const SwapPageIdentity &identity, const uint8_t *source,
                                          uint64_t length_bytes, uint64_t &slot_index) noexcept;
    [[nodiscard]] SwapManagerStatus LoadAndRelease(const SwapPageIdentity &identity,
                                                   uint8_t *destination,
                                                   uint64_t capacity_bytes) noexcept;
    [[nodiscard]] SwapManagerStatus Clone(const SwapPageIdentity &source_identity,
                                          const SwapPageIdentity &destination_identity,
                                          uint8_t *scratch_page, uint64_t capacity_bytes,
                                          uint64_t &destination_slot_index) noexcept;
    [[nodiscard]] SwapManagerStatus Release(const SwapPageIdentity &identity) noexcept;
    [[nodiscard]] SwapManagerStatus FindSlot(const SwapPageIdentity &identity,
                                             uint64_t &slot_index) const noexcept;
    [[nodiscard]] SwapManagerStatistics Statistics() const noexcept;
    [[nodiscard]] SwapManagerStatus Validate() const noexcept;

  private:
    enum class ProbePurpose : uint64_t {
        Find,
        Insert,
    };

    [[nodiscard]] bool IdentityIsValid(const SwapPageIdentity &identity) const noexcept;
    [[nodiscard]] bool IdentitiesEqual(const SwapPageIdentity &left,
                                       const SwapPageIdentity &right) const noexcept;
    [[nodiscard]] SwapManagerStatus Probe(const SwapPageIdentity &identity, ProbePurpose purpose,
                                          uint64_t &slot_index, SwapSlotEntry &entry,
                                          bool &found) const noexcept;
    [[nodiscard]] uint64_t CalculateInitialSlot(const SwapPageIdentity &identity) const noexcept;
    [[nodiscard]] uint64_t CalculateChecksum(const uint8_t *page) const noexcept;
    [[nodiscard]] SwapManagerStatus StoreUnlocked(const SwapPageIdentity &identity,
                                                  const uint8_t *source,
                                                  uint64_t length_bytes,
                                                  uint64_t &slot_index) noexcept;
    [[nodiscard]] SwapManagerStatus LoadAndReleaseUnlocked(const SwapPageIdentity &identity,
                                                           uint8_t *destination,
                                                           uint64_t capacity_bytes) noexcept;
    [[nodiscard]] SwapManagerStatus ReleaseUnlocked(const SwapPageIdentity &identity) noexcept;

    uint64_t slot_capacity_{};
    uint64_t page_size_bytes_{};
    void *operation_context_{};
    SwapEntryReadOperation entry_read_operation_{};
    SwapEntryWriteOperation entry_write_operation_{};
    SwapReadOperation read_operation_{};
    SwapWriteOperation write_operation_{};
    SwapManagerStatistics statistics_{};
    mutable RuntimeMutex lock_{};
    bool initialized_{};
};

}
