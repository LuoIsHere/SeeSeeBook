#include "hal_battery.hpp"

#include <atomic>

#include <M5Unified.h>
#include <esp_log.h>
#include <freertos/queue.h>
#include <utility/M5IOE1_Class.hpp>

#include "hal.hpp"
#include "hal_internal_i2c.hpp"

namespace {

constexpr char log_tag[] = "hal_battery";
constexpr std::uint8_t ip2315_i2c_address = 0x75U;
constexpr std::uint8_t ip2315_charge_status_register = 0xC7U;
constexpr std::uint8_t ip2315_charging_mask = 1U << 7U;
constexpr std::uint32_t ip2315_i2c_frequency_hz = 100000U;
constexpr std::uint8_t m5pm1_power_source_register = 0x04U;
constexpr std::uint32_t m5pm1_i2c_frequency_hz = 100000U;

QueueHandle_t event_queue = nullptr;
TaskHandle_t battery_task_handle = nullptr;
portMUX_TYPE cache_mutex = portMUX_INITIALIZER_UNLOCKED;
battery_snapshot cached_snapshot = {};
bool cache_has_sample = false;
std::atomic_bool app_sampling_enabled = false;

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

bool read_ip2315_charging(bool& charging)
{
    const std::uint32_t start_ms = hal_get_tick_ms();
    ip2315_session session;
    const bool attached = session.connect();
    bool ready = false;
    bool read_succeeded = false;
    std::uint8_t charge_status = 0U;

    if (attached) {
        vTaskDelay(pdMS_TO_TICKS(BATTERY_IP2315_READY_DELAY_MS));
        for (std::uint8_t attempt = 0U;
             attempt < BATTERY_IP2315_READY_ATTEMPTS;
             ++attempt) {
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
        "IP2315 session attached=%u ready=%u read=%u detached=%u valid=%u duration_ms=%lu",
        attached ? 1U : 0U,
        ready ? 1U : 0U,
        read_succeeded ? 1U : 0U,
        detached ? 1U : 0U,
        valid ? 1U : 0U,
        static_cast<unsigned long>(hal_get_tick_ms() - start_ms));
    return valid;
}

void update_battery_telemetry(battery_snapshot& snapshot)
{
    const std::int32_t level = M5.Power.getBatteryLevel();
    snapshot.level_valid = level >= 0 && level <= 100;
    if (snapshot.level_valid) {
        snapshot.percent = static_cast<std::uint8_t>(level);
    }

    const std::int16_t voltage_mv = M5.Power.getBatteryVoltage();
    snapshot.voltage_valid = voltage_mv > 0;
    if (snapshot.voltage_valid) {
        snapshot.voltage_mv = static_cast<std::uint16_t>(voltage_mv);
    }

    // PaperMono exposes no measured battery-current path; zero would be ambiguous.
    snapshot.current_valid = false;
}

bool read_vbus_present(bool& vbus_present)
{
    std::uint8_t power_sources = 0U;
    const bool read_succeeded = M5.In_I2C.readRegister(
        m5::M5PM1_Class::DEFAULT_ADDRESS,
        m5pm1_power_source_register,
        &power_sources,
        sizeof(power_sources),
        m5pm1_i2c_frequency_hz);
    if (!read_succeeded) {
        return false;
    }

    vbus_present =
        (power_sources & (m5::M5PM1_Class::vin | m5::M5PM1_Class::vinout)) != 0U;
    return true;
}

bool update_vbus_state(
    battery_snapshot& snapshot,
    bool& vbus_initialized,
    bool& previous_vbus_present,
    bool& vbus_changed)
{
    bool vbus_present = false;
    if (!read_vbus_present(vbus_present)) {
        ESP_LOGW(log_tag, "M5PM1 VBUS read failed");
        return false;
    }
    vbus_changed = false;

    if (!vbus_initialized) {
        vbus_initialized = true;
        previous_vbus_present = vbus_present;
        snapshot.charging = false;
        snapshot.charging_valid = !vbus_present;
        ESP_LOGI(
            log_tag,
            "VBUS baseline present=%u; IP2315 query deferred until transition",
            vbus_present ? 1U : 0U);
        return true;
    }

    if (vbus_present == previous_vbus_present) {
        return true;
    }

    previous_vbus_present = vbus_present;
    vbus_changed = true;
    if (!vbus_present) {
        snapshot.charging = false;
        snapshot.charging_valid = true;
    } else {
        bool charging = false;
        snapshot.charging_valid = read_ip2315_charging(charging);
        snapshot.charging = snapshot.charging_valid && charging;
    }

    ESP_LOGI(
        log_tag,
        "VBUS transition present=%u charging=%u charging_valid=%u",
        vbus_present ? 1U : 0U,
        snapshot.charging ? 1U : 0U,
        snapshot.charging_valid ? 1U : 0U);
    return true;
}

bool collect_battery_state(
    battery_snapshot& snapshot,
    bool full_sample,
    bool& vbus_initialized,
    bool& previous_vbus_present,
    bool& vbus_changed)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_BATTERY_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        ESP_LOGW(log_tag, "sample skipped; internal I2C bus busy");
        return false;
    }

