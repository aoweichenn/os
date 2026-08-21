#include "os/kernel/fs/legacy_file_system.hpp"

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_LEGACY_FILE_SYSTEM_INITIAL_GENERATION = 1ULL;
constexpr uint8_t OS_KERNEL_LEGACY_FILE_SYSTEM_PATH_SEPARATOR = static_cast<uint8_t>('/');
constexpr uint8_t OS_KERNEL_LEGACY_FILE_SYSTEM_DOT_CHARACTER = static_cast<uint8_t>('.');
constexpr uint8_t OS_KERNEL_LEGACY_FILE_SYSTEM_MAXIMUM_CONTROL_CHARACTER = 0x1FU;
constexpr uint8_t OS_KERNEL_LEGACY_FILE_SYSTEM_DELETE_CONTROL_CHARACTER = 0x7FU;

[[nodiscard]] NodeType ToVfsNodeType(const FileSystemNodeType type) noexcept {
    if (type == FileSystemNodeType::RegularFile) {
        return NodeType::RegularFile;
    }
    if (type == FileSystemNodeType::Directory) {
        return NodeType::Directory;
    }
    return NodeType::None;
}

[[nodiscard]] FileSystemNodeType ToLegacyNodeType(const NodeType type) noexcept {
    if (type == NodeType::RegularFile) {
        return FileSystemNodeType::RegularFile;
    }
    if (type == NodeType::Directory) {
        return FileSystemNodeType::Directory;
    }
    return FileSystemNodeType::Unused;
}

[[nodiscard]] bool NameIsValid(const uint8_t *const name,
                               const uint64_t name_length_bytes) noexcept {
    if (name == nullptr || name_length_bytes == OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE ||
        name_length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE;
         byte_index < name_length_bytes; ++byte_index) {
        const uint8_t value = name[byte_index];
        if (value <= OS_KERNEL_LEGACY_FILE_SYSTEM_MAXIMUM_CONTROL_CHARACTER ||
            value == OS_KERNEL_LEGACY_FILE_SYSTEM_DELETE_CONTROL_CHARACTER ||
            value == OS_KERNEL_LEGACY_FILE_SYSTEM_PATH_SEPARATOR) {
            return false;
        }
    }
    const bool dot = name_length_bytes == OS_KERNEL_LEGACY_FILE_SYSTEM_INITIAL_GENERATION &&
                     name[OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE] ==
                         OS_KERNEL_LEGACY_FILE_SYSTEM_DOT_CHARACTER;
    const bool dot_dot = name_length_bytes == OS_KERNEL_LEGACY_FILE_SYSTEM_INITIAL_GENERATION +
                                                  OS_KERNEL_LEGACY_FILE_SYSTEM_INITIAL_GENERATION &&
                         name[OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE] ==
                             OS_KERNEL_LEGACY_FILE_SYSTEM_DOT_CHARACTER &&
                         name[OS_KERNEL_LEGACY_FILE_SYSTEM_INITIAL_GENERATION] ==
                             OS_KERNEL_LEGACY_FILE_SYSTEM_DOT_CHARACTER;
    return !dot && !dot_dot;
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

}

