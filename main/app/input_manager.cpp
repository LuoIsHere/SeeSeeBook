#include "input_manager.hpp"

#include "app.hpp"
#include "app_base.hpp"
#include "hal.hpp"
#include "status_bar.hpp"
#include "system_config.hpp"

namespace {

app_base* target_app = nullptr;

void update_status_bar_time()
{
    rtc_datetime datetime = {};
    const bool valid = hal_get_cached_datetime(datetime);
    const bool changed = status_bar_update_time(
        valid ? datetime.time.hour : 0U,
        valid ? datetime.time.minute : 0U,
        valid);
    if (changed) {
        hal_request_status_bar_refresh();
    }
}

}  // namespace

void input_manager_set_target(app_base* new_target_app)
{
    target_app = new_target_app;
}

void input_manager_update()
{
    update_status_bar_time();
    for (std::uint32_t index = 0; index < INPUT_EVENTS_PER_UPDATE; ++index) {
        bool event_dispatched = false;
        touch_event touch = {};
        if (hal_try_get_touch_event(touch)) {
            app_event event = {};
            event.type = app_event_type::touch;
            event.touch = touch;
            if (target_app != nullptr) {
                target_app->handle_app_event(event);
            }
            event_dispatched = true;
        }

        if (app_switch_pending()) {
            return;
        }

        rtc_event rtc = {};
        if (hal_try_get_rtc_event(rtc)) {
            app_event event = {};
            event.type = app_event_type::rtc;
            event.rtc = rtc;
            if (target_app != nullptr) {
                target_app->handle_app_event(event);
            }
            event_dispatched = true;
        }

        if (app_switch_pending()) {
            return;
        }

        battery_event battery = {};
        if (hal_try_get_battery_event(battery)) {
            if (status_bar_update_battery(battery.snapshot)) {
                hal_request_status_bar_refresh();
            }
            app_event event = {};
            event.type = app_event_type::battery;
            event.battery = battery;
            if (target_app != nullptr) {
                target_app->handle_app_event(event);
            }
            event_dispatched = true;
        }

        if (app_switch_pending()) {
            return;
        }

        sd_status_event storage_status = {};
        if (hal_try_get_storage_status_event(storage_status)) {
            app_event event = {};
            event.type = app_event_type::storage_status;
            event.storage_status = storage_status;
            if (target_app != nullptr) {
                target_app->handle_app_event(event);
            }
            event_dispatched = true;
        }

        if (app_switch_pending()) {
            return;
        }

        storage_event storage_result = {};
        if (storage_service_try_get_event(storage_result)) {
            app_event event = {};
            event.type = app_event_type::storage_result;
            event.storage_result = storage_result;
            if (target_app != nullptr) {
                target_app->handle_app_event(event);
            }
            storage_service_release_event(storage_result);
            event_dispatched = true;
        }

        if (app_switch_pending()) {
            return;
        }
        if (!event_dispatched) {
            return;
        }
    }
}
