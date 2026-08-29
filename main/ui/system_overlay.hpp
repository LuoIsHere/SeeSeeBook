#pragma once

#include <cstdint>

struct system_overlay_state {
    std::uint8_t hour;
    std::uint8_t minute;
    bool time_valid;
};

// Builds the common status-bar model from the HAL clock cache.
system_overlay_state system_overlay_get_state();
