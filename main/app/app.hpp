#pragma once

#include <cstdint>

#include "esp_err.h"

enum class app_kind : std::uint8_t {
    menu,
    test,
};

// Installs all Mooncake apps and selects MenuApp as the initial foreground app.
esp_err_t app_init();

// Applies a pending app switch and advances every Mooncake lifecycle once.
void app_update();

// Schedules a foreground app change without blocking the caller.
void app_request_switch(app_kind target);

// Reports whether event dispatch should stop until a requested switch is applied.
bool app_switch_pending();
