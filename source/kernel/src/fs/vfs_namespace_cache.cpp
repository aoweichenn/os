#include <os/kernel/fs/vfs_namespace_cache.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_FIRST_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_POSITIVE_ACCESS_INCREMENT = 2ULL;
constexpr uint8_t OS_KERNEL_VFS_NAMESPACE_CACHE_PATH_SEPARATOR = static_cast<uint8_t>('/');
constexpr uint8_t OS_KERNEL_VFS_NAMESPACE_CACHE_DOT_CHARACTER = static_cast<uint8_t>('.');
constexpr uint8_t OS_KERNEL_VFS_NAMESPACE_CACHE_DELETE_CONTROL_CHARACTER = 0x7FU;
constexpr uint8_t OS_KERNEL_VFS_NAMESPACE_CACHE_MAXIMUM_CONTROL_CHARACTER = 0x1FU;
constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_HASH_OFFSET = 1469598103934665603ULL;
constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_HASH_PRIME = 1099511628211ULL;
constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_UINT64_BYTE_COUNT = 8ULL;
constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_VFS_NAMESPACE_CACHE_BYTE_MASK = 0xFFULL;

[[nodiscard]] bool NodeTypeIsCacheable(const NodeType type) noexcept {
    return type == NodeType::RegularFile || type == NodeType::Directory ||
           type == NodeType::CharacterDevice || type == NodeType::SymbolicLink;
}

[[nodiscard]] os::abi::FileMode ModeTypeForNode(const NodeType type) noexcept {
    if (type == NodeType::RegularFile) {
        return os::abi::OS_ABI_FILE_MODE_REGULAR;
    }
    if (type == NodeType::Directory) {
        return os::abi::OS_ABI_FILE_MODE_DIRECTORY;
    }
    if (type == NodeType::CharacterDevice) {
        return os::abi::OS_ABI_FILE_MODE_CHARACTER_DEVICE;
    }
    if (type == NodeType::SymbolicLink) {
        return os::abi::OS_ABI_FILE_MODE_SYMBOLIC_LINK;
    }
    return static_cast<os::abi::FileMode>(OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE);
}

[[nodiscard]] bool NameByteIsValid(const uint8_t value) noexcept {
    return value > OS_KERNEL_VFS_NAMESPACE_CACHE_MAXIMUM_CONTROL_CHARACTER &&
           value != OS_KERNEL_VFS_NAMESPACE_CACHE_DELETE_CONTROL_CHARACTER &&
           value != OS_KERNEL_VFS_NAMESPACE_CACHE_PATH_SEPARATOR;
}

[[nodiscard]] bool IsDot(const VfsDentryKey &key) noexcept {
    return key.name_length_bytes == OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT &&
           key.name[OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE] ==
               OS_KERNEL_VFS_NAMESPACE_CACHE_DOT_CHARACTER;
}

[[nodiscard]] bool IsDotDot(const VfsDentryKey &key) noexcept {
    return key.name_length_bytes == OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT +
                                        OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT &&
           key.name[OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE] ==
               OS_KERNEL_VFS_NAMESPACE_CACHE_DOT_CHARACTER &&
           key.name[OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT] ==
               OS_KERNEL_VFS_NAMESPACE_CACHE_DOT_CHARACTER;
}

[[nodiscard]] bool CounterCanIncrease(
    const uint64_t value,
    const uint64_t increment = OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT) noexcept {
    return value <= UINT64_MAX - increment;
}

[[nodiscard]] bool BackendNodeInformationIsEmpty(const BackendNodeInformation &metadata) noexcept {
    return metadata.size_bytes == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.allocated_size_bytes == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.link_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.access_time_nanoseconds == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.modification_time_nanoseconds == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.change_time_nanoseconds == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.birth_time_nanoseconds == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.owner_user_identifier == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.owner_group_identifier == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           metadata.mode == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
}

[[nodiscard]] uint64_t HashByte(const uint64_t hash_value, const uint8_t byte) noexcept {
    return (hash_value ^ byte) * OS_KERNEL_VFS_NAMESPACE_CACHE_HASH_PRIME;
}

[[nodiscard]] uint64_t HashUint64(uint64_t hash_value, const uint64_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         byte_index < OS_KERNEL_VFS_NAMESPACE_CACHE_UINT64_BYTE_COUNT; ++byte_index) {
        const uint64_t shift = byte_index * OS_KERNEL_VFS_NAMESPACE_CACHE_BITS_PER_BYTE;
        hash_value =
            HashByte(hash_value, static_cast<uint8_t>((value >> shift) &
                                                      OS_KERNEL_VFS_NAMESPACE_CACHE_BYTE_MASK));
    }
    return hash_value;
}

[[nodiscard]] uint64_t HashInodeIdentity(const VfsInodeIdentity &identity) noexcept {
    uint64_t hash_value = OS_KERNEL_VFS_NAMESPACE_CACHE_HASH_OFFSET;
    hash_value = HashUint64(hash_value, identity.superblock_identifier);
    hash_value = HashUint64(hash_value, identity.superblock_generation);
    hash_value = HashUint64(hash_value, identity.node_identifier);
    return HashUint64(hash_value, identity.node_generation);
}

[[nodiscard]] uint64_t HashDentryKey(const VfsDentryKey &key) noexcept {
    uint64_t hash_value =
        HashUint64(OS_KERNEL_VFS_NAMESPACE_CACHE_HASH_OFFSET, key.mount_identifier);
    hash_value = HashUint64(hash_value, HashInodeIdentity(key.parent));
    hash_value = HashUint64(hash_value, key.name_length_bytes);
    for (uint64_t byte_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         byte_index < key.name_length_bytes; ++byte_index) {
        hash_value = HashByte(hash_value, key.name[byte_index]);
    }
    return hash_value;
}

}

