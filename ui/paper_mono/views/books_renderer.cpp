#include "view_renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "books_layout.hpp"
#include "books_cover.hpp"
#include "design.hpp"
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

void draw_books_toolbar_button(
    display_surface& surface,
    bool settings,
    bool pressed)
{
    const display_rect button = settings ? books_settings_rect() : books_back_rect();
    const display_color background = control_background(pressed);
    const display_color foreground = control_foreground(pressed);
    surface.fill_rect(button, display_color::white);
    const display_rect visual = inset_rect(button, paper_ui::space_md);
    draw_control_surface(
        surface, visual, false, pressed, true, true,
        paper_ui::radius_control);
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(paper_ui::text_title);
    if (!settings) {
        draw_chevron(
            surface,
            static_cast<std::int16_t>(button.left + button.width / 2),
            static_cast<std::int16_t>(button.top + button.height / 2),
            false,
            foreground);
        return;
    }
    const std::int16_t cx = button.left + button.width / 2;
    const std::int16_t cy = button.top + button.height / 2;
    surface.draw_round_rect({static_cast<std::int16_t>(cx - 11),
                             static_cast<std::int16_t>(cy - 11), 23, 23},
                            paper_ui::radius_small, foreground);
    surface.fill_round_rect({static_cast<std::int16_t>(cx - 3),
                             static_cast<std::int16_t>(cy - 3), 7, 7},
                            3, foreground);
    for (std::int16_t stroke = 0; stroke < paper_ui::icon_stroke; ++stroke) {
        surface.draw_line(cx + stroke, cy - 17, cx + stroke, cy - 11, foreground);
        surface.draw_line(cx + stroke, cy + 11, cx + stroke, cy + 17, foreground);
        surface.draw_line(cx - 17, cy + stroke, cx - 11, cy + stroke, foreground);
        surface.draw_line(cx + 11, cy + stroke, cx + 17, cy + stroke, foreground);
    }
}

void draw_books_toolbar(display_surface& surface)
{
    surface.fill_rect(books_toolbar_rect(), display_color::white);
    draw_books_toolbar_button(surface, false, false);
    draw_books_toolbar_button(surface, true, false);
    surface.set_text_color(display_color::black, display_color::white);
    draw_centered_line(
        surface,
        "Books",
        BOOKS_TOOLBAR_TITLE_CENTER_Y,
        BOOKS_TOOLBAR_TEXT_SIZE);
}

}  // namespace

