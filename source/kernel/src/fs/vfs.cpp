#include "os/kernel/fs/vfs.hpp"

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_VFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_VFS_FIRST_INDEX = 0ULL;
constexpr uint64_t OS_KERNEL_VFS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER = 0ULL;
constexpr uint64_t OS_KERNEL_VFS_ROOT_PATH_LENGTH_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_VFS_MAXIMUM_TRAVERSAL_COUNT = OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES;
constexpr uint8_t OS_KERNEL_VFS_PATH_SEPARATOR = static_cast<uint8_t>('/');
constexpr uint8_t OS_KERNEL_VFS_DOT_CHARACTER = static_cast<uint8_t>('.');
constexpr uint8_t OS_KERNEL_VFS_DELETE_CONTROL_CHARACTER = 0x7FU;
constexpr uint8_t OS_KERNEL_VFS_MAXIMUM_CONTROL_CHARACTER = 0x1FU;

[[nodiscard]] bool IsDot(const uint8_t *const name, const uint64_t name_length_bytes) noexcept {
    return name_length_bytes == OS_KERNEL_VFS_COUNTER_INCREMENT &&
           name[OS_KERNEL_VFS_FIRST_INDEX] == OS_KERNEL_VFS_DOT_CHARACTER;
}

[[nodiscard]] bool IsDotDot(const uint8_t *const name, const uint64_t name_length_bytes) noexcept {
    return name_length_bytes == OS_KERNEL_VFS_COUNTER_INCREMENT + OS_KERNEL_VFS_COUNTER_INCREMENT &&
           name[OS_KERNEL_VFS_FIRST_INDEX] == OS_KERNEL_VFS_DOT_CHARACTER &&
           name[OS_KERNEL_VFS_COUNTER_INCREMENT] == OS_KERNEL_VFS_DOT_CHARACTER;
}

[[nodiscard]] bool NameByteIsValid(const uint8_t value) noexcept {
    return value > OS_KERNEL_VFS_MAXIMUM_CONTROL_CHARACTER &&
           value != OS_KERNEL_VFS_DELETE_CONTROL_CHARACTER && value != OS_KERNEL_VFS_PATH_SEPARATOR;
}

[[nodiscard]] bool PathRequestsDirectory(const uint8_t *const path,
                                         const uint64_t path_length_bytes) noexcept {
    return path != nullptr && path_length_bytes != OS_KERNEL_VFS_EMPTY_VALUE &&
           path[path_length_bytes - OS_KERNEL_VFS_COUNTER_INCREMENT] ==
               OS_KERNEL_VFS_PATH_SEPARATOR;
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_VFS_FIRST_INDEX; byte_index < length_bytes; ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] bool TryAdd(const uint64_t left, const uint64_t right, uint64_t &sum) noexcept {
    if (left > UINT64_MAX - right) {
        return false;
    }
    sum = left + right;
    return true;
}

}

Status Vfs::Initialize(Mount *const mount_storage, const uint64_t mount_capacity,
                       Superblock &root_superblock) noexcept {
    if (this->initialized_) {
        return Status::AlreadyInitialized;
    }
    if (mount_storage == nullptr || mount_capacity == OS_KERNEL_VFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    const Status superblock_status = this->ValidateSuperblock(root_superblock);
    if (superblock_status != Status::Succeeded) {
        return superblock_status;
    }
    for (uint64_t mount_index = OS_KERNEL_VFS_FIRST_INDEX; mount_index < mount_capacity;
         ++mount_index) {
        mount_storage[mount_index] = Mount{};
    }
    this->mounts_ = mount_storage;
    this->mount_capacity_ = mount_capacity;
    this->mount_count_ = OS_KERNEL_VFS_COUNTER_INCREMENT;
    this->lock_ = SpinLock{};
    this->statistics_ = Statistics{
        .mount_count = OS_KERNEL_VFS_COUNTER_INCREMENT,
        .path_resolution_count = OS_KERNEL_VFS_EMPTY_VALUE,
        .failed_path_resolution_count = OS_KERNEL_VFS_EMPTY_VALUE,
        .component_lookup_count = OS_KERNEL_VFS_EMPTY_VALUE,
        .mount_transition_count = OS_KERNEL_VFS_EMPTY_VALUE,
        .root_clamp_count = OS_KERNEL_VFS_EMPTY_VALUE,
        .opened_file_count = OS_KERNEL_VFS_EMPTY_VALUE,
        .opened_directory_count = OS_KERNEL_VFS_EMPTY_VALUE,
        .bytes_read = OS_KERNEL_VFS_EMPTY_VALUE,
        .bytes_written = OS_KERNEL_VFS_EMPTY_VALUE,
    };
    this->mounts_[OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER] = Mount{
        .identifier = OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER,
        .parent_mount_identifier = OS_KERNEL_VFS_INVALID_MOUNT_IDENTIFIER,
        .mount_point =
            Path{
                .mount_identifier = OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER,
                .vnode = root_superblock.root,
            },
        .superblock = &root_superblock,
        .active = true,
    };
    this->initialized_ = true;
    return Status::Succeeded;
}

Status Vfs::InitializeContext(FsContext &context) const noexcept {
    context = FsContext{};
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    const Path root{
        .mount_identifier = OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER,
        .vnode = this->mounts_[OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER].superblock->root,
    };
    Superblock *const superblock = root.vnode.superblock;
    Status status = superblock->operations->open(superblock->backend_context, root.vnode);
    if (status != Status::Succeeded) {
        return status;
    }
    status = superblock->operations->open(superblock->backend_context, root.vnode);
    if (status != Status::Succeeded) {
        static_cast<void>(superblock->operations->close(superblock->backend_context, root.vnode));
        return status;
    }
    context = FsContext{
        .root = root,
        .current_working_directory = root,
        .initialized = true,
    };
    return Status::Succeeded;
}

Status Vfs::CloneContext(const FsContext &source,
                         FsContext &context) const noexcept {
    context = FsContext{};
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!source.initialized || !this->PathIsValid(source.root) ||
        !this->PathIsValid(source.current_working_directory)) {
        return Status::InvalidArgument;
    }
    Superblock *const root_superblock = source.root.vnode.superblock;
    Status status = root_superblock->operations->open(
        root_superblock->backend_context, source.root.vnode);
    if (status != Status::Succeeded) {
        return status;
    }
    Superblock *const working_superblock =
        source.current_working_directory.vnode.superblock;
    status = working_superblock->operations->open(
        working_superblock->backend_context,
        source.current_working_directory.vnode);
    if (status != Status::Succeeded) {
        static_cast<void>(root_superblock->operations->close(
            root_superblock->backend_context, source.root.vnode));
        return status;
    }
    context = FsContext{
        .root = source.root,
        .current_working_directory = source.current_working_directory,
        .initialized = true,
    };
    return Status::Succeeded;
}

