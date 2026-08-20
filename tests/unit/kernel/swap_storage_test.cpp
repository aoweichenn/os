#include <os/kernel/memory/swap_manager.hpp>
#include <os/kernel/memory/swap_storage.hpp>
#include <test_context.hpp>

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_SWAP_STORAGE_SUITE_NAME = "kernel/swap_storage/unit";
constexpr std::string_view OS_TEST_SWAP_STORAGE_ROUND_TRIP =
    "独立 ATA 交换盘必须按数据后元数据顺序完成校验往返";
constexpr std::string_view OS_TEST_SWAP_STORAGE_GENERATION =
    "重启代次必须让旧交换映射失效而无需扫描 28 GiB 数据区";
constexpr std::string_view OS_TEST_SWAP_STORAGE_BOUNDARY = "最后交换槽必须精确落在镜像最后扇区以内";
constexpr std::string_view OS_TEST_SWAP_STORAGE_METADATA_CORRUPTION =
    "交换身份元数据损坏必须显式失败而不能表现为未映射";
constexpr std::string_view OS_TEST_SWAP_STORAGE_FAILURE = "交换盘必须拒绝读失败和无效 superblock";

constexpr uint64_t OS_TEST_SWAP_STORAGE_RECORD_CAPACITY = 32ULL;
constexpr uint64_t OS_TEST_SWAP_STORAGE_EMPTY_LBA = UINT64_MAX;
constexpr uint64_t OS_TEST_SWAP_STORAGE_FORMAT_VERSION = 1ULL;
constexpr uint64_t OS_TEST_SWAP_STORAGE_INITIAL_GENERATION = 1ULL;
constexpr uint64_t OS_TEST_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES = 504ULL;
constexpr uint64_t OS_TEST_SWAP_STORAGE_FNV1A_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t OS_TEST_SWAP_STORAGE_FNV1A_PRIME = 1099511628211ULL;
constexpr uint64_t OS_TEST_SWAP_STORAGE_BITS_PER_BYTE = 8ULL;
constexpr uint64_t OS_TEST_SWAP_STORAGE_UINT64_SIZE_BYTES = 8ULL;
constexpr uint64_t OS_TEST_SWAP_STORAGE_ENTRIES_PER_SECTOR =
    os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES /
    os::kernel::OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES;
constexpr uint8_t OS_TEST_SWAP_STORAGE_MAGIC[] = {'O', 'S', 'S', 'W', 'A', 'P', '0', '1'};

struct SectorRecord final {
    uint8_t bytes[os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES];
    uint64_t logical_block_address;
    bool active;
};

