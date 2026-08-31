#include "ui_interaction_router.hpp"

#include <esp_log.h>

#include "layout.hpp"
#include "ui_presentation.hpp"
#include "ui_renderer.hpp"

namespace {

constexpr char log_tag[] = "ui_interaction";
ui_view_id active_view = ui_view_id::menu;
file_view_state active_file_view = {};
bool rtc_controls_enabled = false;
ui_control_type captured_control = ui_control_type::none;
std::uint8_t captured_index = 0U;

bool control_has_feedback(ui_control_type control)
{
    switch (control) {
        case ui_control_type::navigate_back:
        case ui_control_type::menu_entry:
            // Navigation is acknowledged by the destination Quality frame.
            // Avoid a short inverse refresh immediately before that frame.
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
    ui_control_type& control,
    std::uint8_t& index)
{
    control = ui_control_type::none;
    index = 0U;
    if (active_view == ui_view_id::menu) {
        for (std::uint8_t entry = 0U; entry < MENU_ENTRY_COUNT; ++entry) {
            if (point_in_rect(x, y, menu_entry_rect(entry))) {
                control = ui_control_type::menu_entry;
                index = entry;
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
    if (active_file_view.status == file_view_status::ready) {
        for (std::uint8_t row = 0U; row < active_file_view.row_count; ++row) {
            if (active_file_view.rows[row].enabled && point_in_rect(x, y, file_row_rect(row))) {
                control = ui_control_type::file_row;
                index = row;
                return true;
            }
        }
        if (active_file_view.page_index > 0U && point_in_rect(x, y, file_previous_page_rect())) {
            control = ui_control_type::file_previous_page;
            return true;
        }
        if (active_file_view.page_index + 1U < active_file_view.page_count &&
            point_in_rect(x, y, file_next_page_rect())) {
            control = ui_control_type::file_next_page;
            return true;
        }
    }
    return false;
}

bool same_captured_control(std::int16_t x, std::int16_t y)
{
    ui_control_type control = ui_control_type::none;
    std::uint8_t index = 0U;
    return hit_test(x, y, control, index) && control == captured_control &&
           index == captured_index;
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
        if (active_view == ui_view_id::file &&
            !ui_presentation_get_file_view(active_file_view)) {
            return false;
        }
        rtc_controls_enabled =
            ui_presentation_rtc_controls_enabled();
        if (!hit_test(input.start_x, input.start_y, captured_control, captured_index)) {
            return false;
        }
        if (control_has_feedback(captured_control)) {
            ui_render_control(captured_control, captured_index, true);
        }
        action = {captured_control, captured_index, input};
        return true;
    }

    if (captured_control == ui_control_type::none) {
        return false;
    }
    const ui_control_type control = captured_control;
    const std::uint8_t index = captured_index;
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
