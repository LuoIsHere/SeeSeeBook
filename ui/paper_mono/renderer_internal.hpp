#pragma once

#include <cstdint>
#include <type_traits>

#include "battery_view.hpp"
#include "display.hpp"
#include "file_view.hpp"
#include "rtc_view.hpp"
#include "test_view.hpp"
#include "ui_action.hpp"

#define CONTROL_GHOST_DEBT_LIMIT 20U
#define RTC_EDITOR_GHOST_DEBT_LIMIT 12U
#define STATUS_BAR_GHOST_DEBT_LIMIT 60U
#define TEST_CONTENT_GHOST_DEBT_LIMIT 10U
#define BATTERY_CONTENT_GHOST_DEBT_LIMIT 10U
#define FILE_CONTENT_GHOST_DEBT_LIMIT 10U

#define FRONT_LIGHT_BAR_HEIGHT 72U
#define FRONT_LIGHT_LEVEL_COUNT 5U
#define FRONT_LIGHT_DEFAULT_LEVEL_INDEX 2U

#define DISPLAY_REQUEST_QUEUE_LENGTH 2U
#define DISPLAY_CONTROL_QUEUE_LENGTH 16U
#define DISPLAY_TASK_STACK_SIZE 12288U
#define DISPLAY_TASK_PRIORITY 5U

enum class display_update_region : std::uint8_t {
    full,
    control,
    rtc_editor,
    rtc_editor_and_key,
    status_bar,
    test_content,
    battery_content,
    file_content,
};

struct display_request {
    ui_view_id view;
    refresh_mode mode;
    display_update_region update_region;
    test_view_state test;
    rtc_view_state rtc;
    battery_view_state battery;
    file_view_state file;
    std::uint32_t minimum_refresh_interval_ms;
    std::uint16_t released_key_mask;
    bool allow_quality_cleanup;
};

struct display_control_request {
    ui_control_type control;
    refresh_mode mode;
    display_update_region update_region;
    std::uint8_t index;
    bool pressed;
    bool allow_quality_cleanup;
};

static_assert(std::is_trivially_copyable_v<display_request>);
static_assert(std::is_trivially_copyable_v<display_control_request>);
