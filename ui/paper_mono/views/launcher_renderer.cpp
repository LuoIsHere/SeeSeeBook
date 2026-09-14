#include "view_renderer.hpp"

#include <algorithm>

#include "layout.hpp"
#include "project_info.hpp"
#include "renderer_helpers.hpp"

namespace paper_mono_views {

void draw_launcher_entry(
    display_surface& surface,
    const launcher_view_state& state,
    std::uint8_t index,
    bool pressed)
{
    if (index >= state.entry_count || index >= launcher_view_entry_capacity) {
        return;
    }
    const display_rect rect = launcher_entry_rect(index);
    const display_color background =
        pressed ? display_color::black : display_color::white;
    const display_color foreground =
        pressed ? display_color::white : display_color::black;
    surface.fill_rect(rect, background);
    surface.draw_rect(rect, foreground);
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(LAUNCHER_ENTRY_TEXT_SIZE);
    surface.draw_text(
        state.entries[index].label,
        rect.left + rect.width / 2,
        rect.top + rect.height / 2);
}

void draw_launcher_view(
    display_surface& surface,
    const launcher_view_state& state)
{
    draw_centered_line(
        surface,
        PROJECT_NAME,
        LAUNCHER_TITLE_CENTER_Y,
        LAUNCHER_TITLE_TEXT_SIZE);
    const std::size_t count = std::min<std::size_t>(
        state.entry_count,
        launcher_view_entry_capacity);
    for (std::uint8_t index = 0U; index < count; ++index) {
        draw_launcher_entry(surface, state, index, false);
    }
}

}  // namespace paper_mono_views
