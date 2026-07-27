#include "os/kernel/io/file_description.hpp"

#include "os/kernel/fs/legacy_file_system.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE = 0ULL;

struct FileDescriptionStorage final {
    FileDescriptionKind kind;
    uint64_t file_status_flags;
    ConsoleInput *console_input;
    FileDescriptionDeviceWriteOperation device_write_operation;
    void *device_write_context;
    Pipe *pipe;
    fs::Vfs *vfs;
    fs::OpenFile open_file;
};

[[nodiscard]] FileDescriptionStatus MapObjectStatus(const KernelObjectStatus status) noexcept {
    if (status == KernelObjectStatus::Succeeded) {
        return FileDescriptionStatus::Succeeded;
    }
    if (status == KernelObjectStatus::NotInitialized) {
        return FileDescriptionStatus::NotInitialized;
    }
    if (status == KernelObjectStatus::InvalidReference ||
        status == KernelObjectStatus::ReferenceUnavailable) {
        return FileDescriptionStatus::InvalidReference;
    }
    return FileDescriptionStatus::ObjectFailure;
}

[[nodiscard]] FileDescriptionStatus MapPipeReadStatus(const PipeStatus status) noexcept {
    if (status == PipeStatus::Succeeded) {
        return FileDescriptionStatus::Succeeded;
    }
    if (status == PipeStatus::WouldBlock) {
        return FileDescriptionStatus::WouldBlock;
    }
    if (status == PipeStatus::EndOfFile) {
        return FileDescriptionStatus::EndOfFile;
    }
    return status == PipeStatus::BrokenPipe ? FileDescriptionStatus::BrokenPipe
                                            : FileDescriptionStatus::InvalidArgument;
}

[[nodiscard]] FileDescriptionStatus MapPipeWriteStatus(const PipeStatus status) noexcept {
    if (status == PipeStatus::Succeeded) {
        return FileDescriptionStatus::Succeeded;
    }
    if (status == PipeStatus::WouldBlock) {
        return FileDescriptionStatus::WouldBlock;
    }
    if (status == PipeStatus::BrokenPipe) {
        return FileDescriptionStatus::BrokenPipe;
    }
    return FileDescriptionStatus::InvalidArgument;
}

}

FileDescriptionStatus
FileDescriptionManager::Initialize(KernelObjectManager &object_manager) noexcept {
    if (this->initialized_) {
        return FileDescriptionStatus::AlreadyInitialized;
    }
    if (object_manager.Validate() != KernelObjectStatus::Succeeded) {
        return FileDescriptionStatus::InvalidDependency;
    }
    this->object_manager_ = &object_manager;
    this->statistics_lock_ = SpinLock{};
    this->statistics_ = FileDescriptionManagerStatistics{};
    this->initialized_ = true;
    return FileDescriptionStatus::Succeeded;
}

FileDescriptionStatus FileDescriptionManager::Create(const FileDescriptionCreateRequest &request,
                                                     KernelObjectReference &reference) noexcept {
    if (!this->initialized_ || this->object_manager_ == nullptr) {
        return FileDescriptionStatus::NotInitialized;
    }
    if (!this->IsRequestValid(request)) {
        return FileDescriptionStatus::InvalidConfiguration;
    }
    const KernelObjectStatus create_status = this->object_manager_->CreateObject(
        KernelObjectType::FileDescription, static_cast<uint64_t>(request.kind),
        sizeof(FileDescriptionStorage), FileDescriptionManager::FinalizePayload, this, reference);
    if (create_status != KernelObjectStatus::Succeeded) {
        return MapObjectStatus(create_status);
    }

    void *payload = nullptr;
    SpinLock *operation_lock = nullptr;
    const KernelObjectStatus payload_status = this->object_manager_->TryGetPayload(
        reference, KernelObjectType::FileDescription, payload, operation_lock);
    if (payload_status != KernelObjectStatus::Succeeded || payload == nullptr ||
        operation_lock == nullptr) {
        static_cast<void>(reference.Reset());
        return FileDescriptionStatus::ObjectFailure;
    }
    FileDescriptionStorage *const storage = static_cast<FileDescriptionStorage *>(payload);
    *storage = FileDescriptionStorage{
        .kind = request.kind,
        .file_status_flags = request.file_status_flags,
        .console_input = request.console_input,
        .device_write_operation = request.device_write_operation,
        .device_write_context = request.device_write_context,
        .pipe = request.pipe,
        .vfs = request.vfs,
        .open_file = request.open_file,
    };
    return FileDescriptionStatus::Succeeded;
}

