#include "view_renderer.hpp"

#include <algorithm>

#include "design.hpp"
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
    const bool focused = state.selected_index == index;
    const display_color background = control_background(pressed);
    const display_color foreground = control_foreground(pressed);
    draw_control_surface(
        surface, rect, focused, pressed, true, false,
        paper_ui::radius_control);
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_left);
    surface.set_text_size(MENU_ENTRY_TEXT_SIZE);
    surface.draw_text(
        state.entries[index].label,
        rect.left + paper_ui::space_lg,
        rect.top + rect.height / 2);
}

void draw_menu_view(
    display_surface& surface,
    const menu_view_state& state)
{
    draw_back_button(surface, ui_view_id::menu, false);
    draw_centered_line(surface, "Menu", MENU_TITLE_CENTER_Y, MENU_TITLE_TEXT_SIZE);
    const std::size_t entry_count = std::min<std::size_t>(
        state.entry_count,
        menu_view_entry_capacity);
    for (std::uint8_t index = 0U; index < entry_count; ++index) {
        draw_menu_entry(surface, state, index, false);
    }
}

}  // namespace paper_mono_views
