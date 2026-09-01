#include "view_renderer.hpp"

#include <cstdio>
#include <cstring>

#include "layout.hpp"
#include "renderer_helpers.hpp"

namespace paper_mono_views {

namespace {

const char* file_status_text(file_view_status status)
{
    switch (status) {
        case file_view_status::no_card:
            return "SD card not inserted";
        case file_view_status::mounting:
            return "Reading SD card...";
        case file_view_status::loading:
            return "Loading...";
        case file_view_status::error:
            return "SD card error - reinsert card";
        case file_view_status::directory_error:
            return "Unable to read directory";
        case file_view_status::directory_too_large:
            return "Directory has too many entries";
        case file_view_status::path_too_long:
            return "Path is too long";
        case file_view_status::ready:
            return "";
    }
    return "";
}

bool file_page_button_enabled(const file_view_state& state, bool next)
{
    return next ? state.page_index + 1U < state.page_count
                : state.page_index > 0U;
}

}  // namespace

void draw_file_row(
    display_surface& surface,
    const file_view_state& state,
    std::uint8_t index,
    bool pressed)
{
    if (index >= state.row_count) {
        return;
    }
    const display_rect rect = file_row_rect(index);
    const file_row_view_state& row = state.rows[index];
    const bool active_pressed = pressed && row.enabled;
    const display_color background =
        active_pressed ? display_color::black : display_color::white;
    const display_color foreground =
        active_pressed ? display_color::white : display_color::black;
    draw_action_background(surface, rect, active_pressed, row.enabled);
    surface.set_font(display_font::cjk_24);
    surface.set_text_size(FILE_ROW_TEXT_SIZE);
    surface.set_text_alignment(display_text_alignment::middle_left);
    surface.set_text_color(foreground, background);
    char name[FILE_VIEW_NAME_LENGTH + 4U] = {};
    std::snprintf(name, sizeof(name), "%s%s", row.name, row.name_truncated ? "..." : "");
    while (surface.text_width(name) > rect.width - 36 && std::strlen(name) > 3U) {
        const std::size_t length = std::strlen(name);
        std::size_t cut = length - 4U;
        while (cut > 0U && (static_cast<unsigned char>(name[cut]) & 0xc0U) == 0x80U) {
            --cut;
        }
        std::memcpy(name + cut, "...", 4U);
    }
    surface.draw_text(name, rect.left + 4, rect.top + rect.height / 2);
    if (row.directory) {
        surface.set_text_alignment(display_text_alignment::middle_right);
        surface.draw_text(">", rect.left + rect.width - 4, rect.top + rect.height / 2);
    }
    surface.set_font(display_font::default_font);
}

void draw_file_page_button(
    display_surface& surface,
    const file_view_state& state,
    bool next,
    bool pressed)
{
    const display_rect rect = next ? file_next_page_rect() : file_previous_page_rect();
    const bool enabled = file_page_button_enabled(state, next);
    const bool active_pressed = pressed && enabled;
    const display_color background =
        active_pressed ? display_color::black : display_color::white;
    const display_color foreground =
        active_pressed ? display_color::white : display_color::black;
    draw_action_background(surface, rect, active_pressed, enabled);
    if (!enabled) {
        return;
    }
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(FILE_PAGE_BUTTON_TEXT_SIZE);
    surface.draw_text(
        next ? ">" : "<",
        rect.left + rect.width / 2,
        rect.top + rect.height / 2);
}

void draw_file_content(
    display_surface& surface,
    const file_view_state& state)
{
    surface.fill_rect(
        0,
        FILE_CONTENT_REGION_TOP,
        surface.width(),
        FILE_CONTENT_REGION_HEIGHT,
        display_color::white);
    surface.set_font(display_font::cjk_24);
    surface.set_text_size(1U);
    surface.set_text_color(display_color::black, display_color::white);
    surface.set_text_alignment(display_text_alignment::middle_left);
    surface.draw_text(state.path, FILE_PATH_LEFT, FILE_PATH_TOP + FILE_PATH_HEIGHT / 2);
    surface.draw_horizontal_line(
        FILE_PATH_LEFT,
        FILE_PATH_TOP + FILE_PATH_HEIGHT,
        UI_DISPLAY_WIDTH - FILE_PATH_LEFT * 2,
        display_color::black);
    surface.set_font(display_font::default_font);
    if (state.status == file_view_status::ready) {
        for (std::uint8_t index = 0U; index < state.row_count; ++index) {
            draw_file_row(surface, state, index, false);
        }
        draw_file_page_button(surface, state, false, false);
        draw_file_page_button(surface, state, true, false);
        char page[24] = {};
        std::snprintf(
            page,
            sizeof(page),
            "Page %u/%u",
            state.page_index + 1U,
            state.page_count);
        surface.set_text_color(display_color::black, display_color::white);
        surface.set_text_alignment(display_text_alignment::middle_center);
        surface.set_text_size(FILE_PAGE_LABEL_TEXT_SIZE);
        surface.draw_text(
            page,
            surface.width() / 2,
            FILE_PAGINATION_TOP + FILE_PAGINATION_HEIGHT / 2);
    } else {
        surface.set_text_color(display_color::black, display_color::white);
        draw_centered_line(surface, file_status_text(state.status), 360, 2U);
    }
    if (state.popup_visible) {
        surface.fill_rect(
            FILE_POPUP_LEFT,
            FILE_POPUP_TOP,
            FILE_POPUP_WIDTH,
            FILE_POPUP_HEIGHT,
            display_color::white);
        surface.draw_rect(
            FILE_POPUP_LEFT,
            FILE_POPUP_TOP,
            FILE_POPUP_WIDTH,
            FILE_POPUP_HEIGHT,
            display_color::black);
        surface.set_text_color(display_color::black, display_color::white);
        draw_centered_line(
            surface,
            "File preview is not supported",
            FILE_POPUP_TOP + FILE_POPUP_HEIGHT / 2,
            FILE_POPUP_TEXT_SIZE);
    }
}

void draw_file_view(
    display_surface& surface,
    const file_view_state& state)
{
    draw_back_button(surface, ui_view_id::file, false);
    draw_centered_line(surface, "Files", FILE_TITLE_CENTER_Y, FILE_TITLE_TEXT_SIZE);
    draw_file_content(surface, state);
}

}  // namespace paper_mono_views
