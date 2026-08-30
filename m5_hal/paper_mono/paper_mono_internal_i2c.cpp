#include "internal_i2c.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr char log_tag[] = "hal_i2c";

SemaphoreHandle_t internal_i2c_mutex = nullptr;

}  // namespace

bool hal_internal_i2c_init()
{
    if (internal_i2c_mutex != nullptr) {
        return true;
    }

    internal_i2c_mutex = xSemaphoreCreateMutex();
    if (internal_i2c_mutex == nullptr) {
        ESP_LOGE(log_tag, "failed to create internal I2C mutex");
        return false;
    }

    ESP_LOGI(log_tag, "internal I2C bus service initialized");
    return true;
}

bool hal_internal_i2c_lock(std::uint32_t timeout_ms)
{
    if (internal_i2c_mutex == nullptr) {
        return false;
    }

    return xSemaphoreTake(internal_i2c_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void hal_internal_i2c_unlock()
{
    if (internal_i2c_mutex != nullptr) {
        xSemaphoreGive(internal_i2c_mutex);
    }
}

internal_i2c_guard::internal_i2c_guard(std::uint32_t timeout_ms)
    : locked_(hal_internal_i2c_lock(timeout_ms))
{
}

internal_i2c_guard::~internal_i2c_guard()
{
    if (locked_) {
        hal_internal_i2c_unlock();
    }
}

bool internal_i2c_guard::locked() const
{
    return locked_;
}
