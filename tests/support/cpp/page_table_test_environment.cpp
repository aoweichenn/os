#include "page_table_test_environment.hpp"

namespace os::test {

namespace {

constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_SINGLE_ENTRY = 1ULL;
constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_RESERVED_PAGE_COUNT = 1ULL;
constexpr uint32_t OS_TEST_PAGE_TABLE_ENVIRONMENT_MEMORY_ATTRIBUTES = 0U;

}

PageTableTestEnvironment::PageTableTestEnvironment(
    const kernel::PageTableRootKind root_kind) noexcept
    : physical_memory_{}, state_storage_{}, buddy_storage_{},
      frame_allocator_{this->state_storage_,
                       OS_TEST_PAGE_TABLE_ENVIRONMENT_STATE_STORAGE_SIZE_BYTES},
      page_table_manager_{this->frame_allocator_, this->MemoryAccess(), root_kind} {}

bool PageTableTestEnvironment::Initialize() noexcept {
    const kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = OS_TEST_PAGE_TABLE_ENVIRONMENT_EMPTY_VALUE,
            .length_bytes = OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
            .type = kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = OS_TEST_PAGE_TABLE_ENVIRONMENT_MEMORY_ATTRIBUTES,
        },
    };
    return this->frame_allocator_.Initialize(
               memory_map, OS_TEST_PAGE_TABLE_ENVIRONMENT_SINGLE_ENTRY,
               OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES) ==
               kernel::PhysicalFrameAllocatorStatus::Succeeded &&
           this->frame_allocator_.ReserveRange(OS_TEST_PAGE_TABLE_ENVIRONMENT_EMPTY_VALUE,
                                               OS_TEST_PAGE_TABLE_ENVIRONMENT_RESERVED_PAGE_COUNT *
                                                   kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ==
               kernel::PhysicalFrameAllocatorStatus::Succeeded &&
           this->frame_allocator_.ConfigureBuddyStorage(
               this->buddy_storage_, OS_TEST_PAGE_TABLE_ENVIRONMENT_BUDDY_STORAGE_SIZE_BYTES) ==
               kernel::PhysicalFrameAllocatorStatus::Succeeded &&
           this->frame_allocator_.InitializeBuddy() ==
               kernel::PhysicalFrameAllocatorStatus::Succeeded &&
           this->page_table_manager_.Initialize() == kernel::PageTableStatus::Succeeded;
}

kernel::PhysicalFrameAllocator &PageTableTestEnvironment::FrameAllocator() noexcept {
    return this->frame_allocator_;
}

kernel::PageTableManager &PageTableTestEnvironment::PageTableManager() noexcept {
    return this->page_table_manager_;
}

kernel::PageTableMemoryAccess PageTableTestEnvironment::MemoryAccess() const noexcept {
    return kernel::PageTableMemoryAccess{
        .maximum_physical_address_exclusive =
            OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
        .physical_memory_virtual_base = reinterpret_cast<uint64_t>(this->physical_memory_),
        .allocation_maximum_physical_address_exclusive =
            OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
        .invalidate_active_mappings = false,
    };
}

uint64_t *PageTableTestEnvironment::TableAt(const uint64_t physical_address) noexcept {
    return reinterpret_cast<uint64_t *>(this->physical_memory_ + physical_address);
}

uint8_t *PageTableTestEnvironment::PhysicalMemory() noexcept { return this->physical_memory_; }

}

// 宿主测试不执行 INVLPG；页表模型关闭硬件失效动作，但仍保留真实链接边界。
namespace os::kernel {
void InvalidatePage(const uint64_t virtual_address) noexcept { static_cast<void>(virtual_address); }
}
