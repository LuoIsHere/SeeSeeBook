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
constexpr std::uint8_t command_update_control_1 = 0x21U;
constexpr std::uint8_t command_update_control_2 = 0x22U;
constexpr std::uint8_t command_write_ram_1 = 0x24U;
constexpr std::uint8_t command_write_ram_2 = 0x26U;
constexpr std::uint8_t command_data_entry_mode = 0x11U;
constexpr std::uint8_t command_ram_x_range = 0x44U;
constexpr std::uint8_t command_ram_y_range = 0x45U;
constexpr std::uint8_t command_ram_x_counter = 0x4eU;
constexpr std::uint8_t command_ram_y_counter = 0x4fU;

bool driver_initialized = false;
bool baseline_ready = false;
bool panel_awake = false;

otp_refresh_error current_refresh_error()
{
    return epd_otp_transport::last_error() ==
                   epd_otp_transport::transport_error::internal_i2c_unavailable
               ? otp_refresh_error::internal_i2c_unavailable
               : otp_refresh_error::transport_failure;
}

std::uint32_t monotonic_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

const char* refresh_kind_name(otp_refresh_kind kind)
{
    switch (kind) {
        case otp_refresh_kind::partial:
            return "partial_otp";
        case otp_refresh_kind::full_mono:
            return "quality";
    }
    return "unknown";
}

otp_refresh_rect full_native_rect()
{
    return {
        0U,
        0U,
        PAPER_MONO_EPD_NATIVE_WIDTH,
        PAPER_MONO_EPD_NATIVE_HEIGHT,
    };
}

bool valid_refresh_rect(const otp_refresh_rect& rect)
{
    return rect.width > 0U && rect.height > 0U &&
           (rect.left & 0x07U) == 0U &&
           (rect.width & 0x07U) == 0U &&
           static_cast<std::uint32_t>(rect.left) + rect.width <=
               PAPER_MONO_EPD_NATIVE_WIDTH &&
           static_cast<std::uint32_t>(rect.top) + rect.height <=
               PAPER_MONO_EPD_NATIVE_HEIGHT;
}

void set_ram_window(const otp_refresh_rect& rect)
{
    const std::uint16_t right = rect.left + rect.width - 1U;
    const std::uint16_t bottom = rect.top + rect.height - 1U;

    epd_otp_transport::write_register(command_data_entry_mode, {0x03U});

    epd_otp_transport::write_command(command_ram_x_range);
    epd_otp_transport::write_u16(rect.left);
    epd_otp_transport::write_u16(right);

    epd_otp_transport::write_command(command_ram_y_range);
    epd_otp_transport::write_u16(rect.top);
    epd_otp_transport::write_u16(bottom);

    epd_otp_transport::write_command(command_ram_x_counter);
    epd_otp_transport::write_u16(rect.left);
    epd_otp_transport::write_command(command_ram_y_counter);
    epd_otp_transport::write_u16(rect.top);
}

std::size_t write_ram_region(
    std::uint8_t command,
    const std::uint8_t* frame,
    const otp_refresh_rect& rect,
    bool invert = false)
{
    const std::size_t row_bytes = rect.width / 8U;
    const std::size_t first_byte = rect.left / 8U;

    set_ram_window(rect);
    epd_otp_transport::write_command(command);
    if (row_bytes == PAPER_MONO_EPD_BYTES_PER_ROW) {
        const std::size_t frame_offset =
            static_cast<std::size_t>(rect.top) *
            PAPER_MONO_EPD_BYTES_PER_ROW;
        epd_otp_transport::write_data(
            frame + frame_offset,
            row_bytes * rect.height,
            invert);
        return row_bytes * rect.height;
    }

    for (std::uint16_t row = 0U; row < rect.height; ++row) {
        const std::size_t frame_offset =
            (static_cast<std::size_t>(rect.top) + row) *
                PAPER_MONO_EPD_BYTES_PER_ROW +
            first_byte;
        epd_otp_transport::write_data(frame + frame_offset, row_bytes, invert);
    }
    return row_bytes * rect.height;
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
    epd_otp_transport::write_register(command_update_control_1, {0x00U});
    set_ram_window(full_native_rect());
    return epd_otp_transport::end_write();
}

