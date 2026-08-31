#include "ui_presentation.hpp"

#include <freertos/FreeRTOS.h>

namespace {

portMUX_TYPE presentation_lock = portMUX_INITIALIZER_UNLOCKED;
std::uint32_t next_generation = 0U;
ui_view_id expected_view = ui_view_id::menu;
std::uint32_t expected_generation = 0U;
ui_view_id presented_view = ui_view_id::menu;
std::uint32_t presented_generation = 0U;
file_view_state presented_file_view = {};
bool presented_rtc_controls_enabled = false;

std::uint32_t allocate_generation_locked()
{
    ++next_generation;
    if (next_generation == 0U) {
        ++next_generation;
    }
    return next_generation;
}

}  // namespace

std::uint32_t ui_presentation_select_view(ui_view_id view)
{
    portENTER_CRITICAL(&presentation_lock);
    const std::uint32_t generation = allocate_generation_locked();
    expected_view = view;
    expected_generation = generation;
    portEXIT_CRITICAL(&presentation_lock);
    return generation;
}

std::uint32_t ui_presentation_prepare_frame(
    ui_view_id view,
    bool view_opened)
{
    portENTER_CRITICAL(&presentation_lock);
    const std::uint32_t generation =
        view_opened && expected_view == view && expected_generation != 0U
            ? expected_generation
            : allocate_generation_locked();
    expected_view = view;
    expected_generation = generation;
    portEXIT_CRITICAL(&presentation_lock);
    return generation;
}

void ui_presentation_commit_frame(
    ui_view_id view,
    std::uint32_t generation,
    const file_view_state* file,
    bool rtc_controls_enabled)
{
    portENTER_CRITICAL(&presentation_lock);
    presented_view = view;
    presented_generation = generation;
    if (view == ui_view_id::file && file != nullptr) {
        presented_file_view = *file;
    }
    if (view == ui_view_id::rtc_setting) {
        presented_rtc_controls_enabled = rtc_controls_enabled;
    }
    portEXIT_CRITICAL(&presentation_lock);
}

bool ui_presentation_input_ready(ui_view_id view)
{
    portENTER_CRITICAL(&presentation_lock);
    const bool ready = expected_view == view && presented_view == view &&
                       expected_generation != 0U &&
                       expected_generation == presented_generation;
    portEXIT_CRITICAL(&presentation_lock);
    return ready;
}

bool ui_presentation_get_file_view(file_view_state& state)
{
    portENTER_CRITICAL(&presentation_lock);
    const bool available = presented_view == ui_view_id::file &&
                           presented_generation != 0U;
    if (available) {
        state = presented_file_view;
    }
    portEXIT_CRITICAL(&presentation_lock);
    return available;
}

bool ui_presentation_rtc_controls_enabled()
{
    portENTER_CRITICAL(&presentation_lock);
    const bool enabled = presented_view == ui_view_id::rtc_setting &&
                         presented_generation != 0U &&
                         presented_rtc_controls_enabled;
    portEXIT_CRITICAL(&presentation_lock);
    return enabled;
}
