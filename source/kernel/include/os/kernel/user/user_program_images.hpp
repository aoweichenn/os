#pragma once

#include <stdint.h>

namespace os::kernel {

enum class UserProgramSelection : uint64_t {
    Shell,
    Smoke,
    OomPressure,
    InvalidOpcode,
    PageFault,
    SchedulerWorker,
    IpcProducer,
    IpcConsumer,
    TruncatedSmoke,
    DiskExecutable,
};

struct UserProgramImage final {
    const uint8_t *image;
    uint64_t image_size_bytes;
};

[[nodiscard]] UserProgramImage SelectUserProgramImage(UserProgramSelection selection) noexcept;

}
