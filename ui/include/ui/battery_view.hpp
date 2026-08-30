#pragma once

#include <cstdint>

struct battery_view_state {
    std::uint8_t percent;
    std::uint16_t voltage_mv;
    std::int32_t current_ma;
    bool charging;
    bool level_valid;
    bool voltage_valid;
    bool current_valid;
    bool charging_valid;
    bool loading;
};
