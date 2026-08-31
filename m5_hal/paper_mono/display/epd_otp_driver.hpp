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
};

struct otp_refresh_result {
    bool success;
    otp_refresh_kind actual_kind;
    std::uint32_t duration_ms;
};

// Initializes the SSD1677 and stores an invisible white differential baseline.
bool epd_otp_driver_init(
    const std::uint8_t* white_frame,
    std::size_t frame_size);

// Sends one complete 1-bit native framebuffer through the OTP refresh path.
otp_refresh_result epd_otp_driver_refresh(
    const std::uint8_t* frame,
    std::size_t frame_size,
    otp_refresh_kind requested_kind);

}  // namespace paper_mono
