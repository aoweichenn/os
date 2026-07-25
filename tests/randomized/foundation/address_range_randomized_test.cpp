#include "os/foundation/address_range.hpp"
#include "test_context.hpp"

#include <random>
#include <string_view>

namespace {

constexpr std::string_view OS_TEST_RANDOMIZED_SUITE_NAME = "foundation/address_range/randomized";
constexpr std::string_view OS_TEST_RANDOMIZED_VALID_RANGE_CREATION = "合法随机区间应创建成功";
constexpr std::string_view OS_TEST_RANDOMIZED_SIZE_PRESERVATION = "随机区间应保留原始长度";
constexpr std::string_view OS_TEST_RANDOMIZED_BEGIN_CONTAINMENT = "非空随机区间应包含起始地址";
constexpr std::string_view OS_TEST_RANDOMIZED_LAST_BYTE_CONTAINMENT =
    "非空随机区间应包含最后一个字节";
constexpr std::string_view OS_TEST_RANDOMIZED_END_EXCLUSION = "随机半开区间不应包含结束地址";
constexpr std::string_view OS_TEST_RANDOMIZED_SELF_OVERLAP = "非空随机区间应与自身重叠";
constexpr std::string_view OS_TEST_RANDOMIZED_ADJACENT_CREATION = "相邻随机区间应创建成功";
constexpr std::string_view OS_TEST_RANDOMIZED_ADJACENT_SEPARATION = "首尾相邻的随机区间不应重叠";
constexpr std::string_view OS_TEST_RANDOMIZED_OVERFLOW_REJECTION = "随机溢出区间应被拒绝";
constexpr std::string_view OS_TEST_RANDOMIZED_OVERFLOW_PRESERVES_OUTPUT =
    "随机区间创建失败时不得修改输出对象";

constexpr os::test::RandomSeed OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED =
    os::test::RandomSeed{0x9E3779B97F4A7C15};
constexpr os::test::TestCount OS_TEST_RANDOMIZED_ADDRESS_RANGE_CASE_COUNT =
    os::test::TestCount{10000};

}

int main() {
    os::test::TestContext testContext{OS_TEST_RANDOMIZED_SUITE_NAME};
    std::mt19937_64 randomEngine{OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED};

    for (os::test::TestCount iteration{}; iteration < OS_TEST_RANDOMIZED_ADDRESS_RANGE_CASE_COUNT;
         ++iteration) {
        const os::foundation::AddressValue beginValue =
            randomEngine() | os::foundation::OS_FOUNDATION_ADDRESS_UNIT;
        const os::foundation::AddressValue maximumSize =
            os::foundation::OS_FOUNDATION_ADDRESS_MAXIMUM - beginValue;
        const os::foundation::AddressValue sizeValue =
            randomEngine() % (maximumSize + os::foundation::OS_FOUNDATION_ADDRESS_UNIT);

        const os::foundation::PhysicalAddress begin{beginValue};
        const os::foundation::ByteCount size{sizeValue};
        os::foundation::AddressRange range{};
        const os::foundation::AddressRangeCreationStatus creationStatus =
            os::foundation::AddressRange::TryCreate(begin, size, range);

        testContext.ExpectRandom(creationStatus ==
                                     os::foundation::AddressRangeCreationStatus::Succeeded,
                                 OS_TEST_RANDOMIZED_VALID_RANGE_CREATION,
                                 OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
        testContext.ExpectRandom(range.Size().Equals(size), OS_TEST_RANDOMIZED_SIZE_PRESERVATION,
                                 OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
        testContext.ExpectRandom(!range.Contains(range.End()), OS_TEST_RANDOMIZED_END_EXCLUSION,
                                 OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);

        if (sizeValue != os::foundation::OS_FOUNDATION_ADDRESS_ZERO) {
            const os::foundation::PhysicalAddress lastAddress{
                range.End().Value() - os::foundation::OS_FOUNDATION_ADDRESS_UNIT};
            testContext.ExpectRandom(range.Contains(begin), OS_TEST_RANDOMIZED_BEGIN_CONTAINMENT,
                                     OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
            testContext.ExpectRandom(range.Contains(lastAddress),
                                     OS_TEST_RANDOMIZED_LAST_BYTE_CONTAINMENT,
                                     OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
            testContext.ExpectRandom(range.Overlaps(range), OS_TEST_RANDOMIZED_SELF_OVERLAP,
                                     OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
        }

        if (range.End().Value() < os::foundation::OS_FOUNDATION_ADDRESS_MAXIMUM) {
            os::foundation::AddressRange adjacentRange{};
            const os::foundation::AddressRangeCreationStatus adjacentStatus =
                os::foundation::AddressRange::TryCreate(
                    range.End(),
                    os::foundation::ByteCount{os::foundation::OS_FOUNDATION_ADDRESS_UNIT},
                    adjacentRange);
            testContext.ExpectRandom(adjacentStatus ==
                                         os::foundation::AddressRangeCreationStatus::Succeeded,
                                     OS_TEST_RANDOMIZED_ADJACENT_CREATION,
                                     OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
            testContext.ExpectRandom(!range.Overlaps(adjacentRange),
                                     OS_TEST_RANDOMIZED_ADJACENT_SEPARATION,
                                     OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
        }

        os::foundation::AddressRange overflowRange = range;
        const os::foundation::AddressRangeCreationStatus overflowStatus =
            os::foundation::AddressRange::TryCreate(
                begin,
                os::foundation::ByteCount{maximumSize + os::foundation::OS_FOUNDATION_ADDRESS_UNIT},
                overflowRange);
        testContext.ExpectRandom(overflowStatus ==
                                     os::foundation::AddressRangeCreationStatus::AddressOverflow,
                                 OS_TEST_RANDOMIZED_OVERFLOW_REJECTION,
                                 OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
        testContext.ExpectRandom(overflowRange.Begin().Equals(range.Begin()) &&
                                     overflowRange.Size().Equals(range.Size()),
                                 OS_TEST_RANDOMIZED_OVERFLOW_PRESERVES_OUTPUT,
                                 OS_TEST_RANDOMIZED_ADDRESS_RANGE_SEED, iteration);
    }

    return testContext.ExitCode();
}
