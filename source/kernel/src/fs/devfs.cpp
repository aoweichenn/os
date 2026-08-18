#include "os/kernel/fs/devfs.hpp"

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_DEVFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_DEVFS_ROOT_NODE_IDENTIFIER = 1ULL;
constexpr uint64_t OS_KERNEL_DEVFS_FIRST_DEVICE_NODE_IDENTIFIER = 2ULL;
constexpr uint64_t OS_KERNEL_DEVFS_INITIAL_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_DEVFS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_DEVFS_ROOT_LINK_COUNT = 2ULL;
constexpr uint64_t OS_KERNEL_DEVFS_DEVICE_LINK_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_DEVFS_MAXIMUM_COUNTER_VALUE = ~0ULL;

void IncrementSaturatingCounter(uint64_t &counter) noexcept {
    if (counter != OS_KERNEL_DEVFS_MAXIMUM_COUNTER_VALUE) {
        ++counter;
    }
}

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left,
                                 const uint8_t *const right,
                                 const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_DEVFS_EMPTY_VALUE;
         byte_index < length_bytes; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_DEVFS_EMPTY_VALUE;
         byte_index < length_bytes; ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

}

const BackendOperations Devfs::operations{
    .lookup = Devfs::LookupOperation,
    .create = Devfs::CreateOperation,
    .open = Devfs::OpenOperation,
    .close = Devfs::CloseOperation,
    .remove = Devfs::RemoveOperation,
    .rename = Devfs::RenameOperation,
    .link = nullptr,
    .create_symbolic_link = nullptr,
    .read_symbolic_link = nullptr,
    .parent = Devfs::ParentOperation,
    .read = Devfs::ReadOperation,
    .write = Devfs::WriteOperation,
    .truncate = Devfs::TruncateOperation,
    .read_directory = Devfs::ReadDirectoryOperation,
    .get_name = Devfs::GetNameOperation,
    .stat = Devfs::StatOperation,
    .sync = Devfs::SyncOperation,
    .validate = Devfs::ValidateOperation,
    .read_resource_usage = Devfs::ReadResourceUsageOperation,
};

