#include "os/kernel/user/file_backing.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_FILE_BACKING_SUITE_NAME =
    "kernel/user_file_backing/unit";
constexpr std::string_view OS_TEST_FILE_BACKING_MEMORY =
    "内存 ELF 后端必须持久读取并生成稳定文件身份";
constexpr std::string_view OS_TEST_FILE_BACKING_OWNERSHIP =
    "后端代次与所有者必须阻止陈旧引用和越权释放";
constexpr std::string_view OS_TEST_FILE_BACKING_DRAIN =
    "全部后端释放后固定描述符池必须回到空闲基线";

constexpr uint64_t OS_TEST_FILE_BACKING_EMPTY_VALUE = 0ULL;
constexpr uint64_t OS_TEST_FILE_BACKING_SINGLE_UNIT = 1ULL;
constexpr uint64_t OS_TEST_FILE_BACKING_CAPACITY = 2ULL;
constexpr uint64_t OS_TEST_FILE_BACKING_OWNER_IDENTIFIER = 23ULL;
constexpr uint64_t OS_TEST_FILE_BACKING_FOREIGN_OWNER_IDENTIFIER = 29ULL;
constexpr uint64_t OS_TEST_FILE_BACKING_IMAGE_SIZE_BYTES =
    os::kernel::OS_KERNEL_MEMORY_PAGE_SIZE_BYTES;
constexpr uint64_t OS_TEST_FILE_BACKING_READ_OFFSET_BYTES = 37ULL;
constexpr uint64_t OS_TEST_FILE_BACKING_READ_SIZE_BYTES = 19ULL;
constexpr uint8_t OS_TEST_FILE_BACKING_PATTERN = 0x6DU;

}

int main() {
    os::test::TestContext test_context{
        OS_TEST_FILE_BACKING_SUITE_NAME};
    uint8_t image[OS_TEST_FILE_BACKING_IMAGE_SIZE_BYTES]{};
    for (uint64_t byte_index = OS_TEST_FILE_BACKING_EMPTY_VALUE;
         byte_index < sizeof(image); ++byte_index) {
        image[byte_index] = static_cast<uint8_t>(
            OS_TEST_FILE_BACKING_PATTERN + byte_index);
    }
    os::kernel::UserFileBackingDescriptor
        descriptors[OS_TEST_FILE_BACKING_CAPACITY]{};
    os::kernel::UserFileBackingManager manager{};
    uint64_t descriptor_index = UINT64_MAX;
    uint64_t generation = OS_TEST_FILE_BACKING_EMPTY_VALUE;
    uint8_t output[OS_TEST_FILE_BACKING_READ_SIZE_BYTES]{};
    uint8_t page_output[OS_TEST_FILE_BACKING_IMAGE_SIZE_BYTES]{};
    os::kernel::UserFileBackingDescriptor descriptor{};
    const bool memory_valid =
        manager.Initialize(descriptors,
                           OS_TEST_FILE_BACKING_CAPACITY) ==
            os::kernel::UserFileBackingStatus::Succeeded &&
        manager.AcquireMemoryImage(
            OS_TEST_FILE_BACKING_OWNER_IDENTIFIER, image,
            sizeof(image), descriptor_index, generation) ==
            os::kernel::UserFileBackingStatus::Succeeded &&
        descriptor_index != UINT64_MAX &&
        generation != OS_TEST_FILE_BACKING_EMPTY_VALUE &&
        manager.Read(
            descriptor_index, generation,
            OS_TEST_FILE_BACKING_READ_OFFSET_BYTES, output,
            sizeof(output)) ==
            os::kernel::UserFileBackingStatus::Succeeded &&
        output[OS_TEST_FILE_BACKING_EMPTY_VALUE] ==
            image[OS_TEST_FILE_BACKING_READ_OFFSET_BYTES] &&
        output[sizeof(output) -
               OS_TEST_FILE_BACKING_SINGLE_UNIT] ==
            image[OS_TEST_FILE_BACKING_READ_OFFSET_BYTES +
                  sizeof(output) -
                  OS_TEST_FILE_BACKING_SINGLE_UNIT] &&
        manager.ReadDescriptor(descriptor_index, generation,
                               descriptor) ==
            os::kernel::UserFileBackingStatus::Succeeded &&
        os::kernel::ReadUserFileBackingPage(
            &descriptor,
            os::kernel::FilePageIdentity{
                .file = descriptor.identity,
                .page_index =
                    OS_TEST_FILE_BACKING_EMPTY_VALUE,
            },
            page_output, sizeof(page_output)) &&
        page_output[OS_TEST_FILE_BACKING_READ_OFFSET_BYTES] ==
            image[OS_TEST_FILE_BACKING_READ_OFFSET_BYTES];
    test_context.Expect(memory_valid,
                        OS_TEST_FILE_BACKING_MEMORY);

    const bool ownership_valid =
        manager.Release(
            OS_TEST_FILE_BACKING_FOREIGN_OWNER_IDENTIFIER,
            descriptor_index, generation) ==
            os::kernel::UserFileBackingStatus::OwnershipMismatch &&
        manager.Release(OS_TEST_FILE_BACKING_OWNER_IDENTIFIER,
                        descriptor_index, generation) ==
            os::kernel::UserFileBackingStatus::Succeeded &&
        manager.Read(descriptor_index, generation,
                     OS_TEST_FILE_BACKING_EMPTY_VALUE, output,
                     sizeof(output)) ==
            os::kernel::UserFileBackingStatus::InvalidDescriptor &&
        manager.Release(OS_TEST_FILE_BACKING_OWNER_IDENTIFIER,
                        descriptor_index, generation) ==
            os::kernel::UserFileBackingStatus::InvalidDescriptor;
    test_context.Expect(ownership_valid,
                        OS_TEST_FILE_BACKING_OWNERSHIP);

    const bool drain_valid =
        manager.ActiveDescriptorCount() ==
            OS_TEST_FILE_BACKING_EMPTY_VALUE &&
        manager.Validate() ==
            os::kernel::UserFileBackingStatus::Succeeded;
    test_context.Expect(drain_valid, OS_TEST_FILE_BACKING_DRAIN);
    return test_context.ExitCode();
}
