#include "system_overlay.hpp"

#include "hal_rtc.hpp"

system_overlay_state system_overlay_get_state()
{
    system_overlay_state state = {};
    rtc_datetime datetime = {};
    state.time_valid = hal_get_cached_datetime(datetime);
    if (state.time_valid) {
        state.hour = datetime.time.hour;
        state.minute = datetime.time.minute;
    }
    return state;
}
