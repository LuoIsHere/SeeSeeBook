#include "view_renderer.hpp"

#include <cstdio>

#include "layout.hpp"
#include "renderer_helpers.hpp"

namespace paper_mono_views {

namespace {

constexpr const char* front_light_labels[FRONT_LIGHT_LEVEL_COUNT] = {
    "OFF", "25%", "50%", "75%", "100%",
};

}  // namespace

void draw_front_light_bar(
    display_surface& surface,
    std::uint8_t selected,
    std::int16_t pressed)
{
    surface.fill_rect(
        0, 0, surface.width(), FRONT_LIGHT_BAR_HEIGHT, display_color::white);
    surface.set_text_alignment(display_text_alignment::middle_center);
    surface.set_text_size(2U);
    for (std::uint8_t index = 0U; index < FRONT_LIGHT_LEVEL_COUNT; ++index) {
        const display_rect rect = front_light_button_rect(index);
        const bool is_pressed = pressed == static_cast<std::int16_t>(index);
        surface.fill_rect(
            rect,
            is_pressed ? display_color::black : display_color::white);
        if (!is_pressed) {
            surface.draw_rect(rect, display_color::black);
            if (selected == index) {
                surface.draw_rect(
                    rect.left + 3,
                    rect.top + 3,
                    rect.width - 6,
                    rect.height - 6,
                    display_color::black);
            }
        }
        surface.set_text_color(
            is_pressed ? display_color::white : display_color::black,
            is_pressed ? display_color::black : display_color::white);
        surface.draw_text(
            front_light_labels[index],
            rect.left + rect.width / 2,
            rect.top + rect.height / 2);
    }
}

void draw_test_content(
    display_surface& surface,
    const test_view_state& state)
{
    surface.fill_rect(
        0,
        TEST_CONTENT_REGION_TOP,
        surface.width(),
        TEST_CONTENT_REGION_HEIGHT,
        display_color::white);
    surface.set_text_color(display_color::black, display_color::white);
    draw_centered_line(
        surface,
        state.text_state == test_text_state::hi_xi ? "HI XI" : "Hello world",
        340,
        4U);
    char line[80] = {};
    if (state.touch_type == test_touch_display_type::click) {
        std::snprintf(
            line,
            sizeof(line),
            "Click (%d, %d)  %lu ms",
            state.end_x,
            state.end_y,
            static_cast<unsigned long>(state.duration_ms));
        draw_centered_line(surface, line, 460, 2U);
    } else if (state.touch_type == test_touch_display_type::long_press) {
        std::snprintf(
            line, sizeof(line), "Start (%d, %d)", state.start_x, state.start_y);
        draw_centered_line(surface, line, 440, 2U);
        std::snprintf(
            line,
            sizeof(line),
            "End (%d, %d)  %lu ms",
            state.end_x,
            state.end_y,
            static_cast<unsigned long>(state.duration_ms));
        draw_centered_line(surface, line, 490, 2U);
    }
}

void draw_test_view(
    display_surface& surface,
    const test_view_state& state,
    std::uint8_t selected_light,
    std::int16_t pressed_light)
{
    draw_front_light_bar(surface, selected_light, pressed_light);
    draw_back_button(surface, ui_view_id::test, false);
    draw_test_content(surface, state);
}

}  // namespace paper_mono_views
