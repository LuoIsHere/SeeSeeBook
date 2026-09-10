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
    if (event.type != app_event_type::ui_action ||
        event.action.control != ui_control_type::launcher_entry ||
        event.action.input.gesture != input_gesture_type::click ||
        event.action.index >= launcher_layout_entry_count) {
        return;
    }
    const auto index = event.action.index;
    ESP_LOGI(log_tag, "launcher entry selected index=%u", index);
    app_request_switch(launcher_entries[index].target);
}

void launcher_app::on_open()
{
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
