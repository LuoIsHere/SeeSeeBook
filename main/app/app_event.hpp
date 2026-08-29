#pragma once

#include <cstdint>

#include "hal_battery.hpp"
#include "hal_rtc.hpp"
#include "hal_touch.hpp"

enum class app_event_type : std::uint8_t {
    touch,
    rtc,
    battery,
};

struct app_event {
    app_event_type type;
    touch_event touch;
    rtc_event rtc;
    battery_event battery;
};
