#pragma once

#include <cstdint>

#include "esp_err.h"

#include "types.hpp"

// Initializes M5Unified, queues, worker tasks, and the 1 ms GPTimer.
esp_err_t hal_init();

// Waits until the touch task publishes the next complete touch event.
bool hal_wait_touch_event(touch_event& event);

// Replaces any pending frame with the most recent application state.
bool hal_submit_display_request(const display_request& request);

// Queues a front-light button transition without overwriting earlier transitions.
bool hal_submit_front_light_request(const front_light_request& request);

// Updates M5Unified while serializing access to the shared internal I2C bus.
void hal_update_m5();

// Sets the PaperMono front light while serializing the shared internal I2C bus.
void hal_set_front_light(std::uint8_t brightness);

// Returns the monotonic millisecond time maintained by the GPTimer ISR.
std::uint32_t hal_get_tick_ms();
