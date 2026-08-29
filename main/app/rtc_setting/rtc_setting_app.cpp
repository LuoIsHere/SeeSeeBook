#include "rtc_setting_app.hpp"

#include <algorithm>
#include <iterator>

#include <esp_log.h>

#include "app.hpp"
#include "hal.hpp"
#include "ui_config.hpp"

namespace {

constexpr char log_tag[] = "app_rtc_setting";
constexpr std::uint8_t empty_digit = 0xffU;
constexpr std::int8_t key_action_save = -2;
constexpr std::int8_t key_action_backspace = -1;
constexpr std::int8_t key_values[RTC_KEY_COUNT] = {
    7,
    8,
    9,
    4,
    5,
    6,
    1,
    2,
    3,
    key_action_save,
    0,
    key_action_backspace,
};

std::uint32_t next_session_id = 0;

struct field_range {
    std::uint8_t start;
    std::uint8_t length;
};

field_range range_for_field(rtc_edit_field field)
{
    switch (field) {
        case rtc_edit_field::year:
            return {0U, 4U};
        case rtc_edit_field::month:
            return {4U, 2U};
        case rtc_edit_field::day:
            return {6U, 2U};
        case rtc_edit_field::hour:
            return {8U, 2U};
        case rtc_edit_field::minute:
            return {10U, 2U};
        case rtc_edit_field::second:
            return {12U, 2U};
        case rtc_edit_field::none:
            return {0U, 0U};
    }
    return {0U, 0U};
}

rtc_edit_field next_field(rtc_edit_field field)
{
    switch (field) {
        case rtc_edit_field::year:
            return rtc_edit_field::month;
        case rtc_edit_field::month:
            return rtc_edit_field::day;
        case rtc_edit_field::day:
            return rtc_edit_field::hour;
        case rtc_edit_field::hour:
            return rtc_edit_field::minute;
        case rtc_edit_field::minute:
            return rtc_edit_field::second;
        case rtc_edit_field::second:
        case rtc_edit_field::none:
            return rtc_edit_field::none;
    }
    return rtc_edit_field::none;
}

bool is_leap_year(std::uint16_t year)
{
    return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month)
{
    constexpr std::uint8_t month_days[] = {
        31U,
        28U,
        31U,
        30U,
        31U,
        30U,
        31U,
        31U,
        30U,
        31U,
        30U,
        31U,
    };
    if (month < 1U || month > 12U) {
        return 0U;
    }
    return month == 2U && is_leap_year(year) ? 29U : month_days[month - 1U];
}

std::uint16_t digits_to_number(
    const std::uint8_t* digits,
    std::uint8_t length)
{
    std::uint16_t value = 0;
    for (std::uint8_t index = 0; index < length; ++index) {
        value = static_cast<std::uint16_t>(value * 10U + digits[index]);
    }
    return value;
}

bool get_key_index(std::int16_t x, std::int16_t y, std::uint8_t& key_index)
{
    for (std::uint8_t index = 0; index < RTC_KEY_COUNT; ++index) {
        if (ui_point_in_rect(x, y, rtc_key_rect(index))) {
            key_index = index;
            return true;
        }
    }
    return false;
}

bool get_field(std::int16_t x, std::int16_t y, rtc_edit_field& field)
{
    constexpr rtc_edit_field fields[] = {
        rtc_edit_field::year,
        rtc_edit_field::month,
        rtc_edit_field::day,
        rtc_edit_field::hour,
        rtc_edit_field::minute,
        rtc_edit_field::second,
    };
    for (const rtc_edit_field candidate : fields) {
        if (ui_point_in_rect(x, y, rtc_field_rect(candidate))) {
            field = candidate;
            return true;
        }
    }
    return false;
}

void datetime_to_digits(const rtc_datetime& datetime, std::uint8_t* digits)
{
    digits[0] = static_cast<std::uint8_t>(datetime.date.year / 1000U % 10U);
    digits[1] = static_cast<std::uint8_t>(datetime.date.year / 100U % 10U);
    digits[2] = static_cast<std::uint8_t>(datetime.date.year / 10U % 10U);
    digits[3] = static_cast<std::uint8_t>(datetime.date.year % 10U);
    digits[4] = static_cast<std::uint8_t>(datetime.date.month / 10U);
    digits[5] = static_cast<std::uint8_t>(datetime.date.month % 10U);
    digits[6] = static_cast<std::uint8_t>(datetime.date.day / 10U);
    digits[7] = static_cast<std::uint8_t>(datetime.date.day % 10U);
    digits[8] = static_cast<std::uint8_t>(datetime.time.hour / 10U);
    digits[9] = static_cast<std::uint8_t>(datetime.time.hour % 10U);
    digits[10] = static_cast<std::uint8_t>(datetime.time.minute / 10U);
    digits[11] = static_cast<std::uint8_t>(datetime.time.minute % 10U);
    digits[12] = static_cast<std::uint8_t>(datetime.time.second / 10U);
    digits[13] = static_cast<std::uint8_t>(datetime.time.second % 10U);
}

bool rtc_view_states_equal(
    const rtc_setting_view_state& left,
    const rtc_setting_view_state& right)
{
    return std::equal(std::begin(left.digits), std::end(left.digits), std::begin(right.digits)) &&
           left.selected_field == right.selected_field &&
           left.message == right.message &&
           left.loading == right.loading &&
           left.saving == right.saving &&
           left.rtc_available == right.rtc_available;
}

}  // namespace

