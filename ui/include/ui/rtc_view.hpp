#pragma once

#include <cstdint>

enum class rtc_edit_field : std::uint8_t {
    none,
    year,
    month,
    day,
    hour,
    minute,
    second,
};

enum class rtc_setting_message : std::uint8_t {
    none,
    select_field,
    incomplete,
    invalid_date,
    invalid_time,
    rtc_unavailable,
    saving,
    write_failed,
};

struct rtc_view_state {
    std::uint8_t digits[14];
    rtc_edit_field selected_field;
    rtc_setting_message message;
    bool loading;
    bool saving;
    bool rtc_available;
};
