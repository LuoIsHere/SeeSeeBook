#pragma once

#include <cstdint>

#include "esp_err.h"

enum class app_kind : std::uint8_t {
    menu,
    test,
    rtc_setting,
    battery,
};

// Installs all Mooncake apps and selects MenuApp as the initial foreground app.
esp_err_t app_init();

// Applies a pending app switch and advances every Mooncake lifecycle once.
void app_update();

// Schedules a foreground app change without blocking the caller.
void app_request_switch(app_kind target);

// Returns to the app that opened the current transient settings page.
void app_request_back();

// Reports whether event dispatch should stop until a requested switch is applied.
bool app_switch_pending();
