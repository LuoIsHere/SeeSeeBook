/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 SeeSeeBook contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "epd_otp_driver.hpp"

#include <array>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "epd_otp_transport.hpp"
#include "gray4_framebuffer.hpp"
#include "gray4_otp_encoding.hpp"
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
            return "full_mono_otp";
        case otp_refresh_kind::full_gray4:
            return "full_gray4_otp";
    }
    return "unknown";
}

otp_refresh_rect full_native_rect()
{
    return {0U, 0U, PAPER_MONO_EPD_NATIVE_WIDTH,
            PAPER_MONO_EPD_NATIVE_HEIGHT};
}

bool valid_refresh_rect(const otp_refresh_rect& rect)
{
    return rect.width > 0U && rect.height > 0U &&
           (rect.left & 0x07U) == 0U && (rect.width & 0x07U) == 0U &&
           static_cast<std::uint32_t>(rect.left) + rect.width <=
               PAPER_MONO_EPD_NATIVE_WIDTH &&
           static_cast<std::uint32_t>(rect.top) + rect.height <=
               PAPER_MONO_EPD_NATIVE_HEIGHT;
}

gray4_framebuffer_view frame_view(
    const std::uint8_t* frame,
    std::size_t frame_size)
{
    return gray4_framebuffer_view(
        const_cast<std::uint8_t*>(frame), frame_size,
        PAPER_MONO_EPD_NATIVE_WIDTH, PAPER_MONO_EPD_NATIVE_HEIGHT);
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

std::size_t write_mono_frame(
    std::uint8_t command,
    const gray4_framebuffer_view& frame,
    bool invert = false)
{
    set_ram_window(full_native_rect());
    epd_otp_transport::write_command(command);
    std::array<std::uint8_t, PAPER_MONO_EPD_BYTES_PER_ROW> row = {};
    for (std::uint16_t y = 0U; y < PAPER_MONO_EPD_NATIVE_HEIGHT; ++y) {
        for (std::uint16_t byte = 0U; byte < PAPER_MONO_EPD_BYTES_PER_ROW;
             ++byte) {
            row[byte] = gray4_mono_byte(frame, y, byte);
        }
        epd_otp_transport::write_data(row.data(), row.size(), invert);
    }
    return PAPER_MONO_EPD_FRAME_SIZE;
}

std::size_t write_gray_plane(
    std::uint8_t command,
    const gray4_framebuffer_view& frame,
    gray4_otp_plane plane)
{
    epd_otp_transport::write_command(command);
    std::array<std::uint8_t, PAPER_MONO_EPD_BYTES_PER_ROW> row = {};
    for (std::uint16_t y = 0U; y < PAPER_MONO_EPD_NATIVE_HEIGHT; ++y) {
        for (std::uint16_t byte = 0U; byte < PAPER_MONO_EPD_BYTES_PER_ROW;
             ++byte) {
            row[byte] = gray4_otp_plane_byte(frame, y, byte, plane);
        }
        epd_otp_transport::write_data(row.data(), row.size());
    }
    return PAPER_MONO_EPD_FRAME_SIZE;
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
        0x0cU, {0xaeU, 0xc7U, 0xc3U, 0xc0U, 0x80U});
    epd_otp_transport::write_register(0x01U, {0xdfU, 0x01U, 0x02U});
    epd_otp_transport::write_register(0x3cU, {0x01U});
    epd_otp_transport::write_register(command_update_control_1, {0x00U});
    set_ram_window(full_native_rect());
    return epd_otp_transport::end_write();
}

bool reset_and_initialize_mono()
{
    const std::uint32_t start_ms = monotonic_ms();
    if (!epd_otp_transport::hardware_reset()) {
        return false;
    }
    panel_awake = true;
    const bool success = init_mono_mode();
    ESP_LOGI(log_tag, "stage=mono_init success=%d duration_ms=%lu", success,
             static_cast<unsigned long>(monotonic_ms() - start_ms));
    return success;
}

