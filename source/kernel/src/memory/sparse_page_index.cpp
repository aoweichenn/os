#include <os/kernel/memory/sparse_page_index.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_SPARSE_PAGE_INDEX_NODE_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_TRANSACTION_NODE_COUNT =
    2ULL *
    (OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL + OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT);

[[nodiscard]] constexpr uint64_t BitmapForSlot(const uint64_t slot) noexcept {
    return OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT << slot;
}

[[nodiscard]] constexpr bool MarkValueIsValid(const SparsePageIndexMark mark) noexcept {
    return static_cast<uint64_t>(mark) <= static_cast<uint64_t>(SparsePageIndexMark::Error);
}

}

struct alignas(OS_KERNEL_SPARSE_PAGE_INDEX_NODE_ALIGNMENT_BYTES) SparsePageIndex::Node final {
    void *slots[OS_KERNEL_SPARSE_PAGE_INDEX_SLOT_COUNT];
    uint64_t present_bitmap;
    uint64_t dirty_bitmap;
    uint64_t writeback_bitmap;
    uint64_t error_bitmap;
};

struct SparsePageIndex::ValidationCounts final {
    uint64_t entry_count;
    uint64_t node_count;
    uint64_t dirty_entry_count;
    uint64_t writeback_entry_count;
    uint64_t error_entry_count;
};

