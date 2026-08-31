#pragma once

#include <cstdint>
#include <type_traits>

#include "battery_snapshot.hpp"
#include "result_handle.hpp"
#include "rtc_datetime.hpp"
#include "storage_state.hpp"
#include "ui_action.hpp"

enum class app_event_type : std::uint8_t {
    ui_action,
    rtc,
    battery,
    storage_status,
    storage_result,
};

enum class app_rtc_operation : std::uint8_t {
    read,
    write,
};

struct app_rtc_event {
    app_rtc_operation operation;
    std::uint32_t request_id;
    rtc_datetime datetime;
    bool success;
};

struct app_battery_event {
    battery_snapshot snapshot;
};

struct app_storage_status_event {
    storage_state state;
    std::uint32_t media_generation;
    std::int32_t error_code;
};

struct app_storage_result_event {
    result_handle handle;
};

struct app_event {
    app_event_type type;
    ui_action_event action;
    app_rtc_event rtc;
    app_battery_event battery;
    app_storage_status_event storage_status;
    app_storage_result_event storage_result;
};

static_assert(std::is_trivially_copyable_v<app_rtc_event>);
static_assert(std::is_trivially_copyable_v<app_battery_event>);
static_assert(std::is_trivially_copyable_v<app_storage_status_event>);
static_assert(std::is_trivially_copyable_v<app_storage_result_event>);
static_assert(std::is_trivially_copyable_v<app_event>);
