#pragma once

#include <cstdint>

#include "battery_snapshot.hpp"
#include "ui_action.hpp"

struct status_bar_view_state {
    std::uint8_t hour;
    std::uint8_t minute;
    bool time_valid;
    battery_snapshot battery;
    ui_view_id foreground_app;
    bool reader_page_valid;
    std::uint32_t current_page;
    std::uint32_t total_pages;
};
