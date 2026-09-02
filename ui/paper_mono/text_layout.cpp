#include "text_layout_provider.hpp"

#include <algorithm>

#include <lgfx/v1/lgfx_fonts.hpp>

#include "layout.hpp"
#include "text_layout_internal.hpp"
#include "utf8.hpp"

namespace {

std::uint32_t drawable_codepoint(std::uint32_t codepoint, lgfx::FontMetrics& metrics)
{
    const auto& font = fonts::efontCN_24;
    font.getDefaultMetric(&metrics);
    if (codepoint > 0xffffU || !font.updateFontMetric(&metrics, codepoint)) {
        codepoint = '?';
        font.updateFontMetric(&metrics, codepoint);
    }
    return codepoint;
}

std::uint16_t glyph_width(std::uint32_t codepoint)
{
    lgfx::FontMetrics metrics = {};
    drawable_codepoint(codepoint, metrics);
    return std::max<int>(1, std::max<int>(metrics.x_advance, metrics.width + metrics.x_offset));
}

}  // namespace

text_layout_profile ui_reader_text_layout()
{
    return {UI_DISPLAY_WIDTH - READER_MARGIN * 2, READER_LINE_COUNT, glyph_width};
}

text_layout_profile ui_file_name_text_layout()
{
    return {FILE_ROW_WIDTH - 36, 1U, glyph_width};
}

void paper_mono_draw_cjk_text(
    display_surface& surface, const char* text, std::size_t length,
    std::int16_t x, std::int16_t center_y)
{
    surface.set_font(display_font::cjk_24);
    surface.set_text_size(1U);
    surface.set_text_alignment(display_text_alignment::middle_left);
    for (std::size_t offset = 0U; offset < length;) {
        std::uint32_t codepoint = 0U;
        std::size_t size = 0U;
        if (utf8_decode(text + offset, length - offset, codepoint, size) !=
            utf8_decode_result::complete) {
            break;
        }
        const auto advance = glyph_width(codepoint);
        lgfx::FontMetrics metrics = {};
        char encoded[5] = {};
        utf8_encode(drawable_codepoint(codepoint, metrics), encoded);
        surface.draw_text(encoded, x, center_y);
        x += advance;
        offset += size;
    }
    surface.set_font(display_font::default_font);
}
