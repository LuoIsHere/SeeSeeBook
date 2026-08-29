#pragma once

#include <cstdint>

// The GPTimer keeps a 1 ms time base while the touch task is sampled every 10 ms.
#define SYSTEM_TICK_PERIOD_MS 1U
#define TOUCH_SCAN_PERIOD_MS 10U
#define MAIN_LOOP_DELAY_MS 1U
#define INPUT_EVENTS_PER_UPDATE 8U

static_assert(TOUCH_SCAN_PERIOD_MS % SYSTEM_TICK_PERIOD_MS == 0U);
static_assert(INPUT_EVENTS_PER_UPDATE > 0U);
