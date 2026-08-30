#include "system_status_controller.hpp"

#include "ui_renderer.hpp"

void system_status_update_time(const rtc_datetime& datetime, bool valid)
{
    if (ui_status_bar_update_time(
            datetime.time.hour,
            datetime.time.minute,
            valid)) {
        ui_renderer_notify_status_bar();
    }
}

void system_status_update_battery(const battery_snapshot& snapshot)
{
    if (ui_status_bar_update_battery(snapshot)) {
        ui_renderer_notify_status_bar();
    }
}
