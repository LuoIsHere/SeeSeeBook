#pragma once

#include <cstdint>

#include "display.hpp"
#include "ui_action.hpp"

namespace paper_mono_views {

constexpr display_rect inset_rect(const display_rect& rect, std::int16_t inset)
{
    return {
        static_cast<std::int16_t>(rect.left + inset),
        static_cast<std::int16_t>(rect.top + inset),
        static_cast<std::int16_t>(rect.width - inset * 2),
        static_cast<std::int16_t>(rect.height - inset * 2),
    };
}

void draw_centered_line(
    display_surface& surface,
    const char* text,
    std::int32_t y,
    std::uint8_t size);
void draw_action_background(
    display_surface& surface,
    const display_rect& rect,
    bool pressed,
    bool enabled);
void draw_control_surface(
    display_surface& surface,
    const display_rect& rect,
    bool focused,
    bool pressed,
    bool enabled,
    bool outlined,
    std::int16_t radius);
void draw_focus_outline(
    display_surface& surface,
    const display_rect& rect,
    std::int16_t radius);
display_color control_foreground(bool pressed, bool enabled = true);
display_color control_background(bool pressed, bool enabled = true);
void draw_chevron(
    display_surface& surface,
    std::int16_t center_x,
    std::int16_t center_y,
    bool next,
    display_color color);
void draw_back_button(
    display_surface& surface,
    ui_view_id view,
    bool pressed);

}  // namespace paper_mono_views
