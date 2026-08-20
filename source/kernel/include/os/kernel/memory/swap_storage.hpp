#pragma once

#include <os/kernel/fs/block_cache.hpp>
#include <os/kernel/memory/swap_manager.hpp>

#include <stdint.h>

namespace os::kernel {

inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES = 512ULL;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES = 4096ULL;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_DATA_SIZE_BYTES = 30064771072ULL;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY =
    OS_KERNEL_SWAP_STORAGE_DATA_SIZE_BYTES / OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES = 64ULL;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_SUPERBLOCK_SECTOR_COUNT = 8ULL;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_METADATA_START_LBA =
    OS_KERNEL_SWAP_STORAGE_SUPERBLOCK_SECTOR_COUNT;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_METADATA_SIZE_BYTES =
    OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY * OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_METADATA_SECTOR_COUNT =
    OS_KERNEL_SWAP_STORAGE_METADATA_SIZE_BYTES / OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_DATA_START_LBA =
    OS_KERNEL_SWAP_STORAGE_METADATA_START_LBA + OS_KERNEL_SWAP_STORAGE_METADATA_SECTOR_COUNT;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_PAGE_SECTOR_COUNT =
    OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES / OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_IMAGE_SECTOR_COUNT =
    OS_KERNEL_SWAP_STORAGE_DATA_START_LBA +
    OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY * OS_KERNEL_SWAP_STORAGE_PAGE_SECTOR_COUNT;
inline constexpr uint64_t OS_KERNEL_SWAP_STORAGE_IMAGE_SIZE_BYTES =
    OS_KERNEL_SWAP_STORAGE_IMAGE_SECTOR_COUNT * OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES;

static_assert(OS_KERNEL_SWAP_STORAGE_DATA_SIZE_BYTES % OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES ==
              0ULL);
static_assert(OS_KERNEL_SWAP_STORAGE_METADATA_SIZE_BYTES %
                  OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES ==
              0ULL);

enum class SwapStorageStatus : uint64_t {
    Succeeded,
    AlreadyInitialized,
    InvalidDevice,
    ReadFailed,
    WriteFailed,
    FlushFailed,
    InvalidSuperblock,
    GenerationExhausted,
};

class SwapStorage final {
  public:
    [[nodiscard]] SwapStorageStatus Initialize(FileSystemBlockDevice &device) noexcept;
    [[nodiscard]] uint64_t SlotCapacity() const noexcept;
    [[nodiscard]] bool Validate() const noexcept;

    [[nodiscard]] static bool ReadEntryOperation(void *context, uint64_t slot_index,
                                                 SwapSlotEntry &entry) noexcept;
    [[nodiscard]] static bool WriteEntryOperation(void *context, uint64_t slot_index,
                                                  const SwapSlotEntry &entry) noexcept;
    [[nodiscard]] static bool ReadPageOperation(void *context, uint64_t slot_index,
                                                uint8_t *destination,
                                                uint64_t length_bytes) noexcept;
    [[nodiscard]] static bool WritePageOperation(void *context, uint64_t slot_index,
                                                 const uint8_t *source,
                                                 uint64_t length_bytes) noexcept;

  private:
    [[nodiscard]] bool ReadEntry(uint64_t slot_index, SwapSlotEntry &entry) noexcept;
    [[nodiscard]] bool WriteEntry(uint64_t slot_index, const SwapSlotEntry &entry) noexcept;
    [[nodiscard]] bool ReadPage(uint64_t slot_index, uint8_t *destination,
                                uint64_t length_bytes) noexcept;
    [[nodiscard]] bool WritePage(uint64_t slot_index, const uint8_t *source,
                                 uint64_t length_bytes) noexcept;

    FileSystemBlockDevice *device_{};
    uint64_t generation_{};
    uint8_t sector_scratch_[OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES]{};
    bool initialized_{};
};

}
