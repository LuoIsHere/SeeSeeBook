#include "status_bar.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

namespace {

portMUX_TYPE state_mutex = portMUX_INITIALIZER_UNLOCKED;
status_bar_state current_state = {};
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

status_bar_state status_bar_get_state()
{
    status_bar_state state = {};
    portENTER_CRITICAL(&state_mutex);
    state = current_state;
    portEXIT_CRITICAL(&state_mutex);
    return state;
}
