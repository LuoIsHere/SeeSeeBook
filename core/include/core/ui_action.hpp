#pragma once

#include <cstdint>
#include <type_traits>

#include "input_event.hpp"

enum class ui_view_id : std::uint8_t {
    launcher,
    books,
    menu,
    test,
    rtc_setting,
    battery,
    file,
    reader,
    gray4_test,
};

enum class ui_control_type : std::uint8_t {
    none,
    navigate_back,
    launcher_entry,
    menu_entry,
    books_back,
    books_settings,
    books_select_item,
    books_page_previous,
    books_page_next,
    books_setting_toggle_txt,
    books_setting_toggle_epub,
    books_setting_confirm,
    books_setting_cancel,
    front_light,
    rtc_key,
    rtc_field,
    file_row,
    file_previous_page,
    file_next_page,
    reader_previous_zone,
    reader_menu_zone,
    reader_next_zone,
    test_surface,
};

struct ui_action_event {
    ui_control_type control;
    std::uint8_t index;
    input_event input;
};

static_assert(std::is_trivially_copyable_v<ui_action_event>);
