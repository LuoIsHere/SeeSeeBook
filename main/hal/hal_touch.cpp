#include "hal.hpp"

#include <M5Unified.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "system_config.hpp"

namespace {

constexpr char log_tag[] = "hal_touch";

static_assert(TOUCH_DEBOUNCE_TIME_MS % TOUCH_SCAN_PERIOD_MS == 0U);
static_assert(TOUCH_LONG_PRESS_TIME_MS % TOUCH_SCAN_PERIOD_MS == 0U);
static_assert(LONG_PRESS_REFRESH_INTERVAL_MS % TOUCH_SCAN_PERIOD_MS == 0U);

enum class touch_state : std::uint8_t {
    idle,
    debounce,
    pressed,
    long_pressed,
};

struct touch_context {
    touch_state state = touch_state::idle;
    std::int16_t start_x = 0;
    std::int16_t start_y = 0;
    std::int16_t current_x = 0;
    std::int16_t current_y = 0;
    std::uint32_t press_tick_ms = 0;
    std::uint32_t last_long_press_tick_ms = 0;
};

struct screen_point {
    std::int16_t x;
    std::int16_t y;
};

QueueHandle_t event_queue = nullptr;

std::int32_t clamp_coordinate(
    std::int32_t value,
    std::int32_t maximum)
{
    if (value < 0) {
        return 0;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

screen_point transform_touch_to_screen(
    std::int16_t touch_x,
    std::int16_t touch_y)
{
    static_assert(
        PAPER_MONO_DISPLAY_ROTATION == 0U,
        "update the PaperMono touch transform when display rotation changes");

    constexpr std::int32_t touch_x_max = PAPER_MONO_TOUCH_DEVICE_WIDTH - 1U;
    constexpr std::int32_t touch_y_max = PAPER_MONO_TOUCH_DEVICE_HEIGHT - 1U;
    constexpr std::int32_t screen_x_max = PAPER_MONO_PORTRAIT_WIDTH - 1U;
    constexpr std::int32_t screen_y_max = PAPER_MONO_PORTRAIT_HEIGHT - 1U;

    const std::int32_t clamped_touch_x = clamp_coordinate(touch_x, touch_x_max);
    const std::int32_t clamped_touch_y = clamp_coordinate(touch_y, touch_y_max);

    // PaperMono touch axes are rotated 90 degrees relative to the portrait framebuffer.
    const std::int32_t screen_x =
        (clamped_touch_y * screen_x_max + touch_y_max / 2) / touch_y_max;
    const std::int32_t screen_y =
        ((touch_x_max - clamped_touch_x) * screen_y_max + touch_x_max / 2) /
        touch_x_max;

    return {
        static_cast<std::int16_t>(screen_x),
        static_cast<std::int16_t>(screen_y),
    };
}

const char* touch_event_name(touch_event_type type)
{
    switch (type) {
        case touch_event_type::press:
            return "press";
        case touch_event_type::click:
            return "click";
        case touch_event_type::long_press_start:
            return "long_press_start";
        case touch_event_type::long_press_repeat:
            return "long_press_repeat";
        case touch_event_type::long_press_end:
            return "long_press_end";
    }
    return "unknown";
}

void publish_touch_event(
    const touch_context& context,
    touch_event_type type,
    std::uint32_t now_ms)
{
    touch_event event = {};
    event.type = type;
    event.start_x = context.start_x;
    event.start_y = context.start_y;
    event.end_x = context.current_x;
    event.end_y = context.current_y;
    event.duration_ms = now_ms - context.press_tick_ms;
    event.timestamp_ms = now_ms;

    // Drop the oldest item only when required so the final position is retained.
    if (xQueueSend(event_queue, &event, 0) != pdTRUE) {
        touch_event discarded_event = {};
        xQueueReceive(event_queue, &discarded_event, 0);
        xQueueSend(event_queue, &event, 0);
        ESP_LOGW(log_tag, "event queue full; oldest event discarded");
    }

    if (type == touch_event_type::press) {
        ESP_LOGD(
            log_tag,
            "event=%s start=(%d,%d)",
            touch_event_name(type),
            event.start_x,
            event.start_y);
    } else {
        ESP_LOGI(
            log_tag,
            "event=%s start=(%d,%d) end=(%d,%d) duration=%lu ms",
            touch_event_name(type),
            event.start_x,
            event.start_y,
            event.end_x,
            event.end_y,
            static_cast<unsigned long>(event.duration_ms));
    }
}

void update_touch_state(touch_context& context, std::uint32_t now_ms)
{
    const auto detail = M5.Touch.getDetail();
    const bool is_pressed = detail.isPressed();
    screen_point current_point = {};

    if (is_pressed) {
        current_point = transform_touch_to_screen(detail.x, detail.y);
        context.current_x = current_point.x;
        context.current_y = current_point.y;
    }

    switch (context.state) {
        case touch_state::idle:
            if (is_pressed) {
                context.start_x = current_point.x;
                context.start_y = current_point.y;
                context.current_x = current_point.x;
                context.current_y = current_point.y;
                context.press_tick_ms = now_ms;
                context.state = touch_state::debounce;
                ESP_LOGI(
                    log_tag,
                    "touch transform device=(%d,%d) screen=(%d,%d)",
                    detail.x,
                    detail.y,
                    current_point.x,
                    current_point.y);
            }
            break;

        case touch_state::debounce:
            if (!is_pressed) {
                context.state = touch_state::idle;
            } else if (now_ms - context.press_tick_ms >= TOUCH_DEBOUNCE_TIME_MS) {
                context.state = touch_state::pressed;
                publish_touch_event(context, touch_event_type::press, now_ms);
            }
            break;

        case touch_state::pressed:
            if (!is_pressed) {
                publish_touch_event(context, touch_event_type::click, now_ms);
                context.state = touch_state::idle;
            } else if (now_ms - context.press_tick_ms >= TOUCH_LONG_PRESS_TIME_MS) {
                context.last_long_press_tick_ms = now_ms;
                context.state = touch_state::long_pressed;
                publish_touch_event(context, touch_event_type::long_press_start, now_ms);
            }
            break;

        case touch_state::long_pressed:
            if (!is_pressed) {
                publish_touch_event(context, touch_event_type::long_press_end, now_ms);
                context.state = touch_state::idle;
            } else if (now_ms - context.last_long_press_tick_ms >= LONG_PRESS_REFRESH_INTERVAL_MS) {
                context.last_long_press_tick_ms = now_ms;
                publish_touch_event(context, touch_event_type::long_press_repeat, now_ms);
            }
            break;
    }
}

void touch_task(void*)
{
    touch_context context;

    for (;;) {
        // Multiple timer notifications may collapse into one state update after scheduling delays.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!hal_update_m5()) {
            continue;
        }
        update_touch_state(context, hal_get_tick_ms());
    }
}

}  // namespace

bool hal_touch_start(QueueHandle_t target_event_queue, TaskHandle_t& task_handle)
{
    event_queue = target_event_queue;
    const bool started = xTaskCreate(
                             touch_task,
                             "touch_task",
                             TOUCH_TASK_STACK_SIZE,
                             nullptr,
                             TOUCH_TASK_PRIORITY,
                             &task_handle) == pdPASS;
    if (started) {
        ESP_LOGI(log_tag, "touch task started");
    }
    return started;
}
