#include "view_renderer.hpp"

#include <cstdio>

#include "layout.hpp"
#include "renderer_helpers.hpp"

namespace paper_mono_views {

void draw_battery_content(
    display_surface& surface,
    const battery_view_state& state)
{
    surface.fill_rect(
        0,
        BATTERY_CONTENT_REGION_TOP,
        surface.width(),
        BATTERY_CONTENT_REGION_HEIGHT,
        display_color::white);
    if (state.loading) {
        surface.set_text_color(display_color::black, display_color::white);
        draw_centered_line(surface, "Loading...", BATTERY_FIRST_ROW_CENTER_Y, 2U);
        return;
    }
    char value[32] = {};
    constexpr const char* labels[] = {"Level", "Voltage", "Current", "Status"};
    for (std::uint8_t row = 0U; row < 4U; ++row) {
        if (row == 0U) {
            std::snprintf(value, sizeof(value), state.level_valid ? "%u %%" : "--", state.percent);
        } else if (row == 1U) {
            std::snprintf(
                value,
                sizeof(value),
                state.voltage_valid ? "%.2f V" : "--",
                static_cast<double>(state.voltage_mv) / 1000.0);
        } else if (row == 2U) {
            std::snprintf(
                value,
                sizeof(value),
                state.current_valid ? "%ld mA" : "--",
                static_cast<long>(state.current_ma));
        } else {
            std::snprintf(
                value,
                sizeof(value),
                "%s",
                state.charging_valid
                    ? (state.charging ? "Charging" : "Not charging")
                    : "Unknown");
        }
        const std::int32_t y = BATTERY_FIRST_ROW_CENTER_Y + row * BATTERY_ROW_HEIGHT;
        surface.set_text_color(display_color::black, display_color::white);
        surface.set_text_size(BATTERY_ROW_TEXT_SIZE);
        surface.set_text_alignment(display_text_alignment::middle_left);
        surface.draw_text(labels[row], BATTERY_LABEL_LEFT, y);
        surface.set_text_alignment(display_text_alignment::middle_right);
        surface.draw_text(value, BATTERY_VALUE_RIGHT, y);
    }
}

void draw_battery_view(
    display_surface& surface,
    const battery_view_state& state)
{
    draw_back_button(surface, ui_view_id::battery, false);
    draw_centered_line(surface, "Battery", BATTERY_TITLE_CENTER_Y, BATTERY_TITLE_TEXT_SIZE);
    draw_battery_content(surface, state);
}

}  // namespace paper_mono_views
