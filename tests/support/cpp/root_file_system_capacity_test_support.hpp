#pragma once

#include "os/kernel/fs/block_cache.hpp"

#include <stdint.h>

namespace os::test {

inline constexpr uint8_t OS_TEST_ROOTFS_CAPACITY_TARGET_PATH[] = {
    '/', 'c', 'a', 'p', 'a', 'c', 'i', 't', 'y',
};

struct RootFileSystemCapacityImageInformation final {
    uint64_t free_data_block_count;
    uint64_t reservoir_file_count;
    uint64_t allocated_inode_count;
};

// 构造一个盘面一致、只剩少量空闲块的真实 256 MiB rootfs，用于直接验证
// short write 与 ENOSPC；大块零数据保持稀疏，不把宿主内存测试变成吞吐测试。
[[nodiscard]] bool
FormatNearCapacityRootFileSystem(os::kernel::FileSystemBlockDevice &device,
                                 uint64_t requested_free_data_block_count,
                                 RootFileSystemCapacityImageInformation &information) noexcept;

}
