#pragma once

#include <cstdint>
#include <type_traits>

#include <esp_err.h>

#include "battery_snapshot.hpp"
#include "book_types.hpp"
#include "result_handle.hpp"
#include "rtc_datetime.hpp"
#include "storage_state.hpp"

// These raw Service events are consumed only by SystemEventDispatcher.
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

struct storage_status_event {
    storage_state state;
    std::uint32_t media_generation;
    esp_err_t error;
};

struct storage_result_event {
    result_handle handle;
};

struct book_result_event {
    result_handle handle;
};

static_assert(std::is_trivially_copyable_v<rtc_service_event>);
static_assert(std::is_trivially_copyable_v<battery_service_event>);
static_assert(std::is_trivially_copyable_v<storage_status_event>);
static_assert(std::is_trivially_copyable_v<storage_result_event>);
static_assert(std::is_trivially_copyable_v<book_result_event>);

bool rtc_service_try_get_event(rtc_service_event& event);
bool battery_service_try_get_event(battery_service_event& event);
bool storage_service_try_get_status_event(storage_status_event& event);
bool storage_service_try_get_result_event(storage_result_event& event);
bool book_service_try_get_event(book_service_event& event);
bool book_service_try_get_result_event(book_result_event& event);