rtc_setting_app::rtc_setting_app()
{
    // setAppInfo is part of Mooncake's external API.
    setAppInfo().name = "RTCSettingApp";
    reset_session();
}

void rtc_setting_app::handle_app_event(const app_event& event)
{
    if (event.type == app_event_type::touch) {
        handle_touch_event(event.touch);
    } else if (event.type == app_event_type::rtc) {
        handle_rtc_event(event.rtc);
    }
}

void rtc_setting_app::on_open()
{
    reset_session();
    ++next_session_id;
    if (next_session_id == 0U) {
        ++next_session_id;
    }
    state_.session_id = next_session_id;
    state_.loading = true;
    submit_frame(true, display_update_region::full);
    if (!hal_submit_rtc_read(state_.session_id)) {
        state_.loading = false;
        state_.message = rtc_setting_message::rtc_unavailable;
        submit_frame(false, display_update_region::rtc_editor);
    }
    ESP_LOGI(log_tag, "RTCSettingApp opened session_id=%lu", static_cast<unsigned long>(state_.session_id));
}

void rtc_setting_app::on_close()
{
    ESP_LOGI(log_tag, "RTCSettingApp closed session_id=%lu", static_cast<unsigned long>(state_.session_id));
    reset_session();
}

void rtc_setting_app::reset_session()
{
    std::fill(std::begin(state_.digits), std::end(state_.digits), empty_digit);
    state_.selected_field = rtc_edit_field::none;
    state_.message = rtc_setting_message::none;
    state_.input_offset = 0U;
    state_.pressed_key = -1;
    state_.session_id = 0U;
    state_.dirty = false;
    state_.loading = false;
    state_.saving = false;
    state_.rtc_available = false;
    state_.field_input_started = false;
    captured_control_ = captured_control::none;
    captured_index_ = -1;
    captured_field_ = rtc_edit_field::none;
    last_submitted_view_ = {};
    has_last_submitted_view_ = false;
}

void rtc_setting_app::submit_frame(
    bool force_quality,
    display_update_region update_region)
{
    display_request request = {};
    request.view = display_view::rtc_setting;
    request.update_region = update_region;
    request.force_quality = force_quality;
    std::copy(std::begin(state_.digits), std::end(state_.digits), request.rtc_setting.digits);
    request.rtc_setting.selected_field = state_.selected_field;
    request.rtc_setting.message = state_.message;
    request.rtc_setting.loading = state_.loading;
    request.rtc_setting.saving = state_.saving;
    request.rtc_setting.rtc_available = state_.rtc_available;

    // A forced frame marks a lifecycle transition and must not be deduplicated.
    if (!force_quality && has_last_submitted_view_ &&
        rtc_view_states_equal(request.rtc_setting, last_submitted_view_)) {
        return;
    }

    if (hal_submit_display_request(request)) {
        last_submitted_view_ = request.rtc_setting;
        has_last_submitted_view_ = true;
    } else {
        ESP_LOGW(log_tag, "display request queue unavailable");
    }
}

