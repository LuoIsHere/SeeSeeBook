#pragma once

#include <cstdio>

#include "status_bar_view.hpp"

struct reader_status_layout {
    bool visible;
    char current[7];
    char total[7];
    std::int16_t slash_x;
    std::int16_t current_right;
    std::int16_t total_left;
};

inline reader_status_layout make_reader_status_layout(const status_bar_view_state& state, std::int16_t width)
{
    reader_status_layout result = {};
    result.slash_x = width / 2;
    result.current_right = result.slash_x - 8;
    result.total_left = result.slash_x + 8;
    result.visible = state.foreground_app == ui_view_id::reader && state.reader_page_valid &&
                     state.current_page >= 1U && state.current_page <= state.total_pages &&
                     state.current_page <= 999999U && state.total_pages <= 999999U;
    if (result.visible) {
        std::snprintf(result.current, sizeof(result.current), "%lu", static_cast<unsigned long>(state.current_page));
        std::snprintf(result.total, sizeof(result.total), "%lu", static_cast<unsigned long>(state.total_pages));
    }
    return result;
}
