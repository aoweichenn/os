#include "memory_block_device.hpp"
#include "os/kernel/fs/file_system.hpp"
#include "os/kernel/fs/legacy_file_system.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/io/file_description.hpp"
#include "os/kernel/io/file_table.hpp"
#include "os/kernel/ipc/pipe.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "os/kernel/object/kernel_object.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_DESCRIPTION_LIFECYCLE_SUITE_NAME =
    "kernel/file_description/lifecycle/integration";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_SHARED_OFFSET =
    "复制描述符必须共享文件偏移而独立打开必须拥有独立偏移";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_PIPE_LIFETIME =
    "管道端点只能在最后一个文件描述引用释放时关闭";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_FINALIZATION =
    "文件表销毁后文件、管道、对象和堆资源必须全部归零";
constexpr std::string_view OS_TEST_FILE_DESCRIPTION_INVALID_DIRECTORY_CONFIGURATION =
    "目录描述必须拒绝与公开 flags 不一致的内部 OpenFile 能力";

constexpr uint64_t OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_HEAP_SIZE_BYTES = 512ULL * 1024ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_TABLE_LIMIT = 256ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_TRANSFER_SIZE_BYTES = 4ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_SECOND_OFFSET_BYTES = 4ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_FILE_DESCRIPTOR = 3ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_INDEPENDENT_FILE_DESCRIPTOR = 4ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_DUPLICATE_FILE_DESCRIPTOR = 5ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_PIPE_READER_DESCRIPTOR = 6ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_PIPE_WRITER_DESCRIPTOR = 7ULL;
constexpr uint64_t OS_TEST_FILE_DESCRIPTION_DUPLICATE_PIPE_WRITER_DESCRIPTOR = 8ULL;
constexpr uint8_t OS_TEST_FILE_DESCRIPTION_FILE_PATH[] = {
    static_cast<uint8_t>('/'), static_cast<uint8_t>('f'), static_cast<uint8_t>('d'),
    static_cast<uint8_t>('v'), static_cast<uint8_t>('1'), static_cast<uint8_t>('4'),
    static_cast<uint8_t>('.'), static_cast<uint8_t>('b'), static_cast<uint8_t>('i'),
    static_cast<uint8_t>('n'),
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

[[nodiscard]] os::kernel::FileDescriptionStatus
CreateFileDescription(os::kernel::FileDescriptionManager &manager, os::kernel::fs::Vfs &vfs,
                      const os::kernel::fs::OpenFile &open_file,
                      os::kernel::KernelObjectReference &reference) noexcept {
    uint64_t file_status_flags = OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE;
    if (open_file.readable) {
        file_status_flags |= os::kernel::OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG;
    }
    if (open_file.writable) {
        file_status_flags |= os::kernel::OS_KERNEL_FILE_DESCRIPTION_WRITABLE_STATUS_FLAG;
    }
    const os::kernel::FileDescriptionCreateRequest request{
        .kind = os::kernel::FileDescriptionKind::RegularFile,
        .file_status_flags = file_status_flags,
        .console_input = nullptr,
        .device_write_operation = nullptr,
        .device_write_context = nullptr,
        .pipe = nullptr,
        .pipe_manager = nullptr,
        .vfs = &vfs,
        .open_file = open_file,
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
        .console_input = nullptr,
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

    bool formatted = false;
    os::kernel::FileSystemHandle write_handle{};
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
        file_system.Close(write_handle) == os::kernel::FileSystemStatus::Succeeded;

    os::kernel::fs::LegacyFileSystem legacy_adapter{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::Mount mounts[OS_TEST_FILE_DESCRIPTION_MOUNT_CAPACITY]{};
    os::kernel::fs::FsContext file_system_context{};
    os::kernel::fs::OpenFile shared_open_file{};
    os::kernel::fs::OpenFile independent_open_file{};
    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
    };
    const bool handles_opened =
        file_prepared &&
        legacy_adapter.Initialize(file_system, OS_TEST_FILE_DESCRIPTION_SUPERBLOCK_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_FILE_DESCRIPTION_MOUNT_CAPACITY,
                       legacy_adapter.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
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
    os::kernel::FileTable table{};
    const bool object_model_initialized =
        handles_opened &&
        heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer),
                        OS_TEST_FILE_DESCRIPTION_HEAP_SIZE_BYTES) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        object_manager.Initialize(heap) == os::kernel::KernelObjectStatus::Succeeded &&
        description_manager.Initialize(object_manager) ==
            os::kernel::FileDescriptionStatus::Succeeded &&
        table.Initialize(heap, object_manager, OS_TEST_FILE_DESCRIPTION_TABLE_LIMIT,
                         OS_TEST_FILE_DESCRIPTION_TABLE_LIMIT) ==
            os::kernel::FileTableStatus::Succeeded;
    const os::kernel::FileDescriptionCreateRequest invalid_directory_request{
        .kind = os::kernel::FileDescriptionKind::Directory,
        .file_status_flags = os::kernel::OS_KERNEL_FILE_DESCRIPTION_READABLE_STATUS_FLAG,
        .console_input = nullptr,
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
                                     sizeof(OS_TEST_FILE_DESCRIPTION_PIPE_PAYLOAD),
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
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_FILE_DESCRIPTION_EMPTY_VALUE &&
        file_system.CheckConsistency() == os::kernel::FileSystemStatus::Succeeded &&
        vfs.Validate() == os::kernel::fs::Status::Succeeded;
    test_context.Expect(finalized, OS_TEST_FILE_DESCRIPTION_FINALIZATION);
    return test_context.ExitCode();
}
