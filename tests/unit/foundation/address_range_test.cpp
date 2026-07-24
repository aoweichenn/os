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

auto main() -> int {
    os::test::TestContext testContext{OS_TEST_UNIT_SUITE_NAME};

    const os::foundation::PhysicalAddress smallAddress{OS_TEST_UNIT_SMALL_ADDRESS};
    const os::foundation::PhysicalAddress sameSmallAddress{OS_TEST_UNIT_SMALL_ADDRESS};
    const os::foundation::PhysicalAddress largeAddress{OS_TEST_UNIT_LARGE_ADDRESS};
    const os::foundation::ByteCount rangeSize{OS_TEST_UNIT_RANGE_SIZE};

    testContext.expect(smallAddress.value() == OS_TEST_UNIT_SMALL_ADDRESS,
                       OS_TEST_UNIT_PHYSICAL_ADDRESS_VALUE);
    testContext.expect(smallAddress.equals(sameSmallAddress),
                       OS_TEST_UNIT_PHYSICAL_ADDRESS_EQUALITY);
    testContext.expect(smallAddress.isBefore(largeAddress), OS_TEST_UNIT_PHYSICAL_ADDRESS_ORDER);
    testContext.expect(rangeSize.value() == OS_TEST_UNIT_RANGE_SIZE, OS_TEST_UNIT_BYTE_COUNT_VALUE);

    os::foundation::AddressRange emptyRange{};
    const os::foundation::AddressRangeCreationStatus emptyStatus =
        os::foundation::AddressRange::tryCreate(
            smallAddress, os::foundation::ByteCount{os::foundation::OS_FOUNDATION_ADDRESS_ZERO},
            emptyRange);
    testContext.expect(emptyStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_UNIT_EMPTY_RANGE_CREATION);
    testContext.expect(emptyRange.isEmpty(), OS_TEST_UNIT_EMPTY_RANGE_STATE);
    testContext.expect(!emptyRange.contains(smallAddress), OS_TEST_UNIT_EMPTY_RANGE_CONTAINMENT);

    os::foundation::AddressRange regularRange{};
    const os::foundation::AddressRangeCreationStatus regularStatus =
        os::foundation::AddressRange::tryCreate(smallAddress, rangeSize, regularRange);
    testContext.expect(regularStatus == os::foundation::AddressRangeCreationStatus::Succeeded,
                       OS_TEST_UNIT_REGULAR_RANGE_CREATION);
    testContext.expect(regularRange.begin().equals(smallAddress), OS_TEST_UNIT_REGULAR_RANGE_BEGIN);
    testContext.expect(regularRange.size().equals(rangeSize), OS_TEST_UNIT_REGULAR_RANGE_SIZE);
    testContext.expect(regularRange.contains(smallAddress),
                       OS_TEST_UNIT_REGULAR_RANGE_BEGIN_CONTAINMENT);
    testContext.expect(!regularRange.contains(regularRange.end()),
                       OS_TEST_UNIT_REGULAR_RANGE_END_EXCLUSION);

    os::foundation::AddressRange overflowRange = regularRange;
    const os::foundation::AddressRangeCreationStatus overflowStatus =
        os::foundation::AddressRange::tryCreate(
            os::foundation::PhysicalAddress{os::foundation::OS_FOUNDATION_ADDRESS_MAXIMUM},
            os::foundation::ByteCount{os::foundation::OS_FOUNDATION_ADDRESS_UNIT}, overflowRange);
    testContext.expect(overflowStatus ==
                           os::foundation::AddressRangeCreationStatus::AddressOverflow,
                       OS_TEST_UNIT_OVERFLOW_REJECTION);
    testContext.expect(overflowRange.begin().equals(regularRange.begin()) &&
                           overflowRange.size().equals(regularRange.size()),
                       OS_TEST_UNIT_OVERFLOW_PRESERVES_OUTPUT);

    return testContext.exitCode();
}
