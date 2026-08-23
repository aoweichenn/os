#include <os/kernel/user/file_backing.hpp>

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_USER_FILE_BACKING_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_KERNEL_USER_FILE_BACKING_MEMORY_SUPERBLOCK_IDENTIFIER =
    UINT64_MAX - OS_KERNEL_USER_FILE_BACKING_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_USER_FILE_BACKING_MEMORY_SUPERBLOCK_GENERATION =
    OS_KERNEL_USER_FILE_BACKING_SINGLE_UNIT;
constexpr uint64_t OS_KERNEL_USER_FILE_BACKING_WRITEBACK_OWNER_IDENTIFIER = UINT64_MAX;
constexpr uint64_t OS_KERNEL_USER_FILE_BACKING_WAIT_QUEUE_IDENTIFIER =
    0x8000000000000104ULL;

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         byte_index < length_bytes; ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

[[nodiscard]] uint64_t Minimum(const uint64_t left,
                               const uint64_t right) noexcept {
    return left < right ? left : right;
}

}

UserFileBackingStatus UserFileBackingManager::Initialize(
    UserFileBackingDescriptor *const descriptors,
    const uint64_t capacity) noexcept {
    if (this->initialized_) {
        return UserFileBackingStatus::AlreadyInitialized;
    }
    if (descriptors == nullptr) {
        return UserFileBackingStatus::InvalidStorage;
    }
    if (capacity == OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE) {
        return UserFileBackingStatus::InvalidCapacity;
    }
    for (uint64_t descriptor_index =
             OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         descriptor_index < capacity; ++descriptor_index) {
        descriptors[descriptor_index] = UserFileBackingDescriptor{};
    }
    this->descriptors_ = descriptors;
    this->capacity_ = capacity;
    this->active_descriptor_count_ =
        OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
    this->next_generation_ = OS_KERNEL_USER_FILE_BACKING_SINGLE_UNIT;
    this->last_close_status_ = fs::Status::Succeeded;
    if (this->lock_.Initialize(WaitQueueId{
            .value = OS_KERNEL_USER_FILE_BACKING_WAIT_QUEUE_IDENTIFIER,
        }) != RuntimeMutexStatus::Succeeded) {
        this->descriptors_ = nullptr;
        this->capacity_ = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
        return UserFileBackingStatus::Corrupt;
    }
    this->initialized_ = true;
    return UserFileBackingStatus::Succeeded;
}

UserFileBackingStatus UserFileBackingManager::AcquireMemoryImage(
    const uint64_t owner_identifier, const uint8_t *const image,
    const uint64_t image_size_bytes, uint64_t &descriptor_index,
    uint64_t &generation) noexcept {
    descriptor_index = UINT64_MAX;
    generation = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    if (owner_identifier == OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE ||
        image == nullptr ||
        image_size_bytes == OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE ||
        image_size_bytes == UINT64_MAX) {
        return UserFileBackingStatus::InvalidSource;
    }
    RuntimeMutexGuard guard{this->lock_};
    for (uint64_t candidate_index =
             OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         candidate_index < this->capacity_; ++candidate_index) {
        UserFileBackingDescriptor &candidate =
            this->descriptors_[candidate_index];
        if (candidate.active) {
            continue;
        }
        const uint64_t candidate_generation = this->NextGeneration();
        candidate = UserFileBackingDescriptor{
            .kind = UserFileBackingKind::MemoryImage,
            .generation = candidate_generation,
            .owner_identifier = owner_identifier,
            .identity =
                {
                    .superblock_identifier =
                        OS_KERNEL_USER_FILE_BACKING_MEMORY_SUPERBLOCK_IDENTIFIER,
                    .superblock_generation =
                        OS_KERNEL_USER_FILE_BACKING_MEMORY_SUPERBLOCK_GENERATION,
                    .node_identifier =
                        reinterpret_cast<uint64_t>(image),
                    .node_generation = image_size_bytes,
                },
            .size_bytes = image_size_bytes,
            .memory_image = image,
            .vfs = nullptr,
            .open_file = {},
            .active = true,
        };
        ++this->active_descriptor_count_;
        descriptor_index = candidate_index;
        generation = candidate_generation;
        return UserFileBackingStatus::Succeeded;
    }
    return UserFileBackingStatus::CapacityExhausted;
}

