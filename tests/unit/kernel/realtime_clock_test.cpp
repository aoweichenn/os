#include "os/kernel/time/realtime_clock.hpp"
#include "test_context.hpp"

#include <string_view>

namespace {

constexpr std::string_view OS_TEST_REALTIME_SUITE_NAME = "kernel/realtime_clock/unit";
constexpr std::string_view OS_TEST_REALTIME_EPOCH =
    "Unix epoch 必须从 1970-01-01T00:00:00Z 精确起算";
constexpr std::string_view OS_TEST_REALTIME_LEAP = "公历闰年规则必须覆盖世纪例外与 400 年恢复";
constexpr std::string_view OS_TEST_REALTIME_MODERN = "现代 UTC 日历必须转换为确定的 Unix 秒";
constexpr std::string_view OS_TEST_REALTIME_INVALID = "非法月份、日期与时间必须失败且清空输出";

}

int main() {
    os::test::TestContext test_context{OS_TEST_REALTIME_SUITE_NAME};
    os::abi::RealtimeInformation information{};
    test_context.Expect(os::kernel::CalendarToRealtimeInformation(1970ULL, 1ULL, 1ULL, 0ULL, 0ULL,
                                                                  0ULL, information) ==
                                os::kernel::RealtimeClockStatus::Succeeded &&
                            information.unix_seconds == 0ULL && information.reserved == 0ULL,
                        OS_TEST_REALTIME_EPOCH);
    test_context.Expect(!os::kernel::IsLeapYear(1900ULL) && os::kernel::IsLeapYear(2000ULL) &&
                            os::kernel::IsLeapYear(2024ULL) && !os::kernel::IsLeapYear(2025ULL) &&
                            os::kernel::CalendarToRealtimeInformation(2000ULL, 2ULL, 29ULL, 0ULL,
                                                                      0ULL, 0ULL, information) ==
                                os::kernel::RealtimeClockStatus::Succeeded &&
                            information.unix_seconds == 951782400ULL,
                        OS_TEST_REALTIME_LEAP);
    test_context.Expect(os::kernel::CalendarToRealtimeInformation(2026ULL, 8ULL, 18ULL, 0ULL, 0ULL,
                                                                  0ULL, information) ==
                                os::kernel::RealtimeClockStatus::Succeeded &&
                            information.unix_seconds == 1787011200ULL,
                        OS_TEST_REALTIME_MODERN);
    information.year = 1ULL;
    test_context.Expect(os::kernel::CalendarToRealtimeInformation(2025ULL, 2ULL, 29ULL, 24ULL, 0ULL,
                                                                  0ULL, information) ==
                                os::kernel::RealtimeClockStatus::InvalidCalendar &&
                            information.year == 0ULL && information.unix_seconds == 0ULL,
                        OS_TEST_REALTIME_INVALID);
    return test_context.ExitCode();
}
