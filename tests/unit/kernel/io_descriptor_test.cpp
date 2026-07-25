#include "os/kernel/io_descriptor.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_IO_DESCRIPTOR_SUITE_NAME =
    "kernel/io_descriptor/unit";
constexpr std::string_view OS_TEST_IO_DESCRIPTOR_STANDARD =
    "每个进程必须预装标准输入、标准输出和标准错误";
constexpr std::string_view OS_TEST_IO_DESCRIPTOR_PIPE =
    "管道端点必须占用首个动态描述符并与访问方向一致";
constexpr std::string_view OS_TEST_IO_DESCRIPTOR_ALLOCATION =
    "普通文件描述符必须跳过已占用槽并可关闭复用";
constexpr std::string_view OS_TEST_IO_DESCRIPTOR_CAPACITY =
    "描述符表容量耗尽必须返回稳定错误";
constexpr uint64_t OS_TEST_IO_DESCRIPTOR_FIRST_FILE =
    os::kernel::OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR;
constexpr uint64_t OS_TEST_IO_DESCRIPTOR_FILE_AFTER_PIPE =
    os::kernel::OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR + 1ULL;

}

int main() {
    os::test::TestContext testContext{OS_TEST_IO_DESCRIPTOR_SUITE_NAME};

    os::kernel::IoDescriptorTable descriptorTable{};
    descriptorTable.Initialize(false, false);
    os::kernel::IoDescriptorKind descriptorKind =
        os::kernel::IoDescriptorKind::Closed;
    testContext.Expect(
        descriptorTable.Lookup(
            os::kernel::OS_KERNEL_IO_STANDARD_INPUT_DESCRIPTOR,
            descriptorKind) == os::kernel::IoDescriptorStatus::Succeeded &&
            descriptorKind == os::kernel::IoDescriptorKind::ConsoleInput &&
            descriptorTable.Lookup(
                os::kernel::OS_KERNEL_IO_STANDARD_OUTPUT_DESCRIPTOR,
                descriptorKind) ==
                os::kernel::IoDescriptorStatus::Succeeded &&
            descriptorKind == os::kernel::IoDescriptorKind::ConsoleOutput &&
            descriptorTable.Close(
                os::kernel::OS_KERNEL_IO_STANDARD_ERROR_DESCRIPTOR,
                descriptorKind) ==
                os::kernel::IoDescriptorStatus::PermissionDenied,
        OS_TEST_IO_DESCRIPTOR_STANDARD);

    descriptorTable.Initialize(true, false);
    testContext.Expect(
        descriptorTable.Lookup(
            os::kernel::OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR,
            descriptorKind) == os::kernel::IoDescriptorStatus::Succeeded &&
            descriptorKind == os::kernel::IoDescriptorKind::PipeReader,
        OS_TEST_IO_DESCRIPTOR_PIPE);

    uint64_t fileDescriptor = os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    const bool allocatedAfterPipe =
        descriptorTable.Allocate(os::kernel::IoDescriptorKind::RegularFile,
                                 fileDescriptor) ==
            os::kernel::IoDescriptorStatus::Succeeded &&
        fileDescriptor == OS_TEST_IO_DESCRIPTOR_FILE_AFTER_PIPE;
    os::kernel::IoDescriptorKind closedKind =
        os::kernel::IoDescriptorKind::Closed;
    const bool closed =
        descriptorTable.Close(fileDescriptor, closedKind) ==
            os::kernel::IoDescriptorStatus::Succeeded &&
        closedKind == os::kernel::IoDescriptorKind::RegularFile;
    uint64_t reusedDescriptor =
        os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    testContext.Expect(
        allocatedAfterPipe && closed &&
            descriptorTable.Allocate(
                os::kernel::IoDescriptorKind::Directory,
                reusedDescriptor) ==
                os::kernel::IoDescriptorStatus::Succeeded &&
            reusedDescriptor == OS_TEST_IO_DESCRIPTOR_FILE_AFTER_PIPE,
        OS_TEST_IO_DESCRIPTOR_ALLOCATION);

    descriptorTable.Initialize(false, false);
    bool filled = true;
    for (uint64_t descriptor = OS_TEST_IO_DESCRIPTOR_FIRST_FILE;
         descriptor < os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
         ++descriptor) {
        uint64_t allocatedDescriptor =
            os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
        filled =
            filled &&
            descriptorTable.Allocate(
                os::kernel::IoDescriptorKind::RegularFile,
                allocatedDescriptor) ==
                os::kernel::IoDescriptorStatus::Succeeded &&
            allocatedDescriptor == descriptor;
    }
    uint64_t unavailableDescriptor =
        os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    testContext.Expect(
        filled &&
            descriptorTable.Allocate(
                os::kernel::IoDescriptorKind::RegularFile,
                unavailableDescriptor) ==
                os::kernel::IoDescriptorStatus::CapacityExhausted,
        OS_TEST_IO_DESCRIPTOR_CAPACITY);

    return testContext.ExitCode();
}
