#pragma once

#include <cstdint>
#include <type_traits>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#define TOUCH_DEBOUNCE_TIME_MS 20U
#define TOUCH_LONG_PRESS_TIME_MS 800U
#define LONG_PRESS_REFRESH_INTERVAL_MS 250U

#define PAPER_MONO_TOUCH_DEVICE_WIDTH 480U
#define PAPER_MONO_TOUCH_DEVICE_HEIGHT 800U

#define TOUCH_EVENT_QUEUE_LENGTH 16U
#define TOUCH_TASK_STACK_SIZE 4096U
#define TOUCH_TASK_PRIORITY 6U

enum class touch_event_type : std::uint8_t {
    press,
    click,
    long_press_start,
    long_press_repeat,
    long_press_end,
};

struct touch_event {
    touch_event_type type;
    std::int16_t start_x;
    std::int16_t start_y;
    std::int16_t end_x;
    std::int16_t end_y;
    std::uint32_t duration_ms;
    std::uint32_t timestamp_ms;
};

static_assert(std::is_trivially_copyable_v<touch_event>);

// Starts the touch worker that samples M5Unified when notified by the GPTimer.
bool hal_touch_start(QueueHandle_t event_queue, TaskHandle_t& task_handle);

// Reads one touch event without blocking the caller.
bool hal_try_get_touch_event(touch_event& event);
