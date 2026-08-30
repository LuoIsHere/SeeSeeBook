#pragma once

#include <cstdint>
#include <type_traits>

struct battery_snapshot {
    std::uint8_t percent;
    std::uint16_t voltage_mv;
    std::int32_t current_ma;
    bool charging;
    bool level_valid;
    bool voltage_valid;
    bool current_valid;
    bool charging_valid;
    std::uint32_t timestamp_ms;
};

static_assert(std::is_trivially_copyable_v<battery_snapshot>);
