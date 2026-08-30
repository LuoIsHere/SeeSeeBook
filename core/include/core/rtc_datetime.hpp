#pragma once

#include <cstdint>
#include <type_traits>

struct rtc_date {
    std::uint16_t year;
    std::uint8_t month;
    std::uint8_t day;
};

struct rtc_time {
    std::uint8_t hour;
    std::uint8_t minute;
    std::uint8_t second;
};

struct rtc_datetime {
    rtc_date date;
    rtc_time time;
};

static_assert(std::is_trivially_copyable_v<rtc_datetime>);
