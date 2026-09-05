#include "view_renderer.hpp"

#include <cstdio>

#include "layout.hpp"
#include "reader_cover.hpp"
#include "renderer_helpers.hpp"
#include "text_layout_internal.hpp"

namespace paper_mono_views {
namespace {

const char* status_text(reader_view_status status)
{
    switch (status) {
        case reader_view_status::loading: return "Loading text...";
        case reader_view_status::empty_file: return "This TXT file is empty";
        case reader_view_status::invalid_utf8: return "Invalid UTF-8 text";
        case reader_view_status::invalid_epub: return "Invalid EPUB file";
        case reader_view_status::unsupported_epub: return "Unsupported EPUB format";
        case reader_view_status::file_not_found: return "File not found";
        case reader_view_status::storage_error: return "Read error - reopen the file";
        case reader_view_status::no_card: return "SD removed - reopen the file";
        case reader_view_status::ready: return "";
    }
    return "";
}

void draw_menu(display_surface& surface)
{
    const auto rect = reader_menu_rect();
    surface.fill_rect(rect, display_color::white);
    surface.draw_horizontal_line(rect.left, rect.top + rect.height - 1,
                                 rect.width, display_color::black);
    const auto back = reader_menu_item_rect(0U);
    surface.set_text_alignment(display_text_alignment::middle_left);
    surface.set_text_size(APP_BACK_BUTTON_TEXT_SIZE);
    surface.draw_text("<", back.left + READER_MARGIN, back.top + back.height / 2);
}

}  // namespace

void draw_reader_view(display_surface& surface, const reader_view_state& state)
{
    surface.fill_rect(reader_content_rect(), display_color::white);
    surface.set_text_color(display_color::black, display_color::white);
    surface.set_font(display_font::default_font);
    if (state.status == reader_view_status::ready && state.showing_cover) {
        reader_cover_lease cover = {};
        const bool acquired = ui_reader_cover_acquire(state.cover_generation, cover);
        const bool drawn = acquired && surface.draw_image(
            cover.data, cover.size, cover.encoding, reader_content_rect());
        if (acquired) { ui_reader_cover_release(cover); }
        if (!drawn) { draw_centered_line(surface, "Cover unavailable", STATUS_BAR_TOP / 2, 2U); }
    } else if (state.status == reader_view_status::ready) {
        for (std::uint16_t index = 0U;
             index < state.page.line_count && index < READER_LINE_COUNT; ++index) {
            const auto& line = state.page.lines[index];
            if (line.offset > state.page.text_length ||
                line.length > state.page.text_length - line.offset) {
                break;
            }
            paper_mono_draw_cjk_text(
                surface, state.page.text + line.offset, line.length, READER_MARGIN,
                READER_TEXT_TOP + index * READER_LINE_HEIGHT + READER_LINE_HEIGHT / 2);
        }
    } else {
        draw_centered_line(surface, status_text(state.status), STATUS_BAR_TOP / 2, 2U);
    }
    surface.set_font(display_font::default_font);
    if (!state.progress_persistent) {
        draw_centered_line(surface, "Progress: RAM only", STATUS_BAR_TOP - 12, 1U);
    }
    if (state.menu_visible) {
        draw_menu(surface);
    }
}

}  // namespace paper_mono_views
