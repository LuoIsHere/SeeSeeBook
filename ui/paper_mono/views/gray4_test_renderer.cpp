#include "view_renderer.hpp"

#include "layout.hpp"
#include "renderer_helpers.hpp"

namespace paper_mono_views {

namespace {

constexpr display_gray4 levels[] = {
    display_gray4::white,
    display_gray4::light_gray,
    display_gray4::dark_gray,
    display_gray4::black,
};

constexpr const char* labels[] = {"White", "Light", "Dark", "Black"};

}  // namespace

void draw_gray4_test_view(
    display_surface& surface,
    const gray4_test_view_state&)
{
    surface.fill_screen(display_gray4::white);
    draw_back_button(surface, ui_view_id::gray4_test, false);

    surface.set_text_color(display_gray4::black, display_gray4::white);
    draw_centered_line(surface, "Gray4 Diagnostics", 48, 3U);
    draw_centered_line(surface, "0xD7 OTP waveform", 118, 2U);

    constexpr std::int16_t block_top = 168;
    constexpr std::int16_t block_height = 248;
    constexpr std::int16_t block_width = UI_DISPLAY_WIDTH / 4;
    for (std::uint8_t index = 0U; index < 4U; ++index) {
        const display_rect rect = {
            static_cast<std::int16_t>(index * block_width),
            block_top,
            block_width,
            block_height,
        };
        surface.fill_rect(rect, levels[index]);
        surface.set_text_color(
            index >= 2U ? display_gray4::white : display_gray4::black,
            levels[index]);
        surface.set_text_alignment(display_text_alignment::middle_center);
        surface.set_text_size(2U);
        surface.draw_text(labels[index],
                          rect.left + rect.width / 2,
                          rect.top + rect.height / 2);
    }

    surface.set_text_color(display_gray4::black, display_gray4::white);
    draw_centered_line(surface, "Four-level ramp", 472, 2U);
    constexpr std::int16_t ramp_left = 24;
    constexpr std::int16_t ramp_top = 510;
    constexpr std::int16_t ramp_width = UI_DISPLAY_WIDTH - ramp_left * 2;
    constexpr std::int16_t ramp_height = 176;
    constexpr std::int16_t step_width = ramp_width / 16;
    for (std::uint8_t step = 0U; step < 16U; ++step) {
        const display_rect rect = {
            static_cast<std::int16_t>(ramp_left + step * step_width),
            ramp_top,
            static_cast<std::int16_t>(
                step == 15U ? ramp_width - step_width * 15 : step_width),
            ramp_height,
        };
        surface.fill_rect(rect, levels[step / 4U]);
    }
    surface.draw_rect(
        {ramp_left, ramp_top, ramp_width, ramp_height},
        display_color::black);
}

}  // namespace paper_mono_views
