#include "ui_renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "display.hpp"
#include "layout.hpp"
#include "project_info.hpp"
#include "renderer_internal.hpp"
#include "status_bar.hpp"
#include "ui_presentation.hpp"

namespace {

constexpr char log_tag[] = "ui_renderer";
constexpr std::int16_t no_pressed_button = -1;
constexpr std::uint8_t empty_digit = 0xffU;
constexpr const char* front_light_labels[FRONT_LIGHT_LEVEL_COUNT] = {
    "OFF", "25%", "50%", "75%", "100%",
};
constexpr const char* menu_entry_labels[MENU_ENTRY_COUNT] = {
    "Screen Setting", "RTC Setting", "Battery", "Files",
};

QueueHandle_t request_queue = nullptr;
QueueHandle_t control_queue = nullptr;
TaskHandle_t renderer_task_handle = nullptr;

struct region_ghost_debt {
    std::uint8_t fastest_count = 0U;
    std::uint8_t fast_count = 0U;
    std::uint8_t text_count = 0U;
};

struct ghost_debt {
    region_ghost_debt control;
    region_ghost_debt rtc_editor;
    std::uint16_t status_bar = 0U;
    region_ghost_debt test_content;
    region_ghost_debt battery_content;
    region_ghost_debt file_content;
    bool status_cleanup_pending = false;
};

display_surface& canvas()
{
    return hal_display_surface();
}

std::uint32_t monotonic_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

const char* refresh_mode_name(refresh_mode mode)
{
    switch (mode) {
        case refresh_mode::fastest:
            return "fastest";
        case refresh_mode::text:
            return "text";
        case refresh_mode::fast:
            return "fast";
        case refresh_mode::quality:
            return "quality";
    }
    return "unknown";
}

region_ghost_debt& debt_for_region(ghost_debt& debt, display_update_region region)
{
    switch (region) {
        case display_update_region::control:
            return debt.control;
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            return debt.rtc_editor;
        case display_update_region::status_bar:
            return debt.control;
        case display_update_region::test_content:
            return debt.test_content;
        case display_update_region::battery_content:
            return debt.battery_content;
        case display_update_region::file_content:
            return debt.file_content;
        case display_update_region::full:
            return debt.control;
    }
    return debt.control;
}

refresh_mode resolve_mode(
    refresh_mode requested,
    display_update_region region,
    bool allow_cleanup,
    ghost_debt& debt)
{
    if (requested == refresh_mode::quality || region == display_update_region::status_bar ||
        !allow_cleanup) {
        return requested;
    }
    const region_ghost_debt& value = debt_for_region(debt, region);
    if (requested == refresh_mode::fastest &&
        value.fastest_count + 1U >= FASTEST_REFRESHES_BEFORE_FAST) {
        return value.fast_count + 1U >= FAST_REFRESHES_BEFORE_QUALITY
                   ? refresh_mode::quality
                   : refresh_mode::fast;
    }
    if (requested == refresh_mode::fast &&
        value.fast_count + 1U >= FAST_REFRESHES_BEFORE_QUALITY) {
        return refresh_mode::quality;
    }
    if (requested == refresh_mode::text &&
        value.text_count + 1U >= FILE_TEXT_REFRESHES_BEFORE_QUALITY) {
        return refresh_mode::quality;
    }
    return requested;
}

void record_refresh(
    ghost_debt& debt,
    refresh_mode mode,
    display_update_region region)
{
    if (mode == refresh_mode::quality) {
        // Every quality request is rendered as a full frame and cleans all regions.
        debt = {};
        return;
    }
    if (region == display_update_region::status_bar) {
        if (debt.status_bar < UINT16_MAX) {
            ++debt.status_bar;
        }
        if (debt.status_bar >= STATUS_BAR_GHOST_DEBT_LIMIT) {
            debt.status_cleanup_pending = true;
        }
        return;
    }
    region_ghost_debt& value = debt_for_region(debt, region);
    if (mode == refresh_mode::fastest) {
        if (value.fastest_count < FASTEST_REFRESHES_BEFORE_FAST) {
            ++value.fastest_count;
        }
    } else if (mode == refresh_mode::fast) {
        value.fastest_count = 0U;
        if (value.fast_count < FAST_REFRESHES_BEFORE_QUALITY) {
            ++value.fast_count;
        }
    } else if (mode == refresh_mode::text && value.text_count < UINT8_MAX) {
        ++value.text_count;
    }
}

display_refresh_result commit_refresh(
    ghost_debt& debt,
    const display_rect& rect,
    refresh_mode requested_mode,
    display_update_region region)
{
    display_refresh_result result = hal_display_refresh(rect, requested_mode);
    if (!result.success) {
        // Retry once with a full monochrome cycle. The HAL marks an uncertain
        // differential baseline invalid after every failed activation.
        ESP_LOGW(
            log_tag,
            "refresh failed mode=%s; attempting one quality recovery",
            refresh_mode_name(requested_mode));
        result = hal_display_refresh(
            {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT},
            refresh_mode::quality);
    }
    if (result.success) {
        record_refresh(debt, result.actual_mode, region);
        if (result.actual_mode != requested_mode) {
            ESP_LOGI(
                log_tag,
                "HAL upgraded refresh requested=%s actual=%s duration=%lu",
                refresh_mode_name(requested_mode),
                refresh_mode_name(result.actual_mode),
                static_cast<unsigned long>(result.duration_ms));
        }
    } else {
        ESP_LOGE(log_tag, "display refresh and bounded recovery both failed");
    }
    return result;
}

display_rect merged_rect(const display_rect& first, const display_rect& second)
{
    const std::int16_t left = std::min(first.left, second.left);
    const std::int16_t top = std::min(first.top, second.top);
    const std::int16_t right = std::max<std::int16_t>(
        first.left + first.width,
        second.left + second.width);
    const std::int16_t bottom = std::max<std::int16_t>(
        first.top + first.height,
        second.top + second.height);
    return {left, top, static_cast<std::int16_t>(right - left),
            static_cast<std::int16_t>(bottom - top)};
}

bool status_states_equal(
    const status_bar_view_state& left,
    const status_bar_view_state& right)
{
    const bool time_equal = left.time_valid == right.time_valid &&
                            (!left.time_valid ||
                             (left.hour == right.hour && left.minute == right.minute));
    const bool battery_equal =
        left.battery.level_valid == right.battery.level_valid &&
        (!left.battery.level_valid || left.battery.percent == right.battery.percent) &&
        left.battery.charging_valid == right.battery.charging_valid &&
        (!left.battery.charging_valid || left.battery.charging == right.battery.charging);
    return time_equal && battery_equal;
}

void draw_centered_line(const char* text, std::int32_t y, std::uint8_t size)
{
    canvas().set_text_alignment(display_text_alignment::middle_center);
    canvas().set_text_size(size);
    canvas().draw_text(text, canvas().width() / 2, y);
}

void draw_action_background(
    const display_rect& rect,
    bool pressed,
    bool enabled)
{
    const display_color color = pressed && enabled
                                    ? display_color::black
                                    : display_color::white;
    canvas().fill_rect(rect, color);
}

void draw_status_bar(const status_bar_view_state& state)
{
    const display_rect rect = status_bar_rect();
    canvas().fill_rect(rect, display_color::white);
    canvas().set_text_color(display_color::black, display_color::white);
    canvas().set_text_size(STATUS_BAR_TEXT_SIZE);

    char buffer[12] = {};
    if (state.time_valid) {
        std::snprintf(buffer, sizeof(buffer), "%02u:%02u", state.hour, state.minute);
    } else {
        std::snprintf(buffer, sizeof(buffer), "--:--");
    }
    canvas().set_text_alignment(display_text_alignment::middle_left);
    canvas().draw_text(buffer, STATUS_BAR_LEFT_MARGIN, STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);

    if (state.battery.level_valid) {
        std::snprintf(buffer, sizeof(buffer), "%u%%", state.battery.percent);
    } else {
        std::snprintf(buffer, sizeof(buffer), "--%%");
    }
    const std::int16_t percent_right = UI_DISPLAY_WIDTH - STATUS_BAR_RIGHT_MARGIN;
    canvas().set_text_alignment(display_text_alignment::middle_right);
    canvas().draw_text(buffer, percent_right, STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);
    if (state.battery.charging_valid && state.battery.charging) {
        const std::int16_t left = percent_right - STATUS_BATTERY_PERCENT_MAX_WIDTH - 30;
        const std::int16_t top = STATUS_BAR_TOP + 5;
        canvas().fill_triangle(
            left + 16, top, left + 5, top + 18, left + 14, top + 18,
            display_color::black);
        canvas().fill_triangle(
            left + 13, top + 13, left + 24, top + 13, left + 10, top + 30,
            display_color::black);
    }
}

void draw_front_light_bar(std::uint8_t selected, std::int16_t pressed)
{
    canvas().fill_rect(
        0, 0, canvas().width(), FRONT_LIGHT_BAR_HEIGHT, display_color::white);
    canvas().set_text_alignment(display_text_alignment::middle_center);
    canvas().set_text_size(2U);
    for (std::uint8_t index = 0U; index < FRONT_LIGHT_LEVEL_COUNT; ++index) {
        const display_rect rect = front_light_button_rect(index);
        const bool is_pressed = pressed == static_cast<std::int16_t>(index);
        canvas().fill_rect(
            rect,
            is_pressed ? display_color::black : display_color::white);
        if (!is_pressed) {
            canvas().draw_rect(rect, display_color::black);
            if (selected == index) {
                canvas().draw_rect(
                    rect.left + 3,
                    rect.top + 3,
                    rect.width - 6,
                    rect.height - 6,
                    display_color::black);
            }
        }
        canvas().set_text_color(
            is_pressed ? display_color::white : display_color::black,
            is_pressed ? display_color::black : display_color::white);
        canvas().draw_text(
            front_light_labels[index], rect.left + rect.width / 2, rect.top + rect.height / 2);
    }
}

void draw_menu_entry(std::uint8_t index, bool pressed)
{
    if (index >= MENU_ENTRY_COUNT) {
        return;
    }
    const display_rect rect = menu_entry_rect(index);
    const display_color background =
        pressed ? display_color::black : display_color::white;
    const display_color foreground =
        pressed ? display_color::white : display_color::black;
    canvas().fill_rect(rect, background);
    if (index == 0U) {
        canvas().draw_horizontal_line(
            rect.left,
            rect.top,
            rect.width,
            display_color::black);
    }
    canvas().draw_horizontal_line(
        rect.left,
        rect.top + rect.height - 1,
        rect.width,
        display_color::black);
    canvas().set_text_color(foreground, background);
    canvas().set_text_alignment(display_text_alignment::middle_center);
    canvas().set_text_size(MENU_ENTRY_TEXT_SIZE);
    canvas().draw_text(
        menu_entry_labels[index],
        rect.left + rect.width / 2,
        rect.top + rect.height / 2);
}

display_rect back_button_rect(ui_view_id view)
{
    switch (view) {
        case ui_view_id::test:
            return test_back_button_rect();
        case ui_view_id::rtc_setting:
            return rtc_back_button_rect();
        case ui_view_id::battery:
            return battery_back_button_rect();
        case ui_view_id::file:
            return file_back_button_rect();
        case ui_view_id::menu:
            return {0, 0, 0, 0};
    }
    return {0, 0, 0, 0};
}

void draw_back_button(ui_view_id view, bool pressed)
{
    const display_rect rect = back_button_rect(view);
    const display_color background =
        pressed ? display_color::black : display_color::white;
    const display_color foreground =
        pressed ? display_color::white : display_color::black;
    canvas().fill_rect(rect, background);
    canvas().draw_rect(rect, display_color::black);
    canvas().set_text_color(foreground, background);
    canvas().set_text_alignment(display_text_alignment::middle_center);
    canvas().set_text_size(APP_BACK_BUTTON_TEXT_SIZE);
    canvas().draw_text("< Back", rect.left + rect.width / 2, rect.top + rect.height / 2);
}

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

void rtc_field_range(rtc_edit_field field, std::uint8_t& start, std::uint8_t& length)
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

void draw_rtc_field(const rtc_view_state& state, rtc_edit_field field)
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
    canvas().fill_rect(
        rect,
        selected ? display_color::black : display_color::white);
    canvas().set_text_color(
        selected ? display_color::white : display_color::black,
        selected ? display_color::black : display_color::white);
    canvas().set_text_alignment(display_text_alignment::middle_center);
    canvas().set_text_size(RTC_FIELD_TEXT_SIZE);
    canvas().draw_text(text, rect.left + rect.width / 2, rect.top + rect.height / 2);
}

