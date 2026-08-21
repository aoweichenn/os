#include <os/kernel/fs/procfs.hpp>

#include <os/abi/version.hpp>

namespace os::kernel::fs {

namespace {

constexpr uint64_t OS_KERNEL_PROCFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_KERNEL_PROCFS_ROOT_NODE_IDENTIFIER = 1ULL;
constexpr uint64_t OS_KERNEL_PROCFS_FIRST_FILE_NODE_IDENTIFIER = 2ULL;
constexpr uint64_t OS_KERNEL_PROCFS_INITIAL_GENERATION = 1ULL;
constexpr uint64_t OS_KERNEL_PROCFS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCFS_ROOT_LINK_COUNT = 2ULL;
constexpr uint64_t OS_KERNEL_PROCFS_FILE_LINK_COUNT = 1ULL;
constexpr uint64_t OS_KERNEL_PROCFS_DECIMAL_RADIX = 10ULL;
constexpr uint64_t OS_KERNEL_PROCFS_DECIMAL_CAPACITY_BYTES = 20ULL;
constexpr uint64_t OS_KERNEL_PROCFS_STRING_TERMINATOR_SIZE_BYTES = 1ULL;
constexpr uint64_t OS_KERNEL_PROCFS_MAXIMUM_COUNTER_VALUE = ~0ULL;
constexpr uint8_t OS_KERNEL_PROCFS_NEWLINE_CHARACTER = '\n';

struct ProcfsNodeDescription final {
    const char *name;
    uint64_t name_length_bytes;
};

constexpr char OS_KERNEL_PROCFS_VERSION_NAME[] = "version";
constexpr char OS_KERNEL_PROCFS_UPTIME_NAME[] = "uptime";
constexpr char OS_KERNEL_PROCFS_MEMORY_INFORMATION_NAME[] = "meminfo";
constexpr char OS_KERNEL_PROCFS_PROCESSES_NAME[] = "processes";
constexpr char OS_KERNEL_PROCFS_RESOURCES_NAME[] = "resources";
constexpr char OS_KERNEL_PROCFS_MOUNTS_NAME[] = "mounts";

constexpr ProcfsNodeDescription OS_KERNEL_PROCFS_NODES[]{
    {OS_KERNEL_PROCFS_VERSION_NAME,
     sizeof(OS_KERNEL_PROCFS_VERSION_NAME) - OS_KERNEL_PROCFS_STRING_TERMINATOR_SIZE_BYTES},
    {OS_KERNEL_PROCFS_UPTIME_NAME,
     sizeof(OS_KERNEL_PROCFS_UPTIME_NAME) - OS_KERNEL_PROCFS_STRING_TERMINATOR_SIZE_BYTES},
    {OS_KERNEL_PROCFS_MEMORY_INFORMATION_NAME, sizeof(OS_KERNEL_PROCFS_MEMORY_INFORMATION_NAME) -
                                                   OS_KERNEL_PROCFS_STRING_TERMINATOR_SIZE_BYTES},
    {OS_KERNEL_PROCFS_PROCESSES_NAME,
     sizeof(OS_KERNEL_PROCFS_PROCESSES_NAME) - OS_KERNEL_PROCFS_STRING_TERMINATOR_SIZE_BYTES},
    {OS_KERNEL_PROCFS_RESOURCES_NAME,
     sizeof(OS_KERNEL_PROCFS_RESOURCES_NAME) - OS_KERNEL_PROCFS_STRING_TERMINATOR_SIZE_BYTES},
    {OS_KERNEL_PROCFS_MOUNTS_NAME,
     sizeof(OS_KERNEL_PROCFS_MOUNTS_NAME) - OS_KERNEL_PROCFS_STRING_TERMINATOR_SIZE_BYTES},
};

static_assert(sizeof(OS_KERNEL_PROCFS_NODES) /
                  sizeof(OS_KERNEL_PROCFS_NODES[OS_KERNEL_PROCFS_EMPTY_VALUE]) ==
              OS_KERNEL_PROCFS_FILE_COUNT);

constexpr char OS_KERNEL_PROCFS_VERSION_ABI_PREFIX[] = "abi_major ";
constexpr char OS_KERNEL_PROCFS_VERSION_ABI_MINOR_PREFIX[] = "abi_minor ";
constexpr char OS_KERNEL_PROCFS_VERSION_ARCHITECTURE[] = "architecture x86_64\n";
constexpr char OS_KERNEL_PROCFS_UPTIME_PREFIX[] = "monotonic_nanoseconds ";
constexpr char OS_KERNEL_PROCFS_MEMORY_MANAGED_PREFIX[] = "managed_bytes ";
constexpr char OS_KERNEL_PROCFS_MEMORY_FREE_PREFIX[] = "free_bytes ";
constexpr char OS_KERNEL_PROCFS_MEMORY_ALLOCATED_PREFIX[] = "allocated_bytes ";
constexpr char OS_KERNEL_PROCFS_MEMORY_RESIDENT_LIMIT_PREFIX[] = "resident_limit_bytes ";
constexpr char OS_KERNEL_PROCFS_MEMORY_SWAP_TOTAL_PREFIX[] = "swap_total_bytes ";
constexpr char OS_KERNEL_PROCFS_MEMORY_SWAP_FREE_PREFIX[] = "swap_free_bytes ";
constexpr char OS_KERNEL_PROCFS_MEMORY_COMMITTED_PREFIX[] = "committed_bytes ";
constexpr char OS_KERNEL_PROCFS_MEMORY_COMMIT_LIMIT_PREFIX[] = "commit_limit_bytes ";
constexpr char OS_KERNEL_PROCFS_MEMORY_OOM_KILL_COUNT_PREFIX[] = "oom_kills ";
constexpr char OS_KERNEL_PROCFS_PROCESS_ACTIVE_PREFIX[] = "active_processes ";
constexpr char OS_KERNEL_PROCFS_THREAD_ACTIVE_PREFIX[] = "active_threads ";
constexpr char OS_KERNEL_PROCFS_PROCESS_CAPACITY_PREFIX[] = "process_capacity ";
constexpr char OS_KERNEL_PROCFS_THREAD_CAPACITY_PREFIX[] = "thread_capacity ";
constexpr char OS_KERNEL_PROCFS_CURRENT_PROCESS_PREFIX[] = "current_process ";
constexpr char OS_KERNEL_PROCFS_RESOURCE_HEAP_PREFIX[] = "heap_consumed_bytes ";
constexpr char OS_KERNEL_PROCFS_RESOURCE_FILE_DESCRIPTIONS_PREFIX[] = "active_file_descriptions ";
constexpr char OS_KERNEL_PROCFS_RESOURCE_PIPES_PREFIX[] = "active_pipes ";
constexpr char OS_KERNEL_PROCFS_RESOURCE_VNODES_PREFIX[] = "vnodes ";
constexpr char OS_KERNEL_PROCFS_RESOURCE_JOURNAL_COMMITS_PREFIX[] = "journal_commits ";
constexpr char OS_KERNEL_PROCFS_MOUNT_COUNT_PREFIX[] = "mount_count ";

[[nodiscard]] bool BytesAreEqual(const uint8_t *const left, const char *const right,
                                 const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_PROCFS_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != static_cast<uint8_t>(right[byte_index])) {
            return false;
        }
    }
    return true;
}

