#pragma once

#include <cstdint>
#include <type_traits>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define RTC_REQUEST_QUEUE_LENGTH 4U
#define RTC_EVENT_QUEUE_LENGTH 1U
#define RTC_TASK_STACK_SIZE 4096U
#define RTC_TASK_PRIORITY 4U

struct rtc_date {
    std::uint16_t year;
    std::uint8_t month;
    std::uint8_t day;
};

struct rtc_time {
    std::uint8_t hour;
    std::uint8_t minute;
    std::uint8_t second;
};

struct rtc_datetime {
    rtc_date date;
    rtc_time time;
};

enum class rtc_operation : std::uint8_t {
    read,
    write,
};

struct rtc_event {
    rtc_operation operation;
    std::uint32_t request_id;
    rtc_datetime datetime;
    bool success;
};

static_assert(std::is_trivially_copyable_v<rtc_datetime>);
static_assert(std::is_trivially_copyable_v<rtc_event>);

// Starts the RTC worker and schedules the initial global clock cache read.
bool hal_rtc_start(TaskHandle_t& task_handle);

// Submits one RTC read without blocking the Mooncake scheduler.
bool hal_submit_rtc_read(std::uint32_t request_id);

// Submits one complete local date/time write without blocking the caller.
bool hal_submit_rtc_write(
    const rtc_datetime& datetime,
    std::uint32_t request_id);

// Reads the latest RTC completion event without blocking the caller.
bool hal_try_get_rtc_event(rtc_event& event);

// Returns the cached local date/time advanced by the system millisecond tick.
bool hal_get_cached_datetime(rtc_datetime& datetime);
