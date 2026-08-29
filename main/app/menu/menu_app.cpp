#include "menu_app.hpp"

#include <esp_log.h>

#include "app.hpp"
#include "hal.hpp"
#include "types.hpp"
#include "ui_config.hpp"

namespace {

constexpr char log_tag[] = "app_menu";

bool point_in_entry(std::int16_t x, std::int16_t y)
{
    return ui_point_in_rect(
        x,
        y,
        MENU_SCREEN_EDGE_MARGIN,
        MENU_ENTRY_TOP,
        PAPER_MONO_PORTRAIT_WIDTH - MENU_SCREEN_EDGE_MARGIN * 2,
        MENU_ENTRY_HEIGHT);
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
        if (!point_in_entry(touch.start_x, touch.start_y)) {
            return;
        }
        entry_active_ = true;
        submit_entry_feedback(true);
        return;
    }

    if (!entry_active_) {
        return;
    }

    if (touch.type == touch_event_type::click) {
        const bool released_inside = point_in_entry(touch.end_x, touch.end_y);
        submit_entry_feedback(false);
        entry_active_ = false;
        if (released_inside) {
            ESP_LOGI(log_tag, "TestApp entry selected");
            app_request_switch(app_kind::test);
        }
        return;
    }

    if (touch.type == touch_event_type::long_press_end) {
        submit_entry_feedback(false);
        entry_active_ = false;
    }
}

void menu_app::on_open()
{
    entry_active_ = false;
    submit_frame(true);
    ESP_LOGI(log_tag, "MenuApp opened");
}

void menu_app::submit_frame(bool force_quality)
{
    display_request request = {};
    request.view = display_view::menu;
    request.touch_type = touch_display_type::none;
    request.force_quality = force_quality;
    if (!hal_submit_display_request(request)) {
        ESP_LOGW(log_tag, "display request queue unavailable");
    }
}

void menu_app::submit_entry_feedback(bool pressed)
{
    display_control_request request = {};
    request.type = display_control_type::menu_entry;
    request.pressed = pressed;
    if (!hal_submit_display_control_request(request)) {
        ESP_LOGW(log_tag, "menu entry feedback queue unavailable");
    }
}
