#include "test_app.hpp"

#include <esp_log.h>

#include "app.hpp"
#include "front_light_service.hpp"
#include "ui_renderer.hpp"

namespace {
constexpr char log_tag[] = "app_test";
}

test_app::test_app()
{
    setAppInfo().name = "TestApp";
}

void test_app::handle_app_event(const app_event& event)
{
    if (event.type != app_event_type::ui_action) {
        return;
    }
    const ui_action_event& action = event.action;
    if (action.input.gesture == input_gesture_type::click) {
        if (action.control == ui_control_type::navigate_back) {
            app_request_back();
            return;
        }
        if (action.control == ui_control_type::front_light) {
            if (front_light_service_set_level(action.index)) {
                view_.front_light_level = action.index;
                ui_render_test(view_, ui_update_reason::selection_changed);
            }
            return;
        }
        if (action.control == ui_control_type::test_surface) {
            view_.text_state = test_text_state::hello_world;
            view_.touch_type = test_touch_display_type::click;
            view_.start_x = action.input.start_x;
            view_.start_y = action.input.start_y;
            view_.end_x = action.input.end_x;
            view_.end_y = action.input.end_y;
            view_.duration_ms = action.input.duration_ms;
            view_.timestamp_ms = action.input.timestamp_ms;
            ui_render_test(view_, ui_update_reason::content_changed);
        }
        return;
    }
    if (action.control == ui_control_type::test_surface &&
        (action.input.gesture == input_gesture_type::long_press_start ||
         action.input.gesture == input_gesture_type::long_press_repeat ||
         action.input.gesture == input_gesture_type::long_press_end)) {
        view_.touch_type = test_touch_display_type::long_press;
        view_.start_x = action.input.start_x;
        view_.start_y = action.input.start_y;
        view_.end_x = action.input.end_x;
        view_.end_y = action.input.end_y;
        view_.duration_ms = action.input.duration_ms;
        view_.timestamp_ms = action.input.timestamp_ms;
        ui_render_test(view_, ui_update_reason::content_changed);
    }
}

void test_app::on_open()
{
    view_.front_light_level = front_light_service_get_level();
    ui_render_test(view_, ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "TestApp opened");
}
