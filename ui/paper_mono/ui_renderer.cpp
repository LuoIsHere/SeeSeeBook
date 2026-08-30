#include "ui_renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "display.hpp"
#include "layout.hpp"
#include "project_info.hpp"
#include "renderer_internal.hpp"
#include "status_bar.hpp"

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
};

struct ghost_debt {
    region_ghost_debt control;
    region_ghost_debt rtc_editor;
    std::uint16_t status_bar = 0U;
    region_ghost_debt test_content;
    region_ghost_debt battery_content;
    region_ghost_debt file_content;
};

M5GFX& canvas()
{
    return hal_display_canvas();
}

const char* refresh_mode_name(refresh_mode mode)
{
    switch (mode) {
        case refresh_mode::fastest:
            return "fastest";
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
        value.fastest_count + 1U >= 10U) {
        return value.fast_count + 1U >= 5U ? refresh_mode::quality
                                           : refresh_mode::fast;
    }
    if (requested == refresh_mode::fast && value.fast_count + 1U >= 5U) {
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
        // A full quality frame cleans every application-owned region. The status bar
        // keeps an independent counter because it is always refreshed with fastest.
        const std::uint16_t status_debt = debt.status_bar;
        debt = {};
        debt.status_bar = status_debt;
        return;
    }
    if (region == display_update_region::status_bar) {
        if (debt.status_bar < UINT16_MAX) {
            ++debt.status_bar;
        }
        return;
    }
    region_ghost_debt& value = debt_for_region(debt, region);
    if (mode == refresh_mode::fastest) {
        if (value.fastest_count < 10U) {
            ++value.fastest_count;
        }
    } else if (mode == refresh_mode::fast) {
        value.fastest_count = 0U;
        if (value.fast_count < 5U) {
            ++value.fast_count;
        }
    }
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
    canvas().setTextDatum(textdatum_t::middle_center);
    canvas().setTextSize(size);
    canvas().drawString(text, canvas().width() / 2, y);
}

void draw_status_bar(const status_bar_view_state& state)
{
    const display_rect rect = status_bar_rect();
    canvas().fillRect(rect.left, rect.top, rect.width, rect.height, TFT_WHITE);
    canvas().setTextColor(TFT_BLACK, TFT_WHITE);
    canvas().setTextSize(STATUS_BAR_TEXT_SIZE);

    char buffer[12] = {};
    if (state.time_valid) {
        std::snprintf(buffer, sizeof(buffer), "%02u:%02u", state.hour, state.minute);
    } else {
        std::snprintf(buffer, sizeof(buffer), "--:--");
    }
    canvas().setTextDatum(textdatum_t::middle_left);
    canvas().drawString(buffer, STATUS_BAR_LEFT_MARGIN, STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);

    if (state.battery.level_valid) {
        std::snprintf(buffer, sizeof(buffer), "%u%%", state.battery.percent);
    } else {
        std::snprintf(buffer, sizeof(buffer), "--%%");
    }
    const std::int16_t percent_right = PAPER_MONO_PORTRAIT_WIDTH - STATUS_BAR_RIGHT_MARGIN;
    canvas().setTextDatum(textdatum_t::middle_right);
    canvas().drawString(buffer, percent_right, STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);
    if (state.battery.charging_valid && state.battery.charging) {
        const std::int16_t left = percent_right - STATUS_BATTERY_PERCENT_MAX_WIDTH - 30;
        const std::int16_t top = STATUS_BAR_TOP + 5;
        canvas().fillTriangle(left + 16, top, left + 5, top + 18, left + 14, top + 18, TFT_BLACK);
        canvas().fillTriangle(
            left + 13, top + 13, left + 24, top + 13, left + 10, top + 30, TFT_BLACK);
    }
}

void draw_front_light_bar(std::uint8_t selected, std::int16_t pressed)
{
    canvas().fillRect(0, 0, canvas().width(), FRONT_LIGHT_BAR_HEIGHT, TFT_WHITE);
    canvas().setTextDatum(textdatum_t::middle_center);
    canvas().setTextSize(2U);
    for (std::uint8_t index = 0U; index < FRONT_LIGHT_LEVEL_COUNT; ++index) {
        const display_rect rect = front_light_button_rect(index);
        const bool is_pressed = pressed == static_cast<std::int16_t>(index);
        canvas().fillRect(
            rect.left, rect.top, rect.width, rect.height, is_pressed ? TFT_BLACK : TFT_WHITE);
        if (!is_pressed) {
            canvas().drawRect(rect.left, rect.top, rect.width, rect.height, TFT_BLACK);
            if (selected == index) {
                canvas().drawRect(
                    rect.left + 3, rect.top + 3, rect.width - 6, rect.height - 6, TFT_BLACK);
            }
        }
        canvas().setTextColor(is_pressed ? TFT_WHITE : TFT_BLACK,
                              is_pressed ? TFT_BLACK : TFT_WHITE);
        canvas().drawString(
            front_light_labels[index], rect.left + rect.width / 2, rect.top + rect.height / 2);
    }
}

