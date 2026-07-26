#include "memory_block_device.hpp"
#include "os/kernel/fs/file_system.hpp"
#include "os/kernel/fs/legacy_file_system.hpp"
#include "os/kernel/fs/memfs.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_BACKEND_SUITE_NAME =
    "kernel/vfs/backend_contract/integration";
constexpr std::string_view OS_TEST_VFS_BACKEND_PARITY =
    "memfs 与旧格式适配器必须通过同一组基础 VFS 契约";
constexpr std::string_view OS_TEST_VFS_BACKEND_PERSISTENCE =
    "旧格式经 VFS 写入后必须由新挂载实例按原格式读取";
constexpr std::string_view OS_TEST_VFS_BACKEND_RESOURCES = "memfs 销毁后必须归还全部节点与数据分配";

constexpr uint64_t OS_TEST_VFS_BACKEND_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VFS_BACKEND_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_BACKEND_HEAP_SIZE_BYTES = 2ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_BACKEND_MEMFS_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_VFS_BACKEND_LEGACY_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_VFS_BACKEND_REMOUNT_IDENTIFIER = 3ULL;
constexpr uint64_t OS_TEST_VFS_BACKEND_NODE_LIMIT = 64ULL;
constexpr uint64_t OS_TEST_VFS_BACKEND_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_VFS_BACKEND_EXPECTED_WORKING_DIRECTORY_LENGTH_BYTES = 15ULL;
constexpr uint8_t OS_TEST_VFS_BACKEND_SUITE_PATH[] = {
    '/', 'c', 'o', 'n', 't', 'r', 'a', 'c', 't',
};
constexpr uint8_t OS_TEST_VFS_BACKEND_CHILD_PATH[] = {
    'c', 'h', 'i', 'l', 'd',
};
constexpr uint8_t OS_TEST_VFS_BACKEND_FILE_PATH[] = {
    '.', '/', 'd', 'a', 't', 'a',
};
constexpr uint8_t OS_TEST_VFS_BACKEND_ABSOLUTE_FILE_PATH[] = {
    '/', 'c', 'o', 'n', 't', 'r', 'a', 'c', 't', '/',
    'c', 'h', 'i', 'l', 'd', '/', 'd', 'a', 't', 'a',
};
constexpr uint8_t OS_TEST_VFS_BACKEND_CURRENT_PATH[] = {'.'};
constexpr uint8_t OS_TEST_VFS_BACKEND_EXPECTED_WORKING_DIRECTORY[] = {
    '/', 'c', 'o', 'n', 't', 'r', 'a', 'c', 't', '/', 'c', 'h', 'i', 'l', 'd',
};
constexpr uint8_t OS_TEST_VFS_BACKEND_EXPECTED_FILE_NAME[] = {
    'd',
    'a',
    't',
    'a',
};
constexpr uint8_t OS_TEST_VFS_BACKEND_PAYLOAD[] = {
    's', 'a', 'm', 'e', '-', 'v', 'f', 's', '-', 'c', 'o', 'n', 't', 'r', 'a', 'c', 't',
};
constexpr uint64_t OS_TEST_VFS_BACKEND_FILE_SIZE_LIMIT = sizeof(OS_TEST_VFS_BACKEND_PAYLOAD);

