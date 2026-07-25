#include "os/foundation/address_range.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_UNIT_SUITE_NAME = "foundation/address_range/unit";
constexpr std::string_view OS_TEST_UNIT_PHYSICAL_ADDRESS_VALUE = "物理地址应保留构造值";
constexpr std::string_view OS_TEST_UNIT_PHYSICAL_ADDRESS_EQUALITY = "相同物理地址应相等";
constexpr std::string_view OS_TEST_UNIT_PHYSICAL_ADDRESS_ORDER = "较小物理地址应位于较大地址之前";
constexpr std::string_view OS_TEST_UNIT_BYTE_COUNT_VALUE = "字节数应保留构造值";
constexpr std::string_view OS_TEST_UNIT_EMPTY_RANGE_CREATION = "零长度地址区间应创建成功";
constexpr std::string_view OS_TEST_UNIT_EMPTY_RANGE_STATE = "零长度地址区间应为空";
constexpr std::string_view OS_TEST_UNIT_EMPTY_RANGE_CONTAINMENT = "零长度地址区间不应包含起始地址";
constexpr std::string_view OS_TEST_UNIT_REGULAR_RANGE_CREATION = "普通地址区间应创建成功";
constexpr std::string_view OS_TEST_UNIT_REGULAR_RANGE_BEGIN = "普通地址区间应保留起始地址";
constexpr std::string_view OS_TEST_UNIT_REGULAR_RANGE_SIZE = "普通地址区间应保留长度";
constexpr std::string_view OS_TEST_UNIT_REGULAR_RANGE_BEGIN_CONTAINMENT =
    "非空地址区间应包含起始地址";
constexpr std::string_view OS_TEST_UNIT_REGULAR_RANGE_END_EXCLUSION =
    "半开地址区间不应包含结束地址";
constexpr std::string_view OS_TEST_UNIT_OVERFLOW_REJECTION = "超过地址上限的区间应被拒绝";
constexpr std::string_view OS_TEST_UNIT_OVERFLOW_PRESERVES_OUTPUT =
    "区间创建失败时不得修改输出对象";

constexpr os::foundation::AddressValue OS_TEST_UNIT_SMALL_ADDRESS =
    os::foundation::AddressValue{0x1000};
constexpr os::foundation::AddressValue OS_TEST_UNIT_LARGE_ADDRESS =
    os::foundation::AddressValue{0x2000};
constexpr os::foundation::AddressValue OS_TEST_UNIT_RANGE_SIZE =
    os::foundation::AddressValue{0x100};

}

int main() {
    os::test::TestContext testContext{OS_TEST_UNIT_SUITE_NAME};

    const os::foundation::PhysicalAddress smallAddress{OS_TEST_UNIT_SMALL_ADDRESS};
    const os::foundation::PhysicalAddress sameSmallAddress{OS_TEST_UNIT_SMALL_ADDRESS};
    const os::foundation::PhysicalAddress largeAddress{OS_TEST_UNIT_LARGE_ADDRESS};
    const os::foundation::ByteCount rangeSize{OS_TEST_UNIT_RANGE_SIZE};

    testContext.Expect(smallAddress.Value() == OS_TEST_UNIT_SMALL_ADDRESS,
                       OS_TEST_UNIT_PHYSICAL_ADDRESS_VALUE);
    testContext.Expect(smallAddress.Equals(sameSmallAddress),
                       OS_TEST_UNIT_PHYSICAL_ADDRESS_EQUALITY);
    testContext.Expect(smallAddress.IsBefore(largeAddress), OS_TEST_UNIT_PHYSICAL_ADDRESS_ORDER);
    testContext.Expect(rangeSize.Value() == OS_TEST_UNIT_RANGE_SIZE, OS_TEST_UNIT_BYTE_COUNT_VALUE);

    os::foundation::AddressRange emptyRange{};
    const os::foundation::AddressRangeCreationStatus emptyStatus =
        os::foundation::AddressRange::TryCreate(
            smallAddress, os::foundation::ByteCount{os::foundation::OS_FOUNDATION_ADDRESS_ZERO},
            emptyRange);
    testContext.Expect(emptyStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_UNIT_EMPTY_RANGE_CREATION);
    testContext.Expect(emptyRange.IsEmpty(), OS_TEST_UNIT_EMPTY_RANGE_STATE);
    testContext.Expect(!emptyRange.Contains(smallAddress), OS_TEST_UNIT_EMPTY_RANGE_CONTAINMENT);

    os::foundation::AddressRange regularRange{};
    const os::foundation::AddressRangeCreationStatus regularStatus =
        os::foundation::AddressRange::TryCreate(smallAddress, rangeSize, regularRange);
    testContext.Expect(regularStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_UNIT_REGULAR_RANGE_CREATION);
    testContext.Expect(regularRange.Begin().Equals(smallAddress), OS_TEST_UNIT_REGULAR_RANGE_BEGIN);
    testContext.Expect(regularRange.Size().Equals(rangeSize), OS_TEST_UNIT_REGULAR_RANGE_SIZE);
    testContext.Expect(regularRange.Contains(smallAddress),
                       OS_TEST_UNIT_REGULAR_RANGE_BEGIN_CONTAINMENT);
    testContext.Expect(!regularRange.Contains(regularRange.End()),
                       OS_TEST_UNIT_REGULAR_RANGE_END_EXCLUSION);

    os::foundation::AddressRange overflowRange = regularRange;
    const os::foundation::AddressRangeCreationStatus overflowStatus =
        os::foundation::AddressRange::TryCreate(
            os::foundation::PhysicalAddress{os::foundation::OS_FOUNDATION_ADDRESS_MAXIMUM},
            os::foundation::ByteCount{os::foundation::OS_FOUNDATION_ADDRESS_UNIT}, overflowRange);
    testContext.Expect(overflowStatus ==
                           os::foundation::AddressRangeCreationStatus::AddressOverflow,
                       OS_TEST_UNIT_OVERFLOW_REJECTION);
    testContext.Expect(overflowRange.Begin().Equals(regularRange.Begin()) &&
                           overflowRange.Size().Equals(regularRange.Size()),
                       OS_TEST_UNIT_OVERFLOW_PRESERVES_OUTPUT);

    return testContext.ExitCode();
}
