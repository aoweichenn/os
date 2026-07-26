#pragma once

#include "os/kernel/fs/file_system.hpp"
#include "os/kernel/fs/vfs.hpp"

#include <stdint.h>

namespace os::kernel::fs {

[[nodiscard]] Status ToVfsStatus(FileSystemStatus status) noexcept;
[[nodiscard]] FileSystemStatus ToFileSystemStatus(Status status) noexcept;

class LegacyFileSystem final {
  public:
    LegacyFileSystem() noexcept = default;
    LegacyFileSystem(const LegacyFileSystem &) = delete;
    LegacyFileSystem &operator=(const LegacyFileSystem &) = delete;

    [[nodiscard]] Status Initialize(FileSystem &file_system, uint64_t superblock_identifier,
                                    bool read_only = false) noexcept;
    [[nodiscard]] Superblock &GetSuperblock() noexcept;
    [[nodiscard]] const Superblock &GetSuperblock() const noexcept;

  private:
    [[nodiscard]] static Status LookupOperation(void *context, const Vnode &directory,
                                                const uint8_t *name, uint64_t name_length_bytes,
                                                Vnode &vnode) noexcept;
    [[nodiscard]] static Status CreateOperation(void *context, const Vnode &directory,
                                                const uint8_t *name, uint64_t name_length_bytes,
                                                NodeType type, Vnode &vnode) noexcept;
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
    [[nodiscard]] static Status SyncOperation(void *context) noexcept;
    [[nodiscard]] static Status ValidateOperation(void *context) noexcept;
    [[nodiscard]] static Status ReadResourceUsageOperation(void *context,
                                                           ResourceUsage &usage) noexcept;

    [[nodiscard]] Vnode MakeVnode(uint64_t inode_number, FileSystemNodeType type) noexcept;
    [[nodiscard]] bool VnodeIsValid(const Vnode &vnode) const noexcept;

    static const BackendOperations operations;

    FileSystem *file_system_{nullptr};
    Superblock superblock_{};
    bool initialized_{};
};

}
