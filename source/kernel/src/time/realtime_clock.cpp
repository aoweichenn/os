#include "os/kernel/time/realtime_clock.hpp"

namespace os::kernel {

namespace {

constexpr uint64_t OS_KERNEL_REALTIME_FIRST_YEAR = 1970ULL;
constexpr uint64_t OS_KERNEL_REALTIME_LAST_YEAR = 9999ULL;
constexpr uint64_t OS_KERNEL_REALTIME_MONTH_COUNT = 12ULL;
constexpr uint64_t OS_KERNEL_REALTIME_HOURS_PER_DAY = 24ULL;
constexpr uint64_t OS_KERNEL_REALTIME_MINUTES_PER_HOUR = 60ULL;
constexpr uint64_t OS_KERNEL_REALTIME_SECONDS_PER_MINUTE = 60ULL;
constexpr uint64_t OS_KERNEL_REALTIME_SECONDS_PER_DAY = OS_KERNEL_REALTIME_HOURS_PER_DAY *
                                                        OS_KERNEL_REALTIME_MINUTES_PER_HOUR *
                                                        OS_KERNEL_REALTIME_SECONDS_PER_MINUTE;
constexpr uint8_t OS_KERNEL_REALTIME_DAYS_PER_MONTH[OS_KERNEL_REALTIME_MONTH_COUNT]{
    31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
};

[[nodiscard]] uint64_t DaysInMonth(const uint64_t year, const uint64_t month) noexcept {
    if (month == 0ULL || month > OS_KERNEL_REALTIME_MONTH_COUNT) {
        return 0ULL;
    }
    return month == 2ULL && IsLeapYear(year) ? 29ULL
                                             : OS_KERNEL_REALTIME_DAYS_PER_MONTH[month - 1ULL];
}

}

bool IsLeapYear(const uint64_t year) noexcept {
    return year % 4ULL == 0ULL && (year % 100ULL != 0ULL || year % 400ULL == 0ULL);
}

RealtimeClockStatus
CalendarToRealtimeInformation(const uint64_t year, const uint64_t month, const uint64_t day,
                              const uint64_t hour, const uint64_t minute, const uint64_t second,
                              os::abi::RealtimeInformation &information) noexcept {
    information = os::abi::RealtimeInformation{};
    if (year < OS_KERNEL_REALTIME_FIRST_YEAR || year > OS_KERNEL_REALTIME_LAST_YEAR ||
        month == 0ULL || month > OS_KERNEL_REALTIME_MONTH_COUNT || day == 0ULL ||
        day > DaysInMonth(year, month) || hour >= OS_KERNEL_REALTIME_HOURS_PER_DAY ||
        minute >= OS_KERNEL_REALTIME_MINUTES_PER_HOUR ||
        second >= OS_KERNEL_REALTIME_SECONDS_PER_MINUTE) {
        return RealtimeClockStatus::InvalidCalendar;
    }
    uint64_t day_count = 0ULL;
    for (uint64_t current_year = OS_KERNEL_REALTIME_FIRST_YEAR; current_year < year;
         ++current_year) {
        day_count += IsLeapYear(current_year) ? 366ULL : 365ULL;
    }
    for (uint64_t current_month = 1ULL; current_month < month; ++current_month) {
        day_count += DaysInMonth(year, current_month);
    }
    day_count += day - 1ULL;
    const uint64_t unix_seconds =
        day_count * OS_KERNEL_REALTIME_SECONDS_PER_DAY +
        hour * OS_KERNEL_REALTIME_MINUTES_PER_HOUR * OS_KERNEL_REALTIME_SECONDS_PER_MINUTE +
        minute * OS_KERNEL_REALTIME_SECONDS_PER_MINUTE + second;
    information = os::abi::RealtimeInformation{
        .year = year,
        .month = month,
        .day = day,
        .hour = hour,
        .minute = minute,
        .second = second,
        .unix_seconds = unix_seconds,
        .reserved = 0ULL,
    };
    return RealtimeClockStatus::Succeeded;
}

}
