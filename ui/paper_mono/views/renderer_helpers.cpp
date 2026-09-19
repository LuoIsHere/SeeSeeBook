#include "renderer_helpers.hpp"

#include <algorithm>

#include "design.hpp"
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
    draw_control_surface(
        surface,
        rect,
        false,
        pressed,
        enabled,
        false,
        paper_ui::radius_control);
}

display_color control_foreground(bool pressed, bool enabled)
{
    return pressed && enabled ? display_color::white : display_color::black;
}

display_color control_background(bool pressed, bool enabled)
{
    return pressed && enabled ? display_color::black : display_color::white;
}

void draw_control_surface(
    display_surface& surface,
    const display_rect& rect,
    bool focused,
    bool pressed,
    bool enabled,
    bool outlined,
    std::int16_t radius)
{
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    surface.fill_rect(rect, display_color::white);
    const bool active_pressed = pressed && enabled;
    if (active_pressed) {
        const std::int16_t inset = focused ? paper_ui::space_xs : 1;
        surface.fill_round_rect(
            inset_rect(rect, inset),
            std::max<std::int16_t>(paper_ui::radius_small, radius - inset),
            display_color::black);
    }
    if (focused && enabled) {
        for (std::int16_t stroke = 0;
             stroke < paper_ui::stroke_focus;
             ++stroke) {
            surface.draw_round_rect(
                inset_rect(rect, stroke),
                std::max<std::int16_t>(paper_ui::radius_small, radius - stroke),
                display_color::black);
        }
    } else if (outlined || !enabled) {
        surface.draw_round_rect(
            inset_rect(rect, 1),
            std::max<std::int16_t>(paper_ui::radius_small, radius - 1),
            display_color::black);
    }
    if (!enabled) {
        const std::int16_t right = rect.left + rect.width - paper_ui::space_md;
        const std::int16_t top = rect.top + paper_ui::space_sm;
        surface.draw_line(right - 6, top, right, top + 6, display_color::black);
        surface.draw_line(right, top, right - 6, top + 6, display_color::black);
    }
}

void draw_focus_outline(
    display_surface& surface,
    const display_rect& rect,
    std::int16_t radius)
{
    for (std::int16_t stroke = 0;
         stroke < paper_ui::stroke_focus;
         ++stroke) {
        surface.draw_round_rect(
            inset_rect(rect, stroke),
            std::max<std::int16_t>(paper_ui::radius_small, radius - stroke),
            display_color::black);
    }
}

void draw_chevron(
    display_surface& surface,
    std::int16_t center_x,
    std::int16_t center_y,
    bool next,
    display_color color)
{
    const std::int16_t direction = next ? 1 : -1;
    for (std::int16_t stroke = 0; stroke < paper_ui::icon_stroke; ++stroke) {
        surface.draw_line(
            center_x - direction * 5 + stroke,
            center_y - 8,
            center_x + direction * 3 + stroke,
            center_y,
            color);
        surface.draw_line(
            center_x + direction * 3 + stroke,
            center_y,
            center_x - direction * 5 + stroke,
            center_y + 8,
            color);
    }
}

void draw_back_button(
    display_surface& surface,
    ui_view_id view,
    bool pressed)
{
    const display_rect hit_rect = app_back_button_rect(view);
    surface.fill_rect(hit_rect, display_color::white);
    const display_rect rect = view == ui_view_id::reader
                                  ? display_rect{
                                        static_cast<std::int16_t>(
                                            hit_rect.left + paper_ui::space_lg),
                                        static_cast<std::int16_t>(
                                            hit_rect.top + paper_ui::space_lg),
                                        APP_BACK_BUTTON_WIDTH,
                                        APP_BACK_BUTTON_HEIGHT,
                                    }
                                  : hit_rect;
    const display_color background = control_background(pressed);
    const display_color foreground = control_foreground(pressed);
    draw_control_surface(
        surface,
        rect,
        false,
        pressed,
        true,
        true,
        paper_ui::radius_control);
    surface.set_text_color(foreground, background);
    surface.set_text_alignment(display_text_alignment::middle_left);
    surface.set_text_size(APP_BACK_BUTTON_TEXT_SIZE);
    draw_chevron(
        surface,
        static_cast<std::int16_t>(rect.left + paper_ui::space_lg),
        static_cast<std::int16_t>(rect.top + rect.height / 2),
        false,
        foreground);
    surface.draw_text(
        "Back",
        static_cast<std::int16_t>(rect.left + paper_ui::space_xl + paper_ui::space_sm),
        rect.top + rect.height / 2);
}

}  // namespace paper_mono_views
