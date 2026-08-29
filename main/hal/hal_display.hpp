#pragma once

#include <cstdint>
#include <type_traits>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "hal_battery.hpp"

#define CONTROL_GHOST_DEBT_LIMIT 20U
#define RTC_EDITOR_GHOST_DEBT_LIMIT 12U
#define STATUS_BAR_GHOST_DEBT_LIMIT 60U
#define TEST_CONTENT_GHOST_DEBT_LIMIT 10U
#define BATTERY_CONTENT_GHOST_DEBT_LIMIT 10U

#define PAPER_MONO_DISPLAY_ROTATION 0U
#define PAPER_MONO_PORTRAIT_WIDTH 480U
#define PAPER_MONO_PORTRAIT_HEIGHT 800U

#define FRONT_LIGHT_BAR_HEIGHT 72U
#define FRONT_LIGHT_LEVEL_COUNT 5U
#define FRONT_LIGHT_DEFAULT_LEVEL_INDEX 2U
#define FRONT_LIGHT_LEVEL_OFF 0U
#define FRONT_LIGHT_LEVEL_25 64U
#define FRONT_LIGHT_LEVEL_50 128U
#define FRONT_LIGHT_LEVEL_75 192U
#define FRONT_LIGHT_LEVEL_100 255U

#define DISPLAY_REQUEST_QUEUE_LENGTH 2U
#define DISPLAY_CONTROL_QUEUE_LENGTH 16U
#define DISPLAY_TASK_STACK_SIZE 6144U
#define DISPLAY_TASK_PRIORITY 5U

enum class refresh_mode : std::uint8_t {
    fastest,
    fast,
    quality,
};

enum class display_view : std::uint8_t {
    menu,
    test,
    rtc_setting,
    battery,
};

enum class display_update_region : std::uint8_t {
    full,
    control,
    rtc_editor,
    rtc_editor_and_key,
    status_bar,
    test_content,
    battery_content,
};

struct ui_rect {
    std::int16_t left;
    std::int16_t top;
    std::int16_t width;
    std::int16_t height;
};

enum class ui_text_state : std::uint8_t {
    hi_xi,
    hello_world,
};

enum class touch_display_type : std::uint8_t {
    none,
    click,
    long_press,
};

enum class display_control_type : std::uint8_t {
    front_light,
    menu_entry,
    app_back_button,
    rtc_key,
};

enum class rtc_edit_field : std::uint8_t {
    none,
    year,
    month,
    day,
    hour,
    minute,
    second,
};

enum class rtc_setting_message : std::uint8_t {
    none,
    select_field,
    incomplete,
    invalid_date,
    invalid_time,
    rtc_unavailable,
    saving,
    write_failed,
};

struct rtc_setting_view_state {
    std::uint8_t digits[14];
    rtc_edit_field selected_field;
    rtc_setting_message message;
    bool loading;
    bool saving;
    bool rtc_available;
};

struct battery_view_state {
    battery_snapshot snapshot;
    bool loading;
};

// Apps provide both the desired waveform and semantic dirty region.
struct display_request {
    display_view view;
    refresh_mode mode;
    display_update_region update_region;
    ui_text_state text_state;
    touch_display_type touch_type;
    std::int16_t start_x;
    std::int16_t start_y;
    std::int16_t end_x;
    std::int16_t end_y;
    std::uint32_t duration_ms;
    std::uint32_t timestamp_ms;
    std::uint32_t minimum_refresh_interval_ms;
    std::uint16_t released_key_mask;
    bool allow_quality_cleanup;
    rtc_setting_view_state rtc_setting;
    battery_view_state battery;
};

// Control feedback uses the same mode and region contract as frame requests.
struct display_control_request {
    display_control_type type;
    refresh_mode mode;
    display_update_region update_region;
    std::uint8_t button_index;
    ui_rect rect;
    bool pressed;
    bool apply_level;
    bool allow_quality_cleanup;
};

static_assert(std::is_trivially_copyable_v<display_request>);
static_assert(std::is_trivially_copyable_v<display_control_request>);

// Starts the display worker that owns all M5Unified display refresh operations.
bool hal_display_start(
    QueueHandle_t request_queue,
    QueueHandle_t control_queue,
    TaskHandle_t& task_handle);

// Queues the latest application state while preserving lifecycle quality refreshes.
bool hal_submit_display_request(const display_request& request);

// Queues a visual control transition without blocking the Mooncake scheduler.
bool hal_submit_display_control_request(const display_control_request& request);

// Wakes the display worker so changed system status can refresh the status bar.
void hal_request_status_bar_refresh();
