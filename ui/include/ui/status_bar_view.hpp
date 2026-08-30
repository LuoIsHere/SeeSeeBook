#pragma once

#include <cstdint>

#include "battery_snapshot.hpp"

struct status_bar_view_state {
    std::uint8_t hour;
    std::uint8_t minute;
    bool time_valid;
    battery_snapshot battery;
};
