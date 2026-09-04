#include "ui_interaction_router.hpp"

#include <esp_log.h>

#include "layout.hpp"
#include "ui_presentation.hpp"
#include "ui_renderer.hpp"

namespace {

constexpr char log_tag[] = "ui_interaction";
ui_view_id active_view = ui_view_id::menu;
bool rtc_controls_enabled = false;
ui_control_type captured_control = ui_control_type::none;
std::uint8_t captured_index = 0U;

bool control_has_feedback(ui_control_type control)
{
    switch (control) {
        case ui_control_type::navigate_back:
        case ui_control_type::menu_entry:
        case ui_control_type::reader_previous_zone:
        case ui_control_type::reader_menu_zone:
        case ui_control_type::reader_next_zone:
            // The destination or completed Reader page acknowledges the action.
            // Avoid an extra inverse refresh immediately before that frame.
            return false;
        case ui_control_type::front_light:
        case ui_control_type::rtc_key:
        case ui_control_type::file_row:
        case ui_control_type::file_previous_page:
        case ui_control_type::file_next_page:
            return true;
        case ui_control_type::none:
        case ui_control_type::rtc_field:
        case ui_control_type::test_surface:
            return false;
    }
    return false;
}

bool activated_action_replaces_release_feedback(ui_control_type control)
{
    switch (control) {
        case ui_control_type::navigate_back:
        case ui_control_type::menu_entry:
        case ui_control_type::front_light:
        case ui_control_type::rtc_key:
        case ui_control_type::file_row:
        case ui_control_type::file_previous_page:
        case ui_control_type::file_next_page:
        case ui_control_type::reader_previous_zone:
        case ui_control_type::reader_menu_zone:
        case ui_control_type::reader_next_zone:
            return true;
        case ui_control_type::none:
        case ui_control_type::rtc_field:
        case ui_control_type::test_surface:
            return false;
    }
    return false;
}

bool hit_test(
    std::int16_t x,
    std::int16_t y,
    const menu_view_state* menu_view,
    const file_view_state* file_view,
    const reader_view_state* reader_view,
    ui_control_type& control,
    std::uint8_t& index)
{
    control = ui_control_type::none;
    index = 0U;
    if (active_view == ui_view_id::menu) {
        if (menu_view == nullptr) {
            return false;
        }
        for (std::uint8_t entry = 0U;
             entry < menu_view->entry_count &&
             entry < menu_view_entry_capacity;
             ++entry) {
            if (point_in_rect(x, y, menu_entry_rect(entry))) {
                control = ui_control_type::menu_entry;
                index = entry;
                return true;
            }
        }
        return false;
    }

    if (active_view == ui_view_id::reader) {
        if (reader_view == nullptr || !point_in_rect(x, y, reader_content_rect())) {
            return false;
        }
        if (reader_view->menu_visible && point_in_rect(x, y, reader_menu_rect())) {
            if (point_in_rect(x, y, reader_menu_item_rect(0U))) {
                control = ui_control_type::navigate_back;
                return true;
            }
            return false; // The entire overlay consumes input; no click-through.
        }
        if (point_in_rect(x, y, reader_touch_zone_rect(1U))) {
            control = ui_control_type::reader_menu_zone;
            return true;
        }
        if (!reader_view->menu_visible && reader_view->status == reader_view_status::ready) {
            if (reader_view->previous_enabled && point_in_rect(x, y, reader_touch_zone_rect(0U))) {
                control = ui_control_type::reader_previous_zone;
                return true;
            }
            if (reader_view->next_enabled && point_in_rect(x, y, reader_touch_zone_rect(2U))) {
                control = ui_control_type::reader_next_zone;
                return true;
            }
        }
        return false;
    }
    if (point_in_rect(x, y, app_back_button_rect(active_view))) {
        control = ui_control_type::navigate_back;
        return true;
    }
    if (active_view == ui_view_id::test) {
        for (std::uint8_t level = 0U; level < FRONT_LIGHT_LEVEL_COUNT; ++level) {
            if (point_in_rect(x, y, front_light_button_rect(level))) {
                control = ui_control_type::front_light;
                index = level;
                return true;
            }
        }
        const display_rect content = {
            0, TEST_CONTENT_REGION_TOP, UI_DISPLAY_WIDTH, TEST_CONTENT_REGION_HEIGHT,
        };
        if (point_in_rect(x, y, content)) {
            control = ui_control_type::test_surface;
            return true;
        }
        return false;
    }
    if (active_view == ui_view_id::rtc_setting) {
        if (!rtc_controls_enabled) {
            return false;
        }
        constexpr rtc_edit_field fields[] = {
            rtc_edit_field::year, rtc_edit_field::month, rtc_edit_field::day,
            rtc_edit_field::hour, rtc_edit_field::minute, rtc_edit_field::second,
        };
        for (const rtc_edit_field field : fields) {
            if (point_in_rect(x, y, rtc_field_rect(field))) {
                control = ui_control_type::rtc_field;
                index = static_cast<std::uint8_t>(field);
                return true;
            }
        }
        for (std::uint8_t key = 0U; key < RTC_KEY_COUNT; ++key) {
            if (point_in_rect(x, y, rtc_key_rect(key))) {
                control = ui_control_type::rtc_key;
                index = key;
                return true;
            }
        }
        return false;
    }
    if (active_view != ui_view_id::file) {
        return false;
    }
    if (file_view != nullptr && file_view->status == file_view_status::ready) {
        for (std::uint8_t row = 0U; row < file_view->row_count; ++row) {
            if (file_view->rows[row].enabled && point_in_rect(x, y, file_row_rect(row))) {
                control = ui_control_type::file_row;
                index = row;
                return true;
            }
        }
        if (file_view->page_index > 0U && point_in_rect(x, y, file_previous_page_rect())) {
            control = ui_control_type::file_previous_page;
            return true;
        }
        if (file_view->page_index + 1U < file_view->page_count &&
            point_in_rect(x, y, file_next_page_rect())) {
            control = ui_control_type::file_next_page;
            return true;
        }
    }
    return false;
}

bool same_captured_control(std::int16_t x, std::int16_t y)
{
    switch (captured_control) {
        case ui_control_type::navigate_back:
            return point_in_rect(x, y, app_back_button_rect(active_view));
        case ui_control_type::menu_entry:
            return captured_index < menu_view_entry_capacity &&
                   point_in_rect(x, y, menu_entry_rect(captured_index));
        case ui_control_type::front_light:
            return captured_index < FRONT_LIGHT_LEVEL_COUNT &&
                   point_in_rect(x, y, front_light_button_rect(captured_index));
        case ui_control_type::rtc_field:
            return captured_index >= static_cast<std::uint8_t>(rtc_edit_field::year) &&
                   captured_index <= static_cast<std::uint8_t>(rtc_edit_field::second) &&
                   point_in_rect(
                       x,
                       y,
                       rtc_field_rect(static_cast<rtc_edit_field>(captured_index)));
        case ui_control_type::rtc_key:
            return captured_index < RTC_KEY_COUNT &&
                   point_in_rect(x, y, rtc_key_rect(captured_index));
        case ui_control_type::file_row:
            return captured_index < FILE_VIEW_ROW_COUNT &&
                   point_in_rect(x, y, file_row_rect(captured_index));
        case ui_control_type::file_previous_page:
            return point_in_rect(x, y, file_previous_page_rect());
        case ui_control_type::file_next_page:
            return point_in_rect(x, y, file_next_page_rect());
        case ui_control_type::reader_previous_zone:
            return point_in_rect(x, y, reader_touch_zone_rect(0U));
        case ui_control_type::reader_menu_zone:
            return point_in_rect(x, y, reader_touch_zone_rect(1U));
        case ui_control_type::reader_next_zone:
            return point_in_rect(x, y, reader_touch_zone_rect(2U));
        case ui_control_type::test_surface: {
            const display_rect content = {
                0, TEST_CONTENT_REGION_TOP, UI_DISPLAY_WIDTH, TEST_CONTENT_REGION_HEIGHT,
            };
            return point_in_rect(x, y, content);
        }
        case ui_control_type::none:
            return false;
    }
    return false;
}

}  // namespace