[[nodiscard]] bool BytesEqual(const uint8_t *const left, const uint8_t *const right,
                              const uint64_t length_bytes) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (uint64_t byte_index = OS_TEST_VFS_BACKEND_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool RunBackendContract(os::kernel::fs::Vfs &vfs,
                                      os::kernel::fs::FsContext &context) noexcept {
    if (vfs.CreateDirectory(context, OS_TEST_VFS_BACKEND_SUITE_PATH,
                            sizeof(OS_TEST_VFS_BACKEND_SUITE_PATH)) !=
            os::kernel::fs::Status::Succeeded ||
        vfs.ChangeDirectory(context, OS_TEST_VFS_BACKEND_SUITE_PATH,
                            sizeof(OS_TEST_VFS_BACKEND_SUITE_PATH)) !=
            os::kernel::fs::Status::Succeeded ||
        vfs.CreateDirectory(context, OS_TEST_VFS_BACKEND_CHILD_PATH,
                            sizeof(OS_TEST_VFS_BACKEND_CHILD_PATH)) !=
            os::kernel::fs::Status::Succeeded ||
        vfs.ChangeDirectory(context, OS_TEST_VFS_BACKEND_CHILD_PATH,
                            sizeof(OS_TEST_VFS_BACKEND_CHILD_PATH)) !=
            os::kernel::fs::Status::Succeeded) {
        return false;
    }
    uint8_t working_directory[os::kernel::fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES]{};
    uint64_t working_directory_length_bytes = OS_TEST_VFS_BACKEND_EMPTY_VALUE;
    if (vfs.GetWorkingDirectory(context, working_directory, sizeof(working_directory),
                                working_directory_length_bytes) !=
            os::kernel::fs::Status::Succeeded ||
        working_directory_length_bytes !=
            OS_TEST_VFS_BACKEND_EXPECTED_WORKING_DIRECTORY_LENGTH_BYTES ||
        !BytesEqual(working_directory, OS_TEST_VFS_BACKEND_EXPECTED_WORKING_DIRECTORY,
                    working_directory_length_bytes)) {
        return false;
    }

    const os::kernel::fs::OpenOptions write_options{
        .readable = false,
        .writable = true,
        .create = true,
        .truncate = true,
    };
    os::kernel::fs::OpenFile open_file{};
    uint64_t transferred_bytes = OS_TEST_VFS_BACKEND_EMPTY_VALUE;
    if (vfs.Open(context, OS_TEST_VFS_BACKEND_FILE_PATH, sizeof(OS_TEST_VFS_BACKEND_FILE_PATH),
                 write_options, open_file) != os::kernel::fs::Status::Succeeded ||
        vfs.Write(open_file, OS_TEST_VFS_BACKEND_PAYLOAD, sizeof(OS_TEST_VFS_BACKEND_PAYLOAD),
                  transferred_bytes) != os::kernel::fs::Status::Succeeded ||
        transferred_bytes != sizeof(OS_TEST_VFS_BACKEND_PAYLOAD) ||
        vfs.Close(open_file) != os::kernel::fs::Status::Succeeded) {
        return false;
    }

    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
    };
    uint8_t payload[sizeof(OS_TEST_VFS_BACKEND_PAYLOAD)]{};
    transferred_bytes = OS_TEST_VFS_BACKEND_EMPTY_VALUE;
    if (vfs.Open(context, OS_TEST_VFS_BACKEND_ABSOLUTE_FILE_PATH,
                 sizeof(OS_TEST_VFS_BACKEND_ABSOLUTE_FILE_PATH), read_options,
                 open_file) != os::kernel::fs::Status::Succeeded ||
        vfs.Read(open_file, payload, sizeof(payload), transferred_bytes) !=
            os::kernel::fs::Status::Succeeded ||
        transferred_bytes != sizeof(payload) ||
        !BytesEqual(payload, OS_TEST_VFS_BACKEND_PAYLOAD, sizeof(payload)) ||
        vfs.Close(open_file) != os::kernel::fs::Status::Succeeded) {
        return false;
    }

    os::kernel::fs::DirectoryEntry entry{};
    bool end_of_directory = false;
    if (vfs.OpenDirectory(context, OS_TEST_VFS_BACKEND_CURRENT_PATH,
                          sizeof(OS_TEST_VFS_BACKEND_CURRENT_PATH),
                          open_file) != os::kernel::fs::Status::Succeeded ||
        vfs.ReadDirectory(open_file, entry, end_of_directory) !=
            os::kernel::fs::Status::Succeeded ||
        end_of_directory || entry.type != os::kernel::fs::NodeType::RegularFile ||
        entry.name_length_bytes != sizeof(OS_TEST_VFS_BACKEND_EXPECTED_FILE_NAME) ||
        !BytesEqual(entry.name, OS_TEST_VFS_BACKEND_EXPECTED_FILE_NAME, entry.name_length_bytes) ||
        vfs.ReadDirectory(open_file, entry, end_of_directory) !=
            os::kernel::fs::Status::Succeeded ||
        !end_of_directory || vfs.Close(open_file) != os::kernel::fs::Status::Succeeded) {
        return false;
    }
    return vfs.Sync() == os::kernel::fs::Status::Succeeded &&
           vfs.Validate() == os::kernel::fs::Status::Succeeded;
}

