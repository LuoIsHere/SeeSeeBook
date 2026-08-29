#pragma once

#include <cstdint>

#include "app_base.hpp"
#include "hal_display.hpp"
#include "hal_rtc.hpp"

class rtc_setting_app final : public app_base {
public:
    rtc_setting_app();

    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_close() override;

private:
    enum class captured_control : std::uint8_t {
        none,
        back_button,
        keypad,
        field,
    };

    struct rtc_setting_state {
        std::uint8_t digits[14];
        rtc_edit_field selected_field = rtc_edit_field::none;
        rtc_setting_message message = rtc_setting_message::none;
        std::uint8_t input_offset = 0;
        std::int8_t pressed_key = -1;
        std::uint32_t session_id = 0;
        bool dirty = false;
        bool loading = false;
        bool saving = false;
        bool rtc_available = false;
        bool field_input_started = false;
    };

    rtc_setting_state state_;
    rtc_setting_view_state last_submitted_view_ = {};
    captured_control captured_control_ = captured_control::none;
    std::int8_t captured_index_ = -1;
    rtc_edit_field captured_field_ = rtc_edit_field::none;
    bool has_last_submitted_view_ = false;

    void reset_session();
    void submit_frame(bool force_quality, display_update_region update_region);
    void submit_control_feedback(
        display_control_type type,
        std::uint8_t button_index,
        bool pressed);
    void handle_touch_event(const touch_event& event);
    void handle_rtc_event(const rtc_event& event);
    void handle_key(std::uint8_t key_index);
    void input_digit(std::uint8_t digit);
    void clear_selected_field();
    void save_datetime();
    bool build_datetime(rtc_datetime& datetime, rtc_setting_message& error) const;
};
