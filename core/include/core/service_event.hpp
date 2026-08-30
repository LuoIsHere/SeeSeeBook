#pragma once

#include <cstdint>
#include <type_traits>

#include "battery_snapshot.hpp"
#include "result_handle.hpp"
#include "rtc_datetime.hpp"
#include "storage_state.hpp"

enum class rtc_service_operation : std::uint8_t {
    read,
    write,
};

struct rtc_service_event {
    rtc_service_operation operation;
    std::uint32_t request_id;
    rtc_datetime datetime;
    bool success;
};

struct battery_service_event {
    battery_snapshot snapshot;
};

struct storage_result_event {
    result_handle handle;
};

static_assert(std::is_trivially_copyable_v<rtc_service_event>);
static_assert(std::is_trivially_copyable_v<battery_service_event>);
static_assert(std::is_trivially_copyable_v<storage_result_event>);