Status Vfs::ReleaseContext(FsContext &context) const noexcept {
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!context.initialized || !this->PathIsValid(context.root) ||
        !this->PathIsValid(context.current_working_directory)) {
        return Status::InvalidArgument;
    }
    Superblock *const working_superblock = context.current_working_directory.vnode.superblock;
    const Status working_status = working_superblock->operations->close(
        working_superblock->backend_context, context.current_working_directory.vnode);
    Superblock *const root_superblock = context.root.vnode.superblock;
    const Status root_status =
        root_superblock->operations->close(root_superblock->backend_context, context.root.vnode);
    if (working_status != Status::Succeeded || root_status != Status::Succeeded) {
        return working_status != Status::Succeeded ? working_status : root_status;
    }
    context = FsContext{};
    return Status::Succeeded;
}

Status Vfs::MountAt(const FsContext &context, const uint8_t *const path,
                    const uint64_t path_length_bytes, Superblock &superblock) noexcept {
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    const Status superblock_status = this->ValidateSuperblock(superblock);
    if (superblock_status != Status::Succeeded) {
        return superblock_status;
    }
    Path mount_point{};
    const Status resolution_status = this->Resolve(context, path, path_length_bytes, mount_point);
    if (resolution_status != Status::Succeeded) {
        return resolution_status;
    }
    if (mount_point.vnode.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    // 当前版本不支持替换进程可见根；显式拒绝可避免“挂载成功但根路径未跟随”的歧义。
    if (this->PathsAreEqual(mount_point, context.root)) {
        return Status::MountPointBusy;
    }

    SpinLockGuard guard{this->lock_};
    for (uint64_t mount_index = OS_KERNEL_VFS_FIRST_INDEX; mount_index < this->mount_count_;
         ++mount_index) {
        const Mount &existing_mount = this->mounts_[mount_index];
        if (!existing_mount.active) {
            return Status::Corrupt;
        }
        if (existing_mount.superblock == &superblock) {
            return Status::AlreadyMounted;
        }
        if (mount_index != OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER &&
            existing_mount.identifier == mount_point.mount_identifier &&
            this->VnodesAreEqual(existing_mount.superblock->root, mount_point.vnode)) {
            return Status::MountPointBusy;
        }
        if (existing_mount.parent_mount_identifier == mount_point.mount_identifier &&
            this->VnodesAreEqual(existing_mount.mount_point.vnode, mount_point.vnode)) {
            return Status::MountPointBusy;
        }
    }
    if (this->mount_count_ >= this->mount_capacity_) {
        return Status::MountCapacityExhausted;
    }

    const uint64_t mount_identifier = this->mount_count_;
    this->mounts_[mount_identifier] = Mount{
        .identifier = mount_identifier,
        .parent_mount_identifier = mount_point.mount_identifier,
        .mount_point = mount_point,
        .superblock = &superblock,
        .active = true,
    };
    ++this->mount_count_;
    this->statistics_.mount_count = this->mount_count_;
    return Status::Succeeded;
}

Status Vfs::Resolve(const FsContext &context, const uint8_t *const path,
                    const uint64_t path_length_bytes, Path &resolved_path) noexcept {
    resolved_path = Path{};
    if (!this->IsInitialized()) {
        this->RecordResolution(Status::NotInitialized);
        return Status::NotInitialized;
    }
    if (!context.initialized || !this->PathIsValid(context.root) ||
        !this->PathIsValid(context.current_working_directory)) {
        this->RecordResolution(Status::InvalidArgument);
        return Status::InvalidArgument;
    }
    if (path == nullptr || path_length_bytes == OS_KERNEL_VFS_EMPTY_VALUE) {
        this->RecordResolution(Status::InvalidPath);
        return Status::InvalidPath;
    }
    if (path_length_bytes > OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES) {
        this->RecordResolution(Status::PathTooLong);
        return Status::PathTooLong;
    }

    Path current = path[OS_KERNEL_VFS_FIRST_INDEX] == OS_KERNEL_VFS_PATH_SEPARATOR
                       ? context.root
                       : context.current_working_directory;
    uint64_t byte_index = path[OS_KERNEL_VFS_FIRST_INDEX] == OS_KERNEL_VFS_PATH_SEPARATOR
                              ? OS_KERNEL_VFS_COUNTER_INCREMENT
                              : OS_KERNEL_VFS_FIRST_INDEX;
    uint64_t traversal_count = OS_KERNEL_VFS_EMPTY_VALUE;
    while (byte_index < path_length_bytes) {
        while (byte_index < path_length_bytes && path[byte_index] == OS_KERNEL_VFS_PATH_SEPARATOR) {
            ++byte_index;
        }
        if (byte_index == path_length_bytes) {
            break;
        }
        if (traversal_count >= OS_KERNEL_VFS_MAXIMUM_TRAVERSAL_COUNT) {
            this->RecordResolution(Status::LoopDetected);
            return Status::LoopDetected;
        }
        ++traversal_count;

        uint8_t name[OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES]{};
        uint64_t name_length_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
        while (byte_index < path_length_bytes && path[byte_index] != OS_KERNEL_VFS_PATH_SEPARATOR) {
            if (!NameByteIsValid(path[byte_index])) {
                this->RecordResolution(Status::InvalidPath);
                return Status::InvalidPath;
            }
            if (name_length_bytes >= OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES) {
                this->RecordResolution(Status::NameTooLong);
                return Status::NameTooLong;
            }
            name[name_length_bytes] = path[byte_index];
            ++name_length_bytes;
            ++byte_index;
        }

        if (IsDot(name, name_length_bytes)) {
            continue;
        }
        if (IsDotDot(name, name_length_bytes)) {
            const Status parent_status = this->MoveToParent(context, current);
            if (parent_status != Status::Succeeded) {
                this->RecordResolution(parent_status);
                return parent_status;
            }
            continue;
        }
        if (current.vnode.type != NodeType::Directory) {
            this->RecordResolution(Status::NotDirectory);
            return Status::NotDirectory;
        }
        Superblock *const superblock = current.vnode.superblock;
        if (name_length_bytes > superblock->maximum_name_length_bytes) {
            this->RecordResolution(Status::NameTooLong);
            return Status::NameTooLong;
        }
        Vnode child{};
        const Status lookup_status = superblock->operations->lookup(
            superblock->backend_context, current.vnode, name, name_length_bytes, child);
        {
            SpinLockGuard guard{this->lock_};
            ++this->statistics_.component_lookup_count;
        }
        if (lookup_status != Status::Succeeded) {
            this->RecordResolution(lookup_status);
            return lookup_status;
        }
        current.vnode = child;
        const Status mount_status = this->FollowMounts(current);
        if (mount_status != Status::Succeeded) {
            this->RecordResolution(mount_status);
            return mount_status;
        }
    }
    // 尾部分隔符表达“最终对象必须是目录”，不能把 /file/ 静默当成 /file。
    if (PathRequestsDirectory(path, path_length_bytes) &&
        current.vnode.type != NodeType::Directory) {
        this->RecordResolution(Status::NotDirectory);
        return Status::NotDirectory;
    }
    resolved_path = current;
    this->RecordResolution(Status::Succeeded);
    return Status::Succeeded;
}

Status Vfs::CreateDirectory(const FsContext &context, const uint8_t *const path,
                            const uint64_t path_length_bytes) noexcept {
    ParentResolution resolution{};
    const Status resolution_status =
        this->ResolveParent(context, path, path_length_bytes, resolution);
    if (resolution_status != Status::Succeeded) {
        return resolution_status;
    }
    Superblock *const superblock = resolution.parent.vnode.superblock;
    if (superblock->read_only) {
        return Status::ReadOnly;
    }
    Vnode created{};
    return superblock->operations->create(superblock->backend_context, resolution.parent.vnode,
                                          resolution.name, resolution.name_length_bytes,
                                          NodeType::Directory, created);
}

Status Vfs::RemoveFile(const FsContext &context, const uint8_t *const path,
                       const uint64_t path_length_bytes) noexcept {
    return this->Remove(context, path, path_length_bytes, NodeType::RegularFile);
}

Status Vfs::RemoveDirectory(const FsContext &context, const uint8_t *const path,
                            const uint64_t path_length_bytes) noexcept {
    return this->Remove(context, path, path_length_bytes, NodeType::Directory);
}

Status Vfs::Rename(const FsContext &context, const uint8_t *const source_path,
                   const uint64_t source_path_length_bytes, const uint8_t *const destination_path,
                   const uint64_t destination_path_length_bytes, const bool replace) noexcept {
    Path source{};
    const Status source_status =
        this->Resolve(context, source_path, source_path_length_bytes, source);
    if (source_status != Status::Succeeded) {
        return source_status;
    }
    if (this->PathsAreEqual(source, context.root) ||
        (source.vnode.type == NodeType::Directory &&
         this->PathsAreEqual(source, context.current_working_directory)) ||
        this->FindChildMount(source) != nullptr) {
        return Status::Busy;
    }

    ParentResolution source_parent{};
    Status status =
        this->ResolveParent(context, source_path, source_path_length_bytes, source_parent);
    if (status != Status::Succeeded) {
        return status;
    }
    ParentResolution destination_parent{};
    status = this->ResolveParent(context, destination_path, destination_path_length_bytes,
                                 destination_parent);
    if (status != Status::Succeeded) {
        return status;
    }
    if (source.mount_identifier != source_parent.parent.mount_identifier) {
        return Status::MountPointBusy;
    }
    if (source_parent.parent.vnode.superblock != destination_parent.parent.vnode.superblock) {
        return Status::CrossDevice;
    }
    Superblock *const superblock = source_parent.parent.vnode.superblock;
    if (superblock->read_only) {
        return Status::ReadOnly;
    }

    Path destination{};
    const Status destination_status =
        this->Resolve(context, destination_path, destination_path_length_bytes, destination);
    if (destination_status == Status::Succeeded) {
        if (destination.mount_identifier != destination_parent.parent.mount_identifier) {
            return Status::MountPointBusy;
        }
        if (this->PathsAreEqual(destination, context.root) ||
            (destination.vnode.type == NodeType::Directory &&
             this->PathsAreEqual(destination, context.current_working_directory)) ||
            this->FindChildMount(destination) != nullptr) {
            return Status::Busy;
        }
    } else if (destination_status != Status::NotFound) {
        return destination_status;
    }
    return superblock->operations->rename(superblock->backend_context, source_parent.parent.vnode,
                                          source_parent.name, source_parent.name_length_bytes,
                                          destination_parent.parent.vnode, destination_parent.name,
                                          destination_parent.name_length_bytes, replace);
}

Status Vfs::Truncate(const FsContext &context, const uint8_t *const path,
                     const uint64_t path_length_bytes, const uint64_t size_bytes) noexcept {
    Path resolved{};
    const Status status = this->Resolve(context, path, path_length_bytes, resolved);
    if (status != Status::Succeeded) {
        return status;
    }
    if (resolved.vnode.type == NodeType::Directory) {
        return Status::IsDirectory;
    }
    if (resolved.vnode.type != NodeType::RegularFile) {
        return resolved.vnode.type == NodeType::CharacterDevice ? Status::Unsupported
                                                               : Status::Corrupt;
    }
    Superblock *const superblock = resolved.vnode.superblock;
    if (superblock->read_only) {
        return Status::ReadOnly;
    }
    return superblock->operations->truncate(superblock->backend_context, resolved.vnode,
                                            size_bytes);
}

Status Vfs::Stat(const FsContext &context, const uint8_t *const path,
                 const uint64_t path_length_bytes, NodeInformation &information) noexcept {
    information = NodeInformation{};
    Path resolved{};
    const Status status = this->Resolve(context, path, path_length_bytes, resolved);
    if (status != Status::Succeeded) {
        return status;
    }
    BackendNodeInformation backend_information{};
    const Status stat_status = resolved.vnode.superblock->operations->stat(
        resolved.vnode.superblock->backend_context, resolved.vnode, backend_information);
    if (stat_status != Status::Succeeded) {
        return stat_status;
    }
    information = NodeInformation{
        .mount_identifier = resolved.mount_identifier,
        .superblock_identifier = resolved.vnode.superblock->identifier,
        .superblock_generation =
            resolved.vnode.superblock->generation,
        .node_identifier = resolved.vnode.identifier,
        .generation = resolved.vnode.generation,
        .type = resolved.vnode.type,
        .size_bytes = backend_information.size_bytes,
        .allocated_size_bytes = backend_information.allocated_size_bytes,
        .link_count = backend_information.link_count,
    };
    return Status::Succeeded;
}

Status Vfs::Open(const FsContext &context, const uint8_t *const path,
                 const uint64_t path_length_bytes, const OpenOptions &options,
                 OpenFile &open_file) noexcept {
    open_file = OpenFile{};
    if ((!options.readable && !options.writable) || (options.truncate && !options.writable)) {
        return Status::InvalidArgument;
    }

    Path resolved{};
    Status status = this->Resolve(context, path, path_length_bytes, resolved);
    if (status == Status::NotFound && options.create) {
        // 创建普通文件时保留尾部分隔符语义，避免先查找失败后误建同名文件。
        if (PathRequestsDirectory(path, path_length_bytes)) {
            return Status::NotDirectory;
        }
        ParentResolution parent_resolution{};
        status = this->ResolveParent(context, path, path_length_bytes, parent_resolution);
        if (status != Status::Succeeded) {
            return status;
        }
        Superblock *const parent_superblock = parent_resolution.parent.vnode.superblock;
        if (parent_superblock->read_only) {
            return Status::ReadOnly;
        }
        Vnode created{};
        status = parent_superblock->operations->create(
            parent_superblock->backend_context, parent_resolution.parent.vnode,
            parent_resolution.name, parent_resolution.name_length_bytes, NodeType::RegularFile,
            created);
        if (status != Status::Succeeded) {
            return status;
        }
        resolved = Path{
            .mount_identifier = parent_resolution.parent.mount_identifier,
            .vnode = created,
        };
    }
    if (status != Status::Succeeded) {
        return status;
    }
    if (resolved.vnode.type == NodeType::Directory) {
        return Status::IsDirectory;
    }
    if (resolved.vnode.type != NodeType::RegularFile &&
        resolved.vnode.type != NodeType::CharacterDevice) {
        return Status::Corrupt;
    }
    Superblock *const superblock = resolved.vnode.superblock;
    if (options.truncate && resolved.vnode.type != NodeType::RegularFile) {
        return Status::Unsupported;
    }
    if ((options.writable || options.truncate) && superblock->read_only &&
        resolved.vnode.type != NodeType::CharacterDevice) {
        return Status::ReadOnly;
    }
    if (options.truncate) {
        status = superblock->operations->truncate(superblock->backend_context, resolved.vnode,
                                                  OS_KERNEL_VFS_EMPTY_VALUE);
        if (status != Status::Succeeded) {
            return status;
        }
    }
    status = superblock->operations->open(superblock->backend_context, resolved.vnode);
    if (status != Status::Succeeded) {
        return status;
    }
    open_file = OpenFile{
        .path = resolved,
        .offset_bytes = OS_KERNEL_VFS_EMPTY_VALUE,
        .readable = options.readable,
        .writable = options.writable,
        .open = true,
    };
    {
        SpinLockGuard guard{this->lock_};
        ++this->statistics_.opened_file_count;
    }
    return Status::Succeeded;
}

Status Vfs::OpenDirectory(const FsContext &context, const uint8_t *const path,
                          const uint64_t path_length_bytes, OpenFile &open_file) noexcept {
    open_file = OpenFile{};
    Path resolved{};
    const Status status = this->Resolve(context, path, path_length_bytes, resolved);
    if (status != Status::Succeeded) {
        return status;
    }
    if (resolved.vnode.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    Superblock *const superblock = resolved.vnode.superblock;
    const Status open_status =
        superblock->operations->open(superblock->backend_context, resolved.vnode);
    if (open_status != Status::Succeeded) {
        return open_status;
    }
    open_file = OpenFile{
        .path = resolved,
        .offset_bytes = OS_KERNEL_VFS_EMPTY_VALUE,
        .readable = true,
        .writable = false,
        .open = true,
    };
    {
        SpinLockGuard guard{this->lock_};
        ++this->statistics_.opened_directory_count;
    }
    return Status::Succeeded;
}

Status Vfs::RetainOpenFile(const OpenFile &source,
                           OpenFile &retained_file) noexcept {
    retained_file = OpenFile{};
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!source.open || !this->PathIsValid(source.path) ||
        (source.path.vnode.type != NodeType::RegularFile &&
         source.path.vnode.type != NodeType::Directory &&
         source.path.vnode.type != NodeType::CharacterDevice)) {
        return Status::InvalidHandle;
    }
    Superblock *const superblock = source.path.vnode.superblock;
    const Status open_status = superblock->operations->open(
        superblock->backend_context, source.path.vnode);
    if (open_status != Status::Succeeded) {
        return open_status;
    }
    retained_file = source;
    retained_file.offset_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
    return Status::Succeeded;
}

Status Vfs::StatOpenFile(const OpenFile &open_file,
                         NodeInformation &information) noexcept {
    information = NodeInformation{};
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!open_file.open || !this->PathIsValid(open_file.path)) {
        return Status::InvalidHandle;
    }
    BackendNodeInformation backend_information{};
    const Status stat_status =
        open_file.path.vnode.superblock->operations->stat(
            open_file.path.vnode.superblock->backend_context,
            open_file.path.vnode, backend_information);
    if (stat_status != Status::Succeeded) {
        return stat_status;
    }
    information = NodeInformation{
        .mount_identifier = open_file.path.mount_identifier,
        .superblock_identifier =
            open_file.path.vnode.superblock->identifier,
        .superblock_generation =
            open_file.path.vnode.superblock->generation,
        .node_identifier = open_file.path.vnode.identifier,
        .generation = open_file.path.vnode.generation,
        .type = open_file.path.vnode.type,
        .size_bytes = backend_information.size_bytes,
        .allocated_size_bytes = backend_information.allocated_size_bytes,
        .link_count = backend_information.link_count,
    };
    return Status::Succeeded;
}

