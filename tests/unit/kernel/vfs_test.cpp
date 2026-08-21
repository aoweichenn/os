#include <os/kernel/fs/memfs.hpp>
#include <os/kernel/fs/vfs.hpp>
#include <os/kernel/memory/kernel_heap.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_SUITE_NAME = "kernel/vfs/unit";
constexpr std::string_view OS_TEST_VFS_PATH_SEMANTICS =
    "绝对路径、相对路径、重复分隔符、点组件和根目录钳制必须一致";
constexpr std::string_view OS_TEST_VFS_BOUNDARIES =
    "4096 字节路径与 255 字节名称必须精确接受且越界必须显式拒绝";
constexpr std::string_view OS_TEST_VFS_MOUNT_TRAVERSAL =
    "挂载点进入、挂载根返回父目录和工作目录重建必须保持命名空间连续";
constexpr std::string_view OS_TEST_VFS_FILE_IO =
    "打开状态必须维护独立偏移并通过 VFS 完成读写和目录枚举";
constexpr std::string_view OS_TEST_VFS_RETAINED_FILE =
    "保留的文件引用必须独立于原句柄，并支持不改变偏移的定位读取";
constexpr std::string_view OS_TEST_VFS_READ_CACHE_BOUNDARY =
    "VFS 公共读取必须进入缓存 hook，后端填页读取必须显式绕过以避免递归";
constexpr std::string_view OS_TEST_VFS_DATA_CACHE_BOUNDARY =
    "VFS 公共写入、长度与 truncate 必须进入数据缓存 hook 且 uncached 写入不得递归";
constexpr std::string_view OS_TEST_VFS_CONTEXT_CLONE =
    "fork 文件系统上下文必须继承根与工作目录且后续切换互不影响";
constexpr std::string_view OS_TEST_VFS_CYCLE_AND_CAPACITY =
    "挂载环与挂载表溢出必须返回独立错误而不能静默截断";
constexpr std::string_view OS_TEST_VFS_VALIDATION =
    "VFS、两个 memfs 后端和堆资源必须在完整生命周期内保持一致";

constexpr uint64_t OS_TEST_VFS_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VFS_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_VFS_ALTERNATING_COMPONENT_PERIOD = 2ULL;
constexpr uint64_t OS_TEST_VFS_ROOT_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_VFS_CHILD_SUPERBLOCK_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_VFS_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_HEAP_SIZE_BYTES = 2ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_ROOT_NODE_LIMIT = 64ULL;
constexpr uint64_t OS_TEST_VFS_CHILD_NODE_LIMIT = 32ULL;
constexpr uint64_t OS_TEST_VFS_MAXIMUM_FILE_SIZE_BYTES = 64ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_MOUNT_CAPACITY = 8ULL;
constexpr uint64_t OS_TEST_VFS_CONSTRAINED_MOUNT_CAPACITY = 1ULL;
constexpr uint64_t OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_VFS_EXPECTED_ROOT_LENGTH_BYTES = 1ULL;
constexpr uint64_t OS_TEST_VFS_EXPECTED_ALPHA_BETA_LENGTH_BYTES = 11ULL;
constexpr uint64_t OS_TEST_VFS_EXPECTED_MOUNT_LENGTH_BYTES = 4ULL;
constexpr uint64_t OS_TEST_VFS_INSUFFICIENT_PATH_CAPACITY_BYTES = 4ULL;
constexpr uint64_t OS_TEST_VFS_LONG_NAME_LENGTH_BYTES = 255ULL;
constexpr uint64_t OS_TEST_VFS_TOO_LONG_NAME_LENGTH_BYTES = 256ULL;
constexpr uint64_t OS_TEST_VFS_TOO_LONG_PATH_LENGTH_BYTES =
    os::kernel::fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES + OS_TEST_VFS_COUNTER_INCREMENT;
