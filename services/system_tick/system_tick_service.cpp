#include "system_tick_service.hpp"

#include <array>

#include <driver/gptimer.h>
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_log.h>

#include "system_config.hpp"

namespace {

constexpr char log_tag[] = "service_tick";
constexpr std::size_t maximum_subscribers = 4U;

struct tick_subscriber {
    TaskHandle_t task_handle;
    std::uint32_t period_ms;
    std::uint32_t elapsed_ms;
};

std::array<tick_subscriber, maximum_subscribers> subscribers = {};
gptimer_handle_t system_timer = nullptr;
volatile std::uint32_t system_tick_ms = 0U;
bool timer_started = false;

bool IRAM_ATTR system_timer_alarm_callback(
    gptimer_handle_t,
    const gptimer_alarm_event_data_t*,
    void*)
{
    system_tick_ms += SYSTEM_TICK_PERIOD_MS;
    BaseType_t higher_priority_task_woken = pdFALSE;
    for (tick_subscriber& subscriber : subscribers) {
        if (subscriber.task_handle == nullptr) {
            continue;
        }
        subscriber.elapsed_ms += SYSTEM_TICK_PERIOD_MS;
        if (subscriber.elapsed_ms < subscriber.period_ms) {
            continue;
        }
        subscriber.elapsed_ms = 0U;
        vTaskNotifyGiveFromISR(
            subscriber.task_handle,
            &higher_priority_task_woken);
    }
    return higher_priority_task_woken == pdTRUE;
}

}  // namespace

bool system_tick_service_init()
{
    gptimer_config_t timer_config = {};
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = 1'000'000;
    if (gptimer_new_timer(&timer_config, &system_timer) != ESP_OK) {
        return false;
    }

    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = system_timer_alarm_callback;
    if (gptimer_register_event_callbacks(system_timer, &callbacks, nullptr) != ESP_OK) {
        return false;
    }

    gptimer_alarm_config_t alarm_config = {};
    alarm_config.alarm_count = SYSTEM_TICK_PERIOD_MS * 1000U;
    alarm_config.reload_count = 0U;
    alarm_config.flags.auto_reload_on_alarm = true;
    if (gptimer_set_alarm_action(system_timer, &alarm_config) != ESP_OK ||
        gptimer_enable(system_timer) != ESP_OK) {
        return false;
    }
    ESP_LOGI(log_tag, "1 ms GPTimer initialized");
    return true;
}

bool system_tick_service_register_task(
    TaskHandle_t task_handle,
    std::uint32_t period_ms)
{
    if (task_handle == nullptr || period_ms == 0U || timer_started) {
        return false;
    }
    for (tick_subscriber& subscriber : subscribers) {
        if (subscriber.task_handle == nullptr) {
            subscriber.task_handle = task_handle;
            subscriber.period_ms = period_ms;
            subscriber.elapsed_ms = 0U;
            return true;
        }
    }
    return false;
}

bool system_tick_service_start()
{
    if (system_timer == nullptr || timer_started) {
        return false;
    }
    timer_started = gptimer_start(system_timer) == ESP_OK;
    if (timer_started) {
        ESP_LOGI(log_tag, "GPTimer scheduler started");
    }
    return timer_started;
}

std::uint32_t system_tick_now_ms()
{
    return system_tick_ms;
}