bool init_gray_mode()
{
    const std::uint32_t start_ms = monotonic_ms();
    if (!epd_otp_transport::hardware_reset()) {
        return false;
    }
    panel_awake = true;
    if (!software_reset() || !epd_otp_transport::begin_write()) {
        return false;
    }
    epd_otp_transport::write_register(
        0x0cU, {0xaeU, 0xc7U, 0xc3U, 0xc0U, 0x80U});
    epd_otp_transport::write_register(0x01U, {0xdfU, 0x01U, 0x02U});
    // Official Gray4 sequence: controller X decreases while Y increases.
    epd_otp_transport::write_register(command_data_entry_mode, {0x02U});
    epd_otp_transport::write_command(command_ram_x_range);
    epd_otp_transport::write_u16(PAPER_MONO_EPD_NATIVE_WIDTH - 1U);
    epd_otp_transport::write_u16(0U);
    epd_otp_transport::write_command(command_ram_y_range);
    epd_otp_transport::write_u16(0U);
    epd_otp_transport::write_u16(PAPER_MONO_EPD_NATIVE_HEIGHT - 1U);
    epd_otp_transport::write_command(command_ram_x_counter);
    epd_otp_transport::write_u16(PAPER_MONO_EPD_NATIVE_WIDTH - 1U);
    epd_otp_transport::write_command(command_ram_y_counter);
    epd_otp_transport::write_u16(0U);
    epd_otp_transport::write_register(0x3cU, {0x01U});
    epd_otp_transport::write_register(0x18U, {0x80U});
    epd_otp_transport::write_register(0x1aU, {0x5aU});
    const bool success = epd_otp_transport::end_write();
    ESP_LOGI(log_tag, "stage=gray4_init success=%d duration_ms=%lu", success,
             static_cast<unsigned long>(monotonic_ms() - start_ms));
    return success;
}

bool prepare_partial_mode()
{
    if (!panel_awake) {
        if (!epd_otp_transport::hardware_reset() ||
            !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS)) {
            return false;
        }
        panel_awake = true;
    }
    if (!epd_otp_transport::begin_write()) {
        return false;
    }
    epd_otp_transport::write_register(0x3cU, {0x80U});
    return epd_otp_transport::end_write();
}

bool load_white_baseline(const gray4_framebuffer_view& frame)
{
    if (!reset_and_initialize_mono() || !epd_otp_transport::begin_write()) {
        return false;
    }
    const std::size_t bytes =
        write_mono_frame(command_write_ram_2, frame) +
        write_mono_frame(command_write_ram_1, frame);
    const bool success = epd_otp_transport::end_write();
    ESP_LOGI(log_tag, "stage=baseline_transfer bytes=%u success=%d",
             static_cast<unsigned>(bytes), success);
    return success;
}

bool refresh_partial_otp(const gray4_framebuffer_view& frame)
{
    if (!baseline_ready || !prepare_partial_mode() ||
        !epd_otp_transport::begin_write()) {
        return false;
    }
    // The PaperMono OTP 0xFF sequence requires the complete next image in
    // RAM1. Restricting this write to the dirty rectangle leaves the rest of
    // RAM1 on an older page and produces mixed-page refreshes.
    const std::size_t bytes = write_mono_frame(command_write_ram_1, frame);
    epd_otp_transport::write_register(command_update_control_1, {0x00U});
    epd_otp_transport::write_register(command_update_control_2, {0xffU});
    epd_otp_transport::write_command(command_master_activation);
    const bool success = epd_otp_transport::end_write() &&
                         epd_otp_transport::wait_ready(
                             PAPER_MONO_EPD_BUSY_TIMEOUT_MS);
    ESP_LOGI(log_tag, "stage=partial_otp bytes=%u success=%d",
             static_cast<unsigned>(bytes), success);
    return success;
}

bool refresh_full_mono(const gray4_framebuffer_view& frame)
{
    if (!reset_and_initialize_mono() || !epd_otp_transport::begin_write()) {
        return false;
    }
    epd_otp_transport::write_register(command_update_control_2, {0xf8U});
    const std::size_t inverted_bytes =
        write_mono_frame(command_write_ram_1, frame, true);
    epd_otp_transport::write_command(command_master_activation);
    if (!epd_otp_transport::end_write() ||
        !epd_otp_transport::wait_ready(PAPER_MONO_EPD_BUSY_TIMEOUT_MS)) {
        return false;
    }
    if (!epd_otp_transport::begin_write()) {
        return false;
    }
    epd_otp_transport::write_register(command_update_control_2, {0x14U});
    const std::size_t final_bytes =
        write_mono_frame(command_write_ram_2, frame) +
        write_mono_frame(command_write_ram_1, frame);
    epd_otp_transport::write_command(command_master_activation);
    const bool success = epd_otp_transport::end_write() &&
                         epd_otp_transport::wait_ready(
                             PAPER_MONO_EPD_BUSY_TIMEOUT_MS);
    ESP_LOGI(log_tag, "stage=full_mono inverted=%u final=%u success=%d",
             static_cast<unsigned>(inverted_bytes),
             static_cast<unsigned>(final_bytes), success);
    return success;
}

