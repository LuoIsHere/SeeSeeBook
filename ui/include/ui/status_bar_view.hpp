#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "battery_snapshot.hpp"
#include "ui_action.hpp"

inline constexpr std::size_t status_bar_center_text_capacity = 24U;

enum class status_bar_center_kind : std::uint8_t {
    none,
    text,
    page,
};

struct status_bar_view_state {
    std::uint8_t hour;
    std::uint8_t minute;
    bool time_valid;
    battery_snapshot battery;
    ui_view_id foreground_app;
    status_bar_center_kind center_kind;
    char center_text[status_bar_center_text_capacity];
    std::uint32_t center_current_page;
    std::uint32_t center_total_pages;
};

static_assert(std::is_trivially_copyable_v<status_bar_view_state>);
