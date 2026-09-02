#include "view_renderer.hpp"

#include <cstdio>

#include "layout.hpp"
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
        case reader_view_status::file_not_found: return "File not found";
        case reader_view_status::storage_error: return "Read error - reopen the file";
        case reader_view_status::no_card: return "SD removed - reopen the file";
        case reader_view_status::ready: return "";
    }
    return "";
}

void draw_page_button(display_surface& surface, bool next, bool enabled)
{
    const auto rect = reader_button_rect(next ? 2U : 1U);
    surface.draw_rect(rect, display_color::black);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(3U);
    surface.draw_text(enabled ? (next ? ">" : "<") : "-",
                      rect.left + rect.width / 2, rect.top + rect.height / 2);
}

}  // namespace

void draw_reader_view(display_surface& surface, const reader_view_state& state)
{
    surface.fill_rect(0, 0, UI_DISPLAY_WIDTH, STATUS_BAR_TOP, display_color::white);
    surface.set_text_color(display_color::black, display_color::white);
    surface.set_font(display_font::default_font);
    if (state.status == reader_view_status::ready) {
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
        draw_centered_line(surface, status_text(state.status), READER_NAVIGATION_TOP / 2, 2U);
    }
    surface.set_font(display_font::default_font);
    if (!state.progress_persistent) {
        draw_centered_line(surface, "Progress: RAM only", READER_NAVIGATION_TOP - 14, 1U);
    }
    draw_back_button(surface, ui_view_id::reader, false);
    draw_page_button(surface, false, state.previous_enabled);
    draw_page_button(surface, true, state.next_enabled);
}

}  // namespace paper_mono_views