Status Devfs::Initialize(const uint64_t superblock_identifier,
                         DevfsDevice *const device_storage,
                         const uint64_t device_capacity) noexcept {
    if (this->initialized_) {
        return Status::AlreadyInitialized;
    }
    if (superblock_identifier == OS_KERNEL_DEVFS_EMPTY_VALUE ||
        device_storage == nullptr ||
        device_capacity == OS_KERNEL_DEVFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    this->devices_ = device_storage;
    this->device_capacity_ = device_capacity;
    for (uint64_t device_index = OS_KERNEL_DEVFS_EMPTY_VALUE;
         device_index < this->device_capacity_; ++device_index) {
        this->devices_[device_index] = DevfsDevice{};
    }
    this->lock_ = SpinLock{};
    this->statistics_ = DevfsStatistics{};
    this->superblock_ = Superblock{
        .backend_kind = BackendKind::Device,
        .identifier = superblock_identifier,
        .generation = OS_KERNEL_DEVFS_INITIAL_GENERATION,
        .root = {},
        .operations = &Devfs::operations,
        .backend_context = this,
        .maximum_name_length_bytes = OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES,
        .read_only = true,
        .initialized = true,
    };
    this->superblock_.root = this->MakeRootVnode();
    this->initialized_ = true;
    return Status::Succeeded;
}

Status Devfs::RegisterCharacterDevice(const uint8_t *const name,
                                      const uint64_t name_length_bytes,
                                      uint64_t &node_identifier) noexcept {
    node_identifier = OS_KERNEL_DEVFS_EMPTY_VALUE;
    if (!this->initialized_) {
        return Status::NotInitialized;
    }
    if (name == nullptr || name_length_bytes == OS_KERNEL_DEVFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    if (name_length_bytes > OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES) {
        return Status::NameTooLong;
    }

    SpinLockGuard guard{this->lock_};
    uint64_t available_index = this->device_capacity_;
    for (uint64_t device_index = OS_KERNEL_DEVFS_EMPTY_VALUE;
         device_index < this->device_capacity_; ++device_index) {
        DevfsDevice &device = this->devices_[device_index];
        if (device.active && device.name_length_bytes == name_length_bytes &&
            BytesAreEqual(device.name, name, name_length_bytes)) {
            IncrementSaturatingCounter(
                this->statistics_.rejected_registration_count);
            return Status::AlreadyExists;
        }
        if (!device.active && available_index == this->device_capacity_) {
            available_index = device_index;
        }
    }
    if (available_index == this->device_capacity_) {
        IncrementSaturatingCounter(
            this->statistics_.rejected_registration_count);
        return Status::CapacityExhausted;
    }

    DevfsDevice &device = this->devices_[available_index];
    device = DevfsDevice{
        .node_identifier =
            OS_KERNEL_DEVFS_FIRST_DEVICE_NODE_IDENTIFIER + available_index,
        .generation = OS_KERNEL_DEVFS_INITIAL_GENERATION,
        .name_length_bytes = name_length_bytes,
        .name = {},
        .active = true,
    };
    CopyBytes(device.name, name, name_length_bytes);
    IncrementSaturatingCounter(this->statistics_.registered_device_count);
    node_identifier = device.node_identifier;
    return Status::Succeeded;
}

Superblock &Devfs::GetSuperblock() noexcept { return this->superblock_; }

const Superblock &Devfs::GetSuperblock() const noexcept {
    return this->superblock_;
}

DevfsStatistics Devfs::ReadStatistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

Status Devfs::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || !this->superblock_.initialized ||
        this->devices_ == nullptr ||
        this->device_capacity_ == OS_KERNEL_DEVFS_EMPTY_VALUE ||
        this->superblock_.backend_kind != BackendKind::Device ||
        this->superblock_.operations != &Devfs::operations ||
        this->superblock_.backend_context != this ||
        this->superblock_.identifier == OS_KERNEL_DEVFS_EMPTY_VALUE ||
        this->superblock_.generation != OS_KERNEL_DEVFS_INITIAL_GENERATION ||
        !this->superblock_.read_only ||
        !this->VnodeIsValid(this->superblock_.root) ||
        this->statistics_.successful_open_count <
            this->statistics_.active_open_count) {
        return Status::Corrupt;
    }
    uint64_t active_count = OS_KERNEL_DEVFS_EMPTY_VALUE;
    for (uint64_t device_index = OS_KERNEL_DEVFS_EMPTY_VALUE;
         device_index < this->device_capacity_; ++device_index) {
        const DevfsDevice &device = this->devices_[device_index];
        if (!device.active) {
            continue;
        }
        ++active_count;
        if (device.node_identifier !=
                OS_KERNEL_DEVFS_FIRST_DEVICE_NODE_IDENTIFIER + device_index ||
            device.generation != OS_KERNEL_DEVFS_INITIAL_GENERATION ||
            device.name_length_bytes == OS_KERNEL_DEVFS_EMPTY_VALUE ||
            device.name_length_bytes > OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES) {
            return Status::Corrupt;
        }
        for (uint64_t other_index = device_index + OS_KERNEL_DEVFS_COUNTER_INCREMENT;
             other_index < this->device_capacity_; ++other_index) {
            const DevfsDevice &other = this->devices_[other_index];
            if (other.active &&
                other.name_length_bytes == device.name_length_bytes &&
                BytesAreEqual(other.name, device.name,
                              device.name_length_bytes)) {
                return Status::Corrupt;
            }
        }
    }
    return active_count == this->statistics_.registered_device_count
               ? Status::Succeeded
               : Status::Corrupt;
}

