#pragma once

#include <cstdint>

#include "esp_err.h"

#include "hal_display.hpp"
#include "hal_rtc.hpp"
#include "hal_touch.hpp"

// Initializes M5Unified, queues, worker tasks, and the 1 ms GPTimer.
esp_err_t hal_init();

// Updates M5Unified while serializing access to the shared internal I2C bus.
void hal_update_m5();

// Sets the PaperMono front light from the display worker task.
void hal_set_front_light(std::uint8_t brightness);

// Returns the monotonic millisecond time maintained by the GPTimer ISR.
std::uint32_t hal_get_tick_ms();
