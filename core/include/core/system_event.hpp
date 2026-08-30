#pragma once

#include <cstdint>
#include <type_traits>

#include "service_event.hpp"
#include "ui_action.hpp"

enum class system_event_type : std::uint8_t {
    ui_action,
    rtc,
    battery,
    storage_status,
    storage_result,
};

struct system_event {
    system_event_type type;
    ui_action_event action;
    rtc_service_event rtc;
    battery_service_event battery;
    storage_status_event storage_status;
    storage_result_event storage_result;
};

static_assert(std::is_trivially_copyable_v<system_event>);