Status Devfs::LookupOperation(void *const context, const Vnode &directory,
                              const uint8_t *const name,
                              const uint64_t name_length_bytes,
                              Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    Devfs &file_system = *static_cast<Devfs *>(context);
    if (!file_system.VnodeIsValid(directory)) {
        return Status::Corrupt;
    }
    if (directory.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    SpinLockGuard guard{file_system.lock_};
    for (uint64_t device_index = OS_KERNEL_DEVFS_EMPTY_VALUE;
         device_index < file_system.device_capacity_; ++device_index) {
        const DevfsDevice &device = file_system.devices_[device_index];
        if (device.active && device.name_length_bytes == name_length_bytes &&
            BytesAreEqual(device.name, name, name_length_bytes)) {
            vnode = file_system.MakeDeviceVnode(device);
            return Status::Succeeded;
        }
    }
    return Status::NotFound;
}

Status Devfs::CreateOperation(void *const context, const Vnode &directory,
                              const uint8_t *const name,
                              const uint64_t name_length_bytes,
                              const NodeType type, Vnode &vnode) noexcept {
    static_cast<void>(context);
    static_cast<void>(directory);
    static_cast<void>(name);
    static_cast<void>(name_length_bytes);
    static_cast<void>(type);
    vnode = Vnode{};
    return Status::ReadOnly;
}

Status Devfs::OpenOperation(void *const context,
                            const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Devfs &file_system = *static_cast<Devfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    SpinLockGuard guard{file_system.lock_};
    if (file_system.statistics_.active_open_count ==
        OS_KERNEL_DEVFS_MAXIMUM_COUNTER_VALUE) {
        return Status::CapacityExhausted;
    }
    ++file_system.statistics_.active_open_count;
    IncrementSaturatingCounter(
        file_system.statistics_.successful_open_count);
    return Status::Succeeded;
}

Status Devfs::CloseOperation(void *const context,
                             const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Devfs &file_system = *static_cast<Devfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    SpinLockGuard guard{file_system.lock_};
    if (file_system.statistics_.active_open_count ==
        OS_KERNEL_DEVFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    --file_system.statistics_.active_open_count;
    return Status::Succeeded;
}

Status Devfs::RemoveOperation(void *const context, const Vnode &directory,
                              const uint8_t *const name,
                              const uint64_t name_length_bytes,
                              const NodeType expected_type) noexcept {
    static_cast<void>(context);
    static_cast<void>(directory);
    static_cast<void>(name);
    static_cast<void>(name_length_bytes);
    static_cast<void>(expected_type);
    return Status::ReadOnly;
}

Status Devfs::RenameOperation(
    void *const context, const Vnode &source_directory,
    const uint8_t *const source_name, const uint64_t source_name_length_bytes,
    const Vnode &destination_directory, const uint8_t *const destination_name,
    const uint64_t destination_name_length_bytes, const bool replace) noexcept {
    static_cast<void>(context);
    static_cast<void>(source_directory);
    static_cast<void>(source_name);
    static_cast<void>(source_name_length_bytes);
    static_cast<void>(destination_directory);
    static_cast<void>(destination_name);
    static_cast<void>(destination_name_length_bytes);
    static_cast<void>(replace);
    return Status::ReadOnly;
}

Status Devfs::ParentOperation(void *const context, const Vnode &vnode,
                              Vnode &parent) noexcept {
    parent = Vnode{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Devfs &file_system = *static_cast<Devfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    parent = file_system.MakeRootVnode();
    return Status::Succeeded;
}

Status Devfs::ReadOperation(void *const context, const Vnode &vnode,
                            const uint64_t offset_bytes,
                            uint8_t *const destination,
                            const uint64_t capacity_bytes,
                            uint64_t &read_bytes) noexcept {
    static_cast<void>(context);
    static_cast<void>(vnode);
    static_cast<void>(offset_bytes);
    static_cast<void>(destination);
    static_cast<void>(capacity_bytes);
    read_bytes = OS_KERNEL_DEVFS_EMPTY_VALUE;
    return Status::Unsupported;
}

Status Devfs::WriteOperation(void *const context, const Vnode &vnode,
                             const uint64_t offset_bytes,
                             const uint8_t *const source,
                             const uint64_t length_bytes,
                             uint64_t &written_bytes) noexcept {
    static_cast<void>(context);
    static_cast<void>(vnode);
    static_cast<void>(offset_bytes);
    static_cast<void>(source);
    static_cast<void>(length_bytes);
    written_bytes = OS_KERNEL_DEVFS_EMPTY_VALUE;
    return Status::Unsupported;
}

Status Devfs::TruncateOperation(void *const context, const Vnode &vnode,
                                const uint64_t size_bytes) noexcept {
    static_cast<void>(context);
    static_cast<void>(vnode);
    static_cast<void>(size_bytes);
    return Status::Unsupported;
}

Status Devfs::ReadDirectoryOperation(void *const context,
                                     const Vnode &directory,
                                     uint64_t &cursor, DirectoryEntry &entry,
                                     bool &end_of_directory) noexcept {
    entry = DirectoryEntry{};
    end_of_directory = false;
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Devfs &file_system = *static_cast<Devfs *>(context);
    if (!file_system.VnodeIsValid(directory)) {
        return Status::InvalidHandle;
    }
    if (directory.type != NodeType::Directory) {
        return Status::NotDirectory;
    }

    SpinLockGuard guard{file_system.lock_};
    while (cursor < file_system.device_capacity_ &&
           !file_system.devices_[cursor].active) {
        ++cursor;
    }
    if (cursor == file_system.device_capacity_) {
        end_of_directory = true;
        return Status::Succeeded;
    }
    const DevfsDevice &device = file_system.devices_[cursor];
    entry = DirectoryEntry{
        .node_identifier = device.node_identifier,
        .type = NodeType::CharacterDevice,
        .name_length_bytes = device.name_length_bytes,
        .name = {},
    };
    CopyBytes(entry.name, device.name, device.name_length_bytes);
    ++cursor;
    IncrementSaturatingCounter(
        file_system.statistics_.directory_read_count);
    return Status::Succeeded;
}

Status Devfs::GetNameOperation(void *const context, const Vnode &vnode,
                               uint8_t *const name,
                               const uint64_t name_capacity_bytes,
                               uint64_t &name_length_bytes) noexcept {
    name_length_bytes = OS_KERNEL_DEVFS_EMPTY_VALUE;
    if (context == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    Devfs &file_system = *static_cast<Devfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    if (vnode.type == NodeType::Directory) {
        return Status::Succeeded;
    }
    SpinLockGuard guard{file_system.lock_};
    const DevfsDevice *const device =
        file_system.FindDevice(vnode.identifier);
    if (device == nullptr) {
        return Status::InvalidHandle;
    }
    if (name_capacity_bytes < device->name_length_bytes) {
        return Status::NameTooLong;
    }
    CopyBytes(name, device->name, device->name_length_bytes);
    name_length_bytes = device->name_length_bytes;
    return Status::Succeeded;
}

Status Devfs::StatOperation(void *const context, const Vnode &vnode,
                            BackendNodeInformation &information) noexcept {
    information = BackendNodeInformation{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const Devfs &file_system = *static_cast<const Devfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    information = BackendNodeInformation{
        .size_bytes = OS_KERNEL_DEVFS_EMPTY_VALUE,
        .allocated_size_bytes = OS_KERNEL_DEVFS_EMPTY_VALUE,
        .link_count = vnode.type == NodeType::Directory
                          ? OS_KERNEL_DEVFS_ROOT_LINK_COUNT
                          : OS_KERNEL_DEVFS_DEVICE_LINK_COUNT,
        .access_time_nanoseconds = OS_KERNEL_DEVFS_EMPTY_VALUE,
        .modification_time_nanoseconds = OS_KERNEL_DEVFS_EMPTY_VALUE,
        .change_time_nanoseconds = OS_KERNEL_DEVFS_EMPTY_VALUE,
        .birth_time_nanoseconds = OS_KERNEL_DEVFS_EMPTY_VALUE,
    };
    return Status::Succeeded;
}

Status Devfs::SyncOperation(void *const context) noexcept {
    return context == nullptr ? Status::InvalidArgument : Status::Succeeded;
}

Status Devfs::ValidateOperation(void *const context) noexcept {
    return context == nullptr
               ? Status::InvalidArgument
               : static_cast<Devfs *>(context)->Validate();
}

Status Devfs::ReadResourceUsageOperation(void *const context,
                                         ResourceUsage &usage) noexcept {
    usage = ResourceUsage{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const Devfs &file_system = *static_cast<const Devfs *>(context);
    if (file_system.Validate() != Status::Succeeded) {
        return Status::Corrupt;
    }
    usage.vnode_count =
        OS_KERNEL_DEVFS_ROOT_NODE_IDENTIFIER +
        file_system.ReadStatistics().registered_device_count;
    return Status::Succeeded;
}

Vnode Devfs::MakeRootVnode() noexcept {
    return Vnode{
        .superblock = &this->superblock_,
        .identifier = OS_KERNEL_DEVFS_ROOT_NODE_IDENTIFIER,
        .generation = OS_KERNEL_DEVFS_INITIAL_GENERATION,
        .type = NodeType::Directory,
    };
}

Vnode Devfs::MakeDeviceVnode(const DevfsDevice &device) noexcept {
    return Vnode{
        .superblock = &this->superblock_,
        .identifier = device.node_identifier,
        .generation = device.generation,
        .type = NodeType::CharacterDevice,
    };
}

DevfsDevice *Devfs::FindDevice(const uint64_t node_identifier) noexcept {
    return const_cast<DevfsDevice *>(
        static_cast<const Devfs *>(this)->FindDevice(node_identifier));
}

const DevfsDevice *
Devfs::FindDevice(const uint64_t node_identifier) const noexcept {
    if (node_identifier < OS_KERNEL_DEVFS_FIRST_DEVICE_NODE_IDENTIFIER) {
        return nullptr;
    }
    const uint64_t device_index =
        node_identifier - OS_KERNEL_DEVFS_FIRST_DEVICE_NODE_IDENTIFIER;
    if (device_index >= this->device_capacity_) {
        return nullptr;
    }
    const DevfsDevice &device = this->devices_[device_index];
    return device.active && device.node_identifier == node_identifier
               ? &device
               : nullptr;
}

bool Devfs::VnodeIsValid(const Vnode &vnode) const noexcept {
    if (!this->initialized_ || vnode.superblock != &this->superblock_ ||
        vnode.generation != OS_KERNEL_DEVFS_INITIAL_GENERATION) {
        return false;
    }
    if (vnode.identifier == OS_KERNEL_DEVFS_ROOT_NODE_IDENTIFIER) {
        return vnode.type == NodeType::Directory;
    }
    return vnode.type == NodeType::CharacterDevice &&
           this->FindDevice(vnode.identifier) != nullptr;
}

}
