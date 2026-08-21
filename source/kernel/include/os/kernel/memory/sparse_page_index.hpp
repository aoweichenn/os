#pragma once

#include <os/kernel/memory/kernel_heap.hpp>
#include <os/kernel/sync/spin_lock.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_SPARSE_PAGE_INDEX_BITS_PER_LEVEL = 6ULL;
inline constexpr uint64_t OS_KERNEL_SPARSE_PAGE_INDEX_SLOT_COUNT = 64ULL;
inline constexpr uint64_t OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL = 10ULL;

enum class SparsePageIndexMark : uint64_t {
    Present,
    Dirty,
    Writeback,
    Error,
};

enum class SparsePageIndexStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidHeap,
    InvalidEntry,
    InvalidRange,
    InvalidMark,
    AlreadyExists,
    NotFound,
    AllocationFailed,
    MetadataReleaseFailed,
    EntriesRemain,
    Corrupt,
};

struct SparsePageIndexStatistics final {
    uint64_t entry_count;
    uint64_t node_count;
    uint64_t peak_node_count;
    uint64_t current_root_level;
    uint64_t peak_root_level;
    uint64_t dirty_entry_count;
    uint64_t writeback_entry_count;
    uint64_t error_entry_count;
    uint64_t successful_insertion_count;
    uint64_t erasure_count;
    uint64_t root_growth_count;
    uint64_t root_shrink_count;
    uint64_t branch_prune_count;
    uint64_t allocation_failure_count;
    uint64_t rollback_node_release_count;
};

class SparsePageIndex final {
  public:
    SparsePageIndex() noexcept = default;
    SparsePageIndex(const SparsePageIndex &) = delete;
    SparsePageIndex &operator=(const SparsePageIndex &) = delete;

    [[nodiscard]] SparsePageIndexStatus Initialize(KernelHeap &heap) noexcept;
    [[nodiscard]] SparsePageIndexStatus Insert(uint64_t page_index, void *entry) noexcept;
    [[nodiscard]] SparsePageIndexStatus Lookup(uint64_t page_index, void *&entry) const noexcept;
    [[nodiscard]] SparsePageIndexStatus Erase(uint64_t page_index, void *&entry) noexcept;
    [[nodiscard]] SparsePageIndexStatus SetMark(uint64_t page_index,
                                                SparsePageIndexMark mark) noexcept;
    [[nodiscard]] SparsePageIndexStatus ClearMark(uint64_t page_index,
                                                  SparsePageIndexMark mark) noexcept;
    [[nodiscard]] SparsePageIndexStatus IsMarked(uint64_t page_index, SparsePageIndexMark mark,
                                                 bool &marked) const noexcept;
    [[nodiscard]] SparsePageIndexStatus FindNext(uint64_t first_page_index,
                                                 uint64_t last_page_index, SparsePageIndexMark mark,
                                                 uint64_t &page_index, void *&entry) const noexcept;
    [[nodiscard]] SparsePageIndexStatus Validate() const noexcept;
    [[nodiscard]] SparsePageIndexStatistics Statistics() const noexcept;
    [[nodiscard]] SparsePageIndexStatus Destroy() noexcept;

  private:
    struct Node;
    struct ValidationCounts;

    [[nodiscard]] static uint64_t RequiredRootLevel(uint64_t page_index) noexcept;
    [[nodiscard]] static uint64_t SlotAtLevel(uint64_t page_index, uint64_t level) noexcept;
    [[nodiscard]] static uint64_t MaximumSlotAtLevel(uint64_t level) noexcept;
    [[nodiscard]] static bool MarkIsMutable(SparsePageIndexMark mark) noexcept;
    [[nodiscard]] static uint64_t MarkBitmap(const Node &node, SparsePageIndexMark mark) noexcept;
    [[nodiscard]] static uint64_t &MutableMarkBitmap(Node &node, SparsePageIndexMark mark) noexcept;
    static void UpdateChildSummary(Node &parent, uint64_t slot, const Node &child) noexcept;
    [[nodiscard]] SparsePageIndexStatus AllocateNode(Node *&node) noexcept;
    [[nodiscard]] SparsePageIndexStatus ReleaseNode(Node *node) noexcept;
    [[nodiscard]] SparsePageIndexStatus ReleaseUncommittedNodes(Node **nodes,
                                                                uint64_t node_count) noexcept;
    [[nodiscard]] Node *FindLeaf(uint64_t page_index) noexcept;
    [[nodiscard]] const Node *FindLeaf(uint64_t page_index) const noexcept;
    void UpdateMarkSummaries(Node **nodes_by_level, const uint64_t *slots_by_level,
                             uint64_t root_level) noexcept;
    [[nodiscard]] bool FindNextInNode(const Node &node, uint64_t level, uint64_t prefix,
                                      uint64_t first_page_index, uint64_t last_page_index,
                                      SparsePageIndexMark mark, uint64_t &page_index,
                                      void *&entry) const noexcept;
    [[nodiscard]] bool ValidateNode(const Node &node, uint64_t level,
                                    ValidationCounts &counts) const noexcept;

    KernelHeap *heap_{nullptr};
    Node *root_{nullptr};
    uint64_t root_level_{};
    SparsePageIndexStatistics statistics_{};
    mutable SpinLock lock_{};
    bool initialized_{};
};

}