void CopyBytes(uint8_t *const destination, const uint8_t *const source,
               const uint64_t length_bytes) noexcept {
    for (uint64_t byte_index = OS_KERNEL_PROCFS_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        destination[byte_index] = source[byte_index];
    }
}

void IncrementSaturatingCounter(uint64_t &counter) noexcept {
    if (counter != OS_KERNEL_PROCFS_MAXIMUM_COUNTER_VALUE) {
        ++counter;
    }
}

void AddSaturatingCounter(uint64_t &counter, const uint64_t increment) noexcept {
    counter = increment > OS_KERNEL_PROCFS_MAXIMUM_COUNTER_VALUE - counter
                  ? OS_KERNEL_PROCFS_MAXIMUM_COUNTER_VALUE
                  : counter + increment;
}

class SnapshotWriter final {
  public:
    SnapshotWriter(uint8_t *const destination, const uint64_t capacity_bytes) noexcept
        : destination_{destination}, capacity_bytes_{capacity_bytes} {}

    template <uint64_t SizeBytes>
    [[nodiscard]] bool AppendLiteral(const char (&literal)[SizeBytes]) noexcept {
        return this->AppendBytes(reinterpret_cast<const uint8_t *>(literal),
                                 SizeBytes - OS_KERNEL_PROCFS_STRING_TERMINATOR_SIZE_BYTES);
    }