SparsePageIndexStatus SparsePageIndex::Initialize(KernelHeap &heap) noexcept {
    SpinLockGuard guard{this->lock_};
    if (this->initialized_) {
        return SparsePageIndexStatus::AlreadyInitialized;
    }
    if (heap.Validate() != KernelHeapStatus::Succeeded) {
        return SparsePageIndexStatus::InvalidHeap;
    }
    this->heap_ = &heap;
    this->root_ = nullptr;
    this->root_level_ = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    this->statistics_ = SparsePageIndexStatistics{};
    this->initialized_ = true;
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::Insert(const uint64_t page_index,
                                              void *const entry) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->heap_ == nullptr) {
        return SparsePageIndexStatus::NotInitialized;
    }
    if (entry == nullptr) {
        return SparsePageIndexStatus::InvalidEntry;
    }

    Node *const existing_leaf = this->FindLeaf(page_index);
    const uint64_t leaf_slot =
        SparsePageIndex::SlotAtLevel(page_index, OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE);
    if (existing_leaf != nullptr && existing_leaf->slots[leaf_slot] != nullptr) {
        return SparsePageIndexStatus::AlreadyExists;
    }

    const uint64_t required_root_level = SparsePageIndex::RequiredRootLevel(page_index);
    Node *transaction_nodes[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_TRANSACTION_NODE_COUNT]{};
    uint64_t transaction_node_count = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;

    if (this->root_ == nullptr) {
        Node *nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL +
                             OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]{};
        for (uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE; level <= required_root_level;
             ++level) {
            Node *node = nullptr;
            const SparsePageIndexStatus allocation_status = this->AllocateNode(node);
            if (allocation_status != SparsePageIndexStatus::Succeeded) {
                const SparsePageIndexStatus rollback_status =
                    this->ReleaseUncommittedNodes(transaction_nodes, transaction_node_count);
                return rollback_status == SparsePageIndexStatus::Succeeded ? allocation_status
                                                                           : rollback_status;
            }
            nodes_by_level[level] = node;
            transaction_nodes[transaction_node_count] = node;
            ++transaction_node_count;
        }
        for (uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT; level <= required_root_level;
             ++level) {
            Node &parent = *nodes_by_level[level];
            Node &child = *nodes_by_level[level - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT];
            const uint64_t slot = SparsePageIndex::SlotAtLevel(page_index, level);
            parent.slots[slot] = &child;
            SparsePageIndex::UpdateChildSummary(parent, slot, child);
        }
        Node &leaf = *nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE];
        leaf.slots[leaf_slot] = entry;
        leaf.present_bitmap |= BitmapForSlot(leaf_slot);
        for (uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT; level <= required_root_level;
             ++level) {
            SparsePageIndex::UpdateChildSummary(
                *nodes_by_level[level], SparsePageIndex::SlotAtLevel(page_index, level),
                *nodes_by_level[level - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]);
        }
        this->root_ = nodes_by_level[required_root_level];
        this->root_level_ = required_root_level;
        this->statistics_.root_growth_count += required_root_level;
    } else {
        Node *candidate_root = this->root_;
        const uint64_t previous_root_level = this->root_level_;
        if (required_root_level > previous_root_level) {
            for (uint64_t level = previous_root_level + OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT;
                 level <= required_root_level; ++level) {
                Node *new_root = nullptr;
                const SparsePageIndexStatus allocation_status = this->AllocateNode(new_root);
                if (allocation_status != SparsePageIndexStatus::Succeeded) {
                    const SparsePageIndexStatus rollback_status =
                        this->ReleaseUncommittedNodes(transaction_nodes, transaction_node_count);
                    return rollback_status == SparsePageIndexStatus::Succeeded ? allocation_status
                                                                               : rollback_status;
                }
                transaction_nodes[transaction_node_count] = new_root;
                ++transaction_node_count;
                new_root->slots[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE] = candidate_root;
                SparsePageIndex::UpdateChildSummary(
                    *new_root, OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE, *candidate_root);
                candidate_root = new_root;
            }
        }

        Node *current = candidate_root;
        uint64_t current_level =
            required_root_level > previous_root_level ? required_root_level : previous_root_level;
        Node *missing_parent = nullptr;
        uint64_t missing_parent_level = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
        uint64_t missing_parent_slot = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
        Node *new_branch_nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL +
                                        OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]{};
        while (current_level != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            const uint64_t slot = SparsePageIndex::SlotAtLevel(page_index, current_level);
            if (current->slots[slot] == nullptr) {
                missing_parent = current;
                missing_parent_level = current_level;
                missing_parent_slot = slot;
                break;
            }
            current = static_cast<Node *>(current->slots[slot]);
            --current_level;
        }

        Node *leaf = current;
        Node *new_branch_root = nullptr;
        if (missing_parent != nullptr) {
            for (uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
                 level < missing_parent_level; ++level) {
                Node *node = nullptr;
                const SparsePageIndexStatus allocation_status = this->AllocateNode(node);
                if (allocation_status != SparsePageIndexStatus::Succeeded) {
                    const SparsePageIndexStatus rollback_status =
                        this->ReleaseUncommittedNodes(transaction_nodes, transaction_node_count);
                    return rollback_status == SparsePageIndexStatus::Succeeded ? allocation_status
                                                                               : rollback_status;
                }
                new_branch_nodes_by_level[level] = node;
                transaction_nodes[transaction_node_count] = node;
                ++transaction_node_count;
            }
            for (uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT;
                 level < missing_parent_level; ++level) {
                Node &parent = *new_branch_nodes_by_level[level];
                Node &child =
                    *new_branch_nodes_by_level[level - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT];
                const uint64_t slot = SparsePageIndex::SlotAtLevel(page_index, level);
                parent.slots[slot] = &child;
                SparsePageIndex::UpdateChildSummary(parent, slot, child);
            }
            leaf = new_branch_nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE];
            new_branch_root = new_branch_nodes_by_level[missing_parent_level -
                                                        OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT];
        }

        if (leaf == nullptr || leaf->slots[leaf_slot] != nullptr) {
            const SparsePageIndexStatus rollback_status =
                this->ReleaseUncommittedNodes(transaction_nodes, transaction_node_count);
            return rollback_status == SparsePageIndexStatus::Succeeded
                       ? SparsePageIndexStatus::Corrupt
                       : rollback_status;
        }
        leaf->slots[leaf_slot] = entry;
        leaf->present_bitmap |= BitmapForSlot(leaf_slot);
        if (missing_parent != nullptr) {
            for (uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT;
                 level < missing_parent_level; ++level) {
                SparsePageIndex::UpdateChildSummary(
                    *new_branch_nodes_by_level[level],
                    SparsePageIndex::SlotAtLevel(page_index, level),
                    *new_branch_nodes_by_level[level - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]);
            }
            missing_parent->slots[missing_parent_slot] = new_branch_root;
            SparsePageIndex::UpdateChildSummary(*missing_parent, missing_parent_slot,
                                                *new_branch_root);
        }
        if (required_root_level > previous_root_level) {
            this->root_ = candidate_root;
            this->root_level_ = required_root_level;
            this->statistics_.root_growth_count += required_root_level - previous_root_level;
        }
    }

    ++this->statistics_.entry_count;
    ++this->statistics_.successful_insertion_count;
    this->statistics_.current_root_level = this->root_level_;
    if (this->statistics_.peak_root_level < this->root_level_) {
        this->statistics_.peak_root_level = this->root_level_;
    }
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::Lookup(const uint64_t page_index,
                                              void *&entry) const noexcept {
    SpinLockGuard guard{this->lock_};
    entry = nullptr;
    if (!this->initialized_) {
        return SparsePageIndexStatus::NotInitialized;
    }
    const Node *const leaf = this->FindLeaf(page_index);
    if (leaf == nullptr) {
        return SparsePageIndexStatus::NotFound;
    }
    const uint64_t slot =
        SparsePageIndex::SlotAtLevel(page_index, OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE);
    if (leaf->slots[slot] == nullptr) {
        return SparsePageIndexStatus::NotFound;
    }
    entry = leaf->slots[slot];
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::Erase(const uint64_t page_index, void *&entry) noexcept {
    SpinLockGuard guard{this->lock_};
    entry = nullptr;
    if (!this->initialized_ || this->heap_ == nullptr) {
        return SparsePageIndexStatus::NotInitialized;
    }
    if (this->root_ == nullptr ||
        SparsePageIndex::RequiredRootLevel(page_index) > this->root_level_) {
        return SparsePageIndexStatus::NotFound;
    }

    Node *nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL +
                         OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]{};
    uint64_t slots_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL +
                            OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]{};
    Node *current = this->root_;
    for (uint64_t level = this->root_level_;; --level) {
        nodes_by_level[level] = current;
        const uint64_t slot = SparsePageIndex::SlotAtLevel(page_index, level);
        slots_by_level[level] = slot;
        if (level == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            break;
        }
        if (current->slots[slot] == nullptr) {
            return SparsePageIndexStatus::NotFound;
        }
        current = static_cast<Node *>(current->slots[slot]);
    }

    Node &leaf = *nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE];
    const uint64_t leaf_slot = slots_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE];
    if (leaf.slots[leaf_slot] == nullptr) {
        return SparsePageIndexStatus::NotFound;
    }
    const uint64_t leaf_bit = BitmapForSlot(leaf_slot);
    if (((leaf.dirty_bitmap & leaf_bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
         this->statistics_.dirty_entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) ||
        ((leaf.writeback_bitmap & leaf_bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
         this->statistics_.writeback_entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) ||
        ((leaf.error_bitmap & leaf_bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
         this->statistics_.error_entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) ||
        this->statistics_.entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        return SparsePageIndexStatus::Corrupt;
    }

    entry = leaf.slots[leaf_slot];
    leaf.slots[leaf_slot] = nullptr;
    leaf.present_bitmap &= ~leaf_bit;
    if ((leaf.dirty_bitmap & leaf_bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        --this->statistics_.dirty_entry_count;
    }
    if ((leaf.writeback_bitmap & leaf_bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        --this->statistics_.writeback_entry_count;
    }
    if ((leaf.error_bitmap & leaf_bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        --this->statistics_.error_entry_count;
    }
    leaf.dirty_bitmap &= ~leaf_bit;
    leaf.writeback_bitmap &= ~leaf_bit;
    leaf.error_bitmap &= ~leaf_bit;
    --this->statistics_.entry_count;
    ++this->statistics_.erasure_count;

    for (uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT; level <= this->root_level_;
         ++level) {
        Node &child = *nodes_by_level[level - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT];
        Node &parent = *nodes_by_level[level];
        const uint64_t parent_slot = slots_by_level[level];
        if (child.present_bitmap == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            const SparsePageIndexStatus release_status = this->ReleaseNode(&child);
            if (release_status != SparsePageIndexStatus::Succeeded) {
                return release_status;
            }
            const uint64_t parent_bit = BitmapForSlot(parent_slot);
            parent.slots[parent_slot] = nullptr;
            parent.present_bitmap &= ~parent_bit;
            parent.dirty_bitmap &= ~parent_bit;
            parent.writeback_bitmap &= ~parent_bit;
            parent.error_bitmap &= ~parent_bit;
            ++this->statistics_.branch_prune_count;
        } else {
            SparsePageIndex::UpdateChildSummary(parent, parent_slot, child);
        }
    }

    if (this->root_->present_bitmap == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        const SparsePageIndexStatus release_status = this->ReleaseNode(this->root_);
        if (release_status != SparsePageIndexStatus::Succeeded) {
            return release_status;
        }
        this->root_ = nullptr;
        this->root_level_ = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    } else {
        while (this->root_level_ != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
               this->root_->present_bitmap ==
                   BitmapForSlot(OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE)) {
            Node *const child =
                static_cast<Node *>(this->root_->slots[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE]);
            Node *const old_root = this->root_;
            const SparsePageIndexStatus release_status = this->ReleaseNode(old_root);
            if (release_status != SparsePageIndexStatus::Succeeded) {
                return release_status;
            }
            this->root_ = child;
            --this->root_level_;
            ++this->statistics_.root_shrink_count;
        }
    }
    this->statistics_.current_root_level = this->root_level_;
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::SetMark(const uint64_t page_index,
                                               const SparsePageIndexMark mark) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return SparsePageIndexStatus::NotInitialized;
    }
    if (!SparsePageIndex::MarkIsMutable(mark)) {
        return SparsePageIndexStatus::InvalidMark;
    }
    Node *nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL +
                         OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]{};
    uint64_t slots_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL +
                            OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]{};
    Node *current = this->root_;
    if (current == nullptr || SparsePageIndex::RequiredRootLevel(page_index) > this->root_level_) {
        return SparsePageIndexStatus::NotFound;
    }
    for (uint64_t level = this->root_level_;; --level) {
        nodes_by_level[level] = current;
        const uint64_t slot = SparsePageIndex::SlotAtLevel(page_index, level);
        slots_by_level[level] = slot;
        if (level == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            break;
        }
        if (current->slots[slot] == nullptr) {
            return SparsePageIndexStatus::NotFound;
        }
        current = static_cast<Node *>(current->slots[slot]);
    }
    Node &leaf = *nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE];
    const uint64_t leaf_slot = slots_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE];
    if (leaf.slots[leaf_slot] == nullptr) {
        return SparsePageIndexStatus::NotFound;
    }
    uint64_t &bitmap = SparsePageIndex::MutableMarkBitmap(leaf, mark);
    const uint64_t bit = BitmapForSlot(leaf_slot);
    if ((bitmap & bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        return SparsePageIndexStatus::Succeeded;
    }
    bitmap |= bit;
    switch (mark) {
    case SparsePageIndexMark::Dirty:
        ++this->statistics_.dirty_entry_count;
        break;
    case SparsePageIndexMark::Writeback:
        ++this->statistics_.writeback_entry_count;
        break;
    case SparsePageIndexMark::Error:
        ++this->statistics_.error_entry_count;
        break;
    case SparsePageIndexMark::Present:
        return SparsePageIndexStatus::InvalidMark;
    }
    this->UpdateMarkSummaries(nodes_by_level, slots_by_level, this->root_level_);
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::ClearMark(const uint64_t page_index,
                                                 const SparsePageIndexMark mark) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return SparsePageIndexStatus::NotInitialized;
    }
    if (!SparsePageIndex::MarkIsMutable(mark)) {
        return SparsePageIndexStatus::InvalidMark;
    }
    Node *nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL +
                         OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]{};
    uint64_t slots_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL +
                            OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]{};
    Node *current = this->root_;
    if (current == nullptr || SparsePageIndex::RequiredRootLevel(page_index) > this->root_level_) {
        return SparsePageIndexStatus::NotFound;
    }
    for (uint64_t level = this->root_level_;; --level) {
        nodes_by_level[level] = current;
        const uint64_t slot = SparsePageIndex::SlotAtLevel(page_index, level);
        slots_by_level[level] = slot;
        if (level == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            break;
        }
        if (current->slots[slot] == nullptr) {
            return SparsePageIndexStatus::NotFound;
        }
        current = static_cast<Node *>(current->slots[slot]);
    }
    Node &leaf = *nodes_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE];
    const uint64_t leaf_slot = slots_by_level[OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE];
    if (leaf.slots[leaf_slot] == nullptr) {
        return SparsePageIndexStatus::NotFound;
    }
    uint64_t &bitmap = SparsePageIndex::MutableMarkBitmap(leaf, mark);
    const uint64_t bit = BitmapForSlot(leaf_slot);
    if ((bitmap & bit) == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        return SparsePageIndexStatus::Succeeded;
    }
    switch (mark) {
    case SparsePageIndexMark::Dirty:
        if (this->statistics_.dirty_entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            return SparsePageIndexStatus::Corrupt;
        }
        --this->statistics_.dirty_entry_count;
        break;
    case SparsePageIndexMark::Writeback:
        if (this->statistics_.writeback_entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            return SparsePageIndexStatus::Corrupt;
        }
        --this->statistics_.writeback_entry_count;
        break;
    case SparsePageIndexMark::Error:
        if (this->statistics_.error_entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            return SparsePageIndexStatus::Corrupt;
        }
        --this->statistics_.error_entry_count;
        break;
    case SparsePageIndexMark::Present:
        return SparsePageIndexStatus::InvalidMark;
    }
    bitmap &= ~bit;
    this->UpdateMarkSummaries(nodes_by_level, slots_by_level, this->root_level_);
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::IsMarked(const uint64_t page_index,
                                                const SparsePageIndexMark mark,
                                                bool &marked) const noexcept {
    SpinLockGuard guard{this->lock_};
    marked = false;
    if (!this->initialized_) {
        return SparsePageIndexStatus::NotInitialized;
    }
    if (!MarkValueIsValid(mark)) {
        return SparsePageIndexStatus::InvalidMark;
    }
    const Node *const leaf = this->FindLeaf(page_index);
    if (leaf == nullptr) {
        return SparsePageIndexStatus::NotFound;
    }
    const uint64_t slot =
        SparsePageIndex::SlotAtLevel(page_index, OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE);
    if (leaf->slots[slot] == nullptr) {
        return SparsePageIndexStatus::NotFound;
    }
    marked = (SparsePageIndex::MarkBitmap(*leaf, mark) & BitmapForSlot(slot)) !=
             OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::FindNext(const uint64_t first_page_index,
                                                const uint64_t last_page_index,
                                                const SparsePageIndexMark mark,
                                                uint64_t &page_index, void *&entry) const noexcept {
    SpinLockGuard guard{this->lock_};
    page_index = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    entry = nullptr;
    if (!this->initialized_) {
        return SparsePageIndexStatus::NotInitialized;
    }
    if (first_page_index > last_page_index) {
        return SparsePageIndexStatus::InvalidRange;
    }
    if (!MarkValueIsValid(mark)) {
        return SparsePageIndexStatus::InvalidMark;
    }
    if (this->root_ == nullptr ||
        !this->FindNextInNode(*this->root_, this->root_level_,
                              OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE, first_page_index,
                              last_page_index, mark, page_index, entry)) {
        return SparsePageIndexStatus::NotFound;
    }
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->heap_ == nullptr) {
        return SparsePageIndexStatus::NotInitialized;
    }
    if (this->root_level_ > OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL ||
        this->statistics_.current_root_level != this->root_level_ ||
        this->statistics_.peak_root_level < this->root_level_ ||
        this->statistics_.peak_node_count < this->statistics_.node_count) {
        return SparsePageIndexStatus::Corrupt;
    }
    if (this->root_ == nullptr) {
        return this->statistics_.entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
                       this->statistics_.node_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
                       this->statistics_.dirty_entry_count ==
                           OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
                       this->statistics_.writeback_entry_count ==
                           OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
                       this->statistics_.error_entry_count ==
                           OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE
                   ? SparsePageIndexStatus::Succeeded
                   : SparsePageIndexStatus::Corrupt;
    }
    if (this->statistics_.entry_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE ||
        this->statistics_.node_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE ||
        this->root_->present_bitmap == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE ||
        (this->root_level_ != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
         this->root_->present_bitmap == BitmapForSlot(OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE))) {
        return SparsePageIndexStatus::Corrupt;
    }
    ValidationCounts counts{};
    if (!this->ValidateNode(*this->root_, this->root_level_, counts) ||
        counts.entry_count != this->statistics_.entry_count ||
        counts.node_count != this->statistics_.node_count ||
        counts.dirty_entry_count != this->statistics_.dirty_entry_count ||
        counts.writeback_entry_count != this->statistics_.writeback_entry_count ||
        counts.error_entry_count != this->statistics_.error_entry_count) {
        return SparsePageIndexStatus::Corrupt;
    }
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatistics SparsePageIndex::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ ? this->statistics_ : SparsePageIndexStatistics{};
}

