#pragma once

#include "os/kernel/fs/vfs.hpp"
#include "os/kernel/memory/kernel_heap.hpp"

#include <stdint.h>

namespace os::kernel::fs {

struct MemfsStatistics final {
    uint64_t node_limit;
    uint64_t active_node_count;
    uint64_t active_directory_count;
    uint64_t active_file_count;
    uint64_t active_data_capacity_bytes;
    uint64_t active_heap_consumed_bytes;
    uint64_t active_heap_requested_bytes;
    uint64_t active_heap_allocation_count;
    uint64_t created_node_count;
    uint64_t successful_growth_count;
    uint64_t bytes_read;
    uint64_t bytes_written;
};

class Memfs final {
  public:
    Memfs() noexcept = default;
    Memfs(const Memfs &) = delete;
    Memfs &operator=(const Memfs &) = delete;

    [[nodiscard]] Status Initialize(KernelHeap &heap, uint64_t superblock_identifier,
                                    uint64_t node_limit, uint64_t maximum_file_size_bytes) noexcept;
    [[nodiscard]] Status Destroy() noexcept;
    [[nodiscard]] Superblock &GetSuperblock() noexcept;
    [[nodiscard]] const Superblock &GetSuperblock() const noexcept;
    [[nodiscard]] MemfsStatistics ReadStatistics() const noexcept;

  private:
    struct Node;

    [[nodiscard]] static Status LookupOperation(void *context, const Vnode &directory,
                                                const uint8_t *name, uint64_t name_length_bytes,
                                                Vnode &vnode) noexcept;
    [[nodiscard]] static Status CreateOperation(void *context, const Vnode &directory,
                                                const uint8_t *name, uint64_t name_length_bytes,
                                                NodeType type,
                                                const NodeCreationAttributes &attributes,
                                                Vnode &vnode) noexcept;
    [[nodiscard]] static Status OpenOperation(void *context, const Vnode &vnode) noexcept;
    [[nodiscard]] static Status CloseOperation(void *context, const Vnode &vnode) noexcept;
    [[nodiscard]] static Status RemoveOperation(void *context, const Vnode &directory,
                                                const uint8_t *name, uint64_t name_length_bytes,
                                                NodeType expected_type) noexcept;
    [[nodiscard]] static Status
    RenameOperation(void *context, const Vnode &source_directory, const uint8_t *source_name,
                    uint64_t source_name_length_bytes, const Vnode &destination_directory,
                    const uint8_t *destination_name, uint64_t destination_name_length_bytes,
                    bool replace) noexcept;
    [[nodiscard]] static Status ParentOperation(void *context, const Vnode &vnode,
                                                Vnode &parent) noexcept;
    [[nodiscard]] static Status ReadOperation(void *context, const Vnode &vnode,
                                              uint64_t offset_bytes, uint8_t *destination,
                                              uint64_t capacity_bytes,
                                              uint64_t &read_bytes) noexcept;
    [[nodiscard]] static Status WriteOperation(void *context, const Vnode &vnode,
                                               uint64_t offset_bytes, const uint8_t *source,
                                               uint64_t length_bytes,
                                               uint64_t &written_bytes) noexcept;
    [[nodiscard]] static Status TruncateOperation(void *context, const Vnode &vnode,
                                                  uint64_t size_bytes) noexcept;
    [[nodiscard]] static Status ReadDirectoryOperation(void *context, const Vnode &directory,
                                                       uint64_t &cursor, DirectoryEntry &entry,
                                                       bool &end_of_directory) noexcept;
    [[nodiscard]] static Status GetNameOperation(void *context, const Vnode &vnode, uint8_t *name,
                                                 uint64_t name_capacity_bytes,
                                                 uint64_t &name_length_bytes) noexcept;
    [[nodiscard]] static Status StatOperation(void *context, const Vnode &vnode,
                                              BackendNodeInformation &information) noexcept;
    [[nodiscard]] static Status ChangeModeOperation(void *context, const Vnode &vnode,
                                                    os::abi::FileMode mode) noexcept;
    [[nodiscard]] static Status
    ChangeOwnerOperation(void *context, const Vnode &vnode, os::abi::UserIdentifier user_identifier,
                         os::abi::GroupIdentifier group_identifier) noexcept;
    [[nodiscard]] static Status SyncOperation(void *context) noexcept;
    [[nodiscard]] static Status ValidateOperation(void *context) noexcept;
    [[nodiscard]] static Status ReadResourceUsageOperation(void *context,
                                                           ResourceUsage &usage) noexcept;

    [[nodiscard]] Node *FindNode(uint64_t identifier) noexcept;
    [[nodiscard]] const Node *FindNode(uint64_t identifier) const noexcept;
    [[nodiscard]] Node *ValidateVnode(const Vnode &vnode) noexcept;
    [[nodiscard]] const Node *ValidateVnode(const Vnode &vnode) const noexcept;
    [[nodiscard]] Vnode MakeVnode(const Node &node) noexcept;
    [[nodiscard]] Status EnsureCapacity(Node &node, uint64_t required_capacity_bytes) noexcept;
    [[nodiscard]] Status TryAllocate(uint64_t size_bytes, void *&allocation) noexcept;
    [[nodiscard]] Status TryRelease(void *allocation) noexcept;
    [[nodiscard]] Status ValidateUnlocked() const noexcept;

    static const BackendOperations operations;

    KernelHeap *heap_{nullptr};
    Node *nodes_{nullptr};
    Superblock superblock_{};
    uint64_t node_limit_{};
    uint64_t maximum_file_size_bytes_{};
    uint64_t next_node_identifier_{};
    mutable SpinLock lock_{};
    MemfsStatistics statistics_{};
    bool initialized_{};
};

}
