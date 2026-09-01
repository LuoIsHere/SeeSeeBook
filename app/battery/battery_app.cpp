#include "battery_app.hpp"

#include <cstdlib>

#include <esp_log.h>

#include "app.hpp"
#include "battery_service.hpp"
#include "ui_renderer.hpp"

namespace {

constexpr char log_tag[] = "app_battery";

std::uint16_t unsigned_difference(std::uint16_t left, std::uint16_t right)
{
    return left >= right ? left - right : right - left;
}

bool visible_change(const battery_snapshot& previous, const battery_snapshot& current)
{
    if (previous.level_valid != current.level_valid ||
        previous.voltage_valid != current.voltage_valid ||
        previous.current_valid != current.current_valid ||
        previous.charging_valid != current.charging_valid) {
        return true;
    }
    return (current.level_valid && previous.percent != current.percent) ||
           (current.voltage_valid &&
            unsigned_difference(previous.voltage_mv, current.voltage_mv) >=
                BATTERY_VOLTAGE_CHANGE_THRESHOLD_MV) ||
           (current.current_valid &&
            std::abs(previous.current_ma - current.current_ma) >=
                BATTERY_CURRENT_CHANGE_THRESHOLD_MA) ||
           (current.charging_valid && previous.charging != current.charging);
}

}  // namespace

void battery_app::handle_app_event(const app_event& event)
{
    if (event.type == app_event_type::ui_action &&
        event.action.control == ui_control_type::navigate_back &&
        event.action.input.gesture == input_gesture_type::click) {
        app_request_back();
    } else if (event.type == app_event_type::battery) {
        handle_battery_event(event.battery);
    }
}

void battery_app::on_open()
{
    snapshot_ = {};
    has_snapshot_ = battery_service_get_snapshot(snapshot_);
    update_view();
    battery_service_acquire_detail_sampling();
    ui_render_battery(view_, ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "BatteryApp opened cached=%u", has_snapshot_ ? 1U : 0U);
}

void battery_app::on_close()
{
    battery_service_release_detail_sampling();
    view_ = {};
    snapshot_ = {};
    has_snapshot_ = false;
    ESP_LOGI(log_tag, "BatteryApp session cleared");
}

void battery_app::handle_battery_event(const app_battery_event& event)
{
    const bool should_refresh = !has_snapshot_ || visible_change(snapshot_, event.snapshot);
    snapshot_ = event.snapshot;
    has_snapshot_ = true;
    update_view();
    if (should_refresh) {
        ui_render_battery(view_, ui_update_reason::content_changed);
    }
}

void battery_app::update_view()
{
    view_ = {};
    view_.loading = !has_snapshot_;
    if (!has_snapshot_) {
        return;
    }
    view_.percent = snapshot_.percent;
    view_.voltage_mv = snapshot_.voltage_mv;
    view_.current_ma = snapshot_.current_ma;
    view_.charging = snapshot_.charging;
    view_.level_valid = snapshot_.level_valid;
    view_.voltage_valid = snapshot_.voltage_valid;
    view_.current_valid = snapshot_.current_valid;
    view_.charging_valid = snapshot_.charging_valid;
}
