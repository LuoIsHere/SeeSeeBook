#pragma once

#include <cstdint>

struct touch_sample {
    std::int16_t x;
    std::int16_t y;
    bool pressed;
};

// Reads one sample and converts PaperMono touch coordinates to portrait coordinates.
bool hal_touch_sample(touch_sample& sample);