FileDescriptionStatus
FileDescriptionManager::ReadSnapshot(const KernelObjectReference &reference,
                                     FileDescriptionSnapshot &snapshot) noexcept {
    snapshot = FileDescriptionSnapshot{};
    if (!this->initialized_ || this->object_manager_ == nullptr) {
        return FileDescriptionStatus::NotInitialized;
    }
    void *payload = nullptr;
    SpinLock *operation_lock = nullptr;
    const KernelObjectStatus payload_status = this->object_manager_->TryGetPayload(
        reference, KernelObjectType::FileDescription, payload, operation_lock);
    if (payload_status != KernelObjectStatus::Succeeded || payload == nullptr ||
        operation_lock == nullptr) {
        return MapObjectStatus(payload_status);
    }
    KernelObjectIdentity identity{};
    if (reference.ReadIdentity(identity) != KernelObjectStatus::Succeeded) {
        return FileDescriptionStatus::InvalidReference;
    }
    SpinLockGuard guard{*operation_lock};
    const FileDescriptionStorage &storage = *static_cast<const FileDescriptionStorage *>(payload);
    fs::NodeInformation information{};
    if ((storage.kind == FileDescriptionKind::RegularFile ||
         storage.kind == FileDescriptionKind::Directory) &&
        (storage.vfs == nullptr ||
         storage.vfs->StatOpenFile(storage.open_file, information) !=
             fs::Status::Succeeded)) {
        return FileDescriptionStatus::FileSystemFailure;
    }
    snapshot = FileDescriptionSnapshot{
        .kind = storage.kind,
        .file_status_flags = storage.file_status_flags,
        .offset_bytes = storage.kind == FileDescriptionKind::RegularFile ||
                                storage.kind == FileDescriptionKind::Directory
                            ? storage.open_file.offset_bytes
                            : OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE,
        .generation = identity.generation,
        .strong_reference_count = identity.strong_reference_count,
        .superblock_identifier =
            storage.kind == FileDescriptionKind::RegularFile ||
                    storage.kind == FileDescriptionKind::Directory
                ? storage.open_file.path.vnode.superblock->identifier
                : OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE,
        .superblock_generation =
            storage.kind == FileDescriptionKind::RegularFile ||
                    storage.kind == FileDescriptionKind::Directory
                ? storage.open_file.path.vnode.superblock->generation
                : OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE,
        .node_identifier =
            storage.kind == FileDescriptionKind::RegularFile ||
                    storage.kind == FileDescriptionKind::Directory
                ? storage.open_file.path.vnode.identifier
                : OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE,
        .node_generation =
            storage.kind == FileDescriptionKind::RegularFile ||
                    storage.kind == FileDescriptionKind::Directory
                ? storage.open_file.path.vnode.generation
                : OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE,
        .size_bytes =
            storage.kind == FileDescriptionKind::RegularFile ||
                    storage.kind == FileDescriptionKind::Directory
                ? information.size_bytes
                : OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE,
    };
    return FileDescriptionStatus::Succeeded;
}

