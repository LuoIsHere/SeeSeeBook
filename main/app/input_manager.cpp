#include "input_manager.hpp"

#include "app.hpp"
#include "app_base.hpp"
#include "hal.hpp"
#include "system_config.hpp"

namespace {

app_base* target_app = nullptr;

}  // namespace

void input_manager_set_target(app_base* new_target_app)
{
    target_app = new_target_app;
}

void input_manager_update()
{
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
        if (!event_dispatched) {
            return;
        }
    }
}
