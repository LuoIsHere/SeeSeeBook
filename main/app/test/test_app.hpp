#pragma once

#include <cstdint>

#include "app_base.hpp"
#include "hal_display.hpp"

class test_app final : public app_base {
public:
    test_app();

    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;

private:
    ui_text_state text_state_ = ui_text_state::hi_xi;
    std::uint8_t front_light_level_index_ = FRONT_LIGHT_DEFAULT_LEVEL_INDEX;
    std::uint8_t pressed_front_light_button_ = 0;
    bool front_light_button_active_ = false;
    bool back_button_active_ = false;

    void submit_touch_frame(const touch_event& event, touch_display_type touch_type);
    void submit_initial_frame();
    void submit_control_feedback(
        display_control_type type,
        std::uint8_t button_index,
        bool pressed,
        bool apply_level);
    bool handle_back_button_event(const touch_event& event);
    bool handle_front_light_event(const touch_event& event);
};
