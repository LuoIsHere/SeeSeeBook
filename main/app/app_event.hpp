#pragma once

#include <cstdint>

#include "hal_battery.hpp"
#include "hal_rtc.hpp"
#include "hal_touch.hpp"
#include "hal_storage.hpp"
#include "storage_service.hpp"

enum class app_event_type : std::uint8_t {
    touch,
    rtc,
    battery,
    storage_status,
    storage_result,
};

struct app_event {
    app_event_type type;
    touch_event touch;
    rtc_event rtc;
    battery_event battery;
    sd_status_event storage_status;
    storage_event storage_result;
};