Status ToVfsStatus(const FileSystemStatus status) noexcept {
    if (status == FileSystemStatus::Succeeded) {
        return Status::Succeeded;
    }
    if (status == FileSystemStatus::NotInitialized) {
        return Status::NotInitialized;
    }
    if (status == FileSystemStatus::AlreadyInitialized) {
        return Status::AlreadyInitialized;
    }
    if (status == FileSystemStatus::InvalidArgument) {
        return Status::InvalidArgument;
    }
    if (status == FileSystemStatus::InvalidPath) {
        return Status::InvalidPath;
    }
    if (status == FileSystemStatus::PathTooLong) {
        return Status::PathTooLong;
    }
    if (status == FileSystemStatus::NameTooLong) {
        return Status::NameTooLong;
    }
    if (status == FileSystemStatus::NotFound) {
        return Status::NotFound;
    }
    if (status == FileSystemStatus::AlreadyExists) {
        return Status::AlreadyExists;
    }
    if (status == FileSystemStatus::NotDirectory) {
        return Status::NotDirectory;
    }
    if (status == FileSystemStatus::IsDirectory) {
        return Status::IsDirectory;
    }
    if (status == FileSystemStatus::PermissionDenied) {
        return Status::PermissionDenied;
    }
    if (status == FileSystemStatus::InvalidHandle) {
        return Status::InvalidHandle;
    }
    if (status == FileSystemStatus::InodeCapacityExhausted ||
        status == FileSystemStatus::DataCapacityExhausted ||
        status == FileSystemStatus::DirectoryCapacityExhausted ||
        status == FileSystemStatus::MountCapacityExhausted) {
        return status == FileSystemStatus::MountCapacityExhausted ? Status::MountCapacityExhausted
                                                                  : Status::CapacityExhausted;
    }
    if (status == FileSystemStatus::FileTooLarge) {
        return Status::FileTooLarge;
    }
    if (status == FileSystemStatus::Corrupt) {
        return Status::Corrupt;
    }
    if (status == FileSystemStatus::IncompleteTransaction) {
        return Status::IncompleteTransaction;
    }
    if (status == FileSystemStatus::DeviceFailure) {
        return Status::DeviceFailure;
    }
    if (status == FileSystemStatus::ReadOnly) {
        return Status::ReadOnly;
    }
    if (status == FileSystemStatus::LoopDetected) {
        return Status::LoopDetected;
    }
    if (status == FileSystemStatus::DirectoryNotEmpty) {
        return Status::DirectoryNotEmpty;
    }
    if (status == FileSystemStatus::CrossDevice) {
        return Status::CrossDevice;
    }
    if (status == FileSystemStatus::Busy) {
        return Status::Busy;
    }
    return Status::Unsupported;
}

FileSystemStatus ToFileSystemStatus(const Status status) noexcept {
    if (status == Status::Succeeded) {
        return FileSystemStatus::Succeeded;
    }
    if (status == Status::NotInitialized) {
        return FileSystemStatus::NotInitialized;
    }
    if (status == Status::AlreadyInitialized) {
        return FileSystemStatus::AlreadyInitialized;
    }
    if (status == Status::InvalidPath) {
        return FileSystemStatus::InvalidPath;
    }
    if (status == Status::PathTooLong) {
        return FileSystemStatus::PathTooLong;
    }
    if (status == Status::NameTooLong) {
        return FileSystemStatus::NameTooLong;
    }
    if (status == Status::NotFound) {
        return FileSystemStatus::NotFound;
    }
    if (status == Status::AlreadyExists) {
        return FileSystemStatus::AlreadyExists;
    }
    if (status == Status::NotDirectory) {
        return FileSystemStatus::NotDirectory;
    }
    if (status == Status::IsDirectory) {
        return FileSystemStatus::IsDirectory;
    }
    if (status == Status::PermissionDenied) {
        return FileSystemStatus::PermissionDenied;
    }
    if (status == Status::InvalidHandle) {
        return FileSystemStatus::InvalidHandle;
    }
    if (status == Status::CapacityExhausted) {
        return FileSystemStatus::DataCapacityExhausted;
    }
    if (status == Status::MountCapacityExhausted) {
        return FileSystemStatus::MountCapacityExhausted;
    }
    if (status == Status::FileTooLarge) {
        return FileSystemStatus::FileTooLarge;
    }
    if (status == Status::Corrupt) {
        return FileSystemStatus::Corrupt;
    }
    if (status == Status::IncompleteTransaction) {
        return FileSystemStatus::IncompleteTransaction;
    }
    if (status == Status::DeviceFailure) {
        return FileSystemStatus::DeviceFailure;
    }
    if (status == Status::ReadOnly) {
        return FileSystemStatus::ReadOnly;
    }
    if (status == Status::LoopDetected) {
        return FileSystemStatus::LoopDetected;
    }
    if (status == Status::DirectoryNotEmpty) {
        return FileSystemStatus::DirectoryNotEmpty;
    }
    if (status == Status::CrossDevice) {
        return FileSystemStatus::CrossDevice;
    }
    if (status == Status::Busy) {
        return FileSystemStatus::Busy;
    }
    if (status == Status::Unsupported) {
        return FileSystemStatus::Unsupported;
    }
    return FileSystemStatus::InvalidArgument;
}

