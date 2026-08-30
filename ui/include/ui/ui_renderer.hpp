#pragma once

#include <cstdint>

#include <esp_err.h>

#include "battery_view.hpp"
#include "file_view.hpp"
#include "menu_view.hpp"
#include "rtc_view.hpp"
#include "status_bar_view.hpp"
#include "test_view.hpp"
#include "ui_action.hpp"

enum class ui_update_reason : std::uint8_t {
    view_opened,
    content_changed,
    selection_changed,
    popup_changed,
};

esp_err_t ui_renderer_init();

bool ui_render_menu(
    const menu_view_state& state,
    ui_update_reason reason);
bool ui_render_test(
    const test_view_state& state,
    ui_update_reason reason);
bool ui_render_rtc(
    const rtc_view_state& state,
    ui_update_reason reason,
    ui_control_type released_control = ui_control_type::none,
    std::uint8_t released_index = 0U);
bool ui_render_battery(
    const battery_view_state& state,
    ui_update_reason reason);
bool ui_render_file(
    const file_view_state& state,
    ui_update_reason reason);

// Control feedback is driven by the UI interaction middleware, not by Apps.
bool ui_render_control(
    ui_control_type control,
    std::uint8_t index,
    bool pressed);

bool ui_status_bar_update_time(
    std::uint8_t hour,
    std::uint8_t minute,
    bool valid);
bool ui_status_bar_update_battery(const battery_snapshot& snapshot);
status_bar_view_state ui_status_bar_get_state();
void ui_renderer_notify_status_bar();