void draw_rtc_editor(const rtc_view_state& state)
{
    canvas().fill_rect(
        0,
        RTC_EDITOR_REGION_TOP,
        canvas().width(),
        RTC_EDITOR_REGION_HEIGHT,
        display_color::white);
    constexpr rtc_edit_field fields[] = {
        rtc_edit_field::year, rtc_edit_field::month, rtc_edit_field::day,
        rtc_edit_field::hour, rtc_edit_field::minute, rtc_edit_field::second,
    };
    for (const rtc_edit_field field : fields) {
        draw_rtc_field(state, field);
    }
    canvas().set_text_color(display_color::black, display_color::white);
    canvas().set_text_alignment(display_text_alignment::middle_center);
    canvas().set_text_size(RTC_FIELD_TEXT_SIZE);
    canvas().draw_text(":", 231, RTC_DATE_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    canvas().draw_text(":", 285, RTC_DATE_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    canvas().draw_text(":", 213, RTC_TIME_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    canvas().draw_text(":", 267, RTC_TIME_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    draw_centered_line(rtc_message_text(state), RTC_MESSAGE_CENTER_Y, RTC_MESSAGE_TEXT_SIZE);
}

bool rtc_keys_enabled(const rtc_view_state& state)
{
    return state.rtc_available && !state.loading && !state.saving;
}

void draw_rtc_key(std::uint8_t index, bool pressed, bool enabled)
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
    draw_action_background(rect, active_pressed, enabled);
    canvas().draw_horizontal_line(rect.left, rect.top, rect.width, display_color::black);
    canvas().draw_horizontal_line(
        rect.left,
        rect.top + rect.height - 1,
        rect.width,
        display_color::black);
    const std::int32_t center_x = rect.left + rect.width / 2;
    const std::int32_t center_y = rect.top + rect.height / 2;
    canvas().set_text_color(foreground, background);
    canvas().set_text_alignment(display_text_alignment::middle_center);
    canvas().set_text_size(RTC_KEY_TEXT_SIZE);
    if (index == 9U) {
        canvas().draw_line(center_x - 18, center_y, center_x - 5, center_y + 13, foreground);
        canvas().draw_line(center_x - 5, center_y + 13, center_x + 20, center_y - 16, foreground);
    } else if (index == 11U) {
        canvas().draw_line(center_x - 22, center_y, center_x + 22, center_y, foreground);
        canvas().draw_line(center_x - 22, center_y, center_x - 7, center_y - 14, foreground);
        canvas().draw_line(center_x - 22, center_y, center_x - 7, center_y + 14, foreground);
    } else {
        constexpr const char* labels[] = {
            "7", "8", "9", "4", "5", "6", "1", "2", "3", "", "0", "",
        };
        canvas().draw_text(labels[index], center_x, center_y);
    }
}

void draw_test_content(const test_view_state& state)
{
    canvas().fill_rect(
        0,
        TEST_CONTENT_REGION_TOP,
        canvas().width(),
        TEST_CONTENT_REGION_HEIGHT,
        display_color::white);
    canvas().set_text_color(display_color::black, display_color::white);
    draw_centered_line(
        state.text_state == test_text_state::hi_xi ? "HI XI" : "Hello world", 340, 4U);
    char line[80] = {};
    if (state.touch_type == test_touch_display_type::click) {
        std::snprintf(
            line, sizeof(line), "Click (%d, %d)  %lu ms", state.end_x, state.end_y,
            static_cast<unsigned long>(state.duration_ms));
        draw_centered_line(line, 460, 2U);
    } else if (state.touch_type == test_touch_display_type::long_press) {
        std::snprintf(
            line, sizeof(line), "Start (%d, %d)", state.start_x, state.start_y);
        draw_centered_line(line, 440, 2U);
        std::snprintf(
            line, sizeof(line), "End (%d, %d)  %lu ms", state.end_x, state.end_y,
            static_cast<unsigned long>(state.duration_ms));
        draw_centered_line(line, 490, 2U);
    }
}

void draw_battery_content(const battery_view_state& state)
{
    canvas().fill_rect(
        0,
        BATTERY_CONTENT_REGION_TOP,
        canvas().width(),
        BATTERY_CONTENT_REGION_HEIGHT,
        display_color::white);
    if (state.loading) {
        canvas().set_text_color(display_color::black, display_color::white);
        draw_centered_line("Loading...", BATTERY_FIRST_ROW_CENTER_Y, 2U);
        return;
    }
    char value[32] = {};
    constexpr const char* labels[] = {"Level", "Voltage", "Current", "Status"};
    for (std::uint8_t row = 0U; row < 4U; ++row) {
        if (row == 0U) {
            std::snprintf(value, sizeof(value), state.level_valid ? "%u %%" : "--", state.percent);
        } else if (row == 1U) {
            std::snprintf(
                value, sizeof(value), state.voltage_valid ? "%.2f V" : "--",
                static_cast<double>(state.voltage_mv) / 1000.0);
        } else if (row == 2U) {
            std::snprintf(value, sizeof(value), state.current_valid ? "%ld mA" : "--",
                          static_cast<long>(state.current_ma));
        } else {
            std::snprintf(
                value, sizeof(value), "%s",
                state.charging_valid ? (state.charging ? "Charging" : "Not charging") : "Unknown");
        }
        const std::int32_t y = BATTERY_FIRST_ROW_CENTER_Y + row * BATTERY_ROW_HEIGHT;
        canvas().set_text_color(display_color::black, display_color::white);
        canvas().set_text_size(BATTERY_ROW_TEXT_SIZE);
        canvas().set_text_alignment(display_text_alignment::middle_left);
        canvas().draw_text(labels[row], BATTERY_LABEL_LEFT, y);
        canvas().set_text_alignment(display_text_alignment::middle_right);
        canvas().draw_text(value, BATTERY_VALUE_RIGHT, y);
    }
}

const char* file_status_text(file_view_status status)
{
    switch (status) {
        case file_view_status::no_card:
            return "SD card not inserted";
        case file_view_status::mounting:
            return "Reading SD card...";
        case file_view_status::loading:
            return "Loading...";
        case file_view_status::error:
            return "SD card error - reinsert card";
        case file_view_status::directory_error:
            return "Unable to read directory";
        case file_view_status::directory_too_large:
            return "Directory has too many entries";
        case file_view_status::path_too_long:
            return "Path is too long";
        case file_view_status::ready:
            return "";
    }
    return "";
}

void draw_file_row(const file_view_state& state, std::uint8_t index, bool pressed)
{
    if (index >= state.row_count) {
        return;
    }
    const display_rect rect = file_row_rect(index);
    const file_row_view_state& row = state.rows[index];
    const bool active_pressed = pressed && row.enabled;
    const display_color background =
        active_pressed ? display_color::black : display_color::white;
    const display_color foreground =
        active_pressed ? display_color::white : display_color::black;
    draw_action_background(rect, active_pressed, row.enabled);
    canvas().set_font(display_font::cjk_24);
    canvas().set_text_size(FILE_ROW_TEXT_SIZE);
    canvas().set_text_alignment(display_text_alignment::middle_left);
    canvas().set_text_color(foreground, background);
    char name[FILE_VIEW_NAME_LENGTH + 4U] = {};
    std::snprintf(name, sizeof(name), "%s%s", row.name, row.name_truncated ? "..." : "");
    while (canvas().text_width(name) > rect.width - 36 && std::strlen(name) > 3U) {
        const std::size_t length = std::strlen(name);
        std::size_t cut = length - 4U;
        while (cut > 0U && (static_cast<unsigned char>(name[cut]) & 0xc0U) == 0x80U) {
            --cut;
        }
        std::memcpy(name + cut, "...", 4U);
    }
    canvas().draw_text(name, rect.left + 4, rect.top + rect.height / 2);
    if (row.directory) {
        canvas().set_text_alignment(display_text_alignment::middle_right);
        canvas().draw_text(">", rect.left + rect.width - 4, rect.top + rect.height / 2);
    }
    canvas().set_font(display_font::default_font);
}

bool file_page_button_enabled(const file_view_state& state, bool next)
{
    return next ? state.page_index + 1U < state.page_count
                : state.page_index > 0U;
}

void draw_file_page_button(
    const file_view_state& state,
    bool next,
    bool pressed)
{
    const display_rect rect = next ? file_next_page_rect() : file_previous_page_rect();
    const bool enabled = file_page_button_enabled(state, next);
    const bool active_pressed = pressed && enabled;
    const display_color background =
        active_pressed ? display_color::black : display_color::white;
    const display_color foreground =
        active_pressed ? display_color::white : display_color::black;
    draw_action_background(rect, active_pressed, enabled);
    if (!enabled) {
        return;
    }
    canvas().set_text_color(foreground, background);
    canvas().set_text_alignment(display_text_alignment::middle_center);
    canvas().set_text_size(FILE_PAGE_BUTTON_TEXT_SIZE);
    canvas().draw_text(
        next ? ">" : "<",
        rect.left + rect.width / 2,
        rect.top + rect.height / 2);
}

void draw_file_content(const file_view_state& state)
{
    canvas().fill_rect(
        0,
        FILE_CONTENT_REGION_TOP,
        canvas().width(),
        FILE_CONTENT_REGION_HEIGHT,
        display_color::white);
    canvas().set_font(display_font::cjk_24);
    canvas().set_text_size(1U);
    canvas().set_text_color(display_color::black, display_color::white);
    canvas().set_text_alignment(display_text_alignment::middle_left);
    canvas().draw_text(state.path, FILE_PATH_LEFT, FILE_PATH_TOP + FILE_PATH_HEIGHT / 2);
    canvas().draw_horizontal_line(
        FILE_PATH_LEFT,
        FILE_PATH_TOP + FILE_PATH_HEIGHT,
        UI_DISPLAY_WIDTH - FILE_PATH_LEFT * 2,
        display_color::black);
    canvas().set_font(display_font::default_font);
    if (state.status == file_view_status::ready) {
        for (std::uint8_t index = 0U; index < state.row_count; ++index) {
            draw_file_row(state, index, false);
        }
        draw_file_page_button(state, false, false);
        draw_file_page_button(state, true, false);
        char page[24] = {};
        std::snprintf(page, sizeof(page), "Page %u/%u", state.page_index + 1U, state.page_count);
        canvas().set_text_color(display_color::black, display_color::white);
        canvas().set_text_alignment(display_text_alignment::middle_center);
        canvas().set_text_size(FILE_PAGE_LABEL_TEXT_SIZE);
        canvas().draw_text(
            page,
            canvas().width() / 2,
            FILE_PAGINATION_TOP + FILE_PAGINATION_HEIGHT / 2);
    } else {
        canvas().set_text_color(display_color::black, display_color::white);
        draw_centered_line(file_status_text(state.status), 360, 2U);
    }
    if (state.popup_visible) {
        canvas().fill_rect(
            FILE_POPUP_LEFT,
            FILE_POPUP_TOP,
            FILE_POPUP_WIDTH,
            FILE_POPUP_HEIGHT,
            display_color::white);
        canvas().draw_rect(
            FILE_POPUP_LEFT,
            FILE_POPUP_TOP,
            FILE_POPUP_WIDTH,
            FILE_POPUP_HEIGHT,
            display_color::black);
        canvas().set_text_color(display_color::black, display_color::white);
        draw_centered_line("File preview is not supported", FILE_POPUP_TOP + FILE_POPUP_HEIGHT / 2,
                           FILE_POPUP_TEXT_SIZE);
    }
}

void draw_full_view(
    const display_request& request,
    std::uint8_t selected_light,
    std::int16_t pressed_light)
{
    canvas().fill_screen(display_color::white);
    canvas().set_text_color(display_color::black, display_color::white);
    switch (request.view) {
        case ui_view_id::menu:
            draw_centered_line(PROJECT_NAME, MENU_TITLE_CENTER_Y, MENU_TITLE_TEXT_SIZE);
            for (std::uint8_t index = 0U; index < MENU_ENTRY_COUNT; ++index) {
                draw_menu_entry(index, false);
            }
            break;
        case ui_view_id::test:
            draw_front_light_bar(selected_light, pressed_light);
            draw_back_button(request.view, false);
            draw_test_content(request.test);
            break;
        case ui_view_id::rtc_setting:
            draw_back_button(request.view, false);
            draw_centered_line("RTC Setting", RTC_TITLE_CENTER_Y, RTC_TITLE_TEXT_SIZE);
            draw_rtc_editor(request.rtc);
            for (std::uint8_t index = 0U; index < RTC_KEY_COUNT; ++index) {
                draw_rtc_key(index, false, rtc_keys_enabled(request.rtc));
            }
            break;
        case ui_view_id::battery:
            draw_back_button(request.view, false);
            draw_centered_line("Battery", BATTERY_TITLE_CENTER_Y, BATTERY_TITLE_TEXT_SIZE);
            draw_battery_content(request.battery);
            break;
        case ui_view_id::file:
            draw_back_button(request.view, false);
            draw_centered_line("Files", FILE_TITLE_CENTER_Y, FILE_TITLE_TEXT_SIZE);
            draw_file_content(request.file);
            break;
    }
}

display_rect content_rect(display_update_region region)
{
    switch (region) {
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            return {0, RTC_EDITOR_REGION_TOP, UI_DISPLAY_WIDTH, RTC_EDITOR_REGION_HEIGHT};
        case display_update_region::test_content:
            return {0, TEST_CONTENT_REGION_TOP, UI_DISPLAY_WIDTH, TEST_CONTENT_REGION_HEIGHT};
        case display_update_region::battery_content:
            return {0, BATTERY_CONTENT_REGION_TOP, UI_DISPLAY_WIDTH,
                    BATTERY_CONTENT_REGION_HEIGHT};
        case display_update_region::file_content:
            return {0, FILE_CONTENT_REGION_TOP, UI_DISPLAY_WIDTH, FILE_CONTENT_REGION_HEIGHT};
        case display_update_region::status_bar:
            return status_bar_rect();
        case display_update_region::full:
            return {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT};
        case display_update_region::control:
            return {0, 0, UI_DISPLAY_WIDTH, FRONT_LIGHT_BAR_HEIGHT};
    }
    return {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT};
}

void draw_partial_request(const display_request& request, display_rect& rect)
{
    rect = content_rect(request.update_region);
    switch (request.update_region) {
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            draw_rtc_editor(request.rtc);
            for (std::uint8_t index = 0U; index < RTC_KEY_COUNT; ++index) {
                if ((request.released_key_mask & (1U << index)) != 0U) {
                    draw_rtc_key(
                        index,
                        false,
                        rtc_keys_enabled(request.rtc));
                    rect = merged_rect(rect, rtc_key_rect(index));
                }
            }
            break;
        case display_update_region::test_content:
            draw_test_content(request.test);
            break;
        case display_update_region::battery_content:
            draw_battery_content(request.battery);
            break;
        case display_update_region::file_content:
            draw_file_content(request.file);
            break;
        case display_update_region::control:
            if (request.view == ui_view_id::test) {
                draw_front_light_bar(request.test.front_light_level, no_pressed_button);
            }
            break;
        case display_update_region::full:
        case display_update_region::status_bar:
            break;
    }
}

display_rect draw_control(
    const display_control_request& request,
    const display_request& latest,
    std::uint8_t selected_light,
    std::int16_t pressed_light)
{
    switch (request.control) {
        case ui_control_type::front_light:
            draw_front_light_bar(selected_light, pressed_light);
            return {0, 0, UI_DISPLAY_WIDTH, FRONT_LIGHT_BAR_HEIGHT};
        case ui_control_type::menu_entry:
            draw_menu_entry(request.index, request.pressed);
            return menu_entry_rect(request.index);
        case ui_control_type::navigate_back:
            draw_back_button(latest.view, request.pressed);
            return back_button_rect(latest.view);
        case ui_control_type::rtc_key:
            draw_rtc_key(
                request.index,
                request.pressed,
                rtc_keys_enabled(latest.rtc));
            return rtc_key_rect(request.index);
        case ui_control_type::file_row:
            draw_file_row(latest.file, request.index, request.pressed);
            return file_row_rect(request.index);
        case ui_control_type::file_previous_page:
            draw_file_page_button(latest.file, false, request.pressed);
            return file_previous_page_rect();
        case ui_control_type::file_next_page:
            draw_file_page_button(latest.file, true, request.pressed);
            return file_next_page_rect();
        case ui_control_type::none:
        case ui_control_type::rtc_field:
        case ui_control_type::test_surface:
            break;
    }
    return {0, 0, 0, 0};
}

bool queued_not_after(std::uint32_t left, std::uint32_t right)
{
    return static_cast<std::int32_t>(right - left) >= 0;
}

bool control_replaced_by_frame(
    const display_control_request& control,
    const display_request& frame)
{
    if (!queued_not_after(control.queued_at_ms, frame.queued_at_ms)) {
        return false;
    }
    if (frame.mode == refresh_mode::quality ||
        frame.update_region == display_update_region::full) {
        return true;
    }
    if (control.control == ui_control_type::rtc_key &&
        frame.view == ui_view_id::rtc_setting &&
        frame.update_region == display_update_region::rtc_editor_and_key &&
        control.index < RTC_KEY_COUNT &&
        (frame.released_key_mask & (1U << control.index)) != 0U) {
        return true;
    }
    if (control.control == ui_control_type::front_light &&
        frame.view == ui_view_id::test &&
        frame.update_region == display_update_region::control) {
        return true;
    }
    const bool file_control =
        control.control == ui_control_type::file_row ||
        control.control == ui_control_type::file_previous_page ||
        control.control == ui_control_type::file_next_page;
    return file_control && frame.view == ui_view_id::file &&
           frame.update_region == display_update_region::file_content;
}

void process_control_request(
    const display_control_request& control,
    const display_request& latest,
    ghost_debt& debt,
    std::uint8_t selected_light,
    std::int16_t& pressed_light,
    status_bar_view_state& displayed_status,
    bool& status_displayed,
    bool has_frame)
{
    if (!has_frame) {
        return;
    }
    if (control.control == ui_control_type::front_light) {
        pressed_light = control.pressed ? control.index : no_pressed_button;
    }
    display_rect rect =
        draw_control(control, latest, selected_light, pressed_light);
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }

    refresh_mode mode = resolve_mode(
        control.mode,
        control.update_region,
        control.allow_quality_cleanup,
        debt);
    if (mode == refresh_mode::quality) {
        draw_full_view(latest, selected_light, pressed_light);
        displayed_status = status_bar_get_state();
        draw_status_bar(displayed_status);
        rect = {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT};
        status_displayed = true;
    }
    const std::uint32_t start_ms = monotonic_ms();
    commit_refresh(debt, rect, mode, control.update_region);
    ESP_LOGI(
        log_tag,
        "stage=control_refresh control=%u pressed=%d queue_wait_ms=%lu duration_ms=%lu",
        static_cast<unsigned>(control.control),
        control.pressed,
        static_cast<unsigned long>(start_ms - control.queued_at_ms),
        static_cast<unsigned long>(monotonic_ms() - start_ms));
}

bool submit_request(const display_request& request)
{
    if (request_queue == nullptr) {
        return false;
    }
    display_request queued = request;
    queued.queued_at_ms = monotonic_ms();
    if (xQueueSend(request_queue, &queued, 0) != pdTRUE) {
        display_request discarded = {};
        if (xQueueReceive(request_queue, &discarded, 0) != pdTRUE) {
            return false;
        }
        if (discarded.mode == refresh_mode::quality) {
            queued.mode = refresh_mode::quality;
            queued.update_region = display_update_region::full;
        }
        if (xQueueSend(request_queue, &queued, 0) != pdTRUE) {
            return false;
        }
    }
    xTaskNotifyGive(renderer_task_handle);
    return true;
}

void renderer_task(void*)
{
    ghost_debt debt = {};
    display_request latest = {};
    latest.view = ui_view_id::menu;
    std::uint8_t selected_light = FRONT_LIGHT_DEFAULT_LEVEL_INDEX;
    std::int16_t pressed_light = no_pressed_button;
    status_bar_view_state displayed_status = {};
    bool has_frame = false;
    bool status_displayed = false;

    for (;;) {
        if (ulTaskNotifyTake(
                pdTRUE,
                pdMS_TO_TICKS(DISPLAY_IDLE_SLEEP_MS)) == 0U) {
            if (!hal_display_sleep()) {
                ESP_LOGW(log_tag, "idle display sleep failed");
            }
            continue;
        }

        display_request next = {};
        bool has_request = false;
        bool quality_pending = false;
        display_request candidate = {};
        while (xQueueReceive(request_queue, &candidate, 0) == pdTRUE) {
            quality_pending = quality_pending || candidate.mode == refresh_mode::quality;
            next = candidate;
            has_request = true;
        }

        display_control_request controls[DISPLAY_CONTROL_QUEUE_LENGTH] = {};
        std::size_t control_count = 0U;
        while (control_count < DISPLAY_CONTROL_QUEUE_LENGTH &&
               xQueueReceive(control_queue, &controls[control_count], 0) == pdTRUE) {
            ++control_count;
        }
        if (has_request) {
            if (quality_pending) {
                next.mode = refresh_mode::quality;
                next.update_region = display_update_region::full;
            }
            for (std::size_t index = 0U; index < control_count; ++index) {
                if (!control_replaced_by_frame(controls[index], next)) {
                    continue;
                }
                if (controls[index].control == ui_control_type::front_light) {
                    pressed_light = no_pressed_button;
                }
                ESP_LOGI(
                    log_tag,
                    "discard transient control=%u pressed=%d before frame view=%u",
                    static_cast<unsigned>(controls[index].control),
                    controls[index].pressed,
                    static_cast<unsigned>(next.view));
                controls[index].control = ui_control_type::none;
            }

            latest = next;
            selected_light = latest.view == ui_view_id::test
                                 ? latest.test.front_light_level
                                 : selected_light;
            const refresh_mode requested_mode =
                debt.status_cleanup_pending ? refresh_mode::quality : next.mode;
            refresh_mode mode = resolve_mode(
                requested_mode,
                next.update_region,
                next.allow_quality_cleanup,
                debt);
            display_rect rect = content_rect(next.update_region);
            const std::uint32_t draw_start_ms = monotonic_ms();
            if (!has_frame || mode == refresh_mode::quality ||
                next.update_region == display_update_region::full) {
                draw_full_view(next, selected_light, pressed_light);
                rect = {0, 0, UI_DISPLAY_WIDTH, UI_DISPLAY_HEIGHT};
            } else {
                draw_partial_request(next, rect);
            }
            const status_bar_view_state status = status_bar_get_state();
            if (!status_displayed || !status_states_equal(status, displayed_status) ||
                rect.height == UI_DISPLAY_HEIGHT) {
                draw_status_bar(status);
                displayed_status = status;
                status_displayed = true;
                rect = merged_rect(rect, status_bar_rect());
            }
            const std::uint32_t refresh_start_ms = monotonic_ms();
            const display_refresh_result refresh =
                commit_refresh(debt, rect, mode, next.update_region);
            has_frame = refresh.success;
            status_displayed = refresh.success;
            if (refresh.success) {
                ui_presentation_commit_frame(
                    next.view,
                    next.view_generation,
                    next.view == ui_view_id::file ? &next.file : nullptr,
                    next.view == ui_view_id::rtc_setting &&
                        rtc_keys_enabled(next.rtc));
            }
            ESP_LOGI(
                log_tag,
                "stage=view_refresh requested=%s actual=%s success=%d view=%u generation=%lu region=%u queue_wait_ms=%lu draw_ms=%lu refresh_ms=%lu",
                refresh_mode_name(mode),
                refresh_mode_name(refresh.actual_mode),
                refresh.success,
                static_cast<unsigned>(next.view),
                static_cast<unsigned long>(next.view_generation),
                static_cast<unsigned>(next.update_region),
                static_cast<unsigned long>(draw_start_ms - next.queued_at_ms),
                static_cast<unsigned long>(refresh_start_ms - draw_start_ms),
                static_cast<unsigned long>(monotonic_ms() - refresh_start_ms));
        }

        for (std::size_t index = 0U; index < control_count; ++index) {
            if (controls[index].control == ui_control_type::none) {
                continue;
            }
            process_control_request(
                controls[index],
                latest,
                debt,
                selected_light,
                pressed_light,
                displayed_status,
                status_displayed,
                has_frame);
        }

        const status_bar_view_state status = status_bar_get_state();
        if (has_frame && (!status_displayed || !status_states_equal(status, displayed_status))) {
            draw_status_bar(status);
            const display_refresh_result refresh = commit_refresh(
                debt,
                status_bar_rect(),
                refresh_mode::fastest,
                display_update_region::status_bar);
            if (refresh.success) {
                displayed_status = status;
                status_displayed = true;
            }
        }
    }
}

display_request make_request(ui_view_id view, ui_update_reason reason)
{
    display_request request = {};
    request.view_generation = ui_presentation_prepare_frame(
        view,
        reason == ui_update_reason::view_opened);
    request.view = view;
    request.mode = reason == ui_update_reason::view_opened
                       ? refresh_mode::quality
                       : refresh_mode::fastest;
    request.update_region = reason == ui_update_reason::view_opened
                                ? display_update_region::full
                                : display_update_region::control;
    request.allow_quality_cleanup = true;
    return request;
}

}  // namespace

esp_err_t ui_renderer_init()
{
    request_queue = xQueueCreate(DISPLAY_REQUEST_QUEUE_LENGTH, sizeof(display_request));
    control_queue = xQueueCreate(DISPLAY_CONTROL_QUEUE_LENGTH, sizeof(display_control_request));
    if (request_queue == nullptr || control_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(renderer_task, "ui_renderer", DISPLAY_TASK_STACK_SIZE, nullptr,
                    DISPLAY_TASK_PRIORITY, &renderer_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(log_tag, "PaperMono renderer started");
    return ESP_OK;
}

bool ui_render_menu(const menu_view_state&, ui_update_reason reason)
{
    return submit_request(make_request(ui_view_id::menu, reason));
}

bool ui_render_test(const test_view_state& state, ui_update_reason reason)
{
    display_request request = make_request(ui_view_id::test, reason);
    request.test = state;
    if (reason != ui_update_reason::view_opened) {
        const bool light_selection = reason == ui_update_reason::selection_changed;
        request.mode = light_selection || state.touch_type != test_touch_display_type::click
                           ? refresh_mode::fastest
                           : refresh_mode::fast;
        request.update_region = light_selection ? display_update_region::control
                                                : display_update_region::test_content;
    }
    return submit_request(request);
}

bool ui_render_rtc(
    const rtc_view_state& state,
    ui_update_reason reason,
    ui_control_type released_control,
    std::uint8_t released_index)
{
    display_request request = make_request(ui_view_id::rtc_setting, reason);
    request.rtc = state;
    if (reason != ui_update_reason::view_opened) {
        request.update_region = released_control == ui_control_type::rtc_key
                                    ? display_update_region::rtc_editor_and_key
                                    : display_update_region::rtc_editor;
        if (released_control == ui_control_type::rtc_key && released_index < RTC_KEY_COUNT) {
            request.released_key_mask = static_cast<std::uint16_t>(1U << released_index);
        }
    }
    return submit_request(request);
}

bool ui_render_battery(const battery_view_state& state, ui_update_reason reason)
{
    display_request request = make_request(ui_view_id::battery, reason);
    request.battery = state;
    if (reason != ui_update_reason::view_opened) {
        request.update_region = display_update_region::battery_content;
    }
    return submit_request(request);
}

bool ui_render_file(const file_view_state& state, ui_update_reason reason)
{
    display_request request = make_request(ui_view_id::file, reason);
    request.file = state;
    if (reason != ui_update_reason::view_opened) {
        request.mode = refresh_mode::text;
        request.update_region = display_update_region::file_content;
    }
    return submit_request(request);
}

bool ui_render_control(ui_control_type control, std::uint8_t index, bool pressed)
{
    if (control_queue == nullptr || renderer_task_handle == nullptr) {
        return false;
    }
    display_control_request request = {};
    request.queued_at_ms = monotonic_ms();
    request.control = control;
    request.mode = refresh_mode::fastest;
    request.update_region = display_update_region::control;
    request.index = index;
    request.pressed = pressed;
    request.allow_quality_cleanup = !pressed;
    if (xQueueSend(control_queue, &request, 0) != pdTRUE) {
        return false;
    }
    xTaskNotifyGive(renderer_task_handle);
    return true;
}

void ui_renderer_notify_status_bar()
{
    if (renderer_task_handle != nullptr) {
        xTaskNotifyGive(renderer_task_handle);
    }
}
