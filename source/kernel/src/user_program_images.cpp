#include "os/kernel/user_program_images.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_USER_IMAGE_TRUNCATED_ELF_SIZE_BYTES = 63ULL;

extern "C" const uint8_t osKernelUserSmokeElfStart[];
extern "C" const uint8_t osKernelUserSmokeElfEnd[];
extern "C" const uint8_t osKernelUserInvalidOpcodeElfStart[];
extern "C" const uint8_t osKernelUserInvalidOpcodeElfEnd[];
extern "C" const uint8_t osKernelUserPageFaultElfStart[];
extern "C" const uint8_t osKernelUserPageFaultElfEnd[];
extern "C" const uint8_t osKernelUserSchedulerWorkerElfStart[];
extern "C" const uint8_t osKernelUserSchedulerWorkerElfEnd[];
extern "C" const uint8_t osKernelUserIpcProducerElfStart[];
extern "C" const uint8_t osKernelUserIpcProducerElfEnd[];
extern "C" const uint8_t osKernelUserIpcConsumerElfStart[];
extern "C" const uint8_t osKernelUserIpcConsumerElfEnd[];

[[nodiscard]] uint64_t CalculateImageSize(const uint8_t *start, const uint8_t *end) noexcept {
    return static_cast<uint64_t>(end - start);
}

}

UserProgramImage SelectUserProgramImage(const UserProgramSelection selection) noexcept {
    if (selection == UserProgramSelection::InvalidOpcode) {
        return UserProgramImage{
            .image = osKernelUserInvalidOpcodeElfStart,
            .imageSizeBytes = CalculateImageSize(osKernelUserInvalidOpcodeElfStart,
                                                 osKernelUserInvalidOpcodeElfEnd),
        };
    }
    if (selection == UserProgramSelection::PageFault) {
        return UserProgramImage{
            .image = osKernelUserPageFaultElfStart,
            .imageSizeBytes =
                CalculateImageSize(osKernelUserPageFaultElfStart, osKernelUserPageFaultElfEnd),
        };
    }
    if (selection == UserProgramSelection::SchedulerWorker) {
        return UserProgramImage{
            .image = osKernelUserSchedulerWorkerElfStart,
            .imageSizeBytes = CalculateImageSize(osKernelUserSchedulerWorkerElfStart,
                                                 osKernelUserSchedulerWorkerElfEnd),
        };
    }
    if (selection == UserProgramSelection::IpcProducer) {
        return UserProgramImage{
            .image = osKernelUserIpcProducerElfStart,
            .imageSizeBytes =
                CalculateImageSize(osKernelUserIpcProducerElfStart, osKernelUserIpcProducerElfEnd),
        };
    }
    if (selection == UserProgramSelection::IpcConsumer) {
        return UserProgramImage{
            .image = osKernelUserIpcConsumerElfStart,
            .imageSizeBytes =
                CalculateImageSize(osKernelUserIpcConsumerElfStart, osKernelUserIpcConsumerElfEnd),
        };
    }
    const uint64_t smokeImageSizeBytes =
        CalculateImageSize(osKernelUserSmokeElfStart, osKernelUserSmokeElfEnd);
    if (selection == UserProgramSelection::TruncatedSmoke) {
        return UserProgramImage{
            .image = osKernelUserSmokeElfStart,
            .imageSizeBytes = OS_KERNEL_USER_IMAGE_TRUNCATED_ELF_SIZE_BYTES,
        };
    }
    return UserProgramImage{
        .image = osKernelUserSmokeElfStart,
        .imageSizeBytes = smokeImageSizeBytes,
    };
}

}
