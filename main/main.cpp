#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app.hpp"
#include "hal.hpp"
#include "input_manager.hpp"
#include "system_config.hpp"
#include "types.hpp"

namespace {

constexpr char log_tag[] = "main";

}  // namespace

extern "C" void app_main()
{
    ESP_LOGI(log_tag, "starting project=%s version=%s", PROJECT_NAME, PROJECT_VERSION);
    ESP_ERROR_CHECK(hal_init());
    ESP_ERROR_CHECK(app_init());

    for (;;) {
        input_manager_update();
        app_update();
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
    }
}