bool reset_and_initialize_panel()
{
    const std::uint32_t start_ms = monotonic_ms();
    if (!epd_otp_transport::hardware_reset()) {
        return false;
    }
    panel_awake = true;
    const bool success = init_mono_mode();
    ESP_LOGI(
        log_tag,
        "stage=reset_init success=%d duration_ms=%lu",
        success,
        static_cast<unsigned long>(monotonic_ms() - start_ms));
    return success;
}

bool prepare_partial_mode()
{
    const std::uint32_t start_ms = monotonic_ms();
    const bool wake_required = !panel_awake;
    if (wake_required) {
        if (!epd_otp_transport::hardware_reset() ||
            !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS)) {
            return false;
        }
        panel_awake = true;
    }
    if (!epd_otp_transport::begin_write()) {
        return false;
    }
    // The official partial path floats the border after waking. Repeat the
    // register write while already awake so a preceding Quality refresh cannot
    // leave the full-refresh border waveform active.
    epd_otp_transport::write_register(0x3cU, {0x80U});
    const bool success = epd_otp_transport::end_write();
    ESP_LOGI(
        log_tag,
        "stage=partial_prepare wake=%d success=%d duration_ms=%lu",
        wake_required,
        success,
        static_cast<unsigned long>(monotonic_ms() - start_ms));
    return success;
}

bool load_white_baseline(const std::uint8_t* white_frame)
{
    if (!reset_and_initialize_panel() || !epd_otp_transport::begin_write()) {
        return false;
    }

    // Loading both planes without activation creates the first differential
    // baseline without flashing the panel during system startup.
    const otp_refresh_rect rect = full_native_rect();
    const std::uint32_t transfer_start_ms = monotonic_ms();
    const std::size_t transferred_bytes =
        write_ram_region(command_write_ram_2, white_frame, rect) +
        write_ram_region(command_write_ram_1, white_frame, rect);
    const bool success = epd_otp_transport::end_write();
    ESP_LOGI(
        log_tag,
        "stage=baseline_transfer bytes=%u success=%d duration_ms=%lu",
        static_cast<unsigned>(transferred_bytes),
        success,
        static_cast<unsigned long>(monotonic_ms() - transfer_start_ms));
    return success;
}

bool refresh_partial_otp(const std::uint8_t* frame)
{
    if (!baseline_ready || !prepare_partial_mode() ||
        !epd_otp_transport::begin_write()) {
        return false;
    }

    // This is the exact monochrome Partial sequence from the PaperMono OTP
    // demo: upload the complete current frame to RAM1, select OTP mode 2, and
    // activate once. RAM2 must not be synchronized after activation.
    const otp_refresh_rect rect = full_native_rect();
    const std::uint32_t transfer_start_ms = monotonic_ms();
    const std::size_t transferred_bytes =
        write_ram_region(command_write_ram_1, frame, rect);
    epd_otp_transport::write_register(command_update_control_1, {0x00U});
    epd_otp_transport::write_register(command_update_control_2, {0xffU});
    epd_otp_transport::write_command(command_master_activation);
    const bool success = epd_otp_transport::end_write() &&
                         epd_otp_transport::wait_ready(
                             PAPER_MONO_EPD_BUSY_TIMEOUT_MS);
    ESP_LOGI(
        log_tag,
        "stage=partial_otp bytes=%u success=%d duration_ms=%lu",
        static_cast<unsigned>(transferred_bytes),
        success,
        static_cast<unsigned long>(monotonic_ms() - transfer_start_ms));
    return success;
}

