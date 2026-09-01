#pragma once

#include <cstdint>
#include <type_traits>

#include "battery_view.hpp"
#include "display.hpp"
#include "file_view.hpp"
#include "menu_view.hpp"
#include "rtc_view.hpp"
#include "test_view.hpp"
#include "ui_action.hpp"

#define FASTEST_REFRESHES_BEFORE_FAST 10U
#define FAST_REFRESHES_BEFORE_QUALITY 5U
#define FILE_TEXT_REFRESHES_BEFORE_QUALITY 20U
#define STATUS_BAR_GHOST_DEBT_LIMIT 60U

#define FRONT_LIGHT_BAR_HEIGHT 72U
#define FRONT_LIGHT_LEVEL_COUNT 5U
#define FRONT_LIGHT_DEFAULT_LEVEL_INDEX 2U

#define DISPLAY_REQUEST_QUEUE_LENGTH 2U
#define DISPLAY_CONTROL_QUEUE_LENGTH 16U
#define DISPLAY_TASK_STACK_SIZE 12288U
#define DISPLAY_TASK_PRIORITY 5U
#define DISPLAY_IDLE_SLEEP_MS 400U

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

union display_view_payload {
    menu_view_state menu;
    test_view_state test;
    rtc_view_state rtc;
    battery_view_state battery;
    file_view_state file;
};

struct display_request {
    std::uint32_t queued_at_ms;
    std::uint32_t view_generation;
    ui_view_id view;
    refresh_mode mode;
    display_update_region update_region;
    display_view_payload payload;
    std::uint16_t released_key_mask;
    bool allow_quality_cleanup;
};

struct display_control_request {
    std::uint32_t queued_at_ms;
    ui_control_type control;
    refresh_mode mode;
    display_update_region update_region;
    std::uint8_t index;
    bool pressed;
    bool allow_quality_cleanup;
};

static_assert(std::is_trivially_copyable_v<display_view_payload>);
static_assert(std::is_trivially_copyable_v<display_request>);
static_assert(sizeof(display_request) <= 680U);
static_assert(std::is_trivially_copyable_v<display_control_request>);
