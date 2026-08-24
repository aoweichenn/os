#include <memory_block_device.hpp>
#include <os/kernel/fs/file_system.hpp>
#include <os/kernel/fs/legacy_file_system.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/io/file_description.hpp>
#include <os/kernel/io/file_table.hpp>
#include <os/kernel/ipc/pipe.hpp>
#include <os/kernel/memory/file_writeback_error_tracker.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <os/kernel/object/kernel_object.hpp>
#include <test_context.hpp>

#include <atomic>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view OS_TEST_FILE_DESCRIPTION_LIFECYCLE_SUITE_NAME =
    "kernel/file_description/lifecycle/integration";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_SHARED_OFFSET =
    "复制描述符必须共享文件偏移而独立打开必须拥有独立偏移";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_PIPE_LIFETIME =
    "管道端点只能在最后一个文件描述引用释放时关闭";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_APPEND_ATOMICITY =
    "独立 append 文件描述每次写入都必须重新定位到当前文件尾";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_ATOMICITY =
    "独立 append 文件描述并发写入不得覆盖或丢失任何记录";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_POSITIONED_IO =
    "seek 必须由 duplicate 共享而 pread/pwrite 不得修改共享 offset";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_STATUS_FLAGS =
    "file status flags 必须共享、保留 access mode 并只允许 writable regular append";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_WRITEBACK_ERROR_CURSOR =
    "duplicate 必须共享写回错误游标而独立打开保持自己的采样点";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_FINALIZATION =
    "文件表销毁后文件、管道、对象和堆资源必须全部归零";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_INVALID_DIRECTORY_CONFIGURATION =
    "目录描述必须拒绝与公开 flags 不一致的内部 OpenFile 能力";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_READAHEAD_OWNERSHIP =
    "缓存观测必须驱动真实预读决策，duplicate 共享策略而独立 open 隔离策略";

constexpr uint64_t OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_TABLE_LIMIT = 256ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_SECOND_OFFSET_BYTES = 4ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_ITERATION_COUNT = 64ULL;
constexpr int64_t OS_TEST_FILE_DESCRIPTION_NEGATIVE_TRANSFER_DISPLACEMENT_BYTES = -4LL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_WRITEBACK_ERROR_SEQUENCE = 1ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_FILE_DESCRIPTOR = 3ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_INDEPENDENT_FILE_DESCRIPTOR = 4ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_DUPLICATE_FILE_DESCRIPTOR = 5ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_PIPE_READER_DESCRIPTOR = 6ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_PIPE_WRITER_DESCRIPTOR = 7ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_DUPLICATE_PIPE_WRITER_DESCRIPTOR = 8ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_DESCRIPTOR = 9ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_INDEPENDENT_READAHEAD_FILE_DESCRIPTOR = 10ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_DUPLICATE_READAHEAD_FILE_DESCRIPTOR = 11ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_SIZE_BYTES =
    OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES + 510ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_LOGICAL_READAHEAD_FILE_SIZE_BYTES =
    OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES * 8ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_EXPECTED_READAHEAD_SCHEDULE_COUNT = 3ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_EXPECTED_READAHEAD_PAGE_COUNT = 10ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_EXPECTED_READAHEAD_GENERATION_SUM = 4ULL;
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_FILE_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('f'), static_cast<uint8_t>('d'),
    static_cast<uint8_t>('v'), static_cast<uint8_t>('1'), static_cast<uint8_t>('4'),
    static_cast<uint8_t>('.'), static_cast<uint8_t>('b'), static_cast<uint8_t>('i'),
    static_cast<uint8_t>('n'),
};
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('r'), static_cast<uint8_t>('e'),
    static_cast<uint8_t>('a'), static_cast<uint8_t>('d'), static_cast<uint8_t>('a'),
    static_cast<uint8_t>('h'), static_cast<uint8_t>('e'), static_cast<uint8_t>('a'),
    static_cast<uint8_t>('d'), static_cast<uint8_t>('.'), static_cast<uint8_t>('b'),
    static_cast<uint8_t>('i'), static_cast<uint8_t>('n'),
};
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_PAYLOAD[] = {
    static_cast<uint8_t>('A'), static_cast<uint8_t>('B'), static_cast<uint8_t>('C'),
    static_cast<uint8_t>('D'), static_cast<uint8_t>('E'), static_cast<uint8_t>('F'),
    static_cast<uint8_t>('G'), static_cast<uint8_t>('H'), static_cast<uint8_t>('I'),
    static_cast<uint8_t>('J'), static_cast<uint8_t>('K'), static_cast<uint8_t>('L'),
};
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_PIPE_PAYLOAD[] = {
    static_cast<uint8_t>('P'),
    static_cast<uint8_t>('I'),
    static_cast<uint8_t>('P'),
    static_cast<uint8_t>('E'),
};
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_FIRST_APPEND_PAYLOAD[] = {
    static_cast<uint8_t>('X'),
};
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_SECOND_APPEND_PAYLOAD[] = {
    static_cast<uint8_t>('Y'),
};
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_POSITIONED_APPEND_PAYLOAD[] = {
    static_cast<uint8_t>('Z'),
};
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_FIRST_CONCURRENT_APPEND_BYTE =
    static_cast<uint8_t>('M');
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_SECOND_CONCURRENT_APPEND_BYTE =
    static_cast<uint8_t>('N');
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_APPEND_PREFIX_SIZE_BYTES =
    sizeof(OS_TEST_FILE_DESCRIPTION_PAYLOAD) +
    sizeof(OS_TEST_FILE_DESCRIPTION_FIRST_APPEND_PAYLOAD) +
    sizeof(OS_TEST_FILE_DESCRIPTION_POSITIONED_APPEND_PAYLOAD) +
    sizeof(OS_TEST_FILE_DESCRIPTION_SECOND_APPEND_PAYLOAD);
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_SIZE_BYTES =
    OS_TEST_FILE_DESCRIPTION_APPEND_PREFIX_SIZE_BYTES +
    OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_ITERATION_COUNT * 2ULL;

os::kernel::FileWritebackErrorTracker *writeback_error_tracker;

struct ReadaheadTestContext final {
    os::kernel::fs::Vfs *vfs;
    uint64_t cache_read_count;
    uint64_t schedule_count;
    uint64_t scheduled_page_count;
    uint64_t generation_sum;
    uint64_t readahead_node_identifier;
    uint64_t next_stream_slot_index;
    uint64_t pending_useful_page_count;
    uint64_t pending_wasted_page_count;
    uint64_t cancellation_count;
    uint64_t retirement_count;
    os::kernel::MemoryPressureLevel pressure_level;
};

