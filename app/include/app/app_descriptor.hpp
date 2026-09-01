#pragma once

#include <cstddef>

#include "app.hpp"
#include "ui_action.hpp"

struct app_descriptor {
    app_kind kind;
    ui_view_id view;
    const char* name;
};

std::size_t app_descriptor_count();
const app_descriptor* app_descriptor_find(app_kind kind);