Status Vfs::ReadAt(const OpenFile &open_file, const uint64_t offset_bytes,
                   uint8_t *const destination,
                   const uint64_t capacity_bytes,
                   uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!open_file.open || !this->PathIsValid(open_file.path) ||
        open_file.path.vnode.type != NodeType::RegularFile) {
        return Status::InvalidHandle;
    }
    if (!open_file.readable) {
        return Status::PermissionDenied;
    }
    if (destination == nullptr &&
        capacity_bytes != OS_KERNEL_VFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    Superblock *const superblock = open_file.path.vnode.superblock;
    const Status status = superblock->operations->read(
        superblock->backend_context, open_file.path.vnode, offset_bytes,
        destination, capacity_bytes, read_bytes);
    if (status == Status::Succeeded) {
        SpinLockGuard guard{this->lock_};
        this->statistics_.bytes_read += read_bytes;
    }
    return status;
}

Status Vfs::Read(OpenFile &open_file, uint8_t *const destination, const uint64_t capacity_bytes,
                 uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!open_file.open || !this->PathIsValid(open_file.path) ||
        open_file.path.vnode.type != NodeType::RegularFile) {
        return Status::InvalidHandle;
    }
    if (!open_file.readable) {
        return Status::PermissionDenied;
    }
    if (destination == nullptr && capacity_bytes != OS_KERNEL_VFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    Superblock *const superblock = open_file.path.vnode.superblock;
    const Status status = superblock->operations->read(superblock->backend_context,
                                                       open_file.path.vnode, open_file.offset_bytes,
                                                       destination, capacity_bytes, read_bytes);
    if (status == Status::Succeeded) {
        if (open_file.offset_bytes > UINT64_MAX - read_bytes) {
            return Status::Corrupt;
        }
        open_file.offset_bytes += read_bytes;
        SpinLockGuard guard{this->lock_};
        this->statistics_.bytes_read += read_bytes;
    }
    return status;
}

