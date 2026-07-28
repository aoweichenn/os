#pragma once

#include "os/kernel/fs/vfs.hpp"

#include <stdint.h>

namespace os::kernel::fs {

struct ConsoleDeviceFileSystemStatistics final {
    uint64_t active_open_count;
    uint64_t successful_open_count;
    uint64_t directory_read_count;
};

// v1.15 只冻结 /dev/console 的 VFS vnode 契约；可动态注册设备的 devfs 留到 v1.18。
class ConsoleDeviceFileSystem final {
  public:
    ConsoleDeviceFileSystem() noexcept = default;
    ConsoleDeviceFileSystem(const ConsoleDeviceFileSystem &) = delete;
    ConsoleDeviceFileSystem &operator=(const ConsoleDeviceFileSystem &) = delete;

    [[nodiscard]] Status Initialize(uint64_t superblock_identifier) noexcept;
    [[nodiscard]] Superblock &GetSuperblock() noexcept;
    [[nodiscard]] const Superblock &GetSuperblock() const noexcept;
    [[nodiscard]] ConsoleDeviceFileSystemStatistics ReadStatistics() const noexcept;
    [[nodiscard]] Status Validate() const noexcept;

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

    [[nodiscard]] Vnode MakeRootVnode() noexcept;
    [[nodiscard]] Vnode MakeConsoleVnode() noexcept;
    [[nodiscard]] bool VnodeIsValid(const Vnode &vnode) const noexcept;

    static const BackendOperations operations;

    Superblock superblock_{};
    mutable SpinLock lock_{};
    ConsoleDeviceFileSystemStatistics statistics_{};
    bool initialized_{};
};

}
