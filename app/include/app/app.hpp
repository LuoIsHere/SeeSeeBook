#pragma once

#include <cstdint>

#include <esp_err.h>

#include "app_event.hpp"

enum class app_kind : std::uint8_t {
    menu,
    test,
    rtc_setting,
    battery,
    file,
    reader,
};

esp_err_t app_init();
void app_update();
void app_dispatch_event(const app_event& event);
void app_request_switch(app_kind target);
bool app_request_open_reader(const char* path, std::uint32_t media_generation);
void app_request_back();
bool app_switch_pending();
