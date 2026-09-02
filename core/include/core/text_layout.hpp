#pragma once

#include <cstdint>

// Logical width units supplied by the UI backend, never device coordinates.
using text_glyph_width = std::uint16_t (*)(std::uint32_t codepoint);

struct text_layout_profile {
    std::uint16_t line_width;
    std::uint16_t line_count;
    text_glyph_width glyph_width;
};