void draw_menu_entry(std::uint8_t index, bool pressed)
{
    if (index >= MENU_ENTRY_COUNT) {
        return;
    }
    const display_rect rect = menu_entry_rect(index);
    const std::uint32_t background = pressed ? TFT_BLACK : TFT_WHITE;
    const std::uint32_t foreground = pressed ? TFT_WHITE : TFT_BLACK;
    canvas().fillRect(rect.left, rect.top, rect.width, rect.height, background);
    canvas().drawFastHLine(rect.left, rect.top, rect.width, TFT_BLACK);
    canvas().drawFastHLine(rect.left, rect.top + rect.height - 1, rect.width, TFT_BLACK);
    canvas().setTextColor(foreground, background);
    canvas().setTextDatum(textdatum_t::middle_center);
    canvas().setTextSize(MENU_ENTRY_TEXT_SIZE);
    canvas().drawString(menu_entry_labels[index], rect.left + rect.width / 2, rect.top + rect.height / 2);
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
    const std::uint32_t background = pressed ? TFT_BLACK : TFT_WHITE;
    const std::uint32_t foreground = pressed ? TFT_WHITE : TFT_BLACK;
    canvas().fillRect(rect.left, rect.top, rect.width, rect.height, background);
    canvas().drawRect(rect.left, rect.top, rect.width, rect.height, TFT_BLACK);
    canvas().setTextColor(foreground, background);
    canvas().setTextDatum(textdatum_t::middle_center);
    canvas().setTextSize(APP_BACK_BUTTON_TEXT_SIZE);
    canvas().drawString("< Back", rect.left + rect.width / 2, rect.top + rect.height / 2);
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
    canvas().fillRect(
        rect.left, rect.top, rect.width, rect.height, selected ? TFT_BLACK : TFT_WHITE);
    canvas().setTextColor(selected ? TFT_WHITE : TFT_BLACK,
                          selected ? TFT_BLACK : TFT_WHITE);
    canvas().setTextDatum(textdatum_t::middle_center);
    canvas().setTextSize(RTC_FIELD_TEXT_SIZE);
    canvas().drawString(text, rect.left + rect.width / 2, rect.top + rect.height / 2);
}

