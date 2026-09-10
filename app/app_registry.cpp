#include "app_registry.hpp"

#include <array>
#include <memory>

#include <esp_log.h>

#include "app_descriptor.hpp"
#include "battery/battery_app.hpp"
#include "books/books_app.hpp"
#include "file/file_app.hpp"
#include "gray4_test/gray4_test_app.hpp"
#include "launcher/launcher_app.hpp"
#include "launcher/launcher_layout.hpp"
#include "menu/menu_app.hpp"
#include "menu/menu_layout.hpp"
#include "reader/reader_app.hpp"
#include "rtc_setting/rtc_setting_app.hpp"
#include "test/test_app.hpp"

namespace {

constexpr char log_tag[] = "app_registry";

using app_factory = std::unique_ptr<app_base> (*)();

struct app_registration {
    app_descriptor descriptor;
    app_factory create;
};

template <typename app_type>
std::unique_ptr<app_base> create_app()
{
    return std::make_unique<app_type>();
}

constexpr app_registration registrations[] = {
    {{app_kind::launcher, ui_view_id::launcher, "LauncherApp"},
     &create_app<launcher_app>},
    {{app_kind::books, ui_view_id::books, "BooksApp"}, &create_app<books_app>},
    {{app_kind::menu, ui_view_id::menu, "MenuApp"}, &create_app<menu_app>},
    {{app_kind::test, ui_view_id::test, "TestApp"}, &create_app<test_app>},
    {{app_kind::rtc_setting, ui_view_id::rtc_setting, "RTCSettingApp"},
     &create_app<rtc_setting_app>},
    {{app_kind::battery, ui_view_id::battery, "BatteryApp"},
     &create_app<battery_app>},
    {{app_kind::file, ui_view_id::file, "FileApp"}, &create_app<file_app>},
    {{app_kind::reader, ui_view_id::reader, "ReaderApp"}, &create_app<reader_app>},
    {{app_kind::gray4_test, ui_view_id::gray4_test, "Gray4TestApp"},
     &create_app<gray4_test_app>},
};

std::array<app_record, std::size(registrations)> records = {};

template <typename entry_type, std::size_t count>
bool entry_targets_are_registered(const entry_type (&entries)[count])
{
    for (const entry_type& entry : entries) {
        if (app_descriptor_find(entry.target) == nullptr) {
            ESP_LOGE(
                log_tag,
                "configured target is not registered kind=%u",
                static_cast<unsigned>(entry.target));
            return false;
        }
    }
    return true;
}

}  // namespace

std::size_t app_descriptor_count()
{
    return std::size(registrations);
}

const app_descriptor* app_descriptor_find(app_kind kind)
{
    for (const app_registration& registration : registrations) {
        if (registration.descriptor.kind == kind) {
            return &registration.descriptor;
        }
    }
    return nullptr;
}

bool app_registry_install_all(mooncake::Mooncake& runtime)
{
    if (!entry_targets_are_registered(launcher_entries) ||
        !entry_targets_are_registered(menu_entries)) {
        return false;
    }
    for (std::size_t index = 0U; index < std::size(registrations); ++index) {
        const app_registration& registration = registrations[index];
        std::unique_ptr<app_base> instance = registration.create();
        if (instance == nullptr) {
            ESP_LOGE(log_tag, "failed to create app name=%s", registration.descriptor.name);
            return false;
        }
        instance->set_app_name(registration.descriptor.name);
        app_record record = {};
        record.kind = registration.descriptor.kind;
        record.instance = instance.get();
        record.mooncake_id = runtime.installApp(std::move(instance));
        if (record.mooncake_id < 0) {
            ESP_LOGE(log_tag, "failed to install app name=%s", registration.descriptor.name);
            return false;
        }
        records[index] = record;
    }
    return true;
}

app_record* app_registry_find(app_kind kind)
{
    for (app_record& record : records) {
        if (record.kind == kind && record.mooncake_id >= 0 && record.instance != nullptr) {
            return &record;
        }
    }
    return nullptr;
}
