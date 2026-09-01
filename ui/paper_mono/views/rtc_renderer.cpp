#include "view_renderer.hpp"

#include "layout.hpp"
#include "renderer_helpers.hpp"

namespace paper_mono_views {

namespace {

constexpr std::uint8_t empty_digit = 0xffU;

const char* rtc_message_text(const rtc_view_state& state)
{
    if (state.loading) {
        return "Loading RTC...";
    }
    switch (state.message) {
        case rtc_setting_message::none:
            return "";
        case rtc_setting_message::select_field:
            return "Select a field";
        case rtc_setting_message::incomplete:
            return "Incomplete date/time";
        case rtc_setting_message::invalid_date:
            return "Invalid date";
        case rtc_setting_message::invalid_time:
            return "Invalid time";
        case rtc_setting_message::rtc_unavailable:
            return "RTC unavailable";
        case rtc_setting_message::saving:
            return "Saving...";
        case rtc_setting_message::write_failed:
            return "RTC write failed";
    }
    return "";
}

void rtc_field_range(
    rtc_edit_field field,
    std::uint8_t& start,
    std::uint8_t& length)
{
    switch (field) {
        case rtc_edit_field::year:
            start = 0U;
            length = 4U;
            break;
        case rtc_edit_field::month:
            start = 4U;
            length = 2U;
            break;
        case rtc_edit_field::day:
            start = 6U;
            length = 2U;
            break;
        case rtc_edit_field::hour:
            start = 8U;
            length = 2U;
            break;
        case rtc_edit_field::minute:
            start = 10U;
            length = 2U;
            break;
        case rtc_edit_field::second:
            start = 12U;
            length = 2U;
            break;
        case rtc_edit_field::none:
            start = 0U;
            length = 0U;
            break;
    }
}

void draw_rtc_field(
    display_surface& surface,
    const rtc_view_state& state,
    rtc_edit_field field)
{
    std::uint8_t start = 0U;
    std::uint8_t length = 0U;
    rtc_field_range(field, start, length);
    char text[5] = {};
    for (std::uint8_t index = 0U; index < length; ++index) {
        text[index] = state.digits[start + index] == empty_digit
                          ? '-'
                          : static_cast<char>('0' + state.digits[start + index]);
    }
    const display_rect rect = rtc_field_rect(field);
    const bool selected = state.selected_field == field;
    surface.fill_rect(
        rect,
        selected ? display_color::black : display_color::white);
    surface.set_text_color(
        selected ? display_color::white : display_color::black,
        selected ? display_color::black : display_color::white);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(RTC_FIELD_TEXT_SIZE);
    surface.draw_text(text, rect.left + rect.width / 2, rect.top + rect.height / 2);
}

}  // namespace

bool rtc_keys_enabled(const rtc_view_state& state)
{
    return state.rtc_available && !state.loading && !state.saving;
}

void draw_rtc_editor(
    display_surface& surface,
    const rtc_view_state& state)
{
    surface.fill_rect(
        0,
        RTC_EDITOR_REGION_TOP,
        surface.width(),
        RTC_EDITOR_REGION_HEIGHT,
        display_color::white);
    constexpr rtc_edit_field fields[] = {
        rtc_edit_field::year, rtc_edit_field::month, rtc_edit_field::day,
        rtc_edit_field::hour, rtc_edit_field::minute, rtc_edit_field::second,
    };
    for (const rtc_edit_field field : fields) {
        draw_rtc_field(surface, state, field);
    }
    surface.set_text_color(display_color::black, display_color::white);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(RTC_FIELD_TEXT_SIZE);
    surface.draw_text(":", 231, RTC_DATE_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    surface.draw_text(":", 285, RTC_DATE_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    surface.draw_text(":", 213, RTC_TIME_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    surface.draw_text(":", 267, RTC_TIME_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    draw_centered_line(
        surface,
        rtc_message_text(state),
        RTC_MESSAGE_CENTER_Y,
        RTC_MESSAGE_TEXT_SIZE);
}

void draw_rtc_key(
    display_surface& surface,
    std::uint8_t index,
    bool pressed,
    bool enabled)
{
    if (index >= RTC_KEY_COUNT) {
        return;
    }
    const display_rect rect = rtc_key_rect(index);
    const bool active_pressed = pressed && enabled;
    const display_color background =
        active_pressed ? display_color::black : display_color::white;
    const display_color foreground =
        active_pressed ? display_color::white : display_color::black;
    draw_action_background(surface, rect, active_pressed, enabled);
    surface.draw_horizontal_line(rect.left, rect.top, rect.width, display_color::black);
    surface.draw_horizontal_line(
        rect.left,
        rect.top + rect.height - 1,
        rect.width,
        display_color::black);
    const std::int32_t center_x = rect.left + rect.width / 2;
    const std::int32_t center_y = rect.top + rect.height / 2;
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(RTC_KEY_TEXT_SIZE);
    if (index == 9U) {
        surface.draw_line(center_x - 18, center_y, center_x - 5, center_y + 13, foreground);
        surface.draw_line(center_x - 5, center_y + 13, center_x + 20, center_y - 16, foreground);
    } else if (index == 11U) {
        surface.draw_line(center_x - 22, center_y, center_x + 22, center_y, foreground);
        surface.draw_line(center_x - 22, center_y, center_x - 7, center_y - 14, foreground);
        surface.draw_line(center_x - 22, center_y, center_x - 7, center_y + 14, foreground);
    } else {
        constexpr const char* labels[] = {
            "7", "8", "9", "4", "5", "6", "1", "2", "3", "", "0", "",
        };
        surface.draw_text(labels[index], center_x, center_y);
    }
}

void draw_rtc_view(
    display_surface& surface,
    const rtc_view_state& state)
{
    draw_back_button(surface, ui_view_id::rtc_setting, false);
    draw_centered_line(surface, "RTC Setting", RTC_TITLE_CENTER_Y, RTC_TITLE_TEXT_SIZE);
    draw_rtc_editor(surface, state);
    for (std::uint8_t index = 0U; index < RTC_KEY_COUNT; ++index) {
        draw_rtc_key(surface, index, false, rtc_keys_enabled(state));
    }
}

}  // namespace paper_mono_views