UserFileBackingStatus UserFileBackingManager::AcquireVfsFile(
    const uint64_t owner_identifier, fs::Vfs &vfs,
    const fs::OpenFile &open_file, uint64_t &descriptor_index,
    uint64_t &generation) noexcept {
    descriptor_index = UINT64_MAX;
    generation = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    if (owner_identifier == OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE ||
        !open_file.open || !open_file.readable ||
        open_file.path.vnode.type != fs::NodeType::RegularFile) {
        return UserFileBackingStatus::InvalidSource;
    }
    fs::NodeInformation information{};
    if (vfs.StatOpenFile(open_file, information) !=
        fs::Status::Succeeded) {
        return UserFileBackingStatus::InvalidSource;
    }

    RuntimeMutexGuard guard{this->lock_};
    uint64_t candidate_index = UINT64_MAX;
    for (uint64_t observed_index =
             OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         observed_index < this->capacity_; ++observed_index) {
        if (!this->descriptors_[observed_index].active) {
            candidate_index = observed_index;
            break;
        }
    }
    if (candidate_index == UINT64_MAX) {
        return UserFileBackingStatus::CapacityExhausted;
    }
    fs::OpenFile retained_file{};
    if (vfs.RetainOpenFile(open_file, retained_file) !=
        fs::Status::Succeeded) {
        return UserFileBackingStatus::InvalidSource;
    }
    const uint64_t candidate_generation = this->NextGeneration();
    this->descriptors_[candidate_index] = UserFileBackingDescriptor{
        .kind = UserFileBackingKind::VfsFile,
        .generation = candidate_generation,
        .owner_identifier = owner_identifier,
        .identity =
            {
                .superblock_identifier =
                    open_file.path.vnode.superblock->identifier,
                .superblock_generation =
                    open_file.path.vnode.superblock->generation,
                .node_identifier = open_file.path.vnode.identifier,
                .node_generation = open_file.path.vnode.generation,
            },
        .size_bytes = information.size_bytes,
        .memory_image = nullptr,
        .vfs = &vfs,
        .open_file = retained_file,
        .active = true,
    };
    ++this->active_descriptor_count_;
    descriptor_index = candidate_index;
    generation = candidate_generation;
    return UserFileBackingStatus::Succeeded;
}

UserFileBackingStatus UserFileBackingManager::Clone(
    const uint64_t owner_identifier,
    const uint64_t source_descriptor_index,
    const uint64_t source_generation, uint64_t &descriptor_index,
    uint64_t &generation) noexcept {
    descriptor_index = UINT64_MAX;
    generation = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    if (owner_identifier == OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE) {
        return UserFileBackingStatus::InvalidSource;
    }
    RuntimeMutexGuard guard{this->lock_};
    if (!this->IsIndexValid(source_descriptor_index)) {
        return UserFileBackingStatus::InvalidDescriptor;
    }
    const UserFileBackingDescriptor &source =
        this->descriptors_[source_descriptor_index];
    if (!source.active || source.generation != source_generation ||
        source.kind == UserFileBackingKind::None) {
        return UserFileBackingStatus::InvalidDescriptor;
    }
    uint64_t candidate_index = UINT64_MAX;
    for (uint64_t observed_index =
             OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         observed_index < this->capacity_; ++observed_index) {
        if (!this->descriptors_[observed_index].active) {
            candidate_index = observed_index;
            break;
        }
    }
    if (candidate_index == UINT64_MAX) {
        return UserFileBackingStatus::CapacityExhausted;
    }
    fs::OpenFile retained_file{};
    if (source.kind == UserFileBackingKind::VfsFile &&
        (source.vfs == nullptr ||
         source.vfs->RetainOpenFile(source.open_file, retained_file) !=
             fs::Status::Succeeded)) {
        return UserFileBackingStatus::InvalidSource;
    }
    const uint64_t candidate_generation = this->NextGeneration();
    this->descriptors_[candidate_index] = UserFileBackingDescriptor{
        .kind = source.kind,
        .generation = candidate_generation,
        .owner_identifier = owner_identifier,
        .identity = source.identity,
        .size_bytes = source.size_bytes,
        .memory_image = source.memory_image,
        .vfs = source.vfs,
        .open_file =
            source.kind == UserFileBackingKind::VfsFile
                ? retained_file
                : source.open_file,
        .active = true,
    };
    ++this->active_descriptor_count_;
    descriptor_index = candidate_index;
    generation = candidate_generation;
    return UserFileBackingStatus::Succeeded;
}

