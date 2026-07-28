#include "os/kernel/fs/console_device_file_system.hpp"

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_CONSOLE_DEVICE_FS_ROOT_NODE_IDENTIFIER = 1ULL;
constexpr uint64_t OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NODE_IDENTIFIER = 2ULL;
constexpr uint64_t OS_KERNEL_CONSOLE_DEVICE_FS_INITIAL_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_CONSOLE_DEVICE_FS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_CONSOLE_DEVICE_FS_ROOT_LINK_COUNT = 2ULL;
constexpr uint64_t OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_LINK_COUNT = 1ULL;
constexpr char OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME[] = "console";
constexpr uint64_t OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME_LENGTH_BYTES =
    sizeof(OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME) - 1ULL;

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left, const char *const right,
                                 const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE;
         byte_index < length_bytes; ++byte_index) {
        if (left[byte_index] != static_cast<uint8_t>(right[byte_index])) {
            return false;
        }
    }
    return true;
}

void CopyName(uint8_t *const destination) noexcept {
    for (uint64_t byte_index = OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE;
         byte_index < OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME_LENGTH_BYTES; ++byte_index) {
        destination[byte_index] =
            static_cast<uint8_t>(OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME[byte_index]);
    }
}

}

const BackendOperations ConsoleDeviceFileSystem::operations{
    .lookup = ConsoleDeviceFileSystem::LookupOperation,
    .create = ConsoleDeviceFileSystem::CreateOperation,
    .open = ConsoleDeviceFileSystem::OpenOperation,
    .close = ConsoleDeviceFileSystem::CloseOperation,
    .remove = ConsoleDeviceFileSystem::RemoveOperation,
    .rename = ConsoleDeviceFileSystem::RenameOperation,
    .parent = ConsoleDeviceFileSystem::ParentOperation,
    .read = ConsoleDeviceFileSystem::ReadOperation,
    .write = ConsoleDeviceFileSystem::WriteOperation,
    .truncate = ConsoleDeviceFileSystem::TruncateOperation,
    .read_directory = ConsoleDeviceFileSystem::ReadDirectoryOperation,
    .get_name = ConsoleDeviceFileSystem::GetNameOperation,
    .stat = ConsoleDeviceFileSystem::StatOperation,
    .sync = ConsoleDeviceFileSystem::SyncOperation,
    .validate = ConsoleDeviceFileSystem::ValidateOperation,
    .read_resource_usage = ConsoleDeviceFileSystem::ReadResourceUsageOperation,
};

Status
ConsoleDeviceFileSystem::Initialize(const uint64_t superblock_identifier) noexcept {
    if (this->initialized_) {
        return Status::AlreadyInitialized;
    }
    if (superblock_identifier == OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    this->lock_ = SpinLock{};
    this->statistics_ = ConsoleDeviceFileSystemStatistics{};
    this->superblock_ = Superblock{
        .backend_kind = BackendKind::Device,
        .identifier = superblock_identifier,
        .generation = OS_KERNEL_CONSOLE_DEVICE_FS_INITIAL_GENERATION,
        .root = {},
        .operations = &ConsoleDeviceFileSystem::operations,
        .backend_context = this,
        .maximum_name_length_bytes = OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME_LENGTH_BYTES,
        .read_only = true,
        .initialized = true,
    };
    this->superblock_.root = this->MakeRootVnode();
    this->initialized_ = true;
    return Status::Succeeded;
}

Superblock &ConsoleDeviceFileSystem::GetSuperblock() noexcept {
    return this->superblock_;
}

const Superblock &ConsoleDeviceFileSystem::GetSuperblock() const noexcept {
    return this->superblock_;
}

ConsoleDeviceFileSystemStatistics
ConsoleDeviceFileSystem::ReadStatistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

Status ConsoleDeviceFileSystem::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    if (!this->initialized_ || !this->superblock_.initialized ||
        this->superblock_.backend_kind != BackendKind::Device ||
        this->superblock_.operations != &ConsoleDeviceFileSystem::operations ||
        this->superblock_.backend_context != this ||
        this->superblock_.identifier == OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE ||
        this->superblock_.generation != OS_KERNEL_CONSOLE_DEVICE_FS_INITIAL_GENERATION ||
        !this->superblock_.read_only || !this->VnodeIsValid(this->superblock_.root) ||
        this->superblock_.root.type != NodeType::Directory ||
        this->statistics_.successful_open_count < this->statistics_.active_open_count) {
        return Status::Corrupt;
    }
    return Status::Succeeded;
}