FileDescriptionStatus FileDescriptionManager::RetainRegularFile(
    const KernelObjectReference &reference,
    RetainedRegularFile &retained_file) noexcept {
    retained_file = RetainedRegularFile{};
    if (!this->initialized_ || this->object_manager_ == nullptr) {
        return FileDescriptionStatus::NotInitialized;
    }
    void *payload = nullptr;
    SpinLock *operation_lock = nullptr;
    const KernelObjectStatus payload_status =
        this->object_manager_->TryGetPayload(
            reference, KernelObjectType::FileDescription, payload,
            operation_lock);
    if (payload_status != KernelObjectStatus::Succeeded ||
        payload == nullptr || operation_lock == nullptr) {
        return MapObjectStatus(payload_status);
    }
    SpinLockGuard guard{*operation_lock};
    const FileDescriptionStorage &storage =
        *static_cast<const FileDescriptionStorage *>(payload);
    if (storage.kind != FileDescriptionKind::RegularFile ||
        storage.vfs == nullptr ||
        (storage.file_status_flags &
         OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG) ==
            OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return FileDescriptionStatus::PermissionDenied;
    }
    fs::OpenFile retained_open_file{};
    if (storage.vfs->RetainOpenFile(storage.open_file,
                                    retained_open_file) !=
        fs::Status::Succeeded) {
        return FileDescriptionStatus::FileSystemFailure;
    }
    retained_file = RetainedRegularFile{
        .vfs = storage.vfs,
        .open_file = retained_open_file,
        .file_status_flags = storage.file_status_flags,
    };
    return FileDescriptionStatus::Succeeded;
}

FileDescriptionStatus FileDescriptionManager::TryRead(const KernelObjectReference &reference,
                                                      uint8_t *const destination,
                                                      const uint64_t capacity_bytes,
                                                      uint64_t &read_bytes,
                                                      FileSystemStatus &file_system_status,
                                                      PipeStatus &pipe_status) noexcept {
    read_bytes = OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE;
    file_system_status = FileSystemStatus::Succeeded;
    pipe_status = PipeStatus::Succeeded;
    if (!this->initialized_ || this->object_manager_ == nullptr) {
        return FileDescriptionStatus::NotInitialized;
    }
    if (destination == nullptr && capacity_bytes != OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return FileDescriptionStatus::InvalidArgument;
    }
    void *payload = nullptr;
    SpinLock *operation_lock = nullptr;
    const KernelObjectStatus payload_status = this->object_manager_->TryGetPayload(
        reference, KernelObjectType::FileDescription, payload, operation_lock);
    if (payload_status != KernelObjectStatus::Succeeded || payload == nullptr ||
        operation_lock == nullptr) {
        return MapObjectStatus(payload_status);
    }

    SpinLockGuard guard{*operation_lock};
    FileDescriptionStorage &storage = *static_cast<FileDescriptionStorage *>(payload);
    if ((storage.file_status_flags & OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG) ==
        OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return FileDescriptionStatus::PermissionDenied;
    }
    FileDescriptionStatus status = FileDescriptionStatus::InvalidArgument;
    if (storage.kind == FileDescriptionKind::ConsoleInput) {
        const ConsoleInputStatus console_status =
            storage.console_input->TryRead(destination, capacity_bytes, read_bytes);
        status = console_status == ConsoleInputStatus::Succeeded ? FileDescriptionStatus::Succeeded
                 : console_status == ConsoleInputStatus::Empty
                     ? FileDescriptionStatus::WouldBlock
                     : FileDescriptionStatus::InvalidArgument;
    } else if (storage.kind == FileDescriptionKind::RegularFile) {
        file_system_status = fs::ToFileSystemStatus(
            storage.vfs->Read(storage.open_file, destination, capacity_bytes, read_bytes));
        status = file_system_status == FileSystemStatus::Succeeded
                     ? FileDescriptionStatus::Succeeded
                     : FileDescriptionStatus::FileSystemFailure;
    } else if (storage.kind == FileDescriptionKind::PipeReader) {
        pipe_status = storage.pipe->TryRead(destination, capacity_bytes, read_bytes);
        status = MapPipeReadStatus(pipe_status);
    } else {
        status = FileDescriptionStatus::PermissionDenied;
    }
    if (status == FileDescriptionStatus::Succeeded) {
        SpinLockGuard statistics_guard{this->statistics_lock_};
        ++this->statistics_.read_operation_count;
        this->statistics_.bytes_read += read_bytes;
    }
    return status;
}

