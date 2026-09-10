#pragma once

#include <cstddef>

#include "app.hpp"
#include "launcher_view.hpp"

struct launcher_entry_descriptor {
    app_kind target;
    const char* label;
};

// Array position defines the first-level launcher order.
inline constexpr launcher_entry_descriptor launcher_entries[] = {
    {app_kind::books, "Books"},
    {app_kind::file, "File"},
    {app_kind::menu, "Menu"},
};

inline constexpr std::size_t launcher_layout_entry_count =
    std::size(launcher_entries);

constexpr std::size_t launcher_label_length(const char* label)
{
    std::size_t length = 0U;
    while (label[length] != '\0') {
        ++length;
    }
    return length;
}

constexpr bool launcher_layout_has_unique_targets()
{
    for (std::size_t left = 0U; left < launcher_layout_entry_count; ++left) {
        for (std::size_t right = left + 1U;
             right < launcher_layout_entry_count;
             ++right) {
            if (launcher_entries[left].target == launcher_entries[right].target) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool launcher_layout_labels_fit()
{
    for (const launcher_entry_descriptor& entry : launcher_entries) {
        if (launcher_label_length(entry.label) >= launcher_view_label_capacity) {
            return false;
        }
    }
    return true;
}

static_assert(launcher_layout_entry_count == launcher_view_entry_capacity);
static_assert(launcher_entries[0].target == app_kind::books);
static_assert(launcher_entries[1].target == app_kind::file);
static_assert(launcher_entries[2].target == app_kind::menu);
static_assert(launcher_layout_has_unique_targets());
static_assert(launcher_layout_labels_fit());