    [[nodiscard]] bool AppendDecimal(uint64_t value) noexcept {
        uint8_t digits[OS_KERNEL_PROCFS_DECIMAL_CAPACITY_BYTES]{};
        uint64_t digit_count = OS_KERNEL_PROCFS_EMPTY_VALUE;
        do {
            digits[digit_count] = static_cast<uint8_t>(static_cast<uint8_t>('0') +
                                                       value % OS_KERNEL_PROCFS_DECIMAL_RADIX);
            value /= OS_KERNEL_PROCFS_DECIMAL_RADIX;
            ++digit_count;
        } while (value != OS_KERNEL_PROCFS_EMPTY_VALUE &&
                 digit_count < OS_KERNEL_PROCFS_DECIMAL_CAPACITY_BYTES);
        for (uint64_t left_index = OS_KERNEL_PROCFS_EMPTY_VALUE,
                      right_index = digit_count - OS_KERNEL_PROCFS_COUNTER_INCREMENT;
             left_index < right_index; ++left_index, --right_index) {
            const uint8_t temporary = digits[left_index];
            digits[left_index] = digits[right_index];
            digits[right_index] = temporary;
        }
        return this->AppendBytes(digits, digit_count);
    }

    [[nodiscard]] bool AppendLine(const uint64_t value) noexcept {
        return this->AppendDecimal(value) && this->AppendBytes(&OS_KERNEL_PROCFS_NEWLINE_CHARACTER,
                                                               OS_KERNEL_PROCFS_COUNTER_INCREMENT);
    }

    [[nodiscard]] uint64_t SizeBytes() const noexcept { return this->size_bytes_; }

  private:
    [[nodiscard]] bool AppendBytes(const uint8_t *const source,
                                   const uint64_t length_bytes) noexcept {
        if (source == nullptr || length_bytes > this->capacity_bytes_ - this->size_bytes_) {
            return false;
        }
        CopyBytes(this->destination_ + this->size_bytes_, source, length_bytes);
        this->size_bytes_ += length_bytes;
        return true;
    }

    uint8_t *destination_;
    uint64_t capacity_bytes_;
    uint64_t size_bytes_{};
};

}

const BackendOperations Procfs::operations{
    .lookup = Procfs::LookupOperation,
    .create = Procfs::CreateOperation,
    .open = Procfs::OpenOperation,
    .close = Procfs::CloseOperation,
    .remove = Procfs::RemoveOperation,
    .rename = Procfs::RenameOperation,
    .link = nullptr,
    .create_symbolic_link = nullptr,
    .read_symbolic_link = nullptr,
    .parent = Procfs::ParentOperation,
    .read = Procfs::ReadOperation,
    .write = Procfs::WriteOperation,
    .truncate = Procfs::TruncateOperation,
    .read_directory = Procfs::ReadDirectoryOperation,
    .get_name = Procfs::GetNameOperation,
    .stat = Procfs::StatOperation,
    .change_mode = nullptr,
    .change_owner = nullptr,
    .sync = Procfs::SyncOperation,
    .validate = Procfs::ValidateOperation,
    .read_resource_usage = Procfs::ReadResourceUsageOperation,
};

