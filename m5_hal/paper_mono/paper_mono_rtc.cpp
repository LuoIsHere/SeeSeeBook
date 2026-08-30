#include "rtc.hpp"

#include <M5Unified.h>

#include "internal_i2c.hpp"

namespace {

bool is_leap_year(std::uint16_t year)
{
    return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month)
{
    constexpr std::uint8_t month_days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (month < 1U || month > 12U) {
        return 0U;
    }
    return month == 2U && is_leap_year(year) ? 29U : month_days[month - 1U];
}

bool datetime_is_valid(const rtc_datetime& datetime)
{
    return datetime.date.year >= 2000U && datetime.date.year <= 2099U &&
           datetime.date.month >= 1U && datetime.date.month <= 12U &&
           datetime.date.day >= 1U &&
           datetime.date.day <= days_in_month(datetime.date.year, datetime.date.month) &&
           datetime.time.hour <= 23U && datetime.time.minute <= 59U &&
           datetime.time.second <= 59U;
}

std::uint8_t calculate_weekday(const rtc_date& date)
{
    std::int32_t year = date.year;
    std::int32_t month = date.month;
    if (month < 3) {
        --year;
        month += 12;
    }
    const std::int32_t century = year / 100;
    return static_cast<std::uint8_t>(
        (year + year / 4 - century + century / 4 +
         (13 * month + 8) / 5 + date.day) % 7);
}

}  // namespace

bool hal_rtc_read(rtc_datetime& datetime)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_RTC_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        return false;
    }

    m5::rtc_datetime_t hardware_datetime;
    if (!M5.Rtc.isEnabled() || !M5.Rtc.getDateTime(&hardware_datetime)) {
        return false;
    }
    datetime.date.year = static_cast<std::uint16_t>(hardware_datetime.date.year);
    datetime.date.month = static_cast<std::uint8_t>(hardware_datetime.date.month);
    datetime.date.day = static_cast<std::uint8_t>(hardware_datetime.date.date);
    datetime.time.hour = static_cast<std::uint8_t>(hardware_datetime.time.hours);
    datetime.time.minute = static_cast<std::uint8_t>(hardware_datetime.time.minutes);
    datetime.time.second = static_cast<std::uint8_t>(hardware_datetime.time.seconds);
    return datetime_is_valid(datetime);
}

bool hal_rtc_write(const rtc_datetime& datetime)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_RTC_TIMEOUT_MS);
    if (!bus_guard.locked() || !M5.Rtc.isEnabled() || !datetime_is_valid(datetime)) {
        return false;
    }

    m5::rtc_date_t hardware_date(
        static_cast<std::int16_t>(datetime.date.year),
        static_cast<std::int8_t>(datetime.date.month),
        static_cast<std::int8_t>(datetime.date.day),
        static_cast<std::int8_t>(calculate_weekday(datetime.date)));
    m5::rtc_time_t hardware_time(
        static_cast<std::int8_t>(datetime.time.hour),
        static_cast<std::int8_t>(datetime.time.minute),
        static_cast<std::int8_t>(datetime.time.second));
    auto* rtc_instance = M5.Rtc.getRtcInstancePtr();
    return rtc_instance != nullptr &&
           rtc_instance->setDateTime(&hardware_date, &hardware_time);
}
