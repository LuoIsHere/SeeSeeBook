#include "battery.hpp"

#include <M5Unified.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <utility/M5IOE1_Class.hpp>

#include "internal_i2c.hpp"

namespace {

constexpr char log_tag[] = "hal_battery";
constexpr std::uint8_t ip2315_i2c_address = 0x75U;
constexpr std::uint8_t ip2315_charge_status_register = 0xC7U;
constexpr std::uint8_t ip2315_charging_mask = 1U << 7U;
constexpr std::uint32_t ip2315_i2c_frequency_hz = 100000U;
constexpr std::uint8_t m5pm1_power_source_register = 0x04U;
constexpr std::uint32_t m5pm1_i2c_frequency_hz = 100000U;
constexpr std::uint32_t ip2315_ready_delay_ms = 2U;
constexpr std::uint8_t ip2315_ready_attempts = 8U;

class ip2315_session final {
public:
    ~ip2315_session()
    {
        if (disconnect_pending_ && !disconnect()) {
            ESP_LOGE(log_tag, "IP2315 fallback detach failed");
        }
    }

    bool connect()
    {
        disconnect_pending_ = true;
        return M5.getIOExpander(0).digitalWrite(m5::M5IOE1_Class::gpio11, true);
    }

    bool disconnect()
    {
        if (!disconnect_pending_) {
            return true;
        }
        const bool disconnected =
            M5.getIOExpander(0).digitalWrite(m5::M5IOE1_Class::gpio11, false);
        disconnect_pending_ = !disconnected;
        return disconnected;
    }

private:
    bool disconnect_pending_ = false;
};

}  // namespace

bool hal_battery_read_telemetry(battery_telemetry_sample& sample)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_BATTERY_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        return false;
    }

    sample = {};
    const std::int32_t level = M5.Power.getBatteryLevel();
    sample.level_valid = level >= 0 && level <= 100;
    if (sample.level_valid) {
        sample.percent = static_cast<std::uint8_t>(level);
    }

    const std::int16_t voltage_mv = M5.Power.getBatteryVoltage();
    sample.voltage_valid = voltage_mv > 0;
    if (sample.voltage_valid) {
        sample.voltage_mv = static_cast<std::uint16_t>(voltage_mv);
    }

    // PaperMono exposes no measured battery-current path.
    sample.current_valid = false;
    return true;
}

bool hal_battery_read_vbus(bool& present)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_BATTERY_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        return false;
    }

    std::uint8_t power_sources = 0U;
    if (!M5.In_I2C.readRegister(
            m5::M5PM1_Class::DEFAULT_ADDRESS,
            m5pm1_power_source_register,
            &power_sources,
            sizeof(power_sources),
            m5pm1_i2c_frequency_hz)) {
        return false;
    }
    present =
        (power_sources & (m5::M5PM1_Class::vin | m5::M5PM1_Class::vinout)) != 0U;
    return true;
}

bool hal_battery_read_charging(bool& charging)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_BATTERY_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        return false;
    }

    const std::int64_t start_us = esp_timer_get_time();
    ip2315_session session;
    const bool attached = session.connect();
    bool ready = false;
    bool read_succeeded = false;
    std::uint8_t charge_status = 0U;

    if (attached) {
        vTaskDelay(pdMS_TO_TICKS(ip2315_ready_delay_ms));
        for (std::uint8_t attempt = 0U; attempt < ip2315_ready_attempts; ++attempt) {
            if (M5.In_I2C.scanID(ip2315_i2c_address, ip2315_i2c_frequency_hz)) {
                ready = true;
                break;
            }
        }
        if (ready) {
            read_succeeded = M5.In_I2C.readRegister(
                ip2315_i2c_address,
                ip2315_charge_status_register,
                &charge_status,
                sizeof(charge_status),
                ip2315_i2c_frequency_hz);
        }
    }

    const bool detached = session.disconnect();
    const bool valid = attached && ready && read_succeeded && detached;
    if (valid) {
        charging = (charge_status & ip2315_charging_mask) != 0U;
    }
    ESP_LOGI(
        log_tag,
        "IP2315 attached=%u ready=%u read=%u detached=%u duration_ms=%lld",
        attached ? 1U : 0U,
        ready ? 1U : 0U,
        read_succeeded ? 1U : 0U,
        detached ? 1U : 0U,
        static_cast<long long>((esp_timer_get_time() - start_us) / 1000));
    return valid;
}
