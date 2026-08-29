#pragma once

#include <cstdint>
#include <type_traits>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define BATTERY_EVENT_QUEUE_LENGTH 1U
#define BATTERY_TASK_STACK_SIZE 4096U
#define BATTERY_TASK_PRIORITY 4U
#define BATTERY_BACKGROUND_SAMPLE_INTERVAL_MS 30000U
#define BATTERY_APP_SAMPLE_INTERVAL_MS 5000U
#define BATTERY_VBUS_POLL_INTERVAL_MS 1000U
#define BATTERY_IP2315_READY_DELAY_MS 2U
#define BATTERY_IP2315_READY_ATTEMPTS 8U
#define BATTERY_VOLTAGE_CHANGE_THRESHOLD_MV 20U
#define BATTERY_CURRENT_CHANGE_THRESHOLD_MA 20

struct battery_snapshot {
    std::uint8_t percent;
    std::uint16_t voltage_mv;
    std::int32_t current_ma;
    bool charging;
    bool level_valid;
    bool voltage_valid;
    bool current_valid;
    bool charging_valid;
    std::uint32_t timestamp_ms;
};

struct battery_event {
    battery_snapshot snapshot;
};

static_assert(std::is_trivially_copyable_v<battery_snapshot>);
static_assert(std::is_trivially_copyable_v<battery_event>);

// Starts the battery worker and performs the first M5PM1 sample immediately.
bool hal_battery_start(TaskHandle_t& task_handle);

// Selects the 5 second foreground cadence or the 30 second background cadence.
void hal_set_battery_app_sampling(bool enabled);

// Reads the latest battery event without blocking the Mooncake scheduler.
bool hal_try_get_battery_event(battery_event& event);

// Copies the most recent sample, including validity flags for unsupported values.
bool hal_get_cached_battery_snapshot(battery_snapshot& snapshot);
