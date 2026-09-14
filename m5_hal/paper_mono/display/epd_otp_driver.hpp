/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 SeeSeeBook contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace paper_mono {

enum class otp_refresh_kind : std::uint8_t {
    partial,
    full_mono,
    full_gray4,
};

enum class otp_refresh_error : std::uint8_t {
    none,
    invalid_request,
    internal_i2c_unavailable,
    transport_failure,
};

struct otp_refresh_rect {
    std::uint16_t left;
    std::uint16_t top;
    std::uint16_t width;
    std::uint16_t height;
};

struct otp_refresh_result {
    bool success;
    otp_refresh_kind actual_kind;
    std::uint32_t duration_ms;
    otp_refresh_error error;
};

// Initializes the SSD1677 and stores an invisible white differential baseline.
// The input uses the shared native 2bpp framebuffer contract.
bool epd_otp_driver_init(
    const std::uint8_t* white_frame,
    std::size_t frame_size);

// Refreshes the native 2bpp framebuffer through an official OTP sequence.
// PaperMono's OTP 0xFF partial sequence requires a complete next frame in RAM1.
// The rectangle remains part of the request contract for validation and logs;
// both full modes also transfer the entire panel.
otp_refresh_result epd_otp_driver_refresh(
    const std::uint8_t* frame,
    std::size_t frame_size,
    const otp_refresh_rect& rect,
    otp_refresh_kind requested_kind);

// Puts an idle panel into deep sleep. Repeated calls are harmless.
bool epd_otp_driver_sleep();

}  // namespace paper_mono
