#include "os/kernel/user/user_program_images.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_IMAGE_TRUNCATED_ELF_SIZE_BYTES = 63ULL;

extern "C" const uint8_t os_kernel_user_smoke_elf_start[];
extern "C" const uint8_t os_kernel_user_smoke_elf_end[];
extern "C" const uint8_t os_kernel_user_invalid_opcode_elf_start[];
extern "C" const uint8_t os_kernel_user_invalid_opcode_elf_end[];
extern "C" const uint8_t os_kernel_user_page_fault_elf_start[];
extern "C" const uint8_t os_kernel_user_page_fault_elf_end[];
extern "C" const uint8_t os_kernel_user_scheduler_worker_elf_start[];
extern "C" const uint8_t os_kernel_user_scheduler_worker_elf_end[];
extern "C" const uint8_t os_kernel_user_ipc_producer_elf_start[];
extern "C" const uint8_t os_kernel_user_ipc_producer_elf_end[];
extern "C" const uint8_t os_kernel_user_ipc_consumer_elf_start[];
extern "C" const uint8_t os_kernel_user_ipc_consumer_elf_end[];
extern "C" const uint8_t os_kernel_user_shell_elf_start[];
extern "C" const uint8_t os_kernel_user_shell_elf_end[];

[[nodiscard]] uint64_t CalculateImageSize(const uint8_t *start, const uint8_t *end) noexcept {
    return static_cast<uint64_t>(end - start);
}

}

UserProgramImage SelectUserProgramImage(const UserProgramSelection selection) noexcept {
    if (selection == UserProgramSelection::Shell) {
        return UserProgramImage{
            .image = os_kernel_user_shell_elf_start,
            .image_size_bytes =
                CalculateImageSize(os_kernel_user_shell_elf_start, os_kernel_user_shell_elf_end),
        };
    }
    if (selection == UserProgramSelection::InvalidOpcode) {
        return UserProgramImage{
            .image = os_kernel_user_invalid_opcode_elf_start,
            .image_size_bytes = CalculateImageSize(os_kernel_user_invalid_opcode_elf_start,
                                                   os_kernel_user_invalid_opcode_elf_end),
        };
    }
    if (selection == UserProgramSelection::PageFault) {
        return UserProgramImage{
            .image = os_kernel_user_page_fault_elf_start,
            .image_size_bytes = CalculateImageSize(os_kernel_user_page_fault_elf_start,
                                                   os_kernel_user_page_fault_elf_end),
        };
    }
    if (selection == UserProgramSelection::SchedulerWorker) {
        return UserProgramImage{
            .image = os_kernel_user_scheduler_worker_elf_start,
            .image_size_bytes = CalculateImageSize(os_kernel_user_scheduler_worker_elf_start,
                                                   os_kernel_user_scheduler_worker_elf_end),
        };
    }
    if (selection == UserProgramSelection::IpcProducer) {
        return UserProgramImage{
            .image = os_kernel_user_ipc_producer_elf_start,
            .image_size_bytes = CalculateImageSize(os_kernel_user_ipc_producer_elf_start,
                                                   os_kernel_user_ipc_producer_elf_end),
        };
    }
    if (selection == UserProgramSelection::IpcConsumer) {
        return UserProgramImage{
            .image = os_kernel_user_ipc_consumer_elf_start,
            .image_size_bytes = CalculateImageSize(os_kernel_user_ipc_consumer_elf_start,
                                                   os_kernel_user_ipc_consumer_elf_end),
        };
    }
    const uint64_t smoke_image_size_bytes =
        CalculateImageSize(os_kernel_user_smoke_elf_start, os_kernel_user_smoke_elf_end);
    if (selection == UserProgramSelection::TruncatedSmoke) {
        return UserProgramImage{
            .image = os_kernel_user_smoke_elf_start,
            .image_size_bytes = OS_KERNEL_USER_IMAGE_TRUNCATED_ELF_SIZE_BYTES,
        };
    }
    return UserProgramImage{
        .image = os_kernel_user_smoke_elf_start,
        .image_size_bytes = smoke_image_size_bytes,
    };
}

}
