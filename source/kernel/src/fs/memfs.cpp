#include "os/kernel/fs/memfs.hpp"

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_MEMFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER = 1ULL;
constexpr uint64_t OS_KERNEL_MEMFS_INITIAL_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_MEMFS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_MEMFS_ALLOCATION_ALIGNMENT_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_MEMFS_INITIAL_FILE_CAPACITY_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_MEMFS_CAPACITY_GROWTH_FACTOR = 2ULL;
constexpr uint8_t OS_KERNEL_MEMFS_PATH_SEPARATOR = static_cast<uint8_t>('/');
constexpr uint8_t OS_KERNEL_MEMFS_DOT_CHARACTER = static_cast<uint8_t>('.');
constexpr uint8_t OS_KERNEL_MEMFS_MAXIMUM_CONTROL_CHARACTER = 0x1FU;
constexpr uint8_t OS_KERNEL_MEMFS_DELETE_CONTROL_CHARACTER = 0x7FU;
constexpr uint8_t OS_KERNEL_MEMFS_ZERO_BYTE = 0U;

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_MEMFS_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

void ClearBytes(uint8_t *const destination, const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_MEMFS_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = OS_KERNEL_MEMFS_ZERO_BYTE;
    }
}

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left, const uint8_t *const right,
                                 const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_MEMFS_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint64_t Minimum(const uint64_t left, const uint64_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] bool NameIsValid(const uint8_t *const name,
                               const uint64_t name_length_bytes) noexcept {
    if (name == nullptr || name_length_bytes == OS_KERNEL_MEMFS_EMPTY_VALUE ||
        name_length_bytes > OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_MEMFS_EMPTY_VALUE; byte_index < name_length_bytes;
         ++byte_index) {
        const uint8_t value = name[byte_index];
        if (value <= OS_KERNEL_MEMFS_MAXIMUM_CONTROL_CHARACTER ||
            value == OS_KERNEL_MEMFS_DELETE_CONTROL_CHARACTER ||
            value == OS_KERNEL_MEMFS_PATH_SEPARATOR) {
            return false;
        }
    }
    const bool dot =
        name_length_bytes == OS_KERNEL_MEMFS_COUNTER_INCREMENT &&
        name[OS_KERNEL_MEMFS_EMPTY_VALUE] == OS_KERNEL_MEMFS_DOT_CHARACTER;
    const bool dot_dot =
        name_length_bytes ==
            OS_KERNEL_MEMFS_COUNTER_INCREMENT + OS_KERNEL_MEMFS_COUNTER_INCREMENT &&
        name[OS_KERNEL_MEMFS_EMPTY_VALUE] == OS_KERNEL_MEMFS_DOT_CHARACTER &&
        name[OS_KERNEL_MEMFS_COUNTER_INCREMENT] == OS_KERNEL_MEMFS_DOT_CHARACTER;
    return !dot && !dot_dot;
}

}

struct Memfs::Node final {
    Node *next;
    uint64_t identifier;
    uint64_t generation;
    uint64_t parent_identifier;
    NodeType type;
    uint64_t name_length_bytes;
    uint8_t name[OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES];
    uint8_t *data;
    uint64_t size_bytes;
    uint64_t capacity_bytes;
};

const BackendOperations Memfs::operations{
    .lookup = Memfs::LookupOperation,
    .create = Memfs::CreateOperation,
    .parent = Memfs::ParentOperation,
    .read = Memfs::ReadOperation,
    .write = Memfs::WriteOperation,
    .truncate = Memfs::TruncateOperation,
    .read_directory = Memfs::ReadDirectoryOperation,
    .get_name = Memfs::GetNameOperation,
    .sync = Memfs::SyncOperation,
    .validate = Memfs::ValidateOperation,
    .read_resource_usage = Memfs::ReadResourceUsageOperation,
};

