#pragma once

#include <cstdint>

struct battery_telemetry_sample {
    std::uint8_t percent;
    std::uint16_t voltage_mv;
    std::int32_t current_ma;
    bool level_valid;
    bool voltage_valid;
    bool current_valid;
};

// Reads the M5PM1 battery telemetry once.
bool hal_battery_read_telemetry(battery_telemetry_sample& sample);

// Reads whether external VBUS is currently present.
bool hal_battery_read_vbus(bool& present);

// Runs one bounded IP2315 session and detaches it before returning.
bool hal_battery_read_charging(bool& charging);