Status Procfs::Initialize(const uint64_t superblock_identifier,
                          const ProcfsSnapshotOperation snapshot_operation,
                          void *const snapshot_context) noexcept {
    if (this->initialized_) {
        return Status::AlreadyInitialized;
    }
    if (superblock_identifier == OS_KERNEL_PROCFS_EMPTY_VALUE || snapshot_operation == nullptr) {
        return Status::InvalidArgument;
    }
    this->snapshot_operation_ = snapshot_operation;
    this->snapshot_context_ = snapshot_context;
    this->lock_ = SpinLock{};
    this->statistics_ = ProcfsStatistics{};
    this->superblock_ = Superblock{
        .backend_kind = BackendKind::Process,
        .identifier = superblock_identifier,
        .generation = OS_KERNEL_PROCFS_INITIAL_GENERATION,
        .root = {},
        .operations = &Procfs::operations,
        .backend_context = this,
        .maximum_name_length_bytes = OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES,
        .cache_regular_file_data = false,
        .read_only = true,
        .initialized = true,
    };
    this->superblock_.root = this->MakeVnode(NodeKind::Root);
    this->initialized_ = true;
    return Status::Succeeded;
}

Superblock &Procfs::GetSuperblock() noexcept { return this->superblock_; }

const Superblock &Procfs::GetSuperblock() const noexcept { return this->superblock_; }

ProcfsStatistics Procfs::ReadStatistics() const noexcept {
    SpinLockGuard guard{this->lock_};
    return this->statistics_;
}

Status Procfs::Validate() const noexcept {
    SpinLockGuard guard{this->lock_};
    return !this->initialized_ || !this->superblock_.initialized ||
                   this->snapshot_operation_ == nullptr ||
                   this->superblock_.backend_kind != BackendKind::Process ||
                   this->superblock_.operations != &Procfs::operations ||
                   this->superblock_.backend_context != this ||
                   this->superblock_.identifier == OS_KERNEL_PROCFS_EMPTY_VALUE ||
                   this->superblock_.generation != OS_KERNEL_PROCFS_INITIAL_GENERATION ||
                   !this->superblock_.read_only || !this->VnodeIsValid(this->superblock_.root) ||
                   this->statistics_.successful_open_count < this->statistics_.active_open_count
               ? Status::Corrupt
               : Status::Succeeded;
}