[[nodiscard]] os::kernel::fs::Status
ReadThroughCache(void *const context, const os::kernel::fs::OpenFile &open_file,
                 const uint64_t offset_bytes, uint8_t *const destination,
                 const uint64_t capacity_bytes, uint64_t &read_bytes,
                 os::kernel::fs::RegularFileReadCacheObservation &observation) noexcept {
    observation = os::kernel::fs::RegularFileReadCacheObservation{};
    if (context == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    ReadaheadTestContext &test_context = *static_cast<ReadaheadTestContext *>(context);
    os::kernel::fs::NodeInformation information{};
    if (test_context.vfs == nullptr ||
        test_context.vfs->StatOpenFileUncached(open_file, information) !=
            os::kernel::fs::Status::Succeeded) {
        return os::kernel::fs::Status::InvalidHandle;
    }
    const bool readahead_file =
        open_file.path.vnode.identifier == test_context.readahead_node_identifier;
    os::kernel::fs::Status status = os::kernel::fs::Status::Succeeded;
    if (readahead_file) {
        const uint64_t available_bytes =
            offset_bytes < OS_TEST_FILE_DESCRIPTION_LOGICAL_READAHEAD_FILE_SIZE_BYTES
                ? OS_TEST_FILE_DESCRIPTION_LOGICAL_READAHEAD_FILE_SIZE_BYTES - offset_bytes
                : OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
        read_bytes = capacity_bytes < available_bytes ? capacity_bytes : available_bytes;
        for (uint64_t byte_index = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE; byte_index < read_bytes;
             ++byte_index) {
            destination[byte_index] = static_cast<uint8_t>(byte_index & 0xFFULL);
        }
    } else {
        status = test_context.vfs->ReadUncachedAt(open_file, offset_bytes, destination,
                                                  capacity_bytes, read_bytes);
    }
    if (status != os::kernel::fs::Status::Succeeded) {
        return status;
    }
    ++test_context.cache_read_count;
    const uint64_t first_page_index = offset_bytes / OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES;
    const uint64_t first_page_offset =
        offset_bytes & (OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES - 1ULL);
    const uint64_t requested_page_count =
        read_bytes == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE
            ? OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE
            : (first_page_offset + read_bytes - 1ULL) / OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES +
                  1ULL;
    const uint64_t logical_size_bytes =
        readahead_file ? OS_TEST_FILE_DESCRIPTION_LOGICAL_READAHEAD_FILE_SIZE_BYTES
                       : information.size_bytes;
    const uint64_t file_page_count =
        logical_size_bytes / OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES +
        (logical_size_bytes % OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES != 0ULL ? 1ULL : 0ULL);
    const bool simulated_prefetched_hit = readahead_file && offset_bytes != 0ULL;
    if (simulated_prefetched_hit) {
        ++test_context.pending_useful_page_count;
    }
    observation = os::kernel::fs::RegularFileReadCacheObservation{
        .first_page_index = first_page_index,
        .requested_page_count = requested_page_count,
        .file_page_count = file_page_count,
        .cache_hit_page_count =
            simulated_prefetched_hit ? requested_page_count : OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .cache_miss_page_count =
            simulated_prefetched_hit ? OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE : requested_page_count,
        .prefetched_hit_page_count =
            simulated_prefetched_hit ? 1ULL : OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .cache_used = true,
    };
    return os::kernel::fs::Status::Succeeded;
}

[[nodiscard]] bool RegisterReadaheadStream(void *const context,
                                           const os::kernel::FileCacheIdentity &identity,
                                           os::kernel::FileReadaheadStreamToken &stream) noexcept {
    if (context == nullptr || !os::kernel::FileCacheIdentityIsValid(identity)) {
        return false;
    }
    ReadaheadTestContext &test_context = *static_cast<ReadaheadTestContext *>(context);
    stream = os::kernel::FileReadaheadStreamToken{
        .slot_index = test_context.next_stream_slot_index,
        .generation = 1ULL,
    };
    ++test_context.next_stream_slot_index;
    return true;
}

[[nodiscard]] bool TakeReadaheadFeedback(void *const context,
                                         const os::kernel::FileReadaheadStreamToken stream,
                                         os::kernel::FileReadaheadFeedback &feedback) noexcept {
    if (context == nullptr || !os::kernel::FileReadaheadStreamTokenIsValid(stream)) {
        return false;
    }
    ReadaheadTestContext &test_context = *static_cast<ReadaheadTestContext *>(context);
    feedback = os::kernel::FileReadaheadFeedback{
        .useful_page_count = test_context.pending_useful_page_count,
        .wasted_page_count = test_context.pending_wasted_page_count,
    };
    test_context.pending_useful_page_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    test_context.pending_wasted_page_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    return true;
}

[[nodiscard]] bool CancelReadahead(void *const context,
                                   const os::kernel::FileReadaheadStreamToken stream,
                                   const uint64_t maximum_policy_generation) noexcept {
    if (context == nullptr || !os::kernel::FileReadaheadStreamTokenIsValid(stream) ||
        maximum_policy_generation == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE) {
        return false;
    }
    ReadaheadTestContext &test_context = *static_cast<ReadaheadTestContext *>(context);
    ++test_context.cancellation_count;
    return true;
}

[[nodiscard]] bool
RetireReadaheadStream(void *const context,
                      const os::kernel::FileReadaheadStreamToken stream) noexcept {
    if (context == nullptr || !os::kernel::FileReadaheadStreamTokenIsValid(stream)) {
        return false;
    }
    ReadaheadTestContext &test_context = *static_cast<ReadaheadTestContext *>(context);
    ++test_context.retirement_count;
    return true;
}

[[nodiscard]] os::kernel::fs::Status
WriteThroughCache(void *const context, const os::kernel::fs::OpenFile &open_file,
                  const uint64_t offset_bytes, const uint8_t *const source,
                  const uint64_t length_bytes, uint64_t &written_bytes) noexcept {
    if (context == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    ReadaheadTestContext &test_context = *static_cast<ReadaheadTestContext *>(context);
    return test_context.vfs == nullptr
               ? os::kernel::fs::Status::InvalidArgument
               : test_context.vfs->WriteUncachedAt(open_file, offset_bytes, source, length_bytes,
                                                   written_bytes);
}

[[nodiscard]] os::kernel::fs::Status ResolveCachedSize(void *const context,
                                                       const os::kernel::fs::Vnode &vnode,
                                                       const uint64_t backend_size_bytes,
                                                       uint64_t &size_bytes) noexcept {
    if (context == nullptr || vnode.type != os::kernel::fs::NodeType::RegularFile) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    const ReadaheadTestContext &test_context = *static_cast<ReadaheadTestContext *>(context);
    size_bytes = vnode.identifier == test_context.readahead_node_identifier
                     ? OS_TEST_FILE_DESCRIPTION_LOGICAL_READAHEAD_FILE_SIZE_BYTES
                     : backend_size_bytes;
    return os::kernel::fs::Status::Succeeded;
}

[[nodiscard]] os::kernel::fs::Status RecordCachedTruncate(void *const context,
                                                          const os::kernel::fs::Vnode &vnode,
                                                          const uint64_t size_bytes) noexcept {
    static_cast<void>(size_bytes);
    return context != nullptr && vnode.type == os::kernel::fs::NodeType::RegularFile
               ? os::kernel::fs::Status::Succeeded
               : os::kernel::fs::Status::InvalidArgument;
}

[[nodiscard]] bool ReadReadaheadPressure(void *const context,
                                         os::kernel::MemoryPressureLevel &pressure_level) noexcept {
    if (context == nullptr) {
        return false;
    }
    pressure_level = static_cast<ReadaheadTestContext *>(context)->pressure_level;
    return true;
}

[[nodiscard]] bool ScheduleReadahead(void *const context, os::kernel::fs::Vfs &vfs,
                                     const os::kernel::fs::OpenFile &open_file,
                                     const os::kernel::FileReadaheadStreamToken stream,
                                     const os::kernel::FileReadaheadDecision &decision) noexcept {
    if (context == nullptr) {
        return false;
    }
    ReadaheadTestContext &test_context = *static_cast<ReadaheadTestContext *>(context);
    if (test_context.vfs != &vfs || !open_file.open || !open_file.readable ||
        !os::kernel::FileReadaheadStreamTokenIsValid(stream) ||
        decision.action != os::kernel::FileReadaheadAction::Submit ||
        decision.prefetch_page_count == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE ||
        decision.generation == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE) {
        return false;
    }
    ++test_context.schedule_count;
    test_context.scheduled_page_count += decision.prefetch_page_count;
    test_context.generation_sum += decision.generation;
    return true;
}

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RegisterWritebackDescription(const os::kernel::FileCacheIdentity &identity,
                                                uint64_t &sampled_sequence) noexcept {
    return writeback_error_tracker != nullptr &&
           writeback_error_tracker->Register(identity, sampled_sequence) ==
               os::kernel::FileWritebackErrorTrackerStatus::Succeeded;
}

[[nodiscard]] bool
UnregisterWritebackDescription(const os::kernel::FileCacheIdentity &identity) noexcept {
    return writeback_error_tracker != nullptr &&
           writeback_error_tracker->Unregister(identity) ==
               os::kernel::FileWritebackErrorTrackerStatus::Succeeded;
}

[[nodiscard]] os::kernel::FileDescriptionStatus
CreateFileDescription(os::kernel::FileDescriptionManager &manager, os::kernel::fs::Vfs &vfs,
                      const os::kernel::fs::OpenFile &open_file,
                      os::kernel::KernelObjectReference &reference,
                      const bool append = false) noexcept {
    uint64_t file_status_flags = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    if (open_file.readable) {
        file_status_flags |= os::kernel::OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG;
    }
    if (open_file.writable) {
        file_status_flags |= os::kernel::OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG;
    }
    if (append) {
        file_status_flags |= os::kernel::OS_KERNEL_FILE_DESCRIPTION_APPEND_STATUS_FLAG;
    }
    const os::kernel::FileDescriptionCreateRequest request{
        .kind = os::kernel::FileDescriptionKind::RegularFile,
        .file_status_flags = file_status_flags,
        .terminal = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .pipe_manager = nullptr,
        .vfs = &vfs,
        .open_file = open_file,
        .writeback_identity =
            os::kernel::FileCacheIdentity{
                .superblock_identifier = open_file.path.vnode.superblock->identifier,
                .superblock_generation = open_file.path.vnode.superblock->generation,
                .node_identifier = open_file.path.vnode.identifier,
                .node_generation = open_file.path.vnode.generation,
            },
        .writeback_error_register_operation = RegisterWritebackDescription,
        .writeback_error_unregister_operation = UnregisterWritebackDescription,
    };
    return manager.Create(request, reference);
}

[[nodiscard]] os::kernel::FileDescriptionStatus
CreatePipeDescription(os::kernel::FileDescriptionManager &manager, os::kernel::Pipe &pipe,
                      const os::kernel::FileDescriptionKind kind,
                      os::kernel::KernelObjectReference &reference) noexcept {
    const bool reader = kind == os::kernel::FileDescriptionKind::PipeReader;
    const os::kernel::FileDescriptionCreateRequest request{
        .kind = kind,
        .file_status_flags = reader ? os::kernel::OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG
                                    : os::kernel::OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG,
        .terminal = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = &pipe,
        .pipe_manager = nullptr,
        .vfs = nullptr,
        .open_file = {},
    };
    return manager.Create(request, reference);
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_FILE_DESCRIPTION_LIFECYCLE_SUITE_NAME};
    static os::test::MemoryBlockDevice device{};
    static os::kernel::FileSystem file_system{};
    alignas(64) static uint8_t heap_buffer[OS_TEST_FILE_DESCRIPTION_HEAP_SIZE_BYTES]{};
    static uint8_t readahead_payload[OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_SIZE_BYTES]{};
    for (uint64_t byte_index = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
         byte_index < sizeof(readahead_payload); ++byte_index) {
        readahead_payload[byte_index] = static_cast<uint8_t>(byte_index & 0xFFULL);
    }

    bool formatted = false;
    os::kernel::FileSystemHandle write_handle{};
    os::kernel::FileSystemHandle readahead_write_handle{};
    const os::kernel::FileSystemOpenOptions write_options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = true,
    };
    uint64_t written_bytes = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    const bool file_prepared =
        file_system.MountOrFormat(device, formatted) == os::kernel::FileSystemStatus::Succeeded &&
        file_system.Open(OS_TEST_FILE_DESCRIPTION_FILE_PATH,
                         sizeof(OS_TEST_FILE_DESCRIPTION_FILE_PATH), write_options,
                         write_handle) == os::kernel::FileSystemStatus::Succeeded &&
        file_system.Write(write_handle, OS_TEST_FILE_DESCRIPTION_PAYLOAD,
                          sizeof(OS_TEST_FILE_DESCRIPTION_PAYLOAD),
                          written_bytes) == os::kernel::FileSystemStatus::Succeeded &&
        written_bytes == sizeof(OS_TEST_FILE_DESCRIPTION_PAYLOAD) &&
        file_system.Close(write_handle) == os::kernel::FileSystemStatus::Succeeded &&
        file_system.Open(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_PATH,
                         sizeof(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_PATH), write_options,
                         readahead_write_handle) == os::kernel::FileSystemStatus::Succeeded &&
        file_system.Write(readahead_write_handle, readahead_payload, sizeof(readahead_payload),
                          written_bytes) == os::kernel::FileSystemStatus::Succeeded &&
        written_bytes == sizeof(readahead_payload) &&
        file_system.Close(readahead_write_handle) == os::kernel::FileSystemStatus::Succeeded;

    os::kernel::fs::LegacyFileSystem legacy_adapter{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::Mount mounts[OS_TEST_FILE_DESCRIPTION_MOUNT_CAPACITY]{};
    os::kernel::fs::FsContext file_system_context{};
    os::kernel::fs::OpenFile shared_open_file{};
    os::kernel::fs::OpenFile independent_open_file{};
    ReadaheadTestContext readahead_test_context{
        .vfs = &vfs,
        .cache_read_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .schedule_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .scheduled_page_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .generation_sum = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .readahead_node_identifier = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .next_stream_slot_index = 1ULL,
        .pending_useful_page_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .pending_wasted_page_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .cancellation_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .retirement_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
        .pressure_level = os::kernel::MemoryPressureLevel::Balanced,
    };
    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    const bool handles_opened =
        file_prepared &&
        legacy_adapter.Initialize(file_system, OS_TEST_FILE_DESCRIPTION_SUPERBLOCK_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_FILE_DESCRIPTION_MOUNT_CAPACITY,
                       legacy_adapter.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureRegularFileDataCache(
            &readahead_test_context, ReadThroughCache, WriteThroughCache, ResolveCachedSize,
            RecordCachedTruncate) == os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(file_system_context) == os::kernel::fs::Status::Succeeded &&
        vfs.Open(file_system_context, OS_TEST_FILE_DESCRIPTION_FILE_PATH,
                 sizeof(OS_TEST_FILE_DESCRIPTION_FILE_PATH), read_options,
                 shared_open_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Open(file_system_context, OS_TEST_FILE_DESCRIPTION_FILE_PATH,
                 sizeof(OS_TEST_FILE_DESCRIPTION_FILE_PATH), read_options,
                 independent_open_file) == os::kernel::fs::Status::Succeeded;

    os::kernel::KernelHeap heap{};
    os::kernel::KernelObjectManager object_manager{};
    os::kernel::FileDescriptionManager description_manager{};
    os::kernel::FileWritebackErrorTracker error_tracker{};
    os::kernel::FileTable table{};
    writeback_error_tracker = &error_tracker;
    const bool object_model_initialized =
        handles_opened &&
        heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                        OS_TEST_FILE_DESCRIPTION_HEAP_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        error_tracker.Initialize(heap) == os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        object_manager.Initialize(heap) == os::kernel::KernelObjectStatus::Succeeded &&
        description_manager.Initialize(object_manager) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        description_manager.ConfigureReadahead(os::kernel::FileDescriptionReadaheadOperations{
            .context = &readahead_test_context,
            .register_stream = RegisterReadaheadStream,
            .take_feedback = TakeReadaheadFeedback,
            .cancel = CancelReadahead,
            .retire_stream = RetireReadaheadStream,
            .pressure = ReadReadaheadPressure,
            .schedule = ScheduleReadahead,
        }) == os::kernel::FileDescriptionStatus::Succeeded &&
        table.Initialize(heap, object_manager, OS_TEST_FILE_DESCRIPTION_TABLE_LIMIT,
                         OS_TEST_FILE_DESCRIPTION_TABLE_LIMIT) ==
            os::kernel::FileTableStatus::Succeeded;
    const os::kernel::FileDescriptionCreateRequest invalid_directory_request{
        .kind = os::kernel::FileDescriptionKind::Directory,
        .file_status_flags = os::kernel::OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
        .terminal = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .pipe_manager = nullptr,
        .vfs = &vfs,
        .open_file =
            os::kernel::fs::OpenFile{
                .path = file_system_context.root,
                .offset_bytes = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
                .readable = false,
                .writable = false,
                .open = true,
            },
    };
    os::kernel::KernelObjectReference invalid_directory_reference{};
    test_context.Expect(
        object_model_initialized &&
            description_manager.Create(invalid_directory_request, invalid_directory_reference) ==
                os::kernel::FileDescriptionStatus::InvalidConfiguration,
        OS_TEST_FILE_DESCRIPTION_INVALID_DIRECTORY_CONFIGURATION);

    os::kernel::KernelObjectReference shared_reference{};
    os::kernel::KernelObjectReference independent_reference{};
    const bool descriptions_installed =
        object_model_initialized &&
        CreateFileDescription(description_manager, vfs, shared_open_file, shared_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.InstallExact(shared_reference, OS_TEST_FILE_DESCRIPTION_FILE_DESCRIPTOR,
                           OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE) ==
            os::kernel::FileTableStatus::Succeeded &&
        CreateFileDescription(description_manager, vfs, independent_open_file,
                              independent_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.InstallExact(
            independent_reference, OS_TEST_FILE_DESCRIPTION_INDEPENDENT_FILE_DESCRIPTOR,
            OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE) == os::kernel::FileTableStatus::Succeeded;
    uint64_t duplicate_file_descriptor = UINT64_MAX;
    const bool file_duplicated =
        descriptions_installed &&
        table.Duplicate(OS_TEST_FILE_DESCRIPTION_FILE_DESCRIPTOR,
                        OS_TEST_FILE_DESCRIPTION_DUPLICATE_FILE_DESCRIPTOR,
                        os::kernel::OS_KERNEL_FILE_DESCRIPTOR_CLOSE_ON_EXEC_FLAG,
                        duplicate_file_descriptor) == os::kernel::FileTableStatus::Succeeded &&
        duplicate_file_descriptor == OS_TEST_FILE_DESCRIPTION_DUPLICATE_FILE_DESCRIPTOR;

    os::kernel::KernelObjectReference cursor_reference{};
    uint64_t shared_writeback_cursor = UINT64_MAX;
    uint64_t duplicate_writeback_cursor = UINT64_MAX;
    uint64_t independent_writeback_cursor = UINT64_MAX;
    uint64_t current_writeback_sequence = UINT64_MAX;
    os::kernel::FileWritebackError writeback_error = os::kernel::FileWritebackError::None;
    const os::kernel::FileCacheIdentity writeback_identity{
        .superblock_identifier = shared_open_file.path.vnode.superblock->identifier,
        .superblock_generation = shared_open_file.path.vnode.superblock->generation,
        .node_identifier = shared_open_file.path.vnode.identifier,
        .node_generation = shared_open_file.path.vnode.generation,
    };
    const bool writeback_cursor_shared =
        file_duplicated &&
        error_tracker.Record(writeback_identity, os::kernel::FileWritebackError::InputOutput) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_FILE_DESCRIPTOR, cursor_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.ReadWritebackErrorCursor(cursor_reference, shared_writeback_cursor) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        shared_writeback_cursor == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        error_tracker.Check(writeback_identity, shared_writeback_cursor, current_writeback_sequence,
                            writeback_error) ==
            os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        current_writeback_sequence == OS_TEST_FILE_DESCRIPTION_WRITEBACK_ERROR_SEQUENCE &&
        writeback_error == os::kernel::FileWritebackError::InputOutput &&
        description_manager.AdvanceWritebackErrorCursor(cursor_reference,
                                                        current_writeback_sequence) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        cursor_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_DUPLICATE_FILE_DESCRIPTOR, cursor_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.ReadWritebackErrorCursor(cursor_reference,
                                                     duplicate_writeback_cursor) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        duplicate_writeback_cursor == OS_TEST_FILE_DESCRIPTION_WRITEBACK_ERROR_SEQUENCE &&
        cursor_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_INDEPENDENT_FILE_DESCRIPTOR, cursor_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.ReadWritebackErrorCursor(cursor_reference,
                                                     independent_writeback_cursor) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        independent_writeback_cursor == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        cursor_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    test_context.Expect(writeback_cursor_shared, OS_TEST_FILE_DESCRIPTION_WRITEBACK_ERROR_CURSOR);

    uint8_t first_bytes[OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES]{};
    uint8_t second_bytes[OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES]{};
    uint8_t independent_bytes[OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES]{};
    uint64_t read_bytes = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    os::kernel::FileSystemStatus file_system_status = os::kernel::FileSystemStatus::Succeeded;
    os::kernel::PipeStatus pipe_status = os::kernel::PipeStatus::Succeeded;
    os::kernel::KernelObjectReference operation_reference{};
    bool shared_offset_valid =
        file_duplicated &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_FILE_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, first_bytes, sizeof(first_bytes),
                                    read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes == sizeof(first_bytes) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_DUPLICATE_FILE_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, second_bytes, sizeof(second_bytes),
                                    read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes == sizeof(second_bytes) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_INDEPENDENT_FILE_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, independent_bytes,
                                    sizeof(independent_bytes), read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes == sizeof(independent_bytes) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    shared_offset_valid =
        shared_offset_valid &&
        BytesEqual(first_bytes, OS_TEST_FILE_DESCRIPTION_PAYLOAD, sizeof(first_bytes)) &&
        BytesEqual(second_bytes,
                   OS_TEST_FILE_DESCRIPTION_PAYLOAD + OS_TEST_FILE_DESCRIPTION_SECOND_OFFSET_BYTES,
                   sizeof(second_bytes)) &&
        BytesEqual(independent_bytes, OS_TEST_FILE_DESCRIPTION_PAYLOAD, sizeof(independent_bytes));
    test_context.Expect(shared_offset_valid, OS_TEST_FILE_DESCRIPTION_SHARED_OFFSET);

    uint8_t positioned_bytes[OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES]{};
    uint64_t positioned_offset_bytes = UINT64_MAX;
    os::kernel::FileDescriptionSnapshot positioned_snapshot{};
    uint64_t shared_status_flags = UINT64_MAX;
    const bool positioned_io_valid =
        table.Lookup(OS_TEST_FILE_DESCRIPTION_DUPLICATE_FILE_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.Seek(
            operation_reference, OS_TEST_FILE_DESCRIPTION_NEGATIVE_TRANSFER_DISPLACEMENT_BYTES,
            os::kernel::FileSeekOrigin::Current, positioned_offset_bytes,
            file_system_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        positioned_offset_bytes == OS_TEST_FILE_DESCRIPTION_SECOND_OFFSET_BYTES &&
        description_manager.TryReadAt(
            operation_reference, OS_TEST_FILE_DESCRIPTION_SECOND_OFFSET_BYTES, positioned_bytes,
            sizeof(positioned_bytes), read_bytes,
            file_system_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes == sizeof(positioned_bytes) &&
        BytesEqual(positioned_bytes,
                   OS_TEST_FILE_DESCRIPTION_PAYLOAD + OS_TEST_FILE_DESCRIPTION_SECOND_OFFSET_BYTES,
                   sizeof(positioned_bytes)) &&
        description_manager.ReadSnapshot(operation_reference, positioned_snapshot) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        positioned_snapshot.offset_bytes == OS_TEST_FILE_DESCRIPTION_SECOND_OFFSET_BYTES &&
        description_manager.Seek(operation_reference, -1LL, os::kernel::FileSeekOrigin::Beginning,
                                 positioned_offset_bytes, file_system_status) ==
            os::kernel::FileDescriptionStatus::InvalidArgument &&
        description_manager.GetStatusFlags(operation_reference, shared_status_flags) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        shared_status_flags == os::kernel::OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG &&
        description_manager.SetStatusFlags(
            operation_reference,
            shared_status_flags | os::kernel::OS_KERNEL_FILE_DESCRIPTION_APPEND_STATUS_FLAG) ==
            os::kernel::FileDescriptionStatus::InvalidArgument &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    test_context.Expect(positioned_io_valid, OS_TEST_FILE_DESCRIPTION_POSITIONED_IO);

    os::kernel::fs::OpenFile readahead_open_file{};
    os::kernel::fs::OpenFile independent_readahead_open_file{};
    os::kernel::KernelObjectReference readahead_reference{};
    os::kernel::KernelObjectReference independent_readahead_reference{};
    uint64_t duplicate_readahead_descriptor = UINT64_MAX;
    uint8_t readahead_bytes[OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES]{};
    uint8_t readahead_page_bytes[OS_TEST_FILE_DESCRIPTION_PAGE_SIZE_BYTES]{};
    const bool readahead_files_opened =
        vfs.Open(file_system_context, OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_PATH,
                 sizeof(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_PATH), read_options,
                 readahead_open_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Open(file_system_context, OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_PATH,
                 sizeof(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_PATH), read_options,
                 independent_readahead_open_file) == os::kernel::fs::Status::Succeeded;
    if (readahead_files_opened) {
        readahead_test_context.readahead_node_identifier =
            readahead_open_file.path.vnode.identifier;
    }
    const bool readahead_descriptions_installed =
        readahead_files_opened &&
        CreateFileDescription(description_manager, vfs, readahead_open_file, readahead_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.InstallExact(readahead_reference, OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_DESCRIPTOR,
                           OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE) ==
            os::kernel::FileTableStatus::Succeeded &&
        CreateFileDescription(description_manager, vfs, independent_readahead_open_file,
                              independent_readahead_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.InstallExact(independent_readahead_reference,
                           OS_TEST_FILE_DESCRIPTION_INDEPENDENT_READAHEAD_FILE_DESCRIPTOR,
                           OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE) ==
            os::kernel::FileTableStatus::Succeeded &&
        table.Duplicate(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_DESCRIPTOR,
                        OS_TEST_FILE_DESCRIPTION_DUPLICATE_READAHEAD_FILE_DESCRIPTOR,
                        OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
                        duplicate_readahead_descriptor) == os::kernel::FileTableStatus::Succeeded &&
        duplicate_readahead_descriptor ==
            OS_TEST_FILE_DESCRIPTION_DUPLICATE_READAHEAD_FILE_DESCRIPTOR;
    bool readahead_reads_completed =
        readahead_descriptions_installed &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, readahead_bytes, sizeof(readahead_bytes),
                                    read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes == sizeof(readahead_bytes) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_DUPLICATE_READAHEAD_FILE_DESCRIPTOR,
                     operation_reference) == os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, readahead_page_bytes,
                                    sizeof(readahead_page_bytes), read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes == sizeof(readahead_page_bytes) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, readahead_page_bytes,
                                    sizeof(readahead_page_bytes) -
                                        OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES + 1ULL,
                                    read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes ==
            sizeof(readahead_page_bytes) - OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES + 1ULL &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    readahead_test_context.pressure_level = os::kernel::MemoryPressureLevel::BelowMinimum;
    readahead_reads_completed =
        readahead_reads_completed &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, readahead_page_bytes,
                                    sizeof(readahead_page_bytes), read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes == sizeof(readahead_page_bytes) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    readahead_test_context.pressure_level = os::kernel::MemoryPressureLevel::Balanced;
    readahead_reads_completed =
        readahead_reads_completed &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_INDEPENDENT_READAHEAD_FILE_DESCRIPTOR,
                     operation_reference) == os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, readahead_bytes, sizeof(readahead_bytes),
                                    read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        read_bytes == sizeof(readahead_bytes) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    os::kernel::FileDescriptionSnapshot shared_readahead_snapshot{};
    os::kernel::FileDescriptionSnapshot duplicate_readahead_snapshot{};
    os::kernel::FileDescriptionSnapshot independent_readahead_snapshot{};
    readahead_test_context.pending_wasted_page_count = 4ULL;
    readahead_reads_completed =
        readahead_reads_completed &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_READAHEAD_FILE_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.ReadSnapshot(operation_reference, shared_readahead_snapshot) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_DUPLICATE_READAHEAD_FILE_DESCRIPTOR,
                     operation_reference) == os::kernel::FileTableStatus::Succeeded &&
        description_manager.ReadSnapshot(operation_reference, duplicate_readahead_snapshot) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_INDEPENDENT_READAHEAD_FILE_DESCRIPTOR,
                     operation_reference) == os::kernel::FileTableStatus::Succeeded &&
        description_manager.ReadSnapshot(operation_reference, independent_readahead_snapshot) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    const os::kernel::FileDescriptionManagerStatistics readahead_statistics =
        description_manager.Statistics();
    const bool readahead_ownership_valid =
        readahead_reads_completed && shared_readahead_snapshot.readahead.access_count == 4ULL &&
        shared_readahead_snapshot.readahead.prefetched_hit_access_count == 1ULL &&
        shared_readahead_snapshot.readahead.demand_hit_access_count == 2ULL &&
        shared_readahead_snapshot.readahead.pressure_disabled_access_count == 1ULL &&
        !shared_readahead_snapshot.readahead.window_active &&
        shared_readahead_snapshot.readahead.adaptive_maximum_window_page_count == 16ULL &&
        duplicate_readahead_snapshot.readahead.access_count ==
            shared_readahead_snapshot.readahead.access_count &&
        duplicate_readahead_snapshot.readahead.generation ==
            shared_readahead_snapshot.readahead.generation &&
        os::kernel::FileReadaheadStreamTokensEqual(duplicate_readahead_snapshot.readahead_stream,
                                                   shared_readahead_snapshot.readahead_stream) &&
        !os::kernel::FileReadaheadStreamTokensEqual(independent_readahead_snapshot.readahead_stream,
                                                    shared_readahead_snapshot.readahead_stream) &&
        independent_readahead_snapshot.readahead.access_count == 1ULL &&
        independent_readahead_snapshot.readahead.adaptive_maximum_window_page_count ==
            os::kernel::OS_KERNEL_FILE_READAHEAD_DEFAULT_MAXIMUM_WINDOW_PAGE_COUNT &&
        readahead_test_context.schedule_count ==
            OS_TEST_FILE_DESCRIPTION_EXPECTED_READAHEAD_SCHEDULE_COUNT &&
        readahead_test_context.scheduled_page_count ==
            OS_TEST_FILE_DESCRIPTION_EXPECTED_READAHEAD_PAGE_COUNT &&
        readahead_test_context.generation_sum ==
            OS_TEST_FILE_DESCRIPTION_EXPECTED_READAHEAD_GENERATION_SUM &&
        readahead_statistics.readahead_schedule_count ==
            OS_TEST_FILE_DESCRIPTION_EXPECTED_READAHEAD_SCHEDULE_COUNT &&
        readahead_statistics.readahead_useful_page_count == 3ULL &&
        readahead_statistics.readahead_wasted_page_count == 4ULL &&
        readahead_statistics.readahead_feedback_application_count == 4ULL &&
        readahead_statistics.readahead_schedule_rejection_count ==
            OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    test_context.Expect(readahead_ownership_valid, OS_TEST_FILE_DESCRIPTION_READAHEAD_OWNERSHIP);

    os::kernel::fs::OpenFile first_append_open_file{};
    os::kernel::fs::OpenFile second_append_open_file{};
    const os::kernel::fs::OpenOptions append_options{
        .readable = false,
        .writable = true,
        .create = false,
        .truncate = false,
        .append = true,
    };
    os::kernel::KernelObjectReference first_append_reference{};
    os::kernel::KernelObjectReference second_append_reference{};
    uint64_t append_written_bytes = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    uint64_t append_status_flags = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    os::kernel::FileDescriptionSnapshot first_append_snapshot{};
    os::kernel::FileDescriptionSnapshot second_append_snapshot{};
    const bool append_atomic =
        vfs.Open(file_system_context, OS_TEST_FILE_DESCRIPTION_FILE_PATH,
                 sizeof(OS_TEST_FILE_DESCRIPTION_FILE_PATH), append_options,
                 first_append_open_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Open(file_system_context, OS_TEST_FILE_DESCRIPTION_FILE_PATH,
                 sizeof(OS_TEST_FILE_DESCRIPTION_FILE_PATH), append_options,
                 second_append_open_file) == os::kernel::fs::Status::Succeeded &&
        CreateFileDescription(description_manager, vfs, first_append_open_file,
                              first_append_reference,
                              true) == os::kernel::FileDescriptionStatus::Succeeded &&
        CreateFileDescription(description_manager, vfs, second_append_open_file,
                              second_append_reference,
                              true) == os::kernel::FileDescriptionStatus::Succeeded &&
        description_manager.GetStatusFlags(first_append_reference, append_status_flags) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        append_status_flags == (os::kernel::OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG |
                                os::kernel::OS_KERNEL_FILE_DESCRIPTION_APPEND_STATUS_FLAG) &&
        description_manager.SetStatusFlags(
            first_append_reference, os::kernel::OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        description_manager.SetStatusFlags(
            first_append_reference,
            os::kernel::OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG |
                os::kernel::OS_KERNEL_FILE_DESCRIPTION_APPEND_STATUS_FLAG) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        description_manager.TryWrite(
            first_append_reference, OS_TEST_FILE_DESCRIPTION_FIRST_APPEND_PAYLOAD,
            sizeof(OS_TEST_FILE_DESCRIPTION_FIRST_APPEND_PAYLOAD), UINT64_MAX, append_written_bytes,
            file_system_status, pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        append_written_bytes == sizeof(OS_TEST_FILE_DESCRIPTION_FIRST_APPEND_PAYLOAD) &&
        description_manager.TryWriteAt(first_append_reference, OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
                                       OS_TEST_FILE_DESCRIPTION_POSITIONED_APPEND_PAYLOAD,
                                       sizeof(OS_TEST_FILE_DESCRIPTION_POSITIONED_APPEND_PAYLOAD),
                                       UINT64_MAX, append_written_bytes, file_system_status) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        append_written_bytes == sizeof(OS_TEST_FILE_DESCRIPTION_POSITIONED_APPEND_PAYLOAD) &&
        description_manager.TryWrite(second_append_reference,
                                     OS_TEST_FILE_DESCRIPTION_SECOND_APPEND_PAYLOAD,
                                     sizeof(OS_TEST_FILE_DESCRIPTION_SECOND_APPEND_PAYLOAD),
                                     UINT64_MAX, append_written_bytes, file_system_status,
                                     pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        append_written_bytes == sizeof(OS_TEST_FILE_DESCRIPTION_SECOND_APPEND_PAYLOAD) &&
        description_manager.ReadSnapshot(first_append_reference, first_append_snapshot) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        description_manager.ReadSnapshot(second_append_reference, second_append_snapshot) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        first_append_snapshot.offset_bytes ==
            sizeof(OS_TEST_FILE_DESCRIPTION_PAYLOAD) +
                sizeof(OS_TEST_FILE_DESCRIPTION_FIRST_APPEND_PAYLOAD) &&
        second_append_snapshot.offset_bytes ==
            sizeof(OS_TEST_FILE_DESCRIPTION_PAYLOAD) +
                sizeof(OS_TEST_FILE_DESCRIPTION_FIRST_APPEND_PAYLOAD) +
                sizeof(OS_TEST_FILE_DESCRIPTION_POSITIONED_APPEND_PAYLOAD) +
                sizeof(OS_TEST_FILE_DESCRIPTION_SECOND_APPEND_PAYLOAD) &&
        first_append_snapshot.size_bytes == second_append_snapshot.offset_bytes &&
        second_append_snapshot.size_bytes == second_append_snapshot.offset_bytes &&
        first_append_snapshot.writeback_error_cursor ==
            OS_TEST_FILE_DESCRIPTION_WRITEBACK_ERROR_SEQUENCE &&
        second_append_snapshot.writeback_error_cursor ==
            OS_TEST_FILE_DESCRIPTION_WRITEBACK_ERROR_SEQUENCE;
    test_context.Expect(append_atomic, OS_TEST_FILE_DESCRIPTION_APPEND_ATOMICITY);
    test_context.Expect(append_atomic, OS_TEST_FILE_DESCRIPTION_STATUS_FLAGS);

    std::atomic<bool> start_concurrent_append{false};
    std::atomic<uint64_t> concurrent_append_failure_count{OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE};
    const auto append_worker = [&description_manager, &start_concurrent_append,
                                &concurrent_append_failure_count](
                                   const os::kernel::KernelObjectReference *const reference,
                                   const uint8_t append_byte) noexcept {
        while (!start_concurrent_append.load(std::memory_order_acquire)) {
        }
        for (uint64_t iteration = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
             iteration < OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_ITERATION_COUNT; ++iteration) {
            uint64_t written_bytes = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
            os::kernel::FileSystemStatus worker_file_system_status =
                os::kernel::FileSystemStatus::Succeeded;
            os::kernel::PipeStatus worker_pipe_status = os::kernel::PipeStatus::Succeeded;
            if (reference == nullptr ||
                description_manager.TryWrite(*reference, &append_byte, sizeof(append_byte),
                                             UINT64_MAX, written_bytes,
                                             worker_file_system_status,
                                             worker_pipe_status) !=
                    os::kernel::FileDescriptionStatus::Succeeded ||
                written_bytes != sizeof(append_byte)) {
                concurrent_append_failure_count.fetch_add(1ULL, std::memory_order_relaxed);
                return;
            }
        }
    };
    std::thread first_append_thread{append_worker, &first_append_reference,
                                    OS_TEST_FILE_DESCRIPTION_FIRST_CONCURRENT_APPEND_BYTE};
    std::thread second_append_thread{append_worker, &second_append_reference,
                                     OS_TEST_FILE_DESCRIPTION_SECOND_CONCURRENT_APPEND_BYTE};
    start_concurrent_append.store(true, std::memory_order_release);
    first_append_thread.join();
    second_append_thread.join();

    const os::kernel::fs::OpenOptions append_verification_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile append_verification_open_file{};
    uint8_t append_verification_bytes[OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_SIZE_BYTES]{};
    uint64_t append_verification_read_bytes = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    uint64_t first_concurrent_append_byte_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    uint64_t second_concurrent_append_byte_count = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    bool concurrent_append_atomic =
        concurrent_append_failure_count.load(std::memory_order_relaxed) ==
            OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        vfs.Open(file_system_context, OS_TEST_FILE_DESCRIPTION_FILE_PATH,
                 sizeof(OS_TEST_FILE_DESCRIPTION_FILE_PATH), append_verification_options,
                 append_verification_open_file) == os::kernel::fs::Status::Succeeded &&
        vfs.ReadAt(append_verification_open_file, OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE,
                   append_verification_bytes, sizeof(append_verification_bytes),
                   append_verification_read_bytes) == os::kernel::fs::Status::Succeeded &&
        append_verification_read_bytes == sizeof(append_verification_bytes);
    for (uint64_t byte_index = OS_TEST_FILE_DESCRIPTION_APPEND_PREFIX_SIZE_BYTES;
         concurrent_append_atomic && byte_index < sizeof(append_verification_bytes); ++byte_index) {
        if (append_verification_bytes[byte_index] ==
            OS_TEST_FILE_DESCRIPTION_FIRST_CONCURRENT_APPEND_BYTE) {
            ++first_concurrent_append_byte_count;
        } else if (append_verification_bytes[byte_index] ==
                   OS_TEST_FILE_DESCRIPTION_SECOND_CONCURRENT_APPEND_BYTE) {
            ++second_concurrent_append_byte_count;
        } else {
            concurrent_append_atomic = false;
        }
    }
    const bool append_verification_close_succeeded =
        !append_verification_open_file.open ||
        vfs.Close(append_verification_open_file) == os::kernel::fs::Status::Succeeded;
    const bool first_append_reference_released =
        first_append_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    const bool second_append_reference_released =
        second_append_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    concurrent_append_atomic =
        concurrent_append_atomic &&
        first_concurrent_append_byte_count ==
            OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_ITERATION_COUNT &&
        second_concurrent_append_byte_count ==
            OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_ITERATION_COUNT &&
        append_verification_close_succeeded && first_append_reference_released &&
        second_append_reference_released;
    test_context.Expect(concurrent_append_atomic,
                        OS_TEST_FILE_DESCRIPTION_CONCURRENT_APPEND_ATOMICITY);

    os::kernel::Pipe pipe{};
    pipe.Initialize();
    os::kernel::KernelObjectReference pipe_reader_reference{};
    os::kernel::KernelObjectReference pipe_writer_reference{};
    uint64_t duplicate_pipe_writer_descriptor = UINT64_MAX;
    const bool pipe_descriptions_installed =
        CreatePipeDescription(description_manager, pipe,
                              os::kernel::FileDescriptionKind::PipeReader, pipe_reader_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.InstallExact(pipe_reader_reference, OS_TEST_FILE_DESCRIPTION_PIPE_READER_DESCRIPTOR,
                           OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE) ==
            os::kernel::FileTableStatus::Succeeded &&
        CreatePipeDescription(description_manager, pipe,
                              os::kernel::FileDescriptionKind::PipeWriter, pipe_writer_reference) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.InstallExact(pipe_writer_reference, OS_TEST_FILE_DESCRIPTION_PIPE_WRITER_DESCRIPTOR,
                           OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE) ==
            os::kernel::FileTableStatus::Succeeded &&
        table.Duplicate(OS_TEST_FILE_DESCRIPTION_PIPE_WRITER_DESCRIPTOR,
                        OS_TEST_FILE_DESCRIPTION_DUPLICATE_PIPE_WRITER_DESCRIPTOR,
                        OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE, duplicate_pipe_writer_descriptor) ==
            os::kernel::FileTableStatus::Succeeded &&
        duplicate_pipe_writer_descriptor ==
            OS_TEST_FILE_DESCRIPTION_DUPLICATE_PIPE_WRITER_DESCRIPTOR;

    os::kernel::KernelObjectReleaseResult release_result{};
    uint64_t pipe_written_bytes = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    uint64_t pipe_read_bytes = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    uint8_t pipe_output[sizeof(OS_TEST_FILE_DESCRIPTION_PIPE_PAYLOAD)]{};
    const bool first_writer_reference_preserved =
        pipe_descriptions_installed &&
        table.Close(OS_TEST_FILE_DESCRIPTION_PIPE_WRITER_DESCRIPTOR, release_result) ==
            os::kernel::FileTableStatus::Succeeded &&
        !release_result.released_last_reference && !pipe.Statistics().writer_closed &&
        table.Lookup(duplicate_pipe_writer_descriptor, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryWrite(operation_reference, OS_TEST_FILE_DESCRIPTION_PIPE_PAYLOAD,
                                     sizeof(OS_TEST_FILE_DESCRIPTION_PIPE_PAYLOAD), UINT64_MAX,
                                     pipe_written_bytes, file_system_status,
                                     pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        pipe_written_bytes == sizeof(OS_TEST_FILE_DESCRIPTION_PIPE_PAYLOAD) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_PIPE_READER_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, pipe_output, sizeof(pipe_output),
                                    pipe_read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::Succeeded &&
        pipe_read_bytes == sizeof(pipe_output) &&
        BytesEqual(pipe_output, OS_TEST_FILE_DESCRIPTION_PIPE_PAYLOAD, sizeof(pipe_output)) &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    const bool last_writer_closed =
        table.Close(duplicate_pipe_writer_descriptor, release_result) ==
            os::kernel::FileTableStatus::Succeeded &&
        release_result.released_last_reference && pipe.Statistics().writer_closed &&
        table.Lookup(OS_TEST_FILE_DESCRIPTION_PIPE_READER_DESCRIPTOR, operation_reference) ==
            os::kernel::FileTableStatus::Succeeded &&
        description_manager.TryRead(operation_reference, pipe_output, sizeof(pipe_output),
                                    pipe_read_bytes, file_system_status,
                                    pipe_status) == os::kernel::FileDescriptionStatus::EndOfFile &&
        operation_reference.Reset() == os::kernel::KernelObjectStatus::Succeeded;
    test_context.Expect(first_writer_reference_preserved && last_writer_closed,
                        OS_TEST_FILE_DESCRIPTION_PIPE_LIFETIME);

    const bool finalized =
        table.Close(OS_TEST_FILE_DESCRIPTION_PIPE_READER_DESCRIPTOR, release_result) ==
            os::kernel::FileTableStatus::Succeeded &&
        table.Destroy() == os::kernel::FileTableStatus::Succeeded &&
        object_manager.Validate() == os::kernel::KernelObjectStatus::Succeeded &&
        object_manager.Statistics().active_object_count == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        description_manager.Statistics().failed_finalization_count ==
            OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        readahead_test_context.cancellation_count != OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        readahead_test_context.cancellation_count ==
            readahead_test_context.retirement_count + 1ULL &&
        readahead_test_context.pending_useful_page_count == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        readahead_test_context.pending_wasted_page_count == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        error_tracker.Validate() == os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        error_tracker.Statistics().active_record_count == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        error_tracker.Destroy() == os::kernel::FileWritebackErrorTrackerStatus::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        file_system.CheckConsistency() == os::kernel::FileSystemStatus::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded;
    test_context.Expect(finalized, OS_TEST_FILE_DESCRIPTION_FINALIZATION);
    writeback_error_tracker = nullptr;
    return test_context.ExitCode();
}
