#include "app.hpp"

#include <memory>

#include <esp_log.h>
#include <mooncake.h>

#include "app_base.hpp"
#include "battery/battery_app.hpp"
#include "file/file_app.hpp"
#include "menu/menu_app.hpp"
#include "rtc_setting/rtc_setting_app.hpp"
#include "test/test_app.hpp"
#include "ui_interaction_router.hpp"

namespace {

constexpr char log_tag[] = "app_runtime";

struct app_record {
    int mooncake_id = -1;
    app_base* instance = nullptr;
};

mooncake::Mooncake mooncake_runtime;
app_record records[5] = {};
app_record* foreground_record = nullptr;
app_kind foreground_kind = app_kind::menu;
app_kind pending_target = app_kind::menu;
app_kind return_target = app_kind::menu;
bool has_pending_switch = false;

std::size_t app_index(app_kind kind)
{
    return static_cast<std::size_t>(kind);
}

ui_view_id view_for(app_kind kind)
{
    switch (kind) {
        case app_kind::menu:
            return ui_view_id::menu;
        case app_kind::test:
            return ui_view_id::test;
        case app_kind::rtc_setting:
            return ui_view_id::rtc_setting;
        case app_kind::battery:
            return ui_view_id::battery;
        case app_kind::file:
            return ui_view_id::file;
    }
    return ui_view_id::menu;
}

const char* app_kind_name(app_kind kind)
{
    switch (kind) {
        case app_kind::menu:
            return "menu";
        case app_kind::test:
            return "test";
        case app_kind::rtc_setting:
            return "rtc_setting";
        case app_kind::battery:
            return "battery";
        case app_kind::file:
            return "file";
    }
    return "unknown";
}

template <typename app_type>
app_record install_app()
{
    auto instance = std::make_unique<app_type>();
    app_record record = {};
    record.instance = instance.get();
    record.mooncake_id = mooncake_runtime.installApp(std::move(instance));
    return record;
}

void apply_pending_switch()
{
    if (!has_pending_switch) {
        return;
    }
    app_record& target = records[app_index(pending_target)];
    if (foreground_record == &target) {
        has_pending_switch = false;
        return;
    }
    if (foreground_record != nullptr) {
        mooncake_runtime.closeApp(foreground_record->mooncake_id);
    }
    mooncake_runtime.openApp(target.mooncake_id);
    foreground_record = &target;
    foreground_kind = pending_target;
    ui_interaction_set_view(view_for(foreground_kind));
    has_pending_switch = false;
    ESP_LOGI(log_tag, "foreground switched target=%s id=%d",
             app_kind_name(foreground_kind), target.mooncake_id);
}

}  // namespace

esp_err_t app_init()
{
    records[app_index(app_kind::menu)] = install_app<menu_app>();
    records[app_index(app_kind::test)] = install_app<test_app>();
    records[app_index(app_kind::rtc_setting)] = install_app<rtc_setting_app>();
    records[app_index(app_kind::battery)] = install_app<battery_app>();
    records[app_index(app_kind::file)] = install_app<file_app>();
    for (const app_record& record : records) {
        if (record.mooncake_id < 0 || record.instance == nullptr) {
            ESP_LOGE(log_tag, "failed to install Mooncake apps");
            return ESP_FAIL;
        }
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
