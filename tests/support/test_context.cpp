#include "test_context.hpp"

#include <iostream>

namespace {

constexpr os::test::TestCount OS_TEST_SUPPORT_COUNT_INCREMENT = os::test::TestCount{1};
constexpr int OS_TEST_SUPPORT_EXIT_SUCCESS = 0;
constexpr int OS_TEST_SUPPORT_EXIT_FAILURE = 1;
constexpr std::string_view OS_TEST_SUPPORT_SUITE_PREFIX = "[TEST] ";
constexpr std::string_view OS_TEST_SUPPORT_FAILURE_PREFIX = "[FAIL] ";
constexpr std::string_view OS_TEST_SUPPORT_RESULT_PREFIX = "[RESULT] ";
constexpr std::string_view OS_TEST_SUPPORT_ASSERTION_LABEL = " assertions, ";
constexpr std::string_view OS_TEST_SUPPORT_FAILURE_LABEL = " failures";
constexpr std::string_view OS_TEST_SUPPORT_SEED_LABEL = " seed=";
constexpr std::string_view OS_TEST_SUPPORT_ITERATION_LABEL = " iteration=";
constexpr char OS_TEST_SUPPORT_LINE_END = '\n';

}

namespace os::test {

TestContext::TestContext(const std::string_view suiteName) noexcept
    : suiteName{suiteName}, assertionCount{}, failureCount{} {
    std::cout << OS_TEST_SUPPORT_SUITE_PREFIX << this->suiteName << OS_TEST_SUPPORT_LINE_END;
}

void TestContext::expect(const bool condition, const std::string_view description) noexcept {
    this->assertionCount += OS_TEST_SUPPORT_COUNT_INCREMENT;

    if (!condition) {
        this->failureCount += OS_TEST_SUPPORT_COUNT_INCREMENT;
        std::cerr << OS_TEST_SUPPORT_FAILURE_PREFIX << description << OS_TEST_SUPPORT_LINE_END;
    }
}

void TestContext::expectRandom(const bool condition, const std::string_view description,
                               const RandomSeed seed, const TestCount iteration) noexcept {
    this->assertionCount += OS_TEST_SUPPORT_COUNT_INCREMENT;

    if (!condition) {
        this->failureCount += OS_TEST_SUPPORT_COUNT_INCREMENT;
        std::cerr << OS_TEST_SUPPORT_FAILURE_PREFIX << description << OS_TEST_SUPPORT_SEED_LABEL
                  << seed << OS_TEST_SUPPORT_ITERATION_LABEL << iteration
                  << OS_TEST_SUPPORT_LINE_END;
    }
}

auto TestContext::exitCode() const noexcept -> int {
    std::cout << OS_TEST_SUPPORT_RESULT_PREFIX << this->assertionCount
              << OS_TEST_SUPPORT_ASSERTION_LABEL << this->failureCount
              << OS_TEST_SUPPORT_FAILURE_LABEL << OS_TEST_SUPPORT_LINE_END;

    if (this->failureCount == TestCount{}) {
        return OS_TEST_SUPPORT_EXIT_SUCCESS;
    }

    return OS_TEST_SUPPORT_EXIT_FAILURE;
}

}
