#pragma once

#include <cstdint>

#include "hal_battery.hpp"

struct status_bar_state {
    std::uint8_t hour;
    std::uint8_t minute;
    bool time_valid;
    battery_snapshot battery;
};

// Updates the organized local-time model without reading RTC hardware.
bool status_bar_update_time(std::uint8_t hour, std::uint8_t minute, bool valid);

// Updates the organized battery model without reading power-management hardware.
bool status_bar_update_battery(const battery_snapshot& snapshot);

// Returns one coherent copy for the display worker.
status_bar_state status_bar_get_state();
