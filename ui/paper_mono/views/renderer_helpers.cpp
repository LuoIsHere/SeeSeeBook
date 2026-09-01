#include "renderer_helpers.hpp"

#include "layout.hpp"

namespace paper_mono_views {

void draw_centered_line(
    display_surface& surface,
    const char* text,
    std::int32_t y,
    std::uint8_t size)
{
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(size);
    surface.draw_text(text, surface.width() / 2, y);
}

void draw_action_background(
    display_surface& surface,
    const display_rect& rect,
    bool pressed,
    bool enabled)
{
    const display_color color = pressed && enabled
                                    ? display_color::black
                                    : display_color::white;
    surface.fill_rect(rect, color);
}

void draw_back_button(
    display_surface& surface,
    ui_view_id view,
    bool pressed)
{
    const display_rect rect = app_back_button_rect(view);
    const display_color background =
        pressed ? display_color::black : display_color::white;
    const display_color foreground =
        pressed ? display_color::white : display_color::black;
    surface.fill_rect(rect, background);
    surface.draw_rect(rect, display_color::black);
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(APP_BACK_BUTTON_TEXT_SIZE);
    surface.draw_text("< Back", rect.left + rect.width / 2, rect.top + rect.height / 2);
}

}  // namespace paper_mono_views
