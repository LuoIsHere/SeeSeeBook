#include "books_interaction.hpp"

#include "books_layout.hpp"
#include "geometry.hpp"

bool books_hit_test(
    const books_view_state& state,
    std::int16_t x,
    std::int16_t y,
    ui_control_type& control,
    std::uint8_t& index)
{
    control = ui_control_type::none;
    index = 0U;
    if (state.settings_visible) {
        if (point_in_rect(x, y, books_setting_row_rect(false))) {
            control = ui_control_type::books_setting_toggle_txt;
            return true;
        }
        if (point_in_rect(x, y, books_setting_row_rect(true))) {
            control = ui_control_type::books_setting_toggle_epub;
            return true;
        }
        if (point_in_rect(x, y, books_setting_confirm_rect())) {
            control = ui_control_type::books_setting_confirm;
            return true;
        }
        if (point_in_rect(x, y, books_setting_cancel_rect())) {
            control = ui_control_type::books_setting_cancel;
            return true;
        }
        return false;
    }
    if (point_in_rect(x, y, books_back_rect())) {
        control = ui_control_type::books_back;
        return true;
    }
    if (point_in_rect(x, y, books_settings_rect())) {
        control = ui_control_type::books_settings;
        return true;
    }
    const std::uint8_t count = state.item_count < books_view_item_capacity
                                   ? state.item_count
                                   : books_view_item_capacity;
    for (std::uint8_t item = 0U; item < count; ++item) {
        if (state.items[item].occupied && state.items[item].enabled &&
            point_in_rect(x, y, books_item_rect(item))) {
            control = ui_control_type::books_select_item;
            index = item;
            return true;
        }
    }
    if (state.page_index > 0U &&
        point_in_rect(x, y, books_previous_page_rect())) {
        control = ui_control_type::books_page_previous;
        return true;
    }
    if (state.page_index + 1U < state.page_count &&
        point_in_rect(x, y, books_next_page_rect())) {
        control = ui_control_type::books_page_next;
        return true;
    }
    return false;
}

bool books_control_contains(
    ui_control_type control,
    std::uint8_t index,
    std::int16_t x,
    std::int16_t y)
{
    switch (control) {
        case ui_control_type::books_back:
            return point_in_rect(x, y, books_back_rect());
        case ui_control_type::books_settings:
            return point_in_rect(x, y, books_settings_rect());
        case ui_control_type::books_select_item:
            return index < books_view_item_capacity &&
                   point_in_rect(x, y, books_item_rect(index));
        case ui_control_type::books_page_previous:
            return point_in_rect(x, y, books_previous_page_rect());
        case ui_control_type::books_page_next:
            return point_in_rect(x, y, books_next_page_rect());
        case ui_control_type::books_setting_toggle_txt:
            return point_in_rect(x, y, books_setting_row_rect(false));
        case ui_control_type::books_setting_toggle_epub:
            return point_in_rect(x, y, books_setting_row_rect(true));
        case ui_control_type::books_setting_confirm:
            return point_in_rect(x, y, books_setting_confirm_rect());
        case ui_control_type::books_setting_cancel:
            return point_in_rect(x, y, books_setting_cancel_rect());
        default:
            return false;
    }
}