void draw_rtc_editor(const rtc_view_state& state)
{
    canvas().fillRect(0, RTC_EDITOR_REGION_TOP, canvas().width(), RTC_EDITOR_REGION_HEIGHT, TFT_WHITE);
    constexpr rtc_edit_field fields[] = {
        rtc_edit_field::year, rtc_edit_field::month, rtc_edit_field::day,
        rtc_edit_field::hour, rtc_edit_field::minute, rtc_edit_field::second,
    };
    for (const rtc_edit_field field : fields) {
        draw_rtc_field(state, field);
    }
    canvas().setTextColor(TFT_BLACK, TFT_WHITE);
    canvas().setTextDatum(textdatum_t::middle_center);
    canvas().setTextSize(RTC_FIELD_TEXT_SIZE);
    canvas().drawString(":", 231, RTC_DATE_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    canvas().drawString(":", 285, RTC_DATE_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    canvas().drawString(":", 213, RTC_TIME_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    canvas().drawString(":", 267, RTC_TIME_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    draw_centered_line(rtc_message_text(state), RTC_MESSAGE_CENTER_Y, RTC_MESSAGE_TEXT_SIZE);
}

void draw_rtc_key(std::uint8_t index, bool pressed)
{
    if (index >= RTC_KEY_COUNT) {
        return;
    }
    const display_rect rect = rtc_key_rect(index);
    const std::uint32_t background = pressed ? TFT_BLACK : TFT_WHITE;
    const std::uint32_t foreground = pressed ? TFT_WHITE : TFT_BLACK;
    canvas().fillRect(rect.left, rect.top, rect.width, rect.height, background);
    canvas().drawFastHLine(rect.left, rect.top, rect.width, TFT_BLACK);
    canvas().drawFastHLine(rect.left, rect.top + rect.height - 1, rect.width, TFT_BLACK);
    const std::int32_t center_x = rect.left + rect.width / 2;
    const std::int32_t center_y = rect.top + rect.height / 2;
    canvas().setTextColor(foreground, background);
    canvas().setTextDatum(textdatum_t::middle_center);
    canvas().setTextSize(RTC_KEY_TEXT_SIZE);
    if (index == 9U) {
        canvas().drawLine(center_x - 18, center_y, center_x - 5, center_y + 13, foreground);
        canvas().drawLine(center_x - 5, center_y + 13, center_x + 20, center_y - 16, foreground);
    } else if (index == 11U) {
        canvas().drawLine(center_x - 22, center_y, center_x + 22, center_y, foreground);
        canvas().drawLine(center_x - 22, center_y, center_x - 7, center_y - 14, foreground);
        canvas().drawLine(center_x - 22, center_y, center_x - 7, center_y + 14, foreground);
    } else {
        constexpr const char* labels[] = {
            "7", "8", "9", "4", "5", "6", "1", "2", "3", "", "0", "",
        };
        canvas().drawString(labels[index], center_x, center_y);
    }
}

void draw_test_content(const test_view_state& state)
{
    canvas().fillRect(
        0, TEST_CONTENT_REGION_TOP, canvas().width(), TEST_CONTENT_REGION_HEIGHT, TFT_WHITE);
    canvas().setTextColor(TFT_BLACK, TFT_WHITE);
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
    canvas().fillRect(
        0, BATTERY_CONTENT_REGION_TOP, canvas().width(), BATTERY_CONTENT_REGION_HEIGHT, TFT_WHITE);
    if (state.loading) {
        canvas().setTextColor(TFT_BLACK, TFT_WHITE);
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
        canvas().setTextColor(TFT_BLACK, TFT_WHITE);
        canvas().setTextSize(BATTERY_ROW_TEXT_SIZE);
        canvas().setTextDatum(textdatum_t::middle_left);
        canvas().drawString(labels[row], BATTERY_LABEL_LEFT, y);
        canvas().setTextDatum(textdatum_t::middle_right);
        canvas().drawString(value, BATTERY_VALUE_RIGHT, y);
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
    const std::uint32_t background = pressed ? TFT_BLACK : TFT_WHITE;
    const std::uint32_t foreground = pressed ? TFT_WHITE : TFT_BLACK;
    canvas().fillRect(rect.left, rect.top, rect.width, rect.height, background);
    canvas().setFont(&fonts::efontCN_24);
    canvas().setTextSize(FILE_ROW_TEXT_SIZE);
    canvas().setTextDatum(textdatum_t::middle_left);
    canvas().setTextColor(foreground, background);
    char name[FILE_VIEW_NAME_LENGTH + 4U] = {};
    std::snprintf(name, sizeof(name), "%s%s", row.name, row.name_truncated ? "..." : "");
    while (canvas().textWidth(name) > rect.width - 36 && std::strlen(name) > 3U) {
        const std::size_t length = std::strlen(name);
        std::size_t cut = length - 4U;
        while (cut > 0U && (static_cast<unsigned char>(name[cut]) & 0xc0U) == 0x80U) {
            --cut;
        }
        std::memcpy(name + cut, "...", 4U);
    }
    canvas().drawString(name, rect.left + 4, rect.top + rect.height / 2);
    if (row.directory) {
        canvas().setTextDatum(textdatum_t::middle_right);
        canvas().drawString(">", rect.left + rect.width - 4, rect.top + rect.height / 2);
    }
    canvas().setFont(&fonts::Font0);
}

void draw_file_page_button(bool next, bool pressed)
{
    const display_rect rect = next ? file_next_page_rect() : file_previous_page_rect();
    const std::uint32_t background = pressed ? TFT_BLACK : TFT_WHITE;
    const std::uint32_t foreground = pressed ? TFT_WHITE : TFT_BLACK;
    canvas().fillRect(rect.left, rect.top, rect.width, rect.height, background);
    canvas().setTextColor(foreground, background);
    canvas().setTextDatum(textdatum_t::middle_center);
    canvas().setTextSize(FILE_PAGE_BUTTON_TEXT_SIZE);
    canvas().drawString(next ? ">" : "<", rect.left + rect.width / 2, rect.top + rect.height / 2);
}

void draw_file_content(const file_view_state& state)
{
    canvas().fillRect(
        0, FILE_CONTENT_REGION_TOP, canvas().width(), FILE_CONTENT_REGION_HEIGHT, TFT_WHITE);
    canvas().setFont(&fonts::efontCN_24);
    canvas().setTextSize(1U);
    canvas().setTextColor(TFT_BLACK, TFT_WHITE);
    canvas().setTextDatum(textdatum_t::middle_left);
    canvas().drawString(state.path, FILE_PATH_LEFT, FILE_PATH_TOP + FILE_PATH_HEIGHT / 2);
    canvas().drawFastHLine(FILE_PATH_LEFT, FILE_PATH_TOP + FILE_PATH_HEIGHT,
                           PAPER_MONO_PORTRAIT_WIDTH - FILE_PATH_LEFT * 2, TFT_BLACK);
    canvas().setFont(&fonts::Font0);
    if (state.status == file_view_status::ready) {
        for (std::uint8_t index = 0U; index < state.row_count; ++index) {
            draw_file_row(state, index, false);
        }
        draw_file_page_button(false, false);
        draw_file_page_button(true, false);
        char page[24] = {};
        std::snprintf(page, sizeof(page), "Page %u/%u", state.page_index + 1U, state.page_count);
        canvas().setTextColor(TFT_BLACK, TFT_WHITE);
        canvas().setTextDatum(textdatum_t::middle_center);
        canvas().setTextSize(FILE_PAGE_LABEL_TEXT_SIZE);
        canvas().drawString(page, canvas().width() / 2, FILE_PAGINATION_TOP + FILE_PAGINATION_HEIGHT / 2);
    } else {
        canvas().setTextColor(TFT_BLACK, TFT_WHITE);
        draw_centered_line(file_status_text(state.status), 360, 2U);
    }
    if (state.popup_visible) {
        canvas().fillRect(FILE_POPUP_LEFT, FILE_POPUP_TOP, FILE_POPUP_WIDTH, FILE_POPUP_HEIGHT, TFT_WHITE);
        canvas().drawRect(FILE_POPUP_LEFT, FILE_POPUP_TOP, FILE_POPUP_WIDTH, FILE_POPUP_HEIGHT, TFT_BLACK);
        canvas().setTextColor(TFT_BLACK, TFT_WHITE);
        draw_centered_line("File preview is not supported", FILE_POPUP_TOP + FILE_POPUP_HEIGHT / 2,
                           FILE_POPUP_TEXT_SIZE);
    }
}

void draw_full_view(
    const display_request& request,
    std::uint8_t selected_light,
    std::int16_t pressed_light)
{
    canvas().fillScreen(TFT_WHITE);
    canvas().setTextColor(TFT_BLACK, TFT_WHITE);
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
                draw_rtc_key(index, false);
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
            return {0, RTC_EDITOR_REGION_TOP, PAPER_MONO_PORTRAIT_WIDTH, RTC_EDITOR_REGION_HEIGHT};
        case display_update_region::test_content:
            return {0, TEST_CONTENT_REGION_TOP, PAPER_MONO_PORTRAIT_WIDTH, TEST_CONTENT_REGION_HEIGHT};
        case display_update_region::battery_content:
            return {0, BATTERY_CONTENT_REGION_TOP, PAPER_MONO_PORTRAIT_WIDTH,
                    BATTERY_CONTENT_REGION_HEIGHT};
        case display_update_region::file_content:
            return {0, FILE_CONTENT_REGION_TOP, PAPER_MONO_PORTRAIT_WIDTH, FILE_CONTENT_REGION_HEIGHT};
        case display_update_region::status_bar:
            return status_bar_rect();
        case display_update_region::full:
            return {0, 0, PAPER_MONO_PORTRAIT_WIDTH, PAPER_MONO_PORTRAIT_HEIGHT};
        case display_update_region::control:
            return {0, 0, PAPER_MONO_PORTRAIT_WIDTH, FRONT_LIGHT_BAR_HEIGHT};
    }
    return {0, 0, PAPER_MONO_PORTRAIT_WIDTH, PAPER_MONO_PORTRAIT_HEIGHT};
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
                    draw_rtc_key(index, false);
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
            return {0, 0, PAPER_MONO_PORTRAIT_WIDTH, FRONT_LIGHT_BAR_HEIGHT};
        case ui_control_type::menu_entry:
            draw_menu_entry(request.index, request.pressed);
            return menu_entry_rect(request.index);
        case ui_control_type::navigate_back:
            draw_back_button(latest.view, request.pressed);
            return back_button_rect(latest.view);
        case ui_control_type::rtc_key:
            draw_rtc_key(request.index, request.pressed);
            return rtc_key_rect(request.index);
        case ui_control_type::file_row:
            draw_file_row(latest.file, request.index, request.pressed);
            return file_row_rect(request.index);
        case ui_control_type::file_previous_page:
            draw_file_page_button(false, request.pressed);
            return file_previous_page_rect();
        case ui_control_type::file_next_page:
            draw_file_page_button(true, request.pressed);
            return file_next_page_rect();
        case ui_control_type::none:
        case ui_control_type::rtc_field:
        case ui_control_type::test_surface:
            break;
    }
    return {0, 0, 0, 0};
}

bool submit_request(const display_request& request)
{
    if (request_queue == nullptr) {
        return false;
    }
    display_request queued = request;
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
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        display_control_request control = {};
        while (xQueueReceive(control_queue, &control, 0) == pdTRUE) {
            if (!has_frame) {
                continue;
            }
            if (control.control == ui_control_type::front_light) {
                pressed_light = control.pressed ? control.index : no_pressed_button;
            }
            display_rect rect = draw_control(control, latest, selected_light, pressed_light);
            if (rect.width > 0 && rect.height > 0) {
                refresh_mode mode = resolve_mode(
                    control.mode, control.update_region, control.allow_quality_cleanup, debt);
                if (mode == refresh_mode::quality) {
                    draw_full_view(latest, selected_light, pressed_light);
                    displayed_status = status_bar_get_state();
                    draw_status_bar(displayed_status);
                    rect = {0, 0, PAPER_MONO_PORTRAIT_WIDTH, PAPER_MONO_PORTRAIT_HEIGHT};
                    status_displayed = true;
                }
                hal_display_refresh(rect, mode);
                record_refresh(debt, mode, control.update_region);
            }
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
        if (has_request) {
            if (quality_pending) {
                next.mode = refresh_mode::quality;
                next.update_region = display_update_region::full;
            }
            latest = next;
            selected_light = latest.view == ui_view_id::test
                                 ? latest.test.front_light_level
                                 : selected_light;
            refresh_mode mode = resolve_mode(
                next.mode, next.update_region, next.allow_quality_cleanup, debt);
            display_rect rect = content_rect(next.update_region);
            if (!has_frame || mode == refresh_mode::quality ||
                next.update_region == display_update_region::full) {
                mode = mode == refresh_mode::quality ? mode : next.mode;
                draw_full_view(next, selected_light, pressed_light);
                rect = {0, 0, PAPER_MONO_PORTRAIT_WIDTH, PAPER_MONO_PORTRAIT_HEIGHT};
            } else {
                draw_partial_request(next, rect);
            }
            const status_bar_view_state status = status_bar_get_state();
            if (!status_displayed || !status_states_equal(status, displayed_status) ||
                rect.height == PAPER_MONO_PORTRAIT_HEIGHT) {
                draw_status_bar(status);
                displayed_status = status;
                status_displayed = true;
                rect = merged_rect(rect, status_bar_rect());
            }
            hal_display_refresh(rect, mode);
            record_refresh(debt, mode, next.update_region);
            has_frame = true;
            ESP_LOGI(log_tag, "view refresh mode=%s view=%u region=%u",
                     refresh_mode_name(mode), static_cast<unsigned>(next.view),
                     static_cast<unsigned>(next.update_region));
        }

        const status_bar_view_state status = status_bar_get_state();
        if (has_frame && (!status_displayed || !status_states_equal(status, displayed_status))) {
            draw_status_bar(status);
            hal_display_refresh(status_bar_rect(), refresh_mode::fastest);
            record_refresh(debt, refresh_mode::fastest, display_update_region::status_bar);
            displayed_status = status;
            status_displayed = true;
        }
    }
}

display_request make_request(ui_view_id view, ui_update_reason reason)
{
    display_request request = {};
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
        request.minimum_refresh_interval_ms =
            state.touch_type == test_touch_display_type::long_press ? 250U : 0U;
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
        request.mode = refresh_mode::fast;
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