Status Procfs::LookupOperation(void *const context, const Vnode &directory,
                               const uint8_t *const name, const uint64_t name_length_bytes,
                               Vnode &vnode) noexcept {
    vnode = Vnode{};
    if (context == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    Procfs &file_system = *static_cast<Procfs *>(context);
    if (!file_system.VnodeIsValid(directory)) {
        return Status::Corrupt;
    }
    if (directory.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    for (uint64_t node_index = OS_KERNEL_PROCFS_EMPTY_VALUE;
         node_index < OS_KERNEL_PROCFS_FILE_COUNT; ++node_index) {
        const ProcfsNodeDescription &description = OS_KERNEL_PROCFS_NODES[node_index];
        if (description.name_length_bytes == name_length_bytes &&
            BytesAreEqual(name, description.name, name_length_bytes)) {
            vnode = file_system.MakeVnode(
                static_cast<NodeKind>(node_index + OS_KERNEL_PROCFS_COUNTER_INCREMENT));
            return Status::Succeeded;
        }
    }
    return Status::NotFound;
}

Status Procfs::CreateOperation(void *const context, const Vnode &directory,
                               const uint8_t *const name, const uint64_t name_length_bytes,
                               const NodeType type, const NodeCreationAttributes &attributes,
                               Vnode &vnode) noexcept {
    static_cast<void>(context);
    static_cast<void>(directory);
    static_cast<void>(name);
    static_cast<void>(name_length_bytes);
    static_cast<void>(type);
    static_cast<void>(attributes);
    vnode = Vnode{};
    return Status::ReadOnly;
}

Status Procfs::OpenOperation(void *const context, const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Procfs &file_system = *static_cast<Procfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    SpinLockGuard guard{file_system.lock_};
    if (file_system.statistics_.active_open_count == OS_KERNEL_PROCFS_MAXIMUM_COUNTER_VALUE) {
        return Status::CapacityExhausted;
    }
    ++file_system.statistics_.active_open_count;
    IncrementSaturatingCounter(file_system.statistics_.successful_open_count);
    return Status::Succeeded;
}

Status Procfs::CloseOperation(void *const context, const Vnode &vnode) noexcept {
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Procfs &file_system = *static_cast<Procfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    SpinLockGuard guard{file_system.lock_};
    if (file_system.statistics_.active_open_count == OS_KERNEL_PROCFS_EMPTY_VALUE) {
        return Status::Corrupt;
    }
    --file_system.statistics_.active_open_count;
    return Status::Succeeded;
}

Status Procfs::RemoveOperation(void *const context, const Vnode &directory,
                               const uint8_t *const name, const uint64_t name_length_bytes,
                               const NodeType expected_type) noexcept {
    static_cast<void>(context);
    static_cast<void>(directory);
    static_cast<void>(name);
    static_cast<void>(name_length_bytes);
    static_cast<void>(expected_type);
    return Status::ReadOnly;
}

Status
Procfs::RenameOperation(void *const context, const Vnode &source_directory,
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

Status Procfs::ParentOperation(void *const context, const Vnode &vnode, Vnode &parent) noexcept {
    parent = Vnode{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Procfs &file_system = *static_cast<Procfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    parent = file_system.MakeVnode(NodeKind::Root);
    return Status::Succeeded;
}

Status Procfs::ReadOperation(void *const context, const Vnode &vnode, const uint64_t offset_bytes,
                             uint8_t *const destination, const uint64_t capacity_bytes,
                             uint64_t &read_bytes) noexcept {
    read_bytes = OS_KERNEL_PROCFS_EMPTY_VALUE;
    if (context == nullptr ||
        (destination == nullptr && capacity_bytes != OS_KERNEL_PROCFS_EMPTY_VALUE)) {
        return Status::InvalidArgument;
    }
    Procfs &file_system = *static_cast<Procfs *>(context);
    if (!file_system.VnodeIsValid(vnode) || vnode.type != NodeType::RegularFile) {
        return Status::InvalidHandle;
    }
    uint8_t snapshot_bytes[OS_KERNEL_PROCFS_MAXIMUM_SNAPSHOT_SIZE_BYTES]{};
    uint64_t snapshot_size_bytes = OS_KERNEL_PROCFS_EMPTY_VALUE;
    const Status render_status = file_system.Render(KindFromVnode(vnode), snapshot_bytes,
                                                    sizeof(snapshot_bytes), snapshot_size_bytes);
    if (render_status != Status::Succeeded) {
        return render_status;
    }
    if (offset_bytes >= snapshot_size_bytes) {
        return Status::Succeeded;
    }
    const uint64_t available_bytes = snapshot_size_bytes - offset_bytes;
    read_bytes = capacity_bytes < available_bytes ? capacity_bytes : available_bytes;
    CopyBytes(destination, snapshot_bytes + offset_bytes, read_bytes);
    {
        SpinLockGuard guard{file_system.lock_};
        IncrementSaturatingCounter(file_system.statistics_.snapshot_read_count);
        AddSaturatingCounter(file_system.statistics_.bytes_read, read_bytes);
    }
    return Status::Succeeded;
}

Status Procfs::WriteOperation(void *const context, const Vnode &vnode, const uint64_t offset_bytes,
                              const uint8_t *const source, const uint64_t length_bytes,
                              uint64_t &written_bytes) noexcept {
    static_cast<void>(context);
    static_cast<void>(vnode);
    static_cast<void>(offset_bytes);
    static_cast<void>(source);
    static_cast<void>(length_bytes);
    written_bytes = OS_KERNEL_PROCFS_EMPTY_VALUE;
    return Status::ReadOnly;
}

Status Procfs::TruncateOperation(void *const context, const Vnode &vnode,
                                 const uint64_t size_bytes) noexcept {
    static_cast<void>(context);
    static_cast<void>(vnode);
    static_cast<void>(size_bytes);
    return Status::ReadOnly;
}

Status Procfs::ReadDirectoryOperation(void *const context, const Vnode &directory, uint64_t &cursor,
                                      DirectoryEntry &entry, bool &end_of_directory) noexcept {
    entry = DirectoryEntry{};
    end_of_directory = false;
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Procfs &file_system = *static_cast<Procfs *>(context);
    if (!file_system.VnodeIsValid(directory)) {
        return Status::InvalidHandle;
    }
    if (directory.type != NodeType::Directory) {
        return Status::NotDirectory;
    }
    if (cursor >= OS_KERNEL_PROCFS_FILE_COUNT) {
        end_of_directory = true;
        return Status::Succeeded;
    }
    const ProcfsNodeDescription &description = OS_KERNEL_PROCFS_NODES[cursor];
    entry = DirectoryEntry{
        .node_identifier = OS_KERNEL_PROCFS_FIRST_FILE_NODE_IDENTIFIER + cursor,
        .type = NodeType::RegularFile,
        .name_length_bytes = description.name_length_bytes,
        .name = {},
    };
    CopyBytes(entry.name, reinterpret_cast<const uint8_t *>(description.name),
              description.name_length_bytes);
    ++cursor;
    {
        SpinLockGuard guard{file_system.lock_};
        IncrementSaturatingCounter(file_system.statistics_.directory_read_count);
    }
    return Status::Succeeded;
}

Status Procfs::GetNameOperation(void *const context, const Vnode &vnode, uint8_t *const name,
                                const uint64_t name_capacity_bytes,
                                uint64_t &name_length_bytes) noexcept {
    name_length_bytes = OS_KERNEL_PROCFS_EMPTY_VALUE;
    if (context == nullptr || name == nullptr) {
        return Status::InvalidArgument;
    }
    const Procfs &file_system = *static_cast<const Procfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    const NodeKind kind = KindFromVnode(vnode);
    if (kind == NodeKind::Root) {
        return Status::Succeeded;
    }
    const uint64_t node_index = static_cast<uint64_t>(kind) - OS_KERNEL_PROCFS_COUNTER_INCREMENT;
    const ProcfsNodeDescription &description = OS_KERNEL_PROCFS_NODES[node_index];
    if (name_capacity_bytes < description.name_length_bytes) {
        return Status::NameTooLong;
    }
    CopyBytes(name, reinterpret_cast<const uint8_t *>(description.name),
              description.name_length_bytes);
    name_length_bytes = description.name_length_bytes;
    return Status::Succeeded;
}

Status Procfs::StatOperation(void *const context, const Vnode &vnode,
                             BackendNodeInformation &information) noexcept {
    information = BackendNodeInformation{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    Procfs &file_system = *static_cast<Procfs *>(context);
    if (!file_system.VnodeIsValid(vnode)) {
        return Status::InvalidHandle;
    }
    uint64_t size_bytes = OS_KERNEL_PROCFS_EMPTY_VALUE;
    if (vnode.type == NodeType::RegularFile) {
        uint8_t snapshot_bytes[OS_KERNEL_PROCFS_MAXIMUM_SNAPSHOT_SIZE_BYTES]{};
        const Status render_status = file_system.Render(KindFromVnode(vnode), snapshot_bytes,
                                                        sizeof(snapshot_bytes), size_bytes);
        if (render_status != Status::Succeeded) {
            return render_status;
        }
    }
    information = BackendNodeInformation{
        .size_bytes = size_bytes,
        .allocated_size_bytes = OS_KERNEL_PROCFS_EMPTY_VALUE,
        .link_count = vnode.type == NodeType::Directory ? OS_KERNEL_PROCFS_ROOT_LINK_COUNT
                                                        : OS_KERNEL_PROCFS_FILE_LINK_COUNT,
        .access_time_nanoseconds = OS_KERNEL_PROCFS_EMPTY_VALUE,
        .modification_time_nanoseconds = OS_KERNEL_PROCFS_EMPTY_VALUE,
        .change_time_nanoseconds = OS_KERNEL_PROCFS_EMPTY_VALUE,
        .birth_time_nanoseconds = OS_KERNEL_PROCFS_EMPTY_VALUE,
        .owner_user_identifier = os::abi::OS_ABI_ROOT_USER_IDENTIFIER,
        .owner_group_identifier = os::abi::OS_ABI_ROOT_GROUP_IDENTIFIER,
        .mode = vnode.type == NodeType::Directory ? os::abi::OS_ABI_FILE_MODE_DIRECTORY | 0000555U
                                                  : os::abi::OS_ABI_FILE_MODE_REGULAR | 0000444U,
    };
    return Status::Succeeded;
}

Status Procfs::SyncOperation(void *const context) noexcept {
    return context == nullptr ? Status::InvalidArgument : Status::Succeeded;
}

Status Procfs::ValidateOperation(void *const context) noexcept {
    return context == nullptr ? Status::InvalidArgument
                              : static_cast<Procfs *>(context)->Validate();
}

Status Procfs::ReadResourceUsageOperation(void *const context, ResourceUsage &usage) noexcept {
    usage = ResourceUsage{};
    if (context == nullptr) {
        return Status::InvalidArgument;
    }
    const Procfs &file_system = *static_cast<const Procfs *>(context);
    if (file_system.Validate() != Status::Succeeded) {
        return Status::Corrupt;
    }
    usage.vnode_count = OS_KERNEL_PROCFS_ROOT_NODE_IDENTIFIER + OS_KERNEL_PROCFS_FILE_COUNT;
    return Status::Succeeded;
}

Status Procfs::CaptureSnapshot(ProcfsSnapshot &snapshot) noexcept {
    snapshot = ProcfsSnapshot{};
    if (this->snapshot_operation_ == nullptr ||
        !this->snapshot_operation_(this->snapshot_context_, snapshot)) {
        SpinLockGuard guard{this->lock_};
        IncrementSaturatingCounter(this->statistics_.snapshot_failure_count);
        return Status::DeviceFailure;
    }
    return Status::Succeeded;
}

Status Procfs::Render(const NodeKind kind, uint8_t *const destination,
                      const uint64_t capacity_bytes, uint64_t &rendered_bytes) noexcept {
    rendered_bytes = OS_KERNEL_PROCFS_EMPTY_VALUE;
    if (destination == nullptr || capacity_bytes < OS_KERNEL_PROCFS_MAXIMUM_SNAPSHOT_SIZE_BYTES ||
        kind == NodeKind::Root || static_cast<uint64_t>(kind) > OS_KERNEL_PROCFS_FILE_COUNT) {
        return Status::InvalidArgument;
    }
    ProcfsSnapshot snapshot{};
    const Status snapshot_status = this->CaptureSnapshot(snapshot);
    if (snapshot_status != Status::Succeeded) {
        return snapshot_status;
    }
    SnapshotWriter writer{destination, capacity_bytes};
    bool succeeded = false;
    if (kind == NodeKind::Version) {
        succeeded = writer.AppendLiteral(OS_KERNEL_PROCFS_VERSION_ABI_PREFIX) &&
                    writer.AppendLine(os::abi::OS_ABI_VERSION_MAJOR) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_VERSION_ABI_MINOR_PREFIX) &&
                    writer.AppendLine(os::abi::OS_ABI_VERSION_MINOR) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_VERSION_ARCHITECTURE);
    } else if (kind == NodeKind::Uptime) {
        succeeded = writer.AppendLiteral(OS_KERNEL_PROCFS_UPTIME_PREFIX) &&
                    writer.AppendLine(snapshot.monotonic_nanoseconds);
    } else if (kind == NodeKind::MemoryInformation) {
        succeeded = writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_MANAGED_PREFIX) &&
                    writer.AppendLine(snapshot.managed_memory_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_FREE_PREFIX) &&
                    writer.AppendLine(snapshot.free_memory_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_ALLOCATED_PREFIX) &&
                    writer.AppendLine(snapshot.allocated_memory_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_RESIDENT_LIMIT_PREFIX) &&
                    writer.AppendLine(snapshot.resident_limit_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_SWAP_TOTAL_PREFIX) &&
                    writer.AppendLine(snapshot.swap_total_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_SWAP_FREE_PREFIX) &&
                    writer.AppendLine(snapshot.swap_free_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_COMMITTED_PREFIX) &&
                    writer.AppendLine(snapshot.committed_memory_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_COMMIT_LIMIT_PREFIX) &&
                    writer.AppendLine(snapshot.commit_limit_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_MEMORY_OOM_KILL_COUNT_PREFIX) &&
                    writer.AppendLine(snapshot.oom_kill_count);
    } else if (kind == NodeKind::Processes) {
        succeeded = writer.AppendLiteral(OS_KERNEL_PROCFS_PROCESS_ACTIVE_PREFIX) &&
                    writer.AppendLine(snapshot.active_process_count) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_THREAD_ACTIVE_PREFIX) &&
                    writer.AppendLine(snapshot.active_thread_count) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_PROCESS_CAPACITY_PREFIX) &&
                    writer.AppendLine(snapshot.process_capacity) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_THREAD_CAPACITY_PREFIX) &&
                    writer.AppendLine(snapshot.thread_capacity) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_CURRENT_PROCESS_PREFIX) &&
                    writer.AppendLine(snapshot.current_process_id);
    } else if (kind == NodeKind::Resources) {
        succeeded = writer.AppendLiteral(OS_KERNEL_PROCFS_RESOURCE_HEAP_PREFIX) &&
                    writer.AppendLine(snapshot.heap_consumed_bytes) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_RESOURCE_FILE_DESCRIPTIONS_PREFIX) &&
                    writer.AppendLine(snapshot.active_file_description_count) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_RESOURCE_PIPES_PREFIX) &&
                    writer.AppendLine(snapshot.active_pipe_count) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_RESOURCE_VNODES_PREFIX) &&
                    writer.AppendLine(snapshot.vnode_count) &&
                    writer.AppendLiteral(OS_KERNEL_PROCFS_RESOURCE_JOURNAL_COMMITS_PREFIX) &&
                    writer.AppendLine(snapshot.journal_commit_count);
    } else if (kind == NodeKind::Mounts) {
        succeeded = writer.AppendLiteral(OS_KERNEL_PROCFS_MOUNT_COUNT_PREFIX) &&
                    writer.AppendLine(snapshot.mount_count);
    }
    if (!succeeded) {
        return Status::CapacityExhausted;
    }
    rendered_bytes = writer.SizeBytes();
    return Status::Succeeded;
}

