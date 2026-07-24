#pragma once

#include <stdint.h>
#include <string_view>

namespace os::test {

using TestCount = uint64_t;
using RandomSeed = uint64_t;

class TestContext final {
  public:
    explicit TestContext(std::string_view suiteName) noexcept;

    void expect(bool condition, std::string_view description) noexcept;
    void expectRandom(bool condition, std::string_view description, RandomSeed seed,
                      TestCount iteration) noexcept;

    [[nodiscard]] auto exitCode() const noexcept -> int;

  private:
    std::string_view suiteName;
    TestCount assertionCount;
    TestCount failureCount;
};

}