const BackendOperations LegacyFileSystem::operations{
    .lookup = LegacyFileSystem::LookupOperation,
    .create = LegacyFileSystem::CreateOperation,
    .open = LegacyFileSystem::OpenOperation,
    .close = LegacyFileSystem::CloseOperation,
    .remove = LegacyFileSystem::RemoveOperation,
    .rename = LegacyFileSystem::RenameOperation,
    .link = nullptr,
    .create_symbolic_link = nullptr,
    .read_symbolic_link = nullptr,
    .parent = LegacyFileSystem::ParentOperation,
    .read = LegacyFileSystem::ReadOperation,
    .write = LegacyFileSystem::WriteOperation,
    .truncate = LegacyFileSystem::TruncateOperation,
    .read_directory = LegacyFileSystem::ReadDirectoryOperation,
    .get_name = LegacyFileSystem::GetNameOperation,
    .stat = LegacyFileSystem::StatOperation,
    .change_mode = nullptr,
    .change_owner = nullptr,
    .sync = LegacyFileSystem::SyncOperation,
    .validate = LegacyFileSystem::ValidateOperation,
    .read_resource_usage = LegacyFileSystem::ReadResourceUsageOperation,
};

Status LegacyFileSystem::Initialize(FileSystem &file_system, const uint64_t superblock_identifier,
                                    const bool read_only) noexcept {
    if (this->initialized_) {
        return Status::AlreadyInitialized;
    }
    if (superblock_identifier == OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    const FileSystemStatus consistency_status = file_system.CheckConsistency();
    if (consistency_status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(consistency_status);
    }
    this->file_system_ = &file_system;
    this->superblock_ = Superblock{
        .backend_kind = BackendKind::Legacy,
        .identifier = superblock_identifier,
        .generation = OS_KERNEL_LEGACY_FILE_SYSTEM_INITIAL_GENERATION,
        .root = {},
        .operations = &LegacyFileSystem::operations,
        .backend_context = this,
        .maximum_name_length_bytes = OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES,
        .cache_regular_file_data = true,
        .read_only = read_only,
        .initialized = true,
    };
    this->superblock_.root =
        this->MakeVnode(OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER, FileSystemNodeType::Directory);
    this->initialized_ = true;
    return Status::Succeeded;
}

Superblock &LegacyFileSystem::GetSuperblock() noexcept { return this->superblock_; }

const Superblock &LegacyFileSystem::GetSuperblock() const noexcept { return this->superblock_; }

Status LegacyFileSystem::LookupOperation(void *const context, const Vnode &directory,
                                         const uint8_t *const name,
                                         const uint64_t name_length_bytes, Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || !NameIsValid(name, name_length_bytes)) {
        return name_length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES
                   ? Status::NameTooLong
                   : Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (!adapter.VnodeIsValid(directory) || directory.type != NodeType::Directory) {
        return directory.type == NodeType::Directory ? Status::Corrupt : Status::NotDirectory;
    }
    FileSystem &file_system = *adapter.file_system_;
    SpinLockGuard guard{file_system.lock_};
    if (!file_system.initialized_) {
        return Status::NotInitialized;
    }
    FileSystemInode directory_inode{};
    FileSystemStatus status = file_system.ReadInode(directory.identifier, directory_inode);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    FileSystem::PathComponent component{};
    component.length_bytes = name_length_bytes;
    CopyBytes(component.bytes, name, name_length_bytes);
    FileSystem::DirectoryEntryLocation location{};
    status = file_system.FindDirectoryEntry(directory_inode, component, location);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    FileSystemInode child_inode{};
    status = file_system.ReadInode(location.entry.inode_number, child_inode);
    if (status != FileSystemStatus::Succeeded || child_inode.type != location.entry.type) {
        return status == FileSystemStatus::Succeeded ? Status::Corrupt : ToVfsStatus(status);
    }
    vnode = adapter.MakeVnode(location.entry.inode_number, child_inode.type);
    return Status::Succeeded;
}

Status LegacyFileSystem::CreateOperation(void *const context, const Vnode &directory,
                                         const uint8_t *const name,
                                         const uint64_t name_length_bytes, const NodeType type,
                                         const NodeCreationAttributes &attributes,
                                         Vnode &vnode) noexcept {
    static_cast<void>(attributes);
    vnode = Vnode{};
    if (context == nullptr || !NameIsValid(name, name_length_bytes) ||
        (type != NodeType::RegularFile && type != NodeType::Directory)) {
        return name_length_bytes > OS_KERNEL_FILE_SYSTEM_MAXIMUM_NAME_LENGTH_BYTES
                   ? Status::NameTooLong
                   : Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (adapter.superblock_.read_only) {
        return Status::ReadOnly;
    }
    if (!adapter.VnodeIsValid(directory) || directory.type != NodeType::Directory) {
        return directory.type == NodeType::Directory ? Status::Corrupt : Status::NotDirectory;
    }
    FileSystem &file_system = *adapter.file_system_;
    SpinLockGuard guard{file_system.lock_};
    if (!file_system.initialized_) {
        return Status::NotInitialized;
    }
    FileSystemInode directory_inode{};
    FileSystemStatus status = file_system.ReadInode(directory.identifier, directory_inode);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    FileSystem::PathComponent component{};
    component.length_bytes = name_length_bytes;
    CopyBytes(component.bytes, name, name_length_bytes);
    uint64_t inode_number = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE;
    const FileSystemNodeType legacy_type = ToLegacyNodeType(type);
    status = file_system.CreateChildNode(directory.identifier, directory_inode, component,
                                         legacy_type, inode_number);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    vnode = adapter.MakeVnode(inode_number, legacy_type);
    return Status::Succeeded;
}

Status LegacyFileSystem::OpenOperation(void *const context, const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const LegacyFileSystem &adapter = *static_cast<const LegacyFileSystem *>(context);
    return adapter.VnodeIsValid(vnode) ? Status::Succeeded : Status::InvalidHandle;
}

Status LegacyFileSystem::CloseOperation(void *const context, const Vnode &vnode) noexcept {
    return LegacyFileSystem::OpenOperation(context, vnode);
}

Status LegacyFileSystem::RemoveOperation(void *const context, const Vnode &directory,
                                         const uint8_t *const name,
                                         const uint64_t name_length_bytes,
                                         const NodeType expected_type) noexcept {
    static_cast<void>(directory);
    static_cast<void>(name);
    static_cast<void>(name_length_bytes);
    static_cast<void>(expected_type);
    return context == nullptr ? Status::InvalidArgument : Status::Unsupported;
}

Status LegacyFileSystem::RenameOperation(void *const context, const Vnode &source_directory,
                                         const uint8_t *const source_name,
                                         const uint64_t source_name_length_bytes,
                                         const Vnode &destination_directory,
                                         const uint8_t *const destination_name,
                                         const uint64_t destination_name_length_bytes,
                                         const bool replace) noexcept {
    static_cast<void>(source_directory);
    static_cast<void>(source_name);
    static_cast<void>(source_name_length_bytes);
    static_cast<void>(destination_directory);
    static_cast<void>(destination_name);
    static_cast<void>(destination_name_length_bytes);
    static_cast<void>(replace);
    return context == nullptr ? Status::InvalidArgument : Status::Unsupported;
}

Status LegacyFileSystem::ParentOperation(void *const context, const Vnode &vnode,
                                         Vnode &parent) noexcept {
    parent = Vnode{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (!adapter.VnodeIsValid(vnode)) {
        return Status::Corrupt;
    }
    FileSystem &file_system = *adapter.file_system_;
    SpinLockGuard guard{file_system.lock_};
    if (!file_system.initialized_) {
        return Status::NotInitialized;
    }
    uint64_t parent_inode_number = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE;
    FileSystemInode parent_inode{};
    FileSystem::PathComponent child_name{};
    const FileSystemStatus status =
        file_system.FindParentNode(vnode.identifier, parent_inode_number, parent_inode, child_name);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    parent = adapter.MakeVnode(parent_inode_number, parent_inode.type);
    return Status::Succeeded;
}

Status LegacyFileSystem::ReadOperation(void *const context, const Vnode &vnode,
                                       const uint64_t offset_bytes, uint8_t *const destination,
                                       const uint64_t capacity_bytes,
                                       uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE;
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (!adapter.VnodeIsValid(vnode) || vnode.type != NodeType::RegularFile) {
        return vnode.type == NodeType::Directory ? Status::IsDirectory : Status::Corrupt;
    }
    FileSystemHandle handle{
        .inode_number = vnode.identifier,
        .offset_bytes = offset_bytes,
        .node_type = FileSystemNodeType::RegularFile,
        .readable = true,
        .writable = false,
        .open = true,
    };
    return ToVfsStatus(adapter.file_system_->Read(handle, destination, capacity_bytes, read_bytes));
}

Status LegacyFileSystem::WriteOperation(void *const context, const Vnode &vnode,
                                        const uint64_t offset_bytes, const uint8_t *const source,
                                        const uint64_t length_bytes,
                                        uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE;
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (adapter.superblock_.read_only) {
        return Status::ReadOnly;
    }
    if (!adapter.VnodeIsValid(vnode) || vnode.type != NodeType::RegularFile) {
        return vnode.type == NodeType::Directory ? Status::IsDirectory : Status::Corrupt;
    }
    FileSystemHandle handle{
        .inode_number = vnode.identifier,
        .offset_bytes = offset_bytes,
        .node_type = FileSystemNodeType::RegularFile,
        .readable = false,
        .writable = true,
        .open = true,
    };
    return ToVfsStatus(adapter.file_system_->Write(handle, source, length_bytes, written_bytes));
}

Status LegacyFileSystem::TruncateOperation(void *const context, const Vnode &vnode,
                                           const uint64_t size_bytes) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (adapter.superblock_.read_only) {
        return Status::ReadOnly;
    }
    if (size_bytes != OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE) {
        return Status::Unsupported;
    }
    if (!adapter.VnodeIsValid(vnode) || vnode.type != NodeType::RegularFile) {
        return vnode.type == NodeType::Directory ? Status::IsDirectory : Status::Corrupt;
    }
    FileSystem &file_system = *adapter.file_system_;
    SpinLockGuard guard{file_system.lock_};
    if (!file_system.initialized_) {
        return Status::NotInitialized;
    }
    FileSystemInode inode{};
    FileSystemStatus status = file_system.ReadInode(vnode.identifier, inode);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    if (inode.type != FileSystemNodeType::RegularFile) {
        return Status::IsDirectory;
    }
    if (inode.size_bytes == OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE) {
        return Status::Succeeded;
    }
    status = file_system.BeginTransaction();
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    status = file_system.ReleaseInodeDataBlocks(inode);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    inode.generation = file_system.superblock_.transaction_generation;
    status = file_system.WriteInode(vnode.identifier, inode);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    return ToVfsStatus(file_system.CommitTransaction());
}

Status LegacyFileSystem::ReadDirectoryOperation(void *const context, const Vnode &directory,
                                                uint64_t &cursor, DirectoryEntry &entry,
                                                bool &end_of_directory) noexcept {
    entry = DirectoryEntry{};
    end_of_directory = false;
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (!adapter.VnodeIsValid(directory) || directory.type != NodeType::Directory) {
        return directory.type == NodeType::Directory ? Status::Corrupt : Status::NotDirectory;
    }
    FileSystemHandle handle{
        .inode_number = directory.identifier,
        .offset_bytes = cursor,
        .node_type = FileSystemNodeType::Directory,
        .readable = true,
        .writable = false,
        .open = true,
    };
    FileSystemDirectoryEntry legacy_entry{};
    const FileSystemStatus status =
        adapter.file_system_->ReadDirectory(handle, legacy_entry, end_of_directory);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    cursor = handle.offset_bytes;
    if (end_of_directory) {
        return Status::Succeeded;
    }
    entry = DirectoryEntry{
        .node_identifier = legacy_entry.inode_number,
        .type = ToVfsNodeType(legacy_entry.type),
        .name_length_bytes = legacy_entry.name_length_bytes,
        .name = {},
    };
    if (entry.type == NodeType::None) {
        return Status::Corrupt;
    }
    CopyBytes(entry.name, legacy_entry.name, legacy_entry.name_length_bytes);
    return Status::Succeeded;
}

Status LegacyFileSystem::GetNameOperation(void *const context, const Vnode &vnode,
                                          uint8_t *const name, const uint64_t name_capacity_bytes,
                                          uint64_t &name_length_bytes) noexcept {
    name_length_bytes = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE;
    if (context == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (!adapter.VnodeIsValid(vnode)) {
        return Status::Corrupt;
    }
    FileSystem &file_system = *adapter.file_system_;
    SpinLockGuard guard{file_system.lock_};
    if (!file_system.initialized_) {
        return Status::NotInitialized;
    }
    uint64_t parent_inode_number = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE;
    FileSystemInode parent_inode{};
    FileSystem::PathComponent child_name{};
    const FileSystemStatus status =
        file_system.FindParentNode(vnode.identifier, parent_inode_number, parent_inode, child_name);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    if (child_name.length_bytes > name_capacity_bytes) {
        return Status::NameTooLong;
    }
    CopyBytes(name, child_name.bytes, child_name.length_bytes);
    name_length_bytes = child_name.length_bytes;
    return Status::Succeeded;
}

Status LegacyFileSystem::StatOperation(void *const context, const Vnode &vnode,
                                       BackendNodeInformation &information) noexcept {
    information = BackendNodeInformation{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    if (!adapter.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    FileSystem &file_system = *adapter.file_system_;
    SpinLockGuard guard{file_system.lock_};
    FileSystemInode inode{};
    const FileSystemStatus status = file_system.ReadInode(vnode.identifier, inode);
    if (status != FileSystemStatus::Succeeded) {
        return ToVfsStatus(status);
    }
    if (ToVfsNodeType(inode.type) != vnode.type) {
        return Status::Corrupt;
    }
    information = BackendNodeInformation{
        .size_bytes = inode.size_bytes,
        .allocated_size_bytes =
            inode.allocated_block_count * OS_KERNEL_FILE_SYSTEM_BLOCK_SIZE_BYTES,
        .link_count = inode.link_count,
        .access_time_nanoseconds = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE,
        .modification_time_nanoseconds = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE,
        .change_time_nanoseconds = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE,
        .birth_time_nanoseconds = OS_KERNEL_LEGACY_FILE_SYSTEM_EMPTY_VALUE,
        .owner_user_identifier = os::abi::OS_ABI_ROOT_USER_IDENTIFIER,
        .owner_group_identifier = os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER,
        .mode = vnode.type == NodeType::Directory ? os::abi::OS_ABI_FILE_MODE_DIRECTORY | 0000777U
                                                  : os::abi::OS_ABI_FILE_MODE_REGULAR | 0000666U,
    };
    return Status::Succeeded;
}

Status LegacyFileSystem::SyncOperation(void *const context) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    return ToVfsStatus(adapter.file_system_->Sync());
}

Status LegacyFileSystem::ValidateOperation(void *const context) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    LegacyFileSystem &adapter = *static_cast<LegacyFileSystem *>(context);
    return ToVfsStatus(adapter.file_system_->CheckConsistency());
}

Status LegacyFileSystem::ReadResourceUsageOperation(void *const context,
                                                    ResourceUsage &usage) noexcept {
    usage = ResourceUsage{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const LegacyFileSystem &adapter = *static_cast<const LegacyFileSystem *>(context);
    return adapter.initialized_ && adapter.file_system_ != nullptr ? Status::Succeeded
                                                                   : Status::NotInitialized;
}

Vnode LegacyFileSystem::MakeVnode(const uint64_t inode_number,
                                  const FileSystemNodeType type) noexcept {
    return Vnode{
        .superblock = &this->superblock_,
        .identifier = inode_number,
        .generation = OS_KERNEL_LEGACY_FILE_SYSTEM_INITIAL_GENERATION,
        .type = ToVfsNodeType(type),
    };
}

bool LegacyFileSystem::VnodeIsValid(const Vnode &vnode) const noexcept {
    return this->initialized_ && this->file_system_ != nullptr &&
           vnode.superblock == &this->superblock_ &&
           vnode.identifier >= OS_KERNEL_FILE_SYSTEM_ROOT_INODE_NUMBER &&
           vnode.identifier < OS_KERNEL_FILE_SYSTEM_INODE_COUNT &&
           vnode.generation == OS_KERNEL_LEGACY_FILE_SYSTEM_INITIAL_GENERATION &&
           (vnode.type == NodeType::RegularFile || vnode.type == NodeType::Directory);
}

}