class SparseSwapDevice final : public os::kernel::FileSystemBlockDevice {
  public:
    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    ReadBlock(const uint64_t logical_block_address, uint8_t *const block,
              const uint64_t block_size_bytes) noexcept override {
        if (this->fail_next_read_) {
            this->fail_next_read_ = false;
            return os::kernel::FileSystemBlockDeviceStatus::ReadFailed;
        }
        if (block == nullptr ||
            block_size_bytes != os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        const SectorRecord *const record = this->Find(logical_block_address);
        for (uint64_t byte_index = 0ULL; byte_index < block_size_bytes; ++byte_index) {
            block[byte_index] = record == nullptr ? 0U : record->bytes[byte_index];
        }
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus
    WriteBlock(const uint64_t logical_block_address, const uint8_t *const block,
               const uint64_t block_size_bytes) noexcept override {
        if (this->fail_next_write_) {
            this->fail_next_write_ = false;
            return os::kernel::FileSystemBlockDeviceStatus::WriteFailed;
        }
        if (block == nullptr ||
            block_size_bytes != os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES) {
            return os::kernel::FileSystemBlockDeviceStatus::InvalidBuffer;
        }
        SectorRecord *record = this->Find(logical_block_address);
        if (record == nullptr) {
            record = this->Acquire(logical_block_address);
        }
        if (record == nullptr) {
            return os::kernel::FileSystemBlockDeviceStatus::WriteFailed;
        }
        for (uint64_t byte_index = 0ULL; byte_index < block_size_bytes; ++byte_index) {
            record->bytes[byte_index] = block[byte_index];
        }
        if (logical_block_address > this->highest_written_lba_) {
            this->highest_written_lba_ = logical_block_address;
        }
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    [[nodiscard]] os::kernel::FileSystemBlockDeviceStatus Flush() noexcept override {
        if (this->fail_next_flush_) {
            this->fail_next_flush_ = false;
            return os::kernel::FileSystemBlockDeviceStatus::FlushFailed;
        }
        ++this->flush_count_;
        return os::kernel::FileSystemBlockDeviceStatus::Succeeded;
    }

    void SeedSuperblock(const uint8_t *const sector) noexcept {
        static_cast<void>(
            this->WriteBlock(0ULL, sector, os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES));
        this->highest_written_lba_ = 0ULL;
    }

    void FailNextRead() noexcept { this->fail_next_read_ = true; }

    void CorruptByte(const uint64_t logical_block_address, const uint64_t byte_offset) noexcept {
        SectorRecord *const record = this->Find(logical_block_address);
        if (record != nullptr &&
            byte_offset < os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES) {
            record->bytes[byte_offset] ^= 0x01U;
        }
    }

    [[nodiscard]] uint64_t HighestWrittenLba() const noexcept { return this->highest_written_lba_; }

    [[nodiscard]] uint64_t FlushCount() const noexcept { return this->flush_count_; }

  private:
    [[nodiscard]] SectorRecord *Find(const uint64_t logical_block_address) noexcept {
        for (uint64_t record_index = 0ULL; record_index < OS_TEST_SWAP_STORAGE_RECORD_CAPACITY;
             ++record_index) {
            SectorRecord &record = this->records_[record_index];
            if (record.active && record.logical_block_address == logical_block_address) {
                return &record;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const SectorRecord *Find(const uint64_t logical_block_address) const noexcept {
        for (uint64_t record_index = 0ULL; record_index < OS_TEST_SWAP_STORAGE_RECORD_CAPACITY;
             ++record_index) {
            const SectorRecord &record = this->records_[record_index];
            if (record.active && record.logical_block_address == logical_block_address) {
                return &record;
            }
        }
        return nullptr;
    }

    [[nodiscard]] SectorRecord *Acquire(const uint64_t logical_block_address) noexcept {
        for (uint64_t record_index = 0ULL; record_index < OS_TEST_SWAP_STORAGE_RECORD_CAPACITY;
             ++record_index) {
            SectorRecord &record = this->records_[record_index];
            if (!record.active) {
                record = SectorRecord{
                    .bytes = {},
                    .logical_block_address = logical_block_address,
                    .active = true,
                };
                return &record;
            }
        }
        return nullptr;
    }

    SectorRecord records_[OS_TEST_SWAP_STORAGE_RECORD_CAPACITY]{};
    uint64_t highest_written_lba_{OS_TEST_SWAP_STORAGE_EMPTY_LBA};
    uint64_t flush_count_{};
    bool fail_next_read_{};
    bool fail_next_write_{};
    bool fail_next_flush_{};
};

void StoreLittleEndian64(uint8_t *const bytes, const uint64_t value) noexcept {
    for (uint64_t byte_index = 0ULL; byte_index < OS_TEST_SWAP_STORAGE_UINT64_SIZE_BYTES;
         ++byte_index) {
        bytes[byte_index] =
            static_cast<uint8_t>(value >> (byte_index * OS_TEST_SWAP_STORAGE_BITS_PER_BYTE));
    }
}

[[nodiscard]] uint64_t CalculateChecksum(const uint8_t *const bytes,
                                         const uint64_t length_bytes) noexcept {
    uint64_t checksum = OS_TEST_SWAP_STORAGE_FNV1A_OFFSET_BASIS;
    for (uint64_t byte_index = 0ULL; byte_index < length_bytes; ++byte_index) {
        checksum ^= static_cast<uint64_t>(bytes[byte_index]);
        checksum *= OS_TEST_SWAP_STORAGE_FNV1A_PRIME;
    }
    return checksum;
}

void EncodeSuperblock(uint8_t *const sector) noexcept {
    for (uint64_t byte_index = 0ULL;
         byte_index < os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES; ++byte_index) {
        sector[byte_index] = 0U;
    }
    for (uint64_t byte_index = 0ULL; byte_index < sizeof(OS_TEST_SWAP_STORAGE_MAGIC);
         ++byte_index) {
        sector[byte_index] = OS_TEST_SWAP_STORAGE_MAGIC[byte_index];
    }
    const uint64_t fields[] = {
        OS_TEST_SWAP_STORAGE_FORMAT_VERSION,
        os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES,
        os::kernel::OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES,
        os::kernel::OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY,
        os::kernel::OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES,
        os::kernel::OS_KERNEL_SWAP_STORAGE_METADATA_START_LBA,
        os::kernel::OS_KERNEL_SWAP_STORAGE_DATA_START_LBA,
        OS_TEST_SWAP_STORAGE_INITIAL_GENERATION,
    };
    for (uint64_t field_index = 0ULL; field_index < sizeof(fields) / sizeof(fields[0]);
         ++field_index) {
        StoreLittleEndian64(sector + OS_TEST_SWAP_STORAGE_UINT64_SIZE_BYTES +
                                field_index * OS_TEST_SWAP_STORAGE_UINT64_SIZE_BYTES,
                            fields[field_index]);
    }
    StoreLittleEndian64(sector + OS_TEST_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES,
                        CalculateChecksum(sector, OS_TEST_SWAP_STORAGE_CHECKSUM_OFFSET_BYTES));
}

void FillPage(uint8_t *const page) noexcept {
    for (uint64_t byte_index = 0ULL;
         byte_index < os::kernel::OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES; ++byte_index) {
        page[byte_index] = static_cast<uint8_t>(byte_index * 37ULL + 0x5AULL);
    }
}

[[nodiscard]] bool PagesEqual(const uint8_t *const left, const uint8_t *const right) noexcept {
    for (uint64_t byte_index = 0ULL;
         byte_index < os::kernel::OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES; ++byte_index) {
        if (left[byte_index] != right[byte_index]) {
            return false;
        }
    }
    return true;
}

}

int main() {
    os::test::TestContext test_context{OS_TEST_SWAP_STORAGE_SUITE_NAME};
    uint8_t superblock[os::kernel::OS_KERNEL_SWAP_STORAGE_SECTOR_SIZE_BYTES]{};
    EncodeSuperblock(superblock);

    SparseSwapDevice device{};
    device.SeedSuperblock(superblock);
    os::kernel::SwapStorage storage{};
    os::kernel::SwapManager manager{};
    uint8_t source[os::kernel::OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES]{};
    uint8_t destination[os::kernel::OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES]{};
    FillPage(source);
    const os::kernel::SwapPageIdentity identity{
        .address_space_identifier = 7ULL,
        .virtual_address = 0x4000ULL,
    };
    uint64_t slot_index = UINT64_MAX;
    const bool round_trip_valid =
        storage.Initialize(device) == os::kernel::SwapStorageStatus::Succeeded &&
        manager.Initialize(storage.SlotCapacity(),
                           os::kernel::OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES, &storage,
                           os::kernel::SwapStorage::ReadEntryOperation,
                           os::kernel::SwapStorage::WriteEntryOperation,
                           os::kernel::SwapStorage::ReadPageOperation,
                           os::kernel::SwapStorage::WritePageOperation) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        manager.Store(identity, source, sizeof(source), slot_index) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        manager.LoadAndRelease(identity, destination, sizeof(destination)) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        PagesEqual(source, destination) && device.FlushCount() >= 3ULL;
    test_context.Expect(round_trip_valid, OS_TEST_SWAP_STORAGE_ROUND_TRIP);

    uint64_t persisted_slot = UINT64_MAX;
    const bool stored_before_restart =
        manager.Store(identity, source, sizeof(source), persisted_slot) ==
        os::kernel::SwapManagerStatus::Succeeded;
    const uint64_t metadata_sector = os::kernel::OS_KERNEL_SWAP_STORAGE_METADATA_START_LBA +
                                     persisted_slot / OS_TEST_SWAP_STORAGE_ENTRIES_PER_SECTOR;
    const uint64_t metadata_identity_offset =
        (persisted_slot % OS_TEST_SWAP_STORAGE_ENTRIES_PER_SECTOR) *
            os::kernel::OS_KERNEL_SWAP_STORAGE_ENTRY_SIZE_BYTES +
        OS_TEST_SWAP_STORAGE_UINT64_SIZE_BYTES;
    device.CorruptByte(metadata_sector, metadata_identity_offset);
    uint64_t corrupt_slot = UINT64_MAX;
    const bool metadata_corruption_valid =
        stored_before_restart && manager.FindSlot(identity, corrupt_slot) ==
                                     os::kernel::SwapManagerStatus::MetadataReadFailed;
    test_context.Expect(metadata_corruption_valid, OS_TEST_SWAP_STORAGE_METADATA_CORRUPTION);
    os::kernel::SwapStorage restarted_storage{};
    os::kernel::SwapManager restarted_manager{};
    uint64_t obsolete_slot = UINT64_MAX;
    const bool generation_valid =
        stored_before_restart &&
        restarted_storage.Initialize(device) == os::kernel::SwapStorageStatus::Succeeded &&
        restarted_manager.Initialize(
            restarted_storage.SlotCapacity(), os::kernel::OS_KERNEL_SWAP_STORAGE_PAGE_SIZE_BYTES,
            &restarted_storage, os::kernel::SwapStorage::ReadEntryOperation,
            os::kernel::SwapStorage::WriteEntryOperation,
            os::kernel::SwapStorage::ReadPageOperation,
            os::kernel::SwapStorage::WritePageOperation) ==
            os::kernel::SwapManagerStatus::Succeeded &&
        restarted_manager.FindSlot(identity, obsolete_slot) ==
            os::kernel::SwapManagerStatus::MappingNotFound;
    test_context.Expect(generation_valid, OS_TEST_SWAP_STORAGE_GENERATION);

    const uint64_t last_slot = os::kernel::OS_KERNEL_SWAP_STORAGE_SLOT_CAPACITY - 1ULL;
    const bool boundary_valid =
        os::kernel::SwapStorage::WritePageOperation(&restarted_storage, last_slot, source,
                                                    sizeof(source)) &&
        device.HighestWrittenLba() == os::kernel::OS_KERNEL_SWAP_STORAGE_IMAGE_SECTOR_COUNT - 1ULL;
    test_context.Expect(boundary_valid, OS_TEST_SWAP_STORAGE_BOUNDARY);

    SparseSwapDevice read_failure_device{};
    read_failure_device.SeedSuperblock(superblock);
    read_failure_device.FailNextRead();
    os::kernel::SwapStorage read_failure_storage{};
    SparseSwapDevice invalid_device{};
    os::kernel::SwapStorage invalid_storage{};
    const bool failure_valid = read_failure_storage.Initialize(read_failure_device) ==
                                   os::kernel::SwapStorageStatus::ReadFailed &&
                               invalid_storage.Initialize(invalid_device) ==
                                   os::kernel::SwapStorageStatus::InvalidSuperblock;
    test_context.Expect(failure_valid, OS_TEST_SWAP_STORAGE_FAILURE);

    return test_context.ExitCode();
}
