#pragma once

#include "os/kernel/fs/vfs.hpp"

#include <stdint.h>

namespace os::kernel::fs {

inline constexpr uint64_t OS_KERNEL_DEVFS_DEFAULT_DEVICE_CAPACITY = 16ULL;

struct DevfsDevice final {
    uint64_t node_identifier;
    uint64_t generation;
    uint64_t name_length_bytes;
    uint8_t name[OS_KERNEL_VFS_MAXIMUM_NAME_LENGTH_BYTES];
    bool active;
};

struct DevfsStatistics final {
    uint64_t registered_device_count;
    uint64_t active_open_count;
    uint64_t successful_open_count;
    uint64_t directory_read_count;
    uint64_t rejected_registration_count;
};

// v1.18 的最小 devfs 只负责设备命名、只读注册表和 vnode 生命周期。
// 具体字符流仍由 FileDescription 的设备类型实现，不把驱动指针暴露给 VFS。
class Devfs final {
  public:
    Devfs() noexcept = default;
    Devfs(const Devfs &) = delete;
    Devfs &operator=(const Devfs &) = delete;

    [[nodiscard]] Status Initialize(uint64_t superblock_identifier,
                                    DevfsDevice *device_storage,
                                    uint64_t device_capacity) noexcept;
    [[nodiscard]] Status RegisterCharacterDevice(const uint8_t *name,
                                                 uint64_t name_length_bytes,
                                                 uint64_t &node_identifier) noexcept;
    [[nodiscard]] Superblock &GetSuperblock() noexcept;
    [[nodiscard]] const Superblock &GetSuperblock() const noexcept;
    [[nodiscard]] DevfsStatistics ReadStatistics() const noexcept;
    [[nodiscard]] Status Validate() const noexcept;

  private:
    [[nodiscard]] static Status LookupOperation(void *context, const Vnode &directory,
                                                const uint8_t *name,
                                                uint64_t name_length_bytes,
                                                Vnode &vnode) noexcept;
    [[nodiscard]] static Status CreateOperation(void *context, const Vnode &directory,
                                                const uint8_t *name,
                                                uint64_t name_length_bytes, NodeType type,
                                                Vnode &vnode) noexcept;
    [[nodiscard]] static Status OpenOperation(void *context,
                                              const Vnode &vnode) noexcept;
    [[nodiscard]] static Status CloseOperation(void *context,
                                               const Vnode &vnode) noexcept;
    [[nodiscard]] static Status RemoveOperation(void *context, const Vnode &directory,
                                                const uint8_t *name,
                                                uint64_t name_length_bytes,
                                                NodeType expected_type) noexcept;
    [[nodiscard]] static Status RenameOperation(
        void *context, const Vnode &source_directory, const uint8_t *source_name,
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
    [[nodiscard]] static Status ReadDirectoryOperation(
        void *context, const Vnode &directory, uint64_t &cursor, DirectoryEntry &entry,
        bool &end_of_directory) noexcept;
    [[nodiscard]] static Status GetNameOperation(void *context, const Vnode &vnode,
                                                 uint8_t *name,
                                                 uint64_t name_capacity_bytes,
                                                 uint64_t &name_length_bytes) noexcept;
    [[nodiscard]] static Status StatOperation(void *context, const Vnode &vnode,
                                              BackendNodeInformation &information) noexcept;
    [[nodiscard]] static Status SyncOperation(void *context) noexcept;
    [[nodiscard]] static Status ValidateOperation(void *context) noexcept;
    [[nodiscard]] static Status ReadResourceUsageOperation(void *context,
                                                           ResourceUsage &usage) noexcept;

    [[nodiscard]] Vnode MakeRootVnode() noexcept;
    [[nodiscard]] Vnode MakeDeviceVnode(const DevfsDevice &device) noexcept;
    [[nodiscard]] DevfsDevice *FindDevice(uint64_t node_identifier) noexcept;
    [[nodiscard]] const DevfsDevice *
    FindDevice(uint64_t node_identifier) const noexcept;
    [[nodiscard]] bool VnodeIsValid(const Vnode &vnode) const noexcept;

    static const BackendOperations operations;

    Superblock superblock_{};
    DevfsDevice *devices_{};
    uint64_t device_capacity_{};
    mutable SpinLock lock_{};
    DevfsStatistics statistics_{};
    bool initialized_{};
};

}
