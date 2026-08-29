#include "app.hpp"

#include <memory>

#include <esp_log.h>

#include <mooncake.h>

#include "app_base.hpp"
#include "input_manager.hpp"
#include "menu/menu_app.hpp"
#include "test/test_app.hpp"

namespace {

constexpr char log_tag[] = "app_runtime";

struct app_record {
    int mooncake_id = -1;
    app_base* instance = nullptr;
};

mooncake::Mooncake mooncake_runtime;
app_record menu_record;
app_record test_record;
app_record* foreground_record = nullptr;
app_kind pending_target = app_kind::menu;
bool has_pending_switch = false;

app_record& record_for(app_kind kind)
{
    return kind == app_kind::menu ? menu_record : test_record;
}

void apply_pending_switch()
{
    if (!has_pending_switch) {
        return;
    }

    app_record& target_record = record_for(pending_target);
    if (foreground_record == &target_record) {
        has_pending_switch = false;
        return;
    }

    if (foreground_record != nullptr) {
        mooncake_runtime.closeApp(foreground_record->mooncake_id);
    }
    mooncake_runtime.openApp(target_record.mooncake_id);
    foreground_record = &target_record;
    input_manager_set_target(target_record.instance);
    has_pending_switch = false;

    ESP_LOGI(
        log_tag,
        "foreground app switched target=%s id=%d",
        pending_target == app_kind::menu ? "menu" : "test",
        target_record.mooncake_id);
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

}  // namespace

esp_err_t app_init()
{
    menu_record = install_app<menu_app>();
    test_record = install_app<test_app>();
    if (menu_record.mooncake_id < 0 || test_record.mooncake_id < 0) {
        ESP_LOGE(log_tag, "failed to install Mooncake apps");
        return ESP_FAIL;
    }

    app_request_switch(app_kind::menu);
    ESP_LOGI(
        log_tag,
        "Mooncake apps installed menu_id=%d test_id=%d",
        menu_record.mooncake_id,
        test_record.mooncake_id);
    return ESP_OK;
}

void app_update()
{
    apply_pending_switch();
    mooncake_runtime.update();
}

void app_request_switch(app_kind target)
{
    pending_target = target;
    has_pending_switch = true;
}

bool app_switch_pending()
{
    return has_pending_switch;
}
