#pragma once

#include <cstdint>
#include <type_traits>

#include "input_event.hpp"

enum class ui_view_id : std::uint8_t {
    menu,
    test,
    rtc_setting,
    battery,
    file,
    reader,
};

enum class ui_control_type : std::uint8_t {
    none,
    navigate_back,
    menu_entry,
    front_light,
    rtc_key,
    rtc_field,
    file_row,
    file_previous_page,
    file_next_page,
    reader_previous_page,
    reader_next_page,
    test_surface,
};

struct ui_action_event {
    ui_control_type control;
    std::uint8_t index;
    input_event input;
};

static_assert(std::is_trivially_copyable_v<ui_action_event>);
