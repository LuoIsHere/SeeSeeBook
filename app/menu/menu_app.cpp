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
    if (event.type == app_event_type::navigation) {
        handle_navigation(event.navigation.action);
        return;
    }
    if (event.type != app_event_type::ui_action ||
        event.action.input.gesture != input_gesture_type::click) {
        return;
    }
    if (event.action.control == ui_control_type::navigate_back) {
        app_request_back();
        return;
    }
    if (event.action.control != ui_control_type::menu_entry) {
        return;
    }
    activate_entry(event.action.index);
}

void menu_app::activate_entry(std::uint8_t index)
{
    if (index >= menu_layout_entry_count) {
        return;
    }
    ESP_LOGI(log_tag, "menu entry selected index=%u", index);
    app_request_switch(menu_entries[index].target);
}

void menu_app::handle_navigation(navigation_action action)
{
    bool moved = false;
    if (action == navigation_action::previous) {
        moved = selection_.move_previous(0U, menu_layout_entry_count);
    } else if (action == navigation_action::next) {
        moved = selection_.move_next(0U, menu_layout_entry_count);
    } else if (action == navigation_action::confirm &&
               selection_.selected(0U, menu_layout_entry_count)) {
        activate_entry(static_cast<std::uint8_t>(selection_.index()));
        return;
    }
    if (moved) {
        view_.selected_index = static_cast<std::uint8_t>(selection_.index());
        ui_render_menu(view_, ui_update_reason::selection_changed);
    }
}

void menu_app::on_open()
{
    selection_.clear();
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