constexpr uint8_t OS_TEST_VFS_UNCHANGED_OUTPUT_BYTE = 0xA5U;
constexpr uint8_t OS_TEST_VFS_ALPHA_PATH[] = {'/', 'a', 'l', 'p', 'h', 'a'};
constexpr uint8_t OS_TEST_VFS_BETA_RELATIVE_PATH[] = {'b', 'e', 't', 'a'};
constexpr uint8_t OS_TEST_VFS_ALPHA_BETA_PATH[] = {
    '/', 'a', 'l', 'p', 'h', 'a', '/', 'b', 'e', 't', 'a',
};
constexpr uint8_t OS_TEST_VFS_COMPLEX_ROOT_PATH[] = {
    '.', '/', '.', '.', '/', '.', '.', '/', '.', '.',
};
constexpr uint8_t OS_TEST_VFS_REPEATED_SEPARATOR_PATH[] = {
    '/', '/', 'a', 'l', 'p', 'h', 'a', '/', '/', 'b', 'e', 't', 'a', '/',
};
constexpr uint8_t OS_TEST_VFS_MESSAGE_RELATIVE_PATH[] = {
    '.', '/', 'm', 'e', 's', 's', 'a', 'g', 'e',
};
constexpr uint8_t OS_TEST_VFS_MESSAGE_ABSOLUTE_PATH[] = {
    '/', 'a', 'l', 'p', 'h', 'a', '/', 'b', 'e', 't', 'a', '/', 'm', 'e', 's', 's', 'a', 'g', 'e',
};
constexpr uint8_t OS_TEST_VFS_MESSAGE_TRAILING_SEPARATOR_PATH[] = {
    '/', 'a', 'l', 'p', 'h', 'a', '/', 'b', 'e', 't',
    'a', '/', 'm', 'e', 's', 's', 'a', 'g', 'e', '/',
};
constexpr uint8_t OS_TEST_VFS_MISSING_FILE_TRAILING_SEPARATOR_PATH[] = {
    '/', 'a', 'l', 'p', 'h', 'a', '/', 'b', 'e', 't',
    'a', '/', 'm', 'i', 's', 's', 'i', 'n', 'g', '/',
};
constexpr uint8_t OS_TEST_VFS_MOUNT_PATH[] = {'/', 'm', 'n', 't'};
constexpr uint8_t OS_TEST_VFS_MOUNT_CHILD_PATH[] = {
    '/', 'm', 'n', 't', '/', 'c', 'h', 'i', 'l', 'd',
};
constexpr uint8_t OS_TEST_VFS_PARENT_PATH[] = {'.', '.'};
constexpr uint8_t OS_TEST_VFS_PAYLOAD[] = {
    'v', 'f', 's', '-', 'o', 'f', 'f', 's', 'e', 't',
};
constexpr uint8_t OS_TEST_VFS_EXPECTED_ALPHA_BETA[] = {
    '/', 'a', 'l', 'p', 'h', 'a', '/', 'b', 'e', 't', 'a',
};
constexpr uint8_t OS_TEST_VFS_EXPECTED_MOUNT[] = {'/', 'm', 'n', 't'};
constexpr uint8_t OS_TEST_VFS_EXPECTED_ROOT[] = {'/'};
constexpr uint8_t OS_TEST_VFS_ROOT_PATH[] = {'/'};

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_VFS_EMPTY_VALUE; byte_index < length_bytes; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

struct DataCacheContext final {
    os::kernel::fs::Vfs *vfs;
    uint64_t read_invocation_count;
    uint64_t write_invocation_count;
    uint64_t size_invocation_count;
    uint64_t truncate_invocation_count;
};