UserFileBackingStatus UserFileBackingManager::Release(
    const uint64_t owner_identifier, const uint64_t descriptor_index,
    const uint64_t generation) noexcept {
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    RuntimeMutexGuard guard{this->lock_};
    if (!this->IsIndexValid(descriptor_index)) {
        return UserFileBackingStatus::InvalidDescriptor;
    }
    UserFileBackingDescriptor &descriptor =
        this->descriptors_[descriptor_index];
    if (!descriptor.active || descriptor.generation != generation) {
        return UserFileBackingStatus::InvalidDescriptor;
    }
    if (descriptor.owner_identifier != owner_identifier) {
        return UserFileBackingStatus::OwnershipMismatch;
    }
    if (descriptor.kind == UserFileBackingKind::VfsFile) {
        this->last_close_status_ = descriptor.vfs == nullptr
                                       ? fs::Status::InvalidHandle
                                       : descriptor.vfs->Close(descriptor.open_file);
        if (this->last_close_status_ != fs::Status::Succeeded) {
            return UserFileBackingStatus::CloseFailed;
        }
    }
    descriptor = UserFileBackingDescriptor{};
    if (this->active_descriptor_count_ ==
        OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE) {
        return UserFileBackingStatus::Corrupt;
    }
    --this->active_descriptor_count_;
    return UserFileBackingStatus::Succeeded;
}

UserFileBackingStatus UserFileBackingManager::Read(
    const uint64_t descriptor_index, const uint64_t generation,
    const uint64_t offset_bytes, uint8_t *const destination,
    const uint64_t length_bytes) noexcept {
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    if (destination == nullptr ||
        length_bytes == OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE) {
        return UserFileBackingStatus::InvalidSource;
    }
    RuntimeMutexGuard guard{this->lock_};
    if (!this->IsIndexValid(descriptor_index)) {
        return UserFileBackingStatus::InvalidDescriptor;
    }
    const UserFileBackingDescriptor &descriptor =
        this->descriptors_[descriptor_index];
    if (!descriptor.active || descriptor.generation != generation) {
        return UserFileBackingStatus::InvalidDescriptor;
    }
    if (offset_bytes > descriptor.size_bytes ||
        length_bytes > descriptor.size_bytes - offset_bytes) {
        return UserFileBackingStatus::ReadFailed;
    }
    if (descriptor.kind == UserFileBackingKind::MemoryImage) {
        CopyBytes(destination, descriptor.memory_image + offset_bytes,
                  length_bytes);
        return UserFileBackingStatus::Succeeded;
    }
    uint64_t read_bytes = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
    return descriptor.kind == UserFileBackingKind::VfsFile &&
                   descriptor.vfs != nullptr &&
                   descriptor.vfs->ReadAt(descriptor.open_file, offset_bytes, destination,
                                          length_bytes, read_bytes) ==
                       fs::Status::Succeeded &&
                   read_bytes == length_bytes
               ? UserFileBackingStatus::Succeeded
               : UserFileBackingStatus::ReadFailed;
}