bool refresh_full_gray4(const gray4_framebuffer_view& frame)
{
    if (!init_gray_mode() || !epd_otp_transport::begin_write()) {
        return false;
    }
    const std::size_t bytes =
        write_gray_plane(command_write_ram_1, frame, gray4_otp_plane::ram_1) +
        write_gray_plane(command_write_ram_2, frame, gray4_otp_plane::ram_2);
    epd_otp_transport::write_register(command_update_control_2, {0xd7U});
    epd_otp_transport::write_command(command_master_activation);
    const bool success = epd_otp_transport::end_write() &&
                         epd_otp_transport::wait_ready(
                             PAPER_MONO_EPD_BUSY_TIMEOUT_MS);
    ESP_LOGI(log_tag, "stage=full_gray4 bytes=%u success=%d",
             static_cast<unsigned>(bytes), success);
    return success;
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
    const gray4_framebuffer_view frame = frame_view(white_frame, frame_size);
    if (!frame.valid()) {
        ESP_LOGE(log_tag, "invalid initial Gray4 frame size=%u expected=%u",
                 static_cast<unsigned>(frame_size),
                 static_cast<unsigned>(PAPER_MONO_GRAY4_FRAME_SIZE));
        return false;
    }
    baseline_ready = false;
    if (!epd_otp_transport::init() || !load_white_baseline(frame)) {
        ESP_LOGE(log_tag, "initialize SSD1677 OTP backend failed");
        return false;
    }
    baseline_ready = true;
    driver_initialized = true;
    ESP_LOGI(log_tag, "white monochrome baseline ready from Gray4 framebuffer");
    return true;
}

otp_refresh_result epd_otp_driver_refresh(
    const std::uint8_t* frame_data,
    std::size_t frame_size,
    const otp_refresh_rect& rect,
    otp_refresh_kind requested_kind)
{
    otp_refresh_result result = {
        false, requested_kind, 0U, otp_refresh_error::invalid_request};
    const gray4_framebuffer_view frame = frame_view(frame_data, frame_size);
    if (!driver_initialized || !frame.valid() || !valid_refresh_rect(rect)) {
        ESP_LOGE(log_tag, "refresh rejected; driver, framebuffer, or rect invalid");
        return result;
    }

    epd_otp_transport::clear_error();
    result.actual_kind =
        requested_kind == otp_refresh_kind::partial && !baseline_ready
            ? otp_refresh_kind::full_mono
            : requested_kind;
    const std::uint32_t start_ms = monotonic_ms();
    ESP_LOGI(log_tag,
             "refresh begin requested=%s actual=%s rect=%u,%u %ux%u baseline=%d",
             refresh_kind_name(requested_kind), refresh_kind_name(result.actual_kind),
             static_cast<unsigned>(rect.left), static_cast<unsigned>(rect.top),
             static_cast<unsigned>(rect.width), static_cast<unsigned>(rect.height),
             baseline_ready);

    switch (result.actual_kind) {
        case otp_refresh_kind::partial:
            result.success = refresh_partial_otp(frame);
            break;
        case otp_refresh_kind::full_mono:
            result.success = refresh_full_mono(frame);
            break;
        case otp_refresh_kind::full_gray4:
            result.success = refresh_full_gray4(frame);
            break;
    }
    result.duration_ms = monotonic_ms() - start_ms;
    if (result.success) {
        baseline_ready = result.actual_kind != otp_refresh_kind::full_gray4;
        result.error = otp_refresh_error::none;
    } else {
        baseline_ready = false;
        result.error = current_refresh_error();
    }
    ESP_LOGI(log_tag, "refresh end actual=%s success=%d error=%u duration_ms=%lu",
             refresh_kind_name(result.actual_kind), result.success,
             static_cast<unsigned>(result.error),
             static_cast<unsigned long>(result.duration_ms));
    return result;
}

bool epd_otp_driver_sleep()
{
    if (!driver_initialized || !panel_awake) {
        return true;
    }
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
    return true;
}

}  // namespace paper_mono
