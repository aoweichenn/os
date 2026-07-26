#pragma once

#include <stdint.h>

namespace os::user {

[[nodiscard]] bool
InitializeExtendedStateIsolationTest(uint64_t process_id) noexcept;
[[nodiscard]] bool ValidateExtendedStateIsolationTest(uint64_t process_id) noexcept;
[[nodiscard]] bool
CompleteExtendedStateIsolationTest(uint64_t process_id) noexcept;

}
