#include "os/kernel/fs/memfs.hpp"
#include "os/kernel/fs/root_file_system.hpp"
#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/memory/kernel_heap.hpp"
#include "root_file_system_test_support.hpp"
#include "sparse_memory_block_device.hpp"
#include "test_context.hpp"

#include <algorithm>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view OS_TEST_VFS_RANDOM_SUITE_NAME = "kernel/vfs/namespace/randomized";
constexpr std::string_view OS_TEST_VFS_RANDOM_REFERENCE_MODEL =
    "十万步目录树操作必须与独立参考模型保持逐步一致";
constexpr std::string_view OS_TEST_VFS_RANDOM_ROOT_REFERENCE_MODEL =
    "rootfs v4 的十万步目录树操作必须与同一独立参考模型保持逐步一致";
constexpr std::string_view OS_TEST_VFS_RANDOM_FINAL_STATE =
    "随机序列结束后命名空间、统计和堆资源必须完整收敛";
constexpr std::string_view OS_TEST_VFS_RANDOM_BACKEND_PARITY =
    "memfs 与 rootfs v4 必须在同一种子十万步后收敛到相同节点规模";

constexpr os::test::RandomSeed OS_TEST_VFS_RANDOM_SEED = 0x5646532026001500ULL;
constexpr os::test::TestCount OS_TEST_VFS_RANDOM_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_COUNTER_INCREMENT = 1ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_SUPERBLOCK_IDENTIFIER = 1ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_ROOT_SUPERBLOCK_IDENTIFIER = 2ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_HEAP_ALIGNMENT_BYTES = 64ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_HEAP_SIZE_BYTES = 16ULL * 1024ULL * 1024ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_NODE_LIMIT = 384ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_MAXIMUM_FILE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_MOUNT_CAPACITY = 4ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_OPERATION_KIND_COUNT = 7ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_CREATE_OPERATION = 0ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_RESOLVE_OPERATION = 1ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_CHANGE_DIRECTORY_OPERATION = 2ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_PARENT_OPERATION = 3ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_FILE_IO_OPERATION = 4ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_READ_DIRECTORY_OPERATION = 5ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_MISSING_PATH_OPERATION = 6ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_DIRECTORY_TYPE_DIVISOR = 3ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_MAXIMUM_PAYLOAD_SIZE_BYTES = 128ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_VALIDATION_INTERVAL = 2048ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_MINIMUM_RESOLUTION_DIVISOR = 2ULL;
constexpr uint64_t OS_TEST_VFS_RANDOM_MINIMUM_RESOLUTION_COUNT =
    OS_TEST_VFS_RANDOM_STEP_COUNT / OS_TEST_VFS_RANDOM_MINIMUM_RESOLUTION_DIVISOR;
constexpr char OS_TEST_VFS_RANDOM_ROOT_PATH[] = "/";
constexpr char OS_TEST_VFS_RANDOM_NODE_NAME_PREFIX[] = "node_";
constexpr char OS_TEST_VFS_RANDOM_MISSING_PATH_PREFIX[] = "/missing_";
constexpr char OS_TEST_VFS_RANDOM_PATH_SEPARATOR = '/';
constexpr uint8_t OS_TEST_VFS_RANDOM_PARENT_PATH[] = {'.', '.'};

struct ModelNode final {
    uint64_t identifier;
    uint64_t parent_identifier;
    os::kernel::fs::NodeType type;
    std::string name;
    std::vector<uint8_t> data;
};

struct ObservedDirectoryEntry final {
    uint64_t identifier;
    os::kernel::fs::NodeType type;
    std::string name;
};

[[nodiscard]] const ModelNode *FindNode(const std::vector<ModelNode> &nodes,
                                        const uint64_t identifier) noexcept {
    for (const ModelNode &node : nodes) {
        if (node.identifier == identifier) {
            return &node;
        }
    }
    return nullptr;
}

