#include "system_event_dispatcher.hpp"

#include "app.hpp"
#include "battery_service.hpp"
#include "input_service.hpp"
#include "rtc_service.hpp"
#include "storage_service.hpp"
#include "system_config.hpp"
#include "system_event.hpp"
#include "system_status_controller.hpp"
#include "ui_interaction_router.hpp"

namespace {

void route_event(const system_event& event)
{
    app_event target = {};
    switch (event.type) {
        case system_event_type::ui_action:
            target.type = app_event_type::ui_action;
            target.action = event.action;
            break;
        case system_event_type::rtc:
            target.type = app_event_type::rtc;
            target.rtc = event.rtc;
            break;
        case system_event_type::battery:
            target.type = app_event_type::battery;
            target.battery = event.battery;
            break;
        case system_event_type::storage_status:
            target.type = app_event_type::storage_status;
            target.storage_status = event.storage_status;
            break;
        case system_event_type::storage_result:
            target.type = app_event_type::storage_result;
            target.storage_result = event.storage_result;
            break;
    }
    app_dispatch_event(target);
}

bool collect_one_event(system_event& event)
{
    input_event input = {};
    if (input_service_try_get_event(input)) {
        ui_action_event action = {};
        if (ui_interaction_process(input, action)) {
            event.type = system_event_type::ui_action;
            event.action = action;
            return true;
        }
    }

    rtc_service_event rtc = {};
    if (rtc_service_try_get_event(rtc)) {
        event.type = system_event_type::rtc;
        event.rtc = rtc;
        if (rtc.success) {
            system_status_update_time(rtc.datetime, true);
        }
        return true;
    }

    battery_service_event battery = {};
    if (battery_service_try_get_event(battery)) {
        event.type = system_event_type::battery;
        event.battery = battery;
        system_status_update_battery(battery.snapshot);
        return true;
    }

    storage_status_event storage_status = {};
    if (storage_service_try_get_status_event(storage_status)) {
        event.type = system_event_type::storage_status;
        event.storage_status = storage_status;
        return true;
    }

    storage_result_event storage_result = {};
    if (storage_service_try_get_result_event(storage_result)) {
        event.type = system_event_type::storage_result;
        event.storage_result = storage_result;
        return true;
    }
    return false;
}

}  // namespace

void system_event_dispatcher_update()
{
    rtc_datetime datetime = {};
    const bool datetime_valid = rtc_service_get_cached_datetime(datetime);
    system_status_update_time(datetime, datetime_valid);

    for (std::uint8_t count = 0U; count < SYSTEM_EVENTS_PER_UPDATE; ++count) {
        system_event event = {};
        if (!collect_one_event(event)) {
            break;
        }
        route_event(event);
        if (event.type == system_event_type::storage_result) {
            result_handle queue_owner = event.storage_result.handle;
            storage_service_release_result(queue_owner);
        }
        if (app_switch_pending()) {
            break;
        }
    }
}
