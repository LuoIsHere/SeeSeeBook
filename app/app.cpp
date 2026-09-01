#include "app.hpp"

#include <esp_log.h>
#include <mooncake.h>

#include "app_descriptor.hpp"
#include "app_registry.hpp"
#include "ui_interaction_router.hpp"

namespace {

constexpr char log_tag[] = "app_runtime";

mooncake::Mooncake mooncake_runtime;
app_record* foreground_record = nullptr;
app_kind foreground_kind = app_kind::menu;
app_kind pending_target = app_kind::menu;
app_kind return_target = app_kind::menu;
bool has_pending_switch = false;

void apply_pending_switch()
{
    if (!has_pending_switch) {
        return;
    }
    app_record* target = app_registry_find(pending_target);
    const app_descriptor* descriptor = app_descriptor_find(pending_target);
    if (target == nullptr || descriptor == nullptr) {
        ESP_LOGE(log_tag, "switch target is not registered kind=%u",
                 static_cast<unsigned>(pending_target));
        has_pending_switch = false;
        return;
    }
    if (foreground_record == target) {
        has_pending_switch = false;
        return;
    }
    if (foreground_record != nullptr) {
        mooncake_runtime.closeApp(foreground_record->mooncake_id);
    }
    ui_interaction_set_view(descriptor->view);
    mooncake_runtime.openApp(target->mooncake_id);
    foreground_record = target;
    foreground_kind = pending_target;
    has_pending_switch = false;
    ESP_LOGI(log_tag, "foreground switched target=%s id=%d",
             descriptor->name, target->mooncake_id);
}

}  // namespace

esp_err_t app_init()
{
    if (!app_registry_install_all(mooncake_runtime)) {
        ESP_LOGE(log_tag, "failed to install Mooncake apps");
        return ESP_FAIL;
    }
    app_request_switch(app_kind::menu);
    ESP_LOGI(log_tag, "Mooncake applications installed");
    return ESP_OK;
}

void app_update()
{
    apply_pending_switch();
    mooncake_runtime.update();
}

void app_dispatch_event(const app_event& event)
{
    if (!has_pending_switch && foreground_record != nullptr) {
        foreground_record->instance->handle_app_event(event);
    }
}

void app_request_switch(app_kind target)
{
    if (app_descriptor_find(target) == nullptr) {
        ESP_LOGW(log_tag, "ignored unregistered switch target kind=%u",
                 static_cast<unsigned>(target));
        return;
    }
    if (target != app_kind::menu && foreground_record != nullptr &&
        foreground_kind != target) {
        return_target = foreground_kind;
    }
    pending_target = target;
    has_pending_switch = true;
}

void app_request_back()
{
    app_request_switch(return_target);
}

bool app_switch_pending()
{
    return has_pending_switch;
}
