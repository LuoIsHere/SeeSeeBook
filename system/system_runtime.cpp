#include "system_runtime.hpp"

#include <cstdint>

#include <esp_log.h>
#include <esp_check.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app.hpp"
#include "battery_service.hpp"
#include "book_service.hpp"
#include "hal.hpp"
#include "input_service.hpp"
#include "rtc_service.hpp"
#include "storage_service.hpp"
#include "system_event_dispatcher.hpp"
#include "system_tick_service.hpp"
#include "ui_renderer.hpp"

namespace {
constexpr char log_tag[] = "system_runtime";
constexpr std::uint32_t stack_monitor_period_ms = 5000U;
constexpr std::uint32_t stack_warning_bytes = 2048U;
constexpr std::uint32_t stack_log_step_bytes = 256U;

void monitor_main_stack()
{
    static std::uint32_t last_check_ms = 0U;
    static UBaseType_t lowest_reported_bytes = UINT32_MAX;
    const std::uint32_t now_ms = system_tick_now_ms();
    if (now_ms - last_check_ms < stack_monitor_period_ms) {
        return;
    }
    last_check_ms = now_ms;
    const UBaseType_t free_bytes = uxTaskGetStackHighWaterMark(nullptr);
    const bool crossed_warning = free_bytes < stack_warning_bytes &&
                                 lowest_reported_bytes >= stack_warning_bytes;
    const bool meaningful_drop = lowest_reported_bytes == UINT32_MAX ||
                                 free_bytes + stack_log_step_bytes <=
                                     lowest_reported_bytes;
    if (!crossed_warning && !meaningful_drop) {
        return;
    }
    lowest_reported_bytes = free_bytes;
    if (free_bytes < stack_warning_bytes) {
        ESP_LOGW(
            log_tag,
            "main stack low water=%lu bytes",
            static_cast<unsigned long>(free_bytes));
    } else {
        ESP_LOGI(
            log_tag,
            "main stack low water=%lu bytes",
            static_cast<unsigned long>(free_bytes));
    }
}
}

esp_err_t system_runtime_init()
{
    ESP_RETURN_ON_ERROR(hal_init(), log_tag, "HAL initialization failed");
    if (!system_tick_service_init()) {
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(ui_renderer_init(), log_tag, "UI renderer initialization failed");
    ESP_RETURN_ON_ERROR(input_service_init(), log_tag, "input service initialization failed");
    ESP_RETURN_ON_ERROR(storage_service_init(), log_tag, "storage service initialization failed");
    ESP_RETURN_ON_ERROR(book_service_init(), log_tag, "book service initialization failed");
    ESP_RETURN_ON_ERROR(rtc_service_init(), log_tag, "RTC service initialization failed");
    ESP_RETURN_ON_ERROR(battery_service_init(), log_tag, "battery service initialization failed");
    ESP_RETURN_ON_ERROR(app_init(), log_tag, "application initialization failed");
    if (!system_tick_service_start()) {
        return ESP_FAIL;
    }
    ESP_LOGI(log_tag, "system runtime initialized");
    return ESP_OK;
}

void system_runtime_update()
{
    system_event_dispatcher_update();
    app_update();
    monitor_main_stack();
}
