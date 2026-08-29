#include "test_app.hpp"

#include <esp_log.h>

#include "app.hpp"
#include "hal.hpp"
#include "ui_config.hpp"

namespace {

constexpr char log_tag[] = "app_test";

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

}  // namespace

test_app::test_app()
    : back_button_({test_back_button_rect()})
{
    // setAppInfo is part of Mooncake's external API.
    setAppInfo().name = "TestApp";
}

void test_app::handle_app_event(const app_event& event)
{
    if (event.type != app_event_type::touch) {
        return;
    }

    const touch_event& touch = event.touch;
    const app_back_button_result back_result = back_button_.handle_touch(touch);
    if (back_result == app_back_button_result::clicked) {
        ESP_LOGI(log_tag, "back button selected");
        app_request_back();
        return;
    }
    if (back_result == app_back_button_result::handled || handle_front_light_event(touch)) {
        return;
    }

    switch (touch.type) {
        case touch_event_type::click:
            // Only a single click outside controls changes the primary UI text.
            text_state_ = ui_text_state::hello_world;
            ESP_LOGI(log_tag, "single click changed text_state=hello_world");
            submit_touch_frame(touch, touch_display_type::click);
            break;

        case touch_event_type::long_press_start:
        case touch_event_type::long_press_repeat:
        case touch_event_type::long_press_end:
            ESP_LOGI(
                log_tag,
                "long press frame type=%u duration=%lu ms",
                static_cast<unsigned>(touch.type),
                static_cast<unsigned long>(touch.duration_ms));
            submit_touch_frame(touch, touch_display_type::long_press);
            break;

        case touch_event_type::press:
            break;
    }
}

void test_app::on_open()
{
    front_light_button_active_ = false;
    back_button_.reset();
    submit_initial_frame();
    ESP_LOGI(log_tag, "TestApp opened");
}

void test_app::submit_touch_frame(
    const touch_event& event,
    touch_display_type touch_type)
{
    display_request request = {};
    request.view = display_view::test;
    request.mode = touch_type == touch_display_type::click
                       ? refresh_mode::fast
                       : refresh_mode::fastest;
    request.update_region = display_update_region::test_content;
    request.text_state = text_state_;
    request.touch_type = touch_type;
    request.start_x = event.start_x;
    request.start_y = event.start_y;
    request.end_x = event.end_x;
    request.end_y = event.end_y;
    request.duration_ms = event.duration_ms;
    request.timestamp_ms = event.timestamp_ms;
    request.minimum_refresh_interval_ms =
        touch_type == touch_display_type::long_press ? LONG_PRESS_REFRESH_INTERVAL_MS : 0U;
    request.allow_quality_cleanup = touch_type == touch_display_type::click;

    if (!hal_submit_display_request(request)) {
        ESP_LOGW(log_tag, "display request queue unavailable");
    }
}

void test_app::submit_initial_frame()
{
    display_request request = {};
    request.view = display_view::test;
    request.mode = refresh_mode::quality;
    request.update_region = display_update_region::full;
    request.text_state = text_state_;
    request.touch_type = touch_display_type::none;
    request.allow_quality_cleanup = true;
    if (!hal_submit_display_request(request)) {
        ESP_LOGW(log_tag, "initial display request queue unavailable");
    }
}

void test_app::submit_control_feedback(
    display_control_type type,
    std::uint8_t button_index,
    bool pressed,
    bool apply_level)
{
    display_control_request request = {};
    request.type = type;
    request.mode = refresh_mode::fastest;
    request.update_region = display_update_region::control;
    request.button_index = button_index;
    request.pressed = pressed;
    request.apply_level = apply_level;
    request.allow_quality_cleanup = !pressed;
    if (!hal_submit_display_control_request(request)) {
        ESP_LOGW(log_tag, "display control queue unavailable type=%u", static_cast<unsigned>(type));
    }
}

bool test_app::handle_front_light_event(const touch_event& event)
{
    if (event.type == touch_event_type::press) {
        std::uint8_t button_index = 0;
        if (!get_front_light_button_index(event.start_x, event.start_y, button_index)) {
            return false;
        }

        front_light_button_active_ = true;
        pressed_front_light_button_ = button_index;
        submit_control_feedback(display_control_type::front_light, button_index, true, false);
        ESP_LOGI(
            log_tag,
            "front light button pressed index=%u",
            static_cast<unsigned>(button_index));
        return true;
    }

    if (!front_light_button_active_) {
        return false;
    }

    if (event.type == touch_event_type::click) {
        std::uint8_t release_button_index = 0;
        const bool released_on_same_button =
            get_front_light_button_index(event.end_x, event.end_y, release_button_index) &&
            release_button_index == pressed_front_light_button_;
        if (released_on_same_button) {
            front_light_level_index_ = pressed_front_light_button_;
        }

        submit_control_feedback(
            display_control_type::front_light,
            pressed_front_light_button_,
            false,
            released_on_same_button);
        front_light_button_active_ = false;
        ESP_LOGI(
            log_tag,
            "front light button released index=%u applied=%u",
            static_cast<unsigned>(pressed_front_light_button_),
            released_on_same_button ? 1U : 0U);
        return true;
    }

    if (event.type == touch_event_type::long_press_end) {
        // A long press restores the button but never changes the light level.
        submit_control_feedback(
            display_control_type::front_light,
            pressed_front_light_button_,
            false,
            false);
        front_light_button_active_ = false;
    }
    return true;
}
