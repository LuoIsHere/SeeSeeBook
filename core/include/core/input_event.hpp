#pragma once

#include <cstdint>
#include <type_traits>

enum class input_gesture_type : std::uint8_t {
    press,
    click,
    long_press_start,
    long_press_repeat,
    long_press_end,
};

struct input_event {
    input_gesture_type gesture;
    std::int16_t start_x;
    std::int16_t start_y;
    std::int16_t end_x;
    std::int16_t end_y;
    std::uint32_t duration_ms;
    std::uint32_t timestamp_ms;
};

static_assert(std::is_trivially_copyable_v<input_event>);
