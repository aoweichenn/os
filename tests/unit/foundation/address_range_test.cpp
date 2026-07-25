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
    os::test::TestContext test_context{OS_TEST_UNIT_SUITE_NAME};

    const os::foundation::PhysicalAddress small_address{OS_TEST_UNIT_SMALL_ADDRESS};
    const os::foundation::PhysicalAddress same_small_address{OS_TEST_UNIT_SMALL_ADDRESS};
    const os::foundation::PhysicalAddress large_address{OS_TEST_UNIT_LARGE_ADDRESS};
    const os::foundation::ByteCount range_size{OS_TEST_UNIT_RANGE_SIZE};

    test_context.Expect(small_address.Value() == OS_TEST_UNIT_SMALL_ADDRESS,
                        OS_TEST_UNIT_PHYSICAL_ADDRESS_VALUE);
    test_context.Expect(small_address.Equals(same_small_address),
                        OS_TEST_UNIT_PHYSICAL_ADDRESS_EQUALITY);
    test_context.Expect(small_address.IsBefore(large_address), OS_TEST_UNIT_PHYSICAL_ADDRESS_ORDER);
    test_context.Expect(range_size.Value() == OS_TEST_UNIT_RANGE_SIZE,
                        OS_TEST_UNIT_BYTE_COUNT_VALUE);

    os::foundation::AddressRange empty_range{};
    const os::foundation::AddressRangeCreationStatus empty_status =
        os::foundation::AddressRange::TryCreate(
            small_address, os::foundation::ByteCount{os::foundation::OS_FOUNDATION_ADDRESS_ZERO},
            empty_range);
    test_context.Expect(empty_status == os::foundation::AddressRangeCreationStatus::Succeeded,
                        OS_TEST_UNIT_EMPTY_RANGE_CREATION);
    test_context.Expect(empty_range.IsEmpty(), OS_TEST_UNIT_EMPTY_RANGE_STATE);
    test_context.Expect(!empty_range.Contains(small_address), OS_TEST_UNIT_EMPTY_RANGE_CONTAINMENT);

    os::foundation::AddressRange regular_range{};
    const os::foundation::AddressRangeCreationStatus regular_status =
        os::foundation::AddressRange::TryCreate(small_address, range_size, regular_range);
    test_context.Expect(regular_status == os::foundation::AddressRangeCreationStatus::Succeeded,
                        OS_TEST_UNIT_REGULAR_RANGE_CREATION);
    test_context.Expect(regular_range.Begin().Equals(small_address),
                        OS_TEST_UNIT_REGULAR_RANGE_BEGIN);
    test_context.Expect(regular_range.Size().Equals(range_size), OS_TEST_UNIT_REGULAR_RANGE_SIZE);
    test_context.Expect(regular_range.Contains(small_address),
                        OS_TEST_UNIT_REGULAR_RANGE_BEGIN_CONTAINMENT);
    test_context.Expect(!regular_range.Contains(regular_range.End()),
                        OS_TEST_UNIT_REGULAR_RANGE_END_EXCLUSION);

    os::foundation::AddressRange overflow_range = regular_range;
    const os::foundation::AddressRangeCreationStatus overflow_status =
        os::foundation::AddressRange::TryCreate(
            os::foundation::PhysicalAddress{os::foundation::OS_FOUNDATION_ADDRESS_MAXIMUM},
            os::foundation::ByteCount{os::foundation::OS_FOUNDATION_ADDRESS_UNIT}, overflow_range);
    test_context.Expect(overflow_status ==
                            os::foundation::AddressRangeCreationStatus::AddressOverflow,
                        OS_TEST_UNIT_OVERFLOW_REJECTION);
    test_context.Expect(overflow_range.Begin().Equals(regular_range.Begin()) &&
                            overflow_range.Size().Equals(regular_range.Size()),
                        OS_TEST_UNIT_OVERFLOW_PRESERVES_OUTPUT);

    return test_context.ExitCode();
}
