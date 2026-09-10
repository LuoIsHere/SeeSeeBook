#include "view_renderer.hpp"

#include <algorithm>
#include <cstring>

#include "books_layout.hpp"
#include "renderer_helpers.hpp"
#include "text_layout_internal.hpp"

namespace paper_mono_views {

namespace {

const char* format_label(book_file_format format)
{
    switch (format) {
        case book_file_format::txt:
            return "TXT";
        case book_file_format::epub:
            return "EPUB";
        case book_file_format::unknown:
            return "";
    }
    return "";
}

void draw_books_toolbar(display_surface& surface)
{
    surface.fill_rect(books_toolbar_rect(), display_color::white);
    surface.draw_horizontal_line(
        0,
        BOOKS_TOOLBAR_HEIGHT - 1,
        UI_DISPLAY_WIDTH,
        display_color::black);
    surface.set_text_color(display_color::black, display_color::white);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(3U);
    const display_rect back = books_back_rect();
    surface.draw_text("<", back.left + back.width / 2, back.top + back.height / 2);
    draw_centered_line(
        surface,
        "Books",
        BOOKS_TOOLBAR_TITLE_CENTER_Y,
        BOOKS_TOOLBAR_TEXT_SIZE);

    // Programmatic settings glyph; no image asset or external font is needed.
    const display_rect button = books_settings_rect();
    const std::int16_t cx = button.left + button.width / 2;
    const std::int16_t cy = button.top + button.height / 2;
    surface.draw_rect(cx - 13, cy - 13, 27, 27, display_color::black);
    surface.draw_rect(cx - 5, cy - 5, 11, 11, display_color::black);
    surface.draw_line(cx, cy - 20, cx, cy - 13, display_color::black);
    surface.draw_line(cx, cy + 13, cx, cy + 20, display_color::black);
    surface.draw_line(cx - 20, cy, cx - 13, cy, display_color::black);
    surface.draw_line(cx + 13, cy, cx + 20, cy, display_color::black);
}

void draw_books_item(
    display_surface& surface,
    const books_view_state& state,
    std::uint8_t index)
{
    if (index >= books_view_item_capacity) {
        return;
    }
    const display_rect cell = books_item_rect(index);
    const display_rect cover = books_cover_rect(index);
    surface.fill_rect(cell, display_color::white);
    surface.draw_rect(cover, display_color::black);
    if (index >= state.item_count || !state.items[index].occupied) {
        return;
    }

    const books_item_view_state& item = state.items[index];
    const char* label = format_label(item.format);
    if (label[0] != '\0') {
        const display_rect tag = books_type_label_rect(index);
        surface.fill_rect(tag, display_color::white);
        surface.draw_rect(tag, display_color::black);
        surface.set_text_color(display_color::black, display_color::white);
        surface.set_text_alignment(display_text_alignment::middle_center);
        surface.set_text_size(BOOKS_TYPE_LABEL_TEXT_SIZE);
        surface.draw_text(
            label,
            tag.left + tag.width / 2,
            tag.top + tag.height / 2);
    }

    const display_rect name = books_file_name_rect(index);
    surface.set_text_color(display_color::black, display_color::white);
    const std::size_t line_count = std::min<std::size_t>(
        item.file_name.line_count,
        books_file_name_line_count);
    for (std::size_t line = 0U; line < line_count; ++line) {
        const char* text = item.file_name.lines[line];
        const std::size_t length = std::strlen(text);
        paper_mono_draw_cjk_text(
            surface,
            text,
            length,
            name.left,
            static_cast<std::int16_t>(
                name.top + BOOKS_FILE_NAME_LINE_HEIGHT / 2 +
                line * BOOKS_FILE_NAME_LINE_HEIGHT));
    }
}

void draw_checkbox(
    display_surface& surface,
    const display_rect& row,
    bool selected,
    const char* label)
{
    surface.fill_rect(row, display_color::white);
    const display_rect box = {
        row.left,
        static_cast<std::int16_t>(row.top + (row.height - BOOKS_CHECKBOX_SIZE) / 2),
        BOOKS_CHECKBOX_SIZE,
        BOOKS_CHECKBOX_SIZE,
    };
    surface.draw_rect(box, display_color::black);
    if (selected) {
        surface.fill_rect(
            box.left + 5,
            box.top + 5,
            box.width - 10,
            box.height - 10,
            display_color::black);
    }
    surface.set_text_color(display_color::black, display_color::white);
    surface.set_text_alignment(display_text_alignment::middle_left);
    surface.set_text_size(BOOKS_MODAL_TEXT_SIZE);
    surface.draw_text(
        label,
        static_cast<std::int16_t>(box.left + box.width + 16),
        row.top + row.height / 2);
}

void draw_modal_button(
    display_surface& surface,
    const display_rect& rect,
    const char* label)
{
    surface.fill_rect(rect, display_color::white);
    surface.draw_rect(rect, display_color::black);
    surface.set_text_color(display_color::black, display_color::white);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(BOOKS_MODAL_TEXT_SIZE);
    surface.draw_text(label, rect.left + rect.width / 2, rect.top + rect.height / 2);
}

void draw_books_modal(
    display_surface& surface,
    const books_view_state& state)
{
    surface.fill_rect(books_modal_rect(), display_color::white);
    surface.draw_rect(books_modal_rect(), display_color::black);
    draw_centered_line(
        surface,
        "Books Settings",
        BOOKS_MODAL_TITLE_CENTER_Y,
        BOOKS_MODAL_TITLE_TEXT_SIZE);
    draw_books_setting_row(surface, state, false);
    draw_books_setting_row(surface, state, true);
    draw_modal_button(surface, books_setting_confirm_rect(), "Confirm");
    draw_modal_button(surface, books_setting_cancel_rect(), "Cancel");
}

}  // namespace

void draw_books_setting_row(
    display_surface& surface,
    const books_view_state& state,
    bool epub)
{
    draw_checkbox(
        surface,
        books_setting_row_rect(epub),
        epub ? state.pending_settings.auto_scan_epub
             : state.pending_settings.auto_scan_txt,
        epub ? "Auto scan EPUB" : "Auto scan TXT");
}

void draw_books_content(
    display_surface& surface,
    const books_view_state& state)
{
    surface.fill_rect(books_content_rect(), display_color::white);
    for (std::uint8_t index = 0U; index < books_view_item_capacity; ++index) {
        draw_books_item(surface, state, index);
    }

    if (state.page_index > 0U) {
        const display_rect previous = books_previous_page_rect();
        surface.set_text_color(display_color::black, display_color::white);
        surface.set_text_alignment(display_text_alignment::middle_center);
        surface.set_text_size(BOOKS_PAGER_TEXT_SIZE);
        surface.draw_text(
            "<",
            previous.left + previous.width / 2,
            previous.top + previous.height / 2);
    }
    if (state.page_index + 1U < state.page_count) {
        const display_rect next = books_next_page_rect();
        surface.set_text_color(display_color::black, display_color::white);
        surface.set_text_alignment(display_text_alignment::middle_center);
        surface.set_text_size(BOOKS_PAGER_TEXT_SIZE);
        surface.draw_text(
            ">",
            next.left + next.width / 2,
            next.top + next.height / 2);
    }
    if (state.settings_visible) {
        draw_books_modal(surface, state);
    }
}

void draw_books_view(
    display_surface& surface,
    const books_view_state& state)
{
    draw_books_toolbar(surface);
    draw_books_content(surface, state);
}

}  // namespace paper_mono_views
