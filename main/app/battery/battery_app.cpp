#include "battery_app.hpp"

#include <cstdlib>

#include <esp_log.h>

#include "app.hpp"
#include "hal.hpp"
#include "ui_config.hpp"

namespace {

constexpr char log_tag[] = "app_battery";

std::uint16_t unsigned_difference(std::uint16_t left, std::uint16_t right)
{
    return left >= right ? static_cast<std::uint16_t>(left - right)
                         : static_cast<std::uint16_t>(right - left);
}

bool battery_change_is_visible(
    const battery_snapshot& previous,
    const battery_snapshot& current)
{
    if (previous.level_valid != current.level_valid ||
        previous.voltage_valid != current.voltage_valid ||
        previous.current_valid != current.current_valid ||
        previous.charging_valid != current.charging_valid) {
        return true;
    }
    if (current.level_valid && previous.percent != current.percent) {
        return true;
    }
    if (current.voltage_valid &&
        unsigned_difference(previous.voltage_mv, current.voltage_mv) >=
            BATTERY_VOLTAGE_CHANGE_THRESHOLD_MV) {
        return true;
    }
    if (current.current_valid &&
        std::abs(previous.current_ma - current.current_ma) >=
            BATTERY_CURRENT_CHANGE_THRESHOLD_MA) {
        return true;
    }
    return current.charging_valid && previous.charging != current.charging;
}

}  // namespace

battery_app::battery_app()
    : back_button_({battery_back_button_rect()})
{
    // setAppInfo is part of Mooncake's external API.
    setAppInfo().name = "BatteryApp";
}

void battery_app::handle_app_event(const app_event& event)
{
    if (event.type == app_event_type::touch) {
        const app_back_button_result result = back_button_.handle_touch(event.touch);
        if (result == app_back_button_result::clicked) {
            ESP_LOGI(log_tag, "back button selected");
            app_request_back();
        }
        return;
    }
    if (event.type == app_event_type::battery) {
        handle_battery_event(event.battery);
    }
}

void battery_app::on_open()
{
    back_button_.reset();
    view_ = {};
    has_snapshot_ = hal_get_cached_battery_snapshot(view_.snapshot);
    view_.loading = !has_snapshot_;
    hal_set_battery_app_sampling(true);
    submit_frame(refresh_mode::quality, display_update_region::full);
    ESP_LOGI(log_tag, "BatteryApp opened cached=%u", has_snapshot_ ? 1U : 0U);
}

void battery_app::on_close()
{
    hal_set_battery_app_sampling(false);
    back_button_.reset();
    view_ = {};
    has_snapshot_ = false;
    ESP_LOGI(log_tag, "BatteryApp closed");
}

void battery_app::handle_battery_event(const battery_event& event)
{
    const bool should_refresh =
        !has_snapshot_ || battery_change_is_visible(view_.snapshot, event.snapshot);
    view_.snapshot = event.snapshot;
    view_.loading = false;
    has_snapshot_ = true;
    if (should_refresh) {
        submit_frame(refresh_mode::fastest, display_update_region::battery_content);
    }
}

void battery_app::submit_frame(
    refresh_mode mode,
    display_update_region update_region)
{
    display_request request = {};
    request.view = display_view::battery;
    request.mode = mode;
    request.update_region = update_region;
    request.battery = view_;
    request.allow_quality_cleanup = true;
    if (!hal_submit_display_request(request)) {
        ESP_LOGW(log_tag, "display request queue unavailable");
    }
}
