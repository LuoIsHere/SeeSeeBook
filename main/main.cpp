#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "project_info.hpp"
#include "system_config.hpp"
#include "system_runtime.hpp"

namespace {
constexpr char log_tag[] = "main";
}

extern "C" void app_main()
{
    ESP_LOGI(log_tag, "starting project=%s version=%s", PROJECT_NAME, PROJECT_VERSION);
    ESP_ERROR_CHECK(system_runtime_init());
    for (;;) {
        system_runtime_update();
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}