VfsNamespaceCacheStatus VfsNamespaceCache::Initialize(VfsDentrySlot *const dentry_storage,
                                                      const uint64_t dentry_capacity,
                                                      VfsInodeSlot *const inode_storage,
                                                      const uint64_t inode_capacity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (this->initialized_) {
        return VfsNamespaceCacheStatus::AlreadyInitialized;
    }
    if (dentry_storage == nullptr || inode_storage == nullptr) {
        return VfsNamespaceCacheStatus::InvalidStorage;
    }
    if (dentry_capacity == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        inode_capacity == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return VfsNamespaceCacheStatus::InvalidCapacity;
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < dentry_capacity; ++slot_index) {
        dentry_storage[slot_index] = VfsDentrySlot{};
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < inode_capacity; ++slot_index) {
        inode_storage[slot_index] = VfsInodeSlot{};
    }
    this->dentries_ = dentry_storage;
    this->dentry_capacity_ = dentry_capacity;
    this->inodes_ = inode_storage;
    this->inode_capacity_ = inode_capacity;
    this->dentry_hash_entries_ = nullptr;
    this->dentry_hash_buckets_ = nullptr;
    this->dentry_hash_bucket_capacity_ = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    this->inode_hash_entries_ = nullptr;
    this->inode_hash_buckets_ = nullptr;
    this->inode_hash_bucket_capacity_ = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    this->access_generation_ = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    this->statistics_ = VfsNamespaceCacheStatistics{};
    this->statistics_.dentry_capacity = dentry_capacity;
    this->statistics_.inode_capacity = inode_capacity;
    this->initialized_ = true;
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::ConfigureHashIndex(
    VfsNamespaceHashEntry *const dentry_entries, const uint64_t dentry_entry_capacity,
    uint64_t *const dentry_buckets, const uint64_t dentry_bucket_capacity,
    VfsNamespaceHashEntry *const inode_entries, const uint64_t inode_entry_capacity,
    uint64_t *const inode_buckets, const uint64_t inode_bucket_capacity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (this->HashIndexIsConfigured()) {
        return VfsNamespaceCacheStatus::AlreadyInitialized;
    }
    if (dentry_entries == nullptr || dentry_buckets == nullptr || inode_entries == nullptr ||
        inode_buckets == nullptr) {
        return VfsNamespaceCacheStatus::InvalidStorage;
    }
    if (dentry_entry_capacity != this->dentry_capacity_ ||
        inode_entry_capacity != this->inode_capacity_ ||
        dentry_bucket_capacity < this->dentry_capacity_ ||
        inode_bucket_capacity < this->inode_capacity_ ||
        dentry_bucket_capacity == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        inode_bucket_capacity == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return VfsNamespaceCacheStatus::InvalidCapacity;
    }
    if (this->statistics_.active_dentry_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        this->statistics_.active_inode_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return VfsNamespaceCacheStatus::EntriesRemain;
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < dentry_entry_capacity; ++slot_index) {
        dentry_entries[slot_index] = VfsNamespaceHashEntry{
            .hash_value = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
            .next_slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX,
            .indexed = false,
        };
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < inode_entry_capacity; ++slot_index) {
        inode_entries[slot_index] = VfsNamespaceHashEntry{
            .hash_value = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
            .next_slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX,
            .indexed = false,
        };
    }
    for (uint64_t bucket_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         bucket_index < dentry_bucket_capacity; ++bucket_index) {
        dentry_buckets[bucket_index] = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    }
    for (uint64_t bucket_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         bucket_index < inode_bucket_capacity; ++bucket_index) {
        inode_buckets[bucket_index] = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    }
    this->dentry_hash_entries_ = dentry_entries;
    this->dentry_hash_buckets_ = dentry_buckets;
    this->dentry_hash_bucket_capacity_ = dentry_bucket_capacity;
    this->inode_hash_entries_ = inode_entries;
    this->inode_hash_buckets_ = inode_buckets;
    this->inode_hash_bucket_capacity_ = inode_bucket_capacity;
    this->statistics_.dentry_hash_bucket_capacity = dentry_bucket_capacity;
    this->statistics_.inode_hash_bucket_capacity = inode_bucket_capacity;
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::RebuildHashBuckets(
    uint64_t *const dentry_buckets, const uint64_t dentry_bucket_capacity,
    uint64_t *const inode_buckets, const uint64_t inode_bucket_capacity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || !this->HashIndexIsConfigured()) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (dentry_buckets == nullptr || inode_buckets == nullptr) {
        return VfsNamespaceCacheStatus::InvalidStorage;
    }
    if (dentry_bucket_capacity < this->dentry_capacity_ ||
        inode_bucket_capacity < this->inode_capacity_) {
        return VfsNamespaceCacheStatus::InvalidCapacity;
    }
    if (this->statistics_.hash_rebuild_count == UINT64_MAX) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->dentry_capacity_; ++slot_index) {
        const VfsNamespaceHashEntry &entry = this->dentry_hash_entries_[slot_index];
        const bool should_be_indexed =
            this->dentries_[slot_index].state == VfsNamespaceEntryState::Cached;
        if (entry.indexed != should_be_indexed ||
            (entry.indexed && entry.hash_value != HashDentryKey(this->dentries_[slot_index].key))) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->inode_capacity_; ++slot_index) {
        const VfsNamespaceHashEntry &entry = this->inode_hash_entries_[slot_index];
        const bool should_be_indexed =
            this->inodes_[slot_index].state == VfsNamespaceEntryState::Cached;
        if (entry.indexed != should_be_indexed ||
            (entry.indexed &&
             entry.hash_value != HashInodeIdentity(this->inodes_[slot_index].identity))) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
    }

    for (uint64_t bucket_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         bucket_index < dentry_bucket_capacity; ++bucket_index) {
        dentry_buckets[bucket_index] = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->dentry_capacity_; ++slot_index) {
        VfsNamespaceHashEntry &entry = this->dentry_hash_entries_[slot_index];
        if (!entry.indexed) {
            continue;
        }
        if (this->dentries_[slot_index].state != VfsNamespaceEntryState::Cached) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        const uint64_t bucket_index = entry.hash_value % dentry_bucket_capacity;
        entry.next_slot_index = dentry_buckets[bucket_index];
        dentry_buckets[bucket_index] = slot_index;
    }
    for (uint64_t bucket_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         bucket_index < inode_bucket_capacity; ++bucket_index) {
        inode_buckets[bucket_index] = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->inode_capacity_; ++slot_index) {
        VfsNamespaceHashEntry &entry = this->inode_hash_entries_[slot_index];
        if (!entry.indexed) {
            continue;
        }
        if (this->inodes_[slot_index].state != VfsNamespaceEntryState::Cached) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        const uint64_t bucket_index = entry.hash_value % inode_bucket_capacity;
        entry.next_slot_index = inode_buckets[bucket_index];
        inode_buckets[bucket_index] = slot_index;
    }

    this->dentry_hash_buckets_ = dentry_buckets;
    this->dentry_hash_bucket_capacity_ = dentry_bucket_capacity;
    this->inode_hash_buckets_ = inode_buckets;
    this->inode_hash_bucket_capacity_ = inode_bucket_capacity;
    this->statistics_.dentry_hash_bucket_capacity = dentry_bucket_capacity;
    this->statistics_.inode_hash_bucket_capacity = inode_bucket_capacity;
    ++this->statistics_.hash_rebuild_count;
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::PublishPositive(const VfsDentryKey &key,
                                                           const VfsInodeIdentity &inode_identity,
                                                           const NodeType inode_type,
                                                           VfsDentryToken &token) noexcept {
    SpinLockGuard guard{this->lock_};
    token = VfsDentryToken{};
    if (!this->initialized_ || this->dentries_ == nullptr || this->inodes_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (!VfsDentryKeyIsValid(key)) {
        return VfsNamespaceCacheStatus::InvalidKey;
    }
    if (!VfsInodeIdentityIsValid(inode_identity)) {
        return VfsNamespaceCacheStatus::InvalidIdentity;
    }
    if (!NodeTypeIsCacheable(inode_type)) {
        return VfsNamespaceCacheStatus::InvalidNodeType;
    }
    const uint64_t existing_dentry_index = this->FindCachedDentry(key);
    if (existing_dentry_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        VfsDentrySlot &existing_dentry = this->dentries_[existing_dentry_index];
        const bool same_positive =
            existing_dentry.kind == VfsDentryKind::Positive &&
            this->InodeTokenIsValid(existing_dentry.inode_token) &&
            this->inodes_[existing_dentry.inode_token.slot_index].state ==
                VfsNamespaceEntryState::Cached &&
            VfsInodeIdentitiesEqual(this->inodes_[existing_dentry.inode_token.slot_index].identity,
                                    inode_identity) &&
            this->inodes_[existing_dentry.inode_token.slot_index].type == inode_type;
        if (!same_positive) {
            if (!CounterCanIncrease(this->statistics_.conflict_count)) {
                return VfsNamespaceCacheStatus::CounterOverflow;
            }
            ++this->statistics_.conflict_count;
            return VfsNamespaceCacheStatus::EntryConflict;
        }
        if (!CounterCanIncrease(this->access_generation_,
                                OS_KERNEL_VFS_NAMESPACE_CACHE_POSITIVE_ACCESS_INCREMENT) ||
            !CounterCanIncrease(this->statistics_.already_cached_count)) {
            return VfsNamespaceCacheStatus::CounterOverflow;
        }
        static_cast<void>(this->TouchDentry(existing_dentry));
        static_cast<void>(this->TouchInode(this->inodes_[existing_dentry.inode_token.slot_index]));
        ++this->statistics_.already_cached_count;
        token = VfsDentryToken{
            .slot_index = existing_dentry_index,
            .generation = existing_dentry.generation,
        };
        return VfsNamespaceCacheStatus::AlreadyCached;
    }

    const uint64_t dentry_index = this->FindFreeDentry();
    if (dentry_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        if (!CounterCanIncrease(this->statistics_.capacity_rejection_count)) {
            return VfsNamespaceCacheStatus::CounterOverflow;
        }
        ++this->statistics_.capacity_rejection_count;
        return VfsNamespaceCacheStatus::CapacityExhausted;
    }
    uint64_t inode_index = this->FindCachedInode(inode_identity);
    const bool create_inode = inode_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    if (create_inode) {
        inode_index = this->FindFreeInode();
        if (inode_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
            if (!CounterCanIncrease(this->statistics_.capacity_rejection_count)) {
                return VfsNamespaceCacheStatus::CounterOverflow;
            }
            ++this->statistics_.capacity_rejection_count;
            return VfsNamespaceCacheStatus::CapacityExhausted;
        }
    } else if (this->inodes_[inode_index].type != inode_type) {
        if (!CounterCanIncrease(this->statistics_.conflict_count)) {
            return VfsNamespaceCacheStatus::CounterOverflow;
        }
        ++this->statistics_.conflict_count;
        return VfsNamespaceCacheStatus::EntryConflict;
    }
    VfsDentrySlot &dentry = this->dentries_[dentry_index];
    VfsInodeSlot &inode = this->inodes_[inode_index];
    if (dentry.generation == UINT64_MAX || (create_inode && inode.generation == UINT64_MAX)) {
        return VfsNamespaceCacheStatus::GenerationExhausted;
    }
    if (!CounterCanIncrease(this->access_generation_,
                            OS_KERNEL_VFS_NAMESPACE_CACHE_POSITIVE_ACCESS_INCREMENT) ||
        !CounterCanIncrease(this->statistics_.active_dentry_count) ||
        !CounterCanIncrease(this->statistics_.cached_positive_dentry_count) ||
        !CounterCanIncrease(this->statistics_.positive_publish_count) ||
        !CounterCanIncrease(inode.dentry_reference_count) ||
        !CounterCanIncrease(this->statistics_.inode_dentry_reference_count) ||
        (create_inode && (!CounterCanIncrease(this->statistics_.active_inode_count) ||
                          !CounterCanIncrease(this->statistics_.cached_inode_count)))) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    if (create_inode) {
        const uint64_t inode_generation =
            inode.generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE
                ? OS_KERNEL_VFS_NAMESPACE_CACHE_FIRST_GENERATION
                : inode.generation + OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT;
        inode = VfsInodeSlot{
            .identity = inode_identity,
            .metadata = BackendNodeInformation{},
            .access_generation = this->NextAccessGeneration(),
            .dentry_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
            .external_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
            .generation = inode_generation,
            .metadata_generation = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
            .type = inode_type,
            .state = VfsNamespaceEntryState::Cached,
            .metadata_state = VfsInodeMetadataState::Empty,
        };
        if (!this->InsertInodeIndex(inode_index)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        ++this->statistics_.active_inode_count;
        ++this->statistics_.cached_inode_count;
        if (this->statistics_.active_inode_count > this->statistics_.peak_active_inode_count) {
            this->statistics_.peak_active_inode_count = this->statistics_.active_inode_count;
        }
    } else {
        static_cast<void>(this->TouchInode(inode));
    }
    ++inode.dentry_reference_count;
    ++this->statistics_.inode_dentry_reference_count;
    const uint64_t dentry_generation =
        dentry.generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE
            ? OS_KERNEL_VFS_NAMESPACE_CACHE_FIRST_GENERATION
            : dentry.generation + OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT;
    dentry = VfsDentrySlot{
        .key = key,
        .inode_token =
            VfsInodeToken{
                .slot_index = inode_index,
                .generation = inode.generation,
            },
        .access_generation = this->NextAccessGeneration(),
        .external_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
        .generation = dentry_generation,
        .kind = VfsDentryKind::Positive,
        .state = VfsNamespaceEntryState::Cached,
    };
    if (!this->InsertDentryIndex(dentry_index)) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    ++this->statistics_.active_dentry_count;
    ++this->statistics_.cached_positive_dentry_count;
    ++this->statistics_.positive_publish_count;
    if (this->statistics_.active_dentry_count > this->statistics_.peak_active_dentry_count) {
        this->statistics_.peak_active_dentry_count = this->statistics_.active_dentry_count;
    }
    const uint64_t inode_reference_count =
        inode.dentry_reference_count + inode.external_reference_count;
    if (inode_reference_count > this->statistics_.peak_inode_reference_count) {
        this->statistics_.peak_inode_reference_count = inode_reference_count;
    }
    token = VfsDentryToken{
        .slot_index = dentry_index,
        .generation = dentry_generation,
    };
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::PublishNegative(const VfsDentryKey &key,
                                                           VfsDentryToken &token) noexcept {
    SpinLockGuard guard{this->lock_};
    token = VfsDentryToken{};
    if (!this->initialized_ || this->dentries_ == nullptr || this->inodes_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (!VfsDentryKeyIsValid(key)) {
        return VfsNamespaceCacheStatus::InvalidKey;
    }
    const uint64_t existing_index = this->FindCachedDentry(key);
    if (existing_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        VfsDentrySlot &existing = this->dentries_[existing_index];
        if (existing.kind != VfsDentryKind::Negative) {
            if (!CounterCanIncrease(this->statistics_.conflict_count)) {
                return VfsNamespaceCacheStatus::CounterOverflow;
            }
            ++this->statistics_.conflict_count;
            return VfsNamespaceCacheStatus::EntryConflict;
        }
        if (!CounterCanIncrease(this->access_generation_) ||
            !CounterCanIncrease(this->statistics_.already_cached_count)) {
            return VfsNamespaceCacheStatus::CounterOverflow;
        }
        static_cast<void>(this->TouchDentry(existing));
        ++this->statistics_.already_cached_count;
        token = VfsDentryToken{
            .slot_index = existing_index,
            .generation = existing.generation,
        };
        return VfsNamespaceCacheStatus::AlreadyCached;
    }
    const uint64_t dentry_index = this->FindFreeDentry();
    if (dentry_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        if (!CounterCanIncrease(this->statistics_.capacity_rejection_count)) {
            return VfsNamespaceCacheStatus::CounterOverflow;
        }
        ++this->statistics_.capacity_rejection_count;
        return VfsNamespaceCacheStatus::CapacityExhausted;
    }
    VfsDentrySlot &dentry = this->dentries_[dentry_index];
    if (dentry.generation == UINT64_MAX) {
        return VfsNamespaceCacheStatus::GenerationExhausted;
    }
    if (!CounterCanIncrease(this->access_generation_) ||
        !CounterCanIncrease(this->statistics_.active_dentry_count) ||
        !CounterCanIncrease(this->statistics_.cached_negative_dentry_count) ||
        !CounterCanIncrease(this->statistics_.negative_publish_count)) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    const uint64_t generation =
        dentry.generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE
            ? OS_KERNEL_VFS_NAMESPACE_CACHE_FIRST_GENERATION
            : dentry.generation + OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT;
    dentry = VfsDentrySlot{
        .key = key,
        .inode_token = VfsInodeToken{},
        .access_generation = this->NextAccessGeneration(),
        .external_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
        .generation = generation,
        .kind = VfsDentryKind::Negative,
        .state = VfsNamespaceEntryState::Cached,
    };
    if (!this->InsertDentryIndex(dentry_index)) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    ++this->statistics_.active_dentry_count;
    ++this->statistics_.cached_negative_dentry_count;
    ++this->statistics_.negative_publish_count;
    if (this->statistics_.active_dentry_count > this->statistics_.peak_active_dentry_count) {
        this->statistics_.peak_active_dentry_count = this->statistics_.active_dentry_count;
    }
    token = VfsDentryToken{
        .slot_index = dentry_index,
        .generation = generation,
    };
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::AcquireDentry(const VfsDentryKey &key,
                                                         VfsDentrySnapshot &snapshot) noexcept {
    SpinLockGuard guard{this->lock_};
    snapshot = VfsDentrySnapshot{};
    if (!this->initialized_ || this->dentries_ == nullptr || this->inodes_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (!VfsDentryKeyIsValid(key)) {
        return VfsNamespaceCacheStatus::InvalidKey;
    }
    const uint64_t dentry_index = this->FindCachedDentry(key);
    if (dentry_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        if (!CounterCanIncrease(this->statistics_.dentry_miss_count)) {
            return VfsNamespaceCacheStatus::CounterOverflow;
        }
        ++this->statistics_.dentry_miss_count;
        return VfsNamespaceCacheStatus::DentryNotFound;
    }
    VfsDentrySlot &dentry = this->dentries_[dentry_index];
    const bool positive = dentry.kind == VfsDentryKind::Positive;
    if ((positive &&
         (!this->InodeTokenIsValid(dentry.inode_token) ||
          this->inodes_[dentry.inode_token.slot_index].state != VfsNamespaceEntryState::Cached)) ||
        (!positive && dentry.kind != VfsDentryKind::Negative)) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    const uint64_t access_increment = positive
                                          ? OS_KERNEL_VFS_NAMESPACE_CACHE_POSITIVE_ACCESS_INCREMENT
                                          : OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT;
    uint64_t &hit_count =
        positive ? this->statistics_.positive_hit_count : this->statistics_.negative_hit_count;
    if (!CounterCanIncrease(this->access_generation_, access_increment) ||
        !CounterCanIncrease(dentry.external_reference_count) ||
        !CounterCanIncrease(this->statistics_.active_dentry_reference_count) ||
        !CounterCanIncrease(hit_count) ||
        (dentry.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
         !CounterCanIncrease(this->statistics_.referenced_dentry_count))) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    if (dentry.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        ++this->statistics_.referenced_dentry_count;
    }
    ++dentry.external_reference_count;
    ++this->statistics_.active_dentry_reference_count;
    ++hit_count;
    static_cast<void>(this->TouchDentry(dentry));
    if (positive) {
        static_cast<void>(this->TouchInode(this->inodes_[dentry.inode_token.slot_index]));
    }
    if (this->statistics_.active_dentry_reference_count >
        this->statistics_.peak_active_dentry_reference_count) {
        this->statistics_.peak_active_dentry_reference_count =
            this->statistics_.active_dentry_reference_count;
    }
    snapshot = this->SnapshotDentry(dentry_index);
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::ReleaseDentry(const VfsDentryToken token) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->DentryTokenIsValid(token)) {
        return VfsNamespaceCacheStatus::InvalidToken;
    }
    VfsDentrySlot &dentry = this->dentries_[token.slot_index];
    if (dentry.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        this->statistics_.active_dentry_reference_count ==
            OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        !CounterCanIncrease(this->statistics_.dentry_release_count)) {
        return VfsNamespaceCacheStatus::InvalidState;
    }
    --dentry.external_reference_count;
    --this->statistics_.active_dentry_reference_count;
    ++this->statistics_.dentry_release_count;
    if (dentry.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        if (this->statistics_.referenced_dentry_count ==
            OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        --this->statistics_.referenced_dentry_count;
        if (dentry.state == VfsNamespaceEntryState::Stale &&
            !this->ReleaseDentrySlot(token.slot_index, true, false)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
    }
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::AcquireInode(const VfsInodeIdentity &identity,
                                                        VfsInodeSnapshot &snapshot) noexcept {
    SpinLockGuard guard{this->lock_};
    snapshot = VfsInodeSnapshot{};
    if (!this->initialized_ || this->inodes_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (!VfsInodeIdentityIsValid(identity)) {
        return VfsNamespaceCacheStatus::InvalidIdentity;
    }
    const uint64_t inode_index = this->FindCachedInode(identity);
    if (inode_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        if (!CounterCanIncrease(this->statistics_.inode_miss_count)) {
            return VfsNamespaceCacheStatus::CounterOverflow;
        }
        ++this->statistics_.inode_miss_count;
        return VfsNamespaceCacheStatus::InodeNotFound;
    }
    VfsInodeSlot &inode = this->inodes_[inode_index];
    if (!CounterCanIncrease(this->access_generation_) ||
        !CounterCanIncrease(inode.external_reference_count) ||
        !CounterCanIncrease(this->statistics_.active_inode_external_reference_count) ||
        !CounterCanIncrease(this->statistics_.inode_hit_count) ||
        (inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
         !CounterCanIncrease(this->statistics_.referenced_inode_count))) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    if (inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        ++this->statistics_.referenced_inode_count;
    }
    ++inode.external_reference_count;
    ++this->statistics_.active_inode_external_reference_count;
    ++this->statistics_.inode_hit_count;
    static_cast<void>(this->TouchInode(inode));
    const uint64_t reference_count = inode.external_reference_count + inode.dentry_reference_count;
    if (reference_count > this->statistics_.peak_inode_reference_count) {
        this->statistics_.peak_inode_reference_count = reference_count;
    }
    snapshot = this->SnapshotInode(inode_index);
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::ReleaseInode(const VfsInodeToken token) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->InodeTokenIsValid(token)) {
        return VfsNamespaceCacheStatus::InvalidToken;
    }
    VfsInodeSlot &inode = this->inodes_[token.slot_index];
    if (inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        this->statistics_.active_inode_external_reference_count ==
            OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        !CounterCanIncrease(this->statistics_.inode_release_count)) {
        return VfsNamespaceCacheStatus::InvalidState;
    }
    --inode.external_reference_count;
    --this->statistics_.active_inode_external_reference_count;
    ++this->statistics_.inode_release_count;
    if (inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        if (this->statistics_.referenced_inode_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        --this->statistics_.referenced_inode_count;
        if (inode.state == VfsNamespaceEntryState::Stale &&
            inode.dentry_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
            !this->ReleaseInodeSlot(token.slot_index, true, false)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
    }
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus
VfsNamespaceCache::PrepareInodeMetadata(const VfsInodeIdentity &identity, const NodeType type,
                                        VfsInodeMetadataToken &token,
                                        VfsInodeMetadataSnapshot &snapshot) noexcept {
    SpinLockGuard guard{this->lock_};
    token = VfsInodeMetadataToken{};
    snapshot = VfsInodeMetadataSnapshot{};
    if (!this->initialized_ || this->inodes_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (!VfsInodeIdentityIsValid(identity)) {
        return VfsNamespaceCacheStatus::InvalidIdentity;
    }
    if (!NodeTypeIsCacheable(type)) {
        return VfsNamespaceCacheStatus::InvalidNodeType;
    }

    uint64_t inode_index = this->FindCachedInode(identity);
    const bool create_inode = inode_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    if (create_inode) {
        inode_index = this->FindFreeInode();
        if (inode_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
            if (!CounterCanIncrease(this->statistics_.capacity_rejection_count)) {
                return VfsNamespaceCacheStatus::CounterOverflow;
            }
            ++this->statistics_.capacity_rejection_count;
            return VfsNamespaceCacheStatus::CapacityExhausted;
        }
    } else {
        VfsInodeSlot &existing = this->inodes_[inode_index];
        if (existing.type != type) {
            if (!CounterCanIncrease(this->statistics_.conflict_count)) {
                return VfsNamespaceCacheStatus::CounterOverflow;
            }
            ++this->statistics_.conflict_count;
            return VfsNamespaceCacheStatus::EntryConflict;
        }
        if (existing.metadata_state == VfsInodeMetadataState::Ready) {
            if (!CounterCanIncrease(this->access_generation_) ||
                !CounterCanIncrease(this->statistics_.inode_metadata_hit_count)) {
                return VfsNamespaceCacheStatus::CounterOverflow;
            }
            static_cast<void>(this->TouchInode(existing));
            ++this->statistics_.inode_metadata_hit_count;
            snapshot = this->SnapshotInodeMetadata(inode_index);
            return VfsNamespaceCacheStatus::AlreadyCached;
        }
        if (existing.metadata_state == VfsInodeMetadataState::Loading) {
            if (!CounterCanIncrease(this->statistics_.inode_metadata_load_contention_count)) {
                return VfsNamespaceCacheStatus::CounterOverflow;
            }
            ++this->statistics_.inode_metadata_load_contention_count;
            return VfsNamespaceCacheStatus::InodeMetadataLoadInProgress;
        }
        if (existing.metadata_state != VfsInodeMetadataState::Empty) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
    }

    VfsInodeSlot &inode = this->inodes_[inode_index];
    if ((create_inode && inode.generation == UINT64_MAX) ||
        inode.metadata_generation == UINT64_MAX) {
        return VfsNamespaceCacheStatus::GenerationExhausted;
    }
    if (!CounterCanIncrease(this->access_generation_) ||
        !CounterCanIncrease(this->statistics_.loading_inode_metadata_count) ||
        !CounterCanIncrease(this->statistics_.inode_metadata_miss_count) ||
        !CounterCanIncrease(this->statistics_.inode_metadata_load_start_count) ||
        (create_inode && (!CounterCanIncrease(this->statistics_.active_inode_count) ||
                          !CounterCanIncrease(this->statistics_.cached_inode_count)))) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    if (create_inode) {
        const uint64_t inode_generation =
            inode.generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE
                ? OS_KERNEL_VFS_NAMESPACE_CACHE_FIRST_GENERATION
                : inode.generation + OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT;
        inode = VfsInodeSlot{
            .identity = identity,
            .metadata = BackendNodeInformation{},
            .access_generation = this->NextAccessGeneration(),
            .dentry_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
            .external_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
            .generation = inode_generation,
            .metadata_generation = OS_KERNEL_VFS_NAMESPACE_CACHE_FIRST_GENERATION,
            .type = type,
            .state = VfsNamespaceEntryState::Cached,
            .metadata_state = VfsInodeMetadataState::Loading,
        };
        if (!this->InsertInodeIndex(inode_index)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        ++this->statistics_.active_inode_count;
        ++this->statistics_.cached_inode_count;
        if (this->statistics_.active_inode_count > this->statistics_.peak_active_inode_count) {
            this->statistics_.peak_active_inode_count = this->statistics_.active_inode_count;
        }
    } else {
        inode.metadata = BackendNodeInformation{};
        inode.metadata_generation += OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT;
        inode.metadata_state = VfsInodeMetadataState::Loading;
        static_cast<void>(this->TouchInode(inode));
    }
    ++this->statistics_.loading_inode_metadata_count;
    ++this->statistics_.inode_metadata_miss_count;
    ++this->statistics_.inode_metadata_load_start_count;
    token = VfsInodeMetadataToken{
        .inode_token =
            VfsInodeToken{
                .slot_index = inode_index,
                .generation = inode.generation,
            },
        .metadata_generation = inode.metadata_generation,
    };
    return VfsNamespaceCacheStatus::InodeMetadataLoadRequired;
}

VfsNamespaceCacheStatus
VfsNamespaceCache::CompleteInodeMetadata(const VfsInodeMetadataToken token,
                                         const BackendNodeInformation &metadata) noexcept {
    SpinLockGuard guard{this->lock_};
    // inode 与 metadata 两级 generation 同时匹配，失效后的迟到后端结果不能重新发布。
    if (!this->InodeMetadataTokenIsValid(token)) {
        return VfsNamespaceCacheStatus::InvalidToken;
    }
    VfsInodeSlot &inode = this->inodes_[token.inode_token.slot_index];
    if (!VfsInodeMetadataIsValid(metadata, inode.type)) {
        return VfsNamespaceCacheStatus::InvalidMetadata;
    }
    if (this->statistics_.loading_inode_metadata_count ==
        OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    if (!CounterCanIncrease(this->statistics_.ready_inode_metadata_count) ||
        !CounterCanIncrease(this->statistics_.inode_metadata_load_completion_count) ||
        !CounterCanIncrease(this->access_generation_)) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    inode.metadata = metadata;
    inode.metadata_state = VfsInodeMetadataState::Ready;
    static_cast<void>(this->TouchInode(inode));
    --this->statistics_.loading_inode_metadata_count;
    ++this->statistics_.ready_inode_metadata_count;
    ++this->statistics_.inode_metadata_load_completion_count;
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus
VfsNamespaceCache::CancelInodeMetadata(const VfsInodeMetadataToken token) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->InodeMetadataTokenIsValid(token)) {
        return VfsNamespaceCacheStatus::InvalidToken;
    }
    VfsInodeSlot &inode = this->inodes_[token.inode_token.slot_index];
    if (this->statistics_.loading_inode_metadata_count ==
        OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    if (!CounterCanIncrease(this->statistics_.inode_metadata_load_cancellation_count)) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    inode.metadata = BackendNodeInformation{};
    inode.metadata_state = VfsInodeMetadataState::Empty;
    --this->statistics_.loading_inode_metadata_count;
    ++this->statistics_.inode_metadata_load_cancellation_count;
    if (inode.dentry_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
        inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
        !this->ReleaseInodeSlot(token.inode_token.slot_index, false, false)) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus
VfsNamespaceCache::InvalidateInodeMetadata(const VfsInodeIdentity &identity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->inodes_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (!VfsInodeIdentityIsValid(identity)) {
        return VfsNamespaceCacheStatus::InvalidIdentity;
    }
    const uint64_t inode_index = this->FindCachedInode(identity);
    if (inode_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        return VfsNamespaceCacheStatus::InodeNotFound;
    }
    VfsInodeSlot &inode = this->inodes_[inode_index];
    if (inode.metadata_state == VfsInodeMetadataState::Empty) {
        return VfsNamespaceCacheStatus::InodeMetadataNotFound;
    }
    if (inode.metadata_state != VfsInodeMetadataState::Loading &&
        inode.metadata_state != VfsInodeMetadataState::Ready) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    uint64_t &state_count = inode.metadata_state == VfsInodeMetadataState::Loading
                                ? this->statistics_.loading_inode_metadata_count
                                : this->statistics_.ready_inode_metadata_count;
    if (state_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    if (!CounterCanIncrease(this->statistics_.inode_metadata_invalidation_count)) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    inode.metadata = BackendNodeInformation{};
    inode.metadata_state = VfsInodeMetadataState::Empty;
    --state_count;
    ++this->statistics_.inode_metadata_invalidation_count;
    if (inode.dentry_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
        inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
        !this->ReleaseInodeSlot(inode_index, false, false)) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::InvalidateDentry(const VfsDentryKey &key) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->dentries_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (!VfsDentryKeyIsValid(key)) {
        return VfsNamespaceCacheStatus::InvalidKey;
    }
    const uint64_t dentry_index = this->FindCachedDentry(key);
    if (dentry_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        return VfsNamespaceCacheStatus::DentryNotFound;
    }
    if (!CounterCanIncrease(this->statistics_.dentry_invalidation_count) ||
        !this->MarkDentryStale(dentry_index, false)) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    ++this->statistics_.dentry_invalidation_count;
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus
VfsNamespaceCache::InvalidateInode(const VfsInodeIdentity &identity) noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->dentries_ == nullptr || this->inodes_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    if (!VfsInodeIdentityIsValid(identity)) {
        return VfsNamespaceCacheStatus::InvalidIdentity;
    }
    const uint64_t inode_index = this->FindCachedInode(identity);
    if (inode_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
        return VfsNamespaceCacheStatus::InodeNotFound;
    }
    VfsInodeSlot &inode = this->inodes_[inode_index];
    const bool invalidates_metadata = inode.metadata_state != VfsInodeMetadataState::Empty;
    if ((inode.metadata_state == VfsInodeMetadataState::Loading &&
         this->statistics_.loading_inode_metadata_count ==
             OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) ||
        (inode.metadata_state == VfsInodeMetadataState::Ready &&
         this->statistics_.ready_inode_metadata_count ==
             OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) ||
        (inode.metadata_state != VfsInodeMetadataState::Empty &&
         inode.metadata_state != VfsInodeMetadataState::Loading &&
         inode.metadata_state != VfsInodeMetadataState::Ready)) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    if (this->statistics_.cached_inode_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        !CounterCanIncrease(this->statistics_.stale_inode_count) ||
        !CounterCanIncrease(this->statistics_.inode_invalidation_count) ||
        (invalidates_metadata &&
         !CounterCanIncrease(this->statistics_.inode_metadata_invalidation_count))) {
        return VfsNamespaceCacheStatus::CounterOverflow;
    }
    const VfsInodeToken inode_token{
        .slot_index = inode_index,
        .generation = inode.generation,
    };
    if (!this->RemoveInodeIndex(inode_index)) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    if (inode.metadata_state == VfsInodeMetadataState::Loading) {
        --this->statistics_.loading_inode_metadata_count;
    } else if (inode.metadata_state == VfsInodeMetadataState::Ready) {
        --this->statistics_.ready_inode_metadata_count;
    }
    if (invalidates_metadata) {
        ++this->statistics_.inode_metadata_invalidation_count;
    }
    inode.metadata = BackendNodeInformation{};
    inode.metadata_state = VfsInodeMetadataState::Empty;
    inode.state = VfsNamespaceEntryState::Stale;
    --this->statistics_.cached_inode_count;
    ++this->statistics_.stale_inode_count;
    ++this->statistics_.inode_invalidation_count;
    for (uint64_t dentry_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         dentry_index < this->dentry_capacity_; ++dentry_index) {
        const VfsDentrySlot &dentry = this->dentries_[dentry_index];
        if (dentry.state != VfsNamespaceEntryState::Cached) {
            continue;
        }
        const bool targets_inode = dentry.kind == VfsDentryKind::Positive &&
                                   dentry.inode_token.slot_index == inode_token.slot_index &&
                                   dentry.inode_token.generation == inode_token.generation;
        if ((targets_inode || VfsInodeIdentitiesEqual(dentry.key.parent, identity)) &&
            !this->MarkDentryStale(dentry_index, true)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
    }
    if (this->InodeTokenIsValid(inode_token)) {
        const VfsInodeSlot &remaining_inode = this->inodes_[inode_index];
        if (remaining_inode.state == VfsNamespaceEntryState::Stale &&
            remaining_inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
            remaining_inode.dentry_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
            !this->ReleaseInodeSlot(inode_index, true, false)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
    }
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::EvictDentries(const uint64_t maximum_entry_count,
                                                         uint64_t &evicted_entry_count) noexcept {
    SpinLockGuard guard{this->lock_};
    evicted_entry_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    if (!this->initialized_ || this->dentries_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    while (evicted_entry_count < maximum_entry_count) {
        const uint64_t candidate_index = this->FindDentryEvictionCandidate();
        if (candidate_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
            break;
        }
        if (!this->ReleaseDentrySlot(candidate_index, false, true)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        ++evicted_entry_count;
    }
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::EvictInodes(const uint64_t maximum_entry_count,
                                                       uint64_t &evicted_entry_count) noexcept {
    SpinLockGuard guard{this->lock_};
    evicted_entry_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    if (!this->initialized_ || this->inodes_ == nullptr) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    while (evicted_entry_count < maximum_entry_count) {
        const uint64_t candidate_index = this->FindInodeEvictionCandidate();
        if (candidate_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
            break;
        }
        if (!this->ReleaseInodeSlot(candidate_index, false, true)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        ++evicted_entry_count;
    }
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::ReadDentry(const VfsDentryToken token,
                                                      VfsDentrySnapshot &snapshot) const noexcept {
    SpinLockGuard guard{this->lock_};
    snapshot = VfsDentrySnapshot{};
    if (!this->DentryTokenIsValid(token)) {
        return VfsNamespaceCacheStatus::InvalidToken;
    }
    snapshot = this->SnapshotDentry(token.slot_index);
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatus VfsNamespaceCache::ReadInode(const VfsInodeToken token,
                                                     VfsInodeSnapshot &snapshot) const noexcept {
    SpinLockGuard guard{this->lock_};
    snapshot = VfsInodeSnapshot{};
    if (!this->InodeTokenIsValid(token)) {
        return VfsNamespaceCacheStatus::InvalidToken;
    }
    snapshot = this->SnapshotInode(token.slot_index);
    return VfsNamespaceCacheStatus::Succeeded;
}

VfsNamespaceCacheStatistics VfsNamespaceCache::Statistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->initialized_ ? this->statistics_ : VfsNamespaceCacheStatistics{};
}

VfsNamespaceCacheStatus VfsNamespaceCache::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || this->dentries_ == nullptr || this->inodes_ == nullptr ||
        this->dentry_capacity_ == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        this->inode_capacity_ == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return VfsNamespaceCacheStatus::NotInitialized;
    }
    const bool any_hash_storage =
        this->dentry_hash_entries_ != nullptr || this->dentry_hash_buckets_ != nullptr ||
        this->dentry_hash_bucket_capacity_ != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        this->inode_hash_entries_ != nullptr || this->inode_hash_buckets_ != nullptr ||
        this->inode_hash_bucket_capacity_ != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    if (any_hash_storage && !this->HashIndexIsConfigured()) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    uint64_t active_dentry_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t cached_positive_dentry_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t cached_negative_dentry_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t stale_dentry_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t referenced_dentry_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t active_dentry_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    for (uint64_t dentry_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         dentry_index < this->dentry_capacity_; ++dentry_index) {
        const VfsDentrySlot &dentry = this->dentries_[dentry_index];
        if (dentry.state == VfsNamespaceEntryState::Free) {
            if (VfsDentryKeyIsValid(dentry.key) ||
                dentry.inode_token.slot_index != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                dentry.inode_token.generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                dentry.access_generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                dentry.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                dentry.kind != VfsDentryKind::None ||
                (this->HashIndexIsConfigured() &&
                 (this->dentry_hash_entries_[dentry_index].indexed ||
                  this->dentry_hash_entries_[dentry_index].hash_value !=
                      OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                  this->dentry_hash_entries_[dentry_index].next_slot_index !=
                      OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX))) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
            continue;
        }
        if ((dentry.state != VfsNamespaceEntryState::Cached &&
             dentry.state != VfsNamespaceEntryState::Stale) ||
            !VfsDentryKeyIsValid(dentry.key) ||
            dentry.access_generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
            dentry.generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
            (dentry.kind != VfsDentryKind::Positive && dentry.kind != VfsDentryKind::Negative) ||
            (dentry.kind == VfsDentryKind::Positive &&
             !this->InodeTokenIsValid(dentry.inode_token)) ||
            (dentry.kind == VfsDentryKind::Negative &&
             (dentry.inode_token.slot_index != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
              dentry.inode_token.generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE)) ||
            !CounterCanIncrease(active_dentry_reference_count, dentry.external_reference_count)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        if (this->HashIndexIsConfigured()) {
            const VfsNamespaceHashEntry &hash_entry = this->dentry_hash_entries_[dentry_index];
            const bool should_be_indexed = dentry.state == VfsNamespaceEntryState::Cached;
            if (hash_entry.indexed != should_be_indexed ||
                (should_be_indexed && (hash_entry.hash_value != HashDentryKey(dentry.key) ||
                                       this->FindCachedDentry(dentry.key) != dentry_index)) ||
                (!should_be_indexed &&
                 (hash_entry.hash_value != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                  hash_entry.next_slot_index !=
                      OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX))) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
        }
        ++active_dentry_count;
        active_dentry_reference_count += dentry.external_reference_count;
        if (dentry.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            ++referenced_dentry_count;
        }
        if (dentry.state == VfsNamespaceEntryState::Stale) {
            if (dentry.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
            ++stale_dentry_count;
        } else if (dentry.kind == VfsDentryKind::Positive) {
            const VfsInodeSlot &inode = this->inodes_[dentry.inode_token.slot_index];
            if (inode.state != VfsNamespaceEntryState::Cached) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
            ++cached_positive_dentry_count;
        } else {
            ++cached_negative_dentry_count;
        }
        if (dentry.state == VfsNamespaceEntryState::Cached) {
            for (uint64_t comparison_index =
                     dentry_index + OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT;
                 comparison_index < this->dentry_capacity_; ++comparison_index) {
                const VfsDentrySlot &comparison = this->dentries_[comparison_index];
                if (comparison.state == VfsNamespaceEntryState::Cached &&
                    VfsDentryKeysEqual(dentry.key, comparison.key)) {
                    return VfsNamespaceCacheStatus::Corrupt;
                }
            }
        }
    }

    uint64_t active_inode_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t cached_inode_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t stale_inode_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t referenced_inode_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t inode_dentry_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t active_inode_external_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t loading_inode_metadata_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t ready_inode_metadata_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    for (uint64_t inode_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         inode_index < this->inode_capacity_; ++inode_index) {
        const VfsInodeSlot &inode = this->inodes_[inode_index];
        if (inode.state == VfsNamespaceEntryState::Free) {
            if (VfsInodeIdentityIsValid(inode.identity) ||
                inode.access_generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                inode.dentry_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                inode.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                inode.metadata_generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                inode.type != NodeType::None ||
                inode.metadata_state != VfsInodeMetadataState::Empty ||
                !BackendNodeInformationIsEmpty(inode.metadata) ||
                (this->HashIndexIsConfigured() &&
                 (this->inode_hash_entries_[inode_index].indexed ||
                  this->inode_hash_entries_[inode_index].hash_value !=
                      OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                  this->inode_hash_entries_[inode_index].next_slot_index !=
                      OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX))) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
            continue;
        }
        if ((inode.state != VfsNamespaceEntryState::Cached &&
             inode.state != VfsNamespaceEntryState::Stale) ||
            !VfsInodeIdentityIsValid(inode.identity) || !NodeTypeIsCacheable(inode.type) ||
            inode.access_generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
            inode.generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
            (inode.metadata_state != VfsInodeMetadataState::Empty &&
             inode.metadata_state != VfsInodeMetadataState::Loading &&
             inode.metadata_state != VfsInodeMetadataState::Ready) ||
            (inode.metadata_state != VfsInodeMetadataState::Ready &&
             !BackendNodeInformationIsEmpty(inode.metadata)) ||
            (inode.metadata_state == VfsInodeMetadataState::Ready &&
             !VfsInodeMetadataIsValid(inode.metadata, inode.type)) ||
            (inode.metadata_state != VfsInodeMetadataState::Empty &&
             inode.metadata_generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) ||
            (inode.state == VfsNamespaceEntryState::Stale &&
             inode.metadata_state != VfsInodeMetadataState::Empty) ||
            !CounterCanIncrease(inode_dentry_reference_count, inode.dentry_reference_count) ||
            !CounterCanIncrease(active_inode_external_reference_count,
                                inode.external_reference_count)) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        if (this->HashIndexIsConfigured()) {
            const VfsNamespaceHashEntry &hash_entry = this->inode_hash_entries_[inode_index];
            const bool should_be_indexed = inode.state == VfsNamespaceEntryState::Cached;
            if (hash_entry.indexed != should_be_indexed ||
                (should_be_indexed && (hash_entry.hash_value != HashInodeIdentity(inode.identity) ||
                                       this->FindCachedInode(inode.identity) != inode_index)) ||
                (!should_be_indexed &&
                 (hash_entry.hash_value != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
                  hash_entry.next_slot_index !=
                      OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX))) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
        }
        uint64_t observed_dentry_reference_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
        const VfsInodeToken token{
            .slot_index = inode_index,
            .generation = inode.generation,
        };
        for (uint64_t dentry_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
             dentry_index < this->dentry_capacity_; ++dentry_index) {
            const VfsDentrySlot &dentry = this->dentries_[dentry_index];
            if (dentry.state != VfsNamespaceEntryState::Free &&
                dentry.kind == VfsDentryKind::Positive &&
                dentry.inode_token.slot_index == token.slot_index &&
                dentry.inode_token.generation == token.generation) {
                ++observed_dentry_reference_count;
            }
            if (inode.state == VfsNamespaceEntryState::Stale &&
                dentry.state == VfsNamespaceEntryState::Cached &&
                (VfsInodeIdentitiesEqual(dentry.key.parent, inode.identity) ||
                 (dentry.kind == VfsDentryKind::Positive &&
                  dentry.inode_token.slot_index == token.slot_index &&
                  dentry.inode_token.generation == token.generation))) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
        }
        if (observed_dentry_reference_count != inode.dentry_reference_count) {
            return VfsNamespaceCacheStatus::Corrupt;
        }
        ++active_inode_count;
        inode_dentry_reference_count += inode.dentry_reference_count;
        active_inode_external_reference_count += inode.external_reference_count;
        if (inode.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            ++referenced_inode_count;
        }
        if (inode.metadata_state == VfsInodeMetadataState::Loading) {
            ++loading_inode_metadata_count;
        } else if (inode.metadata_state == VfsInodeMetadataState::Ready) {
            ++ready_inode_metadata_count;
        }
        if (inode.state == VfsNamespaceEntryState::Cached) {
            ++cached_inode_count;
            for (uint64_t comparison_index =
                     inode_index + OS_KERNEL_VFS_NAMESPACE_CACHE_COUNTER_INCREMENT;
                 comparison_index < this->inode_capacity_; ++comparison_index) {
                const VfsInodeSlot &comparison = this->inodes_[comparison_index];
                if (comparison.state == VfsNamespaceEntryState::Cached &&
                    VfsInodeIdentitiesEqual(inode.identity, comparison.identity)) {
                    return VfsNamespaceCacheStatus::Corrupt;
                }
            }
        } else {
            if (inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
                inode.dentry_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
            ++stale_inode_count;
        }
    }
    if (this->HashIndexIsConfigured()) {
        for (uint64_t bucket_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
             bucket_index < this->dentry_hash_bucket_capacity_; ++bucket_index) {
            uint64_t slot_index = this->dentry_hash_buckets_[bucket_index];
            uint64_t traversal_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
            while (slot_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX &&
                   traversal_count < this->dentry_capacity_) {
                if (slot_index >= this->dentry_capacity_ ||
                    !this->dentry_hash_entries_[slot_index].indexed ||
                    this->dentry_hash_entries_[slot_index].hash_value %
                            this->dentry_hash_bucket_capacity_ !=
                        bucket_index) {
                    return VfsNamespaceCacheStatus::Corrupt;
                }
                slot_index = this->dentry_hash_entries_[slot_index].next_slot_index;
                ++traversal_count;
            }
            if (slot_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
        }
        for (uint64_t bucket_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
             bucket_index < this->inode_hash_bucket_capacity_; ++bucket_index) {
            uint64_t slot_index = this->inode_hash_buckets_[bucket_index];
            uint64_t traversal_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
            while (slot_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX &&
                   traversal_count < this->inode_capacity_) {
                if (slot_index >= this->inode_capacity_ ||
                    !this->inode_hash_entries_[slot_index].indexed ||
                    this->inode_hash_entries_[slot_index].hash_value %
                            this->inode_hash_bucket_capacity_ !=
                        bucket_index) {
                    return VfsNamespaceCacheStatus::Corrupt;
                }
                slot_index = this->inode_hash_entries_[slot_index].next_slot_index;
                ++traversal_count;
            }
            if (slot_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
                return VfsNamespaceCacheStatus::Corrupt;
            }
        }
    }
    return this->statistics_.dentry_hash_bucket_capacity ==
                       (this->HashIndexIsConfigured()
                            ? this->dentry_hash_bucket_capacity_
                            : OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) &&
                   this->statistics_.inode_hash_bucket_capacity ==
                       (this->HashIndexIsConfigured()
                            ? this->inode_hash_bucket_capacity_
                            : OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) &&
                   active_dentry_count == this->statistics_.active_dentry_count &&
                   cached_positive_dentry_count == this->statistics_.cached_positive_dentry_count &&
                   cached_negative_dentry_count == this->statistics_.cached_negative_dentry_count &&
                   stale_dentry_count == this->statistics_.stale_dentry_count &&
                   referenced_dentry_count == this->statistics_.referenced_dentry_count &&
                   active_dentry_reference_count ==
                       this->statistics_.active_dentry_reference_count &&
                   active_inode_count == this->statistics_.active_inode_count &&
                   cached_inode_count == this->statistics_.cached_inode_count &&
                   stale_inode_count == this->statistics_.stale_inode_count &&
                   referenced_inode_count == this->statistics_.referenced_inode_count &&
                   inode_dentry_reference_count == this->statistics_.inode_dentry_reference_count &&
                   active_inode_external_reference_count ==
                       this->statistics_.active_inode_external_reference_count &&
                   loading_inode_metadata_count == this->statistics_.loading_inode_metadata_count &&
                   ready_inode_metadata_count == this->statistics_.ready_inode_metadata_count &&
                   active_dentry_count <= this->dentry_capacity_ &&
                   active_inode_count <= this->inode_capacity_ &&
                   CounterCanIncrease(loading_inode_metadata_count, ready_inode_metadata_count) &&
                   loading_inode_metadata_count + ready_inode_metadata_count <=
                       cached_inode_count &&
                   this->statistics_.peak_active_dentry_count >= active_dentry_count &&
                   this->statistics_.peak_active_dentry_reference_count >=
                       active_dentry_reference_count &&
                   this->statistics_.peak_active_inode_count >= active_inode_count
               ? VfsNamespaceCacheStatus::Succeeded
               : VfsNamespaceCacheStatus::Corrupt;
}

VfsNamespaceCacheStatus VfsNamespaceCache::Destroy() noexcept {
    uint64_t evicted_dentry_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    uint64_t evicted_inode_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    if (this->EvictDentries(UINT64_MAX, evicted_dentry_count) !=
            VfsNamespaceCacheStatus::Succeeded ||
        this->EvictInodes(UINT64_MAX, evicted_inode_count) != VfsNamespaceCacheStatus::Succeeded) {
        return VfsNamespaceCacheStatus::Corrupt;
    }
    SpinLockGuard guard{this->lock_};
    if (this->statistics_.active_dentry_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        this->statistics_.active_inode_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return VfsNamespaceCacheStatus::EntriesRemain;
    }
    this->dentries_ = nullptr;
    this->dentry_capacity_ = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    this->inodes_ = nullptr;
    this->inode_capacity_ = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    this->dentry_hash_entries_ = nullptr;
    this->dentry_hash_buckets_ = nullptr;
    this->dentry_hash_bucket_capacity_ = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    this->inode_hash_entries_ = nullptr;
    this->inode_hash_buckets_ = nullptr;
    this->inode_hash_bucket_capacity_ = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    this->access_generation_ = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    this->statistics_ = VfsNamespaceCacheStatistics{};
    this->initialized_ = false;
    return VfsNamespaceCacheStatus::Succeeded;
}

uint64_t VfsNamespaceCache::FindCachedDentry(const VfsDentryKey &key) const noexcept {
    if (this->HashIndexIsConfigured()) {
        const uint64_t hash_value = HashDentryKey(key);
        uint64_t slot_index =
            this->dentry_hash_buckets_[hash_value % this->dentry_hash_bucket_capacity_];
        for (uint64_t traversal_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
             traversal_count < this->dentry_capacity_ &&
             slot_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
             ++traversal_count) {
            if (slot_index >= this->dentry_capacity_) {
                return OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
            }
            const VfsNamespaceHashEntry &entry = this->dentry_hash_entries_[slot_index];
            const VfsDentrySlot &dentry = this->dentries_[slot_index];
            if (entry.indexed && entry.hash_value == hash_value &&
                dentry.state == VfsNamespaceEntryState::Cached &&
                VfsDentryKeysEqual(dentry.key, key)) {
                return slot_index;
            }
            slot_index = entry.next_slot_index;
        }
        return OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->dentry_capacity_; ++slot_index) {
        const VfsDentrySlot &dentry = this->dentries_[slot_index];
        if (dentry.state == VfsNamespaceEntryState::Cached && VfsDentryKeysEqual(dentry.key, key)) {
            return slot_index;
        }
    }
    return OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
}

uint64_t VfsNamespaceCache::FindCachedInode(const VfsInodeIdentity &identity) const noexcept {
    if (this->HashIndexIsConfigured()) {
        const uint64_t hash_value = HashInodeIdentity(identity);
        uint64_t slot_index =
            this->inode_hash_buckets_[hash_value % this->inode_hash_bucket_capacity_];
        for (uint64_t traversal_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
             traversal_count < this->inode_capacity_ &&
             slot_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
             ++traversal_count) {
            if (slot_index >= this->inode_capacity_) {
                return OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
            }
            const VfsNamespaceHashEntry &entry = this->inode_hash_entries_[slot_index];
            const VfsInodeSlot &inode = this->inodes_[slot_index];
            if (entry.indexed && entry.hash_value == hash_value &&
                inode.state == VfsNamespaceEntryState::Cached &&
                VfsInodeIdentitiesEqual(inode.identity, identity)) {
                return slot_index;
            }
            slot_index = entry.next_slot_index;
        }
        return OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    }
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->inode_capacity_; ++slot_index) {
        const VfsInodeSlot &inode = this->inodes_[slot_index];
        if (inode.state == VfsNamespaceEntryState::Cached &&
            VfsInodeIdentitiesEqual(inode.identity, identity)) {
            return slot_index;
        }
    }
    return OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
}

uint64_t VfsNamespaceCache::FindFreeDentry() const noexcept {
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->dentry_capacity_; ++slot_index) {
        if (this->dentries_[slot_index].state == VfsNamespaceEntryState::Free) {
            return slot_index;
        }
    }
    return OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
}

uint64_t VfsNamespaceCache::FindFreeInode() const noexcept {
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->inode_capacity_; ++slot_index) {
        if (this->inodes_[slot_index].state == VfsNamespaceEntryState::Free) {
            return slot_index;
        }
    }
    return OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
}

uint64_t VfsNamespaceCache::FindDentryEvictionCandidate() const noexcept {
    uint64_t candidate_index = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->dentry_capacity_; ++slot_index) {
        const VfsDentrySlot &dentry = this->dentries_[slot_index];
        if (dentry.state != VfsNamespaceEntryState::Cached ||
            dentry.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            continue;
        }
        if (candidate_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX ||
            dentry.access_generation < this->dentries_[candidate_index].access_generation) {
            candidate_index = slot_index;
        }
    }
    return candidate_index;
}

uint64_t VfsNamespaceCache::FindInodeEvictionCandidate() const noexcept {
    uint64_t candidate_index = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    for (uint64_t slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         slot_index < this->inode_capacity_; ++slot_index) {
        const VfsInodeSlot &inode = this->inodes_[slot_index];
        if (inode.state != VfsNamespaceEntryState::Cached ||
            inode.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
            inode.dentry_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
            inode.metadata_state == VfsInodeMetadataState::Loading) {
            continue;
        }
        if (candidate_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX ||
            inode.access_generation < this->inodes_[candidate_index].access_generation) {
            candidate_index = slot_index;
        }
    }
    return candidate_index;
}

bool VfsNamespaceCache::InsertDentryIndex(const uint64_t slot_index) noexcept {
    if (!this->HashIndexIsConfigured()) {
        return true;
    }
    if (slot_index >= this->dentry_capacity_ ||
        this->dentries_[slot_index].state != VfsNamespaceEntryState::Cached ||
        this->dentry_hash_entries_[slot_index].indexed) {
        return false;
    }
    const uint64_t hash_value = HashDentryKey(this->dentries_[slot_index].key);
    const uint64_t bucket_index = hash_value % this->dentry_hash_bucket_capacity_;
    // bucket 只保存 Cached 项；Stale 旧 token 继续驻槽但不再参与新 lookup。
    this->dentry_hash_entries_[slot_index] = VfsNamespaceHashEntry{
        .hash_value = hash_value,
        .next_slot_index = this->dentry_hash_buckets_[bucket_index],
        .indexed = true,
    };
    this->dentry_hash_buckets_[bucket_index] = slot_index;
    return true;
}

bool VfsNamespaceCache::InsertInodeIndex(const uint64_t slot_index) noexcept {
    if (!this->HashIndexIsConfigured()) {
        return true;
    }
    if (slot_index >= this->inode_capacity_ ||
        this->inodes_[slot_index].state != VfsNamespaceEntryState::Cached ||
        this->inode_hash_entries_[slot_index].indexed) {
        return false;
    }
    const uint64_t hash_value = HashInodeIdentity(this->inodes_[slot_index].identity);
    const uint64_t bucket_index = hash_value % this->inode_hash_bucket_capacity_;
    this->inode_hash_entries_[slot_index] = VfsNamespaceHashEntry{
        .hash_value = hash_value,
        .next_slot_index = this->inode_hash_buckets_[bucket_index],
        .indexed = true,
    };
    this->inode_hash_buckets_[bucket_index] = slot_index;
    return true;
}

bool VfsNamespaceCache::RemoveDentryIndex(const uint64_t slot_index) noexcept {
    if (!this->HashIndexIsConfigured()) {
        return true;
    }
    if (slot_index >= this->dentry_capacity_ || !this->dentry_hash_entries_[slot_index].indexed) {
        return false;
    }
    const uint64_t bucket_index =
        this->dentry_hash_entries_[slot_index].hash_value % this->dentry_hash_bucket_capacity_;
    uint64_t current_index = this->dentry_hash_buckets_[bucket_index];
    uint64_t previous_index = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    for (uint64_t traversal_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         traversal_count < this->dentry_capacity_ &&
         current_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
         ++traversal_count) {
        if (current_index >= this->dentry_capacity_) {
            return false;
        }
        if (current_index == slot_index) {
            const uint64_t next_index = this->dentry_hash_entries_[current_index].next_slot_index;
            if (previous_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
                this->dentry_hash_buckets_[bucket_index] = next_index;
            } else {
                this->dentry_hash_entries_[previous_index].next_slot_index = next_index;
            }
            this->dentry_hash_entries_[slot_index] = VfsNamespaceHashEntry{
                .hash_value = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
                .next_slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX,
                .indexed = false,
            };
            return true;
        }
        previous_index = current_index;
        current_index = this->dentry_hash_entries_[current_index].next_slot_index;
    }
    return false;
}

bool VfsNamespaceCache::RemoveInodeIndex(const uint64_t slot_index) noexcept {
    if (!this->HashIndexIsConfigured()) {
        return true;
    }
    if (slot_index >= this->inode_capacity_ || !this->inode_hash_entries_[slot_index].indexed) {
        return false;
    }
    const uint64_t bucket_index =
        this->inode_hash_entries_[slot_index].hash_value % this->inode_hash_bucket_capacity_;
    uint64_t current_index = this->inode_hash_buckets_[bucket_index];
    uint64_t previous_index = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
    for (uint64_t traversal_count = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         traversal_count < this->inode_capacity_ &&
         current_index != OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX;
         ++traversal_count) {
        if (current_index >= this->inode_capacity_) {
            return false;
        }
        if (current_index == slot_index) {
            const uint64_t next_index = this->inode_hash_entries_[current_index].next_slot_index;
            if (previous_index == OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX) {
                this->inode_hash_buckets_[bucket_index] = next_index;
            } else {
                this->inode_hash_entries_[previous_index].next_slot_index = next_index;
            }
            this->inode_hash_entries_[slot_index] = VfsNamespaceHashEntry{
                .hash_value = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE,
                .next_slot_index = OS_KERNEL_VFS_NAMESPACE_CACHE_INVALID_SLOT_INDEX,
                .indexed = false,
            };
            return true;
        }
        previous_index = current_index;
        current_index = this->inode_hash_entries_[current_index].next_slot_index;
    }
    return false;
}

bool VfsNamespaceCache::HashIndexIsConfigured() const noexcept {
    return this->dentry_hash_entries_ != nullptr && this->dentry_hash_buckets_ != nullptr &&
           this->dentry_hash_bucket_capacity_ != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           this->inode_hash_entries_ != nullptr && this->inode_hash_buckets_ != nullptr &&
           this->inode_hash_bucket_capacity_ != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
}

bool VfsNamespaceCache::DentryTokenIsValid(const VfsDentryToken token) const noexcept {
    return this->initialized_ && token.slot_index < this->dentry_capacity_ &&
           token.generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           this->dentries_[token.slot_index].state != VfsNamespaceEntryState::Free &&
           this->dentries_[token.slot_index].generation == token.generation;
}

bool VfsNamespaceCache::InodeTokenIsValid(const VfsInodeToken token) const noexcept {
    return this->initialized_ && token.slot_index < this->inode_capacity_ &&
           token.generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           this->inodes_[token.slot_index].state != VfsNamespaceEntryState::Free &&
           this->inodes_[token.slot_index].generation == token.generation;
}

bool VfsNamespaceCache::InodeMetadataTokenIsValid(
    const VfsInodeMetadataToken token) const noexcept {
    return this->InodeTokenIsValid(token.inode_token) &&
           this->inodes_[token.inode_token.slot_index].state == VfsNamespaceEntryState::Cached &&
           this->inodes_[token.inode_token.slot_index].metadata_state ==
               VfsInodeMetadataState::Loading &&
           token.metadata_generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           this->inodes_[token.inode_token.slot_index].metadata_generation ==
               token.metadata_generation;
}

bool VfsNamespaceCache::TouchDentry(VfsDentrySlot &dentry) noexcept {
    const uint64_t generation = this->NextAccessGeneration();
    if (generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return false;
    }
    dentry.access_generation = generation;
    return true;
}

bool VfsNamespaceCache::TouchInode(VfsInodeSlot &inode) noexcept {
    const uint64_t generation = this->NextAccessGeneration();
    if (generation == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
        return false;
    }
    inode.access_generation = generation;
    return true;
}

bool VfsNamespaceCache::MarkDentryStale(const uint64_t slot_index, const bool cascaded) noexcept {
    if (slot_index >= this->dentry_capacity_) {
        return false;
    }
    VfsDentrySlot &dentry = this->dentries_[slot_index];
    if (dentry.state != VfsNamespaceEntryState::Cached ||
        !CounterCanIncrease(this->statistics_.stale_dentry_count) ||
        (cascaded && !CounterCanIncrease(this->statistics_.cascaded_dentry_invalidation_count))) {
        return false;
    }
    if (dentry.kind == VfsDentryKind::Positive) {
        if (this->statistics_.cached_positive_dentry_count ==
            OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return false;
        }
        --this->statistics_.cached_positive_dentry_count;
    } else if (dentry.kind == VfsDentryKind::Negative) {
        if (this->statistics_.cached_negative_dentry_count ==
            OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return false;
        }
        --this->statistics_.cached_negative_dentry_count;
    } else {
        return false;
    }
    if (!this->RemoveDentryIndex(slot_index)) {
        return false;
    }
    dentry.state = VfsNamespaceEntryState::Stale;
    ++this->statistics_.stale_dentry_count;
    if (cascaded) {
        ++this->statistics_.cascaded_dentry_invalidation_count;
    }
    return dentry.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
           this->ReleaseDentrySlot(slot_index, true, false);
}

bool VfsNamespaceCache::ReleaseDentrySlot(const uint64_t slot_index, const bool stale_release,
                                          const bool eviction) noexcept {
    if (slot_index >= this->dentry_capacity_) {
        return false;
    }
    VfsDentrySlot &dentry = this->dentries_[slot_index];
    if (dentry.state == VfsNamespaceEntryState::Free ||
        dentry.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        this->statistics_.active_dentry_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        (stale_release && !CounterCanIncrease(this->statistics_.stale_dentry_release_count)) ||
        (eviction && !CounterCanIncrease(this->statistics_.dentry_eviction_count))) {
        return false;
    }
    if (dentry.kind == VfsDentryKind::Positive) {
        if (!this->InodeTokenIsValid(dentry.inode_token)) {
            return false;
        }
        VfsInodeSlot &inode = this->inodes_[dentry.inode_token.slot_index];
        if (inode.dentry_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
            this->statistics_.inode_dentry_reference_count ==
                OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return false;
        }
        --inode.dentry_reference_count;
        --this->statistics_.inode_dentry_reference_count;
        if (inode.state == VfsNamespaceEntryState::Stale &&
            inode.dentry_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
            inode.external_reference_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
            !this->ReleaseInodeSlot(dentry.inode_token.slot_index, true, false)) {
            return false;
        }
    }
    if (dentry.state == VfsNamespaceEntryState::Cached) {
        uint64_t &cached_count = dentry.kind == VfsDentryKind::Positive
                                     ? this->statistics_.cached_positive_dentry_count
                                     : this->statistics_.cached_negative_dentry_count;
        if (cached_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return false;
        }
        if (!this->RemoveDentryIndex(slot_index)) {
            return false;
        }
        --cached_count;
    } else if (dentry.state == VfsNamespaceEntryState::Stale) {
        if (this->statistics_.stale_dentry_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return false;
        }
        --this->statistics_.stale_dentry_count;
    } else {
        return false;
    }
    const uint64_t generation = dentry.generation;
    dentry = VfsDentrySlot{};
    dentry.generation = generation;
    --this->statistics_.active_dentry_count;
    if (stale_release) {
        ++this->statistics_.stale_dentry_release_count;
    }
    if (eviction) {
        ++this->statistics_.dentry_eviction_count;
    }
    return true;
}

bool VfsNamespaceCache::ReleaseInodeSlot(const uint64_t slot_index, const bool stale_release,
                                         const bool eviction) noexcept {
    if (slot_index >= this->inode_capacity_) {
        return false;
    }
    VfsInodeSlot &inode = this->inodes_[slot_index];
    if (inode.state == VfsNamespaceEntryState::Free ||
        inode.dentry_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        inode.external_reference_count != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        inode.metadata_state == VfsInodeMetadataState::Loading ||
        this->statistics_.active_inode_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        (stale_release && !CounterCanIncrease(this->statistics_.stale_inode_release_count)) ||
        (eviction && !CounterCanIncrease(this->statistics_.inode_eviction_count))) {
        return false;
    }
    if ((inode.metadata_state == VfsInodeMetadataState::Ready &&
         this->statistics_.ready_inode_metadata_count ==
             OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) ||
        (inode.metadata_state != VfsInodeMetadataState::Empty &&
         inode.metadata_state != VfsInodeMetadataState::Ready)) {
        return false;
    }
    if (inode.state == VfsNamespaceEntryState::Cached) {
        if (this->statistics_.cached_inode_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return false;
        }
        if (!this->RemoveInodeIndex(slot_index)) {
            return false;
        }
        --this->statistics_.cached_inode_count;
    } else if (inode.state == VfsNamespaceEntryState::Stale) {
        if (this->statistics_.stale_inode_count == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE) {
            return false;
        }
        --this->statistics_.stale_inode_count;
    } else {
        return false;
    }
    if (inode.metadata_state == VfsInodeMetadataState::Ready) {
        --this->statistics_.ready_inode_metadata_count;
    }
    const uint64_t generation = inode.generation;
    inode = VfsInodeSlot{};
    inode.generation = generation;
    --this->statistics_.active_inode_count;
    if (stale_release) {
        ++this->statistics_.stale_inode_release_count;
    }
    if (eviction) {
        ++this->statistics_.inode_eviction_count;
    }
    return true;
}

VfsDentrySnapshot VfsNamespaceCache::SnapshotDentry(const uint64_t slot_index) const noexcept {
    const VfsDentrySlot &dentry = this->dentries_[slot_index];
    VfsDentrySnapshot snapshot{
        .token =
            VfsDentryToken{
                .slot_index = slot_index,
                .generation = dentry.generation,
            },
        .key = dentry.key,
        .inode_token = dentry.inode_token,
        .inode_identity = VfsInodeIdentity{},
        .access_generation = dentry.access_generation,
        .external_reference_count = dentry.external_reference_count,
        .inode_type = NodeType::None,
        .kind = dentry.kind,
        .state = dentry.state,
    };
    if (dentry.kind == VfsDentryKind::Positive && this->InodeTokenIsValid(dentry.inode_token)) {
        const VfsInodeSlot &inode = this->inodes_[dentry.inode_token.slot_index];
        snapshot.inode_identity = inode.identity;
        snapshot.inode_type = inode.type;
    }
    return snapshot;
}

VfsInodeSnapshot VfsNamespaceCache::SnapshotInode(const uint64_t slot_index) const noexcept {
    const VfsInodeSlot &inode = this->inodes_[slot_index];
    return VfsInodeSnapshot{
        .token =
            VfsInodeToken{
                .slot_index = slot_index,
                .generation = inode.generation,
            },
        .identity = inode.identity,
        .access_generation = inode.access_generation,
        .dentry_reference_count = inode.dentry_reference_count,
        .external_reference_count = inode.external_reference_count,
        .type = inode.type,
        .state = inode.state,
    };
}

VfsInodeMetadataSnapshot
VfsNamespaceCache::SnapshotInodeMetadata(const uint64_t slot_index) const noexcept {
    const VfsInodeSlot &inode = this->inodes_[slot_index];
    return VfsInodeMetadataSnapshot{
        .token =
            VfsInodeMetadataToken{
                .inode_token =
                    VfsInodeToken{
                        .slot_index = slot_index,
                        .generation = inode.generation,
                    },
                .metadata_generation = inode.metadata_generation,
            },
        .identity = inode.identity,
        .metadata = inode.metadata,
        .access_generation = inode.access_generation,
        .type = inode.type,
        .state = inode.metadata_state,
    };
}

uint64_t VfsNamespaceCache::NextAccessGeneration() noexcept {
    if (this->access_generation_ == UINT64_MAX) {
        return OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
    }
    ++this->access_generation_;
    return this->access_generation_;
}

bool VfsInodeIdentityIsValid(const VfsInodeIdentity &identity) noexcept {
    return identity.superblock_identifier != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           identity.superblock_generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           identity.node_identifier != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           identity.node_generation != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
}

bool VfsInodeIdentitiesEqual(const VfsInodeIdentity &left, const VfsInodeIdentity &right) noexcept {
    return left.superblock_identifier == right.superblock_identifier &&
           left.superblock_generation == right.superblock_generation &&
           left.node_identifier == right.node_identifier &&
           left.node_generation == right.node_generation;
}

bool VfsInodeMetadataIsValid(const BackendNodeInformation &metadata, const NodeType type) noexcept {
    const os::abi::FileMode expected_mode = ModeTypeForNode(type);
    return expected_mode != OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE &&
           security::ModeTypeMatches(metadata.mode, expected_mode);
}

uint64_t VfsInodeIdentityHash(const VfsInodeIdentity &identity) noexcept {
    return VfsInodeIdentityIsValid(identity) ? HashInodeIdentity(identity)
                                             : OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
}

bool VfsDentryKeyIsValid(const VfsDentryKey &key) noexcept {
    if (key.mount_identifier == OS_KERNEL_VFS_INVALID_MOUNT_IDENTIFIER ||
        !VfsInodeIdentityIsValid(key.parent) ||
        key.name_length_bytes == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        key.name_length_bytes > OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         byte_index < key.name_length_bytes; ++byte_index) {
        if (!NameByteIsValid(key.name[byte_index])) {
            return false;
        }
    }
    for (uint64_t byte_index = key.name_length_bytes;
         byte_index < OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES; ++byte_index) {
        if (key.name[byte_index] !=
            static_cast<uint8_t>(OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE)) {
            return false;
        }
    }
    return !IsDot(key) && !IsDotDot(key);
}

bool VfsDentryKeysEqual(const VfsDentryKey &left, const VfsDentryKey &right) noexcept {
    if (left.mount_identifier != right.mount_identifier ||
        !VfsInodeIdentitiesEqual(left.parent, right.parent) ||
        left.name_length_bytes != right.name_length_bytes) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         byte_index < left.name_length_bytes; ++byte_index) {
        if (left.name[byte_index] != right.name[byte_index]) {
            return false;
        }
    }
    return true;
}

uint64_t VfsDentryKeyHash(const VfsDentryKey &key) noexcept {
    return VfsDentryKeyIsValid(key) ? HashDentryKey(key)
                                    : OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
}

VfsNamespaceCacheStatus BuildVfsDentryKey(const uint64_t mount_identifier,
                                          const VfsInodeIdentity &parent, const uint8_t *const name,
                                          const uint64_t name_length_bytes,
                                          VfsDentryKey &key) noexcept {
    key = VfsDentryKey{};
    if (name == nullptr || mount_identifier == OS_KERNEL_VFS_INVALID_MOUNT_IDENTIFIER ||
        !VfsInodeIdentityIsValid(parent) ||
        name_length_bytes == OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE ||
        name_length_bytes > OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES) {
        return VfsNamespaceCacheStatus::InvalidKey;
    }
    key.mount_identifier = mount_identifier;
    key.parent = parent;
    key.name_length_bytes = name_length_bytes;
    for (uint64_t byte_index = OS_KERNEL_VFS_NAMESPACE_CACHE_EMPTY_VALUE;
         byte_index < name_length_bytes; ++byte_index) {
        key.name[byte_index] = name[byte_index];
    }
    if (!VfsDentryKeyIsValid(key)) {
        key = VfsDentryKey{};
        return VfsNamespaceCacheStatus::InvalidKey;
    }
    return VfsNamespaceCacheStatus::Succeeded;
}

}
