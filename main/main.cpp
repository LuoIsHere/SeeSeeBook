#include <esp_err.h>
#include <esp_log.h>

#include "app.h"
#include "hal.h"
#include "types.hpp"

namespace {

constexpr char log_tag[] = "main";

}  // namespace

extern "C" void app_main()
{
    ESP_LOGI(log_tag, "starting project=%s version=%s", PROJECT_NAME, PROJECT_VERSION);
    ESP_ERROR_CHECK(hal_init());
    app_init();

    for (;;) {
        app_loop();
    }
}
