/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 SeeSeeBook contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "epd_otp_driver.hpp"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "epd_otp_transport.hpp"
#include "../paper_mono_config.hpp"

namespace paper_mono {
namespace {

constexpr char log_tag[] = "hal_epd_otp";

constexpr std::uint8_t command_soft_reset = 0x12U;
constexpr std::uint8_t command_deep_sleep = 0x10U;
constexpr std::uint8_t command_master_activation = 0x20U;
constexpr std::uint8_t command_update_control = 0x22U;
constexpr std::uint8_t command_write_ram_1 = 0x24U;
constexpr std::uint8_t command_write_ram_2 = 0x26U;
constexpr std::uint8_t command_data_entry_mode = 0x11U;
constexpr std::uint8_t command_ram_x_range = 0x44U;
constexpr std::uint8_t command_ram_y_range = 0x45U;
constexpr std::uint8_t command_ram_x_counter = 0x4eU;
constexpr std::uint8_t command_ram_y_counter = 0x4fU;

bool driver_initialized = false;
bool baseline_ready = false;
std::uint8_t partial_refresh_count = 0U;

std::uint32_t monotonic_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void set_full_ram_window()
{
    epd_otp_transport::write_register(command_data_entry_mode, {0x03U});

    epd_otp_transport::write_command(command_ram_x_range);
    epd_otp_transport::write_u16(0U);
    epd_otp_transport::write_u16(PAPER_MONO_EPD_NATIVE_WIDTH - 1U);

    epd_otp_transport::write_command(command_ram_y_range);
    epd_otp_transport::write_u16(0U);
    epd_otp_transport::write_u16(PAPER_MONO_EPD_NATIVE_HEIGHT - 1U);

    epd_otp_transport::write_command(command_ram_x_counter);
    epd_otp_transport::write_u16(0U);
    epd_otp_transport::write_command(command_ram_y_counter);
    epd_otp_transport::write_u16(0U);
}

void write_full_ram(
    std::uint8_t command,
    const std::uint8_t* frame,
    bool invert = false)
{
    set_full_ram_window();
    epd_otp_transport::write_command(command);
    epd_otp_transport::write_data(
        frame,
        PAPER_MONO_EPD_FRAME_SIZE,
        invert);
}

bool software_reset()
{
    if (!epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS) ||
        !epd_otp_transport::begin_write()) {
        return false;
    }
    epd_otp_transport::write_command(command_soft_reset);
    if (!epd_otp_transport::end_write()) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10U));
    return epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS);
}

bool init_mono_mode()
{
    if (!software_reset() || !epd_otp_transport::begin_write()) {
        return false;
    }

    epd_otp_transport::write_register(0x18U, {0x80U});
    epd_otp_transport::write_register(
        0x0cU,
        {0xaeU, 0xc7U, 0xc3U, 0xc0U, 0x80U});
    epd_otp_transport::write_register(0x01U, {0xdfU, 0x01U, 0x02U});
    epd_otp_transport::write_register(0x3cU, {0x01U});
    epd_otp_transport::write_register(0x21U, {0x00U});
    set_full_ram_window();
    return epd_otp_transport::end_write();
}

bool deep_sleep()
{
    if (!epd_otp_transport::begin_write()) {
        return false;
    }
    epd_otp_transport::write_register(command_deep_sleep, {0x01U});
    if (!epd_otp_transport::end_write()) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(PAPER_MONO_EPD_DEEP_SLEEP_DELAY_MS));
    return true;
}

bool wake_for_partial_update()
{
    if (!epd_otp_transport::hardware_reset() ||
        !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS) ||
        !epd_otp_transport::begin_write()) {
        return false;
    }
    epd_otp_transport::write_register(0x3cU, {0x80U});
    return epd_otp_transport::end_write();
}

bool load_white_baseline(const std::uint8_t* white_frame)
{
    if (!epd_otp_transport::hardware_reset() ||
        !init_mono_mode() ||
        !epd_otp_transport::begin_write()) {
        return false;
    }

    // Loading RAM without activation establishes the first partial baseline
    // without flashing the panel during system startup.
    write_full_ram(command_write_ram_2, white_frame);
    write_full_ram(command_write_ram_1, white_frame);
    if (!epd_otp_transport::end_write()) {
        return false;
    }
    return deep_sleep();
}

