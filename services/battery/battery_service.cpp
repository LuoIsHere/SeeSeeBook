#include "battery_service.hpp"

#include "service_event_source.hpp"

#include <atomic>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "battery.hpp"
#include "system_tick_service.hpp"

namespace {

constexpr char log_tag[] = "service_battery";
constexpr std::uint32_t task_stack_size = 4096U;
constexpr UBaseType_t task_priority = 4U;

static_assert(BATTERY_VBUS_CHANGE_DEBOUNCE_SAMPLES >= 2U);

QueueHandle_t event_queue = nullptr;
TaskHandle_t battery_task_handle = nullptr;
portMUX_TYPE cache_mutex = portMUX_INITIALIZER_UNLOCKED;
battery_snapshot cached_snapshot = {};
bool cache_has_sample = false;
std::atomic_uint32_t detail_subscribers{0U};

struct vbus_monitor_state {
    bool initialized;
    bool stable_present;
    bool candidate_present;
    std::uint8_t candidate_count;
};

void publish_snapshot(const battery_snapshot& snapshot)
{
    portENTER_CRITICAL(&cache_mutex);
    cached_snapshot = snapshot;
    cache_has_sample = true;
    portEXIT_CRITICAL(&cache_mutex);

    battery_service_event event = {};
    event.snapshot = snapshot;
    xQueueOverwrite(event_queue, &event);
}

bool update_vbus_state(
    battery_snapshot& snapshot,
    vbus_monitor_state& monitor,
    bool& changed)
{
    bool present = false;
    if (!hal_battery_read_vbus(present)) {
        ESP_LOGW(log_tag, "M5PM1 VBUS read failed");
        return false;
    }
    changed = false;
    if (!monitor.initialized) {
        monitor.initialized = true;
        monitor.stable_present = present;
        monitor.candidate_present = present;
        monitor.candidate_count = 0U;
        snapshot.charging = false;
        snapshot.charging_valid = !present;
        ESP_LOGI(
            log_tag,
            "VBUS baseline present=%u charging_read_deferred=1 valid=%u",
            present ? 1U : 0U,
            snapshot.charging_valid ? 1U : 0U);
        return true;
    }
    if (present == monitor.stable_present) {
        monitor.candidate_present = present;
        monitor.candidate_count = 0U;
        return true;
    }
    if (present != monitor.candidate_present) {
        monitor.candidate_present = present;
        monitor.candidate_count = 1U;
        return true;
    }
    if (monitor.candidate_count < UINT8_MAX) {
        ++monitor.candidate_count;
    }
    if (monitor.candidate_count < BATTERY_VBUS_CHANGE_DEBOUNCE_SAMPLES) {
        return true;
    }

    monitor.stable_present = present;
    monitor.candidate_count = 0U;
    changed = true;
    if (!present) {
        snapshot.charging = false;
        snapshot.charging_valid = true;
    } else {
        bool charging = false;
        snapshot.charging_valid = hal_battery_read_charging(charging);
        snapshot.charging = snapshot.charging_valid && charging;
    }
    ESP_LOGI(
        log_tag,
        "VBUS changed present=%u charging=%u valid=%u",
        present ? 1U : 0U,
        snapshot.charging ? 1U : 0U,
        snapshot.charging_valid ? 1U : 0U);
    return true;
}

bool update_telemetry(battery_snapshot& snapshot)
{
    battery_telemetry_sample sample = {};
    if (!hal_battery_read_telemetry(sample)) {
        return false;
    }
    snapshot.percent = sample.percent;
    snapshot.voltage_mv = sample.voltage_mv;
    snapshot.current_ma = sample.current_ma;
    snapshot.level_valid = sample.level_valid;
    snapshot.voltage_valid = sample.voltage_valid;
    snapshot.current_valid = sample.current_valid;
    return true;
}

void battery_task(void*)
{
    battery_snapshot snapshot = {};
    vbus_monitor_state vbus_monitor = {};
    bool sample_initialized = false;
    bool force_sample = true;
    std::uint32_t last_sample_ms = 0U;

    for (;;) {
        const std::uint32_t now_ms = system_tick_now_ms();
        const std::uint32_t interval_ms = detail_subscribers.load() > 0U
                                                ? BATTERY_DETAIL_SAMPLE_INTERVAL_MS
                                                : BATTERY_BACKGROUND_SAMPLE_INTERVAL_MS;
        const bool sample_due =
            force_sample || !sample_initialized || now_ms - last_sample_ms >= interval_ms;
        bool vbus_changed = false;
        const bool vbus_valid = update_vbus_state(
            snapshot,
            vbus_monitor,
            vbus_changed);
        const bool telemetry_valid = !sample_due || update_telemetry(snapshot);
        if (vbus_valid && telemetry_valid && (sample_due || vbus_changed)) {
            snapshot.timestamp_ms = now_ms;
            publish_snapshot(snapshot);
            if (sample_due) {
                sample_initialized = true;
                last_sample_ms = now_ms;
            }
        }
        force_sample = false;

        const std::uint32_t notification_count = ulTaskNotifyTake(
            pdTRUE,
            pdMS_TO_TICKS(BATTERY_VBUS_POLL_INTERVAL_MS));
        if (notification_count != 0U) {
            if (detail_subscribers.load() > 0U) {
                force_sample = true;
            } else if (sample_initialized) {
                last_sample_ms = system_tick_now_ms();
            }
        }
    }
}

}  // namespace

esp_err_t battery_service_init()
{
    event_queue = xQueueCreate(1U, sizeof(battery_service_event));
    if (event_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            battery_task,
            "battery_service",
            task_stack_size,
            nullptr,
            task_priority,
            &battery_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(
        log_tag,
        "started background_ms=%u detail_ms=%u",
        BATTERY_BACKGROUND_SAMPLE_INTERVAL_MS,
        BATTERY_DETAIL_SAMPLE_INTERVAL_MS);
    return ESP_OK;
}

bool battery_service_try_get_event(battery_service_event& event)
{
    return event_queue != nullptr && xQueueReceive(event_queue, &event, 0) == pdTRUE;
}

bool battery_service_get_snapshot(battery_snapshot& snapshot)
{
    portENTER_CRITICAL(&cache_mutex);
    snapshot = cached_snapshot;
    const bool has_sample = cache_has_sample;
    portEXIT_CRITICAL(&cache_mutex);
    return has_sample;
}

void battery_service_acquire_detail_sampling()
{
    detail_subscribers.fetch_add(1U);
    if (battery_task_handle != nullptr) {
        xTaskNotifyGive(battery_task_handle);
    }
}

void battery_service_release_detail_sampling()
{
    std::uint32_t current = detail_subscribers.load();
    while (current > 0U &&
           !detail_subscribers.compare_exchange_weak(current, current - 1U)) {
    }
    if (battery_task_handle != nullptr) {
        xTaskNotifyGive(battery_task_handle);
    }
}