[[nodiscard]] ModelNode *FindNode(std::vector<ModelNode> &nodes,
                                  const uint64_t identifier) noexcept {
    for (ModelNode &node : nodes) {
        if (node.identifier == identifier) {
            return &node;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string BuildPath(const std::vector<ModelNode> &nodes,
                                    const uint64_t identifier) {
    if (identifier == OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER) {
        return OS_TEST_VFS_RANDOM_ROOT_PATH;
    }
    std::vector<std::string> components{};
    const ModelNode *node = FindNode(nodes, identifier);
    uint64_t traversal_count = OS_TEST_VFS_RANDOM_EMPTY_VALUE;
    while (node != nullptr && node->identifier != OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER &&
           traversal_count < OS_TEST_VFS_RANDOM_NODE_LIMIT) {
        components.push_back(node->name);
        node = FindNode(nodes, node->parent_identifier);
        ++traversal_count;
    }
    if (node == nullptr || traversal_count >= OS_TEST_VFS_RANDOM_NODE_LIMIT) {
        return {};
    }
    std::string path{};
    for (auto component = components.rbegin(); component != components.rend(); ++component) {
        path.push_back(OS_TEST_VFS_RANDOM_PATH_SEPARATOR);
        path += *component;
    }
    return path;
}

[[nodiscard]] uint64_t SelectNodeIdentifier(const std::vector<ModelNode> &nodes,
                                            const os::kernel::fs::NodeType required_type,
                                            std::mt19937_64 &generator) {
    std::vector<uint64_t> identifiers{};
    for (const ModelNode &node : nodes) {
        if (required_type == os::kernel::fs::NodeType::None || node.type == required_type) {
            identifiers.push_back(node.identifier);
        }
    }
    if (identifiers.empty()) {
        return OS_TEST_VFS_RANDOM_EMPTY_VALUE;
    }
    std::uniform_int_distribution<uint64_t> distribution{
        OS_TEST_VFS_RANDOM_EMPTY_VALUE,
        static_cast<uint64_t>(identifiers.size()) - OS_TEST_VFS_RANDOM_COUNTER_INCREMENT,
    };
    return identifiers[distribution(generator)];
}

[[nodiscard]] bool ResolveMatches(const std::vector<ModelNode> &nodes, os::kernel::fs::Vfs &vfs,
                                  const os::kernel::fs::FsContext &context,
                                  const uint64_t identifier) {
    const ModelNode *const expected = FindNode(nodes, identifier);
    const std::string path = BuildPath(nodes, identifier);
    if (expected == nullptr || path.empty()) {
        return false;
    }
    os::kernel::fs::Path resolved{};
    return vfs.Resolve(context, reinterpret_cast<const uint8_t *>(path.data()),
                       static_cast<uint64_t>(path.size()),
                       resolved) == os::kernel::fs::Status::Succeeded &&
           resolved.vnode.identifier == expected->identifier &&
           resolved.vnode.type == expected->type;
}

[[nodiscard]] bool CreateNode(std::vector<ModelNode> &nodes, os::kernel::fs::Vfs &vfs,
                              const os::kernel::fs::FsContext &context,
                              uint64_t &next_name_identifier, std::mt19937_64 &generator) {
    if (static_cast<uint64_t>(nodes.size()) >= OS_TEST_VFS_RANDOM_NODE_LIMIT) {
        return true;
    }
    const uint64_t parent_identifier =
        SelectNodeIdentifier(nodes, os::kernel::fs::NodeType::Directory, generator);
    const std::string parent_path = BuildPath(nodes, parent_identifier);
    if (parent_identifier == OS_TEST_VFS_RANDOM_EMPTY_VALUE || parent_path.empty()) {
        return false;
    }
    const std::string name =
        OS_TEST_VFS_RANDOM_NODE_NAME_PREFIX + std::to_string(next_name_identifier);
    ++next_name_identifier;
    const std::string path = parent_path == OS_TEST_VFS_RANDOM_ROOT_PATH
                                 ? parent_path + name
                                 : parent_path + OS_TEST_VFS_RANDOM_ROOT_PATH + name;
    const bool create_directory =
        (generator() % OS_TEST_VFS_RANDOM_DIRECTORY_TYPE_DIVISOR) != OS_TEST_VFS_RANDOM_EMPTY_VALUE;
    os::kernel::fs::Status status = os::kernel::fs::Status::InvalidArgument;
    if (create_directory) {
        status = vfs.CreateDirectory(context, reinterpret_cast<const uint8_t *>(path.data()),
                                     static_cast<uint64_t>(path.size()));
    } else {
        const os::kernel::fs::OpenOptions options{
            .readable = false,
            .writable = true,
            .create = true,
            .truncate = false,
            .append = false,
        };
        os::kernel::fs::OpenFile open_file{};
        status = vfs.Open(context, reinterpret_cast<const uint8_t *>(path.data()),
                          static_cast<uint64_t>(path.size()), options, open_file);
        if (status == os::kernel::fs::Status::Succeeded &&
            vfs.Close(open_file) != os::kernel::fs::Status::Succeeded) {
            return false;
        }
    }
    if (status != os::kernel::fs::Status::Succeeded) {
        return false;
    }
    os::kernel::fs::Path resolved{};
    if (vfs.Resolve(context, reinterpret_cast<const uint8_t *>(path.data()),
                    static_cast<uint64_t>(path.size()),
                    resolved) != os::kernel::fs::Status::Succeeded) {
        return false;
    }
    nodes.push_back(ModelNode{
        .identifier = resolved.vnode.identifier,
        .parent_identifier = parent_identifier,
        .type = create_directory ? os::kernel::fs::NodeType::Directory
                                 : os::kernel::fs::NodeType::RegularFile,
        .name = name,
        .data = {},
    });
    return ResolveMatches(nodes, vfs, context, resolved.vnode.identifier);
}

[[nodiscard]] bool ChangeDirectory(const std::vector<ModelNode> &nodes, os::kernel::fs::Vfs &vfs,
                                   os::kernel::fs::FsContext &context,
                                   uint64_t &working_directory_identifier,
                                   std::mt19937_64 &generator) {
    const uint64_t directory_identifier =
        SelectNodeIdentifier(nodes, os::kernel::fs::NodeType::Directory, generator);
    const std::string path = BuildPath(nodes, directory_identifier);
    if (path.empty() || vfs.ChangeDirectory(context, reinterpret_cast<const uint8_t *>(path.data()),
                                            static_cast<uint64_t>(path.size())) !=
                            os::kernel::fs::Status::Succeeded) {
        return false;
    }
    working_directory_identifier = directory_identifier;
    uint8_t actual_path[os::kernel::fs::OS_KERNEL_VFS_MAXIMUM_PATH_LENGTH_BYTES]{};
    uint64_t actual_length_bytes = OS_TEST_VFS_RANDOM_EMPTY_VALUE;
    return vfs.GetWorkingDirectory(context, actual_path, sizeof(actual_path),
                                   actual_length_bytes) == os::kernel::fs::Status::Succeeded &&
           actual_length_bytes == static_cast<uint64_t>(path.size()) &&
           std::string(reinterpret_cast<const char *>(actual_path),
                       static_cast<uint64_t>(actual_length_bytes)) == path;
}

[[nodiscard]] bool MoveToParent(const std::vector<ModelNode> &nodes, os::kernel::fs::Vfs &vfs,
                                os::kernel::fs::FsContext &context,
                                uint64_t &working_directory_identifier) {
    const ModelNode *const current = FindNode(nodes, working_directory_identifier);
    if (current == nullptr || vfs.ChangeDirectory(context, OS_TEST_VFS_RANDOM_PARENT_PATH,
                                                  sizeof(OS_TEST_VFS_RANDOM_PARENT_PATH)) !=
                                  os::kernel::fs::Status::Succeeded) {
        return false;
    }
    working_directory_identifier = current->identifier == OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER
                                       ? OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER
                                       : current->parent_identifier;
    return context.current_working_directory.vnode.identifier == working_directory_identifier &&
           ResolveMatches(nodes, vfs, context, working_directory_identifier);
}

[[nodiscard]] bool ExerciseFile(std::vector<ModelNode> &nodes, os::kernel::fs::Vfs &vfs,
                                const os::kernel::fs::FsContext &context,
                                std::mt19937_64 &generator) {
    const uint64_t file_identifier =
        SelectNodeIdentifier(nodes, os::kernel::fs::NodeType::RegularFile, generator);
    if (file_identifier == OS_TEST_VFS_RANDOM_EMPTY_VALUE) {
        return true;
    }
    ModelNode *const model_file = FindNode(nodes, file_identifier);
    const std::string path = BuildPath(nodes, file_identifier);
    if (model_file == nullptr || path.empty()) {
        return false;
    }
    std::uniform_int_distribution<uint64_t> length_distribution{
        OS_TEST_VFS_RANDOM_EMPTY_VALUE,
        OS_TEST_VFS_RANDOM_MAXIMUM_PAYLOAD_SIZE_BYTES,
    };
    const uint64_t payload_length_bytes = length_distribution(generator);
    std::vector<uint8_t> payload(payload_length_bytes);
    for (uint8_t &value : payload) {
        value = static_cast<uint8_t>(generator());
    }
    const os::kernel::fs::OpenOptions write_options{
        .readable = false,
        .writable = true,
        .create = false,
        .truncate = true,
        .append = false,
    };
    os::kernel::fs::OpenFile open_file{};
    uint64_t written_bytes = OS_TEST_VFS_RANDOM_EMPTY_VALUE;
    if (vfs.Open(context, reinterpret_cast<const uint8_t *>(path.data()),
                 static_cast<uint64_t>(path.size()), write_options,
                 open_file) != os::kernel::fs::Status::Succeeded ||
        vfs.Write(open_file, payload.data(), payload_length_bytes, written_bytes) !=
            os::kernel::fs::Status::Succeeded ||
        written_bytes != payload_length_bytes ||
        vfs.Close(open_file) != os::kernel::fs::Status::Succeeded) {
        return false;
    }
    model_file->data = payload;
    const os::kernel::fs::OpenOptions read_options{
        .readable = true,
        .writable = false,
        .create = false,
        .truncate = false,
        .append = false,
    };
    std::vector<uint8_t> output(payload_length_bytes);
    uint64_t read_bytes = OS_TEST_VFS_RANDOM_EMPTY_VALUE;
    return vfs.Open(context, reinterpret_cast<const uint8_t *>(path.data()),
                    static_cast<uint64_t>(path.size()), read_options,
                    open_file) == os::kernel::fs::Status::Succeeded &&
           vfs.Read(open_file, output.data(), payload_length_bytes, read_bytes) ==
               os::kernel::fs::Status::Succeeded &&
           read_bytes == payload_length_bytes && output == model_file->data &&
           vfs.Close(open_file) == os::kernel::fs::Status::Succeeded;
}

[[nodiscard]] bool ReadDirectory(const std::vector<ModelNode> &nodes, os::kernel::fs::Vfs &vfs,
                                 const os::kernel::fs::FsContext &context,
                                 std::mt19937_64 &generator) {
    const uint64_t directory_identifier =
        SelectNodeIdentifier(nodes, os::kernel::fs::NodeType::Directory, generator);
    const std::string path = BuildPath(nodes, directory_identifier);
    if (path.empty()) {
        return false;
    }
    std::vector<ObservedDirectoryEntry> expected{};
    for (const ModelNode &node : nodes) {
        if (node.identifier != directory_identifier &&
            node.parent_identifier == directory_identifier) {
            expected.push_back(ObservedDirectoryEntry{
                .identifier = node.identifier,
                .type = node.type,
                .name = node.name,
            });
        }
    }
    std::sort(expected.begin(), expected.end(),
              [](const ObservedDirectoryEntry &left, const ObservedDirectoryEntry &right) noexcept {
                  return left.identifier < right.identifier;
              });

    os::kernel::fs::OpenFile open_file{};
    if (vfs.OpenDirectory(context, reinterpret_cast<const uint8_t *>(path.data()),
                          static_cast<uint64_t>(path.size()),
                          open_file) != os::kernel::fs::Status::Succeeded) {
        return false;
    }
    std::vector<ObservedDirectoryEntry> actual{};
    while (true) {
        os::kernel::fs::DirectoryEntry entry{};
        bool end_of_directory = false;
        if (vfs.ReadDirectory(open_file, entry, end_of_directory) !=
            os::kernel::fs::Status::Succeeded) {
            return false;
        }
        if (end_of_directory) {
            break;
        }
        actual.push_back(ObservedDirectoryEntry{
            .identifier = entry.node_identifier,
            .type = entry.type,
            .name = std::string(reinterpret_cast<const char *>(entry.name),
                                static_cast<uint64_t>(entry.name_length_bytes)),
        });
    }
    if (vfs.Close(open_file) != os::kernel::fs::Status::Succeeded ||
        actual.size() != expected.size()) {
        return false;
    }
    for (uint64_t entry_index = OS_TEST_VFS_RANDOM_EMPTY_VALUE;
         entry_index < static_cast<uint64_t>(expected.size()); ++entry_index) {
        if (actual[entry_index].identifier != expected[entry_index].identifier ||
            actual[entry_index].type != expected[entry_index].type ||
            actual[entry_index].name != expected[entry_index].name) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool MissingPathIsRejected(os::kernel::fs::Vfs &vfs,
                                         const os::kernel::fs::FsContext &context,
                                         const uint64_t step) {
    const std::string path = OS_TEST_VFS_RANDOM_MISSING_PATH_PREFIX + std::to_string(step);
    os::kernel::fs::Path resolved{};
    return vfs.Resolve(context, reinterpret_cast<const uint8_t *>(path.data()),
                       static_cast<uint64_t>(path.size()),
                       resolved) == os::kernel::fs::Status::NotFound;
}

struct RandomSequenceResult final {
    bool valid;
    uint64_t node_count;
};

[[nodiscard]] RandomSequenceResult RunRandomSequence(os::test::TestContext &test_context,
                                                     os::kernel::fs::Vfs &vfs,
                                                     os::kernel::fs::FsContext &context,
                                                     const std::string_view assertion_message) {
    std::vector<ModelNode> nodes{};
    nodes.push_back(ModelNode{
        .identifier = OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER,
        .parent_identifier = OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER,
        .type = os::kernel::fs::NodeType::Directory,
        .name = {},
        .data = {},
    });
    std::mt19937_64 generator{OS_TEST_VFS_RANDOM_SEED};
    std::uniform_int_distribution<uint64_t> operation_distribution{
        OS_TEST_VFS_RANDOM_EMPTY_VALUE,
        OS_TEST_VFS_RANDOM_OPERATION_KIND_COUNT - OS_TEST_VFS_RANDOM_COUNTER_INCREMENT,
    };
    uint64_t next_name_identifier = OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER;
    uint64_t working_directory_identifier = OS_TEST_VFS_RANDOM_ROOT_IDENTIFIER;
    bool sequence_valid = true;

    for (os::test::TestCount step = OS_TEST_VFS_RANDOM_EMPTY_VALUE;
         step < OS_TEST_VFS_RANDOM_STEP_COUNT; ++step) {
        const uint64_t operation = operation_distribution(generator);
        bool valid = false;
        if (operation == OS_TEST_VFS_RANDOM_CREATE_OPERATION) {
            valid = CreateNode(nodes, vfs, context, next_name_identifier, generator);
        } else if (operation == OS_TEST_VFS_RANDOM_RESOLVE_OPERATION) {
            valid = ResolveMatches(
                nodes, vfs, context,
                SelectNodeIdentifier(nodes, os::kernel::fs::NodeType::None, generator));
        } else if (operation == OS_TEST_VFS_RANDOM_CHANGE_DIRECTORY_OPERATION) {
            valid = ChangeDirectory(nodes, vfs, context, working_directory_identifier, generator);
        } else if (operation == OS_TEST_VFS_RANDOM_PARENT_OPERATION) {
            valid = MoveToParent(nodes, vfs, context, working_directory_identifier);
        } else if (operation == OS_TEST_VFS_RANDOM_FILE_IO_OPERATION) {
            valid = ExerciseFile(nodes, vfs, context, generator);
        } else if (operation == OS_TEST_VFS_RANDOM_READ_DIRECTORY_OPERATION) {
            valid = ReadDirectory(nodes, vfs, context, generator);
        } else if (operation == OS_TEST_VFS_RANDOM_MISSING_PATH_OPERATION) {
            valid = MissingPathIsRejected(vfs, context, step);
        }
        if (valid && ((step + OS_TEST_VFS_RANDOM_COUNTER_INCREMENT) %
                      OS_TEST_VFS_RANDOM_VALIDATION_INTERVAL) == OS_TEST_VFS_RANDOM_EMPTY_VALUE) {
            valid = vfs.Validate() == os::kernel::fs::Status::Succeeded;
        }
        sequence_valid = sequence_valid && valid;
        test_context.ExpectRandom(valid, assertion_message, OS_TEST_VFS_RANDOM_SEED, step);
    }

    const os::kernel::fs::Statistics statistics = vfs.ReadStatistics();
    os::kernel::fs::ResourceUsage usage{};
    const bool final_valid =
        vfs.Validate() == os::kernel::fs::Status::Succeeded &&
        vfs.ReadResourceUsage(usage) == os::kernel::fs::Status::Succeeded &&
        usage.vnode_count == static_cast<uint64_t>(nodes.size()) &&
        statistics.path_resolution_count >= OS_TEST_VFS_RANDOM_MINIMUM_RESOLUTION_COUNT;
    return RandomSequenceResult{
        .valid = sequence_valid && final_valid,
        .node_count = static_cast<uint64_t>(nodes.size()),
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_RANDOM_SUITE_NAME};
    alignas(OS_TEST_VFS_RANDOM_HEAP_ALIGNMENT_BYTES) static uint8_t
        heap_buffer[OS_TEST_VFS_RANDOM_HEAP_SIZE_BYTES]{};
    os::kernel::KernelHeap heap{};
    os::kernel::fs::Memfs memfs{};
    os::kernel::fs::Mount mounts[OS_TEST_VFS_RANDOM_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs vfs{};
    os::kernel::fs::FsContext context{};
    const bool initialized =
        heap.Initialize(reinterpret_cast<uint64_t>(heap_buffer), sizeof(heap_buffer)) ==
            os::kernel::KernelHeapStatus::Succeeded &&
        memfs.Initialize(
            heap, OS_TEST_VFS_RANDOM_SUPERBLOCK_IDENTIFIER, OS_TEST_VFS_RANDOM_NODE_LIMIT,
            OS_TEST_VFS_RANDOM_MAXIMUM_FILE_SIZE_BYTES) == os::kernel::fs::Status::Succeeded &&
        vfs.Initialize(mounts, OS_TEST_VFS_RANDOM_MOUNT_CAPACITY, memfs.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        vfs.InitializeContext(context) == os::kernel::fs::Status::Succeeded;
    if (!initialized) {
        test_context.Expect(false, OS_TEST_VFS_RANDOM_FINAL_STATE);
        return test_context.ExitCode();
    }

    const RandomSequenceResult memfs_result =
        RunRandomSequence(test_context, vfs, context, OS_TEST_VFS_RANDOM_REFERENCE_MODEL);
    const bool memfs_released =
        memfs.ReadStatistics().active_node_count == memfs_result.node_count &&
        vfs.ReleaseContext(context) == os::kernel::fs::Status::Succeeded &&
        memfs.Destroy() == os::kernel::fs::Status::Succeeded &&
        heap.Validate() == os::kernel::KernelHeapStatus::Succeeded &&
        heap.Statistics().allocation_count == OS_TEST_VFS_RANDOM_EMPTY_VALUE;

    static os::test::SparseMemoryBlockDevice root_device{};
    static os::kernel::fs::RootFileSystem root_file_system{};
    os::kernel::fs::Mount root_mounts[OS_TEST_VFS_RANDOM_MOUNT_CAPACITY]{};
    os::kernel::fs::Vfs root_vfs{};
    os::kernel::fs::FsContext root_context{};
    const bool root_initialized =
        os::test::FormatRootFileSystem(root_device) &&
        root_file_system.Initialize(root_device, OS_TEST_VFS_RANDOM_ROOT_SUPERBLOCK_IDENTIFIER) ==
            os::kernel::fs::Status::Succeeded &&
        root_vfs.Initialize(root_mounts, OS_TEST_VFS_RANDOM_MOUNT_CAPACITY,
                            root_file_system.GetSuperblock()) ==
            os::kernel::fs::Status::Succeeded &&
        root_vfs.InitializeContext(root_context) == os::kernel::fs::Status::Succeeded;
    RandomSequenceResult root_result{};
    if (root_initialized) {
        root_result = RunRandomSequence(test_context, root_vfs, root_context,
                                        OS_TEST_VFS_RANDOM_ROOT_REFERENCE_MODEL);
    }
    const bool root_released =
        root_initialized && root_vfs.Sync() == os::kernel::fs::Status::Succeeded &&
        root_vfs.ReleaseContext(root_context) == os::kernel::fs::Status::Succeeded &&
        root_file_system.ReadStatistics().allocated_inode_count == root_result.node_count;
    test_context.Expect(memfs_result.valid && memfs_released && root_result.valid && root_released,
                        OS_TEST_VFS_RANDOM_FINAL_STATE);
    test_context.Expect(memfs_result.node_count == root_result.node_count,
                        OS_TEST_VFS_RANDOM_BACKEND_PARITY);
    return test_context.ExitCode();
}
