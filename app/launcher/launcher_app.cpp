#include "launcher_app.hpp"

#include <cstdio>

#include <esp_log.h>

#include "app.hpp"
#include "launcher_layout.hpp"
#include "ui_renderer.hpp"

namespace {
constexpr char log_tag[] = "app_launcher";
}

void launcher_app::handle_app_event(const app_event& event)
{
    if (event.type == app_event_type::navigation) {
        handle_navigation(event.navigation.action);
        return;
    }
    if (event.type != app_event_type::ui_action ||
        event.action.control != ui_control_type::launcher_entry ||
        event.action.input.gesture != input_gesture_type::click) {
        return;
    }
    activate_entry(event.action.index);
}

void launcher_app::activate_entry(std::uint8_t index)
{
    if (index >= launcher_layout_entry_count) {
        return;
    }
    ESP_LOGI(log_tag, "launcher entry selected index=%u", index);
    app_request_switch(launcher_entries[index].target);
}

void launcher_app::handle_navigation(navigation_action action)
{
    bool moved = false;
    if (action == navigation_action::previous) {
        moved = selection_.move_previous(0U, launcher_layout_entry_count);
    } else if (action == navigation_action::next) {
        moved = selection_.move_next(0U, launcher_layout_entry_count);
    } else if (action == navigation_action::confirm &&
               selection_.selected(0U, launcher_layout_entry_count)) {
        activate_entry(static_cast<std::uint8_t>(selection_.index()));
        return;
    }
    if (moved) {
        view_.selected_index = static_cast<std::uint8_t>(selection_.index());
        ui_render_launcher(view_, ui_update_reason::selection_changed);
    }
}

void launcher_app::on_open()
{
    selection_.clear();
    view_ = {};
    view_.entry_count = static_cast<std::uint8_t>(launcher_layout_entry_count);
    for (std::size_t index = 0U; index < launcher_layout_entry_count; ++index) {
        std::snprintf(
            view_.entries[index].label,
            sizeof(view_.entries[index].label),
            "%s",
            launcher_entries[index].label);
    }
    ui_render_launcher(view_, ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "LauncherApp opened");
}