void rtc_setting_app::submit_control_feedback(
    display_control_type type,
    std::uint8_t button_index,
    bool pressed)
{
    display_control_request request = {};
    request.type = type;
    request.button_index = button_index;
    request.pressed = pressed;
    if (!hal_submit_display_control_request(request)) {
        ESP_LOGW(log_tag, "control feedback queue unavailable type=%u", static_cast<unsigned>(type));
    }
}

void rtc_setting_app::handle_touch_event(const touch_event& event)
{
    if (event.type == touch_event_type::press) {
        if (state_.saving) {
            return;
        }
        if (ui_point_in_rect(event.start_x, event.start_y, rtc_back_button_rect())) {
            captured_control_ = captured_control::back_button;
            submit_control_feedback(display_control_type::rtc_back_button, 0U, true);
            return;
        }
        if (state_.loading || !state_.rtc_available) {
            return;
        }

        std::uint8_t key_index = 0;
        if (get_key_index(event.start_x, event.start_y, key_index)) {
            captured_control_ = captured_control::keypad;
            captured_index_ = static_cast<std::int8_t>(key_index);
            state_.pressed_key = captured_index_;
            submit_control_feedback(display_control_type::rtc_key, key_index, true);
            return;
        }

        rtc_edit_field field = rtc_edit_field::none;
        if (get_field(event.start_x, event.start_y, field)) {
            captured_control_ = captured_control::field;
            captured_field_ = field;
        }
        return;
    }

    if (captured_control_ == captured_control::none) {
        return;
    }

    if (event.type == touch_event_type::click) {
        if (captured_control_ == captured_control::back_button) {
            submit_control_feedback(display_control_type::rtc_back_button, 0U, false);
            const bool released_inside =
                ui_point_in_rect(event.end_x, event.end_y, rtc_back_button_rect());
            captured_control_ = captured_control::none;
            if (released_inside) {
                app_request_back();
            }
            return;
        }

        if (captured_control_ == captured_control::keypad) {
            const std::uint8_t captured_key = static_cast<std::uint8_t>(captured_index_);
            submit_control_feedback(display_control_type::rtc_key, captured_key, false);
            std::uint8_t released_key = 0;
            const bool released_inside =
                get_key_index(event.end_x, event.end_y, released_key) &&
                released_key == captured_key;
            captured_control_ = captured_control::none;
            captured_index_ = -1;
            state_.pressed_key = -1;
            if (released_inside) {
                handle_key(captured_key);
            }
            return;
        }

        if (captured_control_ == captured_control::field) {
            rtc_edit_field released_field = rtc_edit_field::none;
            const bool released_inside =
                get_field(event.end_x, event.end_y, released_field) &&
                released_field == captured_field_;
            captured_control_ = captured_control::none;
            if (released_inside) {
                state_.selected_field = released_field;
                state_.input_offset = 0U;
                state_.field_input_started = false;
                state_.message = rtc_setting_message::none;
                submit_frame(false, display_update_region::rtc_editor);
            }
            return;
        }
    }

    if (event.type == touch_event_type::long_press_end) {
        if (captured_control_ == captured_control::back_button) {
            submit_control_feedback(display_control_type::rtc_back_button, 0U, false);
        } else if (captured_control_ == captured_control::keypad) {
            submit_control_feedback(
                display_control_type::rtc_key,
                static_cast<std::uint8_t>(captured_index_),
                false);
        }
        captured_control_ = captured_control::none;
        captured_index_ = -1;
        captured_field_ = rtc_edit_field::none;
        state_.pressed_key = -1;
    }
}

void rtc_setting_app::handle_rtc_event(const rtc_event& event)
{
    if (state_.session_id == 0U || event.request_id != state_.session_id) {
        return;
    }

    if (event.operation == rtc_operation::read) {
        state_.loading = false;
        state_.rtc_available = event.success;
        if (event.success) {
            datetime_to_digits(event.datetime, state_.digits);
            state_.message = rtc_setting_message::none;
        } else {
            state_.message = rtc_setting_message::rtc_unavailable;
        }
        submit_frame(false, display_update_region::rtc_editor);
        return;
    }

    if (!state_.saving) {
        return;
    }
    if (event.success) {
        app_request_back();
        return;
    }

    state_.saving = false;
    state_.message = rtc_setting_message::write_failed;
    submit_frame(false, display_update_region::rtc_editor);
}

