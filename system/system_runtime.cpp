#include "system_runtime.hpp"

#include <esp_log.h>
#include <esp_check.h>

#include "app.hpp"
#include "battery_service.hpp"
#include "hal.hpp"
#include "input_service.hpp"
#include "rtc_service.hpp"
#include "storage_service.hpp"
#include "system_event_dispatcher.hpp"
#include "system_tick_service.hpp"
#include "ui_renderer.hpp"

namespace {
constexpr char log_tag[] = "system_runtime";
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
}
