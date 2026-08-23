#include <os/kernel/fs/vfs_namespace_backing.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_VFS_NAMESPACE_BACKING_SUITE_NAME =
    "kernel/vfs_namespace_backing";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_BACKING_LAYOUT_MESSAGE =
    "命名空间 backing 必须按类型对齐并向上取整到完整物理页";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_BACKING_VIEW_MESSAGE =
    "稳定区与可释放 hash 层必须构造为互不混淆的强类型视图";
constexpr std::string_view OS_TEST_VFS_NAMESPACE_BACKING_REJECTION_MESSAGE =
    "非法容量、溢出、未对齐和不足 backing 必须 fail closed";
constexpr uint64_t OS_TEST_VFS_NAMESPACE_BACKING_PAGE_SIZE_BYTES = 4096ULL;
constexpr uint64_t OS_TEST_VFS_NAMESPACE_BACKING_STORAGE_SIZE_BYTES = 128ULL * 1024ULL;

[[nodiscard]] os::kernel::fs::VfsNamespaceBackingConfiguration MakeConfiguration() noexcept {
    return os::kernel::fs::VfsNamespaceBackingConfiguration{
        .dentry_capacity = 8ULL,
        .inode_capacity = 4ULL,
        .preferred_dentry_bucket_capacity = 16ULL,
        .preferred_inode_bucket_capacity = 8ULL,
        .compact_dentry_bucket_capacity = 8ULL,
        .compact_inode_bucket_capacity = 4ULL,
        .resolution_context_capacity = 3ULL,
        .page_size_bytes = OS_TEST_VFS_NAMESPACE_BACKING_PAGE_SIZE_BYTES,
    };
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_VFS_NAMESPACE_BACKING_SUITE_NAME};
    const os::kernel::fs::VfsNamespaceBackingConfiguration configuration = MakeConfiguration();
    os::kernel::fs::VfsNamespaceBackingLayout layout{};
    const bool layout_valid =
        os::kernel::fs::CalculateVfsNamespaceBackingLayout(configuration, layout) ==
            os::kernel::fs::VfsNamespaceBackingStatus::Succeeded &&
        layout.stable_size_bytes != 0ULL && layout.preferred_size_bytes != 0ULL &&
        layout.stable_size_bytes % OS_TEST_VFS_NAMESPACE_BACKING_PAGE_SIZE_BYTES == 0ULL &&
        layout.preferred_size_bytes % OS_TEST_VFS_NAMESPACE_BACKING_PAGE_SIZE_BYTES == 0ULL &&
        layout.stable_page_count * OS_TEST_VFS_NAMESPACE_BACKING_PAGE_SIZE_BYTES ==
            layout.stable_size_bytes &&
        layout.preferred_page_count * OS_TEST_VFS_NAMESPACE_BACKING_PAGE_SIZE_BYTES ==
            layout.preferred_size_bytes &&
        layout.inode_storage_offset_bytes % alignof(os::kernel::fs::VfsInodeSlot) == 0ULL &&
        layout.resolution_context_offset_bytes % alignof(os::kernel::fs::VfsResolutionContext) ==
            0ULL;
    test_context.Expect(layout_valid, OS_TEST_VFS_NAMESPACE_BACKING_LAYOUT_MESSAGE);

    alignas(OS_TEST_VFS_NAMESPACE_BACKING_PAGE_SIZE_BYTES) static uint8_t
        stable_storage[OS_TEST_VFS_NAMESPACE_BACKING_STORAGE_SIZE_BYTES]{};
    alignas(OS_TEST_VFS_NAMESPACE_BACKING_PAGE_SIZE_BYTES) static uint8_t
        preferred_storage[OS_TEST_VFS_NAMESPACE_BACKING_STORAGE_SIZE_BYTES]{};
    os::kernel::fs::VfsNamespaceBackingView view{};
    const bool view_valid =
        layout_valid &&
        os::kernel::fs::BuildVfsNamespaceBackingView(
            configuration, layout, reinterpret_cast<uint64_t>(stable_storage),
            sizeof(stable_storage), reinterpret_cast<uint64_t>(preferred_storage),
            sizeof(preferred_storage),
            view) == os::kernel::fs::VfsNamespaceBackingStatus::Succeeded &&
        reinterpret_cast<uint64_t>(view.dentry_storage) ==
            reinterpret_cast<uint64_t>(stable_storage) + layout.dentry_storage_offset_bytes &&
        reinterpret_cast<uint64_t>(view.resolution_contexts) ==
            reinterpret_cast<uint64_t>(stable_storage) + layout.resolution_context_offset_bytes &&
        reinterpret_cast<uint64_t>(view.preferred_dentry_hash_buckets) ==
            reinterpret_cast<uint64_t>(preferred_storage) +
                layout.preferred_dentry_bucket_offset_bytes &&
        reinterpret_cast<uint64_t>(view.preferred_inode_hash_buckets) ==
            reinterpret_cast<uint64_t>(preferred_storage) +
                layout.preferred_inode_bucket_offset_bytes;
    test_context.Expect(view_valid, OS_TEST_VFS_NAMESPACE_BACKING_VIEW_MESSAGE);

    os::kernel::fs::VfsNamespaceBackingConfiguration invalid_configuration = configuration;
    invalid_configuration.compact_dentry_bucket_capacity = configuration.dentry_capacity - 1ULL;
    os::kernel::fs::VfsNamespaceBackingLayout rejected_layout{};
    bool rejection_valid = os::kernel::fs::CalculateVfsNamespaceBackingLayout(invalid_configuration,
                                                                              rejected_layout) ==
                           os::kernel::fs::VfsNamespaceBackingStatus::InvalidConfiguration;
    invalid_configuration = configuration;
    invalid_configuration.dentry_capacity = UINT64_MAX;
    invalid_configuration.preferred_dentry_bucket_capacity = UINT64_MAX;
    invalid_configuration.compact_dentry_bucket_capacity = UINT64_MAX;
    rejection_valid =
        rejection_valid && os::kernel::fs::CalculateVfsNamespaceBackingLayout(invalid_configuration,
                                                                              rejected_layout) ==
                               os::kernel::fs::VfsNamespaceBackingStatus::ArithmeticOverflow;
    os::kernel::fs::VfsNamespaceBackingView rejected_view{};
    rejection_valid =
        rejection_valid &&
        os::kernel::fs::BuildVfsNamespaceBackingView(
            configuration, layout, reinterpret_cast<uint64_t>(stable_storage) + 1ULL,
            sizeof(stable_storage) - 1ULL, reinterpret_cast<uint64_t>(preferred_storage),
            sizeof(preferred_storage),
            rejected_view) == os::kernel::fs::VfsNamespaceBackingStatus::MisalignedStorage &&
        os::kernel::fs::BuildVfsNamespaceBackingView(
            configuration, layout, reinterpret_cast<uint64_t>(stable_storage),
            layout.stable_size_bytes - 1ULL, reinterpret_cast<uint64_t>(preferred_storage),
            sizeof(preferred_storage),
            rejected_view) == os::kernel::fs::VfsNamespaceBackingStatus::InsufficientStorage;
    test_context.Expect(rejection_valid, OS_TEST_VFS_NAMESPACE_BACKING_REJECTION_MESSAGE);
    return test_context.ExitCode();
}
