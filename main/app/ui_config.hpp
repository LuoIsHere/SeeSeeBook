#pragma once

#include <cstdint>

#include "hal_display.hpp"

#define MENU_SCREEN_EDGE_MARGIN 32
#define MENU_TITLE_CENTER_Y 96
#define MENU_TITLE_TEXT_SIZE 4U
#define MENU_ENTRY_TOP 190
#define MENU_ENTRY_HEIGHT 96
#define MENU_ENTRY_TEXT_SIZE 3U
#define MENU_ENTRY_COUNT 3U

#define RTC_BACK_BUTTON_LEFT 20
#define RTC_BACK_BUTTON_TOP 20
#define RTC_TITLE_CENTER_Y 44
#define RTC_TITLE_TEXT_SIZE 2U
#define RTC_DATE_FIELD_TOP 104
#define RTC_TIME_FIELD_TOP 184
#define RTC_FIELD_HEIGHT 48
#define RTC_FIELD_TEXT_SIZE 3U
#define RTC_MESSAGE_CENTER_Y 292
#define RTC_MESSAGE_TEXT_SIZE 2U
#define RTC_EDITOR_REGION_TOP 88
#define RTC_EDITOR_REGION_HEIGHT 232
#define RTC_KEYPAD_LEFT 36
#define RTC_KEYPAD_TOP 360
#define RTC_KEY_WIDTH 120
#define RTC_KEY_HEIGHT 72
#define RTC_KEY_COLUMN_GAP 24
#define RTC_KEY_ROW_GAP 16
#define RTC_KEY_TEXT_SIZE 3U
#define RTC_KEY_COUNT 12U

#define STATUS_BAR_TOP 760
#define STATUS_BAR_HEIGHT 40
#define STATUS_BAR_LEFT_MARGIN 16
#define STATUS_BAR_RIGHT_MARGIN 12
#define STATUS_BAR_TEXT_SIZE 2U
#define STATUS_BATTERY_PERCENT_MAX_WIDTH 52
#define STATUS_CHARGING_ICON_WIDTH 28
#define STATUS_BAR_IDLE_REFRESH_INTERVAL_MS 60000U

#define APP_BACK_BUTTON_WIDTH 104
#define APP_BACK_BUTTON_HEIGHT 48
#define APP_BACK_BUTTON_TEXT_SIZE 2U
#define TEST_BACK_BUTTON_LEFT 20
#define TEST_BACK_BUTTON_TOP 88
#define TEST_CONTENT_REGION_TOP 152
#define TEST_CONTENT_REGION_HEIGHT (STATUS_BAR_TOP - TEST_CONTENT_REGION_TOP)

#define BATTERY_TITLE_CENTER_Y 48
#define BATTERY_TITLE_TEXT_SIZE 3U
#define BATTERY_CONTENT_REGION_TOP 96
#define BATTERY_CONTENT_REGION_HEIGHT (STATUS_BAR_TOP - BATTERY_CONTENT_REGION_TOP)
#define BATTERY_LABEL_LEFT 48
#define BATTERY_VALUE_RIGHT 432
#define BATTERY_FIRST_ROW_CENTER_Y 190
#define BATTERY_ROW_HEIGHT 72
#define BATTERY_ROW_TEXT_SIZE 2U

// Returns true when a normalized screen coordinate is inside a half-open rectangle.
constexpr bool ui_point_in_rect(
    std::int16_t x,
    std::int16_t y,
    std::int16_t left,
    std::int16_t top,
    std::int16_t width,
    std::int16_t height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

constexpr bool ui_point_in_rect(std::int16_t x, std::int16_t y, const ui_rect& rect)
{
    return ui_point_in_rect(x, y, rect.left, rect.top, rect.width, rect.height);
}

constexpr ui_rect menu_entry_rect(std::uint8_t index)
{
    return {
        MENU_SCREEN_EDGE_MARGIN,
        static_cast<std::int16_t>(MENU_ENTRY_TOP + index * MENU_ENTRY_HEIGHT),
        static_cast<std::int16_t>(PAPER_MONO_PORTRAIT_WIDTH - MENU_SCREEN_EDGE_MARGIN * 2),
        MENU_ENTRY_HEIGHT,
    };
}

constexpr ui_rect rtc_back_button_rect()
{
    return {
        RTC_BACK_BUTTON_LEFT,
        RTC_BACK_BUTTON_TOP,
        APP_BACK_BUTTON_WIDTH,
        APP_BACK_BUTTON_HEIGHT,
    };
}

constexpr ui_rect test_back_button_rect()
{
    return {
        TEST_BACK_BUTTON_LEFT,
        TEST_BACK_BUTTON_TOP,
        APP_BACK_BUTTON_WIDTH,
        APP_BACK_BUTTON_HEIGHT,
    };
}

constexpr ui_rect battery_back_button_rect()
{
    return {
        RTC_BACK_BUTTON_LEFT,
        RTC_BACK_BUTTON_TOP,
        APP_BACK_BUTTON_WIDTH,
        APP_BACK_BUTTON_HEIGHT,
    };
}

constexpr ui_rect status_bar_rect()
{
    return {
        0,
        STATUS_BAR_TOP,
        PAPER_MONO_PORTRAIT_WIDTH,
        STATUS_BAR_HEIGHT,
    };
}

constexpr ui_rect rtc_key_rect(std::uint8_t index)
{
    const std::uint8_t row = index / 3U;
    const std::uint8_t column = index % 3U;
    return {
        static_cast<std::int16_t>(
            RTC_KEYPAD_LEFT + column * (RTC_KEY_WIDTH + RTC_KEY_COLUMN_GAP)),
        static_cast<std::int16_t>(
            RTC_KEYPAD_TOP + row * (RTC_KEY_HEIGHT + RTC_KEY_ROW_GAP)),
        RTC_KEY_WIDTH,
        RTC_KEY_HEIGHT,
    };
}

constexpr ui_rect rtc_field_rect(rtc_edit_field field)
{
    switch (field) {
        case rtc_edit_field::year:
            return {150, RTC_DATE_FIELD_TOP, 72, RTC_FIELD_HEIGHT};
        case rtc_edit_field::month:
            return {240, RTC_DATE_FIELD_TOP, 36, RTC_FIELD_HEIGHT};
        case rtc_edit_field::day:
            return {294, RTC_DATE_FIELD_TOP, 36, RTC_FIELD_HEIGHT};
        case rtc_edit_field::hour:
            return {168, RTC_TIME_FIELD_TOP, 36, RTC_FIELD_HEIGHT};
        case rtc_edit_field::minute:
            return {222, RTC_TIME_FIELD_TOP, 36, RTC_FIELD_HEIGHT};
        case rtc_edit_field::second:
            return {276, RTC_TIME_FIELD_TOP, 36, RTC_FIELD_HEIGHT};
        case rtc_edit_field::none:
            return {0, 0, 0, 0};
    }
    return {0, 0, 0, 0};
}
