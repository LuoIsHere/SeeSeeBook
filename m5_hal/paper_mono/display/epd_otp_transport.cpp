/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 SeeSeeBook contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "epd_otp_transport.hpp"

#include <M5Unified.h>

#include <algorithm>
#include <cstring>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "internal_i2c.hpp"
#include "../paper_mono_config.hpp"

namespace paper_mono::epd_otp_transport {
namespace {

constexpr char log_tag[] = "hal_epd_spi";
constexpr spi_host_device_t spi_host = SPI2_HOST;
constexpr gpio_num_t pin_mosi =
    static_cast<gpio_num_t>(PAPER_MONO_EPD_GPIO_MOSI);
constexpr gpio_num_t pin_sclk =
    static_cast<gpio_num_t>(PAPER_MONO_EPD_GPIO_SCLK);
constexpr gpio_num_t pin_cs =
    static_cast<gpio_num_t>(PAPER_MONO_EPD_GPIO_CS);
constexpr gpio_num_t pin_dc =
    static_cast<gpio_num_t>(PAPER_MONO_EPD_GPIO_DC);
constexpr gpio_num_t pin_busy =
    static_cast<gpio_num_t>(PAPER_MONO_EPD_GPIO_BUSY);

spi_device_handle_t spi_device = nullptr;
std::uint8_t* staging_buffer = nullptr;
bool initialized = false;
bool writing = false;
bool write_ok = false;

std::uint32_t monotonic_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void set_write_error(esp_err_t error, const char* operation)
{
    if (write_ok) {
        ESP_LOGE(log_tag, "%s failed: %s", operation, esp_err_to_name(error));
    }
    write_ok = false;
}

void transmit(
    const std::uint8_t* bytes,
    std::size_t length,
    bool is_data,
    bool invert)
{
    if (!write_ok || !writing || bytes == nullptr || length == 0U) {
        return;
    }

    gpio_set_level(pin_dc, is_data ? 1 : 0);
    while (length != 0U && write_ok) {
        const std::size_t chunk_size = std::min<std::size_t>(
            length,
            PAPER_MONO_EPD_SPI_CHUNK_SIZE);
        const std::uint8_t* source = bytes;

        if (invert) {
            for (std::size_t index = 0U; index < chunk_size; ++index) {
                staging_buffer[index] = static_cast<std::uint8_t>(~bytes[index]);
            }
            source = staging_buffer;
        } else if (chunk_size > 4U) {
            // Sprite buffers live in PSRAM, so copy every DMA chunk internally.
            std::memcpy(staging_buffer, bytes, chunk_size);
            source = staging_buffer;
        }

        spi_transaction_t transaction = {};
        transaction.length = chunk_size * 8U;
        if (chunk_size <= 4U && source == bytes) {
            transaction.flags = SPI_TRANS_USE_TXDATA;
            std::memcpy(transaction.tx_data, source, chunk_size);
        } else {
            transaction.tx_buffer = source;
        }

        const esp_err_t error =
            spi_device_polling_transmit(spi_device, &transaction);
        if (error != ESP_OK) {
            set_write_error(error, "spi_device_polling_transmit");
            return;
        }
        bytes += chunk_size;
        length -= chunk_size;
    }
}

}  // namespace

bool init()
{
    if (initialized) {
        return true;
    }

    // M5Unified remains responsible for board detection and non-display devices.
    // Only its EPD SPI bus is released before this backend takes ownership.
    M5.Display.releaseBus();

    gpio_config_t output_config = {};
    output_config.pin_bit_mask =
        (1ULL << PAPER_MONO_EPD_GPIO_CS) |
        (1ULL << PAPER_MONO_EPD_GPIO_DC);
    output_config.mode = GPIO_MODE_OUTPUT;
    output_config.intr_type = GPIO_INTR_DISABLE;
    esp_err_t error = gpio_config(&output_config);
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "configure output GPIO failed: %s", esp_err_to_name(error));
        return false;
    }
    gpio_set_level(pin_cs, 1);
    gpio_set_level(pin_dc, 0);

    gpio_config_t busy_config = {};
    busy_config.pin_bit_mask = 1ULL << PAPER_MONO_EPD_GPIO_BUSY;
    busy_config.mode = GPIO_MODE_INPUT;
    busy_config.pull_up_en = GPIO_PULLUP_ENABLE;
    busy_config.intr_type = GPIO_INTR_DISABLE;
    error = gpio_config(&busy_config);
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "configure BUSY GPIO failed: %s", esp_err_to_name(error));
        return false;
    }

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = pin_mosi;
    bus_config.miso_io_num = -1;
    bus_config.sclk_io_num = pin_sclk;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.data4_io_num = -1;
    bus_config.data5_io_num = -1;
    bus_config.data6_io_num = -1;
    bus_config.data7_io_num = -1;
    bus_config.max_transfer_sz = PAPER_MONO_EPD_SPI_CHUNK_SIZE;
    bus_config.flags =
        SPICOMMON_BUSFLAG_MASTER |
        SPICOMMON_BUSFLAG_MOSI |
        SPICOMMON_BUSFLAG_SCLK;
    bus_config.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;

    error = spi_bus_initialize(spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "spi_bus_initialize failed: %s", esp_err_to_name(error));
        return false;
    }

    spi_device_interface_config_t device_config = {};
    device_config.mode = 0;
    device_config.clock_speed_hz = PAPER_MONO_EPD_SPI_CLOCK_HZ;
    device_config.spics_io_num = -1;
    device_config.flags = SPI_DEVICE_HALFDUPLEX;
    device_config.queue_size = 1;
    error = spi_bus_add_device(spi_host, &device_config, &spi_device);
    if (error != ESP_OK) {
        ESP_LOGE(log_tag, "spi_bus_add_device failed: %s", esp_err_to_name(error));
        spi_bus_free(spi_host);
        return false;
    }

    staging_buffer = static_cast<std::uint8_t*>(heap_caps_malloc(
        PAPER_MONO_EPD_SPI_CHUNK_SIZE,
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (staging_buffer == nullptr) {
        ESP_LOGE(log_tag, "allocate SPI staging buffer failed");
        spi_bus_remove_device(spi_device);
        spi_device = nullptr;
        spi_bus_free(spi_host);
        return false;
    }

    initialized = true;
    ESP_LOGI(
        log_tag,
        "SPI2 backend ready mode=0 clock=%d",
        PAPER_MONO_EPD_SPI_CLOCK_HZ);
    return true;
}

bool begin_write()
{
    if (!initialized || spi_device == nullptr || writing) {
        ESP_LOGE(
            log_tag,
            "invalid SPI session initialized=%d writing=%d",
            initialized,
            writing);
        return false;
    }

    write_ok = true;
    // ESP-IDF requires an infinite wait when acquiring exclusive SPI bus access.
    const esp_err_t error = spi_device_acquire_bus(
        spi_device,
        portMAX_DELAY);
    if (error != ESP_OK) {
        set_write_error(error, "spi_device_acquire_bus");
        return false;
    }
    gpio_set_level(pin_cs, 0);
    writing = true;
    return true;
}

bool end_write()
{
    if (writing) {
        gpio_set_level(pin_cs, 1);
        spi_device_release_bus(spi_device);
        writing = false;
    }
    return write_ok;
}

bool wait_ready(std::uint32_t timeout_ms)
{
    vTaskDelay(pdMS_TO_TICKS(1U));
    const std::uint32_t start_ms = monotonic_ms();
    while (gpio_get_level(pin_busy) != 0) {
        if (monotonic_ms() - start_ms >= timeout_ms) {
            ESP_LOGE(
                log_tag,
                "BUSY timeout after %lu ms",
                static_cast<unsigned long>(timeout_ms));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    return true;
}

bool hardware_reset()
{
    internal_i2c_guard bus_guard(PAPER_MONO_EPD_IOE_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        ESP_LOGE(log_tag, "EPD reset skipped; internal I2C bus busy");
        return false;
    }

    auto& ioe = M5.getIOExpander(0);
    const bool enable_ready =
        ioe.setHighImpedance(PAPER_MONO_EPD_IOE_ENABLE, false) &&
        ioe.setDirection(PAPER_MONO_EPD_IOE_ENABLE, true) &&
        ioe.digitalWrite(PAPER_MONO_EPD_IOE_ENABLE, true);
    const bool reset_ready =
        ioe.setHighImpedance(PAPER_MONO_EPD_IOE_RESET, false) &&
        ioe.setDirection(PAPER_MONO_EPD_IOE_RESET, true) &&
        ioe.digitalWrite(PAPER_MONO_EPD_IOE_RESET, false);
    if (!enable_ready || !reset_ready) {
        ESP_LOGE(log_tag, "configure M5IOE1 EPD pins failed");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(PAPER_MONO_EPD_RESET_LOW_MS));
    if (!ioe.digitalWrite(PAPER_MONO_EPD_IOE_RESET, true)) {
        ESP_LOGE(log_tag, "release EPD reset failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(PAPER_MONO_EPD_RESET_HIGH_MS));
    return true;
}

void write_command(std::uint8_t command)
{
    transmit(&command, 1U, false, false);
}

void write_data(
    const std::uint8_t* data,
    std::size_t length,
    bool invert)
{
    transmit(data, length, true, invert);
}

void write_register(
    std::uint8_t command,
    std::initializer_list<std::uint8_t> data)
{
    write_command(command);
    write_data(data.begin(), data.size());
}

void write_u16(std::uint16_t value)
{
    const std::uint8_t bytes[] = {
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
    };
    write_data(bytes, sizeof(bytes));
}

}  // namespace paper_mono::epd_otp_transport
