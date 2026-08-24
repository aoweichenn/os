#include <os/kernel/fs/root_directory_index.hpp>
#include <os/kernel/fs/root_inode_metadata.hpp>
#include <test_context.hpp>

#include <array>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_ROOT_DIRECTORY_RANDOM_SUITE_NAME =
    "kernel/root_directory_metadata/randomized";
constexpr std::string_view OS_TEST_ROOT_DIRECTORY_RANDOM_MESSAGE =
    "十万步 HTree/xattr/quota 必须与独立 name/value/usage oracle 一致";
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_SEED = 0x4449524D45544131ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_STEP_COUNT = 100000ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_KEY_COUNT = 128ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_KEY_COUNT = 16ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_COUNT = 8ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_OPERATION_MODULUS = 1000ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_DIRECTORY_INSERT_THRESHOLD = 8ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_DIRECTORY_REMOVE_THRESHOLD = 14ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_DIRECTORY_LOOKUP_THRESHOLD = 30ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_SET_THRESHOLD = 38ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_REMOVE_THRESHOLD = 44ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_GET_THRESHOLD = 52ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_CHARGE_THRESHOLD = 60ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_RELEASE_THRESHOLD = 66ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_OBSERVATION_INTERVAL = 64ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_LEFT_SHIFT = 13ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_RIGHT_SHIFT = 7ULL;
constexpr uint64_t OS_TEST_ROOT_DIRECTORY_RANDOM_FINAL_LEFT_SHIFT = 17ULL;
constexpr os::kernel::fs::RootV5Uuid OS_TEST_ROOT_DIRECTORY_RANDOM_UUID{
    .low = 0x52414E4444495231ULL,
    .high = 0x1020304050607080ULL,
};

[[nodiscard]] uint64_t NextRandom(uint64_t &state) noexcept {
    state ^= state << OS_TEST_ROOT_DIRECTORY_RANDOM_LEFT_SHIFT;
    state ^= state >> OS_TEST_ROOT_DIRECTORY_RANDOM_RIGHT_SHIFT;
    state ^= state << OS_TEST_ROOT_DIRECTORY_RANDOM_FINAL_LEFT_SHIFT;
    return state;
}

void MakeName(const uint64_t key, uint8_t *const name, uint64_t &length) noexcept {
    name[0] = static_cast<uint8_t>('k');
    name[1] = static_cast<uint8_t>('0') + static_cast<uint8_t>((key / 100ULL) % 10ULL);
    name[2] = static_cast<uint8_t>('0') + static_cast<uint8_t>((key / 10ULL) % 10ULL);
    name[3] = static_cast<uint8_t>('0') + static_cast<uint8_t>(key % 10ULL);
    length = 4ULL;
}

}