UserFileBackingStatus UserFileBackingManager::WritePage(
    const FilePageIdentity &identity, const uint8_t *const source,
    const uint64_t length_bytes) noexcept {
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    if (source == nullptr || length_bytes == OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE ||
        length_bytes > OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        identity.page_index > UINT64_MAX / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return UserFileBackingStatus::InvalidSource;
    }
    const uint64_t offset_bytes =
        identity.page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    RuntimeMutexGuard guard{this->lock_};
    for (uint64_t descriptor_index =
             OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         descriptor_index < this->capacity_; ++descriptor_index) {
        const UserFileBackingDescriptor &descriptor =
            this->descriptors_[descriptor_index];
        if (!descriptor.active ||
            (descriptor.kind != UserFileBackingKind::VfsFile &&
             descriptor.kind != UserFileBackingKind::VfsWriteback) ||
            !descriptor.open_file.writable || descriptor.vfs == nullptr ||
            !this->IdentitiesEqual(descriptor.identity, identity.file)) {
            continue;
        }
        if (offset_bytes > descriptor.size_bytes ||
            length_bytes > descriptor.size_bytes - offset_bytes) {
            return UserFileBackingStatus::WriteFailed;
        }
        uint64_t written_bytes = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
        return descriptor.vfs->WriteUncachedAt(descriptor.open_file, offset_bytes, source,
                                               length_bytes, written_bytes) ==
                       fs::Status::Succeeded &&
                       written_bytes == length_bytes
                   ? UserFileBackingStatus::Succeeded
                   : UserFileBackingStatus::WriteFailed;
    }
    return UserFileBackingStatus::WriteFailed;
}

UserFileBackingStatus
UserFileBackingManager::RetainWritebackFile(fs::Vfs &vfs, const fs::OpenFile &open_file,
                                            const uint64_t size_bytes) noexcept {
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    if (!open_file.open || !open_file.writable ||
        open_file.path.vnode.type != fs::NodeType::RegularFile) {
        return UserFileBackingStatus::InvalidSource;
    }
    const FileIdentity identity{
        .superblock_identifier = open_file.path.vnode.superblock->identifier,
        .superblock_generation = open_file.path.vnode.superblock->generation,
        .node_identifier = open_file.path.vnode.identifier,
        .node_generation = open_file.path.vnode.generation,
    };
    RuntimeMutexGuard guard{this->lock_};
    uint64_t candidate_index = UINT64_MAX;
    for (uint64_t descriptor_index = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         descriptor_index < this->capacity_; ++descriptor_index) {
        UserFileBackingDescriptor &descriptor = this->descriptors_[descriptor_index];
        if (descriptor.active && descriptor.kind == UserFileBackingKind::VfsWriteback &&
            this->IdentitiesEqual(descriptor.identity, identity)) {
            descriptor.size_bytes = size_bytes;
            return UserFileBackingStatus::Succeeded;
        }
        if (!descriptor.active && candidate_index == UINT64_MAX) {
            candidate_index = descriptor_index;
        }
    }
    if (candidate_index == UINT64_MAX) {
        return UserFileBackingStatus::CapacityExhausted;
    }
    fs::OpenFile retained_file{};
    if (vfs.RetainOpenFile(open_file, retained_file) != fs::Status::Succeeded) {
        return UserFileBackingStatus::InvalidSource;
    }
    this->descriptors_[candidate_index] = UserFileBackingDescriptor{
        .kind = UserFileBackingKind::VfsWriteback,
        .generation = this->NextGeneration(),
        .owner_identifier = OS_KERNEL_USER_FILE_BACKING_WRITEBACK_OWNER_IDENTIFIER,
        .identity = identity,
        .size_bytes = size_bytes,
        .memory_image = nullptr,
        .vfs = &vfs,
        .open_file = retained_file,
        .active = true,
    };
    ++this->active_descriptor_count_;
    return UserFileBackingStatus::Succeeded;
}

