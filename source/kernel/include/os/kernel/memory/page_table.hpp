#pragma once

#include "os/kernel/memory/physical_frame_allocator.hpp"

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_PAGE_TABLE_ENTRY_COUNT = 512ULL;
inline constexpr uint64_t OS_KERNEL_PAGE_TABLE_LARGE_PAGE_SIZE_BYTES = 2ULL * 1024ULL * 1024ULL;

struct PageTableIndices final {
    uint64_t level4;
    uint64_t level3;
    uint64_t level2;
    uint64_t level1;
};

struct PagePermissions final {
    bool writable;
    bool executable;
    bool user_accessible;
    bool cache_disabled;
    bool copy_on_write{};
};

struct PageMapping final {
    uint64_t physical_address;
    uint64_t page_size_bytes;
    PagePermissions permissions;
    bool accessed;
};

struct PageTableMemoryAccess final {
    uint64_t maximum_physical_address_exclusive;
    uint64_t physical_memory_virtual_base;
    uint64_t allocation_maximum_physical_address_exclusive;
    bool invalidate_active_mappings;
};

enum class PageTableRootKind : uint64_t {
    // 四级根及其全部下级表只属于当前管理器，空分支可一直回收到四级根。
    Exclusive,
    // 四级项会复制到进程根；下级空表可回收，但三级表必须保留为稳定共享入口。
    KernelShared,
    // 只拥有用户程序分支、用户栈分支和克隆的低地址三级表，其余分支均为借用。
    Process,
};

// 撤销成功时返回本次实际释放的页表帧，不把数据页帧计入其中。
struct PageTableUnmapResult final {
    uint64_t reclaimed_level1_table_count;
    uint64_t reclaimed_level2_table_count;
    uint64_t reclaimed_level3_table_count;
    uint64_t reclaimed_table_frame_count;
};

enum class PageTableStatus : uint64_t {
    Succeeded,
    NotInitialized,
    AlreadyInitialized,
    InvalidRootKind,
    InvalidVirtualAddress,
    InvalidPhysicalAddress,
    InvalidAlignment,
    FrameAllocationFailed,
    TemplateRootInvalid,
    UnexpectedLargePage,
    AlreadyMapped,
    NotMapped,
    SharedBranchMutationDenied,
    InvalidTableFrame,
    TableFrameNotOwned,
    FrameReleaseFailed,
    RollbackFailed,
    InvalidMemoryAccess,
};

[[nodiscard]] bool IsCanonicalVirtualAddress(uint64_t virtual_address) noexcept;
[[nodiscard]] PageTableIndices CalculatePageTableIndices(uint64_t virtual_address) noexcept;
[[nodiscard]] bool IsPageTableMemoryAccessValid(PageTableMemoryAccess memory_access) noexcept;
[[nodiscard]] uint64_t EncodePageTableLeafEntry(uint64_t physical_address,
                                                PagePermissions permissions) noexcept;
[[nodiscard]] PageMapping DecodePageTableLeafEntry(uint64_t entry) noexcept;

class PageTableManager final {
  public:
    PageTableManager(PhysicalFrameAllocator &frame_allocator, PageTableMemoryAccess memory_access,
                     PageTableRootKind root_kind) noexcept;
    PageTableManager(PhysicalFrameAllocator &frame_allocator, uint64_t root_physical_address,
                     PageTableMemoryAccess memory_access, PageTableRootKind root_kind) noexcept;
    PageTableManager(const PageTableManager &) = delete;
    PageTableManager &operator=(const PageTableManager &) = delete;

    [[nodiscard]] PageTableStatus Initialize() noexcept;
    [[nodiscard]] PageTableStatus
    InitializeProcessRoot(uint64_t template_root_physical_address) noexcept;
    [[nodiscard]] PageTableStatus ReleaseProcessRoot() noexcept;
    [[nodiscard]] PageTableStatus MapPage(uint64_t virtual_address, uint64_t physical_address,
                                          PagePermissions permissions) noexcept;
    [[nodiscard]] PageTableStatus MapLargePage(uint64_t virtual_address, uint64_t physical_address,
                                               PagePermissions permissions) noexcept;
    [[nodiscard]] PageTableStatus UnmapPage(uint64_t virtual_address) noexcept;
    [[nodiscard]] PageTableStatus UnmapPage(uint64_t virtual_address,
                                            PageTableUnmapResult &result) noexcept;
    [[nodiscard]] PageTableStatus QueryPage(uint64_t virtual_address,
                                            PageMapping &mapping) const noexcept;
    [[nodiscard]] PageTableStatus
    TestAndClearAccessed(uint64_t virtual_address, PageMapping &mapping, bool &accessed) noexcept;
    [[nodiscard]] PageTableStatus
    ReplacePage(uint64_t virtual_address, uint64_t physical_address,
                PagePermissions permissions) noexcept;
    [[nodiscard]] uint64_t RootPhysicalAddress() const noexcept;
    [[nodiscard]] PageTableRootKind RootKind() const noexcept;
    [[nodiscard]] PageTableStatus SetMemoryAccess(PageTableMemoryAccess memory_access) noexcept;

  private:
    struct TableMutation final {
        uint64_t *entry;
        uint64_t original_entry;
        uint64_t table_physical_address;
        bool table_created;
    };

    struct PageTableWalkPath final {
        uint64_t *level4_entry;
        uint64_t *level3_entry;
        uint64_t *level2_entry;
        uint64_t *level1_entry;
        uint64_t level3_physical_address;
        uint64_t level2_physical_address;
        uint64_t level1_physical_address;
    };

    [[nodiscard]] uint64_t *TableAtPhysicalAddress(uint64_t physical_address) const noexcept;
    [[nodiscard]] bool IsPhysicalAddressValid(uint64_t physical_address,
                                              uint64_t page_size_bytes) const noexcept;
    [[nodiscard]] bool CanMutateAddress(PageTableIndices indices) const noexcept;
    [[nodiscard]] bool CanReclaimLevel3Table(PageTableIndices indices) const noexcept;
    [[nodiscard]] bool IsTableEmptyExcept(const uint64_t *table,
                                          uint64_t excluded_entry_index) const noexcept;
    [[nodiscard]] PageTableStatus AllocateTable(uint64_t &physical_address) noexcept;
    [[nodiscard]] PageTableStatus EnsureNextTable(uint64_t &entry, bool user_accessible,
                                                  uint64_t &physical_address,
                                                  TableMutation &mutation) noexcept;
    [[nodiscard]] PageTableStatus MapLeaf(uint64_t virtual_address, uint64_t physical_address,
                                          PagePermissions permissions, bool large_page) noexcept;
    [[nodiscard]] bool RollbackTableMutations(uint64_t virtual_address, TableMutation *mutations,
                                              uint64_t mutation_count) noexcept;
    [[nodiscard]] PageTableStatus WalkToLeaf(uint64_t virtual_address,
                                             PageTableWalkPath &path) const noexcept;
    [[nodiscard]] PageTableStatus ReleaseTableFrame(uint64_t table_physical_address) noexcept;
    [[nodiscard]] PageTableStatus ReleaseOwnedTable(uint64_t table_physical_address,
                                                    uint64_t table_level,
                                                    const uint64_t *ancestor_table_addresses,
                                                    uint64_t ancestor_table_count) noexcept;

    PhysicalFrameAllocator *frame_allocator_;
    uint64_t root_physical_address_;
    PageTableMemoryAccess memory_access_;
    PageTableRootKind root_kind_;
};

}
