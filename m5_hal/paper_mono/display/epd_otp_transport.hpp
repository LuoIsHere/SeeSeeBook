/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 SeeSeeBook contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace paper_mono::epd_otp_transport {

// Takes ownership of the PaperMono EPD SPI2 bus after M5Unified board setup.
bool init();

// Starts and ends one finite-time SPI transaction session.
bool begin_write();
bool end_write();

// Waits for the SSD1677 BUSY signal with a finite timeout.
bool wait_ready(std::uint32_t timeout_ms);

// Enables the EPD rail and resets the controller through M5IOE1.
bool hardware_reset();

void write_command(std::uint8_t command);
void write_data(
    const std::uint8_t* data,
    std::size_t length,
    bool invert = false);
void write_register(
    std::uint8_t command,
    std::initializer_list<std::uint8_t> data);
void write_u16(std::uint16_t value);

}  // namespace paper_mono::epd_otp_transport
