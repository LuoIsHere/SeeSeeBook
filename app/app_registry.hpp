#pragma once

#include <mooncake.h>

#include "app.hpp"
#include "app_base.hpp"

struct app_record {
    app_kind kind = app_kind::menu;
    int mooncake_id = -1;
    app_base* instance = nullptr;
};

bool app_registry_install_all(mooncake::Mooncake& runtime);
app_record* app_registry_find(app_kind kind);
