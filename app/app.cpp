#include "app.hpp"

#include <esp_log.h>
#include <mooncake.h>
#include <algorithm>
#include <array>

#include "app_descriptor.hpp"
#include "app_registry.hpp"
#include "ui_interaction_router.hpp"
#include "ui_renderer.hpp"

namespace {

constexpr char log_tag[] = "app_runtime";

mooncake::Mooncake mooncake_runtime;
app_record* foreground_record = nullptr;
app_kind foreground_kind = app_kind::launcher;
app_kind pending_target = app_kind::launcher;
std::array<app_kind, 8U> return_history = {};
std::size_t return_depth = 0U;
app_launch_context pending_launch = {};
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
        pending_launch.clear();
        return;
    }
    if (foreground_record == target) {
        has_pending_switch = false;
        pending_launch.clear();
        return;
    }
    const bool prepared = target->instance->prepare_launch(pending_launch);
    pending_launch.clear();
    if (!prepared) {
        has_pending_switch = false;
        if (return_depth > 0U) {
            --return_depth;
        }
        return;
    }
    if (foreground_record != nullptr) {
        mooncake_runtime.closeApp(foreground_record->mooncake_id);
    }
    ui_interaction_set_view(descriptor->view);
    ui_status_bar_set_foreground(descriptor->view);
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
    app_request_switch(app_kind::launcher);
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
    if (event.type == app_event_type::navigation &&
        event.navigation.action == navigation_action::back) {
        if (!has_pending_switch && foreground_record != nullptr &&
            foreground_kind != app_kind::launcher) {
            app_request_back();
        }
        return;
    }
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
    if (target != app_kind::launcher && foreground_record != nullptr &&
        foreground_kind != target) {
        if (return_depth == return_history.size()) {
            std::move(return_history.begin() + 1U, return_history.end(), return_history.begin());
            --return_depth;
        }
        return_history[return_depth++] = foreground_kind;
    } else if (target == app_kind::launcher) {
        return_depth = 0U;
    }
    pending_launch.clear();
    pending_target = target;
    has_pending_switch = true;
}

void app_request_back()
{
    pending_target = return_depth == 0U ? app_kind::launcher : return_history[--return_depth];
    has_pending_switch = true;
    pending_launch.clear();
}

bool app_request_launch(app_kind target, const app_launch_context& context)
{
    if (!context.has_value() || app_descriptor_find(target) == nullptr || has_pending_switch ||
        foreground_kind == target) {
        return false;
    }
    app_request_switch(target);
    if (!has_pending_switch) { return false; }
    pending_launch = context;
    return true;
}

bool app_switch_pending()
{
    return has_pending_switch;
}
