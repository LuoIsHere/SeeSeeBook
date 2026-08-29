#include "hal_battery.hpp"

#include <atomic>

#include <M5Unified.h>
#include <esp_log.h>
#include <freertos/queue.h>

#include "hal.hpp"

namespace {

constexpr char log_tag[] = "hal_battery";

QueueHandle_t event_queue = nullptr;
SemaphoreHandle_t internal_i2c_mutex = nullptr;
TaskHandle_t battery_task_handle = nullptr;
portMUX_TYPE cache_mutex = portMUX_INITIALIZER_UNLOCKED;
battery_snapshot cached_snapshot = {};
bool cache_has_sample = false;
std::atomic_bool app_sampling_enabled = false;

battery_snapshot read_battery_snapshot()
{
    battery_snapshot snapshot = {};
    snapshot.timestamp_ms = hal_get_tick_ms();

    const std::int32_t level = M5.Power.getBatteryLevel();
    if (level >= 0 && level <= 100) {
        snapshot.percent = static_cast<std::uint8_t>(level);
        snapshot.level_valid = true;
    }

    const std::int16_t voltage_mv = M5.Power.getBatteryVoltage();
    if (voltage_mv > 0) {
        snapshot.voltage_mv = static_cast<std::uint16_t>(voltage_mv);
        snapshot.voltage_valid = true;
    }

    const m5::Power_Class::is_charging_t charging_state = M5.Power.isCharging();
    snapshot.charging_valid = charging_state != m5::Power_Class::charge_unknown;
    snapshot.charging = charging_state == m5::Power_Class::is_charging;

    // PaperMono exposes no measured battery-current path; zero would be ambiguous.
    snapshot.current_valid = false;
    return snapshot;
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
    bool should_sample = true;
    for (;;) {
        if (should_sample) {
            xSemaphoreTake(internal_i2c_mutex, portMAX_DELAY);
            const battery_snapshot snapshot = read_battery_snapshot();
            xSemaphoreGive(internal_i2c_mutex);
            publish_snapshot(snapshot);

            ESP_LOGD(
                log_tag,
                "sample level=%u valid=%u voltage_mv=%u charging=%u charging_valid=%u current_valid=%u",
                static_cast<unsigned>(snapshot.percent),
                snapshot.level_valid ? 1U : 0U,
                static_cast<unsigned>(snapshot.voltage_mv),
                snapshot.charging ? 1U : 0U,
                snapshot.charging_valid ? 1U : 0U,
                snapshot.current_valid ? 1U : 0U);
        }

        const std::uint32_t interval_ms = app_sampling_enabled.load()
                                              ? BATTERY_APP_SAMPLE_INTERVAL_MS
                                              : BATTERY_BACKGROUND_SAMPLE_INTERVAL_MS;
        const std::uint32_t notification_count =
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval_ms));
        // Entering the App samples immediately; leaving it only rebases to 30 seconds.
        should_sample = notification_count == 0U || app_sampling_enabled.load();
    }
}

}  // namespace

bool hal_battery_start(SemaphoreHandle_t i2c_mutex, TaskHandle_t& task_handle)
{
    event_queue = xQueueCreate(BATTERY_EVENT_QUEUE_LENGTH, sizeof(battery_event));
    internal_i2c_mutex = i2c_mutex;
    if (event_queue == nullptr || internal_i2c_mutex == nullptr) {
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
            "battery task started background_ms=%u app_ms=%u",
            static_cast<unsigned>(BATTERY_BACKGROUND_SAMPLE_INTERVAL_MS),
            static_cast<unsigned>(BATTERY_APP_SAMPLE_INTERVAL_MS));
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