FileDescriptionStatus FileDescriptionManager::TryWrite(const KernelObjectReference &reference,
                                                       const uint8_t *const source,
                                                       const uint64_t length_bytes,
                                                       uint64_t &written_bytes,
                                                       FileSystemStatus &file_system_status,
                                                       PipeStatus &pipe_status) noexcept {
    written_bytes = OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE;
    file_system_status = FileSystemStatus::Succeeded;
    pipe_status = PipeStatus::Succeeded;
    if (!this->initialized_ || this->object_manager_ == nullptr) {
        return FileDescriptionStatus::NotInitialized;
    }
    if (source == nullptr && length_bytes != OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return FileDescriptionStatus::InvalidArgument;
    }
    void *payload = nullptr;
    SpinLock *operation_lock = nullptr;
    const KernelObjectStatus payload_status = this->object_manager_->TryGetPayload(
        reference, KernelObjectType::FileDescription, payload, operation_lock);
    if (payload_status != KernelObjectStatus::Succeeded || payload == nullptr ||
        operation_lock == nullptr) {
        return MapObjectStatus(payload_status);
    }

    SpinLockGuard guard{*operation_lock};
    FileDescriptionStorage &storage = *static_cast<FileDescriptionStorage *>(payload);
    if ((storage.file_status_flags & OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG) ==
        OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return FileDescriptionStatus::PermissionDenied;
    }
    FileDescriptionStatus status = FileDescriptionStatus::InvalidArgument;
    if (storage.kind == FileDescriptionKind::ConsoleOutput ||
        storage.kind == FileDescriptionKind::ConsoleError) {
        status = storage.device_write_operation(storage.device_write_context, source, length_bytes,
                                                written_bytes)
                     ? FileDescriptionStatus::Succeeded
                     : FileDescriptionStatus::DeviceFailure;
    } else if (storage.kind == FileDescriptionKind::RegularFile) {
        file_system_status = fs::ToFileSystemStatus(
            storage.vfs->Write(storage.open_file, source, length_bytes, written_bytes));
        status = file_system_status == FileSystemStatus::Succeeded
                     ? FileDescriptionStatus::Succeeded
                     : FileDescriptionStatus::FileSystemFailure;
    } else if (storage.kind == FileDescriptionKind::PipeWriter) {
        pipe_status = storage.pipe->TryWrite(source, length_bytes, written_bytes);
        status = MapPipeWriteStatus(pipe_status);
    } else {
        status = FileDescriptionStatus::PermissionDenied;
    }
    if (status == FileDescriptionStatus::Succeeded) {
        SpinLockGuard statistics_guard{this->statistics_lock_};
        ++this->statistics_.write_operation_count;
        this->statistics_.bytes_written += written_bytes;
    }
    return status;
}

