#include "hal.hpp"

#include <M5Unified.h>
#include <driver/gptimer.h>
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "system_config.hpp"
#include "types.hpp"

namespace {

constexpr char log_tag[] = "hal_core";

QueueHandle_t touch_event_queue = nullptr;
QueueHandle_t display_request_queue = nullptr;
QueueHandle_t display_control_queue = nullptr;
TaskHandle_t touch_task_handle = nullptr;
TaskHandle_t display_task_handle = nullptr;
SemaphoreHandle_t internal_i2c_mutex = nullptr;
gptimer_handle_t system_timer = nullptr;
volatile std::uint32_t system_tick_ms = 0;
std::uint32_t touch_notification_elapsed_ms = 0;

bool IRAM_ATTR system_timer_alarm_callback(
    gptimer_handle_t,
    const gptimer_alarm_event_data_t*,
    void*)
{
    system_tick_ms += SYSTEM_TICK_PERIOD_MS;
    touch_notification_elapsed_ms += SYSTEM_TICK_PERIOD_MS;

    BaseType_t higher_priority_task_woken = pdFALSE;
    if (touch_task_handle != nullptr &&
        touch_notification_elapsed_ms >= TOUCH_SCAN_PERIOD_MS) {
        touch_notification_elapsed_ms = 0;
        vTaskNotifyGiveFromISR(touch_task_handle, &higher_priority_task_woken);
    }

    return higher_priority_task_woken == pdTRUE;
}

esp_err_t system_timer_init()
{
    gptimer_config_t timer_config = {};
    timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_config.direction = GPTIMER_COUNT_UP;
    timer_config.resolution_hz = 1'000'000;
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&timer_config, &system_timer), log_tag, "create GPTimer");

    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = system_timer_alarm_callback;
    ESP_RETURN_ON_ERROR(
        gptimer_register_event_callbacks(system_timer, &callbacks, nullptr),
        log_tag,
        "register GPTimer callback");

    gptimer_alarm_config_t alarm_config = {};
    alarm_config.alarm_count = SYSTEM_TICK_PERIOD_MS * 1000U;
    alarm_config.reload_count = 0;
    alarm_config.flags.auto_reload_on_alarm = true;
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(system_timer, &alarm_config), log_tag, "set GPTimer alarm");
    ESP_RETURN_ON_ERROR(gptimer_enable(system_timer), log_tag, "enable GPTimer");
    ESP_RETURN_ON_ERROR(gptimer_start(system_timer), log_tag, "start GPTimer");

    return ESP_OK;
}

}  // namespace

esp_err_t hal_init()
{
    touch_event_queue = xQueueCreate(TOUCH_EVENT_QUEUE_LENGTH, sizeof(touch_event));
    display_request_queue = xQueueCreate(DISPLAY_REQUEST_QUEUE_LENGTH, sizeof(display_request));
    display_control_queue =
        xQueueCreate(DISPLAY_CONTROL_QUEUE_LENGTH, sizeof(display_control_request));
    internal_i2c_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(
        touch_event_queue != nullptr && display_request_queue != nullptr &&
            display_control_queue != nullptr && internal_i2c_mutex != nullptr,
        ESP_ERR_NO_MEM,
        log_tag,
        "create HAL synchronization objects");

    auto m5_config = M5.config();
    m5_config.clear_display = false;
    m5_config.internal_imu = false;
    m5_config.internal_rtc = false;
    m5_config.internal_mic = false;
    m5_config.internal_spk = false;
    m5_config.fallback_board = m5::board_t::board_M5PaperMono;
    M5.begin(m5_config);

    ESP_RETURN_ON_FALSE(M5.Display.isEPD(), ESP_ERR_NOT_FOUND, log_tag, "PaperMono EPD not found");
    ESP_RETURN_ON_FALSE(M5.Touch.isEnabled(), ESP_ERR_NOT_FOUND, log_tag, "PaperMono touch not found");

    // Use one explicit portrait orientation for display and touch normalization.
    M5.Display.setRotation(PAPER_MONO_DISPLAY_ROTATION);
    ESP_RETURN_ON_FALSE(
        M5.Display.width() == PAPER_MONO_PORTRAIT_WIDTH &&
            M5.Display.height() == PAPER_MONO_PORTRAIT_HEIGHT,
        ESP_ERR_INVALID_SIZE,
        log_tag,
        "unexpected PaperMono portrait dimensions");
    M5.Display.setAutoDisplay(false);
    M5.Display.setBrightness(FRONT_LIGHT_LEVEL_50);

    ESP_RETURN_ON_FALSE(
        hal_display_start(
            display_request_queue,
            display_control_queue,
            display_task_handle),
        ESP_ERR_NO_MEM,
        log_tag,
        "start display task");
    ESP_RETURN_ON_FALSE(
        hal_touch_start(touch_event_queue, touch_task_handle),
        ESP_ERR_NO_MEM,
        log_tag,
        "start touch task");
    ESP_RETURN_ON_ERROR(system_timer_init(), log_tag, "initialize system timer");

    ESP_LOGI(
        log_tag,
        "project=%s version=%s board=%d rotation=%u display=%dx%d",
        PROJECT_NAME,
        PROJECT_VERSION,
        static_cast<int>(M5.getBoard()),
        static_cast<unsigned>(M5.Display.getRotation()),
        M5.Display.width(),
        M5.Display.height());
    return ESP_OK;
}

bool hal_try_get_touch_event(touch_event& event)
{
    return touch_event_queue != nullptr &&
           xQueueReceive(touch_event_queue, &event, 0) == pdTRUE;
}

bool hal_submit_display_request(const display_request& request)
{
    if (display_request_queue == nullptr) {
        return false;
    }
    const bool submitted = xQueueOverwrite(display_request_queue, &request) == pdTRUE;
    if (submitted && display_task_handle != nullptr) {
        xTaskNotifyGive(display_task_handle);
    }
    return submitted;
}

bool hal_submit_display_control_request(const display_control_request& request)
{
    if (display_control_queue == nullptr) {
        return false;
    }

    bool submitted = xQueueSend(display_control_queue, &request, 0) == pdTRUE;
    if (!submitted) {
        // Prefer the newest transition so a released button cannot remain inverted.
        display_control_request discarded_request = {};
        xQueueReceive(display_control_queue, &discarded_request, 0);
        submitted = xQueueSend(display_control_queue, &request, 0) == pdTRUE;
        ESP_LOGW(log_tag, "display control queue full; oldest request discarded");
    }
    if (submitted && display_task_handle != nullptr) {
        xTaskNotifyGive(display_task_handle);
    }
    return submitted;
}

void hal_update_m5()
{
    if (internal_i2c_mutex == nullptr) {
        return;
    }

    xSemaphoreTake(internal_i2c_mutex, portMAX_DELAY);
    M5.update();
    xSemaphoreGive(internal_i2c_mutex);
}

void hal_set_front_light(std::uint8_t brightness)
{
    if (internal_i2c_mutex == nullptr) {
        return;
    }

    xSemaphoreTake(internal_i2c_mutex, portMAX_DELAY);
    M5.Display.setBrightness(brightness);
    xSemaphoreGive(internal_i2c_mutex);
}

std::uint32_t hal_get_tick_ms()
{
    // Aligned 32-bit reads are atomic on ESP32-S3; unsigned subtraction handles wraparound.
    return system_tick_ms;
}