Status ConsoleDeviceFileSystem::LookupOperation(
    void *const context, const Vnode &directory, const uint8_t *const name,
    const uint64_t name_length_bytes, Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    ConsoleDeviceFileSystem &file_system =
        *static_cast<ConsoleDeviceFileSystem *>(context);
    if (!file_system.VnodeIsValid(directory)) {
        return Status::Corrupt;
    }
    if (directory.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if (name_length_bytes != OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME_LENGTH_BYTES ||
        !BytesAreEqual(name, OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME, name_length_bytes)) {
        return Status::NotFound;
    }
    vnode = file_system.MakeConsoleVnode();
    return Status::Succeeded;
}

Status ConsoleDeviceFileSystem::CreateOperation(
    void *const context, const Vnode &directory, const uint8_t *const name,
    const uint64_t name_length_bytes, const NodeType type, Vnode &vnode) noexcept {
    static_cast<void>(context);
    static_cast<void>(directory);
    static_cast<void>(name);
    static_cast<void>(name_length_bytes);
    static_cast<void>(type);
    vnode = Vnode{};
    return Status::ReadOnly;
}

Status ConsoleDeviceFileSystem::OpenOperation(void *const context,
                                              const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    ConsoleDeviceFileSystem &file_system =
        *static_cast<ConsoleDeviceFileSystem *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    SpinLockGuard guard{file_system.lock_};
    ++file_system.statistics_.active_open_count;
    ++file_system.statistics_.successful_open_count;
    return Status::Succeeded;
}

Status ConsoleDeviceFileSystem::CloseOperation(void *const context,
                                               const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    ConsoleDeviceFileSystem &file_system =
        *static_cast<ConsoleDeviceFileSystem *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    SpinLockGuard guard{file_system.lock_};
    if (file_system.statistics_.active_open_count ==
        OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    --file_system.statistics_.active_open_count;
    return Status::Succeeded;
}

Status ConsoleDeviceFileSystem::RemoveOperation(
    void *const context, const Vnode &directory, const uint8_t *const name,
    const uint64_t name_length_bytes, const NodeType expected_type) noexcept {
    static_cast<void>(context);
    static_cast<void>(directory);
    static_cast<void>(name);
    static_cast<void>(name_length_bytes);
    static_cast<void>(expected_type);
    return Status::ReadOnly;
}

Status ConsoleDeviceFileSystem::RenameOperation(
    void *const context, const Vnode &source_directory, const uint8_t *const source_name,
    const uint64_t source_name_length_bytes, const Vnode &destination_directory,
    const uint8_t *const destination_name, const uint64_t destination_name_length_bytes,
    const bool replace) noexcept {
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

Status ConsoleDeviceFileSystem::ParentOperation(void *const context, const Vnode &vnode,
                                                Vnode &parent) noexcept {
    parent = Vnode{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    ConsoleDeviceFileSystem &file_system =
        *static_cast<ConsoleDeviceFileSystem *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    parent = file_system.MakeRootVnode();
    return Status::Succeeded;
}

Status ConsoleDeviceFileSystem::ReadOperation(
    void *const context, const Vnode &vnode, const uint64_t offset_bytes,
    uint8_t *const destination, const uint64_t capacity_bytes,
    uint64_t &read_bytes) noexcept {
    static_cast<void>(context);
    static_cast<void>(vnode);
    static_cast<void>(offset_bytes);
    static_cast<void>(destination);
    static_cast<void>(capacity_bytes);
    read_bytes = OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE;
    return Status::Unsupported;
}

Status ConsoleDeviceFileSystem::WriteOperation(
    void *const context, const Vnode &vnode, const uint64_t offset_bytes,
    const uint8_t *const source, const uint64_t length_bytes,
    uint64_t &written_bytes) noexcept {
    static_cast<void>(context);
    static_cast<void>(vnode);
    static_cast<void>(offset_bytes);
    static_cast<void>(source);
    static_cast<void>(length_bytes);
    written_bytes = OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE;
    return Status::Unsupported;
}

Status ConsoleDeviceFileSystem::TruncateOperation(
    void *const context, const Vnode &vnode, const uint64_t size_bytes) noexcept {
    static_cast<void>(context);
    static_cast<void>(vnode);
    static_cast<void>(size_bytes);
    return Status::Unsupported;
}

Status ConsoleDeviceFileSystem::ReadDirectoryOperation(
    void *const context, const Vnode &directory, uint64_t &cursor,
    DirectoryEntry &entry, bool &end_of_directory) noexcept {
    entry = DirectoryEntry{};
    end_of_directory = false;
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    ConsoleDeviceFileSystem &file_system =
        *static_cast<ConsoleDeviceFileSystem *>(context);
    if (!file_system.VnodeIsValid(directory)) {
        return Status::InvalidHandle;
    }
    if (directory.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if (cursor != OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE) {
        end_of_directory = true;
        return Status::Succeeded;
    }
    entry = DirectoryEntry{
        .node_identifier = OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NODE_IDENTIFIER,
        .type = NodeType::CharacterDevice,
        .name_length_bytes = OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME_LENGTH_BYTES,
        .name = {},
    };
    CopyName(entry.name);
    cursor += OS_KERNEL_CONSOLE_DEVICE_FS_COUNTER_INCREMENT;
    SpinLockGuard guard{file_system.lock_};
    ++file_system.statistics_.directory_read_count;
    return Status::Succeeded;
}

Status ConsoleDeviceFileSystem::GetNameOperation(
    void *const context, const Vnode &vnode, uint8_t *const name,
    const uint64_t name_capacity_bytes, uint64_t &name_length_bytes) noexcept {
    name_length_bytes = OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE;
    if (context == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    ConsoleDeviceFileSystem &file_system =
        *static_cast<ConsoleDeviceFileSystem *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    if (vnode.type == NodeType::Directory) {
        return Status::Succeeded;
    }
    if (name_capacity_bytes < OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME_LENGTH_BYTES) {
        return Status::NameTooLong;
    }
    CopyName(name);
    name_length_bytes = OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NAME_LENGTH_BYTES;
    return Status::Succeeded;
}

Status ConsoleDeviceFileSystem::StatOperation(
    void *const context, const Vnode &vnode,
    BackendNodeInformation &information) noexcept {
    information = BackendNodeInformation{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const ConsoleDeviceFileSystem &file_system =
        *static_cast<const ConsoleDeviceFileSystem *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    information = BackendNodeInformation{
        .size_bytes = OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE,
        .allocated_size_bytes = OS_KERNEL_CONSOLE_DEVICE_FS_EMPTY_VALUE,
        .link_count = vnode.type == NodeType::Directory
                          ? OS_KERNEL_CONSOLE_DEVICE_FS_ROOT_LINK_COUNT
                          : OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_LINK_COUNT,
    };
    return Status::Succeeded;
}

Status ConsoleDeviceFileSystem::SyncOperation(void *const context) noexcept {
    return context == nullptr ? Status::InvalidArgument : Status::Succeeded;
}

Status ConsoleDeviceFileSystem::ValidateOperation(void *const context) noexcept {
    return context == nullptr
               ? Status::InvalidArgument
               : static_cast<ConsoleDeviceFileSystem *>(context)->Validate();
}

Status ConsoleDeviceFileSystem::ReadResourceUsageOperation(
    void *const context, ResourceUsage &usage) noexcept {
    usage = ResourceUsage{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const ConsoleDeviceFileSystem &file_system =
        *static_cast<const ConsoleDeviceFileSystem *>(context);
    if (file_system.Validate() != Status::Succeeded) {
        return Status::Corrupt;
    }
    usage.vnode_count = OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NODE_IDENTIFIER;
    return Status::Succeeded;
}

Vnode ConsoleDeviceFileSystem::MakeRootVnode() noexcept {
    return Vnode{
        .superblock = &this->superblock_,
        .identifier = OS_KERNEL_CONSOLE_DEVICE_FS_ROOT_NODE_IDENTIFIER,
        .generation = OS_KERNEL_CONSOLE_DEVICE_FS_INITIAL_GENERATION,
        .type = NodeType::Directory,
    };
}

Vnode ConsoleDeviceFileSystem::MakeConsoleVnode() noexcept {
    return Vnode{
        .superblock = &this->superblock_,
        .identifier = OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NODE_IDENTIFIER,
        .generation = OS_KERNEL_CONSOLE_DEVICE_FS_INITIAL_GENERATION,
        .type = NodeType::CharacterDevice,
    };
}

bool ConsoleDeviceFileSystem::VnodeIsValid(const Vnode &vnode) const noexcept {
    if (!this->initialized_ || vnode.superblock != &this->superblock_ ||
        vnode.generation != OS_KERNEL_CONSOLE_DEVICE_FS_INITIAL_GENERATION) {
        return false;
    }
    return (vnode.identifier == OS_KERNEL_CONSOLE_DEVICE_FS_ROOT_NODE_IDENTIFIER &&
            vnode.type == NodeType::Directory) ||
           (vnode.identifier == OS_KERNEL_CONSOLE_DEVICE_FS_CONSOLE_NODE_IDENTIFIER &&
            vnode.type == NodeType::CharacterDevice);
}

}