[[nodiscard]] os::kernel::fs::Status ReadThroughCache(
    void *const context, const os::kernel::fs::OpenFile &open_file,
    const uint64_t offset_bytes, uint8_t *const destination, const uint64_t capacity_bytes,
    uint64_t &read_bytes) noexcept {
    if (context == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    DataCacheContext &cache_context = *static_cast<DataCacheContext *>(context);
    if (cache_context.vfs == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    ++cache_context.read_invocation_count;
    return cache_context.vfs->ReadUncachedAt(open_file, offset_bytes, destination, capacity_bytes,
                                              read_bytes);
}

[[nodiscard]] os::kernel::fs::Status WriteThroughCache(
    void *const context, const os::kernel::fs::OpenFile &open_file,
    const uint64_t offset_bytes, const uint8_t *const source, const uint64_t length_bytes,
    uint64_t &written_bytes) noexcept {
    if (context == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    DataCacheContext &cache_context = *static_cast<DataCacheContext *>(context);
    if (cache_context.vfs == nullptr) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    ++cache_context.write_invocation_count;
    return cache_context.vfs->WriteUncachedAt(open_file, offset_bytes, source, length_bytes,
                                               written_bytes);
}

[[nodiscard]] os::kernel::fs::Status ResolveSizeThroughCache(
    void *const context, const os::kernel::fs::Vnode &vnode,
    const uint64_t backend_size_bytes, uint64_t &size_bytes) noexcept {
    if (context == nullptr || vnode.type != os::kernel::fs::NodeType::RegularFile) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    DataCacheContext &cache_context = *static_cast<DataCacheContext *>(context);
    ++cache_context.size_invocation_count;
    size_bytes = backend_size_bytes;
    return os::kernel::fs::Status::Succeeded;
}

[[nodiscard]] os::kernel::fs::Status RecordCachedTruncate(
    void *const context, const os::kernel::fs::Vnode &vnode,
    const uint64_t size_bytes) noexcept {
    static_cast<void>(size_bytes);
    if (context == nullptr || vnode.type != os::kernel::fs::NodeType::RegularFile) {
        return os::kernel::fs::Status::InvalidArgument;
    }
    DataCacheContext &cache_context = *static_cast<DataCacheContext *>(context);
    ++cache_context.truncate_invocation_count;
    return os::kernel::fs::Status::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_SUITE_NAME};
    alignas(
        OS_TEST_VFS_HEAP_ALIGNMENT_BYTES) static uint8_t heap_buffer[OS_TEST_VFS_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs root_memfs{};
    os::kernel::fs::Memfs child_memfs{};
    os::kernel::fs::Mount mounts[OS_TEST_VFS_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};

    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer), sizeof(heap_buffer)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        root_memfs.Initialize(heap, OS_TEST_VFS_ROOT_SUPERBLOCK_IDENTIFIER,
                              OS_TEST_VFS_ROOT_NODE_LIMIT, OS_TEST_VFS_MAXIMUM_FILE_SIZE_BYTES) ==
            os::kernel::fs::Status::Succeeded &&
        child_memfs.Initialize(heap, OS_TEST_VFS_CHILD_SUPERBLOCK_IDENTIFIER,
                               OS_TEST_VFS_CHILD_NODE_LIMIT, OS_TEST_VFS_MAXIMUM_FILE_SIZE_BYTES) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_VFS_MOUNT_CAPACITY, root_memfs.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded;

    os::kernel::fs::Path resolved_path{};
    uint8_t working_directory[os::kernel::fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES]{};
    uint64_t working_directory_length_bytes = OS_TEST_VFS_EMPTY_VALUE;
    const bool path_semantics_valid =
        initialized &&
        vfs.CreateDirectory(context, OS_TEST_VFS_ALPHA_PATH, sizeof(OS_TEST_VFS_ALPHA_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ChangeDirectory(context, OS_TEST_VFS_ALPHA_PATH, sizeof(OS_TEST_VFS_ALPHA_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, OS_TEST_VFS_BETA_RELATIVE_PATH,
                            sizeof(OS_TEST_VFS_BETA_RELATIVE_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ChangeDirectory(context, OS_TEST_VFS_BETA_RELATIVE_PATH,
                            sizeof(OS_TEST_VFS_BETA_RELATIVE_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Resolve(context, OS_TEST_VFS_REPEATED_SEPARATOR_PATH,
                    sizeof(OS_TEST_VFS_REPEATED_SEPARATOR_PATH),
                    resolved_path) == os::kernel::fs::Status::Succeeded &&
        resolved_path.vnode.type == os::kernel::fs::NodeType::Directory;
    const bool working_directory_valid =
        path_semantics_valid &&
        vfs.GetWorkingDirectory(context, working_directory, sizeof(working_directory),
                                working_directory_length_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        working_directory_length_bytes == OS_TEST_VFS_EXPECTED_ALPHA_BETA_LENGTH_BYTES &&
        BytesEqual(working_directory, OS_TEST_VFS_EXPECTED_ALPHA_BETA,
                   working_directory_length_bytes) &&
        vfs.Resolve(context, OS_TEST_VFS_COMPLEX_ROOT_PATH, sizeof(OS_TEST_VFS_COMPLEX_ROOT_PATH),
                    resolved_path) == os::kernel::fs::Status::Succeeded &&
        resolved_path.mount_identifier == context.root.mount_identifier &&
        resolved_path.vnode.identifier == context.root.vnode.identifier;
    test_context.Expect(working_directory_valid, OS_TEST_VFS_PATH_SEMANTICS);

    os::kernel::fs::FsContext cloned_context{};
    uint8_t cloned_working_directory
        [os::kernel::fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES]{};
    uint64_t cloned_working_directory_length_bytes =
        OS_TEST_VFS_EMPTY_VALUE;
    const bool context_clone_valid =
        working_directory_valid &&
        vfs.CloneContext(context, cloned_context) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.GetWorkingDirectory(
            cloned_context, cloned_working_directory,
            sizeof(cloned_working_directory),
            cloned_working_directory_length_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        cloned_working_directory_length_bytes ==
            OS_TEST_VFS_EXPECTED_ALPHA_BETA_LENGTH_BYTES &&
        BytesEqual(
            cloned_working_directory,
            OS_TEST_VFS_EXPECTED_ALPHA_BETA,
            cloned_working_directory_length_bytes) &&
        vfs.ChangeDirectory(
            cloned_context, OS_TEST_VFS_COMPLEX_ROOT_PATH,
            sizeof(OS_TEST_VFS_COMPLEX_ROOT_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.GetWorkingDirectory(
            context, working_directory,
            sizeof(working_directory),
            working_directory_length_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        working_directory_length_bytes ==
            OS_TEST_VFS_EXPECTED_ALPHA_BETA_LENGTH_BYTES &&
        BytesEqual(
            working_directory, OS_TEST_VFS_EXPECTED_ALPHA_BETA,
            working_directory_length_bytes);
    test_context.Expect(context_clone_valid,
                        OS_TEST_VFS_CONTEXT_CLONE);

    uint8_t maximum_path[os::kernel::fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES]{};
    maximum_path[OS_TEST_VFS_EMPTY_VALUE] = static_cast<uint8_t>('/');
    for (uint64_t byte_index = OS_TEST_VFS_COUNTER_INCREMENT; byte_index < sizeof(maximum_path);
         ++byte_index) {
        maximum_path[byte_index] =
            (byte_index % OS_TEST_VFS_ALTERNATING_COMPONENT_PERIOD) == OS_TEST_VFS_COUNTER_INCREMENT
                ? static_cast<uint8_t>('.')
                : static_cast<uint8_t>('/');
    }
    uint8_t too_long_path[OS_TEST_VFS_TOO_LONG_PATH_LENGTH_BYTES]{};
    for (uint64_t byte_index = OS_TEST_VFS_EMPTY_VALUE; byte_index < sizeof(maximum_path);
         ++byte_index) {
        too_long_path[byte_index] = maximum_path[byte_index];
    }
    too_long_path[sizeof(too_long_path) - OS_TEST_VFS_COUNTER_INCREMENT] =
        static_cast<uint8_t>('/');
    uint8_t insufficient_path_output[OS_TEST_VFS_INSUFFICIENT_PATH_CAPACITY_BYTES]{};
    for (uint64_t byte_index = OS_TEST_VFS_EMPTY_VALUE;
         byte_index < sizeof(insufficient_path_output); ++byte_index) {
        insufficient_path_output[byte_index] = OS_TEST_VFS_UNCHANGED_OUTPUT_BYTE;
    }
    uint64_t insufficient_path_length_bytes = UINT64_MAX;
    const os::kernel::fs::Status insufficient_path_status =
        vfs.GetWorkingDirectory(context, insufficient_path_output, sizeof(insufficient_path_output),
                                insufficient_path_length_bytes);
    bool insufficient_output_unchanged = true;
    for (uint64_t byte_index = OS_TEST_VFS_EMPTY_VALUE;
         byte_index < sizeof(insufficient_path_output); ++byte_index) {
        if (insufficient_path_output[byte_index] != OS_TEST_VFS_UNCHANGED_OUTPUT_BYTE) {
            insufficient_output_unchanged = false;
        }
    }
    uint8_t maximum_name_path[OS_TEST_VFS_LONG_NAME_LENGTH_BYTES + OS_TEST_VFS_COUNTER_INCREMENT]{};
    uint8_t too_long_name_path[OS_TEST_VFS_TOO_LONG_NAME_LENGTH_BYTES +
                               OS_TEST_VFS_COUNTER_INCREMENT]{};
    maximum_name_path[OS_TEST_VFS_EMPTY_VALUE] = static_cast<uint8_t>('/');
    too_long_name_path[OS_TEST_VFS_EMPTY_VALUE] = static_cast<uint8_t>('/');
    for (uint64_t name_index = OS_TEST_VFS_EMPTY_VALUE;
         name_index < OS_TEST_VFS_TOO_LONG_NAME_LENGTH_BYTES; ++name_index) {
        if (name_index < OS_TEST_VFS_LONG_NAME_LENGTH_BYTES) {
            maximum_name_path[name_index + OS_TEST_VFS_COUNTER_INCREMENT] =
                static_cast<uint8_t>('a');
        }
        too_long_name_path[name_index + OS_TEST_VFS_COUNTER_INCREMENT] = static_cast<uint8_t>('b');
    }
    const bool boundaries_valid =
        vfs.Resolve(context, maximum_path, sizeof(maximum_path), resolved_path) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Resolve(context, too_long_path, sizeof(too_long_path), resolved_path) ==
            os::kernel::fs::Status::PathTooLong &&
        insufficient_path_status == os::kernel::fs::Status::PathTooLong &&
        insufficient_path_length_bytes == OS_TEST_VFS_EMPTY_VALUE &&
        insufficient_output_unchanged &&
        vfs.CreateDirectory(context, maximum_name_path, sizeof(maximum_name_path)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.CreateDirectory(context, too_long_name_path, sizeof(too_long_name_path)) ==
            os::kernel::fs::Status::NameTooLong;
    test_context.Expect(boundaries_valid, OS_TEST_VFS_BOUNDARIES);

    const os::kernel::fs::OpenOptions write_options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = true,
        .append = false,
    };
    os::kernel::fs::OpenFile write_file{};
    uint64_t written_bytes = OS_TEST_VFS_EMPTY_VALUE;
    const bool file_written =
        vfs.ChangeDirectory(context, OS_TEST_VFS_ALPHA_BETA_PATH,
                            sizeof(OS_TEST_VFS_ALPHA_BETA_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Open(context, OS_TEST_VFS_MESSAGE_RELATIVE_PATH,
                 sizeof(OS_TEST_VFS_MESSAGE_RELATIVE_PATH), write_options,
                 write_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Write(write_file, OS_TEST_VFS_PAYLOAD, sizeof(OS_TEST_VFS_PAYLOAD), written_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        written_bytes == sizeof(OS_TEST_VFS_PAYLOAD) &&
        vfs.Close(write_file) == os::kernel::fs::Status::Succeeded;
    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::OpenFile read_file{};
    os::kernel::fs::OpenFile retained_file{};
    uint8_t payload[sizeof(OS_TEST_VFS_PAYLOAD)]{};
    uint8_t retained_payload[sizeof(OS_TEST_VFS_PAYLOAD)]{};
    uint8_t uncached_payload[sizeof(OS_TEST_VFS_PAYLOAD)]{};
    uint8_t policy_bypass_payload[sizeof(OS_TEST_VFS_PAYLOAD)]{};
    uint64_t read_bytes = OS_TEST_VFS_EMPTY_VALUE;
    uint64_t retained_read_bytes = OS_TEST_VFS_EMPTY_VALUE;
    os::kernel::fs::NodeInformation retained_information{};
    DataCacheContext data_cache_context{
        .vfs = &vfs,
        .read_invocation_count = OS_TEST_VFS_EMPTY_VALUE,
        .write_invocation_count = OS_TEST_VFS_EMPTY_VALUE,
        .size_invocation_count = OS_TEST_VFS_EMPTY_VALUE,
        .truncate_invocation_count = OS_TEST_VFS_EMPTY_VALUE,
    };
    const bool file_read =
        file_written &&
        vfs.Open(context, OS_TEST_VFS_MESSAGE_ABSOLUTE_PATH,
                 sizeof(OS_TEST_VFS_MESSAGE_ABSOLUTE_PATH), read_options,
                 read_file) == os::kernel::fs::Status::Succeeded &&
        vfs.Read(read_file, payload, sizeof(payload), read_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        read_bytes == sizeof(payload) &&
        BytesEqual(payload, OS_TEST_VFS_PAYLOAD, sizeof(payload));
    root_memfs.GetSuperblock().cache_regular_file_data = true;
    const bool retained_file_valid =
        file_read &&
        vfs.RetainOpenFile(read_file, retained_file) ==
            os::kernel::fs::Status::Succeeded &&
        retained_file.offset_bytes == OS_TEST_VFS_EMPTY_VALUE &&
        vfs.StatOpenFile(retained_file, retained_information) ==
            os::kernel::fs::Status::Succeeded &&
        retained_information.superblock_identifier ==
            read_file.path.vnode.superblock->identifier &&
        retained_information.node_identifier ==
            read_file.path.vnode.identifier &&
        retained_information.generation ==
            read_file.path.vnode.generation &&
        retained_information.size_bytes == sizeof(OS_TEST_VFS_PAYLOAD) &&
        vfs.Close(read_file) == os::kernel::fs::Status::Succeeded &&
        vfs.ConfigureRegularFileDataCache(&data_cache_context, ReadThroughCache,
                                          WriteThroughCache, ResolveSizeThroughCache,
                                          RecordCachedTruncate) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ReadAt(retained_file, OS_TEST_VFS_EMPTY_VALUE,
                   retained_payload, sizeof(retained_payload),
                   retained_read_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        retained_read_bytes == sizeof(retained_payload) &&
        retained_file.offset_bytes == OS_TEST_VFS_EMPTY_VALUE &&
        BytesEqual(retained_payload, OS_TEST_VFS_PAYLOAD,
                   sizeof(retained_payload));
    root_memfs.GetSuperblock().cache_regular_file_data = false;
    const bool read_cache_boundary_valid =
        retained_file_valid &&
        data_cache_context.read_invocation_count == OS_TEST_VFS_COUNTER_INCREMENT &&
        vfs.ReadUncachedAt(retained_file, OS_TEST_VFS_EMPTY_VALUE, uncached_payload,
                           sizeof(uncached_payload), retained_read_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        retained_read_bytes == sizeof(uncached_payload) &&
        BytesEqual(uncached_payload, OS_TEST_VFS_PAYLOAD, sizeof(uncached_payload)) &&
        data_cache_context.read_invocation_count == OS_TEST_VFS_COUNTER_INCREMENT &&
        vfs.ReadAt(retained_file, OS_TEST_VFS_EMPTY_VALUE, policy_bypass_payload,
                   sizeof(policy_bypass_payload), retained_read_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        retained_read_bytes == sizeof(policy_bypass_payload) &&
        BytesEqual(policy_bypass_payload, OS_TEST_VFS_PAYLOAD,
                   sizeof(policy_bypass_payload)) &&
        data_cache_context.read_invocation_count == OS_TEST_VFS_COUNTER_INCREMENT &&
        vfs.ConfigureRegularFileDataCache(&data_cache_context, ReadThroughCache,
                                          WriteThroughCache, ResolveSizeThroughCache,
                                          RecordCachedTruncate) ==
            os::kernel::fs::Status::AlreadyExists &&
        vfs.Close(retained_file) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(retained_file_valid, OS_TEST_VFS_RETAINED_FILE);
    test_context.Expect(read_cache_boundary_valid, OS_TEST_VFS_READ_CACHE_BOUNDARY);
    const os::kernel::fs::OpenOptions cache_write_options{
        .readable = false,
        .writable = true,
        .create = false,
        .truncate = false,
        .append = false,
    };
    os::kernel::fs::NodeInformation cached_information{};
    root_memfs.GetSuperblock().cache_regular_file_data = true;
    const bool data_cache_boundary_valid =
        read_cache_boundary_valid &&
        vfs.Open(context, OS_TEST_VFS_MESSAGE_ABSOLUTE_PATH,
                 sizeof(OS_TEST_VFS_MESSAGE_ABSOLUTE_PATH), cache_write_options, write_file) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.WriteAt(write_file, OS_TEST_VFS_EMPTY_VALUE, OS_TEST_VFS_PAYLOAD,
                    sizeof(OS_TEST_VFS_PAYLOAD), written_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        written_bytes == sizeof(OS_TEST_VFS_PAYLOAD) &&
        data_cache_context.write_invocation_count == OS_TEST_VFS_COUNTER_INCREMENT &&
        vfs.WriteUncachedAt(write_file, OS_TEST_VFS_EMPTY_VALUE, OS_TEST_VFS_PAYLOAD,
                            sizeof(OS_TEST_VFS_PAYLOAD), written_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        data_cache_context.write_invocation_count == OS_TEST_VFS_COUNTER_INCREMENT &&
        vfs.ReadUncachedAt(write_file, OS_TEST_VFS_EMPTY_VALUE, uncached_payload,
                           sizeof(uncached_payload), retained_read_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        retained_read_bytes == sizeof(uncached_payload) &&
        BytesEqual(uncached_payload, OS_TEST_VFS_PAYLOAD, sizeof(uncached_payload)) &&
        vfs.TruncateOpenFile(write_file, sizeof(OS_TEST_VFS_PAYLOAD)) ==
            os::kernel::fs::Status::Succeeded &&
        data_cache_context.truncate_invocation_count == OS_TEST_VFS_COUNTER_INCREMENT &&
        vfs.StatOpenFile(write_file, cached_information) ==
            os::kernel::fs::Status::Succeeded &&
        cached_information.size_bytes == sizeof(OS_TEST_VFS_PAYLOAD) &&
        data_cache_context.size_invocation_count == OS_TEST_VFS_COUNTER_INCREMENT &&
        vfs.Close(write_file) == os::kernel::fs::Status::Succeeded;
    root_memfs.GetSuperblock().cache_regular_file_data = false;
    test_context.Expect(data_cache_boundary_valid, OS_TEST_VFS_DATA_CACHE_BOUNDARY);
    const bool path_failure_valid =
        data_cache_boundary_valid &&
        vfs.Open(context, OS_TEST_VFS_MESSAGE_TRAILING_SEPARATOR_PATH,
                 sizeof(OS_TEST_VFS_MESSAGE_TRAILING_SEPARATOR_PATH), read_options,
                 read_file) == os::kernel::fs::Status::NotDirectory &&
        vfs.Open(context, OS_TEST_VFS_MISSING_FILE_TRAILING_SEPARATOR_PATH,
                 sizeof(OS_TEST_VFS_MISSING_FILE_TRAILING_SEPARATOR_PATH), write_options,
                 write_file) == os::kernel::fs::Status::NotDirectory &&
        vfs.Resolve(context, OS_TEST_VFS_MISSING_FILE_TRAILING_SEPARATOR_PATH,
                    sizeof(OS_TEST_VFS_MISSING_FILE_TRAILING_SEPARATOR_PATH),
                    resolved_path) == os::kernel::fs::Status::NotFound;
    os::kernel::fs::OpenFile directory{};
    os::kernel::fs::DirectoryEntry entry{};
    bool end_of_directory = false;
    const bool directory_read =
        path_failure_valid &&
        vfs.OpenDirectory(context, OS_TEST_VFS_ALPHA_BETA_PATH, sizeof(OS_TEST_VFS_ALPHA_BETA_PATH),
                          directory) == os::kernel::fs::Status::Succeeded &&
        vfs.ReadDirectory(directory, entry, end_of_directory) ==
            os::kernel::fs::Status::Succeeded &&
        !end_of_directory && entry.type == os::kernel::fs::NodeType::RegularFile &&
        vfs.ReadDirectory(directory, entry, end_of_directory) ==
            os::kernel::fs::Status::Succeeded &&
        end_of_directory && vfs.Close(directory) == os::kernel::fs::Status::Succeeded;
    test_context.Expect(directory_read, OS_TEST_VFS_FILE_IO);

    const bool mount_traversal_valid =
        vfs.CreateDirectory(context, OS_TEST_VFS_MOUNT_PATH, sizeof(OS_TEST_VFS_MOUNT_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.MountAt(context, OS_TEST_VFS_ROOT_PATH, sizeof(OS_TEST_VFS_ROOT_PATH),
                    child_memfs.GetSuperblock()) == os::kernel::fs::Status::MountPointBusy &&
        vfs.MountAt(context, OS_TEST_VFS_MOUNT_PATH, sizeof(OS_TEST_VFS_MOUNT_PATH),
                    child_memfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        vfs.MountAt(context, OS_TEST_VFS_MOUNT_PATH, sizeof(OS_TEST_VFS_MOUNT_PATH),
                    child_memfs.GetSuperblock()) == os::kernel::fs::Status::AlreadyMounted &&
        vfs.CreateDirectory(context, OS_TEST_VFS_MOUNT_CHILD_PATH,
                            sizeof(OS_TEST_VFS_MOUNT_CHILD_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.ChangeDirectory(context, OS_TEST_VFS_MOUNT_PATH, sizeof(OS_TEST_VFS_MOUNT_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.GetWorkingDirectory(context, working_directory, sizeof(working_directory),
                                working_directory_length_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        working_directory_length_bytes == OS_TEST_VFS_EXPECTED_MOUNT_LENGTH_BYTES &&
        BytesEqual(working_directory, OS_TEST_VFS_EXPECTED_MOUNT, working_directory_length_bytes) &&
        vfs.ChangeDirectory(context, OS_TEST_VFS_PARENT_PATH, sizeof(OS_TEST_VFS_PARENT_PATH)) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.GetWorkingDirectory(context, working_directory, sizeof(working_directory),
                                working_directory_length_bytes) ==
            os::kernel::fs::Status::Succeeded &&
        working_directory_length_bytes == OS_TEST_VFS_EXPECTED_ROOT_LENGTH_BYTES &&
        BytesEqual(working_directory, OS_TEST_VFS_EXPECTED_ROOT, working_directory_length_bytes);
    test_context.Expect(mount_traversal_valid, OS_TEST_VFS_MOUNT_TRAVERSAL);

    os::kernel::fs::Mount capacity_mounts[OS_TEST_VFS_CONSTRAINED_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs capacity_vfs{};
    os::kernel::fs::FsContext capacity_context{};
    const bool mount_capacity_rejected =
        capacity_vfs.Initialize(capacity_mounts, OS_TEST_VFS_CONSTRAINED_MOUNT_CAPACITY,
                                root_memfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        capacity_vfs.InitializeContext(capacity_context) == os::kernel::fs::Status::Succeeded &&
        capacity_vfs.MountAt(capacity_context, OS_TEST_VFS_MOUNT_PATH,
                             sizeof(OS_TEST_VFS_MOUNT_PATH), child_memfs.GetSuperblock()) ==
            os::kernel::fs::Status::MountCapacityExhausted;
    const os::kernel::fs::Path original_mount_point =
        mounts[OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER].mount_point;
    const uint64_t original_parent_mount_identifier =
        mounts[OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER].parent_mount_identifier;
    mounts[OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER].parent_mount_identifier =
        mounts[OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER].identifier;
    mounts[OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER].mount_point = os::kernel::fs::Path{
        .mount_identifier = mounts[OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER].identifier,
        .vnode = child_memfs.GetSuperblock().root,
    };
    const bool mount_cycle_detected = vfs.Validate() == os::kernel::fs::Status::LoopDetected;
    mounts[OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER].parent_mount_identifier =
        original_parent_mount_identifier;
    mounts[OS_TEST_VFS_CHILD_MOUNT_IDENTIFIER].mount_point = original_mount_point;
    test_context.Expect(mount_capacity_rejected && mount_cycle_detected &&
                            vfs.Validate() == os::kernel::fs::Status::Succeeded,
                        OS_TEST_VFS_CYCLE_AND_CAPACITY);

    const os::kernel::fs::MemfsStatistics root_statistics = root_memfs.ReadStatistics();
    const os::kernel::fs::MemfsStatistics child_statistics = child_memfs.ReadStatistics();
    os::kernel::fs::ResourceUsage resource_usage{};
    const bool resource_usage_valid =
        vfs.ReadResourceUsage(resource_usage) == os::kernel::fs::Status::Succeeded &&
        resource_usage.heap_consumed_bytes == root_statistics.active_heap_consumed_bytes +
                                                  child_statistics.active_heap_consumed_bytes &&
        resource_usage.heap_active_requested_bytes ==
            root_statistics.active_heap_requested_bytes +
                child_statistics.active_heap_requested_bytes &&
        resource_usage.heap_allocation_count == root_statistics.active_heap_allocation_count +
                                                    child_statistics.active_heap_allocation_count &&
        resource_usage.vnode_count ==
            root_statistics.active_node_count + child_statistics.active_node_count;
    const bool validation_succeeded =
        resource_usage_valid && vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        root_memfs.GetSuperblock().operations->validate(
            root_memfs.GetSuperblock().backend_context) == os::kernel::fs::Status::Succeeded &&
        child_memfs.GetSuperblock().operations->validate(
            child_memfs.GetSuperblock().backend_context) == os::kernel::fs::Status::Succeeded;
    const bool resources_released =
        capacity_vfs.ReleaseContext(capacity_context) == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(cloned_context) == os::kernel::fs::Status::Succeeded &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        child_memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        root_memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_VFS_EMPTY_VALUE;
    test_context.Expect(validation_succeeded && resources_released, OS_TEST_VFS_VALIDATION);
    return test_context.ExitCode();
}
