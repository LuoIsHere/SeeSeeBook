#include "internal_i2c.hpp"

#include <M5Unified.h>

#include <atomic>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <utility/M5IOE1_Class.hpp>

namespace {

constexpr char log_tag[] = "hal_i2c";
constexpr std::uint8_t m5ioe1_uid_register = 0x00U;
constexpr std::uint8_t m5pm1_uid_register = 0x00U;
constexpr std::uint32_t internal_i2c_frequency_hz = 100000U;

SemaphoreHandle_t internal_i2c_mutex = nullptr;
std::atomic<internal_i2c_state> bus_state{internal_i2c_state::unavailable};

bool validate_device(std::uint8_t address, std::uint8_t uid_register)
{
    std::uint8_t uid[2] = {};
    return M5.In_I2C.readRegister(
        address,
        uid_register,
        uid,
        sizeof(uid),
        internal_i2c_frequency_hz);
}

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

    bus_state.store(internal_i2c_state::healthy);
    ESP_LOGI(log_tag, "internal I2C bus service initialized");
    return true;
}

bool hal_internal_i2c_lock(std::uint32_t timeout_ms)
{
    if (internal_i2c_mutex == nullptr ||
        bus_state.load() != internal_i2c_state::healthy) {
        return false;
    }

    if (xSemaphoreTake(internal_i2c_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;
    }
    if (bus_state.load() != internal_i2c_state::healthy) {
        xSemaphoreGive(internal_i2c_mutex);
        return false;
    }
    return true;
}

void hal_internal_i2c_unlock()
{
    if (internal_i2c_mutex != nullptr) {
        xSemaphoreGive(internal_i2c_mutex);
    }
}

void hal_internal_i2c_mark_fault()
{
    const internal_i2c_state previous = bus_state.exchange(
        internal_i2c_state::faulted);
    if (previous != internal_i2c_state::faulted) {
        ESP_LOGE(
            log_tag,
            "internal I2C bus marked faulted previous=%u",
            static_cast<unsigned>(previous));
    }
}

internal_i2c_state hal_internal_i2c_get_state()
{
    return bus_state.load();
}

bool hal_internal_i2c_recover()
{
    if (internal_i2c_mutex == nullptr) {
        return false;
    }
    const std::int64_t start_us = esp_timer_get_time();
    if (xSemaphoreTake(
            internal_i2c_mutex,
            pdMS_TO_TICKS(INTERNAL_I2C_RECOVERY_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(log_tag, "recovery skipped; bus mutex timeout");
        return false;
    }

    const internal_i2c_state previous = bus_state.exchange(
        internal_i2c_state::recovering);
    bool released = true;
    bool initialized = true;
    if (previous == internal_i2c_state::faulted ||
        previous == internal_i2c_state::unavailable) {
        released = M5.In_I2C.release();
        vTaskDelay(pdMS_TO_TICKS(1U));
        initialized = M5.In_I2C.begin();
    }

    // GPIO11 must be low before any other client resumes using the shared bus.
    const bool ip2315_detached = initialized &&
                                 M5.getIOExpander(0).digitalWrite(
                                     m5::M5IOE1_Class::gpio11,
                                     false);
    const bool m5ioe1_ready = ip2315_detached && validate_device(
        m5::M5IOE1_Class::DEFAULT_ADDRESS,
        m5ioe1_uid_register);
    const bool m5pm1_ready = m5ioe1_ready && validate_device(
        m5::M5PM1_Class::DEFAULT_ADDRESS,
        m5pm1_uid_register);
    const bool recovered = initialized && ip2315_detached &&
                           m5ioe1_ready && m5pm1_ready;
    bus_state.store(
        recovered ? internal_i2c_state::healthy
                  : internal_i2c_state::faulted);
    xSemaphoreGive(internal_i2c_mutex);

    if (recovered) {
        ESP_LOGI(
            log_tag,
            "recovery previous=%u released=%u initialized=%u ip2315_detached=%u m5ioe1=%u m5pm1=%u success=1 duration_ms=%lld",
            static_cast<unsigned>(previous),
            released ? 1U : 0U,
            initialized ? 1U : 0U,
            ip2315_detached ? 1U : 0U,
            m5ioe1_ready ? 1U : 0U,
            m5pm1_ready ? 1U : 0U,
            static_cast<long long>((esp_timer_get_time() - start_us) / 1000));
    } else {
        ESP_LOGE(
            log_tag,
            "recovery previous=%u released=%u initialized=%u ip2315_detached=%u m5ioe1=%u m5pm1=%u success=0 duration_ms=%lld",
            static_cast<unsigned>(previous),
            released ? 1U : 0U,
            initialized ? 1U : 0U,
            ip2315_detached ? 1U : 0U,
            m5ioe1_ready ? 1U : 0U,
            m5pm1_ready ? 1U : 0U,
            static_cast<long long>((esp_timer_get_time() - start_us) / 1000));
    }
    return recovered;
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