Status Vfs::Write(OpenFile &open_file, const uint8_t *const source, const uint64_t length_bytes,
                  uint64_t &written_bytes) noexcept {
    written_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!open_file.open || !this->PathIsValid(open_file.path) ||
        open_file.path.vnode.type != NodeType::RegularFile) {
        return Status::InvalidHandle;
    }
    if (!open_file.writable) {
        return Status::PermissionDenied;
    }
    if (source == nullptr && length_bytes != OS_KERNEL_VFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    Superblock *const superblock = open_file.path.vnode.superblock;
    if (superblock->read_only) {
        return Status::ReadOnly;
    }
    const Status status =
        superblock->operations->write(superblock->backend_context, open_file.path.vnode,
                                      open_file.offset_bytes, source, length_bytes, written_bytes);
    if (status == Status::Succeeded) {
        if (open_file.offset_bytes > UINT64_MAX - written_bytes) {
            return Status::Corrupt;
        }
        open_file.offset_bytes += written_bytes;
        SpinLockGuard guard{this->lock_};
        this->statistics_.bytes_written += written_bytes;
    }
    return status;
}

Status Vfs::ReadDirectory(OpenFile &open_file, DirectoryEntry &entry,
                          bool &end_of_directory) noexcept {
    entry = DirectoryEntry{};
    end_of_directory = false;
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!open_file.open || !this->PathIsValid(open_file.path) ||
        open_file.path.vnode.type != NodeType::Directory) {
        return Status::InvalidHandle;
    }
    if (!open_file.readable) {
        return Status::PermissionDenied;
    }
    Superblock *const superblock = open_file.path.vnode.superblock;
    return superblock->operations->read_directory(superblock->backend_context, open_file.path.vnode,
                                                  open_file.offset_bytes, entry, end_of_directory);
}