UserFileBackingStatus
UserFileBackingManager::ReleaseCleanWritebackFiles(
    void *const context, const UserFileBackingWritebackRequiredOperation operation) noexcept {
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    if (context == nullptr || operation == nullptr) {
        return UserFileBackingStatus::InvalidSource;
    }
    RuntimeMutexGuard guard{this->lock_};
    for (uint64_t descriptor_index = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         descriptor_index < this->capacity_; ++descriptor_index) {
        UserFileBackingDescriptor &descriptor = this->descriptors_[descriptor_index];
        if (!descriptor.active || descriptor.kind != UserFileBackingKind::VfsWriteback) {
            continue;
        }
        bool writeback_required = false;
        if (!operation(context, descriptor.identity, writeback_required)) {
            return UserFileBackingStatus::Corrupt;
        }
        if (writeback_required) {
            continue;
        }
        if (descriptor.vfs == nullptr ||
            descriptor.vfs->Close(descriptor.open_file) != fs::Status::Succeeded) {
            return UserFileBackingStatus::CloseFailed;
        }
        descriptor = UserFileBackingDescriptor{};
        if (this->active_descriptor_count_ == OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE) {
            return UserFileBackingStatus::Corrupt;
        }
        --this->active_descriptor_count_;
    }
    return UserFileBackingStatus::Succeeded;
}

UserFileBackingStatus UserFileBackingManager::ReadDescriptor(
    const uint64_t descriptor_index, const uint64_t generation,
    UserFileBackingDescriptor &descriptor) const noexcept {
    descriptor = UserFileBackingDescriptor{};
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    RuntimeMutexGuard guard{this->lock_};
    if (!this->IsIndexValid(descriptor_index) ||
        !this->descriptors_[descriptor_index].active ||
        this->descriptors_[descriptor_index].generation != generation) {
        return UserFileBackingStatus::InvalidDescriptor;
    }
    descriptor = this->descriptors_[descriptor_index];
    return UserFileBackingStatus::Succeeded;
}

UserFileBackingStatus UserFileBackingManager::UpdateFileSize(
    const FileIdentity &identity, const uint64_t size_bytes) noexcept {
    if (!this->initialized_) {
        return UserFileBackingStatus::NotInitialized;
    }
    RuntimeMutexGuard guard{this->lock_};
    for (uint64_t descriptor_index =
             OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         descriptor_index < this->capacity_; ++descriptor_index) {
        UserFileBackingDescriptor &descriptor =
            this->descriptors_[descriptor_index];
        if (descriptor.active &&
            this->IdentitiesEqual(descriptor.identity, identity)) {
            descriptor.size_bytes = size_bytes;
        }
    }
    return UserFileBackingStatus::Succeeded;
}

UserFileBackingStatus UserFileBackingManager::Validate() const noexcept {
    if (!this->initialized_ || this->descriptors_ == nullptr) {
        return UserFileBackingStatus::NotInitialized;
    }
    RuntimeMutexGuard guard{this->lock_};
    uint64_t observed_active_count =
        OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
    for (uint64_t descriptor_index =
             OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
         descriptor_index < this->capacity_; ++descriptor_index) {
        const UserFileBackingDescriptor &descriptor =
            this->descriptors_[descriptor_index];
        if (!descriptor.active) {
            continue;
        }
        if (descriptor.kind == UserFileBackingKind::None ||
            descriptor.generation ==
                OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE ||
            descriptor.owner_identifier ==
                OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE ||
            (descriptor.kind == UserFileBackingKind::MemoryImage &&
             (descriptor.size_bytes ==
                  OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE ||
              descriptor.memory_image == nullptr)) ||
            ((descriptor.kind == UserFileBackingKind::VfsFile ||
              descriptor.kind == UserFileBackingKind::VfsWriteback) &&
             (descriptor.vfs == nullptr || !descriptor.open_file.open))) {
            return UserFileBackingStatus::Corrupt;
        }
        ++observed_active_count;
    }
    return observed_active_count == this->active_descriptor_count_
               ? UserFileBackingStatus::Succeeded
               : UserFileBackingStatus::Corrupt;
}

