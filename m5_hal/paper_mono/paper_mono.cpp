#include "hal.hpp"

#include <M5Unified.h>
#include <esp_check.h>
#include <esp_log.h>

#include "display.hpp"
#include "internal_i2c.hpp"
#include "paper_mono_config.hpp"
#include "storage.hpp"

namespace {

constexpr char log_tag[] = "hal_paper_mono";

}  // namespace

esp_err_t hal_init()
{
    ESP_RETURN_ON_FALSE(
        hal_internal_i2c_init(),
        ESP_ERR_NO_MEM,
        log_tag,
        "initialize internal I2C bus");

    auto m5_config = M5.config();
    m5_config.clear_display = false;
    m5_config.internal_imu = false;
    m5_config.internal_rtc = true;
    m5_config.internal_mic = false;
    m5_config.internal_spk = false;
    m5_config.fallback_board = m5::board_t::board_M5PaperMono;
    M5.begin(m5_config);

    ESP_RETURN_ON_FALSE(
        M5.getBoard() == m5::board_t::board_M5PaperMono,
        ESP_ERR_NOT_SUPPORTED,
        log_tag,
        "unsupported board for PaperMono HAL");
    ESP_RETURN_ON_FALSE(
        M5.Display.isEPD(),
        ESP_ERR_NOT_FOUND,
        log_tag,
        "PaperMono EPD not found");
    ESP_RETURN_ON_FALSE(
        M5.Touch.isEnabled(),
        ESP_ERR_NOT_FOUND,
        log_tag,
        "PaperMono touch not found");
    ESP_RETURN_ON_FALSE(
        hal_display_init(),
        ESP_FAIL,
        log_tag,
        "initialize display");
    ESP_RETURN_ON_FALSE(
        hal_storage_init(),
        ESP_FAIL,
        log_tag,
        "initialize SD hardware");

    ESP_LOGI(
        log_tag,
        "board=%d display=%dx%d",
        static_cast<int>(M5.getBoard()),
        PAPER_MONO_DISPLAY_WIDTH,
        PAPER_MONO_DISPLAY_HEIGHT);
    return ESP_OK;
}
