#pragma once

#include <cstdint>

#include "display.hpp"
#include "ui_action.hpp"

namespace paper_mono_views {

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
void draw_back_button(
    display_surface& surface,
    ui_view_id view,
    bool pressed);

}  // namespace paper_mono_views
