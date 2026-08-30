#pragma once

#include <cstdint>

#include "app_base.hpp"
#include "rtc_view.hpp"
#include "ui_renderer.hpp"

class rtc_setting_app final : public app_base {
public:
    rtc_setting_app();

    void handle_app_event(const app_event& event) override;

protected:
    void on_open() override;
    void on_close() override;

private:
    struct rtc_setting_state {
        std::uint8_t digits[14];
        rtc_edit_field selected_field = rtc_edit_field::none;
        rtc_setting_message message = rtc_setting_message::none;
        std::uint8_t input_offset = 0;
        std::uint32_t session_id = 0;
        bool dirty = false;
        bool loading = false;
        bool saving = false;
        bool rtc_available = false;
        bool field_input_started = false;
    };

    rtc_setting_state state_;

    void reset_session();
    rtc_view_state build_view() const;
    void submit_frame(ui_update_reason reason, std::int8_t released_key_index = -1);
    void handle_action(const ui_action_event& action);
    void handle_rtc_event(const rtc_service_event& event);
    void handle_key(std::uint8_t key_index);
    void input_digit(std::uint8_t digit, std::int8_t released_key_index);
    void clear_selected_field(std::int8_t released_key_index);
    void save_datetime(std::int8_t released_key_index);
    bool build_datetime(rtc_datetime& datetime, rtc_setting_message& error) const;
};
