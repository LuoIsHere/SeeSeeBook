#include "view_renderer.hpp"

#include <algorithm>

#include "layout.hpp"
#include "project_info.hpp"
#include "renderer_helpers.hpp"

namespace paper_mono_views {

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
        const display_rect rect = launcher_entry_rect(index);
        surface.fill_rect(rect, display_color::white);
        surface.draw_rect(rect, display_color::black);
        surface.set_text_color(display_color::black, display_color::white);
        surface.set_text_alignment(display_text_alignment::middle_center);
        surface.set_text_size(LAUNCHER_ENTRY_TEXT_SIZE);
        surface.draw_text(
            state.entries[index].label,
            rect.left + rect.width / 2,
            rect.top + rect.height / 2);
    }
}

}  // namespace paper_mono_views