Status Memfs::Initialize(KernelHeap &heap, const uint64_t superblock_identifier,
                         const uint64_t node_limit,
                         const uint64_t maximum_file_size_bytes) noexcept {
    if (this->initialized_) {
        return Status::AlreadyInitialized;
    }
    if (superblock_identifier == OS_KERNEL_MEMFS_EMPTY_VALUE ||
        node_limit == OS_KERNEL_MEMFS_EMPTY_VALUE ||
        maximum_file_size_bytes == OS_KERNEL_MEMFS_EMPTY_VALUE ||
        heap.Validate() != KernelHeapStatus::Succeeded) {
        return Status::InvalidArgument;
    }
    const KernelHeapStatistics heap_before = heap.Statistics();
    void *allocation = nullptr;
    if (heap.TryAllocate(sizeof(Node), OS_KERNEL_MEMFS_ALLOCATION_ALIGNMENT_BYTES, allocation) !=
        KernelHeapStatus::Succeeded) {
        return Status::CapacityExhausted;
    }
    const KernelHeapStatistics heap_after = heap.Statistics();
    if (heap_after.consumed_bytes < heap_before.consumed_bytes ||
        heap_after.active_requested_bytes < heap_before.active_requested_bytes ||
        heap_after.allocation_count !=
            heap_before.allocation_count + OS_KERNEL_MEMFS_COUNTER_INCREMENT) {
        static_cast<void>(heap.TryRelease(allocation));
        return Status::Corrupt;
    }
    Node *const root = static_cast<Node *>(allocation);
    *root = Node{
        .next = nullptr,
        .identifier = OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER,
        .generation = OS_KERNEL_MEMFS_INITIAL_GENERATION,
        .parent_identifier = OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER,
        .type = NodeType::Directory,
        .name_length_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE,
        .name = {},
        .data = nullptr,
        .size_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE,
        .capacity_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE,
    };

    this->heap_ = &heap;
    this->nodes_ = root;
    this->node_limit_ = node_limit;
    this->maximum_file_size_bytes_ = maximum_file_size_bytes;
    this->next_node_identifier_ =
        OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER + OS_KERNEL_MEMFS_COUNTER_INCREMENT;
    this->lock_ = SpinLock{};
    this->statistics_ = MemfsStatistics{
        .node_limit = node_limit,
        .active_node_count = OS_KERNEL_MEMFS_COUNTER_INCREMENT,
        .active_directory_count = OS_KERNEL_MEMFS_COUNTER_INCREMENT,
        .active_file_count = OS_KERNEL_MEMFS_EMPTY_VALUE,
        .active_data_capacity_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE,
        .active_heap_consumed_bytes =
            heap_after.consumed_bytes - heap_before.consumed_bytes,
        .active_heap_requested_bytes =
            heap_after.active_requested_bytes - heap_before.active_requested_bytes,
        .active_heap_allocation_count = OS_KERNEL_MEMFS_COUNTER_INCREMENT,
        .created_node_count = OS_KERNEL_MEMFS_COUNTER_INCREMENT,
        .successful_growth_count = OS_KERNEL_MEMFS_EMPTY_VALUE,
        .bytes_read = OS_KERNEL_MEMFS_EMPTY_VALUE,
        .bytes_written = OS_KERNEL_MEMFS_EMPTY_VALUE,
    };
    this->superblock_ = Superblock{
        .backend_kind = BackendKind::Memory,
        .identifier = superblock_identifier,
        .generation = OS_KERNEL_MEMFS_INITIAL_GENERATION,
        .root = {},
        .operations = &Memfs::operations,
        .backend_context = this,
        .maximum_name_length_bytes = OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES,
        .read_only = false,
        .initialized = true,
    };
    this->superblock_.root = this->MakeVnode(*root);
    this->initialized_ = true;
    return Status::Succeeded;
}

