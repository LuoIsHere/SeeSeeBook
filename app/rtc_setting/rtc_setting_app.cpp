#include "rtc_setting_app.hpp"

#include <algorithm>
#include <iterator>

#include <esp_log.h>

#include "app.hpp"
#include "rtc_service.hpp"

namespace {

constexpr char log_tag[] = "app_rtc_setting";
constexpr std::uint8_t empty_digit = 0xffU;
constexpr std::int8_t key_action_save = -2;
constexpr std::int8_t key_action_backspace = -1;
constexpr std::int8_t key_values[] = {
    7, 8, 9, 4, 5, 6, 1, 2, 3, key_action_save, 0, key_action_backspace,
};
std::uint32_t next_session_id = 0U;

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

bool leap_year(std::uint16_t year)
{
    return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month)
{
    constexpr std::uint8_t days[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                     31U, 31U, 30U, 31U, 30U, 31U};
    if (month < 1U || month > 12U) {
        return 0U;
    }
    return month == 2U && leap_year(year) ? 29U : days[month - 1U];
}

std::uint16_t digits_to_number(const std::uint8_t* digits, std::uint8_t length)
{
    std::uint16_t value = 0U;
    for (std::uint8_t index = 0U; index < length; ++index) {
        value = static_cast<std::uint16_t>(value * 10U + digits[index]);
    }
    return value;
}

void datetime_to_digits(const rtc_datetime& value, std::uint8_t* digits)
{
    digits[0] = value.date.year / 1000U % 10U;
    digits[1] = value.date.year / 100U % 10U;
    digits[2] = value.date.year / 10U % 10U;
    digits[3] = value.date.year % 10U;
    digits[4] = value.date.month / 10U;
    digits[5] = value.date.month % 10U;
    digits[6] = value.date.day / 10U;
    digits[7] = value.date.day % 10U;
    digits[8] = value.time.hour / 10U;
    digits[9] = value.time.hour % 10U;
    digits[10] = value.time.minute / 10U;
    digits[11] = value.time.minute % 10U;
    digits[12] = value.time.second / 10U;
    digits[13] = value.time.second % 10U;
}

}  // namespace

void rtc_setting_app::handle_app_event(const app_event& event)
{
    if (event.type == app_event_type::ui_action) {
        handle_action(event.action);
    } else if (event.type == app_event_type::rtc) {
        handle_rtc_event(event.rtc);
    }
}

void rtc_setting_app::on_open()
{
    reset_session();
    state_.session_id = ++next_session_id;
    state_.loading = true;
    if (!rtc_service_submit_read(state_.session_id)) {
        state_.loading = false;
        state_.message = rtc_setting_message::rtc_unavailable;
    }
    submit_frame(ui_update_reason::view_opened);
    ESP_LOGI(log_tag, "RTCSettingApp opened session=%lu",
             static_cast<unsigned long>(state_.session_id));
}

void rtc_setting_app::on_close()
{
    reset_session();
    ESP_LOGI(log_tag, "RTCSettingApp session cleared");
}

void rtc_setting_app::reset_session()
{
    state_ = {};
    std::fill(std::begin(state_.digits), std::end(state_.digits), empty_digit);
    state_.selected_field = rtc_edit_field::none;
}

rtc_view_state rtc_setting_app::build_view() const
{
    rtc_view_state view = {};
    std::copy(std::begin(state_.digits), std::end(state_.digits), std::begin(view.digits));
    view.selected_field = state_.selected_field;
    view.message = state_.message;
    view.loading = state_.loading;
    view.saving = state_.saving;
    view.rtc_available = state_.rtc_available;
    return view;
}

void rtc_setting_app::submit_frame(ui_update_reason reason, std::int8_t released_key_index)
{
    ui_render_rtc(
        build_view(), reason,
        released_key_index >= 0 ? ui_control_type::rtc_key : ui_control_type::none,
        released_key_index >= 0 ? static_cast<std::uint8_t>(released_key_index) : 0U);
}