bool refresh_partial(const std::uint8_t* frame)
{
    if (!baseline_ready || !wake_for_partial_update() ||
        !epd_otp_transport::begin_write()) {
        return false;
    }

    // Keep the manufacturer sequence: send the complete next frame and let
    // SSD1677 OTP Mode 2 drive only the changed monochrome pixels.
    write_full_ram(command_write_ram_1, frame);
    epd_otp_transport::write_register(0x21U, {0x00U});
    epd_otp_transport::write_register(command_update_control, {0xffU});
    epd_otp_transport::write_command(command_master_activation);
    if (!epd_otp_transport::end_write() ||
        !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS)) {
        return false;
    }
    return deep_sleep();
}

bool refresh_full_mono(const std::uint8_t* frame)
{
    if (!epd_otp_transport::hardware_reset() ||
        !init_mono_mode() ||
        !epd_otp_transport::begin_write()) {
        return false;
    }

    // Synchronize controller drive state against an inverted frame first.
    epd_otp_transport::write_register(command_update_control, {0xf8U});
    write_full_ram(command_write_ram_1, frame, true);
    epd_otp_transport::write_command(command_master_activation);
    if (!epd_otp_transport::end_write() ||
        !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS)) {
        return false;
    }

    if (!epd_otp_transport::begin_write()) {
        return false;
    }
    // OTP Mode 1 writes both planes to rebuild a valid partial baseline.
    epd_otp_transport::write_register(command_update_control, {0x14U});
    write_full_ram(command_write_ram_2, frame);
    write_full_ram(command_write_ram_1, frame);
    epd_otp_transport::write_command(command_master_activation);
    if (!epd_otp_transport::end_write() ||
        !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS)) {
        return false;
    }
    return deep_sleep();
}

}  // namespace

bool epd_otp_driver_init(
    const std::uint8_t* white_frame,
    std::size_t frame_size)
{
    if (driver_initialized) {
        return true;
    }
    if (white_frame == nullptr || frame_size != PAPER_MONO_EPD_FRAME_SIZE) {
        ESP_LOGE(
            log_tag,
            "invalid initial frame size=%u expected=%u",
            static_cast<unsigned>(frame_size),
            static_cast<unsigned>(PAPER_MONO_EPD_FRAME_SIZE));
        return false;
    }

    baseline_ready = false;
    partial_refresh_count = 0U;
    if (!epd_otp_transport::init() || !load_white_baseline(white_frame)) {
        ESP_LOGE(log_tag, "initialize SSD1677 OTP backend failed");
        return false;
    }

    baseline_ready = true;
    driver_initialized = true;
    ESP_LOGI(log_tag, "white monochrome baseline ready");
    return true;
}

otp_refresh_result epd_otp_driver_refresh(
    const std::uint8_t* frame,
    std::size_t frame_size,
    otp_refresh_kind requested_kind)
{
    otp_refresh_result result = {
        false,
        requested_kind,
        0U,
    };
    if (!driver_initialized || frame == nullptr ||
        frame_size != PAPER_MONO_EPD_FRAME_SIZE) {
        ESP_LOGE(log_tag, "refresh rejected; driver or framebuffer invalid");
        return result;
    }

    const bool cleanup_due =
        partial_refresh_count >= PAPER_MONO_EPD_PARTIAL_REFRESH_LIMIT;
    result.actual_kind =
        requested_kind == otp_refresh_kind::full_mono ||
                !baseline_ready || cleanup_due
            ? otp_refresh_kind::full_mono
            : otp_refresh_kind::partial;

    const std::uint32_t start_ms = monotonic_ms();
    if (result.actual_kind == otp_refresh_kind::full_mono) {
        result.success = refresh_full_mono(frame);
        if (result.success) {
            baseline_ready = true;
            partial_refresh_count = 0U;
        }
    } else {
        result.success = refresh_partial(frame);
        if (result.success && partial_refresh_count < UINT8_MAX) {
            ++partial_refresh_count;
        }
    }
    result.duration_ms = monotonic_ms() - start_ms;

    if (!result.success) {
        // A failed activation leaves the controller RAM state uncertain. The
        // next request therefore rebuilds it through a full monochrome cycle.
        baseline_ready = false;
    }

    ESP_LOGI(
        log_tag,
        "refresh requested=%s actual=%s success=%d duration=%lu partial_count=%u",
        requested_kind == otp_refresh_kind::full_mono ? "full" : "partial",
        result.actual_kind == otp_refresh_kind::full_mono ? "full" : "partial",
        result.success,
        static_cast<unsigned long>(result.duration_ms),
        static_cast<unsigned>(partial_refresh_count));
    return result;
}

}  // namespace paper_mono
