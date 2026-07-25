#include "os/kernel/io_descriptor.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_IO_DESCRIPTOR_SUITE_NAME = "kernel/io_descriptor/unit";
constexpr std::string_view OS_TEST_IO_DESCRIPTOR_STANDARD =
    "每个进程必须预装标准输入、标准输出和标准错误";
constexpr std::string_view OS_TEST_IO_DESCRIPTOR_PIPE =
    "管道端点必须占用首个动态描述符并与访问方向一致";
constexpr std::string_view OS_TEST_IO_DESCRIPTOR_ALLOCATION =
    "普通文件描述符必须跳过已占用槽并可关闭复用";
constexpr std::string_view OS_TEST_IO_DESCRIPTOR_CAPACITY = "描述符表容量耗尽必须返回稳定错误";
constexpr uint64_t OS_TEST_IO_DESCRIPTOR_FIRST_FILE =
    os::kernel::OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR;
constexpr uint64_t OS_TEST_IO_DESCRIPTOR_FILE_AFTER_PIPE =
    os::kernel::OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR + 1ULL;

}

int main() {
    os::test::TestContext test_context{OS_TEST_IO_DESCRIPTOR_SUITE_NAME};

    os::kernel::IoDescriptorTable descriptor_table{};
    descriptor_table.Initialize(false, false);
    os::kernel::IoDescriptorKind descriptor_kind = os::kernel::IoDescriptorKind::Closed;
    test_context.Expect(
        descriptor_table.Lookup(os::kernel::OS_KERNEL_IO_STANDARD_INPUT_DESCRIPTOR,
                                descriptor_kind) == os::kernel::IoDescriptorStatus::Succeeded &&
            descriptor_kind == os::kernel::IoDescriptorKind::ConsoleInput &&
            descriptor_table.Lookup(os::kernel::OS_KERNEL_IO_STANDARD_OUTPUT_DESCRIPTOR,
                                    descriptor_kind) == os::kernel::IoDescriptorStatus::Succeeded &&
            descriptor_kind == os::kernel::IoDescriptorKind::ConsoleOutput &&
            descriptor_table.Close(os::kernel::OS_KERNEL_IO_STANDARD_ERROR_DESCRIPTOR,
                                   descriptor_kind) ==
                os::kernel::IoDescriptorStatus::PermissionDenied,
        OS_TEST_IO_DESCRIPTOR_STANDARD);

    descriptor_table.Initialize(true, false);
    test_context.Expect(descriptor_table.Lookup(os::kernel::OS_KERNEL_IO_FIRST_DYNAMIC_DESCRIPTOR,
                                                descriptor_kind) ==
                                os::kernel::IoDescriptorStatus::Succeeded &&
                            descriptor_kind == os::kernel::IoDescriptorKind::PipeReader,
                        OS_TEST_IO_DESCRIPTOR_PIPE);

    uint64_t file_descriptor = os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    const bool allocated_after_pipe =
        descriptor_table.Allocate(os::kernel::IoDescriptorKind::RegularFile, file_descriptor) ==
            os::kernel::IoDescriptorStatus::Succeeded &&
        file_descriptor == OS_TEST_IO_DESCRIPTOR_FILE_AFTER_PIPE;
    os::kernel::IoDescriptorKind closed_kind = os::kernel::IoDescriptorKind::Closed;
    const bool closed = descriptor_table.Close(file_descriptor, closed_kind) ==
                            os::kernel::IoDescriptorStatus::Succeeded &&
                        closed_kind == os::kernel::IoDescriptorKind::RegularFile;
    uint64_t reused_descriptor = os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    test_context.Expect(
        allocated_after_pipe && closed &&
            descriptor_table.Allocate(os::kernel::IoDescriptorKind::Directory, reused_descriptor) ==
                os::kernel::IoDescriptorStatus::Succeeded &&
            reused_descriptor == OS_TEST_IO_DESCRIPTOR_FILE_AFTER_PIPE,
        OS_TEST_IO_DESCRIPTOR_ALLOCATION);

    descriptor_table.Initialize(false, false);
    bool filled = true;
    for (uint64_t descriptor = OS_TEST_IO_DESCRIPTOR_FIRST_FILE;
         descriptor < os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY; ++descriptor) {
        uint64_t allocated_descriptor = os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
        filled = filled &&
                 descriptor_table.Allocate(os::kernel::IoDescriptorKind::RegularFile,
                                           allocated_descriptor) ==
                     os::kernel::IoDescriptorStatus::Succeeded &&
                 allocated_descriptor == descriptor;
    }
    uint64_t unavailable_descriptor = os::kernel::OS_KERNEL_IO_DESCRIPTOR_CAPACITY;
    test_context.Expect(filled &&
                            descriptor_table.Allocate(os::kernel::IoDescriptorKind::RegularFile,
                                                      unavailable_descriptor) ==
                                os::kernel::IoDescriptorStatus::CapacityExhausted,
                        OS_TEST_IO_DESCRIPTOR_CAPACITY);

    return test_context.ExitCode();
}