bool refresh_full_mono(const std::uint8_t* frame)
{
    if (!reset_and_initialize_panel() ||
        !epd_otp_transport::begin_write()) {
        return false;
    }

    const otp_refresh_rect rect = full_native_rect();
    const std::uint32_t inverted_start_ms = monotonic_ms();
    epd_otp_transport::write_register(command_update_control_2, {0xf8U});
    const std::size_t inverted_bytes =
        write_ram_region(command_write_ram_1, frame, rect, true);
    epd_otp_transport::write_command(command_master_activation);
    if (!epd_otp_transport::end_write() ||
        !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS)) {
        return false;
    }
    ESP_LOGI(
        log_tag,
        "stage=quality_invert bytes=%u duration_ms=%lu",
        static_cast<unsigned>(inverted_bytes),
        static_cast<unsigned long>(monotonic_ms() - inverted_start_ms));

    if (!epd_otp_transport::begin_write()) {
        return false;
    }
    const std::uint32_t final_start_ms = monotonic_ms();
    epd_otp_transport::write_register(command_update_control_2, {0x14U});
    const std::size_t final_bytes =
        write_ram_region(command_write_ram_2, frame, rect) +
        write_ram_region(command_write_ram_1, frame, rect);
    epd_otp_transport::write_command(command_master_activation);
    if (!epd_otp_transport::end_write() ||
        !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS)) {
        return false;
    }
    ESP_LOGI(
        log_tag,
        "stage=quality_final bytes=%u duration_ms=%lu",
        static_cast<unsigned>(final_bytes),
        static_cast<unsigned long>(monotonic_ms() - final_start_ms));
    return true;
}

}  // namespace

bool epd_otp_driver_init(
    const std::uint8_t* white_frame,
    std::size_t frame_size)
{
    epd_otp_transport::clear_error();
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
    const otp_refresh_rect& rect,
    otp_refresh_kind requested_kind)
{
    otp_refresh_result result = {
        false,
        requested_kind,
        0U,
        otp_refresh_error::invalid_request,
    };
    if (!driver_initialized || frame == nullptr ||
        frame_size != PAPER_MONO_EPD_FRAME_SIZE || !valid_refresh_rect(rect)) {
        ESP_LOGE(log_tag, "refresh rejected; driver, framebuffer, or rect invalid");
        return result;
    }

    // Only a missing baseline may force Quality. Ghost cleanup policy belongs
    // exclusively to the UI renderer's per-region debt counters.
    epd_otp_transport::clear_error();
    result.actual_kind = !baseline_ready
                             ? otp_refresh_kind::full_mono
                             : requested_kind;
    const std::uint32_t start_ms = monotonic_ms();
    ESP_LOGI(
        log_tag,
        "refresh begin requested=%s actual=%s requested_rect=%u,%u %ux%u transfer=full_frame awake=%d baseline=%d",
        refresh_kind_name(requested_kind),
        refresh_kind_name(result.actual_kind),
        static_cast<unsigned>(rect.left),
        static_cast<unsigned>(rect.top),
        static_cast<unsigned>(rect.width),
        static_cast<unsigned>(rect.height),
        panel_awake,
        baseline_ready);

    result.success = result.actual_kind == otp_refresh_kind::full_mono
                         ? refresh_full_mono(frame)
                         : refresh_partial_otp(frame);
    result.duration_ms = monotonic_ms() - start_ms;

    if (result.success) {
        baseline_ready = true;
        result.error = otp_refresh_error::none;
    } else {
        // A failed activation leaves controller RAM state uncertain. The next
        // request rebuilds it through the baseline safety path.
        baseline_ready = false;
        result.error = current_refresh_error();
    }

    ESP_LOGI(
        log_tag,
        "refresh end requested=%s actual=%s success=%d error=%u duration_ms=%lu",
        refresh_kind_name(requested_kind),
        refresh_kind_name(result.actual_kind),
        result.success,
        static_cast<unsigned>(result.error),
        static_cast<unsigned long>(result.duration_ms));
    return result;
}

bool epd_otp_driver_sleep()
{
    if (!driver_initialized || !panel_awake) {
        return true;
    }

    const std::uint32_t start_ms = monotonic_ms();
    epd_otp_transport::clear_error();
    if (!epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS) ||
        !epd_otp_transport::begin_write()) {
        return false;
    }
    epd_otp_transport::write_register(command_deep_sleep, {0x01U});
    if (!epd_otp_transport::end_write()) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(PAPER_MONO_EPD_DEEP_SLEEP_DELAY_MS));
    panel_awake = false;
    ESP_LOGI(
        log_tag,
        "stage=deep_sleep success=1 duration_ms=%lu",
        static_cast<unsigned long>(monotonic_ms() - start_ms));
    return true;
}

}  // namespace paper_mono
