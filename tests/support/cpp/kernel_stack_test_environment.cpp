#include "kernel_stack_test_environment.hpp"

namespace os::test {

namespace {

constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_SINGLE_ENTRY = 1ULL;
constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_RESERVED_PAGE_COUNT = 1ULL;
constexpr uint32_t OS_TEST_KERNEL_STACK_ENVIRONMENT_MEMORY_ATTRIBUTES = 0U;

}

KernelStackTestEnvironment::KernelStackTestEnvironment() noexcept
    : physical_memory_{}, state_storage_{}, buddy_storage_{}, descriptors_{}, stacks_{},
      frame_allocator_{this->state_storage_,
                       OS_TEST_KERNEL_STACK_ENVIRONMENT_STATE_STORAGE_SIZE_BYTES},
      virtual_address_allocator_{},
      page_table_manager_{
          this->frame_allocator_,
          kernel::PageTableMemoryAccess{
              .maximum_physical_address_exclusive =
                  OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
              .physical_memory_virtual_base = reinterpret_cast<uint64_t>(this->physical_memory_),
              .allocation_maximum_physical_address_exclusive =
                  OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
              .invalidate_active_mappings = false,
          },
          kernel::PageTableRootKind::KernelShared,
      },
      stack_manager_{
          this->frame_allocator_,
          this->virtual_address_allocator_,
          this->page_table_manager_,
          kernel::KernelStackMemoryAccess{
              .physical_memory_virtual_base = reinterpret_cast<uint64_t>(this->physical_memory_),
              .maximum_physical_address_exclusive =
                  OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
          },
      } {}

bool KernelStackTestEnvironment::Initialize(const uint64_t stack_capacity) noexcept {
    if (stack_capacity == OS_TEST_KERNEL_STACK_ENVIRONMENT_EMPTY_VALUE ||
        stack_capacity > OS_TEST_KERNEL_STACK_ENVIRONMENT_STACK_CAPACITY) {
        return false;
    }
    const kernel::PhysicalMemoryMapEntry memory_map[] = {
        {
            .base_address = OS_TEST_KERNEL_STACK_ENVIRONMENT_EMPTY_VALUE,
            .length_bytes = OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES,
            .type = kernel::OS_KERNEL_MEMORY_MAP_USABLE_REGION_TYPE,
            .attributes = OS_TEST_KERNEL_STACK_ENVIRONMENT_MEMORY_ATTRIBUTES,
        },
    };
    return this->frame_allocator_.Initialize(
               memory_map, OS_TEST_KERNEL_STACK_ENVIRONMENT_SINGLE_ENTRY,
               OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES) ==
               kernel::PhysicalFrameAllocatorStatus::Succeeded &&
           this->frame_allocator_.ReserveRange(
               OS_TEST_KERNEL_STACK_ENVIRONMENT_EMPTY_VALUE,
               OS_TEST_KERNEL_STACK_ENVIRONMENT_RESERVED_PAGE_COUNT *
                   kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) ==
               kernel::PhysicalFrameAllocatorStatus::Succeeded &&
           this->frame_allocator_.ConfigureBuddyStorage(
               this->buddy_storage_, OS_TEST_KERNEL_STACK_ENVIRONMENT_BUDDY_STORAGE_SIZE_BYTES) ==
               kernel::PhysicalFrameAllocatorStatus::Succeeded &&
           this->frame_allocator_.InitializeBuddy() ==
               kernel::PhysicalFrameAllocatorStatus::Succeeded &&
           this->page_table_manager_.Initialize() == kernel::PageTableStatus::Succeeded &&
           this->virtual_address_allocator_.Initialize(
               OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_BASE,
               OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_PAGE_COUNT, this->descriptors_,
               OS_TEST_KERNEL_STACK_ENVIRONMENT_DESCRIPTOR_CAPACITY) ==
               kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
           this->virtual_address_allocator_.ReserveRange(
               OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_BASE,
               OS_TEST_KERNEL_STACK_ENVIRONMENT_RESERVED_PAGE_COUNT) ==
               kernel::KernelVirtualAddressAllocatorStatus::Succeeded &&
           this->stack_manager_.Initialize(this->stacks_, stack_capacity) ==
               kernel::KernelStackManagerStatus::Succeeded;
}

kernel::PhysicalFrameAllocator &KernelStackTestEnvironment::FrameAllocator() noexcept {
    return this->frame_allocator_;
}

kernel::KernelVirtualAddressAllocator &
KernelStackTestEnvironment::VirtualAddressAllocator() noexcept {
    return this->virtual_address_allocator_;
}

kernel::PageTableManager &KernelStackTestEnvironment::PageTableManager() noexcept {
    return this->page_table_manager_;
}

kernel::KernelStackManager &KernelStackTestEnvironment::StackManager() noexcept {
    return this->stack_manager_;
}

uint8_t *KernelStackTestEnvironment::PhysicalMemory() noexcept { return this->physical_memory_; }

const uint8_t *KernelStackTestEnvironment::PhysicalMemory() const noexcept {
    return this->physical_memory_;
}

}

// 宿主测试不执行 INVLPG；页表模型显式关闭失效动作，但链接仍需保留硬件边界。
namespace os::kernel {
void InvalidatePage(const uint64_t virtual_address) noexcept { static_cast<void>(virtual_address); }
}
