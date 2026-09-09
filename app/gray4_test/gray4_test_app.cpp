#include "gray4_test_app.hpp"

#include <esp_log.h>

#include "app.hpp"
#include "gray4_test_view.hpp"
#include "ui_renderer.hpp"

namespace {
constexpr char log_tag[] = "app_gray4_test";
}

void gray4_test_app::handle_app_event(const app_event& event)
{
    if (event.type == app_event_type::ui_action &&
        event.action.control == ui_control_type::navigate_back &&
        event.action.input.gesture == input_gesture_type::click) {
        app_request_back();
    }
}

void gray4_test_app::on_open()
{
    ui_render_gray4_test({}, ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "Gray4TestApp opened");
}