[[nodiscard]] bool ReadPersistedPayload(os::kernel::fs::Vfs &vfs,
                                        os::kernel::fs::FsContext &context) noexcept {
    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
    };
    os::kernel::fs::OpenFile open_file{};
    uint8_t payload[sizeof(OS_TEST_VFS_BACKEND_PAYLOAD)]{};
    uint64_t read_bytes = OS_TEST_VFS_BACKEND_EMPTY_VALUE;
    return vfs.Open(context, OS_TEST_VFS_BACKEND_ABSOLUTE_FILE_PATH,
                    sizeof(OS_TEST_VFS_BACKEND_ABSOLUTE_FILE_PATH), read_options,
                    open_file) == os::kernel::fs::Status::Succeeded &&
           vfs.Read(open_file, payload, sizeof(payload), read_bytes) ==
               os::kernel::fs::Status::Succeeded &&
           read_bytes == sizeof(payload) &&
           BytesEqual(payload, OS_TEST_VFS_BACKEND_PAYLOAD, sizeof(payload)) &&
           vfs.Close(open_file) == os::kernel::fs::Status::Succeeded;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_BACKEND_SUITE_NAME};
    alignas(OS_TEST_VFS_BACKEND_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_buffer[OS_TEST_VFS_BACKEND_HEAP_SIZE_BYTES]{};
    static os::test::MemoryBlockDevice device{};

    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs memfs{};
    os::kernel::fs::Mount memfs_mounts[OS_TEST_VFS_BACKEND_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs memfs_vfs{};
    os::kernel::fs::FsContext memfs_context{};
    const bool memfs_ready =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer), sizeof(heap_buffer)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        memfs.Initialize(heap, OS_TEST_VFS_BACKEND_MEMFS_IDENTIFIER, OS_TEST_VFS_BACKEND_NODE_LIMIT,
                         OS_TEST_VFS_BACKEND_FILE_SIZE_LIMIT) ==
            os::kernel::fs::Status::Succeeded &&
        memfs_vfs.Initialize(memfs_mounts, OS_TEST_VFS_BACKEND_MOUNT_CAPACITY,
                             memfs.GetSuperblock()) == os::kernel::fs::Status::Succeeded &&
        memfs_vfs.InitializeContext(memfs_context) == os::kernel::fs::Status::Succeeded;
    const bool memfs_contract_valid = memfs_ready && RunBackendContract(memfs_vfs, memfs_context);

    os::kernel::FileSystem legacy_file_system{};
    bool formatted = false;
    os::kernel::fs::LegacyFileSystem legacy_adapter{};
    os::kernel::fs::Mount legacy_mounts[OS_TEST_VFS_BACKEND_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs legacy_vfs{};
    os::kernel::fs::FsContext legacy_context{};
    const bool legacy_ready =
        legacy_file_system.MountOrFormat(device, formatted) ==
            os::kernel::FileSystemStatus::Succeeded &&
        formatted &&
        legacy_adapter.Initialize(legacy_file_system, OS_TEST_VFS_BACKEND_LEGACY_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        legacy_vfs.Initialize(legacy_mounts, OS_TEST_VFS_BACKEND_MOUNT_CAPACITY,
                              legacy_adapter.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        legacy_vfs.InitializeContext(legacy_context) == os::kernel::fs::Status::Succeeded;
    const bool legacy_contract_valid =
        legacy_ready && RunBackendContract(legacy_vfs, legacy_context);
    test_context.Expect(memfs_contract_valid && legacy_contract_valid, OS_TEST_VFS_BACKEND_PARITY);

    os::kernel::FileSystem remounted_file_system{};
    formatted = true;
    os::kernel::fs::LegacyFileSystem remounted_adapter{};
    os::kernel::fs::Mount remounted_mounts[OS_TEST_VFS_BACKEND_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs remounted_vfs{};
    os::kernel::fs::FsContext remounted_context{};
    const bool persistence_valid =
        legacy_contract_valid &&
        remounted_file_system.MountOrFormat(device, formatted) ==
            os::kernel::FileSystemStatus::Succeeded &&
        !formatted &&
        remounted_adapter.Initialize(remounted_file_system,
                                     OS_TEST_VFS_BACKEND_REMOUNT_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        remounted_vfs.Initialize(remounted_mounts, OS_TEST_VFS_BACKEND_MOUNT_CAPACITY,
                                 remounted_adapter.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        remounted_vfs.InitializeContext(remounted_context) == os::kernel::fs::Status::Succeeded &&
        ReadPersistedPayload(remounted_vfs, remounted_context);
    test_context.Expect(persistence_valid, OS_TEST_VFS_BACKEND_PERSISTENCE);

    const bool resources_released =
        remounted_vfs.ReleaseContext(remounted_context) == os::kernel::fs::Status::Succeeded &&
        legacy_vfs.ReleaseContext(legacy_context) == os::kernel::fs::Status::Succeeded &&
        memfs_vfs.ReleaseContext(memfs_context) == os::kernel::fs::Status::Succeeded &&
        memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_VFS_BACKEND_EMPTY_VALUE;
    test_context.Expect(resources_released, OS_TEST_VFS_BACKEND_RESOURCES);
    return test_context.ExitCode();
}
