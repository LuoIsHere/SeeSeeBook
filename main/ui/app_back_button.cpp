#include "app_back_button.hpp"

#include <esp_log.h>

#include "hal.hpp"
#include "ui_config.hpp"

namespace {

constexpr char log_tag[] = "ui_back_button";

}  // namespace

app_back_button::app_back_button(const app_back_button_config& config)
    : config_(config)
{
}

app_back_button_result app_back_button::handle_touch(const touch_event& event)
{
    if (event.type == touch_event_type::press) {
        if (!ui_point_in_rect(event.start_x, event.start_y, config_.rect)) {
            return app_back_button_result::ignored;
        }
        active_ = true;
        submit_feedback(true);
        return app_back_button_result::handled;
    }

    if (!active_) {
        return app_back_button_result::ignored;
    }

    if (event.type == touch_event_type::click) {
        const bool released_inside =
            ui_point_in_rect(event.end_x, event.end_y, config_.rect);
        submit_feedback(false);
        active_ = false;
        return released_inside ? app_back_button_result::clicked
                               : app_back_button_result::handled;
    }

    if (event.type == touch_event_type::long_press_end) {
        submit_feedback(false);
        active_ = false;
    }
    return app_back_button_result::handled;
}

void app_back_button::reset()
{
    active_ = false;
}

void app_back_button::submit_feedback(bool pressed)
{
    display_control_request request = {};
    request.type = display_control_type::app_back_button;
    request.mode = refresh_mode::fastest;
    request.update_region = display_update_region::control;
    request.rect = config_.rect;
    request.pressed = pressed;
    // The destination App performs a lifecycle Quality refresh after a click.
    request.allow_quality_cleanup = false;
    if (!hal_submit_display_control_request(request)) {
        ESP_LOGW(log_tag, "back button feedback queue unavailable");
    }
}
