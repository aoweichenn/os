#include "os/kernel/device/cmos_rtc.hpp"

#include "os/kernel/device/port_io.hpp"
#include "os/kernel/time/realtime_clock.hpp"

namespace os::kernel {

namespace {

constexpr uint16_t OS_KERNEL_CMOS_INDEX_PORT = 0x0070U;
constexpr uint16_t OS_KERNEL_CMOS_DATA_PORT = 0x0071U;
constexpr uint8_t OS_KERNEL_CMOS_NMI_DISABLED = 0x80U;
constexpr uint8_t OS_KERNEL_CMOS_SECONDS_REGISTER = 0x00U;
constexpr uint8_t OS_KERNEL_CMOS_MINUTES_REGISTER = 0x02U;
constexpr uint8_t OS_KERNEL_CMOS_HOURS_REGISTER = 0x04U;
constexpr uint8_t OS_KERNEL_CMOS_DAY_REGISTER = 0x07U;
constexpr uint8_t OS_KERNEL_CMOS_MONTH_REGISTER = 0x08U;
constexpr uint8_t OS_KERNEL_CMOS_YEAR_REGISTER = 0x09U;
constexpr uint8_t OS_KERNEL_CMOS_STATUS_A_REGISTER = 0x0AU;
constexpr uint8_t OS_KERNEL_CMOS_STATUS_B_REGISTER = 0x0BU;
constexpr uint8_t OS_KERNEL_CMOS_CENTURY_REGISTER = 0x32U;
constexpr uint8_t OS_KERNEL_CMOS_UPDATE_IN_PROGRESS = 0x80U;
constexpr uint8_t OS_KERNEL_CMOS_24_HOUR_MODE = 0x02U;
constexpr uint8_t OS_KERNEL_CMOS_BINARY_MODE = 0x04U;
constexpr uint8_t OS_KERNEL_CMOS_PM_FLAG = 0x80U;
constexpr uint64_t OS_KERNEL_CMOS_WAIT_LIMIT = 100000ULL;
constexpr uint64_t OS_KERNEL_CMOS_SNAPSHOT_ATTEMPTS = 4ULL;

struct CmosSnapshot final {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
    uint8_t status_b;
};

[[nodiscard]] uint8_t ReadRegister(const uint8_t register_index) noexcept {
    WritePort8(OS_KERNEL_CMOS_INDEX_PORT,
               static_cast<uint8_t>(OS_KERNEL_CMOS_NMI_DISABLED | register_index));
    return ReadPort8(OS_KERNEL_CMOS_DATA_PORT);
}

void EnableNonMaskableInterrupts() noexcept { WritePort8(OS_KERNEL_CMOS_INDEX_PORT, 0U); }

[[nodiscard]] bool WaitForStableWindow() noexcept {
    for (uint64_t attempt = 0ULL; attempt < OS_KERNEL_CMOS_WAIT_LIMIT; ++attempt) {
        if ((ReadRegister(OS_KERNEL_CMOS_STATUS_A_REGISTER) & OS_KERNEL_CMOS_UPDATE_IN_PROGRESS) ==
            0U) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] CmosSnapshot ReadSnapshot() noexcept {
    return CmosSnapshot{
        .second = ReadRegister(OS_KERNEL_CMOS_SECONDS_REGISTER),
        .minute = ReadRegister(OS_KERNEL_CMOS_MINUTES_REGISTER),
        .hour = ReadRegister(OS_KERNEL_CMOS_HOURS_REGISTER),
        .day = ReadRegister(OS_KERNEL_CMOS_DAY_REGISTER),
        .month = ReadRegister(OS_KERNEL_CMOS_MONTH_REGISTER),
        .year = ReadRegister(OS_KERNEL_CMOS_YEAR_REGISTER),
        .century = ReadRegister(OS_KERNEL_CMOS_CENTURY_REGISTER),
        .status_b = ReadRegister(OS_KERNEL_CMOS_STATUS_B_REGISTER),
    };
}

[[nodiscard]] bool SnapshotsEqual(const CmosSnapshot &first, const CmosSnapshot &second) noexcept {
    return first.second == second.second && first.minute == second.minute &&
           first.hour == second.hour && first.day == second.day && first.month == second.month &&
           first.year == second.year && first.century == second.century &&
           first.status_b == second.status_b;
}

[[nodiscard]] uint64_t DecodeBcd(const uint8_t value) noexcept {
    return static_cast<uint64_t>(value & 0x0FU) +
           static_cast<uint64_t>((value >> 4U) & 0x0FU) * 10ULL;
}

}

CmosRtcStatus ReadCmosRtc(os::abi::RealtimeInformation &information) noexcept {
    information = os::abi::RealtimeInformation{};
    CmosSnapshot stable_snapshot{};
    bool snapshot_ready = false;
    for (uint64_t attempt = 0ULL; attempt < OS_KERNEL_CMOS_SNAPSHOT_ATTEMPTS; ++attempt) {
        if (!WaitForStableWindow()) {
            EnableNonMaskableInterrupts();
            return CmosRtcStatus::TimedOut;
        }
        const CmosSnapshot first = ReadSnapshot();
        if (!WaitForStableWindow()) {
            EnableNonMaskableInterrupts();
            return CmosRtcStatus::TimedOut;
        }
        const CmosSnapshot second = ReadSnapshot();
        if (SnapshotsEqual(first, second)) {
            stable_snapshot = second;
            snapshot_ready = true;
            break;
        }
    }
    EnableNonMaskableInterrupts();
    if (!snapshot_ready) {
        return CmosRtcStatus::Unstable;
    }

    const bool binary_mode = (stable_snapshot.status_b & OS_KERNEL_CMOS_BINARY_MODE) != 0U;
    const auto decode = [binary_mode](const uint8_t value) {
        return binary_mode ? static_cast<uint64_t>(value) : DecodeBcd(value);
    };
    const bool pm = (stable_snapshot.hour & OS_KERNEL_CMOS_PM_FLAG) != 0U;
    uint64_t hour = decode(static_cast<uint8_t>(stable_snapshot.hour & ~OS_KERNEL_CMOS_PM_FLAG));
    if ((stable_snapshot.status_b & OS_KERNEL_CMOS_24_HOUR_MODE) == 0U) {
        hour = hour % 12ULL + (pm ? 12ULL : 0ULL);
    }
    uint64_t century = decode(stable_snapshot.century);
    if (century == 0ULL) {
        century = 20ULL;
    }
    const RealtimeClockStatus status = CalendarToRealtimeInformation(
        century * 100ULL + decode(stable_snapshot.year), decode(stable_snapshot.month),
        decode(stable_snapshot.day), hour, decode(stable_snapshot.minute),
        decode(stable_snapshot.second), information);
    return status == RealtimeClockStatus::Succeeded ? CmosRtcStatus::Succeeded
                                                    : CmosRtcStatus::InvalidCalendar;
}

}
