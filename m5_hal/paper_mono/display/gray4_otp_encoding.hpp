#pragma once

#include <cstdint>

#include "gray4_framebuffer.hpp"

namespace paper_mono {

enum class gray4_otp_plane : std::uint8_t {
    ram_1,
    ram_2,
};

// Produces one MSB-first SSD1677 byte for monochrome mode. Intermediate levels
// are thresholded only as a defensive fallback; the HAL selects this path only
// for frames containing black and white.
std::uint8_t gray4_mono_byte(
    const gray4_framebuffer_view& frame,
    std::uint16_t row,
    std::uint16_t byte_index);

// Gray OTP mode streams controller X from 799 down to 0. This helper reverses
// the source pixel order and maps levels to the official plane encoding:
// white=00, light=10, dark=01, black=11 (RAM1, RAM2).
std::uint8_t gray4_otp_plane_byte(
    const gray4_framebuffer_view& frame,
    std::uint16_t row,
    std::uint16_t stream_byte_index,
    gray4_otp_plane plane);

}  // namespace paper_mono