    if (!update_vbus_state(
            snapshot,
            vbus_initialized,
            previous_vbus_present,
            vbus_changed)) {
        return false;
    }
    if (full_sample) {
        update_battery_telemetry(snapshot);
    }
    snapshot.timestamp_ms = hal_get_tick_ms();
    return true;
}

void publish_snapshot(const battery_snapshot& snapshot)
{
    portENTER_CRITICAL(&cache_mutex);
    cached_snapshot = snapshot;
    cache_has_sample = true;
    portEXIT_CRITICAL(&cache_mutex);

    battery_event event = {};
    event.snapshot = snapshot;
    xQueueOverwrite(event_queue, &event);
}

void battery_task(void*)
{
    battery_snapshot snapshot = {};
    bool vbus_initialized = false;
    bool previous_vbus_present = false;
    bool full_sample_initialized = false;
    bool force_full_sample = true;
    std::uint32_t last_full_sample_ms = 0U;

    for (;;) {
        const std::uint32_t now_ms = hal_get_tick_ms();
        const std::uint32_t sample_interval_ms = app_sampling_enabled.load()
                                                     ? BATTERY_APP_SAMPLE_INTERVAL_MS
                                                     : BATTERY_BACKGROUND_SAMPLE_INTERVAL_MS;
        const bool full_sample_due =
            force_full_sample || !full_sample_initialized ||
            now_ms - last_full_sample_ms >= sample_interval_ms;
        bool vbus_changed = false;
        const bool collected = collect_battery_state(
            snapshot,
            full_sample_due,
            vbus_initialized,
            previous_vbus_present,
            vbus_changed);

        if (collected && (full_sample_due || vbus_changed)) {
            publish_snapshot(snapshot);
            if (full_sample_due) {
                full_sample_initialized = true;
                last_full_sample_ms = now_ms;
            }
            ESP_LOGD(
                log_tag,
                "sample level=%u valid=%u voltage_mv=%u charging=%u charging_valid=%u",
                static_cast<unsigned>(snapshot.percent),
                snapshot.level_valid ? 1U : 0U,
                static_cast<unsigned>(snapshot.voltage_mv),
                snapshot.charging ? 1U : 0U,
                snapshot.charging_valid ? 1U : 0U);
        }
        force_full_sample = false;

        const std::uint32_t notification_count =
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BATTERY_VBUS_POLL_INTERVAL_MS));
        if (notification_count != 0U) {
            if (app_sampling_enabled.load()) {
                force_full_sample = true;
            } else if (full_sample_initialized) {
                // Leaving the App rebases the 30 second background interval.
                last_full_sample_ms = hal_get_tick_ms();
            }
        }
    }
}

}  // namespace

bool hal_battery_start(TaskHandle_t& task_handle)
{
    event_queue = xQueueCreate(BATTERY_EVENT_QUEUE_LENGTH, sizeof(battery_event));
    if (event_queue == nullptr) {
        return false;
    }

    const bool started = xTaskCreate(
                             battery_task,
                             "battery_task",
                             BATTERY_TASK_STACK_SIZE,
                             nullptr,
                             BATTERY_TASK_PRIORITY,
                             &task_handle) == pdPASS;
    if (started) {
        battery_task_handle = task_handle;
        ESP_LOGI(
            log_tag,
            "battery task started background_ms=%u app_ms=%u vbus_poll_ms=%u",
            static_cast<unsigned>(BATTERY_BACKGROUND_SAMPLE_INTERVAL_MS),
            static_cast<unsigned>(BATTERY_APP_SAMPLE_INTERVAL_MS),
            static_cast<unsigned>(BATTERY_VBUS_POLL_INTERVAL_MS));
    }
    return started;
}

void hal_set_battery_app_sampling(bool enabled)
{
    const bool was_enabled = app_sampling_enabled.exchange(enabled);
    if (enabled != was_enabled && battery_task_handle != nullptr) {
        // Wake the task to sample on entry or rebase the background interval on exit.
        xTaskNotifyGive(battery_task_handle);
    }
    ESP_LOGI(log_tag, "app sampling enabled=%u", enabled ? 1U : 0U);
}

bool hal_try_get_battery_event(battery_event& event)
{
    return event_queue != nullptr && xQueueReceive(event_queue, &event, 0) == pdTRUE;
}

bool hal_get_cached_battery_snapshot(battery_snapshot& snapshot)
{
    bool has_sample = false;
    portENTER_CRITICAL(&cache_mutex);
    snapshot = cached_snapshot;
    has_sample = cache_has_sample;
    portEXIT_CRITICAL(&cache_mutex);
    return has_sample;
}
