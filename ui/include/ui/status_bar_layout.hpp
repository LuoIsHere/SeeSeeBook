#pragma once

#include <cstdio>
#include <cstdint>

#include "status_bar_view.hpp"

struct status_bar_page_layout {
    bool visible;
    char current[8];
    char total[8];
    std::int16_t current_right;
    std::int16_t slash_x;
    std::int16_t total_left;
};

inline status_bar_page_layout make_status_bar_page_layout(
    const status_bar_view_state& state,
    std::int16_t width)
{
    status_bar_page_layout result = {};
    result.slash_x = width / 2;
    result.current_right = result.slash_x - 8;
    result.total_left = result.slash_x + 8;
    result.visible = state.center_kind == status_bar_center_kind::page &&
                     ((state.center_current_page == 0U && state.center_total_pages == 0U) ||
                      (state.center_current_page >= 1U &&
                       state.center_current_page <= state.center_total_pages)) &&
                     state.center_current_page <= 999999U &&
                     state.center_total_pages <= 999999U;
    if (result.visible) {
        std::snprintf(
            result.current,
            sizeof(result.current),
            "%lu",
            static_cast<unsigned long>(state.center_current_page));
        std::snprintf(
            result.total,
            sizeof(result.total),
            "%lu",
            static_cast<unsigned long>(state.center_total_pages));
    }
    return result;
}
