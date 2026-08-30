#include "menu_app.hpp"

#include <esp_log.h>

#include "app.hpp"
#include "ui_renderer.hpp"

namespace {
constexpr char log_tag[] = "app_menu";
}

menu_app::menu_app()
{
    setAppInfo().name = "MenuApp";
}

void menu_app::handle_app_event(const app_event& event)
{
    if (event.type != app_event_type::ui_action ||
        event.action.control != ui_control_type::menu_entry ||
        event.action.input.gesture != input_gesture_type::click) {
        return;
    }
    const std::uint8_t index = event.action.index;
    if (index >= 4U) {
        return;
    }
    constexpr app_kind targets[] = {
        app_kind::test, app_kind::rtc_setting, app_kind::battery, app_kind::file,
    };
    ESP_LOGI(log_tag, "menu entry selected index=%u", index);
    app_request_switch(targets[index]);
}

void menu_app::on_open()
{
    ui_render_menu(view_, ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "MenuApp opened");
}