void rtc_setting_app::handle_action(const ui_action_event& action)
{
    if (action.input.gesture != input_gesture_type::click) {
        return;
    }
    if (action.control == ui_control_type::navigate_back) {
        app_request_back();
        return;
    }
    if (action.control == ui_control_type::rtc_field) {
        const rtc_edit_field field = static_cast<rtc_edit_field>(action.index);
        if (field >= rtc_edit_field::year && field <= rtc_edit_field::second) {
            state_.selected_field = field;
            state_.input_offset = 0U;
            state_.field_input_started = false;
            state_.message = state_.rtc_available ? rtc_setting_message::none
                                                  : rtc_setting_message::rtc_unavailable;
            submit_frame(ui_update_reason::selection_changed);
        }
        return;
    }
    if (action.control != ui_control_type::rtc_key) {
        return;
    }
    if (!state_.rtc_available || state_.loading || state_.saving) {
        submit_frame(ui_update_reason::content_changed,
                     static_cast<std::int8_t>(action.index));
        return;
    }
    handle_key(action.index);
}

void rtc_setting_app::handle_rtc_event(const app_rtc_event& event)
{
    if (event.request_id != state_.session_id) {
        return;
    }
    if (event.operation == app_rtc_operation::write) {
        state_.saving = false;
        if (event.success) {
            app_request_back();
        } else {
            state_.message = rtc_setting_message::write_failed;
            submit_frame(ui_update_reason::content_changed);
        }
        return;
    }
    state_.loading = false;
    state_.rtc_available = event.success;
    if (event.success) {
        datetime_to_digits(event.datetime, state_.digits);
        state_.message = rtc_setting_message::none;
    } else {
        state_.message = rtc_setting_message::rtc_unavailable;
    }
    submit_frame(ui_update_reason::content_changed);
}

void rtc_setting_app::handle_key(std::uint8_t key_index)
{
    if (key_index >= std::size(key_values)) {
        return;
    }
    const std::int8_t value = key_values[key_index];
    if (value == key_action_save) {
        save_datetime(static_cast<std::int8_t>(key_index));
    } else if (value == key_action_backspace) {
        clear_selected_field(static_cast<std::int8_t>(key_index));
    } else {
        input_digit(static_cast<std::uint8_t>(value), static_cast<std::int8_t>(key_index));
    }
}

void rtc_setting_app::input_digit(std::uint8_t digit, std::int8_t released_key_index)
{
    const field_range range = range_for_field(state_.selected_field);
    if (range.length == 0U) {
        state_.message = rtc_setting_message::select_field;
        submit_frame(ui_update_reason::content_changed, released_key_index);
        return;
    }
    if (!state_.field_input_started) {
        std::fill_n(&state_.digits[range.start], range.length, empty_digit);
        state_.input_offset = 0U;
        state_.field_input_started = true;
    }
    state_.digits[range.start + state_.input_offset++] = digit;
    state_.dirty = true;
    state_.message = rtc_setting_message::none;
    if (state_.input_offset >= range.length) {
        state_.selected_field = next_field(state_.selected_field);
        state_.input_offset = 0U;
        state_.field_input_started = false;
    }
    submit_frame(ui_update_reason::content_changed, released_key_index);
}

void rtc_setting_app::clear_selected_field(std::int8_t released_key_index)
{
    const field_range range = range_for_field(state_.selected_field);
    if (range.length == 0U) {
        state_.message = rtc_setting_message::select_field;
    } else {
        std::fill_n(&state_.digits[range.start], range.length, empty_digit);
        state_.input_offset = 0U;
        state_.field_input_started = true;
        state_.dirty = true;
        state_.message = rtc_setting_message::none;
    }
    submit_frame(ui_update_reason::content_changed, released_key_index);
}

void rtc_setting_app::save_datetime(std::int8_t released_key_index)
{
    rtc_datetime datetime = {};
    rtc_setting_message error = rtc_setting_message::none;
    if (!build_datetime(datetime, error)) {
        state_.message = error;
        submit_frame(ui_update_reason::content_changed, released_key_index);
        return;
    }
    state_.saving = true;
    state_.message = rtc_setting_message::saving;
    if (!rtc_service_submit_write(datetime, state_.session_id)) {
        state_.saving = false;
        state_.message = rtc_setting_message::write_failed;
    }
    submit_frame(ui_update_reason::content_changed, released_key_index);
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
    datetime.date.month = digits_to_number(&state_.digits[4], 2U);
    datetime.date.day = digits_to_number(&state_.digits[6], 2U);
    datetime.time.hour = digits_to_number(&state_.digits[8], 2U);
    datetime.time.minute = digits_to_number(&state_.digits[10], 2U);
    datetime.time.second = digits_to_number(&state_.digits[12], 2U);
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