void ui_interaction_set_view(ui_view_id view)
{
    if (control_has_feedback(captured_control)) {
        ui_render_control(captured_control, captured_index, false);
    }
    active_view = view;
    ui_presentation_select_view(view);
    captured_control = ui_control_type::none;
    captured_index = 0U;
}

bool ui_interaction_process(const input_event& input, ui_action_event& action)
{
    action = {};
    if (captured_control == ui_control_type::none &&
        !ui_presentation_input_ready(active_view)) {
        if (input.gesture == input_gesture_type::press) {
            ESP_LOGI(
                log_tag,
                "press ignored while view=%u presentation is pending",
                static_cast<unsigned>(active_view));
        }
        return false;
    }
    if (input.gesture == input_gesture_type::press) {
        ui_presentation_read_guard presented(active_view);
        const bool requires_view_state = active_view == ui_view_id::menu ||
                                         active_view == ui_view_id::file ||
                                         active_view == ui_view_id::reader;
        if (requires_view_state && !presented.valid()) {
            return false;
        }
        rtc_controls_enabled =
            ui_presentation_rtc_controls_enabled();
        if (!hit_test(
                input.start_x,
                input.start_y,
                presented.menu_view(),
                presented.file_view(),
                presented.reader_view(),
                captured_control,
                captured_index)) {
            return false;
        }
        if (control_has_feedback(captured_control)) {
            ui_render_control(captured_control, captured_index, true);
        }
        if (active_view == ui_view_id::reader) {
            return false; // Capture only; Reader emits one semantic action on click.
        }
        action = {captured_control, captured_index, input};
        return true;
    }

    if (captured_control == ui_control_type::none) {
        return false;
    }
    const ui_control_type control = captured_control;
    const std::uint8_t index = captured_index;
    if (active_view == ui_view_id::reader) {
        // InputService reports short drags as click. Reject movement and all
        // long-press phases here, without changing other applications' gestures.
        captured_control = ui_control_type::none;
        const int dx = input.end_x - input.start_x;
        const int dy = input.end_y - input.start_y;
        if (input.gesture != input_gesture_type::click ||
            dx * dx + dy * dy > READER_TAP_SLOP * READER_TAP_SLOP ||
            !ui_presentation_input_ready(active_view)) {
            return false;
        }
        ui_presentation_read_guard presented(active_view);
        ui_control_type current = ui_control_type::none;
        std::uint8_t current_index = 0U;
        if (!hit_test(input.end_x, input.end_y, nullptr, nullptr,
                      presented.reader_view(), current, current_index) ||
            current != control || current_index != index) {
            return false;
        }
        action = {control, index, input};
        return true;
    }
    if (input.gesture == input_gesture_type::click) {
        const bool activated = same_captured_control(input.end_x, input.end_y);
        // Activated controls are restored by the resulting App frame or view switch.
        if (control_has_feedback(control) &&
            (!activated || !activated_action_replaces_release_feedback(control))) {
            ui_render_control(control, index, false);
        }
        captured_control = ui_control_type::none;
        if (!activated) {
            return false;
        }
        action = {control, index, input};
        return true;
    }

    if (input.gesture == input_gesture_type::long_press_end) {
        if (control_has_feedback(control)) {
            ui_render_control(control, index, false);
        }
        captured_control = ui_control_type::none;
        action = {control, index, input};
        return true;
    }
    action = {control, index, input};
    return true;
}
