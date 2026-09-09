#include "gray4_otp_encoding.hpp"

namespace paper_mono {
namespace {

bool otp_plane_bit(display_gray4 level, gray4_otp_plane plane)
{
    const std::uint8_t value = static_cast<std::uint8_t>(level);
    return plane == gray4_otp_plane::ram_1
               ? (value & 0x01U) == 0U
               : (value & 0x02U) == 0U;
}

}  // namespace

std::uint8_t gray4_mono_byte(
    const gray4_framebuffer_view& frame,
    std::uint16_t row,
    std::uint16_t byte_index)
{
    if (!frame.valid() || row >= frame.height() ||
        static_cast<std::uint32_t>(byte_index) * 8U >= frame.width()) {
        return 0xFFU;
    }
    std::uint8_t result = 0U;
    for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
        display_gray4 level = display_gray4::white;
        const std::uint16_t x = static_cast<std::uint16_t>(byte_index * 8U + bit);
        if (x < frame.width() && frame.get_pixel(x, row, level) &&
            static_cast<std::uint8_t>(level) >=
                static_cast<std::uint8_t>(display_gray4::light_gray)) {
            result |= static_cast<std::uint8_t>(0x80U >> bit);
        }
    }
    return result;
}

std::uint8_t gray4_otp_plane_byte(
    const gray4_framebuffer_view& frame,
    std::uint16_t row,
    std::uint16_t stream_byte_index,
    gray4_otp_plane plane)
{
    if (!frame.valid() || row >= frame.height() ||
        static_cast<std::uint32_t>(stream_byte_index) * 8U >= frame.width()) {
        return 0U;
    }
    std::uint8_t result = 0U;
    for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
        const std::uint32_t stream_pixel =
            static_cast<std::uint32_t>(stream_byte_index) * 8U + bit;
        if (stream_pixel >= frame.width()) {
            continue;
        }
        const std::uint16_t x = static_cast<std::uint16_t>(
            frame.width() - 1U - stream_pixel);
        display_gray4 level = display_gray4::white;
        if (frame.get_pixel(x, row, level) && otp_plane_bit(level, plane)) {
            result |= static_cast<std::uint8_t>(0x80U >> bit);
        }
    }
    return result;
}

}  // namespace paper_mono
