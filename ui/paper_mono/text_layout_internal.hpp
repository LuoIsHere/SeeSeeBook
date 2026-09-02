#pragma once

#include <cstddef>
#include <cstdint>

#include "display.hpp"

// Rendering and pagination share the same immutable font metrics/fallback.
void paper_mono_draw_cjk_text(
    display_surface& surface, const char* text, std::size_t length,
    std::int16_t x, std::int16_t center_y);