Status Memfs::Destroy() noexcept {
    if (!this->initialized_ || this->heap_ == nullptr) {
        return Status::NotInitialized;
    }
    this->lock_.Lock();
    while (this->nodes_ != nullptr) {
        Node *const node = this->nodes_;
        Node *const next = node->next;
        const NodeType node_type = node->type;
        if (node->data != nullptr) {
            const uint64_t capacity_bytes = node->capacity_bytes;
            if (this->TryRelease(node->data) != Status::Succeeded) {
                this->lock_.Unlock();
                return Status::Corrupt;
            }
            node->data = nullptr;
            node->size_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE;
            node->capacity_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE;
            this->statistics_.active_data_capacity_bytes -= capacity_bytes;
        }
        if (this->TryRelease(node) != Status::Succeeded) {
            this->lock_.Unlock();
            return Status::Corrupt;
        }
        this->nodes_ = next;
        --this->statistics_.active_node_count;
        if (node_type == NodeType::Directory) {
            --this->statistics_.active_directory_count;
        } else {
            --this->statistics_.active_file_count;
        }
    }
    if (this->statistics_.active_node_count != OS_KERNEL_MEMFS_EMPTY_VALUE ||
        this->statistics_.active_directory_count != OS_KERNEL_MEMFS_EMPTY_VALUE ||
        this->statistics_.active_file_count != OS_KERNEL_MEMFS_EMPTY_VALUE ||
        this->statistics_.active_data_capacity_bytes != OS_KERNEL_MEMFS_EMPTY_VALUE ||
        this->statistics_.active_heap_consumed_bytes != OS_KERNEL_MEMFS_EMPTY_VALUE ||
        this->statistics_.active_heap_requested_bytes != OS_KERNEL_MEMFS_EMPTY_VALUE ||
        this->statistics_.active_heap_allocation_count != OS_KERNEL_MEMFS_EMPTY_VALUE) {
        this->lock_.Unlock();
        return Status::Corrupt;
    }
    this->initialized_ = false;
    this->lock_.Unlock();
    this->heap_ = nullptr;
    this->superblock_ = Superblock{};
    this->node_limit_ = OS_KERNEL_MEMFS_EMPTY_VALUE;
    this->maximum_file_size_bytes_ = OS_KERNEL_MEMFS_EMPTY_VALUE;
    this->next_node_identifier_ = OS_KERNEL_MEMFS_EMPTY_VALUE;
    this->statistics_ = MemfsStatistics{};
    this->lock_ = SpinLock{};
    return Status::Succeeded;
}

Superblock &Memfs::GetSuperblock() noexcept { return this->superblock_; }

const Superblock &Memfs::GetSuperblock() const noexcept { return this->superblock_; }

MemfsStatistics Memfs::ReadStatistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

Status Memfs::LookupOperation(void *const context, const Vnode &directory,
                              const uint8_t *const name, const uint64_t name_length_bytes,
                              Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || !NameIsValid(name, name_length_bytes)) {
        return Status::InvalidArgument;
    }
    Memfs &memfs = *static_cast<Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    Node *const directory_node = memfs.ValidateVnode(directory);
    if (directory_node == nullptr) {
        return Status::Corrupt;
    }
    if (directory_node->type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    for (Node *candidate = memfs.nodes_; candidate != nullptr; candidate = candidate->next) {
        if (candidate->parent_identifier == directory_node->identifier &&
            candidate->identifier != directory_node->identifier &&
            candidate->name_length_bytes == name_length_bytes &&
            BytesAreEqual(candidate->name, name, name_length_bytes)) {
            vnode = memfs.MakeVnode(*candidate);
            return Status::Succeeded;
        }
    }
    return Status::NotFound;
}

Status Memfs::CreateOperation(void *const context, const Vnode &directory,
                              const uint8_t *const name, const uint64_t name_length_bytes,
                              const NodeType type, Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || !NameIsValid(name, name_length_bytes) ||
        (type != NodeType::RegularFile && type != NodeType::Directory)) {
        return Status::InvalidArgument;
    }
    Memfs &memfs = *static_cast<Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    Node *const directory_node = memfs.ValidateVnode(directory);
    if (directory_node == nullptr) {
        return Status::Corrupt;
    }
    if (directory_node->type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    for (Node *candidate = memfs.nodes_; candidate != nullptr; candidate = candidate->next) {
        if (candidate->parent_identifier == directory_node->identifier &&
            candidate->identifier != directory_node->identifier &&
            candidate->name_length_bytes == name_length_bytes &&
            BytesAreEqual(candidate->name, name, name_length_bytes)) {
            return Status::AlreadyExists;
        }
    }
    if (memfs.statistics_.active_node_count >= memfs.node_limit_ ||
        memfs.next_node_identifier_ == UINT64_MAX) {
        return Status::CapacityExhausted;
    }
    void *allocation = nullptr;
    const Status allocation_status = memfs.TryAllocate(sizeof(Node), allocation);
    if (allocation_status != Status::Succeeded) {
        return allocation_status;
    }
    Node *const node = static_cast<Node *>(allocation);
    *node = Node{
        .next = memfs.nodes_,
        .identifier = memfs.next_node_identifier_,
        .generation = OS_KERNEL_MEMFS_INITIAL_GENERATION,
        .parent_identifier = directory_node->identifier,
        .type = type,
        .name_length_bytes = name_length_bytes,
        .name = {},
        .data = nullptr,
        .size_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE,
        .capacity_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE,
    };
    CopyBytes(node->name, name, name_length_bytes);
    memfs.nodes_ = node;
    ++memfs.next_node_identifier_;
    ++memfs.statistics_.active_node_count;
    ++memfs.statistics_.created_node_count;
    if (type == NodeType::Directory) {
        ++memfs.statistics_.active_directory_count;
    } else {
        ++memfs.statistics_.active_file_count;
    }
    vnode = memfs.MakeVnode(*node);
    return Status::Succeeded;
}

