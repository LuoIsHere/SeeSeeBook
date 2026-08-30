#pragma once

#include <cstdint>

struct display_rect {
    std::int16_t left;
    std::int16_t top;
    std::int16_t width;
    std::int16_t height;
};

constexpr bool point_in_rect(
    std::int16_t x,
    std::int16_t y,
    const display_rect& rect)
{
    return x >= rect.left && x < rect.left + rect.width &&
           y >= rect.top && y < rect.top + rect.height;
}
