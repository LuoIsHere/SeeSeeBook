#include "input_service.hpp"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "system_config.hpp"
#include "system_tick_service.hpp"
#include "touch.hpp"

namespace {

constexpr char log_tag[] = "service_input";

enum class input_state : std::uint8_t {
    idle,
    debounce,
    pressed,
    long_pressed,
};

struct input_context {
    input_state state = input_state::idle;
    std::int16_t start_x = 0;
    std::int16_t start_y = 0;
    std::int16_t current_x = 0;
    std::int16_t current_y = 0;
    std::uint32_t press_tick_ms = 0U;
    std::uint32_t last_long_press_tick_ms = 0U;
};

QueueHandle_t event_queue = nullptr;
TaskHandle_t input_task_handle = nullptr;

const char* gesture_name(input_gesture_type gesture)
{
    switch (gesture) {
        case input_gesture_type::press:
            return "press";
        case input_gesture_type::click:
            return "click";
        case input_gesture_type::long_press_start:
            return "long_press_start";
        case input_gesture_type::long_press_repeat:
            return "long_press_repeat";
        case input_gesture_type::long_press_end:
            return "long_press_end";
    }
    return "unknown";
}

void publish_event(
    const input_context& context,
    input_gesture_type gesture,
    std::uint32_t now_ms)
{
    input_event event = {};
    event.gesture = gesture;
    event.start_x = context.start_x;
    event.start_y = context.start_y;
    event.end_x = context.current_x;
    event.end_y = context.current_y;
    event.duration_ms = now_ms - context.press_tick_ms;
    event.timestamp_ms = now_ms;
    if (xQueueSend(event_queue, &event, 0) != pdTRUE) {
        input_event discarded = {};
        xQueueReceive(event_queue, &discarded, 0);
        xQueueSend(event_queue, &event, 0);
        ESP_LOGW(log_tag, "event queue full; oldest event discarded");
    }
    if (gesture == input_gesture_type::click ||
        gesture == input_gesture_type::long_press_end) {
        ESP_LOGI(
            log_tag,
            "release gesture=%s start=%d,%d end=%d,%d duration_ms=%lu timestamp_ms=%lu",
            gesture_name(gesture),
            event.start_x,
            event.start_y,
            event.end_x,
            event.end_y,
            static_cast<unsigned long>(event.duration_ms),
            static_cast<unsigned long>(event.timestamp_ms));
    }
}

void update_state(
    input_context& context,
    const touch_sample& sample,
    std::uint32_t now_ms)
{
    if (sample.pressed) {
        context.current_x = sample.x;
        context.current_y = sample.y;
    }

    switch (context.state) {
        case input_state::idle:
            if (sample.pressed) {
                context.start_x = sample.x;
                context.start_y = sample.y;
                context.current_x = sample.x;
                context.current_y = sample.y;
                context.press_tick_ms = now_ms;
                context.state = input_state::debounce;
            }
            break;
        case input_state::debounce:
            if (!sample.pressed) {
                context.state = input_state::idle;
            } else if (now_ms - context.press_tick_ms >= INPUT_DEBOUNCE_TIME_MS) {
                context.state = input_state::pressed;
                publish_event(context, input_gesture_type::press, now_ms);
            }
            break;
        case input_state::pressed:
            if (!sample.pressed) {
                publish_event(context, input_gesture_type::click, now_ms);
                context.state = input_state::idle;
            } else if (now_ms - context.press_tick_ms >= INPUT_LONG_PRESS_TIME_MS) {
                context.last_long_press_tick_ms = now_ms;
                context.state = input_state::long_pressed;
                publish_event(context, input_gesture_type::long_press_start, now_ms);
            }
            break;
        case input_state::long_pressed:
            if (!sample.pressed) {
                publish_event(context, input_gesture_type::long_press_end, now_ms);
                context.state = input_state::idle;
            } else if (now_ms - context.last_long_press_tick_ms >=
                       INPUT_LONG_PRESS_REPEAT_MS) {
                context.last_long_press_tick_ms = now_ms;
                publish_event(context, input_gesture_type::long_press_repeat, now_ms);
            }
            break;
    }
}

void input_task(void*)
{
    input_context context;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        touch_sample sample = {};
        if (hal_touch_sample(sample)) {
            update_state(context, sample, system_tick_now_ms());
        }
    }
}

}  // namespace

esp_err_t input_service_init()
{
    event_queue = xQueueCreate(INPUT_EVENT_QUEUE_LENGTH, sizeof(input_event));
    if (event_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            input_task,
            "input_service",
            INPUT_TASK_STACK_SIZE,
            nullptr,
            INPUT_TASK_PRIORITY,
            &input_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (!system_tick_service_register_task(input_task_handle, TOUCH_SCAN_PERIOD_MS)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(log_tag, "input state machine started period_ms=%u", TOUCH_SCAN_PERIOD_MS);
    return ESP_OK;
}

bool input_service_try_get_event(input_event& event)
{
    return event_queue != nullptr && xQueueReceive(event_queue, &event, 0) == pdTRUE;
}
