#pragma once

#include <cstdint>

#include <M5GFX.h>

#include "geometry.hpp"

#define PAPER_MONO_DISPLAY_ROTATION 0U
#define PAPER_MONO_PORTRAIT_WIDTH 480U
#define PAPER_MONO_PORTRAIT_HEIGHT 800U

enum class refresh_mode : std::uint8_t {
    fastest,
    fast,
    quality,
};

// Configures the PaperMono EPD and its normalized portrait framebuffer.
bool hal_display_init();

// Returns the hardware-owned drawing surface. Only the UI render task may use it.
M5GFX& hal_display_canvas();

// Commits one framebuffer region using the requested PaperMono waveform.
bool hal_display_refresh(
    const display_rect& rect,
    refresh_mode mode);

// Applies one front-light level through the shared internal I2C bus.
bool hal_display_set_front_light(std::uint8_t brightness);