Status Memfs::ParentOperation(void *const context, const Vnode &vnode, Vnode &parent) noexcept {
    parent = Vnode{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Memfs &memfs = *static_cast<Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    const Node *const node = memfs.ValidateVnode(vnode);
    if (node == nullptr) {
        return Status::Corrupt;
    }
    Node *const parent_node = memfs.FindNode(node->parent_identifier);
    if (parent_node == nullptr || parent_node->type != NodeType::Directory) {
        return Status::Corrupt;
    }
    parent = memfs.MakeVnode(*parent_node);
    return Status::Succeeded;
}

Status Memfs::ReadOperation(void *const context, const Vnode &vnode,
                            const uint64_t offset_bytes, uint8_t *const destination,
                            const uint64_t capacity_bytes, uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE;
    if (context == nullptr ||
        (destination == nullptr && capacity_bytes != OS_KERNEL_MEMFS_EMPTY_VALUE)) {
        return Status::InvalidArgument;
    }
    Memfs &memfs = *static_cast<Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    const Node *const node = memfs.ValidateVnode(vnode);
    if (node == nullptr) {
        return Status::Corrupt;
    }
    if (node->type == NodeType::Directory) {
        return Status::IsDirectory;
    }
    if (node->type != NodeType::RegularFile) {
        return Status::Corrupt;
    }
    if (offset_bytes >= node->size_bytes || capacity_bytes == OS_KERNEL_MEMFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    read_bytes = Minimum(capacity_bytes, node->size_bytes - offset_bytes);
    CopyBytes(destination, node->data + offset_bytes, read_bytes);
    memfs.statistics_.bytes_read += read_bytes;
    return Status::Succeeded;
}

Status Memfs::WriteOperation(void *const context, const Vnode &vnode,
                             const uint64_t offset_bytes, const uint8_t *const source,
                             const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE;
    if (context == nullptr ||
        (source == nullptr && length_bytes != OS_KERNEL_MEMFS_EMPTY_VALUE)) {
        return Status::InvalidArgument;
    }
    Memfs &memfs = *static_cast<Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    Node *const node = memfs.ValidateVnode(vnode);
    if (node == nullptr) {
        return Status::Corrupt;
    }
    if (node->type == NodeType::Directory) {
        return Status::IsDirectory;
    }
    if (node->type != NodeType::RegularFile) {
        return Status::Corrupt;
    }
    if (length_bytes == OS_KERNEL_MEMFS_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    if (offset_bytes > memfs.maximum_file_size_bytes_ ||
        length_bytes > memfs.maximum_file_size_bytes_ - offset_bytes) {
        return Status::FileTooLarge;
    }
    const uint64_t final_size_bytes = offset_bytes + length_bytes;
    const Status capacity_status = memfs.EnsureCapacity(*node, final_size_bytes);
    if (capacity_status != Status::Succeeded) {
        return capacity_status;
    }
    if (offset_bytes > node->size_bytes) {
        ClearBytes(node->data + node->size_bytes, offset_bytes - node->size_bytes);
    }
    CopyBytes(node->data + offset_bytes, source, length_bytes);
    if (final_size_bytes > node->size_bytes) {
        node->size_bytes = final_size_bytes;
    }
    written_bytes = length_bytes;
    memfs.statistics_.bytes_written += written_bytes;
    return Status::Succeeded;
}

Status Memfs::TruncateOperation(void *const context, const Vnode &vnode,
                                const uint64_t size_bytes) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Memfs &memfs = *static_cast<Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    Node *const node = memfs.ValidateVnode(vnode);
    if (node == nullptr) {
        return Status::Corrupt;
    }
    if (node->type == NodeType::Directory) {
        return Status::IsDirectory;
    }
    if (node->type != NodeType::RegularFile) {
        return Status::Corrupt;
    }
    if (size_bytes > memfs.maximum_file_size_bytes_) {
        return Status::FileTooLarge;
    }
    const Status capacity_status = memfs.EnsureCapacity(*node, size_bytes);
    if (capacity_status != Status::Succeeded) {
        return capacity_status;
    }
    if (size_bytes > node->size_bytes) {
        ClearBytes(node->data + node->size_bytes, size_bytes - node->size_bytes);
    } else if (size_bytes < node->size_bytes) {
        ClearBytes(node->data + size_bytes, node->size_bytes - size_bytes);
    }
    node->size_bytes = size_bytes;
    return Status::Succeeded;
}

Status Memfs::ReadDirectoryOperation(void *const context, const Vnode &directory,
                                     uint64_t &cursor, DirectoryEntry &entry,
                                     bool &end_of_directory) noexcept {
    entry = DirectoryEntry{};
    end_of_directory = false;
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Memfs &memfs = *static_cast<Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    const Node *const directory_node = memfs.ValidateVnode(directory);
    if (directory_node == nullptr) {
        return Status::Corrupt;
    }
    if (directory_node->type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    const Node *selected_node = nullptr;
    for (const Node *candidate = memfs.nodes_; candidate != nullptr;
         candidate = candidate->next) {
        if (candidate->parent_identifier != directory_node->identifier ||
            candidate->identifier == directory_node->identifier ||
            candidate->identifier <= cursor) {
            continue;
        }
        if (selected_node == nullptr || candidate->identifier < selected_node->identifier) {
            selected_node = candidate;
        }
    }
    if (selected_node == nullptr) {
        end_of_directory = true;
        return Status::Succeeded;
    }
    entry = DirectoryEntry{
        .node_identifier = selected_node->identifier,
        .type = selected_node->type,
        .name_length_bytes = selected_node->name_length_bytes,
        .name = {},
    };
    CopyBytes(entry.name, selected_node->name, selected_node->name_length_bytes);
    cursor = selected_node->identifier;
    return Status::Succeeded;
}

Status Memfs::GetNameOperation(void *const context, const Vnode &vnode, uint8_t *const name,
                               const uint64_t name_capacity_bytes,
                               uint64_t &name_length_bytes) noexcept {
    name_length_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE;
    if (context == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    Memfs &memfs = *static_cast<Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    const Node *const node = memfs.ValidateVnode(vnode);
    if (node == nullptr) {
        return Status::Corrupt;
    }
    if (node->name_length_bytes > name_capacity_bytes) {
        return Status::NameTooLong;
    }
    CopyBytes(name, node->name, node->name_length_bytes);
    name_length_bytes = node->name_length_bytes;
    return Status::Succeeded;
}

Status Memfs::SyncOperation(void *const context) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const Memfs &memfs = *static_cast<const Memfs *>(context);
    return memfs.initialized_ ? Status::Succeeded : Status::NotInitialized;
}

Status Memfs::ValidateOperation(void *const context) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const Memfs &memfs = *static_cast<const Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    return memfs.ValidateUnlocked();
}

Status Memfs::ReadResourceUsageOperation(void *const context,
                                         ResourceUsage &usage) noexcept {
    usage = ResourceUsage{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const Memfs &memfs = *static_cast<const Memfs *>(context);
    SpinLockGuard guard{memfs.lock_};
    if (!memfs.initialized_) {
        return Status::NotInitialized;
    }
    usage = ResourceUsage{
        .heap_consumed_bytes = memfs.statistics_.active_heap_consumed_bytes,
        .heap_active_requested_bytes = memfs.statistics_.active_heap_requested_bytes,
        .heap_allocation_count = memfs.statistics_.active_heap_allocation_count,
        .vnode_count = memfs.statistics_.active_node_count,
    };
    return Status::Succeeded;
}

Memfs::Node *Memfs::FindNode(const uint64_t identifier) noexcept {
    for (Node *node = this->nodes_; node != nullptr; node = node->next) {
        if (node->identifier == identifier) {
            return node;
        }
    }
    return nullptr;
}

const Memfs::Node *Memfs::FindNode(const uint64_t identifier) const noexcept {
    for (const Node *node = this->nodes_; node != nullptr; node = node->next) {
        if (node->identifier == identifier) {
            return node;
        }
    }
    return nullptr;
}

Memfs::Node *Memfs::ValidateVnode(const Vnode &vnode) noexcept {
    if (!this->initialized_ || vnode.superblock != &this->superblock_ ||
        vnode.identifier == OS_KERNEL_MEMFS_EMPTY_VALUE ||
        vnode.generation == OS_KERNEL_MEMFS_EMPTY_VALUE) {
        return nullptr;
    }
    Node *const node = this->FindNode(vnode.identifier);
    return node != nullptr && node->generation == vnode.generation && node->type == vnode.type
               ? node
               : nullptr;
}

const Memfs::Node *Memfs::ValidateVnode(const Vnode &vnode) const noexcept {
    if (!this->initialized_ || vnode.superblock != &this->superblock_ ||
        vnode.identifier == OS_KERNEL_MEMFS_EMPTY_VALUE ||
        vnode.generation == OS_KERNEL_MEMFS_EMPTY_VALUE) {
        return nullptr;
    }
    const Node *const node = this->FindNode(vnode.identifier);
    return node != nullptr && node->generation == vnode.generation && node->type == vnode.type
               ? node
               : nullptr;
}

Vnode Memfs::MakeVnode(const Node &node) noexcept {
    return Vnode{
        .superblock = &this->superblock_,
        .identifier = node.identifier,
        .generation = node.generation,
        .type = node.type,
    };
}

Status Memfs::EnsureCapacity(Node &node, const uint64_t required_capacity_bytes) noexcept {
    if (required_capacity_bytes <= node.capacity_bytes) {
        return Status::Succeeded;
    }
    uint64_t new_capacity_bytes = node.capacity_bytes;
    if (new_capacity_bytes == OS_KERNEL_MEMFS_EMPTY_VALUE) {
        new_capacity_bytes =
            Minimum(OS_KERNEL_MEMFS_INITIAL_FILE_CAPACITY_BYTES,
                    this->maximum_file_size_bytes_);
    }
    while (new_capacity_bytes < required_capacity_bytes) {
        if (new_capacity_bytes > this->maximum_file_size_bytes_ /
                                     OS_KERNEL_MEMFS_CAPACITY_GROWTH_FACTOR) {
            new_capacity_bytes = this->maximum_file_size_bytes_;
            break;
        }
        new_capacity_bytes *= OS_KERNEL_MEMFS_CAPACITY_GROWTH_FACTOR;
    }
    if (new_capacity_bytes < required_capacity_bytes ||
        new_capacity_bytes > this->maximum_file_size_bytes_) {
        return Status::FileTooLarge;
    }
    void *allocation = nullptr;
    const Status allocation_status = this->TryAllocate(new_capacity_bytes, allocation);
    if (allocation_status != Status::Succeeded) {
        return allocation_status;
    }
    uint8_t *const new_data = static_cast<uint8_t *>(allocation);
    ClearBytes(new_data, new_capacity_bytes);
    if (node.data != nullptr) {
        CopyBytes(new_data, node.data, node.size_bytes);
        if (this->TryRelease(node.data) != Status::Succeeded) {
            static_cast<void>(this->TryRelease(new_data));
            return Status::Corrupt;
        }
    }
    this->statistics_.active_data_capacity_bytes -= node.capacity_bytes;
    this->statistics_.active_data_capacity_bytes += new_capacity_bytes;
    ++this->statistics_.successful_growth_count;
    node.data = new_data;
    node.capacity_bytes = new_capacity_bytes;
    return Status::Succeeded;
}

Status Memfs::TryAllocate(const uint64_t size_bytes, void *&allocation) noexcept {
    allocation = nullptr;
    if (this->heap_ == nullptr || size_bytes == OS_KERNEL_MEMFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    const KernelHeapStatistics before = this->heap_->Statistics();
    const KernelHeapStatus status =
        this->heap_->TryAllocate(size_bytes, OS_KERNEL_MEMFS_ALLOCATION_ALIGNMENT_BYTES,
                                 allocation);
    if (status != KernelHeapStatus::Succeeded) {
        return status == KernelHeapStatus::OutOfMemory ? Status::CapacityExhausted
                                                       : Status::Corrupt;
    }
    const KernelHeapStatistics after = this->heap_->Statistics();
    if (after.consumed_bytes < before.consumed_bytes ||
        after.active_requested_bytes < before.active_requested_bytes ||
        after.allocation_count !=
            before.allocation_count + OS_KERNEL_MEMFS_COUNTER_INCREMENT) {
        static_cast<void>(this->heap_->TryRelease(allocation));
        allocation = nullptr;
        return Status::Corrupt;
    }
    const uint64_t consumed_delta = after.consumed_bytes - before.consumed_bytes;
    const uint64_t requested_delta =
        after.active_requested_bytes - before.active_requested_bytes;
    if (this->statistics_.active_heap_consumed_bytes > UINT64_MAX - consumed_delta ||
        this->statistics_.active_heap_requested_bytes > UINT64_MAX - requested_delta ||
        this->statistics_.active_heap_allocation_count == UINT64_MAX) {
        static_cast<void>(this->heap_->TryRelease(allocation));
        allocation = nullptr;
        return Status::Corrupt;
    }
    this->statistics_.active_heap_consumed_bytes += consumed_delta;
    this->statistics_.active_heap_requested_bytes += requested_delta;
    ++this->statistics_.active_heap_allocation_count;
    return Status::Succeeded;
}

Status Memfs::TryRelease(void *const allocation) noexcept {
    if (this->heap_ == nullptr || allocation == nullptr) {
        return Status::InvalidArgument;
    }
    const KernelHeapStatistics before = this->heap_->Statistics();
    if (this->heap_->TryRelease(allocation) != KernelHeapStatus::Succeeded) {
        return Status::Corrupt;
    }
    const KernelHeapStatistics after = this->heap_->Statistics();
    if (before.consumed_bytes < after.consumed_bytes ||
        before.active_requested_bytes < after.active_requested_bytes ||
        before.allocation_count !=
            after.allocation_count + OS_KERNEL_MEMFS_COUNTER_INCREMENT) {
        return Status::Corrupt;
    }
    const uint64_t consumed_delta = before.consumed_bytes - after.consumed_bytes;
    const uint64_t requested_delta =
        before.active_requested_bytes - after.active_requested_bytes;
    if (this->statistics_.active_heap_consumed_bytes < consumed_delta ||
        this->statistics_.active_heap_requested_bytes < requested_delta ||
        this->statistics_.active_heap_allocation_count ==
            OS_KERNEL_MEMFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    this->statistics_.active_heap_consumed_bytes -= consumed_delta;
    this->statistics_.active_heap_requested_bytes -= requested_delta;
    --this->statistics_.active_heap_allocation_count;
    return Status::Succeeded;
}

Status Memfs::ValidateUnlocked() const noexcept {
    if (!this->initialized_ || this->heap_ == nullptr || this->nodes_ == nullptr ||
        !this->superblock_.initialized || this->superblock_.backend_context != this ||
        this->statistics_.node_limit != this->node_limit_ ||
        this->statistics_.active_node_count == OS_KERNEL_MEMFS_EMPTY_VALUE ||
        this->statistics_.active_node_count > this->node_limit_) {
        return Status::Corrupt;
    }
    uint64_t visited_count = OS_KERNEL_MEMFS_EMPTY_VALUE;
    uint64_t directory_count = OS_KERNEL_MEMFS_EMPTY_VALUE;
    uint64_t file_count = OS_KERNEL_MEMFS_EMPTY_VALUE;
    uint64_t data_capacity_bytes = OS_KERNEL_MEMFS_EMPTY_VALUE;
    uint64_t data_allocation_count = OS_KERNEL_MEMFS_EMPTY_VALUE;
    const Node *root = nullptr;
    for (const Node *node = this->nodes_; node != nullptr; node = node->next) {
        if (visited_count >= this->node_limit_) {
            return Status::LoopDetected;
        }
        ++visited_count;
        if (node->identifier == OS_KERNEL_MEMFS_EMPTY_VALUE ||
            node->generation == OS_KERNEL_MEMFS_EMPTY_VALUE ||
            (node->type != NodeType::RegularFile && node->type != NodeType::Directory) ||
            node->size_bytes > node->capacity_bytes ||
            node->capacity_bytes > this->maximum_file_size_bytes_ ||
            ((node->capacity_bytes == OS_KERNEL_MEMFS_EMPTY_VALUE) != (node->data == nullptr))) {
            return Status::Corrupt;
        }
        if (node->identifier == OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER) {
            if (root != nullptr || node->type != NodeType::Directory ||
                node->parent_identifier != OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER ||
                node->name_length_bytes != OS_KERNEL_MEMFS_EMPTY_VALUE) {
                return Status::Corrupt;
            }
            root = node;
        } else {
            if (!NameIsValid(node->name, node->name_length_bytes)) {
                return Status::Corrupt;
            }
            const Node *const parent = this->FindNode(node->parent_identifier);
            if (parent == nullptr || parent->type != NodeType::Directory) {
                return Status::Corrupt;
            }
        }
        for (const Node *other = node->next; other != nullptr; other = other->next) {
            if (other->identifier == node->identifier ||
                (other->parent_identifier == node->parent_identifier &&
                 other->identifier != OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER &&
                 node->identifier != OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER &&
                 other->name_length_bytes == node->name_length_bytes &&
                 BytesAreEqual(other->name, node->name, node->name_length_bytes))) {
                return Status::Corrupt;
            }
        }
        uint64_t ancestor_identifier = node->identifier;
        uint64_t ancestor_count = OS_KERNEL_MEMFS_EMPTY_VALUE;
        while (ancestor_identifier != OS_KERNEL_MEMFS_ROOT_NODE_IDENTIFIER) {
            if (ancestor_count >= this->node_limit_) {
                return Status::LoopDetected;
            }
            ++ancestor_count;
            const Node *const ancestor = this->FindNode(ancestor_identifier);
            if (ancestor == nullptr) {
                return Status::Corrupt;
            }
            ancestor_identifier = ancestor->parent_identifier;
        }
        if (node->type == NodeType::Directory) {
            if (node->data != nullptr || node->size_bytes != OS_KERNEL_MEMFS_EMPTY_VALUE ||
                node->capacity_bytes != OS_KERNEL_MEMFS_EMPTY_VALUE) {
                return Status::Corrupt;
            }
            ++directory_count;
        } else {
            ++file_count;
            if (data_capacity_bytes > UINT64_MAX - node->capacity_bytes) {
                return Status::Corrupt;
            }
            data_capacity_bytes += node->capacity_bytes;
            if (node->data != nullptr) {
                ++data_allocation_count;
            }
        }
    }
    if (visited_count > UINT64_MAX / sizeof(Node) ||
        visited_count + data_allocation_count < visited_count) {
        return Status::Corrupt;
    }
    const uint64_t node_requested_bytes = visited_count * sizeof(Node);
    if (node_requested_bytes > UINT64_MAX - data_capacity_bytes) {
        return Status::Corrupt;
    }
    const uint64_t expected_heap_requested_bytes =
        node_requested_bytes + data_capacity_bytes;
    const uint64_t expected_heap_allocation_count =
        visited_count + data_allocation_count;
    const KernelHeapStatistics heap_statistics = this->heap_->Statistics();
    if (root == nullptr || visited_count != this->statistics_.active_node_count ||
        directory_count != this->statistics_.active_directory_count ||
        file_count != this->statistics_.active_file_count ||
        data_capacity_bytes != this->statistics_.active_data_capacity_bytes ||
        expected_heap_requested_bytes !=
            this->statistics_.active_heap_requested_bytes ||
        expected_heap_allocation_count !=
            this->statistics_.active_heap_allocation_count ||
        this->statistics_.active_heap_consumed_bytes <
            this->statistics_.active_heap_requested_bytes ||
        this->statistics_.active_heap_consumed_bytes > heap_statistics.consumed_bytes ||
        this->statistics_.active_heap_requested_bytes >
            heap_statistics.active_requested_bytes ||
        this->statistics_.active_heap_allocation_count >
            heap_statistics.allocation_count ||
        visited_count != directory_count + file_count) {
        return Status::Corrupt;
    }
    return Status::Succeeded;
}

}