Vnode Procfs::MakeVnode(const NodeKind kind) noexcept {
    return Vnode{
        .superblock = &this->superblock_,
        .identifier = kind == NodeKind::Root
                          ? OS_KERNEL_PROCFS_ROOT_NODE_IDENTIFIER
                          : OS_KERNEL_PROCFS_FIRST_FILE_NODE_IDENTIFIER +
                                static_cast<uint64_t>(kind) - OS_KERNEL_PROCFS_COUNTER_INCREMENT,
        .generation = OS_KERNEL_PROCFS_INITIAL_GENERATION,
        .type = kind == NodeKind::Root ? NodeType::Directory : NodeType::RegularFile,
    };
}

bool Procfs::VnodeIsValid(const Vnode &vnode) const noexcept {
    if (!this->initialized_ || vnode.superblock != &this->superblock_ ||
        vnode.generation != OS_KERNEL_PROCFS_INITIAL_GENERATION) {
        return false;
    }
    if (vnode.identifier == OS_KERNEL_PROCFS_ROOT_NODE_IDENTIFIER) {
        return vnode.type == NodeType::Directory;
    }
    return vnode.type == NodeType::RegularFile &&
           vnode.identifier >= OS_KERNEL_PROCFS_FIRST_FILE_NODE_IDENTIFIER &&
           vnode.identifier <
               OS_KERNEL_PROCFS_FIRST_FILE_NODE_IDENTIFIER + OS_KERNEL_PROCFS_FILE_COUNT;
}

Procfs::NodeKind Procfs::KindFromVnode(const Vnode &vnode) noexcept {
    return vnode.identifier == OS_KERNEL_PROCFS_ROOT_NODE_IDENTIFIER
               ? NodeKind::Root
               : static_cast<NodeKind>(vnode.identifier -
                                       OS_KERNEL_PROCFS_FIRST_FILE_NODE_IDENTIFIER +
                                       OS_KERNEL_PROCFS_COUNTER_INCREMENT);
}

}
