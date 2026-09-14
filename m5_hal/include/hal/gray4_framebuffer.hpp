#pragma once

#include <cstddef>
#include <cstdint>

enum class display_gray4 : std::uint8_t {
    black = 0U,
    dark_gray = 1U,
    light_gray = 2U,
    white = 3U,
};

// Non-owning row-major 2bpp framebuffer view. Four pixels are packed in each
// byte; the first pixel occupies bits 7:6 and the fourth occupies bits 1:0.
class gray4_framebuffer_view {
public:
    gray4_framebuffer_view() = default;
    gray4_framebuffer_view(
        std::uint8_t* data,
        std::size_t size,
        std::uint16_t width,
        std::uint16_t height);

    static constexpr std::size_t required_size(
        std::uint16_t width,
        std::uint16_t height)
    {
        return (static_cast<std::size_t>(width) * height + 3U) / 4U;
    }

    bool valid() const;
    std::uint16_t width() const;
    std::uint16_t height() const;
    std::size_t size() const;
    std::uint8_t* data() const;

    bool set_pixel(
        std::uint16_t x,
        std::uint16_t y,
        display_gray4 level);
    bool get_pixel(
        std::uint16_t x,
        std::uint16_t y,
        display_gray4& level) const;
    bool fill(display_gray4 level);
    bool fill_rect(
        std::uint16_t x,
        std::uint16_t y,
        std::uint16_t width,
        std::uint16_t height,
        display_gray4 level);
    bool has_intermediate_gray() const;

private:
    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0U;
    std::uint16_t width_ = 0U;
    std::uint16_t height_ = 0U;
};

// Matches M5GFX's grayscale conversion weights used by the image decoder.
std::uint8_t gray4_luminance(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue);

// Uniform four-bin mapping: 0..63 black, 64..127 dark gray,
// 128..191 light gray, and 192..255 white.
display_gray4 gray4_quantize(std::uint8_t luminance);

// Maps 8-bit luminance to the two endpoint levels using a stable 4x4 Bayer
// pattern. This is an explicit monochrome projection, not a Gray4 level.
display_gray4 gray4_mono_dither(
    std::uint8_t luminance,
    std::uint16_t x,
    std::uint16_t y);
