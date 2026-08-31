#include "system_event_dispatcher.hpp"

#include "app.hpp"
#include "app_event.hpp"
#include "battery_service.hpp"
#include "input_service.hpp"
#include "rtc_service.hpp"
#include "service_event_source.hpp"
#include "storage_service.hpp"
#include "system_config.hpp"
#include "system_status_controller.hpp"
#include "ui_interaction_router.hpp"

namespace {

bool collect_one_event(app_event& event)
{
    input_event input = {};
    if (input_service_try_get_event(input)) {
        ui_action_event action = {};
        if (ui_interaction_process(input, action)) {
            event.type = app_event_type::ui_action;
            event.action = action;
            return true;
        }
    }

    rtc_service_event rtc = {};
    if (rtc_service_try_get_event(rtc)) {
        event.type = app_event_type::rtc;
        event.rtc.operation = rtc.operation == rtc_service_operation::read
                                  ? app_rtc_operation::read
                                  : app_rtc_operation::write;
        event.rtc.request_id = rtc.request_id;
        event.rtc.datetime = rtc.datetime;
        event.rtc.success = rtc.success;
        if (rtc.success) {
            system_status_update_time(rtc.datetime, true);
        }
        return true;
    }

    battery_service_event battery = {};
    if (battery_service_try_get_event(battery)) {
        event.type = app_event_type::battery;
        event.battery.snapshot = battery.snapshot;
        system_status_update_battery(battery.snapshot);
        return true;
    }

    storage_status_event storage_status = {};
    if (storage_service_try_get_status_event(storage_status)) {
        event.type = app_event_type::storage_status;
        event.storage_status.state = storage_status.state;
        event.storage_status.media_generation = storage_status.media_generation;
        event.storage_status.error_code =
            static_cast<std::int32_t>(storage_status.error);
        return true;
    }

    storage_result_event storage_result = {};
    if (storage_service_try_get_result_event(storage_result)) {
        event.type = app_event_type::storage_result;
        event.storage_result.handle = storage_result.handle;
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
        app_event event = {};
        if (!collect_one_event(event)) {
            break;
        }
        app_dispatch_event(event);
        if (event.type == app_event_type::storage_result) {
            result_handle queue_owner = event.storage_result.handle;
            storage_service_release_result(queue_owner);
        }
        if (app_switch_pending()) {
            break;
        }
    }
}