FileDescriptionStatus
FileDescriptionManager::ReadDirectory(const KernelObjectReference &reference,
                                      fs::DirectoryEntry &entry, bool &end_of_directory,
                                      FileSystemStatus &file_system_status) noexcept {
    entry = fs::DirectoryEntry{};
    end_of_directory = false;
    file_system_status = FileSystemStatus::Succeeded;
    if (!this->initialized_ || this->object_manager_ == nullptr) {
        return FileDescriptionStatus::NotInitialized;
    }
    void *payload = nullptr;
    SpinLock *operation_lock = nullptr;
    const KernelObjectStatus payload_status = this->object_manager_->TryGetPayload(
        reference, KernelObjectType::FileDescription, payload, operation_lock);
    if (payload_status != KernelObjectStatus::Succeeded || payload == nullptr ||
        operation_lock == nullptr) {
        return MapObjectStatus(payload_status);
    }
    SpinLockGuard guard{*operation_lock};
    FileDescriptionStorage &storage = *static_cast<FileDescriptionStorage *>(payload);
    if (storage.kind != FileDescriptionKind::Directory ||
        (storage.file_status_flags & OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG) ==
            OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return FileDescriptionStatus::PermissionDenied;
    }
    file_system_status = fs::ToFileSystemStatus(
        storage.vfs->ReadDirectory(storage.open_file, entry, end_of_directory));
    if (file_system_status != FileSystemStatus::Succeeded) {
        return FileDescriptionStatus::FileSystemFailure;
    }
    SpinLockGuard statistics_guard{this->statistics_lock_};
    ++this->statistics_.directory_read_operation_count;
    return FileDescriptionStatus::Succeeded;
}

FileDescriptionStatus
FileDescriptionManager::ReadCanProgress(const KernelObjectReference &reference,
                                        bool &can_progress) noexcept {
    can_progress = false;
    if (!this->initialized_ || this->object_manager_ == nullptr) {
        return FileDescriptionStatus::NotInitialized;
    }
    void *payload = nullptr;
    SpinLock *operation_lock = nullptr;
    const KernelObjectStatus payload_status = this->object_manager_->TryGetPayload(
        reference, KernelObjectType::FileDescription, payload, operation_lock);
    if (payload_status != KernelObjectStatus::Succeeded || payload == nullptr ||
        operation_lock == nullptr) {
        return MapObjectStatus(payload_status);
    }
    SpinLockGuard guard{*operation_lock};
    const FileDescriptionStorage &storage = *static_cast<const FileDescriptionStorage *>(payload);
    if ((storage.file_status_flags & OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG) ==
        OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return FileDescriptionStatus::PermissionDenied;
    }
    if (storage.kind == FileDescriptionKind::ConsoleInput) {
        can_progress = storage.console_input->ReadCanProgress();
    } else if (storage.kind == FileDescriptionKind::RegularFile ||
               storage.kind == FileDescriptionKind::Directory) {
        can_progress = true;
    } else if (storage.kind == FileDescriptionKind::PipeReader) {
        can_progress = storage.pipe->ReadCanProgress();
    } else {
        return FileDescriptionStatus::PermissionDenied;
    }
    return FileDescriptionStatus::Succeeded;
}

FileDescriptionStatus
FileDescriptionManager::WriteCanProgress(const KernelObjectReference &reference,
                                         bool &can_progress) noexcept {
    can_progress = false;
    if (!this->initialized_ || this->object_manager_ == nullptr) {
        return FileDescriptionStatus::NotInitialized;
    }
    void *payload = nullptr;
    SpinLock *operation_lock = nullptr;
    const KernelObjectStatus payload_status = this->object_manager_->TryGetPayload(
        reference, KernelObjectType::FileDescription, payload, operation_lock);
    if (payload_status != KernelObjectStatus::Succeeded || payload == nullptr ||
        operation_lock == nullptr) {
        return MapObjectStatus(payload_status);
    }
    SpinLockGuard guard{*operation_lock};
    const FileDescriptionStorage &storage = *static_cast<const FileDescriptionStorage *>(payload);
    if ((storage.file_status_flags & OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG) ==
        OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return FileDescriptionStatus::PermissionDenied;
    }
    if (storage.kind == FileDescriptionKind::ConsoleOutput ||
        storage.kind == FileDescriptionKind::ConsoleError ||
        storage.kind == FileDescriptionKind::RegularFile) {
        can_progress = true;
    } else if (storage.kind == FileDescriptionKind::PipeWriter) {
        can_progress = storage.pipe->WriteCanProgress();
    } else {
        return FileDescriptionStatus::PermissionDenied;
    }
    return FileDescriptionStatus::Succeeded;
}

