#pragma once

#include <cstddef>

#include "app.hpp"
#include "menu_view.hpp"

struct menu_entry_descriptor {
    app_kind target;
    const char* label;
};

// Array position defines the visible menu order.
inline constexpr menu_entry_descriptor menu_entries[] = {
    {app_kind::test, "Screen Setting"},
    {app_kind::rtc_setting, "RTC Setting"},
    {app_kind::battery, "Battery"},
    {app_kind::file, "Files"},
};

inline constexpr std::size_t menu_layout_entry_count = std::size(menu_entries);

constexpr std::size_t menu_label_length(const char* label)
{
    std::size_t length = 0U;
    while (label[length] != '\0') {
        ++length;
    }
    return length;
}

constexpr bool menu_layout_has_unique_targets()
{
    for (std::size_t left = 0U; left < menu_layout_entry_count; ++left) {
        for (std::size_t right = left + 1U; right < menu_layout_entry_count; ++right) {
            if (menu_entries[left].target == menu_entries[right].target) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool menu_layout_labels_fit()
{
    for (const menu_entry_descriptor& entry : menu_entries) {
        if (menu_label_length(entry.label) >= menu_view_label_capacity) {
            return false;
        }
    }
    return true;
}

static_assert(
    menu_layout_entry_count <= menu_view_entry_capacity,
    "Menu layout exceeds menu_view_entry_capacity");
static_assert(menu_layout_has_unique_targets(), "Menu layout contains duplicate App targets");
static_assert(menu_layout_labels_fit(), "Menu label exceeds menu_view_label_capacity");