void rtc_setting_app::handle_key(std::uint8_t key_index)
{
    if (key_index >= RTC_KEY_COUNT) {
        return;
    }
    const std::int8_t value = key_values[key_index];
    if (value == key_action_save) {
        save_datetime();
    } else if (value == key_action_backspace) {
        clear_selected_field();
    } else {
        input_digit(static_cast<std::uint8_t>(value));
    }
}

void rtc_setting_app::input_digit(std::uint8_t digit)
{
    const field_range range = range_for_field(state_.selected_field);
    if (range.length == 0U) {
        state_.message = rtc_setting_message::select_field;
        submit_frame(false, display_update_region::rtc_editor);
        return;
    }

    if (!state_.field_input_started) {
        std::fill_n(&state_.digits[range.start], range.length, empty_digit);
        state_.input_offset = 0U;
        state_.field_input_started = true;
    }
    state_.digits[range.start + state_.input_offset] = digit;
    ++state_.input_offset;
    state_.dirty = true;
    state_.message = rtc_setting_message::none;

    if (state_.input_offset >= range.length) {
        state_.selected_field = next_field(state_.selected_field);
        state_.input_offset = 0U;
        state_.field_input_started = false;
    }
    submit_frame(false, display_update_region::rtc_editor);
}

void rtc_setting_app::clear_selected_field()
{
    const field_range range = range_for_field(state_.selected_field);
    if (range.length == 0U) {
        state_.message = rtc_setting_message::select_field;
        submit_frame(false, display_update_region::rtc_editor);
        return;
    }

    std::fill_n(&state_.digits[range.start], range.length, empty_digit);
    state_.input_offset = 0U;
    state_.field_input_started = true;
    state_.dirty = true;
    state_.message = rtc_setting_message::none;
    submit_frame(false, display_update_region::rtc_editor);
}

void rtc_setting_app::save_datetime()
{
    rtc_datetime datetime = {};
    rtc_setting_message error = rtc_setting_message::none;
    if (!build_datetime(datetime, error)) {
        state_.message = error;
        submit_frame(false, display_update_region::rtc_editor);
        return;
    }

    state_.saving = true;
    state_.message = rtc_setting_message::saving;
    submit_frame(false, display_update_region::rtc_editor);
    if (!hal_submit_rtc_write(datetime, state_.session_id)) {
        state_.saving = false;
        state_.message = rtc_setting_message::write_failed;
        submit_frame(false, display_update_region::rtc_editor);
    }
}

bool rtc_setting_app::build_datetime(
    rtc_datetime& datetime,
    rtc_setting_message& error) const
{
    for (const std::uint8_t digit : state_.digits) {
        if (digit == empty_digit) {
            error = rtc_setting_message::incomplete;
            return false;
        }
    }

    datetime.date.year = digits_to_number(&state_.digits[0], 4U);
    datetime.date.month = static_cast<std::uint8_t>(digits_to_number(&state_.digits[4], 2U));
    datetime.date.day = static_cast<std::uint8_t>(digits_to_number(&state_.digits[6], 2U));
    datetime.time.hour = static_cast<std::uint8_t>(digits_to_number(&state_.digits[8], 2U));
    datetime.time.minute = static_cast<std::uint8_t>(digits_to_number(&state_.digits[10], 2U));
    datetime.time.second = static_cast<std::uint8_t>(digits_to_number(&state_.digits[12], 2U));

    if (datetime.date.year < 2000U || datetime.date.year > 2099U ||
        datetime.date.month < 1U || datetime.date.month > 12U ||
        datetime.date.day < 1U ||
        datetime.date.day > days_in_month(datetime.date.year, datetime.date.month)) {
        error = rtc_setting_message::invalid_date;
        return false;
    }
    if (datetime.time.hour > 23U || datetime.time.minute > 59U ||
        datetime.time.second > 59U) {
        error = rtc_setting_message::invalid_time;
        return false;
    }
    error = rtc_setting_message::none;
    return true;
}