void draw_books_item(
    display_surface& surface,
    const books_view_state& state,
    std::uint8_t index,
    bool pressed)
{
    if (index >= books_view_item_capacity) {
        return;
    }
    const display_rect card = books_item_card_rect(index);
    const display_rect cover = books_cover_rect(index);
    surface.fill_rect(books_item_redraw_rect(index), display_color::white);
    if (index >= state.item_count || !state.items[index].occupied) {
        return;
    }

    const books_item_view_state& item = state.items[index];
    surface.set_text_color(display_color::black, display_color::white);
    surface.draw_rect(cover, display_color::black);
    bool cover_drawn = false;
    if (item.format == book_file_format::epub && item.cover_generation != 0U) {
        books_cover_lease lease = {};
        const bool acquired = ui_books_cover_acquire(index, item.cover_generation, lease);
        cover_drawn = acquired && surface.draw_image(
            lease.data, lease.size, lease.encoding, cover,
            display_image_mode::mono_dither);
        if (acquired) { ui_books_cover_release(lease); }
    } else if (item.format == book_file_format::txt && item.preview_line_count != 0U) {
        const std::size_t lines = std::min<std::size_t>(
            item.preview_line_count, books_preview_line_count);
        for (std::size_t line = 0U; line < lines; ++line) {
            const char* text = item.preview[line];
            paper_mono_draw_cjk_text(
                surface, text, std::strlen(text),
                static_cast<std::int16_t>(cover.left + BOOKS_PREVIEW_MARGIN),
                static_cast<std::int16_t>(
                    cover.top + BOOKS_PREVIEW_MARGIN + BOOKS_PREVIEW_LINE_HEIGHT / 2 +
                    line * BOOKS_PREVIEW_LINE_HEIGHT));
        }
        cover_drawn = true;
    }
    if (!cover_drawn) {
        surface.set_text_color(display_color::black, display_color::white);
        surface.set_text_alignment(display_text_alignment::middle_center);
        surface.set_text_size(1U);
        const char* placeholder = item.preview_state == books_preview_view_state::invalid_utf8
                                      ? "Invalid UTF-8"
                                      : format_label(item.format);
        surface.draw_text(placeholder, cover.left + cover.width / 2,
                          cover.top + cover.height / 2);
    }
    surface.draw_rect(cover, display_color::black);
    const char* label = format_label(item.format);
    if (label[0] != '\0') {
        const display_rect tag = books_type_label_rect(index);
        surface.fill_round_rect(
            tag, paper_ui::radius_small, display_color::white);
        surface.draw_round_rect(
            tag, paper_ui::radius_small, display_color::black);
        surface.set_text_color(display_color::black, display_color::white);
        surface.set_text_alignment(display_text_alignment::middle_center);
        surface.set_text_size(BOOKS_TYPE_LABEL_TEXT_SIZE);
        surface.draw_text(
            label,
            tag.left + tag.width / 2,
            tag.top + tag.height / 2);
    }

    const display_rect name = books_file_name_rect(index);
    const bool focused = state.selected_index == index && item.enabled;
    const bool active_pressed = pressed && item.enabled;
    const display_color name_background =
        control_background(active_pressed, item.enabled);
    const display_color name_foreground =
        control_foreground(active_pressed, item.enabled);
    draw_control_surface(
        surface, name, false, active_pressed, item.enabled, false,
        paper_ui::radius_small);
    surface.set_text_color(name_foreground, name_background);
    const std::size_t line_count = std::min<std::size_t>(
        item.file_name.line_count,
        books_file_name_line_count);
    const std::int16_t first_baseline = line_count == 1U
                                            ? name.top + name.height / 2
                                            : name.top + BOOKS_FILE_NAME_LINE_HEIGHT / 2;
    for (std::size_t line = 0U; line < line_count; ++line) {
        const char* text = item.file_name.lines[line];
        const std::size_t length = std::strlen(text);
        paper_mono_draw_cjk_text(
            surface,
            text,
            length,
            static_cast<std::int16_t>(
                name.left + BOOKS_FILE_NAME_TEXT_INSET),
            static_cast<std::int16_t>(
                first_baseline +
                line * BOOKS_FILE_NAME_LINE_HEIGHT));
    }
    if (focused) {
        draw_focus_outline(
            surface, card, paper_ui::radius_card);
    }
}

namespace {

void draw_checkbox(
    display_surface& surface,
    const display_rect& row,
    bool selected,
    const char* label,
    bool pressed)
{
    const display_color background = control_background(pressed);
    const display_color foreground = control_foreground(pressed);
    draw_control_surface(
        surface, row, false, pressed, true, false,
        paper_ui::radius_control);
    const display_rect box = {
        static_cast<std::int16_t>(row.left + paper_ui::space_sm),
        static_cast<std::int16_t>(row.top + (row.height - BOOKS_CHECKBOX_SIZE) / 2),
        BOOKS_CHECKBOX_SIZE,
        BOOKS_CHECKBOX_SIZE,
    };
    surface.draw_round_rect(box, paper_ui::radius_small, foreground);
    if (selected) {
        for (std::int16_t stroke = 0; stroke < paper_ui::icon_stroke; ++stroke) {
            surface.draw_line(
                box.left + 6,
                box.top + 14 + stroke,
                box.left + 12,
                box.top + 20 + stroke,
                foreground);
            surface.draw_line(
                box.left + 12,
                box.top + 20 + stroke,
                box.left + 23,
                box.top + 7 + stroke,
                foreground);
        }
    }
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_left);
    surface.set_text_size(BOOKS_MODAL_TEXT_SIZE);
    surface.draw_text(
        label,
        static_cast<std::int16_t>(box.left + box.width + paper_ui::space_lg),
        row.top + row.height / 2);
}

void draw_modal_button(
    display_surface& surface,
    const display_rect& rect,
    const char* label,
    bool pressed)
{
    const display_color background = control_background(pressed);
    const display_color foreground = control_foreground(pressed);
    draw_control_surface(
        surface, rect, false, pressed, true, true,
        paper_ui::radius_control);
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(BOOKS_MODAL_TEXT_SIZE);
    surface.draw_text(label, rect.left + rect.width / 2, rect.top + rect.height / 2);
}

