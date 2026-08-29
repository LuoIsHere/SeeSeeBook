#include "menu_app.hpp"

#include <esp_log.h>

#include "app.hpp"
#include "hal.hpp"
#include "types.hpp"
#include "ui_config.hpp"

namespace {

constexpr char log_tag[] = "app_menu";

bool get_entry_index(
    std::int16_t x,
    std::int16_t y,
    std::uint8_t& entry_index)
{
    for (std::uint8_t index = 0; index < MENU_ENTRY_COUNT; ++index) {
        if (ui_point_in_rect(x, y, menu_entry_rect(index))) {
            entry_index = index;
            return true;
        }
    }
    return false;
}

}  // namespace

menu_app::menu_app()
{
    // setAppInfo is part of Mooncake's external API.
    setAppInfo().name = "MenuApp";
}

void menu_app::handle_app_event(const app_event& event)
{
    if (event.type != app_event_type::touch) {
        return;
    }

    const touch_event& touch = event.touch;
    if (touch.type == touch_event_type::press) {
        std::uint8_t entry_index = 0;
        if (!get_entry_index(touch.start_x, touch.start_y, entry_index)) {
            return;
        }
        active_entry_index_ = static_cast<std::int8_t>(entry_index);
        submit_entry_feedback(entry_index, true);
        return;
    }

    if (active_entry_index_ < 0) {
        return;
    }

    if (touch.type == touch_event_type::click) {
        const std::uint8_t captured_entry = static_cast<std::uint8_t>(active_entry_index_);
        std::uint8_t released_entry = 0;
        const bool released_inside =
            get_entry_index(touch.end_x, touch.end_y, released_entry) &&
            released_entry == captured_entry;
        submit_entry_feedback(captured_entry, false);
        active_entry_index_ = -1;
        if (released_inside) {
            app_kind target = app_kind::test;
            if (captured_entry == 1U) {
                target = app_kind::rtc_setting;
            } else if (captured_entry == 2U) {
                target = app_kind::battery;
            } else if (captured_entry == 3U) {
                target = app_kind::file;
            }
            ESP_LOGI(log_tag, "menu entry selected index=%u", captured_entry);
            app_request_switch(target);
        }
        return;
    }

    if (touch.type == touch_event_type::long_press_end) {
        submit_entry_feedback(static_cast<std::uint8_t>(active_entry_index_), false);
        active_entry_index_ = -1;
    }
}

void menu_app::on_open()
{
    active_entry_index_ = -1;
    submit_frame(refresh_mode::quality, display_update_region::full);
    ESP_LOGI(log_tag, "MenuApp opened");
}

void menu_app::submit_frame(
    refresh_mode mode,
    display_update_region update_region)
{
    display_request request = {};
    request.view = display_view::menu;
    request.mode = mode;
    request.update_region = update_region;
    request.touch_type = touch_display_type::none;
    request.allow_quality_cleanup = true;
    if (!hal_submit_display_request(request)) {
        ESP_LOGW(log_tag, "display request queue unavailable");
    }
}

void menu_app::submit_entry_feedback(
    std::uint8_t entry_index,
    bool pressed)
{
    display_control_request request = {};
    request.type = display_control_type::menu_entry;
    request.mode = refresh_mode::fastest;
    request.update_region = display_update_region::control;
    request.button_index = entry_index;
    request.pressed = pressed;
    request.allow_quality_cleanup = !pressed;
    if (!hal_submit_display_control_request(request)) {
        ESP_LOGW(log_tag, "menu entry feedback queue unavailable");
    }
}