SparsePageIndexStatus SparsePageIndex::Destroy() noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return SparsePageIndexStatus::NotInitialized;
    }
    if (this->root_ != nullptr || this->statistics_.entry_count != 0ULL ||
        this->statistics_.node_count != 0ULL) {
        return SparsePageIndexStatus::EntriesRemain;
    }
    this->heap_ = nullptr;
    this->root_level_ = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    this->statistics_ = SparsePageIndexStatistics{};
    this->initialized_ = false;
    return SparsePageIndexStatus::Succeeded;
}

uint64_t SparsePageIndex::RequiredRootLevel(const uint64_t page_index) noexcept {
    uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    uint64_t remaining_index = page_index >> OS_KERNEL_SPARSE_PAGE_INDEX_BITS_PER_LEVEL;
    while (remaining_index != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        ++level;
        remaining_index >>= OS_KERNEL_SPARSE_PAGE_INDEX_BITS_PER_LEVEL;
    }
    return level;
}

uint64_t SparsePageIndex::SlotAtLevel(const uint64_t page_index, const uint64_t level) noexcept {
    const uint64_t shift = level * OS_KERNEL_SPARSE_PAGE_INDEX_BITS_PER_LEVEL;
    return (page_index >> shift) &
           (OS_KERNEL_SPARSE_PAGE_INDEX_SLOT_COUNT - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT);
}

