#pragma once

#include "os/kernel/memory/kernel_stack_manager.hpp"

#include <stdint.h>

namespace os::test {

inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_PAGE_COUNT = 4096ULL;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES =
    OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_PAGE_COUNT * kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_FRAME_STATE_BITS_PER_PAGE = 2ULL;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_BITS_PER_BYTE = 8ULL;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_STATE_STORAGE_SIZE_BYTES =
    OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_PAGE_COUNT *
    OS_TEST_KERNEL_STACK_ENVIRONMENT_FRAME_STATE_BITS_PER_PAGE /
    OS_TEST_KERNEL_STACK_ENVIRONMENT_BITS_PER_BYTE;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_KIBIBYTE_SIZE_BYTES = 1024ULL;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_BUDDY_STORAGE_SIZE_KIBIBYTES = 64ULL;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_BUDDY_STORAGE_SIZE_BYTES =
    OS_TEST_KERNEL_STACK_ENVIRONMENT_BUDDY_STORAGE_SIZE_KIBIBYTES *
    OS_TEST_KERNEL_STACK_ENVIRONMENT_KIBIBYTE_SIZE_BYTES;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_DESCRIPTOR_CAPACITY = 512ULL;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_STACK_CAPACITY = 256ULL;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_BASE = 0xFFFFC90000000000ULL;
inline constexpr uint64_t OS_TEST_KERNEL_STACK_ENVIRONMENT_WINDOW_PAGE_COUNT = 4096ULL;

class KernelStackTestEnvironment final {
  public:
    KernelStackTestEnvironment() noexcept;
    KernelStackTestEnvironment(const KernelStackTestEnvironment &) = delete;
    KernelStackTestEnvironment &operator=(const KernelStackTestEnvironment &) = delete;

    [[nodiscard]] bool Initialize(uint64_t stack_capacity) noexcept;
    [[nodiscard]] kernel::PhysicalFrameAllocator &FrameAllocator() noexcept;
    [[nodiscard]] kernel::KernelVirtualAddressAllocator &VirtualAddressAllocator() noexcept;
    [[nodiscard]] kernel::PageTableManager &PageTableManager() noexcept;
    [[nodiscard]] kernel::KernelStackManager &StackManager() noexcept;
    [[nodiscard]] uint8_t *PhysicalMemory() noexcept;
    [[nodiscard]] const uint8_t *PhysicalMemory() const noexcept;

  private:
    alignas(kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES) uint8_t
        physical_memory_[OS_TEST_KERNEL_STACK_ENVIRONMENT_PHYSICAL_MEMORY_SIZE_BYTES];
    uint8_t state_storage_[OS_TEST_KERNEL_STACK_ENVIRONMENT_STATE_STORAGE_SIZE_BYTES];
    uint8_t buddy_storage_[OS_TEST_KERNEL_STACK_ENVIRONMENT_BUDDY_STORAGE_SIZE_BYTES];
    kernel::KernelVirtualAddressRangeDescriptor
        descriptors_[OS_TEST_KERNEL_STACK_ENVIRONMENT_DESCRIPTOR_CAPACITY];
    kernel::KernelStack stacks_[OS_TEST_KERNEL_STACK_ENVIRONMENT_STACK_CAPACITY];
    kernel::PhysicalFrameAllocator frame_allocator_;
    kernel::KernelVirtualAddressAllocator virtual_address_allocator_;
    kernel::PageTableManager page_table_manager_;
    kernel::KernelStackManager stack_manager_;
};

}
