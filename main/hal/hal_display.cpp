#include "hal.hpp"

#include <algorithm>
#include <cstdio>

#include <M5Unified.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "status_bar.hpp"
#include "types.hpp"
#include "ui_config.hpp"

namespace {

constexpr char log_tag[] = "hal_display";
constexpr std::int16_t no_pressed_button = -1;
constexpr std::uint8_t empty_digit = 0xffU;
constexpr std::uint8_t front_light_values[FRONT_LIGHT_LEVEL_COUNT] = {
    FRONT_LIGHT_LEVEL_OFF,
    FRONT_LIGHT_LEVEL_25,
    FRONT_LIGHT_LEVEL_50,
    FRONT_LIGHT_LEVEL_75,
    FRONT_LIGHT_LEVEL_100,
};
constexpr const char* front_light_labels[FRONT_LIGHT_LEVEL_COUNT] = {
    "OFF",
    "25%",
    "50%",
    "75%",
    "100%",
};
constexpr const char* menu_entry_labels[MENU_ENTRY_COUNT] = {
    "Screen Setting",
    "RTC Setting",
    "Battery",
};

QueueHandle_t request_queue = nullptr;
QueueHandle_t control_queue = nullptr;
TaskHandle_t display_task_handle = nullptr;

struct ghost_debt {
    std::uint16_t control = 0;
    std::uint16_t rtc_editor = 0;
    std::uint16_t status_bar = 0;
    std::uint16_t test_content = 0;
    std::uint16_t battery_content = 0;
    bool cleanup_pending = false;
};

const char* main_text(ui_text_state text_state)
{
    return text_state == ui_text_state::hi_xi ? "HI XI" : "Hello world";
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

void apply_refresh_mode(refresh_mode mode)
{
    switch (mode) {
        case refresh_mode::fastest:
            M5.Display.setEpdMode(epd_mode_t::epd_fastest);
            break;
        case refresh_mode::fast:
            M5.Display.setEpdMode(epd_mode_t::epd_fast);
            break;
        case refresh_mode::quality:
            M5.Display.setEpdMode(epd_mode_t::epd_quality);
            break;
    }
}

std::uint16_t& debt_for_region(
    ghost_debt& debt,
    display_update_region update_region)
{
    switch (update_region) {
        case display_update_region::control:
            return debt.control;
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            return debt.rtc_editor;
        case display_update_region::status_bar:
            return debt.status_bar;
        case display_update_region::test_content:
            return debt.test_content;
        case display_update_region::battery_content:
            return debt.battery_content;
        case display_update_region::full:
            return debt.test_content;
    }
    return debt.test_content;
}

std::uint16_t debt_limit(display_update_region update_region)
{
    switch (update_region) {
        case display_update_region::control:
            return CONTROL_GHOST_DEBT_LIMIT;
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            return RTC_EDITOR_GHOST_DEBT_LIMIT;
        case display_update_region::status_bar:
            return STATUS_BAR_GHOST_DEBT_LIMIT;
        case display_update_region::test_content:
            return TEST_CONTENT_GHOST_DEBT_LIMIT;
        case display_update_region::battery_content:
            return BATTERY_CONTENT_GHOST_DEBT_LIMIT;
        case display_update_region::full:
            return TEST_CONTENT_GHOST_DEBT_LIMIT;
    }
    return TEST_CONTENT_GHOST_DEBT_LIMIT;
}

std::uint16_t debt_value_for_region(
    const ghost_debt& debt,
    display_update_region update_region)
{
    switch (update_region) {
        case display_update_region::control:
            return debt.control;
        case display_update_region::rtc_editor:
        case display_update_region::rtc_editor_and_key:
            return debt.rtc_editor;
        case display_update_region::status_bar:
            return debt.status_bar;
        case display_update_region::test_content:
            return debt.test_content;
        case display_update_region::battery_content:
            return debt.battery_content;
        case display_update_region::full:
            return 0U;
    }
    return 0U;
}

void update_cleanup_pending(ghost_debt& debt)
{
    debt.cleanup_pending =
        debt.control >= CONTROL_GHOST_DEBT_LIMIT ||
        debt.rtc_editor >= RTC_EDITOR_GHOST_DEBT_LIMIT ||
        debt.test_content >= TEST_CONTENT_GHOST_DEBT_LIMIT ||
        debt.battery_content >= BATTERY_CONTENT_GHOST_DEBT_LIMIT;
}

void record_completed_refresh(
    ghost_debt& debt,
    refresh_mode completed_mode,
    display_update_region update_region)
{
    const bool cleanup_was_pending = debt.cleanup_pending;
    if (completed_mode == refresh_mode::quality) {
        debt = {};
    } else {
        std::uint16_t& region_debt = debt_for_region(debt, update_region);
        if (completed_mode == refresh_mode::fastest) {
            if (region_debt < UINT16_MAX) {
                ++region_debt;
            }
        } else if (completed_mode == refresh_mode::fast) {
            region_debt = 0;
        }
        update_cleanup_pending(debt);
    }

    if (!cleanup_was_pending && debt.cleanup_pending) {
        ESP_LOGI(
            log_tag,
            "ghost cleanup pending control=%u rtc=%u status=%u test=%u battery=%u",
            static_cast<unsigned>(debt.control),
            static_cast<unsigned>(debt.rtc_editor),
            static_cast<unsigned>(debt.status_bar),
            static_cast<unsigned>(debt.test_content),
            static_cast<unsigned>(debt.battery_content));
    }
}

refresh_mode resolve_refresh_mode(
    refresh_mode requested_mode,
    display_update_region update_region,
    bool allow_quality_cleanup,
    const ghost_debt& debt)
{
    if (requested_mode == refresh_mode::quality) {
        return refresh_mode::quality;
    }
    if (update_region != display_update_region::status_bar &&
        allow_quality_cleanup &&
        debt_value_for_region(debt, update_region) >= debt_limit(update_region)) {
        return refresh_mode::quality;
    }
    return requested_mode;
}

bool status_bar_states_equal(
    const status_bar_state& left,
    const status_bar_state& right)
{
    const bool time_equal =
        left.time_valid == right.time_valid &&
        (!left.time_valid || (left.hour == right.hour && left.minute == right.minute));
    const bool battery_equal =
        left.battery.level_valid == right.battery.level_valid &&
        (!left.battery.level_valid || left.battery.percent == right.battery.percent) &&
        left.battery.charging_valid == right.battery.charging_valid &&
        (!left.battery.charging_valid || left.battery.charging == right.battery.charging);
    return time_equal && battery_equal;
}

void draw_centered_line(const char* text, std::int32_t y, std::uint8_t text_size)
{
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(text_size);
    M5.Display.drawString(text, M5.Display.width() / 2, y);
}

void draw_status_bar(const status_bar_state& state)
{
    char time_buffer[8];
    if (state.time_valid) {
        std::snprintf(
            time_buffer,
            sizeof(time_buffer),
            "%02u:%02u",
            static_cast<unsigned>(state.hour),
            static_cast<unsigned>(state.minute));
    } else {
        std::snprintf(time_buffer, sizeof(time_buffer), "--:--");
    }

    const ui_rect rect = status_bar_rect();
    M5.Display.fillRect(rect.left, rect.top, rect.width, rect.height, TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextSize(STATUS_BAR_TEXT_SIZE);
    M5.Display.drawString(
        time_buffer,
        STATUS_BAR_LEFT_MARGIN,
        STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);

    char battery_buffer[8];
    if (state.battery.level_valid) {
        std::snprintf(
            battery_buffer,
            sizeof(battery_buffer),
            "%u%%",
            static_cast<unsigned>(state.battery.percent));
    } else {
        std::snprintf(battery_buffer, sizeof(battery_buffer), "--%%");
    }
    const std::int16_t percent_right = static_cast<std::int16_t>(
        PAPER_MONO_PORTRAIT_WIDTH - STATUS_BAR_RIGHT_MARGIN);
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextSize(STATUS_BAR_TEXT_SIZE);
    M5.Display.drawString(
        battery_buffer,
        percent_right,
        STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);

    if (state.battery.charging_valid && state.battery.charging) {
        const std::int16_t icon_left = static_cast<std::int16_t>(
            percent_right - STATUS_BATTERY_PERCENT_MAX_WIDTH -
            STATUS_CHARGING_ICON_WIDTH - 8);
        const std::int16_t icon_top = static_cast<std::int16_t>(STATUS_BAR_TOP + 5);
        M5.Display.fillTriangle(
            icon_left + 16,
            icon_top,
            icon_left + 5,
            icon_top + 18,
            icon_left + 14,
            icon_top + 18,
            TFT_BLACK);
        M5.Display.fillTriangle(
            icon_left + 13,
            icon_top + 13,
            icon_left + 24,
            icon_top + 13,
            icon_left + 10,
            icon_top + 30,
            TFT_BLACK);
    }
}

void draw_front_light_bar(
    std::uint8_t selected_button,
    std::int16_t pressed_button)
{
    const std::int32_t display_width = M5.Display.width();
    M5.Display.fillRect(0, 0, display_width, FRONT_LIGHT_BAR_HEIGHT, TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(2);

    for (std::uint8_t index = 0; index < FRONT_LIGHT_LEVEL_COUNT; ++index) {
        const std::int32_t left = display_width * index / FRONT_LIGHT_LEVEL_COUNT;
        const std::int32_t right = display_width * (index + 1U) / FRONT_LIGHT_LEVEL_COUNT;
        const std::int32_t button_width = right - left;
        const bool is_pressed = pressed_button == static_cast<std::int16_t>(index);

        if (is_pressed) {
            M5.Display.fillRect(left, 0, button_width, FRONT_LIGHT_BAR_HEIGHT, TFT_BLACK);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        } else {
            M5.Display.fillRect(left, 0, button_width, FRONT_LIGHT_BAR_HEIGHT, TFT_WHITE);
            M5.Display.drawRect(left, 0, button_width, FRONT_LIGHT_BAR_HEIGHT, TFT_BLACK);
            if (index == selected_button) {
                M5.Display.drawRect(
                    left + 3,
                    3,
                    button_width - 6,
                    FRONT_LIGHT_BAR_HEIGHT - 6,
                    TFT_BLACK);
            }
            M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
        }
        M5.Display.drawString(
            front_light_labels[index],
            left + button_width / 2,
            FRONT_LIGHT_BAR_HEIGHT / 2);
    }
}

void draw_menu_entry(std::uint8_t entry_index, bool pressed)
{
    if (entry_index >= MENU_ENTRY_COUNT) {
        return;
    }
    const ui_rect rect = menu_entry_rect(entry_index);
    M5.Display.fillRect(
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.drawFastHLine(rect.left, rect.top, rect.width, TFT_BLACK);
    M5.Display.drawFastHLine(rect.left, rect.top + rect.height, rect.width, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(MENU_ENTRY_TEXT_SIZE);
    M5.Display.setTextColor(pressed ? TFT_WHITE : TFT_BLACK, pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.drawString(
        menu_entry_labels[entry_index],
        rect.left + rect.width / 2,
        rect.top + rect.height / 2);
}

void draw_app_back_button(const ui_rect& rect, bool pressed)
{
    M5.Display.fillRect(
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.drawRect(
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(APP_BACK_BUTTON_TEXT_SIZE);
    M5.Display.setTextColor(pressed ? TFT_WHITE : TFT_BLACK, pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.drawString(
        "< Back",
        rect.left + rect.width / 2,
        rect.top + rect.height / 2);
}

const char* rtc_message_text(const rtc_setting_view_state& state)
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

void format_rtc_field(
    const rtc_setting_view_state& state,
    rtc_edit_field field,
    char* output,
    std::size_t output_size)
{
    std::uint8_t start = 0U;
    std::uint8_t length = 0U;
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
            break;
    }

    const std::size_t writable_length = std::min<std::size_t>(length, output_size - 1U);
    for (std::size_t index = 0; index < writable_length; ++index) {
        const std::uint8_t digit = state.digits[start + index];
        output[index] = digit == empty_digit ? '-' : static_cast<char>('0' + digit);
    }
    output[writable_length] = '\0';
}

void draw_rtc_field(
    const rtc_setting_view_state& state,
    rtc_edit_field field)
{
    const ui_rect rect = rtc_field_rect(field);
    const bool selected = state.selected_field == field;
    char value[5] = {};
    format_rtc_field(state, field, value, sizeof(value));
    M5.Display.fillRect(
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        selected ? TFT_BLACK : TFT_WHITE);
    M5.Display.setTextColor(selected ? TFT_WHITE : TFT_BLACK, selected ? TFT_BLACK : TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(RTC_FIELD_TEXT_SIZE);
    M5.Display.drawString(value, rect.left + rect.width / 2, rect.top + rect.height / 2);
}

void draw_rtc_editor(const rtc_setting_view_state& state)
{
    M5.Display.fillRect(
        0,
        RTC_EDITOR_REGION_TOP,
        M5.Display.width(),
        RTC_EDITOR_REGION_HEIGHT,
        TFT_WHITE);
    constexpr rtc_edit_field fields[] = {
        rtc_edit_field::year,
        rtc_edit_field::month,
        rtc_edit_field::day,
        rtc_edit_field::hour,
        rtc_edit_field::minute,
        rtc_edit_field::second,
    };
    for (const rtc_edit_field field : fields) {
        draw_rtc_field(state, field);
    }

    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(RTC_FIELD_TEXT_SIZE);
    M5.Display.drawString(":", 231, RTC_DATE_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    M5.Display.drawString(":", 285, RTC_DATE_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    M5.Display.drawString(":", 213, RTC_TIME_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    M5.Display.drawString(":", 267, RTC_TIME_FIELD_TOP + RTC_FIELD_HEIGHT / 2);
    draw_centered_line(
        rtc_message_text(state),
        RTC_MESSAGE_CENTER_Y,
        RTC_MESSAGE_TEXT_SIZE);
}

void draw_rtc_key(std::uint8_t key_index, bool pressed)
{
    if (key_index >= RTC_KEY_COUNT) {
        return;
    }
    const ui_rect rect = rtc_key_rect(key_index);
    const std::uint32_t foreground = pressed ? TFT_WHITE : TFT_BLACK;
    const std::uint32_t background = pressed ? TFT_BLACK : TFT_WHITE;
    M5.Display.fillRect(rect.left, rect.top, rect.width, rect.height, background);
    M5.Display.drawFastHLine(rect.left, rect.top, rect.width, TFT_BLACK);
    M5.Display.drawFastHLine(rect.left, rect.top + rect.height - 1, rect.width, TFT_BLACK);

    const std::int32_t center_x = rect.left + rect.width / 2;
    const std::int32_t center_y = rect.top + rect.height / 2;
    M5.Display.setTextColor(foreground, background);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(RTC_KEY_TEXT_SIZE);
    if (key_index == 9U) {
        M5.Display.drawLine(center_x - 18, center_y, center_x - 5, center_y + 13, foreground);
        M5.Display.drawLine(center_x - 5, center_y + 13, center_x + 20, center_y - 16, foreground);
    } else if (key_index == 11U) {
        M5.Display.drawLine(center_x - 22, center_y, center_x + 22, center_y, foreground);
        M5.Display.drawLine(center_x - 22, center_y, center_x - 7, center_y - 14, foreground);
        M5.Display.drawLine(center_x - 22, center_y, center_x - 7, center_y + 14, foreground);
    } else {
        constexpr const char* labels[] = {
            "7",
            "8",
            "9",
            "4",
            "5",
            "6",
            "1",
            "2",
            "3",
            "",
            "0",
            "",
        };
        M5.Display.drawString(labels[key_index], center_x, center_y);
    }
}

void draw_menu_request()
{
    M5.Display.fillScreen(TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    draw_centered_line(PROJECT_NAME, MENU_TITLE_CENTER_Y, MENU_TITLE_TEXT_SIZE);
    for (std::uint8_t index = 0; index < MENU_ENTRY_COUNT; ++index) {
        draw_menu_entry(index, false);
    }
}

void draw_test_content(const display_request& request)
{
    const auto height = M5.Display.height();
    char line_buffer[64];

    M5.Display.fillRect(
        0,
        TEST_CONTENT_REGION_TOP,
        M5.Display.width(),
        TEST_CONTENT_REGION_HEIGHT,
        TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    draw_centered_line(main_text(request.text_state), height / 2, 4);

    if (request.touch_type == touch_display_type::click) {
        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "Point: (%d, %d)",
            request.end_x,
            request.end_y);
        draw_centered_line(line_buffer, height * 7 / 10, 2);
        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "Time: %lu ms",
            static_cast<unsigned long>(request.duration_ms));
        draw_centered_line(line_buffer, height * 3 / 4, 2);
    } else if (request.touch_type == touch_display_type::long_press) {
        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "Start: (%d, %d)",
            request.start_x,
            request.start_y);
        draw_centered_line(line_buffer, height * 13 / 20, 2);
        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "End: (%d, %d)",
            request.end_x,
            request.end_y);
        draw_centered_line(line_buffer, height * 7 / 10, 2);
        std::snprintf(
            line_buffer,
            sizeof(line_buffer),
            "Time: %lu ms",
            static_cast<unsigned long>(request.duration_ms));
        draw_centered_line(line_buffer, height * 3 / 4, 2);
    }
}

void draw_test_request(
    const display_request& request,
    std::uint8_t selected_button,
    std::int16_t pressed_button)
{
    M5.Display.fillScreen(TFT_WHITE);
    draw_front_light_bar(selected_button, pressed_button);
    draw_app_back_button(test_back_button_rect(), false);
    draw_test_content(request);
}

void draw_rtc_request(const display_request& request)
{
    M5.Display.fillScreen(TFT_WHITE);
    draw_app_back_button(rtc_back_button_rect(), false);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    draw_centered_line("RTC Setting", RTC_TITLE_CENTER_Y, RTC_TITLE_TEXT_SIZE);
    draw_rtc_editor(request.rtc_setting);
    for (std::uint8_t index = 0; index < RTC_KEY_COUNT; ++index) {
        draw_rtc_key(index, false);
    }
}

void draw_battery_row(
    const char* label,
    const char* value,
    std::uint8_t row_index)
{
    const std::int32_t center_y =
        BATTERY_FIRST_ROW_CENTER_Y + row_index * BATTERY_ROW_HEIGHT;
    M5.Display.setTextSize(BATTERY_ROW_TEXT_SIZE);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.drawString(label, BATTERY_LABEL_LEFT, center_y);
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.drawString(value, BATTERY_VALUE_RIGHT, center_y);
}

void draw_battery_content(const battery_view_state& state)
{
    M5.Display.fillRect(
        0,
        BATTERY_CONTENT_REGION_TOP,
        M5.Display.width(),
        BATTERY_CONTENT_REGION_HEIGHT,
        TFT_WHITE);
    if (state.loading) {
        M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
        draw_centered_line("Loading battery...", 300, 2);
        return;
    }

    char level_buffer[16];
    char voltage_buffer[16];
    char current_buffer[20];
    const char* current_label = "Current";
    const char* status_text = "Unknown";

    if (state.snapshot.level_valid) {
        std::snprintf(
            level_buffer,
            sizeof(level_buffer),
            "%u %%",
            static_cast<unsigned>(state.snapshot.percent));
    } else {
        std::snprintf(level_buffer, sizeof(level_buffer), "N/A");
    }
    if (state.snapshot.voltage_valid) {
        std::snprintf(
            voltage_buffer,
            sizeof(voltage_buffer),
            "%u.%02u V",
            static_cast<unsigned>(state.snapshot.voltage_mv / 1000U),
            static_cast<unsigned>((state.snapshot.voltage_mv % 1000U) / 10U));
    } else {
        std::snprintf(voltage_buffer, sizeof(voltage_buffer), "N/A");
    }
    if (state.snapshot.current_valid) {
        const std::int32_t current_ma = state.snapshot.current_ma;
        current_label = current_ma > 0 ? "Charge Current"
                                      : current_ma < 0 ? "Discharge Current" : "Current";
        std::snprintf(
            current_buffer,
            sizeof(current_buffer),
            "%ld mA",
            static_cast<long>(current_ma < 0 ? -current_ma : current_ma));
    } else {
        std::snprintf(current_buffer, sizeof(current_buffer), "N/A");
    }
    if (state.snapshot.charging_valid) {
        status_text = state.snapshot.charging ? "Charging" : "Not charging";
    }

    draw_battery_row("Level", level_buffer, 0U);
    draw_battery_row("Voltage", voltage_buffer, 1U);
    draw_battery_row(current_label, current_buffer, 2U);
    draw_battery_row("Status", status_text, 3U);
}

void draw_battery_request(const display_request& request)
{
    M5.Display.fillScreen(TFT_WHITE);
    draw_app_back_button(battery_back_button_rect(), false);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    draw_centered_line("Battery", BATTERY_TITLE_CENTER_Y, BATTERY_TITLE_TEXT_SIZE);
    draw_battery_content(request.battery);
}

void draw_request(
    const display_request& request,
    std::uint8_t selected_button,
    std::int16_t pressed_button)
{
    switch (request.view) {
        case display_view::menu:
            draw_menu_request();
            break;
        case display_view::test:
            draw_test_request(request, selected_button, pressed_button);
            break;
        case display_view::rtc_setting:
            draw_rtc_request(request);
            break;
        case display_view::battery:
            draw_battery_request(request);
            break;
    }
}

void refresh_full_screen(
    const display_request& request,
    refresh_mode active_mode,
    ghost_debt& debt,
    std::uint8_t selected_button,
    std::int16_t pressed_button,
    std::uint32_t& last_refresh_tick_ms,
    status_bar_state& displayed_status_bar)
{
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    draw_request(request, selected_button, pressed_button);
    displayed_status_bar = status_bar_get_state();
    draw_status_bar(displayed_status_bar);
    M5.Display.display();
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(debt, active_mode, request.update_region);
    ESP_LOGI(
        log_tag,
        "full refresh complete mode=%s view=%u region=%u cleanup_pending=%u",
        refresh_mode_name(active_mode),
        static_cast<unsigned>(request.view),
        static_cast<unsigned>(request.update_region),
        debt.cleanup_pending ? 1U : 0U);
}

ui_rect merged_rect(const ui_rect& first, const ui_rect& second)
{
    const std::int16_t left = std::min(first.left, second.left);
    const std::int16_t top = std::min(first.top, second.top);
    const std::int16_t right = std::max(
        static_cast<std::int16_t>(first.left + first.width),
        static_cast<std::int16_t>(second.left + second.width));
    const std::int16_t bottom = std::max(
        static_cast<std::int16_t>(first.top + first.height),
        static_cast<std::int16_t>(second.top + second.height));
    return {
        left,
        top,
        static_cast<std::int16_t>(right - left),
        static_cast<std::int16_t>(bottom - top),
    };
}

void refresh_rtc_editor(
    const display_request& request,
    refresh_mode active_mode,
    ghost_debt& debt,
    std::uint32_t& last_refresh_tick_ms)
{
    ui_rect rect = {
        0,
        RTC_EDITOR_REGION_TOP,
        static_cast<std::int16_t>(M5.Display.width()),
        RTC_EDITOR_REGION_HEIGHT,
    };
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    draw_rtc_editor(request.rtc_setting);
    if (request.update_region == display_update_region::rtc_editor_and_key) {
        for (std::uint8_t key_index = 0; key_index < RTC_KEY_COUNT; ++key_index) {
            if ((request.released_key_mask & (1U << key_index)) == 0U) {
                continue;
            }
            const ui_rect key_rect = rtc_key_rect(key_index);
            draw_rtc_key(key_index, false);
            rect = merged_rect(rect, key_rect);
        }
    }
    M5.Display.display(rect.left, rect.top, rect.width, rect.height);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(debt, active_mode, request.update_region);
}

void refresh_test_content(
    const display_request& request,
    refresh_mode active_mode,
    ghost_debt& debt,
    std::uint32_t& last_refresh_tick_ms)
{
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    draw_test_content(request);
    M5.Display.display(
        0,
        TEST_CONTENT_REGION_TOP,
        M5.Display.width(),
        TEST_CONTENT_REGION_HEIGHT);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(debt, active_mode, display_update_region::test_content);
}

void refresh_battery_content(
    const display_request& request,
    refresh_mode active_mode,
    ghost_debt& debt,
    std::uint32_t& last_refresh_tick_ms)
{
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    draw_battery_content(request.battery);
    M5.Display.display(
        0,
        BATTERY_CONTENT_REGION_TOP,
        M5.Display.width(),
        BATTERY_CONTENT_REGION_HEIGHT);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(debt, active_mode, display_update_region::battery_content);
}

void refresh_control_region(
    const display_control_request& request,
    refresh_mode active_mode,
    ghost_debt& debt,
    std::uint8_t selected_button,
    std::int16_t pressed_button,
    std::uint32_t& last_refresh_tick_ms)
{
    ui_rect rect = {0, 0, static_cast<std::int16_t>(M5.Display.width()), FRONT_LIGHT_BAR_HEIGHT};
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    switch (request.type) {
        case display_control_type::front_light:
            draw_front_light_bar(selected_button, pressed_button);
            break;
        case display_control_type::menu_entry:
            rect = menu_entry_rect(request.button_index);
            draw_menu_entry(request.button_index, request.pressed);
            ++rect.height;
            break;
        case display_control_type::app_back_button:
            rect = request.rect;
            draw_app_back_button(rect, request.pressed);
            break;
        case display_control_type::rtc_key:
            rect = rtc_key_rect(request.button_index);
            draw_rtc_key(request.button_index, request.pressed);
            break;
    }
    M5.Display.display(rect.left, rect.top, rect.width, rect.height);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(debt, active_mode, request.update_region);
}

void refresh_status_bar(
    ghost_debt& debt,
    std::uint32_t& last_refresh_tick_ms,
    status_bar_state& displayed_status_bar)
{
    constexpr refresh_mode active_mode = refresh_mode::fastest;
    const ui_rect rect = status_bar_rect();
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    displayed_status_bar = status_bar_get_state();
    draw_status_bar(displayed_status_bar);
    M5.Display.display(rect.left, rect.top, rect.width, rect.height);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(
        debt,
        active_mode,
        display_update_region::status_bar);
    ESP_LOGI(
        log_tag,
        "status bar refresh complete mode=%s debt=%u",
        refresh_mode_name(active_mode),
        static_cast<unsigned>(debt.status_bar));
}

void process_display_control_request(
    const display_control_request& request,
    const display_request& latest_request,
    ghost_debt& debt,
    std::uint8_t& selected_button,
    std::int16_t& pressed_button,
    std::uint32_t& last_refresh_tick_ms,
    status_bar_state& displayed_status_bar)
{
    if (request.type == display_control_type::front_light) {
        if (request.button_index >= FRONT_LIGHT_LEVEL_COUNT) {
            ESP_LOGW(log_tag, "invalid front light button index=%u", request.button_index);
            return;
        }
        pressed_button = request.pressed ? request.button_index : no_pressed_button;
        if (!request.pressed && request.apply_level) {
            selected_button = request.button_index;
            hal_set_front_light(front_light_values[selected_button]);
        }
    } else if (request.type == display_control_type::menu_entry &&
               request.button_index >= MENU_ENTRY_COUNT) {
        return;
    } else if (request.type == display_control_type::rtc_key &&
               request.button_index >= RTC_KEY_COUNT) {
        return;
    }

    const refresh_mode active_mode = request.pressed
                                         ? refresh_mode::fastest
                                         : resolve_refresh_mode(
                                               request.mode,
                                               request.update_region,
                                               request.allow_quality_cleanup,
                                               debt);
    if (active_mode == refresh_mode::quality) {
        refresh_control_region(
            request,
            refresh_mode::fastest,
            debt,
            selected_button,
            pressed_button,
            last_refresh_tick_ms);
        refresh_full_screen(
            latest_request,
            refresh_mode::quality,
            debt,
            selected_button,
            pressed_button,
            last_refresh_tick_ms,
            displayed_status_bar);
        return;
    }
    refresh_control_region(
        request,
        active_mode,
        debt,
        selected_button,
        pressed_button,
        last_refresh_tick_ms);
}

bool display_request_is_due(
    const display_request& request,
    std::uint32_t last_refresh_tick_ms,
    bool has_refreshed)
{
    return !has_refreshed || request.minimum_refresh_interval_ms == 0U ||
           hal_get_tick_ms() - last_refresh_tick_ms >= request.minimum_refresh_interval_ms;
}

TickType_t request_wait_ticks(
    const display_request& request,
    std::uint32_t last_refresh_tick_ms,
    bool has_refreshed)
{
    if (display_request_is_due(request, last_refresh_tick_ms, has_refreshed)) {
        return 0;
    }
    const std::uint32_t elapsed_ms = hal_get_tick_ms() - last_refresh_tick_ms;
    return pdMS_TO_TICKS(request.minimum_refresh_interval_ms - elapsed_ms);
}

TickType_t status_wait_ticks(std::uint32_t status_schedule_tick_ms)
{
    const std::uint32_t elapsed_ms = hal_get_tick_ms() - status_schedule_tick_ms;
    if (elapsed_ms >= STATUS_BAR_IDLE_REFRESH_INTERVAL_MS) {
        return 0;
    }
    return pdMS_TO_TICKS(STATUS_BAR_IDLE_REFRESH_INTERVAL_MS - elapsed_ms);
}

void display_task(void*)
{
    ghost_debt debt;
    display_request latest_request = {};
    display_request pending_request = {};
    std::uint8_t selected_button = FRONT_LIGHT_DEFAULT_LEVEL_INDEX;
    std::int16_t pressed_button = no_pressed_button;
    std::uint32_t last_refresh_tick_ms = 0;
    std::uint32_t status_schedule_tick_ms = hal_get_tick_ms();
    status_bar_state displayed_status_bar = {};
    bool has_pending_request = false;
    bool has_refreshed = false;
    bool has_displayed_status_bar = false;

    latest_request.view = display_view::menu;
    latest_request.mode = refresh_mode::quality;
    latest_request.update_region = display_update_region::full;
    latest_request.touch_type = touch_display_type::none;

    for (;;) {
        TickType_t wait_ticks = status_wait_ticks(status_schedule_tick_ms);
        if (has_pending_request) {
            wait_ticks = std::min(
                wait_ticks,
                request_wait_ticks(pending_request, last_refresh_tick_ms, has_refreshed));
        }
        ulTaskNotifyTake(pdTRUE, wait_ticks);

        bool ui_refreshed = false;
        display_control_request control_request = {};
        while (xQueueReceive(control_queue, &control_request, 0) == pdTRUE) {
            process_display_control_request(
                control_request,
                latest_request,
                debt,
                selected_button,
                pressed_button,
                last_refresh_tick_ms,
                displayed_status_bar);
            has_refreshed = true;
            ui_refreshed = true;
        }

        display_request newer_request = {};
        while (xQueueReceive(request_queue, &newer_request, 0) == pdTRUE) {
            if (has_pending_request) {
                newer_request.released_key_mask |= pending_request.released_key_mask;
            }
            // Render only the newest content, but never lose an earlier lifecycle Quality request.
            if (has_pending_request && pending_request.mode == refresh_mode::quality) {
                newer_request.mode = refresh_mode::quality;
                newer_request.update_region = display_update_region::full;
            }
            pending_request = newer_request;
            has_pending_request = true;
        }

        if (has_pending_request &&
            display_request_is_due(pending_request, last_refresh_tick_ms, has_refreshed)) {
            const bool rtc_page_is_current =
                has_refreshed && latest_request.view == display_view::rtc_setting;
            const bool test_page_is_current =
                has_refreshed && latest_request.view == display_view::test;
            const bool battery_page_is_current =
                has_refreshed && latest_request.view == display_view::battery;
            latest_request = pending_request;
            const refresh_mode active_mode = resolve_refresh_mode(
                pending_request.mode,
                pending_request.update_region,
                pending_request.allow_quality_cleanup,
                debt);
            const bool partial_rtc_editor =
                pending_request.view == display_view::rtc_setting &&
                (pending_request.update_region == display_update_region::rtc_editor ||
                 pending_request.update_region == display_update_region::rtc_editor_and_key) &&
                rtc_page_is_current &&
                active_mode != refresh_mode::quality;
            const bool partial_test_content =
                pending_request.view == display_view::test &&
                pending_request.update_region == display_update_region::test_content &&
                test_page_is_current &&
                active_mode == refresh_mode::fastest;
            const bool partial_battery_content =
                pending_request.view == display_view::battery &&
                pending_request.update_region == display_update_region::battery_content &&
                battery_page_is_current &&
                active_mode == refresh_mode::fastest;
            if (partial_rtc_editor) {
                refresh_rtc_editor(
                    pending_request,
                    active_mode,
                    debt,
                    last_refresh_tick_ms);
            } else if (partial_test_content) {
                refresh_test_content(
                    pending_request,
                    active_mode,
                    debt,
                    last_refresh_tick_ms);
            } else if (partial_battery_content) {
                refresh_battery_content(
                    pending_request,
                    active_mode,
                    debt,
                    last_refresh_tick_ms);
            } else {
                refresh_full_screen(
                    pending_request,
                    active_mode,
                    debt,
                    selected_button,
                    pressed_button,
                    last_refresh_tick_ms,
                    displayed_status_bar);
                has_displayed_status_bar = true;
            }
            has_pending_request = false;
            has_refreshed = true;
            ui_refreshed = true;
        }

        if (ui_refreshed) {
            status_schedule_tick_ms = hal_get_tick_ms();
        }

        const status_bar_state current_status_bar = status_bar_get_state();
        const bool status_bar_changed =
            !has_displayed_status_bar ||
            !status_bar_states_equal(current_status_bar, displayed_status_bar);
        const bool status_due =
            hal_get_tick_ms() - status_schedule_tick_ms >= STATUS_BAR_IDLE_REFRESH_INTERVAL_MS;
        if (status_bar_changed || status_due) {
            if (status_bar_changed) {
                refresh_status_bar(
                    debt,
                    last_refresh_tick_ms,
                    displayed_status_bar);
                has_refreshed = true;
                has_displayed_status_bar = true;
            }
            status_schedule_tick_ms = hal_get_tick_ms();
        }
    }
}

}  // namespace

bool hal_display_start(
    QueueHandle_t target_request_queue,
    QueueHandle_t target_control_queue,
    TaskHandle_t& task_handle)
{
    request_queue = target_request_queue;
    control_queue = target_control_queue;
    const bool started = xTaskCreate(
                             display_task,
                             "display_task",
                             DISPLAY_TASK_STACK_SIZE,
                             nullptr,
                             DISPLAY_TASK_PRIORITY,
                             &task_handle) == pdPASS;
    if (started) {
        display_task_handle = task_handle;
        ESP_LOGI(log_tag, "display task started");
    }
    return started;
}

void hal_request_status_bar_refresh()
{
    if (display_task_handle != nullptr) {
        xTaskNotifyGive(display_task_handle);
    }
}
