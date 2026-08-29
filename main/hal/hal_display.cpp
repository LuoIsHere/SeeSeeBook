#include "hal.hpp"

#include <algorithm>
#include <cstdio>

#include <M5Unified.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "system_overlay.hpp"
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
    "Test",
    "RTC Setting",
};

QueueHandle_t request_queue = nullptr;
QueueHandle_t control_queue = nullptr;
TaskHandle_t display_task_handle = nullptr;

struct refresh_policy {
    std::uint8_t fastest_count = 0;
    std::uint8_t fast_count = 0;
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

refresh_mode select_refresh_mode(const refresh_policy& policy)
{
    if (policy.fast_count >= EPD_FAST_REFRESH_LIMIT) {
        return refresh_mode::quality;
    }
    if (policy.fastest_count >= EPD_FASTEST_REFRESH_LIMIT) {
        return refresh_mode::fast;
    }
    return refresh_mode::fastest;
}

void record_completed_refresh(refresh_policy& policy, refresh_mode completed_mode)
{
    switch (completed_mode) {
        case refresh_mode::fastest:
            ++policy.fastest_count;
            break;
        case refresh_mode::fast:
            policy.fastest_count = 0;
            ++policy.fast_count;
            break;
        case refresh_mode::quality:
            policy.fastest_count = 0;
            policy.fast_count = 0;
            break;
    }
}

bool overlay_states_equal(
    const system_overlay_state& left,
    const system_overlay_state& right)
{
    return left.time_valid == right.time_valid &&
           (!left.time_valid || (left.hour == right.hour && left.minute == right.minute));
}

void draw_centered_line(const char* text, std::int32_t y, std::uint8_t text_size)
{
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(text_size);
    M5.Display.drawString(text, M5.Display.width() / 2, y);
}

void draw_status_bar(const system_overlay_state& overlay)
{
    char time_buffer[8];
    if (overlay.time_valid) {
        std::snprintf(
            time_buffer,
            sizeof(time_buffer),
            "%02u:%02u",
            static_cast<unsigned>(overlay.hour),
            static_cast<unsigned>(overlay.minute));
    } else {
        std::snprintf(time_buffer, sizeof(time_buffer), "--:--");
    }

    M5.Display.fillRect(0, STATUS_BAR_TOP, M5.Display.width(), STATUS_BAR_HEIGHT, TFT_WHITE);
    M5.Display.drawFastHLine(0, STATUS_BAR_TOP, M5.Display.width(), TFT_BLACK);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextSize(STATUS_BAR_TEXT_SIZE);
    M5.Display.drawString(
        time_buffer,
        STATUS_BAR_LEFT_MARGIN,
        STATUS_BAR_TOP + STATUS_BAR_HEIGHT / 2);
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

void draw_test_back_button(bool pressed)
{
    M5.Display.fillRect(
        TEST_BACK_BUTTON_LEFT,
        TEST_BACK_BUTTON_TOP,
        TEST_BACK_BUTTON_WIDTH,
        TEST_BACK_BUTTON_HEIGHT,
        pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.drawRect(
        TEST_BACK_BUTTON_LEFT,
        TEST_BACK_BUTTON_TOP,
        TEST_BACK_BUTTON_WIDTH,
        TEST_BACK_BUTTON_HEIGHT,
        TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(TEST_BACK_BUTTON_TEXT_SIZE);
    M5.Display.setTextColor(pressed ? TFT_WHITE : TFT_BLACK, pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.drawString(
        "< Back",
        TEST_BACK_BUTTON_LEFT + TEST_BACK_BUTTON_WIDTH / 2,
        TEST_BACK_BUTTON_TOP + TEST_BACK_BUTTON_HEIGHT / 2);
}

void draw_rtc_back_button(bool pressed)
{
    const ui_rect rect = rtc_back_button_rect();
    M5.Display.fillRect(
        rect.left,
        rect.top,
        rect.width,
        rect.height,
        pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.drawRect(rect.left, rect.top, rect.width, rect.height, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(pressed ? TFT_WHITE : TFT_BLACK, pressed ? TFT_BLACK : TFT_WHITE);
    M5.Display.drawString("< Back", rect.left + rect.width / 2, rect.top + rect.height / 2);
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

void draw_test_request(
    const display_request& request,
    std::uint8_t selected_button,
    std::int16_t pressed_button)
{
    const auto height = M5.Display.height();
    char line_buffer[64];

    M5.Display.fillScreen(TFT_WHITE);
    draw_front_light_bar(selected_button, pressed_button);
    draw_test_back_button(false);
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

void draw_rtc_request(const display_request& request)
{
    M5.Display.fillScreen(TFT_WHITE);
    draw_rtc_back_button(false);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    draw_centered_line("RTC Setting", RTC_TITLE_CENTER_Y, RTC_TITLE_TEXT_SIZE);
    draw_rtc_editor(request.rtc_setting);
    for (std::uint8_t index = 0; index < RTC_KEY_COUNT; ++index) {
        draw_rtc_key(index, false);
    }
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
    }
}

void refresh_full_screen(
    const display_request& request,
    refresh_mode active_mode,
    refresh_policy& policy,
    std::uint8_t selected_button,
    std::int16_t pressed_button,
    std::uint32_t& last_refresh_tick_ms,
    system_overlay_state& displayed_overlay)
{
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    draw_request(request, selected_button, pressed_button);
    displayed_overlay = system_overlay_get_state();
    draw_status_bar(displayed_overlay);
    M5.Display.display();
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(policy, active_mode);
    ESP_LOGI(
        log_tag,
        "full refresh complete mode=%s view=%u fastest_count=%u fast_count=%u",
        refresh_mode_name(active_mode),
        static_cast<unsigned>(request.view),
        static_cast<unsigned>(policy.fastest_count),
        static_cast<unsigned>(policy.fast_count));
}

void refresh_rtc_editor(
    const display_request& request,
    refresh_mode active_mode,
    refresh_policy& policy,
    std::uint32_t& last_refresh_tick_ms)
{
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    draw_rtc_editor(request.rtc_setting);
    M5.Display.display(
        0,
        RTC_EDITOR_REGION_TOP,
        M5.Display.width(),
        RTC_EDITOR_REGION_HEIGHT);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(policy, active_mode);
}

void refresh_control_region(
    const display_control_request& request,
    refresh_mode active_mode,
    refresh_policy& policy,
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
        case display_control_type::back_button:
            rect = {
                TEST_BACK_BUTTON_LEFT,
                TEST_BACK_BUTTON_TOP,
                TEST_BACK_BUTTON_WIDTH,
                TEST_BACK_BUTTON_HEIGHT,
            };
            draw_test_back_button(request.pressed);
            break;
        case display_control_type::rtc_back_button:
            rect = rtc_back_button_rect();
            draw_rtc_back_button(request.pressed);
            break;
        case display_control_type::rtc_key:
            rect = rtc_key_rect(request.button_index);
            draw_rtc_key(request.button_index, request.pressed);
            break;
    }
    M5.Display.display(rect.left, rect.top, rect.width, rect.height);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(policy, active_mode);
}

void refresh_status_bar(
    refresh_policy& policy,
    std::uint32_t& last_refresh_tick_ms,
    system_overlay_state& displayed_overlay)
{
    const refresh_mode active_mode = select_refresh_mode(policy);
    M5.Display.waitDisplay();
    apply_refresh_mode(active_mode);
    displayed_overlay = system_overlay_get_state();
    draw_status_bar(displayed_overlay);
    // Clock-only updates must never expand into a full-screen EPD refresh.
    M5.Display.display(0, STATUS_BAR_TOP, M5.Display.width(), STATUS_BAR_HEIGHT);
    M5.Display.waitDisplay();
    last_refresh_tick_ms = hal_get_tick_ms();
    record_completed_refresh(policy, active_mode);
    ESP_LOGI(log_tag, "status bar refresh complete mode=%s", refresh_mode_name(active_mode));
}

void process_display_control_request(
    const display_control_request& request,
    const display_request& latest_request,
    refresh_policy& policy,
    std::uint8_t& selected_button,
    std::int16_t& pressed_button,
    std::uint32_t& last_refresh_tick_ms,
    system_overlay_state& displayed_overlay)
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
                                         : select_refresh_mode(policy);
    if (active_mode == refresh_mode::quality) {
        refresh_control_region(
            request,
            refresh_mode::fastest,
            policy,
            selected_button,
            pressed_button,
            last_refresh_tick_ms);
        refresh_full_screen(
            latest_request,
            refresh_mode::quality,
            policy,
            selected_button,
            pressed_button,
            last_refresh_tick_ms,
            displayed_overlay);
        return;
    }
    refresh_control_region(
        request,
        active_mode,
        policy,
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
    refresh_policy policy;
    display_request latest_request = {};
    display_request pending_request = {};
    std::uint8_t selected_button = FRONT_LIGHT_DEFAULT_LEVEL_INDEX;
    std::int16_t pressed_button = no_pressed_button;
    std::uint32_t last_refresh_tick_ms = 0;
    std::uint32_t status_schedule_tick_ms = hal_get_tick_ms();
    system_overlay_state displayed_overlay = {};
    bool has_pending_request = false;
    bool has_refreshed = false;
    bool has_displayed_overlay = false;

    latest_request.view = display_view::menu;
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
                policy,
                selected_button,
                pressed_button,
                last_refresh_tick_ms,
                displayed_overlay);
            has_refreshed = true;
            ui_refreshed = true;
        }

        display_request newer_request = {};
        while (xQueueReceive(request_queue, &newer_request, 0) == pdTRUE) {
            // Render only the newest content, but never lose an earlier lifecycle Quality request.
            if (has_pending_request && pending_request.force_quality) {
                newer_request.force_quality = true;
                newer_request.update_region = display_update_region::full;
            }
            pending_request = newer_request;
            has_pending_request = true;
        }

        if (has_pending_request &&
            display_request_is_due(pending_request, last_refresh_tick_ms, has_refreshed)) {
            const bool rtc_page_is_current =
                has_refreshed && latest_request.view == display_view::rtc_setting;
            latest_request = pending_request;
            const refresh_mode active_mode = pending_request.force_quality
                                                 ? refresh_mode::quality
                                                 : select_refresh_mode(policy);
            const bool partial_rtc_editor =
                pending_request.view == display_view::rtc_setting &&
                pending_request.update_region == display_update_region::rtc_editor &&
                rtc_page_is_current &&
                active_mode != refresh_mode::quality;
            if (partial_rtc_editor) {
                refresh_rtc_editor(
                    pending_request,
                    active_mode,
                    policy,
                    last_refresh_tick_ms);
            } else {
                refresh_full_screen(
                    pending_request,
                    active_mode,
                    policy,
                    selected_button,
                    pressed_button,
                    last_refresh_tick_ms,
                    displayed_overlay);
                has_displayed_overlay = true;
            }
            has_pending_request = false;
            has_refreshed = true;
            ui_refreshed = true;
        }

        if (ui_refreshed) {
            status_schedule_tick_ms = hal_get_tick_ms();
        }

        const system_overlay_state current_overlay = system_overlay_get_state();
        const bool overlay_changed =
            !has_displayed_overlay || !overlay_states_equal(current_overlay, displayed_overlay);
        const bool status_due =
            hal_get_tick_ms() - status_schedule_tick_ms >= STATUS_BAR_IDLE_REFRESH_INTERVAL_MS;
        if (overlay_changed || status_due) {
            if (overlay_changed) {
                refresh_status_bar(
                    policy,
                    last_refresh_tick_ms,
                    displayed_overlay);
                has_refreshed = true;
                has_displayed_overlay = true;
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
