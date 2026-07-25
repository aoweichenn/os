#pragma once

#include <stdint.h>

namespace os::kernel {

enum class UserProgramSelection : uint64_t {
    Shell,
    Smoke,
    InvalidOpcode,
    PageFault,
    SchedulerWorker,
    IpcProducer,
    IpcConsumer,
    TruncatedSmoke,
};

struct UserProgramImage final {
    const uint8_t *image;
    uint64_t imageSizeBytes;
};

[[nodiscard]] UserProgramImage SelectUserProgramImage(UserProgramSelection selection) noexcept;

}
