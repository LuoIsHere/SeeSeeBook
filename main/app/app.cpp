#include "app.h"

#include <esp_log.h>

#include "hal.h"
#include "types.hpp"

namespace {

constexpr char log_tag[] = "app";

struct app_state {
    ui_text_state text_state = ui_text_state::hi_xi;
    std::uint8_t front_light_level_index = FRONT_LIGHT_DEFAULT_LEVEL_INDEX;
    std::uint8_t pressed_front_light_button = 0;
    bool front_light_button_active = false;
};

app_state state;

void submit_touch_frame(const touch_event& event, touch_display_type touch_type)
{
    display_request request = {};
    request.text_state = state.text_state;
    request.touch_type = touch_type;
    request.start_x = event.start_x;
    request.start_y = event.start_y;
    request.end_x = event.end_x;
    request.end_y = event.end_y;
    request.duration_ms = event.duration_ms;
    request.timestamp_ms = event.timestamp_ms;
    request.minimum_refresh_interval_ms =
        touch_type == touch_display_type::long_press ? LONG_PRESS_REFRESH_INTERVAL_MS : 0;

    if (!hal_submit_display_request(request)) {
        ESP_LOGW(log_tag, "display request queue unavailable");
    }
}

bool get_front_light_button_index(
    std::int16_t x,
    std::int16_t y,
    std::uint8_t& button_index)
{
    if (x < 0 || y < 0 ||
        x >= static_cast<std::int16_t>(PAPER_MONO_PORTRAIT_WIDTH) ||
        y >= static_cast<std::int16_t>(FRONT_LIGHT_BAR_HEIGHT)) {
        return false;
    }

    button_index = static_cast<std::uint8_t>(
        static_cast<std::uint32_t>(x) * FRONT_LIGHT_LEVEL_COUNT /
        PAPER_MONO_PORTRAIT_WIDTH);
    return button_index < FRONT_LIGHT_LEVEL_COUNT;
}

void submit_front_light_button(
    std::uint8_t button_index,
    bool pressed,
    bool apply_level)
{
    front_light_request request = {};
    request.button_index = button_index;
    request.pressed = pressed;
    request.apply_level = apply_level;

    if (!hal_submit_front_light_request(request)) {
        ESP_LOGW(log_tag, "front light request queue unavailable");
    }
}

bool handle_front_light_event(const touch_event& event)
{
    if (event.type == touch_event_type::press) {
        std::uint8_t button_index = 0;
        if (!get_front_light_button_index(event.start_x, event.start_y, button_index)) {
            return false;
        }

        state.front_light_button_active = true;
        state.pressed_front_light_button = button_index;
        submit_front_light_button(button_index, true, false);
        ESP_LOGI(
            log_tag,
            "front light button pressed index=%u",
            static_cast<unsigned>(button_index));
        return true;
    }

    if (!state.front_light_button_active) {
        return false;
    }

    if (event.type == touch_event_type::click) {
        std::uint8_t release_button_index = 0;
        const bool release_on_same_button =
            get_front_light_button_index(event.end_x, event.end_y, release_button_index) &&
            release_button_index == state.pressed_front_light_button;

        if (release_on_same_button) {
            state.front_light_level_index = state.pressed_front_light_button;
        }
        submit_front_light_button(
            state.pressed_front_light_button,
            false,
            release_on_same_button);
        ESP_LOGI(
            log_tag,
            "front light button released index=%u applied=%u",
            static_cast<unsigned>(state.pressed_front_light_button),
            release_on_same_button ? 1U : 0U);
        state.front_light_button_active = false;
        return true;
    }

    if (event.type == touch_event_type::long_press_end) {
        // A long press restores the button but never changes the light level.
        submit_front_light_button(state.pressed_front_light_button, false, false);
        ESP_LOGI(
            log_tag,
            "front light long press released index=%u without applying",
            static_cast<unsigned>(state.pressed_front_light_button));
        state.front_light_button_active = false;
    }

    // Consume every long-press phase captured by a front-light button.
    return true;
}

}  // namespace

void app_init()
{
    display_request initial_request = {};
    initial_request.text_state = state.text_state;
    initial_request.touch_type = touch_display_type::none;
    initial_request.force_quality = true;
    hal_submit_display_request(initial_request);
}

void app_loop()
{
    touch_event event = {};
    if (!hal_wait_touch_event(event)) {
        return;
    }

    if (handle_front_light_event(event)) {
        return;
    }

    switch (event.type) {
        case touch_event_type::click:
            // Only a single click is allowed to change the primary UI text.
            state.text_state = ui_text_state::hello_world;
            ESP_LOGI(log_tag, "single click changed text_state=hello_world");
            submit_touch_frame(event, touch_display_type::click);
            break;

        case touch_event_type::long_press_start:
        case touch_event_type::long_press_repeat:
        case touch_event_type::long_press_end:
            ESP_LOGI(
                log_tag,
                "long press frame type=%u duration=%lu ms",
                static_cast<unsigned>(event.type),
                static_cast<unsigned long>(event.duration_ms));
            submit_touch_frame(event, touch_display_type::long_press);
            break;

        case touch_event_type::press:
            break;
    }
}
