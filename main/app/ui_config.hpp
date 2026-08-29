#pragma once

#include <cstdint>

#define MENU_SCREEN_EDGE_MARGIN 32
#define MENU_TITLE_CENTER_Y 96
#define MENU_TITLE_TEXT_SIZE 4U
#define MENU_ENTRY_TOP 190
#define MENU_ENTRY_HEIGHT 96
#define MENU_ENTRY_TEXT_SIZE 3U

#define TEST_BACK_BUTTON_LEFT 24
#define TEST_BACK_BUTTON_TOP 720
#define TEST_BACK_BUTTON_WIDTH 136
#define TEST_BACK_BUTTON_HEIGHT 56
#define TEST_BACK_BUTTON_TEXT_SIZE 2U

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
