#include "view_renderer.hpp"

#include <algorithm>

#include "layout.hpp"
#include "project_info.hpp"
#include "renderer_helpers.hpp"

namespace paper_mono_views {

void draw_menu_entry(
    display_surface& surface,
    const menu_view_state& state,
    std::uint8_t index,
    bool pressed)
{
    if (index >= state.entry_count || index >= menu_view_entry_capacity) {
        return;
    }
    const display_rect rect = menu_entry_rect(index);
    const display_color background =
        pressed ? display_color::black : display_color::white;
    const display_color foreground =
        pressed ? display_color::white : display_color::black;
    surface.fill_rect(rect, background);
    if (index == 0U) {
        surface.draw_horizontal_line(
            rect.left,
            rect.top,
            rect.width,
            display_color::black);
    }
    surface.draw_horizontal_line(
        rect.left,
        rect.top + rect.height - 1,
        rect.width,
        display_color::black);
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(MENU_ENTRY_TEXT_SIZE);
    surface.draw_text(
        state.entries[index].label,
        rect.left + rect.width / 2,
        rect.top + rect.height / 2);
}

void draw_menu_view(
    display_surface& surface,
    const menu_view_state& state)
{
    draw_centered_line(surface, PROJECT_NAME, MENU_TITLE_CENTER_Y, MENU_TITLE_TEXT_SIZE);
    const std::size_t entry_count = std::min<std::size_t>(
        state.entry_count,
        menu_view_entry_capacity);
    for (std::uint8_t index = 0U; index < entry_count; ++index) {
        draw_menu_entry(surface, state, index, false);
    }
}

}  // namespace paper_mono_views
