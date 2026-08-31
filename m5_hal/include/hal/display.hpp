#pragma once

#include <cstdint>

#include "geometry.hpp"

enum class refresh_mode : std::uint8_t {
    fastest,
    text,
    fast,
    quality,
};

enum class display_color : std::uint8_t {
    white,
    black,
};

enum class display_font : std::uint8_t {
    default_font,
    cjk_24,
};

enum class display_text_alignment : std::uint8_t {
    middle_left,
    middle_center,
    middle_right,
};

struct display_refresh_result {
    bool success;
    refresh_mode actual_mode;
    std::uint32_t duration_ms;
};

// Device-independent drawing surface backed by the active display driver.
class display_surface {
public:
    std::int16_t width() const;
    std::int16_t height() const;

    void fill_screen(display_color color);
    void fill_rect(const display_rect& rect, display_color color);
    void fill_rect(
        std::int16_t x,
        std::int16_t y,
        std::int16_t width,
        std::int16_t height,
        display_color color);
    void draw_rect(const display_rect& rect, display_color color);
    void draw_rect(
        std::int16_t x,
        std::int16_t y,
        std::int16_t width,
        std::int16_t height,
        display_color color);
    void draw_horizontal_line(
        std::int16_t x,
        std::int16_t y,
        std::int16_t width,
        display_color color);
    void draw_line(
        std::int16_t x0,
        std::int16_t y0,
        std::int16_t x1,
        std::int16_t y1,
        display_color color);
    void fill_triangle(
        std::int16_t x0,
        std::int16_t y0,
        std::int16_t x1,
        std::int16_t y1,
        std::int16_t x2,
        std::int16_t y2,
        display_color color);

    void set_font(display_font font);
    void set_text_color(display_color foreground, display_color background);
    void set_text_alignment(display_text_alignment alignment);
    void set_text_size(std::uint8_t size);
    void draw_text(const char* text, std::int16_t x, std::int16_t y);
    std::int32_t text_width(const char* text) const;
};

// Initializes the active display backend.
bool hal_display_init();

// Returns the hardware-owned generic drawing surface.
display_surface& hal_display_surface();

// Commits one framebuffer region and reports the waveform actually executed.
display_refresh_result hal_display_refresh(
    const display_rect& rect,
    refresh_mode mode);

// Puts the active display backend into its idle low-power state.
bool hal_display_sleep();

// Applies one front-light level through the shared internal I2C bus.
bool hal_display_set_front_light(std::uint8_t brightness);
