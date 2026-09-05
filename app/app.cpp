#include "app.hpp"

#include <esp_log.h>
#include <mooncake.h>
#include <algorithm>
#include <array>
#include <cstring>

#include "app_descriptor.hpp"
#include "app_registry.hpp"
#include "ui_interaction_router.hpp"
#include "ui_renderer.hpp"

namespace {

constexpr char log_tag[] = "app_runtime";

mooncake::Mooncake mooncake_runtime;
app_record* foreground_record = nullptr;
app_kind foreground_kind = app_kind::menu;
app_kind pending_target = app_kind::menu;
std::array<app_kind, 8U> return_history = {};
std::size_t return_depth = 0U;
app_launch_context reader_launch = {};
bool has_reader_launch = false;
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
    if (pending_target == app_kind::reader) {
        const bool prepared = has_reader_launch && target->instance->prepare_launch(reader_launch);
        reader_launch = {};
        has_reader_launch = false;
        if (!prepared) {
            has_pending_switch = false;
            if (return_depth > 0U) {
                --return_depth;
            }
            return;
        }
    }
    if (foreground_record != nullptr) {
        mooncake_runtime.closeApp(foreground_record->mooncake_id);
    }
    ui_interaction_set_view(descriptor->view);
    if (ui_status_bar_set_foreground(descriptor->view)) { ui_renderer_notify_status_bar(); }
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
        if (return_depth == return_history.size()) {
            std::move(return_history.begin() + 1U, return_history.end(), return_history.begin());
            --return_depth;
        }
        return_history[return_depth++] = foreground_kind;
    } else if (target == app_kind::menu) {
        return_depth = 0U;
    }
    if (target != app_kind::reader) {
        reader_launch = {};
        has_reader_launch = false;
    }
    pending_target = target;
    has_pending_switch = true;
}

void app_request_back()
{
    pending_target = return_depth == 0U ? app_kind::menu : return_history[--return_depth];
    has_pending_switch = true;
    reader_launch = {};
    has_reader_launch = false;
}

bool app_request_open_reader(const char* path, std::uint32_t media_generation,
                             book_file_format format)
{
    if (path == nullptr || path[0] != '/' || has_pending_switch ||
        foreground_kind == app_kind::reader || std::strlen(path) > STORAGE_MAX_PATH_LENGTH ||
        (format != book_file_format::txt && format != book_file_format::epub)) {
        return false;
    }
    reader_launch = {};
    std::strcpy(reader_launch.file_path, path);
    reader_launch.media_generation = media_generation;
    reader_launch.format = format;
    has_reader_launch = true;
    app_request_switch(app_kind::reader);
    return has_pending_switch;
}

bool app_switch_pending()
{
    return has_pending_switch;
}