Status Vfs::Close(OpenFile &open_file) noexcept {
    if (!open_file.open || !this->PathIsValid(open_file.path)) {
        return Status::InvalidHandle;
    }
    Superblock *const superblock = open_file.path.vnode.superblock;
    const Status status =
        superblock->operations->close(superblock->backend_context, open_file.path.vnode);
    if (status != Status::Succeeded) {
        return status;
    }
    open_file = OpenFile{};
    return Status::Succeeded;
}

Status Vfs::ChangeDirectory(FsContext &context, const uint8_t *const path,
                            const uint64_t path_length_bytes) noexcept {
    Path resolved{};
    const Status status = this->Resolve(context, path, path_length_bytes, resolved);
    if (status != Status::Succeeded) {
        return status;
    }
    if (resolved.vnode.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    Superblock *const new_superblock = resolved.vnode.superblock;
    Status reference_status =
        new_superblock->operations->open(new_superblock->backend_context, resolved.vnode);
    if (reference_status != Status::Succeeded) {
        return reference_status;
    }
    Superblock *const old_superblock = context.current_working_directory.vnode.superblock;
    reference_status = old_superblock->operations->close(old_superblock->backend_context,
                                                         context.current_working_directory.vnode);
    if (reference_status != Status::Succeeded) {
        static_cast<void>(
            new_superblock->operations->close(new_superblock->backend_context, resolved.vnode));
        return reference_status;
    }
    context.current_working_directory = resolved;
    return Status::Succeeded;
}

Status Vfs::GetWorkingDirectory(const FsContext &context, uint8_t *const destination,
                                const uint64_t capacity_bytes,
                                uint64_t &path_length_bytes) noexcept {
    path_length_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (!context.initialized || !this->PathIsValid(context.root) ||
        !this->PathIsValid(context.current_working_directory) || destination == nullptr ||
        capacity_bytes == OS_KERNEL_VFS_EMPTY_VALUE) {
        return Status::InvalidArgument;
    }
    if (this->PathsAreEqual(context.root, context.current_working_directory)) {
        if (capacity_bytes < OS_KERNEL_VFS_ROOT_PATH_LENGTH_BYTES) {
            return Status::PathTooLong;
        }
        destination[OS_KERNEL_VFS_FIRST_INDEX] = OS_KERNEL_VFS_PATH_SEPARATOR;
        path_length_bytes = OS_KERNEL_VFS_ROOT_PATH_LENGTH_BYTES;
        return Status::Succeeded;
    }

    const uint64_t working_capacity_bytes = capacity_bytes < OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES
                                                ? capacity_bytes
                                                : OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES;
    // 先在内核临时缓冲中完成整条父链重建，任何失败都不得留下半条调用者输出。
    uint8_t working_path[OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES]{};
    uint64_t write_index = working_capacity_bytes;
    uint64_t traversal_count = OS_KERNEL_VFS_EMPTY_VALUE;
    Path current = context.current_working_directory;
    while (!this->PathsAreEqual(context.root, current)) {
        if (traversal_count >= OS_KERNEL_VFS_MAXIMUM_TRAVERSAL_COUNT) {
            return Status::LoopDetected;
        }
        ++traversal_count;
        uint8_t name[OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES]{};
        uint64_t name_length_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
        const Status name_status =
            this->ReadPathName(current, name, sizeof(name), name_length_bytes);
        if (name_status != Status::Succeeded) {
            return name_status;
        }
        if (name_length_bytes == OS_KERNEL_VFS_EMPTY_VALUE ||
            write_index < name_length_bytes + OS_KERNEL_VFS_COUNTER_INCREMENT) {
            return name_length_bytes == OS_KERNEL_VFS_EMPTY_VALUE ? Status::Corrupt
                                                                  : Status::PathTooLong;
        }
        write_index -= name_length_bytes;
        CopyBytes(working_path + write_index, name, name_length_bytes);
        --write_index;
        working_path[write_index] = OS_KERNEL_VFS_PATH_SEPARATOR;
        const Status parent_status = this->MoveToParent(context, current);
        if (parent_status != Status::Succeeded) {
            return parent_status;
        }
    }
    path_length_bytes = working_capacity_bytes - write_index;
    if (path_length_bytes > capacity_bytes) {
        path_length_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
        return Status::PathTooLong;
    }
    CopyBytes(destination, working_path + write_index, path_length_bytes);
    return Status::Succeeded;
}

Status Vfs::Sync() noexcept {
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    for (uint64_t mount_index = OS_KERNEL_VFS_FIRST_INDEX; mount_index < this->mount_count_;
         ++mount_index) {
        Mount &mount = this->mounts_[mount_index];
        if (!mount.active || mount.superblock == nullptr) {
            return Status::Corrupt;
        }
        const Status status = mount.superblock->operations->sync(mount.superblock->backend_context);
        if (status != Status::Succeeded) {
            return status;
        }
    }
    return Status::Succeeded;
}

Status Vfs::Validate() noexcept {
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (this->mount_count_ == OS_KERNEL_VFS_EMPTY_VALUE ||
        this->mount_count_ > this->mount_capacity_) {
        return Status::Corrupt;
    }
    for (uint64_t mount_index = OS_KERNEL_VFS_FIRST_INDEX; mount_index < this->mount_count_;
         ++mount_index) {
        const Mount &mount = this->mounts_[mount_index];
        if (!mount.active || mount.identifier != mount_index || mount.superblock == nullptr ||
            this->ValidateSuperblock(*mount.superblock) != Status::Succeeded) {
            return Status::Corrupt;
        }
        if (mount_index == OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER) {
            if (mount.parent_mount_identifier != OS_KERNEL_VFS_INVALID_MOUNT_IDENTIFIER ||
                mount.mount_point.mount_identifier != OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER ||
                !this->VnodesAreEqual(mount.mount_point.vnode, mount.superblock->root)) {
                return Status::Corrupt;
            }
        } else {
            const Mount *const parent_mount = this->FindMount(mount.parent_mount_identifier);
            if (parent_mount == nullptr ||
                mount.mount_point.mount_identifier != mount.parent_mount_identifier ||
                mount.mount_point.vnode.type != NodeType::Directory ||
                mount.mount_point.vnode.superblock != parent_mount->superblock) {
                return Status::Corrupt;
            }
        }
        for (uint64_t previous_index = OS_KERNEL_VFS_FIRST_INDEX; previous_index < mount_index;
             ++previous_index) {
            if (this->mounts_[previous_index].superblock == mount.superblock) {
                return Status::Corrupt;
            }
        }
        uint64_t ancestor_identifier = mount.identifier;
        uint64_t ancestor_count = OS_KERNEL_VFS_EMPTY_VALUE;
        while (ancestor_identifier != OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER) {
            if (ancestor_count >= this->mount_count_) {
                return Status::LoopDetected;
            }
            ++ancestor_count;
            const Mount *const ancestor = this->FindMount(ancestor_identifier);
            if (ancestor == nullptr) {
                return Status::Corrupt;
            }
            ancestor_identifier = ancestor->parent_mount_identifier;
        }
        const Status backend_status =
            mount.superblock->operations->validate(mount.superblock->backend_context);
        if (backend_status != Status::Succeeded) {
            return backend_status;
        }
    }
    const Statistics statistics = this->ReadStatistics();
    return statistics.mount_count == this->mount_count_ ? Status::Succeeded : Status::Corrupt;
}

Statistics Vfs::ReadStatistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

Status Vfs::ReadResourceUsage(ResourceUsage &usage) const noexcept {
    usage = ResourceUsage{};
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    for (uint64_t mount_index = OS_KERNEL_VFS_FIRST_INDEX; mount_index < this->mount_count_;
         ++mount_index) {
        const Mount &mount = this->mounts_[mount_index];
        if (!mount.active || mount.superblock == nullptr ||
            mount.superblock->operations == nullptr) {
            return Status::Corrupt;
        }
        ResourceUsage backend_usage{};
        const Status status = mount.superblock->operations->read_resource_usage(
            mount.superblock->backend_context, backend_usage);
        if (status != Status::Succeeded ||
            !TryAdd(usage.heap_consumed_bytes, backend_usage.heap_consumed_bytes,
                    usage.heap_consumed_bytes) ||
            !TryAdd(usage.heap_active_requested_bytes, backend_usage.heap_active_requested_bytes,
                    usage.heap_active_requested_bytes) ||
            !TryAdd(usage.heap_allocation_count, backend_usage.heap_allocation_count,
                    usage.heap_allocation_count) ||
            !TryAdd(usage.vnode_count, backend_usage.vnode_count, usage.vnode_count)) {
            usage = ResourceUsage{};
            return status == Status::Succeeded ? Status::Corrupt : status;
        }
    }
    return Status::Succeeded;
}

bool Vfs::IsInitialized() const noexcept {
    return this->initialized_ && this->mounts_ != nullptr &&
           this->mount_capacity_ != OS_KERNEL_VFS_EMPTY_VALUE &&
           this->mount_count_ != OS_KERNEL_VFS_EMPTY_VALUE;
}

bool Vfs::PathIsValid(const Path &path) const noexcept {
    const Mount *const mount = this->FindMount(path.mount_identifier);
    return mount != nullptr && path.vnode.superblock == mount->superblock &&
           path.vnode.identifier != OS_KERNEL_VFS_EMPTY_VALUE &&
           path.vnode.generation != OS_KERNEL_VFS_EMPTY_VALUE &&
           (path.vnode.type == NodeType::RegularFile || path.vnode.type == NodeType::Directory ||
            path.vnode.type == NodeType::CharacterDevice);
}

bool Vfs::PathsAreEqual(const Path &left, const Path &right) const noexcept {
    return left.mount_identifier == right.mount_identifier &&
           this->VnodesAreEqual(left.vnode, right.vnode);
}

bool Vfs::VnodesAreEqual(const Vnode &left, const Vnode &right) const noexcept {
    return left.superblock == right.superblock && left.identifier == right.identifier &&
           left.generation == right.generation && left.type == right.type;
}

Mount *Vfs::FindMount(const uint64_t mount_identifier) noexcept {
    if (this->mounts_ == nullptr || mount_identifier >= this->mount_count_) {
        return nullptr;
    }
    Mount &mount = this->mounts_[mount_identifier];
    return mount.active && mount.identifier == mount_identifier ? &mount : nullptr;
}

const Mount *Vfs::FindMount(const uint64_t mount_identifier) const noexcept {
    if (this->mounts_ == nullptr || mount_identifier >= this->mount_count_) {
        return nullptr;
    }
    const Mount &mount = this->mounts_[mount_identifier];
    return mount.active && mount.identifier == mount_identifier ? &mount : nullptr;
}

Mount *Vfs::FindChildMount(const Path &mount_point) noexcept {
    for (uint64_t mount_index = OS_KERNEL_VFS_COUNTER_INCREMENT; mount_index < this->mount_count_;
         ++mount_index) {
        Mount &mount = this->mounts_[mount_index];
        if (mount.active && mount.parent_mount_identifier == mount_point.mount_identifier &&
            this->VnodesAreEqual(mount.mount_point.vnode, mount_point.vnode)) {
            return &mount;
        }
    }
    return nullptr;
}

Status Vfs::FollowMounts(Path &path) noexcept {
    for (uint64_t transition_count = OS_KERNEL_VFS_EMPTY_VALUE;
         transition_count < this->mount_count_; ++transition_count) {
        Mount *const child_mount = this->FindChildMount(path);
        if (child_mount == nullptr) {
            return Status::Succeeded;
        }
        path = Path{
            .mount_identifier = child_mount->identifier,
            .vnode = child_mount->superblock->root,
        };
        SpinLockGuard guard{this->lock_};
        ++this->statistics_.mount_transition_count;
    }
    return Status::LoopDetected;
}

Status Vfs::MoveToParent(const FsContext &context, Path &path) noexcept {
    if (this->PathsAreEqual(context.root, path)) {
        SpinLockGuard guard{this->lock_};
        ++this->statistics_.root_clamp_count;
        return Status::Succeeded;
    }
    Mount *const mount = this->FindMount(path.mount_identifier);
    if (mount == nullptr) {
        return Status::Corrupt;
    }
    if (mount->identifier != OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER &&
        this->VnodesAreEqual(path.vnode, mount->superblock->root)) {
        const Mount *const parent_mount = this->FindMount(mount->parent_mount_identifier);
        if (parent_mount == nullptr) {
            return Status::Corrupt;
        }
        Vnode parent{};
        const Status status = mount->mount_point.vnode.superblock->operations->parent(
            mount->mount_point.vnode.superblock->backend_context, mount->mount_point.vnode, parent);
        if (status != Status::Succeeded) {
            return status;
        }
        path = Path{
            .mount_identifier = parent_mount->identifier,
            .vnode = parent,
        };
        return Status::Succeeded;
    }
    Vnode parent{};
    const Status status = path.vnode.superblock->operations->parent(
        path.vnode.superblock->backend_context, path.vnode, parent);
    if (status != Status::Succeeded) {
        return status;
    }
    path.vnode = parent;
    if (!this->PathIsValid(path)) {
        return Status::Corrupt;
    }
    return Status::Succeeded;
}

Status Vfs::ResolveParent(const FsContext &context, const uint8_t *const path,
                          const uint64_t path_length_bytes, ParentResolution &resolution) noexcept {
    resolution = ParentResolution{};
    if (!this->IsInitialized()) {
        return Status::NotInitialized;
    }
    if (path == nullptr || path_length_bytes == OS_KERNEL_VFS_EMPTY_VALUE) {
        return Status::InvalidPath;
    }
    if (path_length_bytes > OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES) {
        return Status::PathTooLong;
    }

    uint64_t final_end = path_length_bytes;
    while (final_end > OS_KERNEL_VFS_EMPTY_VALUE &&
           path[final_end - OS_KERNEL_VFS_COUNTER_INCREMENT] == OS_KERNEL_VFS_PATH_SEPARATOR) {
        --final_end;
    }
    if (final_end == OS_KERNEL_VFS_EMPTY_VALUE) {
        return Status::InvalidPath;
    }
    uint64_t final_begin = final_end;
    while (final_begin > OS_KERNEL_VFS_EMPTY_VALUE &&
           path[final_begin - OS_KERNEL_VFS_COUNTER_INCREMENT] != OS_KERNEL_VFS_PATH_SEPARATOR) {
        --final_begin;
    }
    const uint64_t name_length_bytes = final_end - final_begin;
    if (name_length_bytes == OS_KERNEL_VFS_EMPTY_VALUE) {
        return Status::InvalidPath;
    }
    if (name_length_bytes > OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES) {
        return Status::NameTooLong;
    }
    for (uint64_t name_index = OS_KERNEL_VFS_FIRST_INDEX; name_index < name_length_bytes;
         ++name_index) {
        if (!NameByteIsValid(path[final_begin + name_index])) {
            return Status::InvalidPath;
        }
        resolution.name[name_index] = path[final_begin + name_index];
    }
    if (IsDot(resolution.name, name_length_bytes) || IsDotDot(resolution.name, name_length_bytes)) {
        return Status::InvalidPath;
    }
    resolution.name_length_bytes = name_length_bytes;

    if (final_begin == OS_KERNEL_VFS_EMPTY_VALUE) {
        if (!context.initialized || !this->PathIsValid(context.current_working_directory)) {
            return Status::InvalidArgument;
        }
        resolution.parent = context.current_working_directory;
    } else {
        const Status status = this->Resolve(context, path, final_begin, resolution.parent);
        if (status != Status::Succeeded) {
            return status;
        }
    }
    if (resolution.parent.vnode.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if (name_length_bytes > resolution.parent.vnode.superblock->maximum_name_length_bytes) {
        return Status::NameTooLong;
    }
    return Status::Succeeded;
}

Status Vfs::Remove(const FsContext &context, const uint8_t *const path,
                   const uint64_t path_length_bytes, const NodeType expected_type) noexcept {
    if (expected_type != NodeType::RegularFile && expected_type != NodeType::Directory) {
        return Status::InvalidArgument;
    }
    Path resolved{};
    const Status resolution_status = this->Resolve(context, path, path_length_bytes, resolved);
    if (resolution_status != Status::Succeeded) {
        return resolution_status;
    }
    if (resolved.vnode.type != expected_type) {
        return expected_type == NodeType::Directory ? Status::NotDirectory : Status::IsDirectory;
    }
    if (this->PathsAreEqual(resolved, context.root) ||
        (expected_type == NodeType::Directory &&
         this->PathsAreEqual(resolved, context.current_working_directory))) {
        return Status::Busy;
    }
    ParentResolution parent{};
    const Status parent_status = this->ResolveParent(context, path, path_length_bytes, parent);
    if (parent_status != Status::Succeeded) {
        return parent_status;
    }
    // 解析最终组件时若进入了子挂载，不能删除被挂载覆盖的底层目录项。
    if (resolved.mount_identifier != parent.parent.mount_identifier ||
        this->FindChildMount(resolved) != nullptr) {
        return Status::MountPointBusy;
    }
    Superblock *const superblock = parent.parent.vnode.superblock;
    if (superblock->read_only) {
        return Status::ReadOnly;
    }
    return superblock->operations->remove(superblock->backend_context, parent.parent.vnode,
                                          parent.name, parent.name_length_bytes, expected_type);
}

Status Vfs::ReadPathName(const Path &path, uint8_t *const name, const uint64_t name_capacity_bytes,
                         uint64_t &name_length_bytes) noexcept {
    name_length_bytes = OS_KERNEL_VFS_EMPTY_VALUE;
    Mount *const mount = this->FindMount(path.mount_identifier);
    if (mount == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    if (mount->identifier != OS_KERNEL_VFS_ROOT_MOUNT_IDENTIFIER &&
        this->VnodesAreEqual(path.vnode, mount->superblock->root)) {
        return mount->mount_point.vnode.superblock->operations->get_name(
            mount->mount_point.vnode.superblock->backend_context, mount->mount_point.vnode, name,
            name_capacity_bytes, name_length_bytes);
    }
    return path.vnode.superblock->operations->get_name(path.vnode.superblock->backend_context,
                                                       path.vnode, name, name_capacity_bytes,
                                                       name_length_bytes);
}

Status Vfs::ValidateSuperblock(const Superblock &superblock) const noexcept {
    if (!superblock.initialized || superblock.backend_kind == BackendKind::None ||
        superblock.identifier == OS_KERNEL_VFS_EMPTY_VALUE ||
        superblock.generation == OS_KERNEL_VFS_EMPTY_VALUE ||
        superblock.root.superblock != &superblock ||
        superblock.root.identifier == OS_KERNEL_VFS_EMPTY_VALUE ||
        superblock.root.generation == OS_KERNEL_VFS_EMPTY_VALUE ||
        superblock.root.type != NodeType::Directory || superblock.operations == nullptr ||
        superblock.backend_context == nullptr ||
        superblock.maximum_name_length_bytes == OS_KERNEL_VFS_EMPTY_VALUE ||
        superblock.maximum_name_length_bytes > OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES) {
        return Status::InvalidArgument;
    }
    const BackendOperations &operations = *superblock.operations;
    return operations.lookup != nullptr && operations.create != nullptr &&
                   operations.open != nullptr && operations.close != nullptr &&
                   operations.remove != nullptr && operations.rename != nullptr &&
                   operations.parent != nullptr && operations.read != nullptr &&
                   operations.write != nullptr && operations.truncate != nullptr &&
                   operations.read_directory != nullptr && operations.get_name != nullptr &&
                   operations.stat != nullptr && operations.sync != nullptr &&
                   operations.validate != nullptr && operations.read_resource_usage != nullptr
               ? Status::Succeeded
               : Status::InvalidArgument;
}

void Vfs::RecordResolution(const Status status) noexcept {
    SpinLockGuard guard{this->lock_};
    ++this->statistics_.path_resolution_count;
    if (status != Status::Succeeded) {
        ++this->statistics_.failed_path_resolution_count;
    }
}

}
