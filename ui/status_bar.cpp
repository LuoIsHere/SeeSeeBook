#include "status_bar.hpp"

#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include "ui_renderer.hpp"

namespace {

portMUX_TYPE state_mutex = portMUX_INITIALIZER_UNLOCKED;
status_bar_view_state current_state = {};
constexpr char log_tag[] = "ui_status_bar";

bool battery_visuals_equal(
    const battery_snapshot& left,
    const battery_snapshot& right)
{
    return left.level_valid == right.level_valid &&
           (!left.level_valid || left.percent == right.percent) &&
           left.charging_valid == right.charging_valid &&
           (!left.charging_valid || left.charging == right.charging);
}

}  // namespace

bool status_bar_update_time(std::uint8_t hour, std::uint8_t minute, bool valid)
{
    bool changed = false;
    portENTER_CRITICAL(&state_mutex);
    changed = current_state.time_valid != valid ||
              (valid && (current_state.hour != hour || current_state.minute != minute));
    current_state.hour = hour;
    current_state.minute = minute;
    current_state.time_valid = valid;
    portEXIT_CRITICAL(&state_mutex);
    if (changed) {
        ESP_LOGI(
            log_tag,
            "time updated valid=%u hour=%u minute=%u",
            valid ? 1U : 0U,
            static_cast<unsigned>(hour),
            static_cast<unsigned>(minute));
    }
    return changed;
}

bool status_bar_update_battery(const battery_snapshot& snapshot)
{
    bool changed = false;
    portENTER_CRITICAL(&state_mutex);
    changed = !battery_visuals_equal(current_state.battery, snapshot);
    current_state.battery = snapshot;
    portEXIT_CRITICAL(&state_mutex);
    if (changed) {
        ESP_LOGI(
            log_tag,
            "battery updated level_valid=%u percent=%u charging_valid=%u charging=%u",
            snapshot.level_valid ? 1U : 0U,
            static_cast<unsigned>(snapshot.percent),
            snapshot.charging_valid ? 1U : 0U,
            snapshot.charging ? 1U : 0U);
    }
    return changed;
}

status_bar_view_state status_bar_get_state()
{
    status_bar_view_state state = {};
    portENTER_CRITICAL(&state_mutex);
    state = current_state;
    portEXIT_CRITICAL(&state_mutex);
    return state;
}

bool ui_status_bar_update_time(std::uint8_t hour, std::uint8_t minute, bool valid)
{
    return status_bar_update_time(hour, minute, valid);
}

bool ui_status_bar_update_battery(const battery_snapshot& snapshot)
{
    return status_bar_update_battery(snapshot);
}

status_bar_view_state ui_status_bar_get_state()
{
    return status_bar_get_state();
}

bool ui_status_bar_set_foreground(ui_view_id app)
{
    portENTER_CRITICAL(&state_mutex);
    const bool changed = current_state.foreground_app != app ||
                         current_state.center_kind != status_bar_center_kind::none;
    current_state.foreground_app = app;
    current_state.center_kind = status_bar_center_kind::none;
    std::memset(current_state.center_text, 0, sizeof(current_state.center_text));
    current_state.center_current_page = 0U;
    current_state.center_total_pages = 0U;
    portEXIT_CRITICAL(&state_mutex);
    return changed;
}

bool ui_status_bar_set_center_text(const char* text)
{
    char candidate[status_bar_center_text_capacity] = {};
    if (text != nullptr) {
        std::snprintf(candidate, sizeof(candidate), "%s", text);
    }
    const status_bar_center_kind kind = candidate[0] == '\0'
                                            ? status_bar_center_kind::none
                                            : status_bar_center_kind::text;
    portENTER_CRITICAL(&state_mutex);
    const bool changed = current_state.center_kind != kind ||
                         std::strncmp(
                             current_state.center_text,
                             candidate,
                             sizeof(candidate)) != 0;
    current_state.center_kind = kind;
    std::memcpy(current_state.center_text, candidate, sizeof(candidate));
    current_state.center_current_page = 0U;
    current_state.center_total_pages = 0U;
    portEXIT_CRITICAL(&state_mutex);
    return changed;
}

bool ui_status_bar_set_page_status(
    bool valid,
    std::uint32_t current,
    std::uint32_t total)
{
    valid = valid && current > 0U && current <= total;
    const status_bar_center_kind kind = valid
                                            ? status_bar_center_kind::page
                                            : status_bar_center_kind::none;
    portENTER_CRITICAL(&state_mutex);
    const bool changed = current_state.center_kind != kind ||
                         (valid &&
                          (current_state.center_current_page != current ||
                           current_state.center_total_pages != total));
    current_state.center_kind = kind;
    std::memset(current_state.center_text, 0, sizeof(current_state.center_text));
    current_state.center_current_page = valid ? current : 0U;
    current_state.center_total_pages = valid ? total : 0U;
    portEXIT_CRITICAL(&state_mutex);
    if (changed && valid && (current > 999999U || total > 999999U)) {
        ESP_LOGW(log_tag, "page center hidden: current=%lu total=%lu",
                 static_cast<unsigned long>(current), static_cast<unsigned long>(total));
    }
    return changed;
}

bool ui_status_bar_clear_center()
{
    return ui_status_bar_set_center_text(nullptr);
}

bool ui_status_bar_update_reader_page(
    bool valid,
    std::uint32_t current,
    std::uint32_t total)
{
    portENTER_CRITICAL(&state_mutex);
    const bool reader_foreground =
        current_state.foreground_app == ui_view_id::reader;
    portEXIT_CRITICAL(&state_mutex);
    return reader_foreground
               ? ui_status_bar_set_page_status(valid, current, total)
               : false;
}