int main() {
    os::test::TestContext context{OS_TEST_ROOT_DIRECTORY_RANDOM_SUITE_NAME};
    static os::kernel::fs::RootDirectoryIndex directory{};
    os::kernel::fs::RootXattrSet xattrs{};
    os::kernel::fs::RootQuotaManager quota{};
    bool consistent = directory.Initialize(2ULL, 1ULL, OS_TEST_ROOT_DIRECTORY_RANDOM_UUID) ==
                          os::kernel::fs::RootDirectoryStatus::Succeeded &&
                      xattrs.Initialize(32ULL, 1ULL, OS_TEST_ROOT_DIRECTORY_RANDOM_UUID) ==
                          os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
                      quota.Initialize(OS_TEST_ROOT_DIRECTORY_RANDOM_UUID) ==
                          os::kernel::fs::RootInodeMetadataStatus::Succeeded;
    for (uint64_t quota_index = 0ULL;
         consistent && quota_index < OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_COUNT; ++quota_index) {
        consistent =
            quota.SetLimits(os::kernel::fs::RootQuotaType::User, quota_index, 80ULL, 100ULL, 0ULL,
                            0ULL, 1000ULL) == os::kernel::fs::RootInodeMetadataStatus::Succeeded;
    }
    std::array<bool, OS_TEST_ROOT_DIRECTORY_RANDOM_KEY_COUNT> present{};
    std::array<uint64_t, OS_TEST_ROOT_DIRECTORY_RANDOM_KEY_COUNT> inode_numbers{};
    std::array<bool, OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_KEY_COUNT> xattr_present{};
    std::array<uint8_t, OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_KEY_COUNT> xattr_values{};
    std::array<uint64_t, OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_COUNT> quota_usage{};
    uint64_t random_state = OS_TEST_ROOT_DIRECTORY_RANDOM_SEED;
    uint8_t name[8]{};
    uint64_t name_length = 0ULL;

    for (uint64_t step = 0ULL; consistent && step < OS_TEST_ROOT_DIRECTORY_RANDOM_STEP_COUNT;
         ++step) {
        const uint64_t operation =
            NextRandom(random_state) % OS_TEST_ROOT_DIRECTORY_RANDOM_OPERATION_MODULUS;
        const uint64_t key = NextRandom(random_state) % OS_TEST_ROOT_DIRECTORY_RANDOM_KEY_COUNT;
        MakeName(key, name, name_length);
        if (operation < OS_TEST_ROOT_DIRECTORY_RANDOM_DIRECTORY_INSERT_THRESHOLD && !present[key]) {
            const uint64_t inode_number = 16ULL + key;
            consistent = directory.Insert(name, name_length, inode_number, step + 1ULL,
                                          os::kernel::fs::RootV5NodeType::RegularFile) ==
                         os::kernel::fs::RootDirectoryStatus::Succeeded;
            if (consistent) {
                present[key] = true;
                inode_numbers[key] = inode_number;
            }
        } else if (operation < OS_TEST_ROOT_DIRECTORY_RANDOM_DIRECTORY_REMOVE_THRESHOLD &&
                   present[key]) {
            consistent = directory.Remove(name, name_length) ==
                         os::kernel::fs::RootDirectoryStatus::Succeeded;
            if (consistent) {
                present[key] = false;
                inode_numbers[key] = 0ULL;
            }
        } else if (operation < OS_TEST_ROOT_DIRECTORY_RANDOM_DIRECTORY_LOOKUP_THRESHOLD) {
            os::kernel::fs::RootDirectoryEntryV2 entry{};
            const os::kernel::fs::RootDirectoryStatus status =
                directory.Lookup(name, name_length, entry);
            consistent = present[key] ? status == os::kernel::fs::RootDirectoryStatus::Succeeded &&
                                            entry.inode_number == inode_numbers[key]
                                      : status == os::kernel::fs::RootDirectoryStatus::NotFound;
        } else if (operation < OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_SET_THRESHOLD) {
            const uint64_t xattr_key = key % OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_KEY_COUNT;
            MakeName(xattr_key, name, name_length);
            const uint8_t value = static_cast<uint8_t>(NextRandom(random_state));
            consistent =
                xattrs.Set(os::kernel::fs::RootXattrNamespace::User, name, name_length, &value,
                           1ULL) == os::kernel::fs::RootInodeMetadataStatus::Succeeded;
            if (consistent) {
                xattr_present[xattr_key] = true;
                xattr_values[xattr_key] = value;
            }
        } else if (operation < OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_REMOVE_THRESHOLD) {
            const uint64_t xattr_key = key % OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_KEY_COUNT;
            MakeName(xattr_key, name, name_length);
            const os::kernel::fs::RootInodeMetadataStatus status =
                xattrs.Remove(os::kernel::fs::RootXattrNamespace::User, name, name_length);
            consistent = xattr_present[xattr_key]
                             ? status == os::kernel::fs::RootInodeMetadataStatus::Succeeded
                             : status == os::kernel::fs::RootInodeMetadataStatus::NotFound;
            if (consistent && xattr_present[xattr_key]) {
                xattr_present[xattr_key] = false;
            }
        } else if (operation < OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_GET_THRESHOLD) {
            const uint64_t xattr_key = key % OS_TEST_ROOT_DIRECTORY_RANDOM_XATTR_KEY_COUNT;
            MakeName(xattr_key, name, name_length);
            os::kernel::fs::RootXattrEntry entry{};
            const os::kernel::fs::RootInodeMetadataStatus status =
                xattrs.Get(os::kernel::fs::RootXattrNamespace::User, name, name_length, entry);
            consistent = xattr_present[xattr_key]
                             ? status == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
                                   entry.value[0] == xattr_values[xattr_key]
                             : status == os::kernel::fs::RootInodeMetadataStatus::NotFound;
        } else if (operation < OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_CHARGE_THRESHOLD) {
            const uint64_t quota_key = key % OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_COUNT;
            const os::kernel::fs::RootInodeMetadataStatus status = quota.Charge(
                os::kernel::fs::RootQuotaType::User, quota_key, 1ULL, 0ULL, step % 900ULL);
            consistent = quota_usage[quota_key] < 100ULL
                             ? status == os::kernel::fs::RootInodeMetadataStatus::Succeeded
                             : status == os::kernel::fs::RootInodeMetadataStatus::QuotaExceeded;
            if (consistent && quota_usage[quota_key] < 100ULL) {
                ++quota_usage[quota_key];
            }
        } else if (operation < OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_RELEASE_THRESHOLD) {
            const uint64_t quota_key = key % OS_TEST_ROOT_DIRECTORY_RANDOM_QUOTA_COUNT;
            if (quota_usage[quota_key] != 0ULL) {
                consistent =
                    quota.Release(os::kernel::fs::RootQuotaType::User, quota_key, 1ULL, 0ULL) ==
                    os::kernel::fs::RootInodeMetadataStatus::Succeeded;
                if (consistent) {
                    --quota_usage[quota_key];
                }
            }
        }
        if (consistent && step % OS_TEST_ROOT_DIRECTORY_RANDOM_OBSERVATION_INTERVAL == 0ULL) {
            consistent = directory.Validate() == os::kernel::fs::RootDirectoryStatus::Succeeded &&
                         xattrs.Validate() == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
                         quota.Validate() == os::kernel::fs::RootInodeMetadataStatus::Succeeded;
        }
    }
    consistent = consistent &&
                 directory.Validate() == os::kernel::fs::RootDirectoryStatus::Succeeded &&
                 xattrs.Validate() == os::kernel::fs::RootInodeMetadataStatus::Succeeded &&
                 quota.Validate() == os::kernel::fs::RootInodeMetadataStatus::Succeeded;
    context.ExpectRandom(consistent, OS_TEST_ROOT_DIRECTORY_RANDOM_MESSAGE,
                         OS_TEST_ROOT_DIRECTORY_RANDOM_SEED,
                         OS_TEST_ROOT_DIRECTORY_RANDOM_STEP_COUNT);
    return context.ExitCode();
}