uint64_t SparsePageIndex::MaximumSlotAtLevel(const uint64_t level) noexcept {
    if (level != OS_KERNEL_SPARSE_PAGE_INDEX_MAXIMUM_ROOT_LEVEL) {
        return OS_KERNEL_SPARSE_PAGE_INDEX_SLOT_COUNT - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT;
    }
    return (OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT
            << (64ULL - level * OS_KERNEL_SPARSE_PAGE_INDEX_BITS_PER_LEVEL)) -
           OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT;
}

bool SparsePageIndex::MarkIsMutable(const SparsePageIndexMark mark) noexcept {
    return mark == SparsePageIndexMark::Dirty || mark == SparsePageIndexMark::Writeback ||
           mark == SparsePageIndexMark::Error;
}

uint64_t SparsePageIndex::MarkBitmap(const Node &node, const SparsePageIndexMark mark) noexcept {
    switch (mark) {
    case SparsePageIndexMark::Present:
        return node.present_bitmap;
    case SparsePageIndexMark::Dirty:
        return node.dirty_bitmap;
    case SparsePageIndexMark::Writeback:
        return node.writeback_bitmap;
    case SparsePageIndexMark::Error:
        return node.error_bitmap;
    }
    return OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
}

uint64_t &SparsePageIndex::MutableMarkBitmap(Node &node, const SparsePageIndexMark mark) noexcept {
    switch (mark) {
    case SparsePageIndexMark::Dirty:
        return node.dirty_bitmap;
    case SparsePageIndexMark::Writeback:
        return node.writeback_bitmap;
    case SparsePageIndexMark::Error:
        return node.error_bitmap;
    case SparsePageIndexMark::Present:
        return node.present_bitmap;
    }
    return node.present_bitmap;
}

