#pragma once

#include <cstdint>

enum class test_text_state : std::uint8_t {
    hi_xi,
    hello_world,
};

enum class test_touch_display_type : std::uint8_t {
    none,
    click,
    long_press,
};

struct test_view_state {
    test_text_state text_state;
    test_touch_display_type touch_type;
    std::int16_t start_x;
    std::int16_t start_y;
    std::int16_t end_x;
    std::int16_t end_y;
    std::uint32_t duration_ms;
    std::uint32_t timestamp_ms;
    std::uint8_t front_light_level;
};
