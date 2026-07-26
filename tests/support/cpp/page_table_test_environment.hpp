#pragma once

#include "os/kernel/memory/page_table.hpp"

#include <stdint.h>

namespace os::test {

inline constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_PAGE_COUNT = 4096ULL;
inline constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES =
    OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_PAGE_COUNT * kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_FRAME_STATES_PER_BYTE = 4ULL;
inline constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_STATE_STORAGE_SIZE_BYTES =
    OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_PAGE_COUNT /
    OS_TEST_PAGE_TABLE_ENVIRONMENT_FRAME_STATES_PER_BYTE;
inline constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_KIBIBYTE_SIZE_BYTES = 1024ULL;
inline constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_BUDDY_STORAGE_SIZE_KIBIBYTES = 64ULL;
inline constexpr uint64_t OS_TEST_PAGE_TABLE_ENVIRONMENT_BUDDY_STORAGE_SIZE_BYTES =
    OS_TEST_PAGE_TABLE_ENVIRONMENT_BUDDY_STORAGE_SIZE_KIBIBYTES *
    OS_TEST_PAGE_TABLE_ENVIRONMENT_KIBIBYTE_SIZE_BYTES;

class PageTableTestEnvironment final {
  public:
    explicit PageTableTestEnvironment(kernel::PageTableRootKind root_kind) noexcept;
    PageTableTestEnvironment(const PageTableTestEnvironment &) = delete;
    PageTableTestEnvironment &operator=(const PageTableTestEnvironment &) = delete;

    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] kernel::PhysicalFrameAllocator &FrameAllocator() noexcept;
    [[nodiscard]] kernel::PageTableManager &PageTableManager() noexcept;
    [[nodiscard]] kernel::PageTableMemoryAccess MemoryAccess() const noexcept;
    [[nodiscard]] uint64_t *TableAt(uint64_t physical_address) noexcept;
    [[nodiscard]] uint8_t *PhysicalMemory() noexcept;

  private:
    alignas(kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) uint8_t
        physical_memory_[OS_TEST_PAGE_TABLE_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES];
    uint8_t state_storage_[OS_TEST_PAGE_TABLE_ENVIRONMENT_STATE_STORAGE_SIZE_BYTES];
    uint8_t buddy_storage_[OS_TEST_PAGE_TABLE_ENVIRONMENT_BUDDY_STORAGE_SIZE_BYTES];
    kernel::PhysicalFrameAllocator frame_allocator_;
    kernel::PageTableManager page_table_manager_;
};

}
