#pragma once

#include <esp_err.h>

#include "battery_snapshot.hpp"
#include "service_event.hpp"

#define BATTERY_BACKGROUND_SAMPLE_INTERVAL_MS 30000U
#define BATTERY_DETAIL_SAMPLE_INTERVAL_MS 5000U
#define BATTERY_VBUS_POLL_INTERVAL_MS 1000U
#define BATTERY_VOLTAGE_CHANGE_THRESHOLD_MV 20U
#define BATTERY_CURRENT_CHANGE_THRESHOLD_MA 20

esp_err_t battery_service_init();
bool battery_service_try_get_event(battery_service_event& event);
bool battery_service_get_snapshot(battery_snapshot& snapshot);
void battery_service_acquire_detail_sampling();
void battery_service_release_detail_sampling();
