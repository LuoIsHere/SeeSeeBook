#include "menu_app.hpp"

#include <cstdio>

#include <esp_log.h>

#include "app.hpp"
#include "menu_layout.hpp"
#include "ui_renderer.hpp"

namespace {
constexpr char log_tag[] = "app_menu";
}

void menu_app::handle_app_event(const app_event& event)
{
    if (event.type != app_event_type::ui_action ||
        event.action.control != ui_control_type::menu_entry ||
        event.action.input.gesture != input_gesture_type::click) {
        return;
    }
    const std::uint8_t index = event.action.index;
    if (index >= menu_layout_entry_count) {
        return;
    }
    ESP_LOGI(log_tag, "menu entry selected index=%u", index);
    app_request_switch(menu_entries[index].target);
}

void menu_app::on_open()
{
    view_ = {};
    view_.entry_count = static_cast<std::uint8_t>(menu_layout_entry_count);
    for (std::size_t index = 0U; index < menu_layout_entry_count; ++index) {
        std::snprintf(
            view_.entries[index].label,
            sizeof(view_.entries[index].label),
            "%s",
            menu_entries[index].label);
    }
    ui_render_menu(view_, ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "MenuApp opened");
}
