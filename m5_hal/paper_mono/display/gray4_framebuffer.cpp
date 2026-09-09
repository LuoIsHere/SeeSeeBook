#include "gray4_framebuffer.hpp"

#include <cstring>

namespace {

bool valid_level(display_gray4 level)
{
    return static_cast<std::uint8_t>(level) <=
           static_cast<std::uint8_t>(display_gray4::white);
}

std::uint8_t fill_byte(display_gray4 level)
{
    const std::uint8_t value = static_cast<std::uint8_t>(level);
    return static_cast<std::uint8_t>(value | (value << 2U) |
                                     (value << 4U) | (value << 6U));
}

}  // namespace

gray4_framebuffer_view::gray4_framebuffer_view(
    std::uint8_t* data,
    std::size_t size,
    std::uint16_t width,
    std::uint16_t height)
    : data_(data), size_(size), width_(width), height_(height)
{
}

bool gray4_framebuffer_view::valid() const
{
    return data_ != nullptr && width_ != 0U && height_ != 0U &&
           size_ == required_size(width_, height_);
}

std::uint16_t gray4_framebuffer_view::width() const
{
    return width_;
}

std::uint16_t gray4_framebuffer_view::height() const
{
    return height_;
}

std::size_t gray4_framebuffer_view::size() const
{
    return size_;
}

std::uint8_t* gray4_framebuffer_view::data() const
{
    return data_;
}

bool gray4_framebuffer_view::set_pixel(
    std::uint16_t x,
    std::uint16_t y,
    display_gray4 level)
{
    if (!valid() || !valid_level(level) || x >= width_ || y >= height_) {
        return false;
    }
    const std::size_t pixel = static_cast<std::size_t>(y) * width_ + x;
    const std::uint8_t shift = static_cast<std::uint8_t>(
        6U - 2U * (pixel & 0x03U));
    const std::uint8_t mask = static_cast<std::uint8_t>(0x03U << shift);
    data_[pixel >> 2U] = static_cast<std::uint8_t>(
        (data_[pixel >> 2U] & ~mask) |
        (static_cast<std::uint8_t>(level) << shift));
    return true;
}

bool gray4_framebuffer_view::get_pixel(
    std::uint16_t x,
    std::uint16_t y,
    display_gray4& level) const
{
    if (!valid() || x >= width_ || y >= height_) {
        return false;
    }
    const std::size_t pixel = static_cast<std::size_t>(y) * width_ + x;
    const std::uint8_t shift = static_cast<std::uint8_t>(
        6U - 2U * (pixel & 0x03U));
    level = static_cast<display_gray4>((data_[pixel >> 2U] >> shift) & 0x03U);
    return true;
}

bool gray4_framebuffer_view::fill(display_gray4 level)
{
    if (!valid() || !valid_level(level)) {
        return false;
    }
    std::memset(data_, fill_byte(level), size_);
    return true;
}

bool gray4_framebuffer_view::fill_rect(
    std::uint16_t x,
    std::uint16_t y,
    std::uint16_t width,
    std::uint16_t height,
    display_gray4 level)
{
    if (!valid() || !valid_level(level) || width == 0U || height == 0U ||
        x >= width_ || y >= height_ ||
        static_cast<std::uint32_t>(x) + width > width_ ||
        static_cast<std::uint32_t>(y) + height > height_) {
        return false;
    }
    for (std::uint16_t row = y; row < y + height; ++row) {
        for (std::uint16_t column = x; column < x + width; ++column) {
            set_pixel(column, row, level);
        }
    }
    return true;
}

bool gray4_framebuffer_view::has_intermediate_gray() const
{
    if (!valid()) {
        return false;
    }
    for (std::size_t index = 0U; index < size_; ++index) {
        // A 2-bit pixel is intermediate exactly when its two bits differ.
        if (((data_[index] ^ (data_[index] >> 1U)) & 0x55U) != 0U) {
            return true;
        }
    }
    return false;
}

std::uint8_t gray4_luminance(
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue)
{
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(red) * 77U +
         static_cast<std::uint32_t>(green) * 151U +
         static_cast<std::uint32_t>(blue) * 29U) >> 8U);
}

display_gray4 gray4_quantize(std::uint8_t luminance)
{
    return static_cast<display_gray4>(luminance >> 6U);
}

