#pragma once

#include <stdint.h>
#include <string_view>

namespace os::test {

using TestCount = uint64_t;
using RandomSeed = uint64_t;

class TestContext final {
  public:
    explicit TestContext(std::string_view suite_name) noexcept;

    void Expect(bool condition, std::string_view description) noexcept;
    void ExpectRandom(bool condition, std::string_view description, RandomSeed seed,
                      TestCount iteration) noexcept;

    [[nodiscard]] int ExitCode() const noexcept;

  private:
    std::string_view suite_name_;
    TestCount assertion_count_;
    TestCount failure_count_;
};

}
