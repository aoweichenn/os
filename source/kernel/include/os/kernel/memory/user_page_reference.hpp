#pragma once

#include "os/kernel/sync/spin_lock.hpp"

#include <stdint.h>

namespace os::kernel {

struct UserPageReferenceEntry final {
    uint64_t physical_address;
    uint64_t reference_count;
    bool active;
};

struct UserPageReferenceStatistics final {
    uint64_t capacity{};
    uint64_t active_entry_count{};
    uint64_t active_reference_count{};
    uint64_t peak_active_entry_count{};
    uint64_t peak_active_reference_count{};
    uint64_t first_share_count{};
    uint64_t retain_count{};
    uint64_t release_count{};
    uint64_t exclusive_restore_count{};
};

enum class UserPageReferenceStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidStorage,
    InvalidCapacity,
    InvalidPhysicalAddress,
    CapacityExhausted,
    ReferenceNotFound,
    ReferenceUnderflow,
    NotExclusive,
    ReferenceOverflow,
    Corrupt,
};

// 该表只登记至少经历过一次 fork 的私有用户页。未登记页隐含只有一个映射，
// 因而普通缺页路径不需要为每个驻留页消耗一项元数据。
class UserPageReferenceManager final {
  public:
    UserPageReferenceManager() noexcept = default;
    UserPageReferenceManager(const UserPageReferenceManager &) = delete;
    UserPageReferenceManager &operator=(const UserPageReferenceManager &) = delete;

    [[nodiscard]] UserPageReferenceStatus Initialize(UserPageReferenceEntry *entries,
                                                     uint64_t capacity,
                                                     uint64_t page_size_bytes) noexcept;
    [[nodiscard]] UserPageReferenceStatus RetainForFork(uint64_t physical_address,
                                                       bool &first_share) noexcept;
    [[nodiscard]] UserPageReferenceStatus Release(uint64_t physical_address,
                                                  bool &release_frame) noexcept;
    [[nodiscard]] UserPageReferenceStatus RestoreExclusive(uint64_t physical_address) noexcept;
    [[nodiscard]] UserPageReferenceStatus ReadReferenceCount(uint64_t physical_address,
                                                             uint64_t &reference_count) const
        noexcept;
    [[nodiscard]] UserPageReferenceStatus Validate() const noexcept;
    [[nodiscard]] UserPageReferenceStatistics Statistics() const noexcept;

  private:
    [[nodiscard]] UserPageReferenceEntry *Find(uint64_t physical_address) noexcept;
    [[nodiscard]] const UserPageReferenceEntry *Find(uint64_t physical_address) const noexcept;
    [[nodiscard]] bool IsPhysicalAddressValid(uint64_t physical_address) const noexcept;

    UserPageReferenceEntry *entries_{};
    uint64_t capacity_{};
    uint64_t page_size_bytes_{};
    UserPageReferenceStatistics statistics_{};
    mutable SpinLock lock_{};
    bool initialized_{};
};

}
