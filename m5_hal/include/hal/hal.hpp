#pragma once

#include <esp_err.h>

// Initializes the active hardware backend without starting application services.
esp_err_t hal_init();