void SparsePageIndex::UpdateChildSummary(Node &parent, const uint64_t slot,
                                         const Node &child) noexcept {
    const uint64_t bit = BitmapForSlot(slot);
    parent.present_bitmap = child.present_bitmap == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE
                                ? parent.present_bitmap & ~bit
                                : parent.present_bitmap | bit;
    parent.dirty_bitmap = child.dirty_bitmap == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE
                              ? parent.dirty_bitmap & ~bit
                              : parent.dirty_bitmap | bit;
    parent.writeback_bitmap = child.writeback_bitmap == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE
                                  ? parent.writeback_bitmap & ~bit
                                  : parent.writeback_bitmap | bit;
    parent.error_bitmap = child.error_bitmap == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE
                              ? parent.error_bitmap & ~bit
                              : parent.error_bitmap | bit;
}

SparsePageIndexStatus SparsePageIndex::AllocateNode(Node *&node) noexcept {
    node = nullptr;
    void *allocation = nullptr;
    if (this->heap_->TryAllocate(sizeof(Node), alignof(Node), allocation) !=
        KernelHeapStatus::Succeeded) {
        ++this->statistics_.allocation_failure_count;
        return SparsePageIndexStatus::AllocationFailed;
    }
    node = static_cast<Node *>(allocation);
    *node = Node{};
    ++this->statistics_.node_count;
    if (this->statistics_.peak_node_count < this->statistics_.node_count) {
        this->statistics_.peak_node_count = this->statistics_.node_count;
    }
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::ReleaseNode(Node *const node) noexcept {
    if (node == nullptr ||
        this->statistics_.node_count == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
        return SparsePageIndexStatus::Corrupt;
    }
    if (this->heap_->TryRelease(node) != KernelHeapStatus::Succeeded) {
        return SparsePageIndexStatus::MetadataReleaseFailed;
    }
    --this->statistics_.node_count;
    return SparsePageIndexStatus::Succeeded;
}

SparsePageIndexStatus SparsePageIndex::ReleaseUncommittedNodes(Node **const nodes,
                                                               const uint64_t node_count) noexcept {
    SparsePageIndexStatus result = SparsePageIndexStatus::Succeeded;
    for (uint64_t remaining_count = node_count;
         remaining_count != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE; --remaining_count) {
        if (this->ReleaseNode(nodes[remaining_count - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]) ==
            SparsePageIndexStatus::Succeeded) {
            ++this->statistics_.rollback_node_release_count;
        } else {
            result = SparsePageIndexStatus::MetadataReleaseFailed;
        }
    }
    return result;
}

SparsePageIndex::Node *SparsePageIndex::FindLeaf(const uint64_t page_index) noexcept {
    if (this->root_ == nullptr ||
        SparsePageIndex::RequiredRootLevel(page_index) > this->root_level_) {
        return nullptr;
    }
    Node *current = this->root_;
    for (uint64_t level = this->root_level_; level != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
         --level) {
        const uint64_t slot = SparsePageIndex::SlotAtLevel(page_index, level);
        if (current->slots[slot] == nullptr) {
            return nullptr;
        }
        current = static_cast<Node *>(current->slots[slot]);
    }
    return current;
}

const SparsePageIndex::Node *SparsePageIndex::FindLeaf(const uint64_t page_index) const noexcept {
    if (this->root_ == nullptr ||
        SparsePageIndex::RequiredRootLevel(page_index) > this->root_level_) {
        return nullptr;
    }
    const Node *current = this->root_;
    for (uint64_t level = this->root_level_; level != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
         --level) {
        const uint64_t slot = SparsePageIndex::SlotAtLevel(page_index, level);
        if (current->slots[slot] == nullptr) {
            return nullptr;
        }
        current = static_cast<const Node *>(current->slots[slot]);
    }
    return current;
}

void SparsePageIndex::UpdateMarkSummaries(Node **const nodes_by_level,
                                          const uint64_t *const slots_by_level,
                                          const uint64_t root_level) noexcept {
    for (uint64_t level = OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT; level <= root_level; ++level) {
        SparsePageIndex::UpdateChildSummary(
            *nodes_by_level[level], slots_by_level[level],
            *nodes_by_level[level - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT]);
    }
}

bool SparsePageIndex::FindNextInNode(const Node &node, const uint64_t level, const uint64_t prefix,
                                     const uint64_t first_page_index,
                                     const uint64_t last_page_index, const SparsePageIndexMark mark,
                                     uint64_t &page_index, void *&entry) const noexcept {
    const uint64_t marked_slots = SparsePageIndex::MarkBitmap(node, mark);
    const uint64_t maximum_slot = SparsePageIndex::MaximumSlotAtLevel(level);
    for (uint64_t slot = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE; slot <= maximum_slot; ++slot) {
        const uint64_t bit = BitmapForSlot(slot);
        if ((marked_slots & bit) == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            continue;
        }
        if (level == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            const uint64_t candidate_page_index = prefix | slot;
            if (candidate_page_index < first_page_index || candidate_page_index > last_page_index ||
                node.slots[slot] == nullptr) {
                continue;
            }
            page_index = candidate_page_index;
            entry = node.slots[slot];
            return true;
        }
        const uint64_t shift = level * OS_KERNEL_SPARSE_PAGE_INDEX_BITS_PER_LEVEL;
        const uint64_t child_prefix = prefix | (slot << shift);
        const uint64_t child_end =
            child_prefix | ((OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT << shift) -
                            OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT);
        if (child_end < first_page_index || child_prefix > last_page_index ||
            node.slots[slot] == nullptr) {
            continue;
        }
        if (this->FindNextInNode(*static_cast<const Node *>(node.slots[slot]),
                                 level - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT, child_prefix,
                                 first_page_index, last_page_index, mark, page_index, entry)) {
            return true;
        }
    }
    return false;
}

bool SparsePageIndex::ValidateNode(const Node &node, const uint64_t level,
                                   ValidationCounts &counts) const noexcept {
    ++counts.node_count;
    uint64_t expected_present_bitmap = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    uint64_t expected_dirty_bitmap = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    uint64_t expected_writeback_bitmap = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    uint64_t expected_error_bitmap = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
    const uint64_t maximum_slot = SparsePageIndex::MaximumSlotAtLevel(level);
    for (uint64_t slot = OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
         slot < OS_KERNEL_SPARSE_PAGE_INDEX_SLOT_COUNT; ++slot) {
        const uint64_t bit = BitmapForSlot(slot);
        if (slot > maximum_slot) {
            if (node.slots[slot] != nullptr || (node.present_bitmap & bit) != 0ULL ||
                (node.dirty_bitmap & bit) != 0ULL || (node.writeback_bitmap & bit) != 0ULL ||
                (node.error_bitmap & bit) != 0ULL) {
                return false;
            }
            continue;
        }
        if (node.slots[slot] == nullptr) {
            continue;
        }
        expected_present_bitmap |= bit;
        if (level == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            ++counts.entry_count;
            if ((node.dirty_bitmap & bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
                expected_dirty_bitmap |= bit;
                ++counts.dirty_entry_count;
            }
            if ((node.writeback_bitmap & bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
                expected_writeback_bitmap |= bit;
                ++counts.writeback_entry_count;
            }
            if ((node.error_bitmap & bit) != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
                expected_error_bitmap |= bit;
                ++counts.error_entry_count;
            }
            continue;
        }
        const Node &child = *static_cast<const Node *>(node.slots[slot]);
        if (child.present_bitmap == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE ||
            !this->ValidateNode(child, level - OS_KERNEL_SPARSE_PAGE_INDEX_SINGLE_UNIT, counts)) {
            return false;
        }
        if (child.dirty_bitmap != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            expected_dirty_bitmap |= bit;
        }
        if (child.writeback_bitmap != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            expected_writeback_bitmap |= bit;
        }
        if (child.error_bitmap != OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE) {
            expected_error_bitmap |= bit;
        }
    }
    return node.present_bitmap == expected_present_bitmap &&
           node.dirty_bitmap == expected_dirty_bitmap &&
           node.writeback_bitmap == expected_writeback_bitmap &&
           node.error_bitmap == expected_error_bitmap &&
           (node.dirty_bitmap & ~node.present_bitmap) == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
           (node.writeback_bitmap & ~node.present_bitmap) ==
               OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE &&
           (node.error_bitmap & ~node.present_bitmap) == OS_KERNEL_SPARSE_PAGE_INDEX_EMPTY_VALUE;
}

}