void draw_books_modal(
    display_surface& surface,
    const books_view_state& state)
{
    surface.fill_round_rect(
        books_modal_rect(), paper_ui::radius_dialog, display_color::white);
    surface.draw_round_rect(
        books_modal_rect(), paper_ui::radius_dialog, display_color::black);
    draw_centered_line(
        surface,
        "Books Settings",
        BOOKS_MODAL_TITLE_CENTER_Y,
        BOOKS_MODAL_TITLE_TEXT_SIZE);
    draw_books_setting_row(surface, state, false);
    draw_books_setting_row(surface, state, true);
    draw_modal_button(surface, books_setting_confirm_rect(), "Confirm", false);
    draw_modal_button(surface, books_setting_cancel_rect(), "Cancel", false);
}

void draw_books_pager_button(
    display_surface& surface,
    const books_view_state& state,
    bool next,
    bool pressed)
{
    const display_rect rect = next ? books_next_page_rect()
                                   : books_previous_page_rect();
    const bool enabled = next ? state.page_index + 1U < state.page_count
                              : state.page_index > 0U;
    const display_color foreground = control_foreground(pressed, enabled);
    draw_control_surface(
        surface, rect, false, pressed, enabled, true,
        paper_ui::radius_control);
    draw_chevron(
        surface,
        static_cast<std::int16_t>(rect.left + rect.width / 2),
        static_cast<std::int16_t>(rect.top + rect.height / 2),
        next,
        foreground);
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
        epub ? "Auto scan EPUB" : "Auto scan TXT",
        false);
}

void draw_books_control(
    display_surface& surface,
    const books_view_state& state,
    ui_control_type control,
    std::uint8_t index,
    bool pressed)
{
    switch (control) {
        case ui_control_type::books_back:
            draw_books_toolbar_button(surface, false, pressed);
            break;
        case ui_control_type::books_settings:
            draw_books_toolbar_button(surface, true, pressed);
            break;
        case ui_control_type::books_select_item:
            draw_books_item(surface, state, index, pressed);
            break;
        case ui_control_type::books_page_previous:
        case ui_control_type::books_page_next: {
            const bool next = control == ui_control_type::books_page_next;
            draw_books_pager_button(surface, state, next, pressed);
            break;
        }
        case ui_control_type::books_setting_toggle_txt:
        case ui_control_type::books_setting_toggle_epub: {
            const bool epub =
                control == ui_control_type::books_setting_toggle_epub;
            draw_checkbox(
                surface,
                books_setting_row_rect(epub),
                epub ? state.pending_settings.auto_scan_epub
                     : state.pending_settings.auto_scan_txt,
                epub ? "Auto scan EPUB" : "Auto scan TXT",
                pressed);
            break;
        }
        case ui_control_type::books_setting_confirm:
            draw_modal_button(
                surface, books_setting_confirm_rect(), "Confirm", pressed);
            break;
        case ui_control_type::books_setting_cancel:
            draw_modal_button(
                surface, books_setting_cancel_rect(), "Cancel", pressed);
            break;
        default:
            break;
    }
}

void draw_books_content(
    display_surface& surface,
    const books_view_state& state)
{
    surface.fill_rect(books_content_rect(), display_color::white);
    for (std::uint8_t index = 0U; index < books_view_item_capacity; ++index) {
        draw_books_item(surface, state, index);
    }

    if (state.item_count == 0U) {
        draw_centered_line(
            surface,
            state.catalog_busy ? "Scanning books..." :
            state.catalog_error ? "Catalog unavailable" : "No books found",
            (BOOKS_GRID_TOP + BOOKS_PAGER_TOP) / 2,
            2U);
    }

    draw_books_pager_button(surface, state, false, false);
    draw_books_pager_button(surface, state, true, false);
    if (state.page_count > 0U) {
        char page[24] = {};
        std::snprintf(
            page,
            sizeof(page),
            "%u / %u",
            static_cast<unsigned>(state.page_index + 1U),
            static_cast<unsigned>(state.page_count));
        surface.set_text_color(display_color::black, display_color::white);
        draw_centered_line(
            surface,
            page,
            BOOKS_PAGER_TOP + BOOKS_PAGER_HEIGHT / 2,
            paper_ui::text_body);
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