uint64_t UserFileBackingManager::ActiveDescriptorCount() const noexcept {
    if (!this->initialized_) {
        return OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
    }
    RuntimeMutexGuard guard{this->lock_};
    return this->active_descriptor_count_;
}

fs::Status UserFileBackingManager::LastCloseStatus() const noexcept {
    if (!this->initialized_) {
        return fs::Status::NotInitialized;
    }
    RuntimeMutexGuard guard{this->lock_};
    return this->last_close_status_;
}

bool UserFileBackingManager::IsIndexValid(
    const uint64_t descriptor_index) const noexcept {
    return descriptor_index < this->capacity_;
}

bool UserFileBackingManager::IdentitiesEqual(
    const FileIdentity &left, const FileIdentity &right) const noexcept {
    return FileCacheIdentitiesEqual(left, right);
}

uint64_t UserFileBackingManager::NextGeneration() noexcept {
    if (this->next_generation_ ==
        OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE) {
        this->next_generation_ =
            OS_KERNEL_USER_FILE_BACKING_SINGLE_UNIT;
    }
    const uint64_t generation = this->next_generation_;
    ++this->next_generation_;
    return generation;
}

bool ReadUserFileBackingPage(
    void *const context, const FilePageIdentity &identity,
    uint8_t *const destination, const uint64_t capacity_bytes) noexcept {
    if (context == nullptr || destination == nullptr ||
        capacity_bytes != OS_KERNEL_MEMORY_PAGE_SIZE_BYTES ||
        identity.page_index >
            UINT64_MAX / OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) {
        return false;
    }
    UserFileBackingDescriptor &descriptor =
        *static_cast<UserFileBackingDescriptor *>(context);
    const bool identity_matches =
        descriptor.identity.superblock_identifier ==
            identity.file.superblock_identifier &&
        descriptor.identity.superblock_generation ==
            identity.file.superblock_generation &&
        descriptor.identity.node_identifier ==
            identity.file.node_identifier &&
        descriptor.identity.node_generation ==
            identity.file.node_generation;
    const uint64_t offset_bytes =
        identity.page_index * OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
    if (!descriptor.active || !identity_matches ||
        offset_bytes >= descriptor.size_bytes) {
        return false;
    }
    const uint64_t read_capacity =
        Minimum(capacity_bytes, descriptor.size_bytes - offset_bytes);
    if (descriptor.kind == UserFileBackingKind::MemoryImage) {
        CopyBytes(destination, descriptor.memory_image + offset_bytes,
                  read_capacity);
        return true;
    }
    fs::NodeInformation source_information{};
    if (descriptor.kind != UserFileBackingKind::VfsFile || descriptor.vfs == nullptr ||
        descriptor.vfs->StatOpenFileUncached(descriptor.open_file, source_information) !=
            fs::Status::Succeeded) {
        return false;
    }
    if (offset_bytes >= source_information.size_bytes) {
        return true;
    }
    const uint64_t source_read_capacity =
        Minimum(read_capacity, source_information.size_bytes - offset_bytes);
    uint64_t read_bytes = OS_KERNEL_USER_FILE_BACKING_EMPTY_VALUE;
    return descriptor.vfs->ReadUncachedAt(descriptor.open_file, offset_bytes, destination,
                                          source_read_capacity, read_bytes) ==
               fs::Status::Succeeded &&
           read_bytes == source_read_capacity;
}

bool WriteUserFileBackingPage(
    void *const context, const FilePageIdentity &identity,
    const uint8_t *const source, const uint64_t length_bytes) noexcept {
    if (context == nullptr) {
        return false;
    }
    UserFileBackingManager &manager =
        *static_cast<UserFileBackingManager *>(context);
    return manager.WritePage(identity, source, length_bytes) ==
           UserFileBackingStatus::Succeeded;
}

}