FileDescriptionManagerStatistics FileDescriptionManager::Statistics() const noexcept {
    SpinLockGuard guard{this->statistics_lock_};
    return this->statistics_;
}

bool FileDescriptionManager::FinalizePayload(void *const payload, void *const context) noexcept {
    if (context == nullptr) {
        return false;
    }
    return static_cast<FileDescriptionManager *>(context)->Finalize(payload);
}

bool FileDescriptionManager::Finalize(void *const payload) noexcept {
    if (payload == nullptr) {
        SpinLockGuard guard{this->statistics_lock_};
        ++this->statistics_.failed_finalization_count;
        return false;
    }
    FileDescriptionStorage &storage = *static_cast<FileDescriptionStorage *>(payload);
    bool finalized = true;
    if (storage.kind == FileDescriptionKind::RegularFile ||
        storage.kind == FileDescriptionKind::Directory) {
        finalized = storage.vfs != nullptr &&
                    storage.vfs->Close(storage.open_file) == fs::Status::Succeeded;
    } else if (storage.kind == FileDescriptionKind::PipeReader) {
        const PipeStatus status =
            storage.pipe == nullptr ? PipeStatus::InvalidArgument : storage.pipe->CloseReader();
        finalized = status == PipeStatus::Succeeded || status == PipeStatus::AlreadyClosed;
    } else if (storage.kind == FileDescriptionKind::PipeWriter) {
        const PipeStatus status =
            storage.pipe == nullptr ? PipeStatus::InvalidArgument : storage.pipe->CloseWriter();
        finalized = status == PipeStatus::Succeeded || status == PipeStatus::AlreadyClosed;
    } else if (storage.kind == FileDescriptionKind::None) {
        finalized = false;
    }
    storage.kind = FileDescriptionKind::None;
    SpinLockGuard guard{this->statistics_lock_};
    if (finalized) {
        ++this->statistics_.successful_finalization_count;
    } else {
        ++this->statistics_.failed_finalization_count;
    }
    return finalized;
}

bool FileDescriptionManager::IsRequestValid(
    const FileDescriptionCreateRequest &request) const noexcept {
    if (request.kind == FileDescriptionKind::None ||
        (request.file_status_flags & ~OS_KERNEL_FILE_DESCRIPTION_VALID_STATUS_FLAG_MASK) !=
            OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE) {
        return false;
    }
    const bool readable =
        (request.file_status_flags & OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG) !=
        OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE;
    const bool writable =
        (request.file_status_flags & OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG) !=
        OS_KERNEL_FILE_DESCRIPTION_EMPTY_VALUE;
    if (request.kind == FileDescriptionKind::ConsoleInput) {
        return readable && !writable && request.console_input != nullptr;
    }
    if (request.kind == FileDescriptionKind::ConsoleOutput ||
        request.kind == FileDescriptionKind::ConsoleError) {
        return !readable && writable && request.device_write_operation != nullptr;
    }
    if (request.kind == FileDescriptionKind::PipeReader) {
        return readable && !writable && request.pipe != nullptr;
    }
    if (request.kind == FileDescriptionKind::PipeWriter) {
        return !readable && writable && request.pipe != nullptr;
    }
    if (request.kind == FileDescriptionKind::Directory) {
        return readable && !writable && request.vfs != nullptr && request.open_file.open &&
               request.open_file.path.vnode.type == fs::NodeType::Directory &&
               request.open_file.readable && !request.open_file.writable;
    }
    return request.kind == FileDescriptionKind::RegularFile && (readable || writable) &&
           request.vfs != nullptr && request.open_file.open &&
           request.open_file.path.vnode.type == fs::NodeType::RegularFile &&
           request.open_file.readable == readable && request.open_file.writable == writable;
}

}
