#include <os/kernel/memory/swap_storage.hpp>

namespace os::kernel {

namespace {

constexpr uint8_t OS_KERNEL_SWAP_STORAGE_MAGIC[] = {'O', 'S', 'S', 'W', 'A', 'P', '0', '1'};
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_FORMAT_VERSION = 1ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_MAGIC_OFFSET_BYTES = 0ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_VERSION_OFFSET_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_OFFSET_BYTES = 16ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_OFFSET_BYTES = 24ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_METADATA_START_OFFSET_BYTES = 48ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_DATA_START_OFFSET_BYTES = 56ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_GENERATION_OFFSET_BYTES = 64ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES = 504ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_ENTRY_CHECKSUM_OFFSET_BYTES = 32ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_ENTRY_RESERVED_OFFSET_BYTES = 40ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_ENTRIES_PER_SECTOR =
    OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES / OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_FNV1A_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_FNV1A_PRIME = 1099511628211ULL;
constexpr uint64_t OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE = 0ULL;

[[nodiscard]] uint64_t LoadLittleEndian64(const uint8_t *const bytes) noexcept {
    uint64_t value = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
    for (uint64_t byte_index = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
         byte_index < OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES; ++byte_index) {
        value |= static_cast<uint64_t>(bytes[byte_index])
                 << (byte_index * OS_KERNEL_SWAP_STORAGE_BITS_PER_BYTE);
    }
    return value;
}

void StoreLittleEndian64(uint8_t *const bytes, const uint64_t value) noexcept {
    for (uint64_t byte_index = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
         byte_index < OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES; ++byte_index) {
        bytes[byte_index] =
            static_cast<uint8_t>(value >> (byte_index * OS_KERNEL_SWAP_STORAGE_BITS_PER_BYTE));
    }
}

[[nodiscard]] uint64_t CalculateChecksum(const uint8_t *const bytes,
                                         const uint64_t length_bytes) noexcept {
    uint64_t checksum = OS_KERNEL_SWAP_STORAGE_FNV1A_OFFSET_BASIS;
    for (uint64_t byte_index = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE; byte_index < length_bytes;
         ++byte_index) {
        checksum ^= static_cast<uint64_t>(bytes[byte_index]);
        checksum *= OS_KERNEL_SWAP_STORAGE_FNV1A_PRIME;
    }
    return checksum;
}

[[nodiscard]] bool MagicMatches(const uint8_t *const sector) noexcept {
    for (uint64_t byte_index = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
         byte_index < sizeof(OS_KERNEL_SWAP_STORAGE_MAGIC); ++byte_index) {
        if (sector[OS_KERNEL_SWAP_STORAGE_MAGIC_OFFSET_BYTES + byte_index] !=
            OS_KERNEL_SWAP_STORAGE_MAGIC[byte_index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ReservedBytesAreZero(const uint8_t *const sector) noexcept {
    for (uint64_t byte_index = OS_KERNEL_SWAP_STORAGE_GENERATION_OFFSET_BYTES +
                               OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES;
         byte_index < OS_KERNEL_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES; ++byte_index) {
        if (sector[byte_index] != 0U) {
            return false;
        }
    }
    return true;
}

}

SwapStorageStatus SwapStorage::Initialize(FileSystemBlockDevice &device) noexcept {
    if (this->initialized_) {
        return SwapStorageStatus::AlreadyInitialized;
    }
    if (device.ReadBlock(OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE, this->sector_scratch_,
                         sizeof(this->sector_scratch_)) != FileSystemBlockDeviceStatus::Succeeded) {
        return SwapStorageStatus::ReadFailed;
    }
    const uint64_t stored_checksum =
        LoadLittleEndian64(this->sector_scratch_ + OS_KERNEL_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES);
    if (!MagicMatches(this->sector_scratch_) || !ReservedBytesAreZero(this->sector_scratch_) ||
        LoadLittleEndian64(this->sector_scratch_ + OS_KERNEL_SWAP_STORAGE_VERSION_OFFSET_BYTES) !=
            OS_KERNEL_SWAP_STORAGE_FORMAT_VERSION ||
        LoadLittleEndian64(this->sector_scratch_ +
                           OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_OFFSET_BYTES) !=
            OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES ||
        LoadLittleEndian64(this->sector_scratch_ + OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_OFFSET_BYTES) !=
            OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES ||
        LoadLittleEndian64(this->sector_scratch_ +
                           OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY_OFFSET_BYTES) !=
            OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY ||
        LoadLittleEndian64(this->sector_scratch_ +
                           OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_OFFSET_BYTES) !=
            OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES ||
        LoadLittleEndian64(this->sector_scratch_ +
                           OS_KERNEL_SWAP_STORAGE_METADATA_START_OFFSET_BYTES) !=
            OS_KERNEL_SWAP_STORAGE_METADATA_START_LBA ||
        LoadLittleEndian64(this->sector_scratch_ +
                           OS_KERNEL_SWAP_STORAGE_DATA_START_OFFSET_BYTES) !=
            OS_KERNEL_SWAP_STORAGE_DATA_START_LBA ||
        stored_checksum != CalculateChecksum(this->sector_scratch_,
                                             OS_KERNEL_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES)) {
        return SwapStorageStatus::InvalidSuperblock;
    }
    const uint64_t stored_generation =
        LoadLittleEndian64(this->sector_scratch_ + OS_KERNEL_SWAP_STORAGE_GENERATION_OFFSET_BYTES);
    if (stored_generation == OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE) {
        return SwapStorageStatus::InvalidSuperblock;
    }
    if (stored_generation == UINT64_MAX) {
        return SwapStorageStatus::GenerationExhausted;
    }
    this->generation_ = stored_generation + 1ULL;
    StoreLittleEndian64(this->sector_scratch_ + OS_KERNEL_SWAP_STORAGE_GENERATION_OFFSET_BYTES,
                        this->generation_);
    StoreLittleEndian64(
        this->sector_scratch_ + OS_KERNEL_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES,
        CalculateChecksum(this->sector_scratch_, OS_KERNEL_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES));
    if (device.WriteBlock(OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE, this->sector_scratch_,
                          sizeof(this->sector_scratch_)) !=
        FileSystemBlockDeviceStatus::Succeeded) {
        this->generation_ = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
        return SwapStorageStatus::WriteFailed;
    }
    if (device.Flush() != FileSystemBlockDeviceStatus::Succeeded) {
        this->generation_ = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
        return SwapStorageStatus::FlushFailed;
    }
    this->device_ = &device;
    this->initialized_ = true;
    return SwapStorageStatus::Succeeded;
}

uint64_t SwapStorage::SlotCapacity() const noexcept {
    return this->initialized_ ? OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY
                              : OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
}

bool SwapStorage::Validate() const noexcept {
    return this->initialized_ && this->device_ != nullptr &&
           this->generation_ != OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
}

bool SwapStorage::ReadEntryOperation(void *const context, const uint64_t slot_index,
                                     SwapSlotEntry &entry) noexcept {
    return context != nullptr && static_cast<SwapStorage *>(context)->ReadEntry(slot_index, entry);
}

bool SwapStorage::WriteEntryOperation(void *const context, const uint64_t slot_index,
                                      const SwapSlotEntry &entry) noexcept {
    return context != nullptr && static_cast<SwapStorage *>(context)->WriteEntry(slot_index, entry);
}

bool SwapStorage::ReadPageOperation(void *const context, const uint64_t slot_index,
                                    uint8_t *const destination,
                                    const uint64_t length_bytes) noexcept {
    return context != nullptr &&
           static_cast<SwapStorage *>(context)->ReadPage(slot_index, destination, length_bytes);
}

bool SwapStorage::WritePageOperation(void *const context, const uint64_t slot_index,
                                     const uint8_t *const source,
                                     const uint64_t length_bytes) noexcept {
    return context != nullptr &&
           static_cast<SwapStorage *>(context)->WritePage(slot_index, source, length_bytes);
}

bool SwapStorage::ReadEntry(const uint64_t slot_index, SwapSlotEntry &entry) noexcept {
    entry = SwapSlotEntry{};
    if (!this->Validate() || slot_index >= OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY) {
        return false;
    }
    const uint64_t sector_index = OS_KERNEL_SWAP_STORAGE_METADATA_START_LBA +
                                  slot_index / OS_KERNEL_SWAP_STORAGE_ENTRIES_PER_SECTOR;
    const uint64_t entry_offset_bytes = (slot_index % OS_KERNEL_SWAP_STORAGE_ENTRIES_PER_SECTOR) *
                                        OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES;
    if (this->device_->ReadBlock(sector_index, this->sector_scratch_,
                                 sizeof(this->sector_scratch_)) !=
        FileSystemBlockDeviceStatus::Succeeded) {
        return false;
    }
    const uint8_t *const encoded_entry = this->sector_scratch_ + entry_offset_bytes;
    if (LoadLittleEndian64(encoded_entry) != this->generation_) {
        entry.state = SwapSlotState::Empty;
        return true;
    }
    if (LoadLittleEndian64(encoded_entry + OS_KERNEL_SWAP_STORAGE_ENTRY_CHECKSUM_OFFSET_BYTES) !=
        CalculateChecksum(encoded_entry, OS_KERNEL_SWAP_STORAGE_ENTRY_CHECKSUM_OFFSET_BYTES)) {
        return false;
    }
    for (uint64_t byte_index = OS_KERNEL_SWAP_STORAGE_ENTRY_RESERVED_OFFSET_BYTES;
         byte_index < OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES; ++byte_index) {
        if (encoded_entry[byte_index] != 0U) {
            return false;
        }
    }
    const uint64_t address_space_identifier =
        LoadLittleEndian64(encoded_entry + OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES);
    if (address_space_identifier == OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE) {
        entry.state = SwapSlotState::Tombstone;
        return true;
    }
    entry = SwapSlotEntry{
        .identity =
            SwapPageIdentity{
                .address_space_identifier = address_space_identifier,
                .virtual_address = LoadLittleEndian64(
                    encoded_entry + 2ULL * OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES),
            },
        .checksum =
            LoadLittleEndian64(encoded_entry + 3ULL * OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES),
        .state = SwapSlotState::Active,
    };
    return true;
}

bool SwapStorage::WriteEntry(const uint64_t slot_index, const SwapSlotEntry &entry) noexcept {
    if (!this->Validate() || slot_index >= OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY ||
        (entry.state != SwapSlotState::Active && entry.state != SwapSlotState::Tombstone)) {
        return false;
    }
    const uint64_t sector_index = OS_KERNEL_SWAP_STORAGE_METADATA_START_LBA +
                                  slot_index / OS_KERNEL_SWAP_STORAGE_ENTRIES_PER_SECTOR;
    const uint64_t entry_offset_bytes = (slot_index % OS_KERNEL_SWAP_STORAGE_ENTRIES_PER_SECTOR) *
                                        OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES;
    if (this->device_->ReadBlock(sector_index, this->sector_scratch_,
                                 sizeof(this->sector_scratch_)) !=
        FileSystemBlockDeviceStatus::Succeeded) {
        return false;
    }
    uint8_t *const encoded_entry = this->sector_scratch_ + entry_offset_bytes;
    for (uint64_t byte_index = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
         byte_index < OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES; ++byte_index) {
        encoded_entry[byte_index] = 0U;
    }
    StoreLittleEndian64(encoded_entry, this->generation_);
    StoreLittleEndian64(encoded_entry + OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES,
                        entry.state == SwapSlotState::Active
                            ? entry.identity.address_space_identifier
                            : OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE);
    StoreLittleEndian64(encoded_entry + 2ULL * OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES,
                        entry.state == SwapSlotState::Active ? entry.identity.virtual_address
                                                             : OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE);
    StoreLittleEndian64(encoded_entry + 3ULL * OS_KERNEL_SWAP_STORAGE_UINT64_SIZE_BYTES,
                        entry.state == SwapSlotState::Active ? entry.checksum
                                                             : OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE);
    StoreLittleEndian64(
        encoded_entry + OS_KERNEL_SWAP_STORAGE_ENTRY_CHECKSUM_OFFSET_BYTES,
        CalculateChecksum(encoded_entry, OS_KERNEL_SWAP_STORAGE_ENTRY_CHECKSUM_OFFSET_BYTES));
    return this->device_->WriteBlock(sector_index, this->sector_scratch_,
                                     sizeof(this->sector_scratch_)) ==
               FileSystemBlockDeviceStatus::Succeeded &&
           this->device_->Flush() == FileSystemBlockDeviceStatus::Succeeded;
}

bool SwapStorage::ReadPage(const uint64_t slot_index, uint8_t *const destination,
                           const uint64_t length_bytes) noexcept {
    if (!this->Validate() || destination == nullptr ||
        slot_index >= OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY ||
        length_bytes != OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES) {
        return false;
    }
    const uint64_t first_sector = OS_KERNEL_SWAP_STORAGE_DATA_START_LBA +
                                  slot_index * OS_KERNEL_SWAP_STORAGE_PAGE_SECTOR_COUNT;
    for (uint64_t sector_offset = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
         sector_offset < OS_KERNEL_SWAP_STORAGE_PAGE_SECTOR_COUNT; ++sector_offset) {
        if (this->device_->ReadBlock(first_sector + sector_offset,
                                     destination +
                                         sector_offset * OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES,
                                     OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES) !=
            FileSystemBlockDeviceStatus::Succeeded) {
            return false;
        }
    }
    return true;
}

bool SwapStorage::WritePage(const uint64_t slot_index, const uint8_t *const source,
                            const uint64_t length_bytes) noexcept {
    if (!this->Validate() || source == nullptr ||
        slot_index >= OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY ||
        length_bytes != OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES) {
        return false;
    }
    const uint64_t first_sector = OS_KERNEL_SWAP_STORAGE_DATA_START_LBA +
                                  slot_index * OS_KERNEL_SWAP_STORAGE_PAGE_SECTOR_COUNT;
    for (uint64_t sector_offset = OS_KERNEL_SWAP_STORAGE_EMPTY_VALUE;
         sector_offset < OS_KERNEL_SWAP_STORAGE_PAGE_SECTOR_COUNT; ++sector_offset) {
        if (this->device_->WriteBlock(first_sector + sector_offset,
                                      source +
                                          sector_offset * OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES,
                                      OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES) !=
            FileSystemBlockDeviceStatus::Succeeded) {
            return false;
        }
    }
    return this->device_->Flush() == FileSystemBlockDeviceStatus::Succeeded;
}

}
